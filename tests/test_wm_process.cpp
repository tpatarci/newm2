// Process-level tests against the REAL compiled wm2-born-again binary (D-07).
//
// Unlike test_client / test_ewmh, which exercise raw Xlib primitives on the
// shared :99 fixture, every case here runs a live WM child on a display the
// fixture reserved for itself (Pitfall 3 -- a running WM owns
// SubstructureRedirectMask exclusively and would reparent those tests' windows).
//
// Tag: [wm_process] -- registered as a ctest LABEL via ADD_TAGS_AS_LABELS (D-33),
// selected with `ctest -L '^wm_process$' --no-tests=error`.

#include <catch2/catch_test_macros.hpp>

#include "support/WmFixture.h"
#include "support/XTestDriver.h"

#include "x11wrap.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

using namespace wm2test;

namespace {

constexpr int kScreenWidth = 1024;
constexpr int kScreenHeight = 768;
constexpr int kDockHeight = 40;

// Create an undecorated dock window carrying _NET_WM_WINDOW_TYPE_DOCK and a
// bottom strut. Properties are set BEFORE the map, because Client::getWindowType()
// runs during MapRequest handling (src/Client.cpp:115).
Window createDockWindow(Display* d, int strutBottom)
{
    Window root = DefaultRootWindow(d);
    Window w = XCreateSimpleWindow(d, root,
                                   0, kScreenHeight - kDockHeight,
                                   kScreenWidth, kDockHeight,
                                   0, BlackPixel(d, DefaultScreen(d)),
                                   WhitePixel(d, DefaultScreen(d)));

    Atom windowType = XInternAtom(d, "_NET_WM_WINDOW_TYPE", False);
    Atom dockType = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DOCK", False);
    XChangeProperty(d, w, windowType, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&dockType), 1);

    // _NET_WM_STRUT_PARTIAL: left, right, top, bottom, then 8 start/end values.
    Atom strutPartial = XInternAtom(d, "_NET_WM_STRUT_PARTIAL", False);
    long struts[12] = {0, 0, 0, static_cast<long>(strutBottom),
                       0, 0, 0, 0, 0, 0, 0, kScreenWidth - 1};
    XChangeProperty(d, w, strutPartial, XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(struts), 12);

    XMapWindow(d, w);
    XSync(d, False);
    return w;
}

// Compute a root-window coordinate that lands on the sideways tab.
//
// Derived from live geometry, never hardcoded. The tab WINDOW (Border::m_tab,
// src/Border.cpp:722) is a bounding box spanning most of the frame's top strip
// -- on a 246x169 frame it measures 245x54 -- and the Shape extension carves the
// actual sideways tab out of its left column. So "find the tall narrow child"
// does not work: the clickable region is a shaped subset, not the window rect.
//
// What IS reliable is the client's own offset inside the frame. Border::xIndent()
// returns `m_tabWidth + FRAME_WIDTH + 1` (include/Border.h:54), so the client's
// x offset within the frame is exactly the width of the tab column. Clicking at
// half that offset puts the pointer inside the tab, and dropping below the small
// square button at +4+4 avoids the hide/destroy control.
bool tabClickPoint(Display* d, Window frame, Window client, int& rootX, int& rootY)
{
    // Client position relative to its frame == the tab column width / top inset.
    int indentX = 0, indentY = 0;
    Window dummy = None;
    if (!XTranslateCoordinates(d, client, frame, 0, 0, &indentX, &indentY, &dummy)) {
        return false;
    }
    if (indentX <= 2) return false;   // no tab column yet (not reparented/shaped)

    // Inside the tab column horizontally; below the button vertically but still
    // within the tab's shaped height (m_tabHeight + m_tabWidth).
    const int localX = indentX / 2;
    const int localY = indentY + 20;

    int rx = 0, ry = 0;
    if (!XTranslateCoordinates(d, frame, DefaultRootWindow(d),
                               localX, localY, &rx, &ry, &dummy)) {
        return false;
    }
    rootX = rx;
    rootY = ry;
    return true;
}

bool workareaEquals(Display* d, Atom workarea,
                    unsigned long x, unsigned long y,
                    unsigned long w, unsigned long h)
{
    std::vector<unsigned long> vals;
    if (!readCardinals(d, DefaultRootWindow(d), workarea, vals)) return false;
    if (vals.size() < 4) return false;
    return vals[0] == x && vals[1] == y && vals[2] == w && vals[3] == h;
}

// Wake the WM's event loop so it flushes its X output buffer (deferred item 9).
//
// Same inert nudge tests/test_wm_geometry.cpp uses and for the same reason: the
// WM issues requests while handling an event and then blocks again, and those
// requests are not observable until its loop wakes for a LATER event. The nudge
// is InputOnly and override-redirect, so WindowManager::eventCreate returns
// immediately for it (src/Events.cpp) -- the WM never manages it, it never
// enters _NET_CLIENT_LIST, and it cannot perturb the circulate order the case
// below depends on, while its CreateNotify/DestroyNotify still reach the WM
// through root's substructure selection.
//
// A harness workaround, not a convention to copy.
void pumpWm(Display* d)
{
    XSetWindowAttributes attr;
    attr.override_redirect = True;
    Window nudge = XCreateWindow(d, DefaultRootWindow(d), -20, -20, 1, 1, 0,
                                 CopyFromParent, InputOnly, CopyFromParent,
                                 CWOverrideRedirect, &attr);
    XSync(d, False);
    XDestroyWindow(d, nudge);
    XSync(d, False);
}

// Poll until the WM has reparented `w` into a frame. Deadline-bounded: this is
// the responsiveness probe, so it must report a wedged WM as a FAILURE within a
// known time rather than waiting on it forever.
bool awaitFramed(Display* d, Window w, int timeoutMs)
{
    Window root = DefaultRootWindow(d);
    return WmFixture::pollUntil([&] {
        pumpWm(d);
        Window wroot = None, parent = None, *children = nullptr;
        unsigned int n = 0;
        if (!XQueryTree(d, w, &wroot, &parent, &children, &n)) return false;
        if (children) XFree(children);
        return parent != None && parent != root;
    }, timeoutMs);
}

// A managed, mapped, NON-ELIGIBLE client: Normal state but transient, which is
// exactly what WindowManager::circulate()'s break condition rejects
// (`isNormal() && !isTransient()`).
//
// Why a transient rather than simply running with no clients at all: with no
// client mapped, m_clients is non-empty anyway because the WM adopts its own
// menu/submenu/WM-check windows (deferred item 7), and that is what makes the
// spin reachable today. But item 7 is itself scheduled to be fixed, and a test
// whose precondition is another defect would go GREEN-then-VACUOUS the day that
// defect is repaired -- it would stop exercising the loop and nobody would
// notice. A transient the test creates itself keeps m_clients non-empty and
// eligible-free under the test's own control, whichever way item 7 goes.
//
// WM_TRANSIENT_FOR is set BEFORE the map, because Client::getTransient() runs
// during MapRequest handling.
Window createTransientWindow(Display* d, int x, int y, int w, int h)
{
    Window root = DefaultRootWindow(d);
    Window win = XCreateSimpleWindow(d, root, x, y,
                                     static_cast<unsigned>(w), static_cast<unsigned>(h),
                                     0, BlackPixel(d, DefaultScreen(d)),
                                     WhitePixel(d, DefaultScreen(d)));
    // Transient for root: a window the WM does not manage, so the hint is
    // honoured (Client::getTransient() only rejects a SELF-referencing hint)
    // and no second client is created as a side effect.
    XSetTransientForHint(d, win, root);
    XMapWindow(d, win);
    XSync(d, False);
    return win;
}

// Synthesise the root Button3 that reaches WindowManager::circulate(true).
//
// XSendEvent rather than XTEST, on purpose and safely:
// WindowManager::eventButton() (src/Buttons.cpp) applies NO send_event guard on
// its root branch -- unlike Client::eventButton(), which explicitly ignores
// synthetic events -- so the branch under test is entered identically to how the
// server would enter it. Same justification tests/test_wm_geometry.cpp records
// for its clamp trigger. XTEST would also work here but would additionally
// depend on there being a root pixel under the pointer.
void sendRootButton3(Display* d)
{
    Window root = DefaultRootWindow(d);

    XEvent ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.type = ButtonPress;
    ev.xbutton.display = d;
    ev.xbutton.window = root;      // eventButton() takes the root branch on
    ev.xbutton.root = root;        // window == root, and only then
    ev.xbutton.subwindow = None;
    ev.xbutton.button = Button3;   // Button3 on root == circulate(activeFirst)
    ev.xbutton.same_screen = True;
    ev.xbutton.time = CurrentTime;

    XSendEvent(d, root, False, ButtonPressMask, &ev);
    XSync(d, False);
}

} // namespace

// ---------------------------------------------------------------------------
// Behavior 1 (the tracer): the fixture brings the real binary up on its own display
// ---------------------------------------------------------------------------

TEST_CASE("WmFixture starts the real WM and it publishes _NET_SUPPORTING_WM_CHECK",
          "[wm_process]")
{
    WmFixture fixture;

    // The fixture owns its display -- never the process DISPLAY env var.
    REQUIRE_FALSE(fixture.display().empty());
    REQUIRE(fixture.display() != ":99");

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    Atom check = XInternAtom(d.get(), "_NET_SUPPORTING_WM_CHECK", False);
    REQUIRE(check != None);

    Window root = DefaultRootWindow(d.get());

    Atom actualType = None;
    int actualFormat = 0;
    unsigned long nItems = 0, bytesAfter = 0;
    Window* data = nullptr;
    int status = XGetWindowProperty(d.get(), root, check, 0, 1, False, XA_WINDOW,
                                    &actualType, &actualFormat, &nItems, &bytesAfter,
                                    reinterpret_cast<unsigned char**>(&data));
    REQUIRE(status == Success);
    REQUIRE(nItems == 1);
    REQUIRE(data != nullptr);
    Window checkWindow = data[0];
    XFree(data);
    REQUIRE(checkWindow != None);

    // Per EWMH the check window points at itself (src/Manager.cpp:462-465).
    Window selfRef = None;
    REQUIRE(readWindowProp(d.get(), checkWindow, check, selfRef));
    REQUIRE(selfRef == checkWindow);

    REQUIRE(fixture.wmAlive());
}

// ---------------------------------------------------------------------------
// Behavior 2: a dock window's strut shrinks _NET_WORKAREA
// ---------------------------------------------------------------------------

TEST_CASE("A dock window with _NET_WM_STRUT_PARTIAL shrinks _NET_WORKAREA",
          "[wm_process]")
{
    WmFixture fixture;

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    Atom workarea = XInternAtom(d.get(), "_NET_WORKAREA", False);
    REQUIRE(workarea != None);

    // Baseline: full screen before any dock exists.
    REQUIRE(WmFixture::pollUntil([&] {
        return workareaEquals(d.get(), workarea, 0, 0, kScreenWidth, kScreenHeight);
    }, 5000));

    Window dock = createDockWindow(d.get(), kDockHeight);
    REQUIRE(dock != None);

    // The WM must notice the dock and subtract its strut.
    REQUIRE(WmFixture::pollUntil([&] {
        return workareaEquals(d.get(), workarea, 0, 0,
                              kScreenWidth, kScreenHeight - kDockHeight);
    }, 8000));

    REQUIRE(fixture.wmAlive());

    XDestroyWindow(d.get(), dock);
    XSync(d.get(), False);
}

// ---------------------------------------------------------------------------
// Behavior 3: destroying the dock restores the workarea, with no ASan report
//
// This is the proof for the eventDestroy use-after-free (SCAN-02 /
// COMPILED_CODE_BEHAVIOR_CHECKLIST.md "Current Scan Findings", threat T-8-UAF).
// Under the build/asan tree the pre-fix code reads freed memory here; the child
// would write an ASan report and exit with the sentinel exitcode 42.
// ---------------------------------------------------------------------------

TEST_CASE("Destroying a dock window restores _NET_WORKAREA with no sanitizer report",
          "[wm_process]")
{
    WmFixture fixture;

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    Atom workarea = XInternAtom(d.get(), "_NET_WORKAREA", False);
    REQUIRE(workarea != None);

    Window dock = createDockWindow(d.get(), kDockHeight);
    REQUIRE(dock != None);

    REQUIRE(WmFixture::pollUntil([&] {
        return workareaEquals(d.get(), workarea, 0, 0,
                              kScreenWidth, kScreenHeight - kDockHeight);
    }, 8000));

    // The use-after-free trigger: erase the owning unique_ptr, then recompute.
    XDestroyWindow(d.get(), dock);
    XSync(d.get(), False);

    // Workarea recomputed back to the full screen.
    REQUIRE(WmFixture::pollUntil([&] {
        return workareaEquals(d.get(), workarea, 0, 0, kScreenWidth, kScreenHeight);
    }, 8000));

    // The WM survived and is still serving requests.
    REQUIRE(fixture.wmAlive());
    Window checkWindow = None;
    Atom check = XInternAtom(d.get(), "_NET_SUPPORTING_WM_CHECK", False);
    REQUIRE(readWindowProp(d.get(), DefaultRootWindow(d.get()), check, checkWindow));
    REQUIRE(checkWindow != None);

    // Pitfall 4: a sanitizer report inside the child must fail the test rather
    // than being swallowed. Zero report files means zero findings.
    const std::vector<std::string> reports = fixture.asanReports();
    INFO("ASan log prefix: " << fixture.asanLogPrefix());
    for (const std::string& r : reports) INFO("ASan report: " << r);
    REQUIRE(reports.empty());
}

// ---------------------------------------------------------------------------
// Behavior 4: SIGTERM produces a clean exit
// ---------------------------------------------------------------------------

TEST_CASE("SIGTERM shuts the WM down cleanly with exit status 0", "[wm_process]")
{
    WmFixture fixture;
    REQUIRE(fixture.wmAlive());

    // Exercises the self-pipe signal path (src/Events.cpp:130, src/Manager.cpp:269).
    REQUIRE(fixture.terminateWmCleanly());

    // Explicitly NOT the ASan exitcode sentinel and NOT a signal death.
    REQUIRE(fixture.wm().exitedNormally());
    REQUIRE(fixture.wm().exitCode() == 0);
    REQUIRE(fixture.wm().exitCode() != 42);
    REQUIRE(fixture.wm().termSignal() == 0);

    INFO("wm stderr: " << fixture.wmStderr());
    REQUIRE(fixture.asanReports().empty());
}

// ---------------------------------------------------------------------------
// Behavior 5 (Task 3 expansion): synthesised input actually drives the real WM
//
// Proves the XTEST driver reaches the WM's real input path rather than emitting
// events into the void. XSendEvent could not prove this at all: Client::eventButton
// returns early on `e->send_event` (src/Client.cpp:1377), so a synthetic event
// would never reach activate(). XTEST events carry send_event = False and
// therefore exercise the genuine path.
//
// SCOPE, stated precisely: this asserts that synthesised input AS A WHOLE
// (warp + press + release) drives the WM. It does NOT isolate the click from the
// warp, because today's WM activates on pointer entry -- the three focus
// booleans are parsed but unwired (D-15), so click-to-focus does not yet exist
// as a distinct behavior. Verified by removing the XTEST calls: the test fails.
// Verified by removing only the click: it still passes, via focus-follows-pointer.
// Once FOCUS-02 wires click-to-focus, a later plan should split these.
// ---------------------------------------------------------------------------

TEST_CASE("Synthesised XTEST input on a client's tab activates that window",
          "[wm_process]")
{
    WmFixture fixture;

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    Atom activeWindow = XInternAtom(d.get(), "_NET_ACTIVE_WINDOW", False);
    REQUIRE(activeWindow != None);

    Window root = DefaultRootWindow(d.get());

    // Two normal clients, so "the right one got activated" is a real assertion
    // rather than something a single-window run would satisfy by accident.
    Window first = XCreateSimpleWindow(d.get(), root, 40, 40, 200, 150, 0,
                                       BlackPixel(d.get(), DefaultScreen(d.get())),
                                       WhitePixel(d.get(), DefaultScreen(d.get())));
    Window target = XCreateSimpleWindow(d.get(), root, 320, 200, 220, 160, 0,
                                        BlackPixel(d.get(), DefaultScreen(d.get())),
                                        WhitePixel(d.get(), DefaultScreen(d.get())));
    XMapWindow(d.get(), first);
    XMapWindow(d.get(), target);
    XSync(d.get(), False);

    // Wait for the WM to frame the target (reparent away from root).
    Window frame = None;
    REQUIRE(WmFixture::pollUntil([&] {
        Window wroot = None, parent = None, *children = nullptr;
        unsigned int n = 0;
        if (!XQueryTree(d.get(), target, &wroot, &parent, &children, &n)) return false;
        if (children) XFree(children);
        if (parent == None || parent == root) return false;
        frame = parent;
        return true;
    }, 8000));
    REQUIRE(frame != None);

    // Dedicated input connection with grab control enabled (Pitfall 10).
    XTestDriver driver(fixture.display());

    // Park the pointer in a corner clear of BOTH frames before doing anything
    // else. This WM runs focus-follows-pointer with auto-raise, so a pointer
    // resting over either window would race the activation below and flip
    // _NET_ACTIVE_WINDOW back underneath the test.
    driver.moveTo(5, 5);

    // Precondition: with the pointer parked on root and no click yet, this WM
    // (focus-follows-pointer, activation on click/enter) has activated nothing.
    // Assert that explicitly so the post-click assertion cannot pass vacuously.
    Window preClickActive = None;
    const bool preRead = readWindowProp(d.get(), root, activeWindow, preClickActive);
    INFO("first=" << first << " target=" << target
         << " pre-click active=" << preClickActive << " (read ok: " << preRead << ")");
    REQUIRE(preClickActive != target);

    int tabX = 0, tabY = 0;
    REQUIRE(WmFixture::pollUntil([&] {
        return tabClickPoint(d.get(), frame, target, tabX, tabY);
    }, 5000));

    driver.moveTo(tabX, tabY);

    // press -> brief wait -> release, NOT click(). A Button1 press on the tab
    // makes the WM take a pointer grab and enter its interactive move loop
    // (Border::eventButton). A release delivered back-to-back can arrive before
    // that grab is established, leaving the WM waiting in the move loop for a
    // release that already happened -- which under parallel load failed roughly
    // 4 runs in 6. The wait is the sanctioned sequencing shape for press/release
    // pairs (08-RESEARCH.md Example 2); it is deliberately NOT passed as the
    // XTEST `delay` argument, which would block the driver connection instead.
    driver.press(Button1);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    driver.release(Button1);

    Window active = None;
    const bool activated = WmFixture::pollUntil([&] {
        return readWindowProp(d.get(), root, activeWindow, active) && active == target;
    }, 8000);

    INFO("target window: " << target << "  frame: " << frame);
    INFO("clicked at root coords: " << tabX << "," << tabY);
    INFO("_NET_ACTIVE_WINDOW: " << active);
    INFO("wm stderr: " << fixture.wmStderr());
    REQUIRE(activated);

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());

    XDestroyWindow(d.get(), target);
    XDestroyWindow(d.get(), first);
    XSync(d.get(), False);
}

// ---------------------------------------------------------------------------
// Behavior 6 (deferred item 6): a root right-click with no eligible client must
// not wedge the WM
//
// The defect, pre-existing since long before Phase 8 and measured in plan 08-04
// at 299 CPU ticks over 3 seconds of wall clock: WindowManager::circulate()
// scanned m_clients with a loop whose only non-break exit was `j == i`. When
// there is no active client `i` is -1, and `j` -- being an index wrapped into
// [0, size) -- can never equal -1. So whenever no entry satisfied the break
// condition the loop was unbounded, and the WM froze at 100% CPU permanently.
// A right-click on the root of a freshly started WM was enough to reach it.
//
// TWO assertions, because "alive" and "working" are different claims and the
// bug satisfies the first one:
//
//   * RESPONSIVENESS -- a window mapped after the click must still be framed.
//     A wedged WM never leaves circulate(), so it never processes another
//     MapRequest. This is deadline-bounded (6s) and therefore FAILS rather than
//     hanging the suite; the fixture's own teardown escalates SIGTERM to
//     SIGKILL, so a wedged child cannot outlive the case either. A spinning WM
//     does not answer SIGTERM at all -- the handler only sets a flag the event
//     loop never gets back to checking.
//
//   * IDLENESS -- the WM's CPU time over a quiet interval must be effectively
//     unchanged. This is what separates "blocked in poll()" from "burning a
//     core", which are indistinguishable from the outside otherwise. It is also
//     the assertion that would catch a "fix" that terminated the loop by
//     turning it into a busy-wait somewhere else.
//
// The precondition -- clients present, none of them eligible -- is established
// by the test itself with a transient window rather than inherited from another
// defect; see createTransientWindow() above.
// ---------------------------------------------------------------------------

TEST_CASE("A root right-click with no eligible client leaves the WM responsive and idle",
          "[wm_process][wm_circulate]")
{
    WmFixture fixture;

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    Window root = DefaultRootWindow(d.get());

    // Placed clear of the pointer's start position (screen centre on this
    // server): an EnterNotify would activate it under focus-follows-pointer,
    // m_activeClient would stop being null, and circulate(true) would return on
    // its first line without ever reaching the loop under test.
    Window transientWin = createTransientWindow(d.get(), 40, 40, 200, 150);
    REQUIRE(awaitFramed(d.get(), transientWin, 8000));

    // Precondition 1: the WM has at least one managed client, so the
    // `m_clients.empty()` guard above the loop does not carry the case.
    Atom clientList = XInternAtom(d.get(), "_NET_CLIENT_LIST", False);
    REQUIRE(clientList != None);
    std::vector<unsigned long> managed;
    {
        Atom actualType = None;
        int actualFormat = 0;
        unsigned long nItems = 0, bytesAfter = 0;
        unsigned char* raw = nullptr;
        REQUIRE(XGetWindowProperty(d.get(), root, clientList, 0, 256, False, XA_WINDOW,
                                   &actualType, &actualFormat, &nItems, &bytesAfter,
                                   &raw) == Success);
        for (unsigned long i = 0; raw && i < nItems; ++i) {
            managed.push_back(reinterpret_cast<Window*>(raw)[i]);
        }
        if (raw) XFree(raw);
    }
    INFO("managed clients: " << managed.size());
    REQUIRE(managed.size() >= 1);

    // Precondition 2: nothing is active, so circulate(true) cannot short-circuit
    // on m_activeClient and must run the scan.
    Atom activeWindow = XInternAtom(d.get(), "_NET_ACTIVE_WINDOW", False);
    REQUIRE(activeWindow != None);
    Window active = None;
    readWindowProp(d.get(), root, activeWindow, active);
    INFO("_NET_ACTIVE_WINDOW before the click: " << active);
    REQUIRE(active == None);

    unsigned long long ticksBefore = 0;
    REQUIRE(processCpuTicks(fixture.wm().pid(), ticksBefore));

    sendRootButton3(d.get());

    // --- Assertion 1: still processing events -------------------------------
    Window after = XCreateSimpleWindow(d.get(), root, 320, 400, 220, 160, 0,
                                       BlackPixel(d.get(), DefaultScreen(d.get())),
                                       WhitePixel(d.get(), DefaultScreen(d.get())));
    XMapWindow(d.get(), after);
    XSync(d.get(), False);

    const bool responsive = awaitFramed(d.get(), after, 6000);

    unsigned long long ticksAfter = 0;
    const bool sampled = processCpuTicks(fixture.wm().pid(), ticksAfter);

    INFO("wm stderr: " << fixture.wmStderr());
    INFO("CPU ticks during the responsiveness probe: "
         << (sampled ? static_cast<long long>(ticksAfter - ticksBefore) : -1));
    REQUIRE(responsive);

    // --- Assertion 2: genuinely idle, not spinning --------------------------
    //
    // Sampled over a quiet interval with no X traffic at all. A wedged WM
    // accumulates roughly 100 ticks per second here (measured: 299 over 3s); a
    // healthy one blocked in poll() accumulates none. The bound is set an order
    // of magnitude below the wedged rate so it can never be met by a spin, and
    // an order of magnitude above the healthy cost so it can never flake.
    unsigned long long idleBefore = 0;
    REQUIRE(processCpuTicks(fixture.wm().pid(), idleBefore));
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    unsigned long long idleAfter = 0;
    REQUIRE(processCpuTicks(fixture.wm().pid(), idleAfter));

    const long long idleDelta = static_cast<long long>(idleAfter - idleBefore);
    INFO("CPU ticks over 2s of idleness: " << idleDelta << " (wedged is ~200)");
    CHECK(idleDelta < 20);

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());

    XDestroyWindow(d.get(), after);
    XDestroyWindow(d.get(), transientWin);
    XSync(d.get(), False);
}
