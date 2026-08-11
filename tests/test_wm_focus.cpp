// Focus-policy tests against the REAL compiled wm2-born-again binary (FOCUS-02).
//
// Every case here flips one of the three focus booleans on the WM's COMMAND LINE
// and asserts that the running window manager behaves differently as a result.
// That is the whole point: the booleans were parsed from Phase 5 onward and read
// by nothing but tests/test_config.cpp, so a passing config test proved only that
// a value had been stored. Nothing until this file proved that any of them
// reached a runtime branch.
//
// Tag: [wm_focus] -- registered as a ctest LABEL via ADD_TAGS_AS_LABELS (D-33),
// selected with `ctest -L '^wm_focus$' --no-tests=error`.
//
// Three rules this file follows throughout:
//
//   INPUT COMES FROM XTEST, ON ITS OWN CONNECTION. The WM takes pointer grabs
//   during interaction, and grab-imperviousness is a property of the CALLING
//   connection (RESEARCH Pitfall 10). Client::eventButton() additionally bails
//   out on `e->send_event`, so a hand-rolled synthetic button event would never
//   reach the activation path these cases exist to exercise.
//
//   OUTCOMES ARE OBSERVED THROUGH THE SERVER, never through WM internals. Focus
//   is _NET_ACTIVE_WINDOW on root; stacking order is the position of a frame in
//   root's child list, which XQueryTree returns bottom-to-top.
//
//   NON-EVENTS ARE PROVEN BY WAITING PAST THE CONFIGURED DELAY. Three cases here
//   assert that something did NOT happen. There is no observation that says "the
//   timer you did not arm has not fired", so those cases run with deliberately
//   short delays (see kAutoRaiseDelayMs below), wait well past them, and then
//   assert. Every other wait in this file is a deadline-bounded poll on a real
//   observation.

#include <catch2/catch_test_macros.hpp>

#include "support/WmFixture.h"
#include "support/XTestDriver.h"

#include "x11wrap.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace wm2test;

namespace {

// The delays this suite runs the WM with, passed on its command line rather than
// waited out at their shipped values (400ms / 80ms). Named here because the
// non-event wait below is derived from them.
constexpr int kAutoRaiseDelayMs      = 100;
constexpr int kPointerStoppedDelayMs = 30;

// How long a "this did not happen" case waits before asserting. An order of
// magnitude past the auto-raise delay, so a timer that was going to fire has had
// many opportunities. This is the ONLY fixed wait in the file and it exists for
// the one thing polling cannot express -- see the header.
constexpr int kNonEventWaitMs = 1500;

// A parking spot for the pointer that is clear of every window this file maps.
// Set BEFORE anything is mapped: the pointer starts at the screen centre, and a
// window mapped underneath it would generate an EnterNotify that races the case.
constexpr int kParkX = 5;
constexpr int kParkY = 5;

WmFixtureOptions focusFixture(std::vector<std::string> flags)
{
    WmFixtureOptions o;
    o.wmArgs = std::move(flags);
    o.wmArgs.push_back("--auto-raise-delay=" + std::to_string(kAutoRaiseDelayMs));
    o.wmArgs.push_back("--pointer-stopped-delay=" + std::to_string(kPointerStoppedDelayMs));
    return o;
}

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
};

std::string describe(const Rect& r)
{
    return "(" + std::to_string(r.x) + "," + std::to_string(r.y) + " " +
           std::to_string(r.w) + "x" + std::to_string(r.h) + ")";
}

// Wake the WM's event loop so it flushes its X output buffer (deferred item 9).
// Same inert override-redirect nudge tests/test_wm_geometry.cpp documents at
// length: WindowManager::eventCreate returns immediately for such windows, so
// the WM never manages it and it cannot perturb anything these cases assert on,
// while its CreateNotify/DestroyNotify still wake the loop.
//
// Deliberately NOT used by the idle-CPU case, which must observe a WM nobody is
// poking.
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

// Give the WM every opportunity to act before concluding that it did not.
//
// Load-bearing, and found the hard way. A single pump before a non-event read is
// NOT enough: deferred item 9 means the WM's output sits in its buffer until its
// loop wakes again, so one nudge-then-read consistently observes the PREVIOUS
// state. Every "this did not happen" assertion in this file therefore reads
// stale-by-construction unless the WM is repeatedly woken first -- measured by
// deleting the click-to-focus gate, which left the negative half of behaviour 1
// green because the focus change it should have seen had not reached the server
// yet. With this settle in place the same deletion turns it red.
//
// The SPACING is as load-bearing as the repetition, and is the second half of
// the same lesson: fifteen nudges back to back are not fifteen wake-ups, because
// the WM is not necessarily scheduled between them. Measured with the gate
// removed: a tight ten-pump loop still read the stale value, while the same
// number of pumps at 20ms intervals read the true one every time.
//
// Same shape as the multi-pump settle tests/test_wm_geometry.cpp uses before its
// own "the WM did nothing" assertions.
void settleWm(Display* d)
{
    for (int i = 0; i < 15; ++i) {
        pumpWm(d);
        pollSleep();          // 20ms -- see above: the WM must actually be SCHEDULED
    }
}

// Absolute rectangle of a window as the SERVER has it. XGetGeometry's x/y are
// parent-relative, so the translate is what makes this absolute for a reparented
// child.
bool serverRect(Display* d, Window w, Rect& out)
{
    Window rootRet = None;
    int x = 0, y = 0;
    unsigned int width = 0, height = 0, bw = 0, depth = 0;
    if (!XGetGeometry(d, w, &rootRet, &x, &y, &width, &height, &bw, &depth)) return false;

    int absX = 0, absY = 0;
    Window child = None;
    if (!XTranslateCoordinates(d, w, DefaultRootWindow(d), 0, 0, &absX, &absY, &child)) {
        return false;
    }
    out.x = absX;
    out.y = absY;
    out.w = static_cast<int>(width);
    out.h = static_cast<int>(height);
    return true;
}

// Position of `w` in root's child list. XQueryTree returns children in
// BOTTOM-TO-TOP order, so a larger index means higher in the stack. Returns -1
// if `w` is not a direct child of root.
//
// This is the whole stacking observation used in this file: the frames of two
// deliberately overlapping windows are compared by index. Nothing here reads a
// WM-internal notion of "the raised window".
int stackIndex(Display* d, Window w)
{
    Window root = DefaultRootWindow(d);
    Window wroot = None, parent = None, *children = nullptr;
    unsigned int n = 0;
    if (!XQueryTree(d, root, &wroot, &parent, &children, &n)) return -1;

    int index = -1;
    for (unsigned int i = 0; i < n; ++i) {
        if (children[i] == w) { index = static_cast<int>(i); break; }
    }
    if (children) XFree(children);
    return index;
}

int pumpedStackIndex(Display* d, Window w)
{
    pumpWm(d);
    return stackIndex(d, w);
}

Window activeWindow(Display* d)
{
    static Atom atom = None;
    if (atom == None) atom = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
    Window w = None;
    if (!readWindowProp(d, DefaultRootWindow(d), atom, w)) return None;
    return w;
}

Window pumpedActiveWindow(Display* d)
{
    pumpWm(d);
    return activeWindow(d);
}

// Map a normal client at an exact geometry and wait until the WM has both
// reparented it into a frame AND published it in _NET_CLIENT_LIST. The second
// half keeps later reads from racing the rest of Client::manage().
Window mapClientAndAwaitFrame(Display* d, int x, int y, int w, int h, Window& clientOut)
{
    Window root = DefaultRootWindow(d);
    Window win = XCreateSimpleWindow(d, root, x, y,
                                     static_cast<unsigned>(w), static_cast<unsigned>(h), 0,
                                     BlackPixel(d, DefaultScreen(d)),
                                     WhitePixel(d, DefaultScreen(d)));
    XMapWindow(d, win);
    XSync(d, False);
    clientOut = win;

    Window frame = None;
    const bool framed = WmFixture::pollUntil([&] {
        pumpWm(d);
        Window wroot = None, parent = None, *children = nullptr;
        unsigned int n = 0;
        if (!XQueryTree(d, win, &wroot, &parent, &children, &n)) return false;
        if (children) XFree(children);
        if (parent == None || parent == root) return false;
        frame = parent;
        return true;
    }, 8000);
    if (!framed) return None;

    Atom clientList = XInternAtom(d, "_NET_CLIENT_LIST", False);
    WmFixture::pollUntil([&] {
        Atom actualType = None;
        int actualFormat = 0;
        unsigned long nItems = 0, bytesAfter = 0;
        unsigned char* raw = nullptr;
        if (XGetWindowProperty(d, root, clientList, 0, 256, False, XA_WINDOW,
                               &actualType, &actualFormat, &nItems, &bytesAfter,
                               &raw) != Success) {
            return false;
        }
        bool found = false;
        if (raw && actualType == XA_WINDOW && actualFormat == 32) {
            Window* vals = reinterpret_cast<Window*>(raw);
            for (unsigned long i = 0; i < nItems; ++i) if (vals[i] == win) found = true;
        }
        if (raw) XFree(raw);
        return found;
    }, 8000);

    return frame;
}

bool contains(const Rect& r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// A point inside `low` that `high` does not cover, so moving the pointer there
// enters the LOWER window rather than the one stacked over it.
//
// Computed from the rectangles the server actually reports rather than from the
// coordinates the test asked for: the frame indents depend on the tab width,
// which depends on the font, and hardcoding them here would make this file fail
// the day someone changes a font.
bool exposedPoint(const Rect& low, const Rect& high, int& x, int& y)
{
    const int qx[] = { low.w / 4, (low.w * 3) / 4, low.w / 4, (low.w * 3) / 4, low.w / 2 };
    const int qy[] = { low.h / 4, low.h / 4, (low.h * 3) / 4, (low.h * 3) / 4, low.h / 2 };

    for (int i = 0; i < 5; ++i) {
        const int cx = low.x + qx[i];
        const int cy = low.y + qy[i];
        if (!contains(high, cx, cy)) { x = cx; y = cy; return true; }
    }
    return false;
}

// The two overlapping clients every stacking case uses, plus the input driver,
// brought up in the one order that cannot race the WM.
//
// Order matters and is not incidental: the driver is constructed and the pointer
// parked in a corner BEFORE anything is mapped. The pointer starts at the screen
// centre, and a window mapped under it would generate an EnterNotify that starts
// focus tracking before the case has begun -- which is exactly the flake this
// ordering removes.
struct OverlappingPair {
    Window clientA = None, frameA = None;
    Window clientB = None, frameB = None;

    // Resolved after the WM has settled: which frame is currently on top.
    Window lowFrame = None, highFrame = None;
    Window lowClient = None;
    Rect lowRect{}, highRect{};
    int enterX = 0, enterY = 0;      // a point inside the LOWER window only
};

bool buildOverlappingPair(Display* d, OverlappingPair& out)
{
    out.frameA = mapClientAndAwaitFrame(d, 100, 100, 300, 220, out.clientA);
    if (out.frameA == None) return false;
    out.frameB = mapClientAndAwaitFrame(d, 260, 190, 300, 220, out.clientB);
    if (out.frameB == None) return false;

    const int idxA = pumpedStackIndex(d, out.frameA);
    const int idxB = pumpedStackIndex(d, out.frameB);
    if (idxA < 0 || idxB < 0 || idxA == idxB) return false;

    // Whichever the WM actually left underneath is the one to point at. Deriving
    // this rather than assuming map order fixes the stack keeps the cases honest
    // if the WM's initial placement ever changes.
    const bool aLower = idxA < idxB;
    out.lowFrame  = aLower ? out.frameA  : out.frameB;
    out.highFrame = aLower ? out.frameB  : out.frameA;
    out.lowClient = aLower ? out.clientA : out.clientB;

    if (!serverRect(d, out.lowFrame, out.lowRect)) return false;
    if (!serverRect(d, out.highFrame, out.highRect)) return false;

    return exposedPoint(out.lowRect, out.highRect, out.enterX, out.enterY);
}

// The one fixed wait in the file. Wrapped in a named function so every call site
// reads as what it is -- proving a non-event -- rather than as an unexplained
// pause. See the file header for why polling cannot replace it here.
void waitPastFocusDelays()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(kNonEventWaitMs));
}

} // namespace

// ---------------------------------------------------------------------------
// Behaviour 1: click-to-focus ON -- the pointer does not focus, a click does
//
// Both halves are in one case on purpose. The negative half alone would pass on
// a WM that had simply stopped focusing anything at all; the positive half is
// what proves the gate suppressed one focus route rather than every route.
// ---------------------------------------------------------------------------

TEST_CASE("With click-to-focus the pointer alone does not focus a window but a click does",
          "[wm_focus]")
{
    WmFixture fixture(focusFixture({ "--click-to-focus" }));

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    // Prove the configuration reached the running process before relying on it.
    const std::string log = fixture.wmStderr();
    INFO("wm stderr: " << log);
    REQUIRE(log.find("Click to focus.") != std::string::npos);
    REQUIRE(log.find("Focus follows pointer.") == std::string::npos);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d.get(), 200, 160, 300, 220, client);
    REQUIRE(client != None);
    REQUIRE(frame != None);

    Rect clientRect;
    REQUIRE(serverRect(d.get(), client, clientRect));
    INFO("client rect: " << describe(clientRect));

    REQUIRE(pumpedActiveWindow(d.get()) != client);

    // --- negative half: pointer entry must NOT focus -------------------------
    driver.moveTo(clientRect.x + clientRect.w / 2, clientRect.y + clientRect.h / 2);
    waitPastFocusDelays();
    settleWm(d.get());

    const Window afterEnter = activeWindow(d.get());
    INFO("active after pointer entry: " << afterEnter << "  client: " << client);
    CHECK(afterEnter != client);

    // --- positive half: a click on the same spot MUST focus ------------------
    //
    // press -> wait -> release rather than click(): a back-to-back release can
    // arrive before the WM has established the pointer grab its button handling
    // takes, leaving it waiting for a release that already happened. This is the
    // sanctioned sequencing shape (08-RESEARCH Example 2) and is deliberately
    // not expressed as the XTEST `delay` argument, which would block the driver
    // connection instead.
    driver.press(Button1);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    driver.release(Button1);

    const bool focused = WmFixture::pollUntil([&] {
        return pumpedActiveWindow(d.get()) == client;
    }, 8000);

    INFO("wm stderr: " << fixture.wmStderr());
    INFO("active after click: " << pumpedActiveWindow(d.get()));
    CHECK(focused);

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());
    XDestroyWindow(d.get(), client);
    XSync(d.get(), False);
}

// ---------------------------------------------------------------------------
// Behaviour 2: click-to-focus OFF -- the pointer alone focuses
//
// The control for behaviour 1, and the shipped default. Auto-raise is on here
// because with it off nothing consults the pointer at all -- which is behaviour
// 3's subject, not this one's.
// ---------------------------------------------------------------------------

TEST_CASE("With click-to-focus off the pointer alone focuses a window", "[wm_focus]")
{
    WmFixture fixture(focusFixture({ "--no-click-to-focus", "--auto-raise" }));

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    const std::string log = fixture.wmStderr();
    INFO("wm stderr: " << log);
    REQUIRE(log.find("Focus follows pointer.") != std::string::npos);
    REQUIRE(log.find("Click to focus.") == std::string::npos);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d.get(), 200, 160, 300, 220, client);
    REQUIRE(client != None);
    REQUIRE(frame != None);

    Rect clientRect;
    REQUIRE(serverRect(d.get(), client, clientRect));
    REQUIRE(pumpedActiveWindow(d.get()) != client);

    // No click anywhere in this case -- the pointer move is the entire input.
    driver.moveTo(clientRect.x + clientRect.w / 2, clientRect.y + clientRect.h / 2);

    const bool focused = WmFixture::pollUntil([&] {
        return pumpedActiveWindow(d.get()) == client;
    }, 8000);

    INFO("wm stderr: " << fixture.wmStderr());
    INFO("active: " << pumpedActiveWindow(d.get()) << "  client: " << client);
    CHECK(focused);

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());
    XDestroyWindow(d.get(), client);
    XSync(d.get(), False);
}

// ---------------------------------------------------------------------------
// Behaviour 3: auto-raise OFF -- pointer entry neither raises nor focuses
//
// With the deadline never armed, the branch that calls focusIfAppropriate() on
// expiry is never reached, so pointer entry has no effect at all. Both halves
// are asserted: the stacking order is unchanged AND the window under the pointer
// did not become active. The second is what distinguishes this case from
// behaviour 5, where activation still happens and only the raise is suppressed.
// ---------------------------------------------------------------------------

TEST_CASE("With auto-raise off pointer entry neither raises nor focuses the window",
          "[wm_focus]")
{
    WmFixture fixture(focusFixture({ "--no-auto-raise", "--no-click-to-focus" }));

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    const std::string log = fixture.wmStderr();
    INFO("wm stderr: " << log);
    REQUIRE(log.find("Auto-raise off.") != std::string::npos);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    OverlappingPair pair;
    REQUIRE(buildOverlappingPair(d.get(), pair));
    INFO("low " << describe(pair.lowRect) << "  high " << describe(pair.highRect)
         << "  entering at " << pair.enterX << "," << pair.enterY);

    const int lowBefore  = pumpedStackIndex(d.get(), pair.lowFrame);
    const int highBefore = pumpedStackIndex(d.get(), pair.highFrame);
    REQUIRE(lowBefore < highBefore);          // precondition: it really is below

    driver.moveTo(pair.enterX, pair.enterY);
    waitPastFocusDelays();
    settleWm(d.get());

    const int lowAfter  = stackIndex(d.get(), pair.lowFrame);
    const int highAfter = stackIndex(d.get(), pair.highFrame);

    INFO("wm stderr: " << fixture.wmStderr());
    INFO("stack before: low=" << lowBefore << " high=" << highBefore
         << "  after: low=" << lowAfter << " high=" << highAfter);
    CHECK(lowAfter < highAfter);              // never raised over its neighbour

    const Window active = activeWindow(d.get());
    INFO("active: " << active << "  low client: " << pair.lowClient);
    CHECK(active != pair.lowClient);          // and never focused either

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());
    XDestroyWindow(d.get(), pair.clientA);
    XDestroyWindow(d.get(), pair.clientB);
    XSync(d.get(), False);
}

// ---------------------------------------------------------------------------
// Behaviour 4: auto-raise ON -- pointer entry raises after the delay
//
// The control for behaviour 3 and the shipped default. Without this case the
// auto-raise gate could be stuck off and behaviour 3 would still be green.
// ---------------------------------------------------------------------------

TEST_CASE("With auto-raise on pointer entry raises the window above its neighbour",
          "[wm_focus]")
{
    WmFixture fixture(focusFixture({ "--auto-raise", "--raise-on-focus",
                                     "--no-click-to-focus" }));

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    const std::string log = fixture.wmStderr();
    INFO("wm stderr: " << log);
    REQUIRE(log.find("Auto-raise on.") != std::string::npos);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    OverlappingPair pair;
    REQUIRE(buildOverlappingPair(d.get(), pair));
    INFO("low " << describe(pair.lowRect) << "  high " << describe(pair.highRect)
         << "  entering at " << pair.enterX << "," << pair.enterY);

    REQUIRE(pumpedStackIndex(d.get(), pair.lowFrame) <
            pumpedStackIndex(d.get(), pair.highFrame));

    driver.moveTo(pair.enterX, pair.enterY);

    const bool raised = WmFixture::pollUntil([&] {
        return pumpedStackIndex(d.get(), pair.lowFrame) >
               pumpedStackIndex(d.get(), pair.highFrame);
    }, 8000);

    INFO("wm stderr: " << fixture.wmStderr());
    INFO("stack after: low=" << pumpedStackIndex(d.get(), pair.lowFrame)
         << " high=" << pumpedStackIndex(d.get(), pair.highFrame));
    CHECK(raised);

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());
    XDestroyWindow(d.get(), pair.clientA);
    XDestroyWindow(d.get(), pair.clientB);
    XSync(d.get(), False);
}

// ---------------------------------------------------------------------------
// Behaviour 5: raise-on-focus OFF -- focusing does not restack
//
// This is the case that pins the SPLIT. Before this plan, activating a window
// and raising it were one inseparable action at two sites; the assertion pair
// below -- became active, did not move in the stack -- is expressible only once
// they are separate. Auto-raise stays ON so the focus route actually runs;
// turning it off would make this indistinguishable from behaviour 3.
// ---------------------------------------------------------------------------

TEST_CASE("With raise-on-focus off a window is focused without changing stacking order",
          "[wm_focus]")
{
    WmFixture fixture(focusFixture({ "--no-raise-on-focus", "--auto-raise",
                                     "--no-click-to-focus" }));

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    const std::string log = fixture.wmStderr();
    INFO("wm stderr: " << log);
    REQUIRE(log.find("No raise on focus.") != std::string::npos);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    OverlappingPair pair;
    REQUIRE(buildOverlappingPair(d.get(), pair));
    INFO("low " << describe(pair.lowRect) << "  high " << describe(pair.highRect)
         << "  entering at " << pair.enterX << "," << pair.enterY);

    const int lowBefore  = pumpedStackIndex(d.get(), pair.lowFrame);
    const int highBefore = pumpedStackIndex(d.get(), pair.highFrame);
    REQUIRE(lowBefore < highBefore);

    driver.moveTo(pair.enterX, pair.enterY);

    // The activation still happens -- that half is unconditional.
    const bool focused = WmFixture::pollUntil([&] {
        return pumpedActiveWindow(d.get()) == pair.lowClient;
    }, 8000);
    INFO("active: " << pumpedActiveWindow(d.get()) << "  low client: " << pair.lowClient);
    REQUIRE(focused);

    // ...and having focused it, the WM left the stack alone. Waited out past the
    // delays so a raise that was merely slower than the focus cannot slip by.
    waitPastFocusDelays();
    settleWm(d.get());

    const int lowAfter  = stackIndex(d.get(), pair.lowFrame);
    const int highAfter = stackIndex(d.get(), pair.highFrame);

    INFO("wm stderr: " << fixture.wmStderr());
    INFO("stack before: low=" << lowBefore << " high=" << highBefore
         << "  after: low=" << lowAfter << " high=" << highAfter);
    CHECK(lowAfter < highAfter);

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());
    XDestroyWindow(d.get(), pair.clientA);
    XDestroyWindow(d.get(), pair.clientB);
    XSync(d.get(), False);
}

// ---------------------------------------------------------------------------
// Behaviour 6: raise-on-focus ON -- the pair fires together, as it does today
//
// The control for behaviour 5. Same configuration except the one boolean, and
// both halves of the previously-fused action are asserted to happen.
// ---------------------------------------------------------------------------

TEST_CASE("With raise-on-focus on a focused window is also raised", "[wm_focus]")
{
    WmFixture fixture(focusFixture({ "--raise-on-focus", "--auto-raise",
                                     "--no-click-to-focus" }));

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    const std::string log = fixture.wmStderr();
    INFO("wm stderr: " << log);
    REQUIRE(log.find("Raise on focus.") != std::string::npos);
    REQUIRE(log.find("No raise on focus.") == std::string::npos);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    OverlappingPair pair;
    REQUIRE(buildOverlappingPair(d.get(), pair));
    INFO("low " << describe(pair.lowRect) << "  high " << describe(pair.highRect)
         << "  entering at " << pair.enterX << "," << pair.enterY);

    REQUIRE(pumpedStackIndex(d.get(), pair.lowFrame) <
            pumpedStackIndex(d.get(), pair.highFrame));

    driver.moveTo(pair.enterX, pair.enterY);

    const bool focusedAndRaised = WmFixture::pollUntil([&] {
        return pumpedActiveWindow(d.get()) == pair.lowClient &&
               pumpedStackIndex(d.get(), pair.lowFrame) >
               pumpedStackIndex(d.get(), pair.highFrame);
    }, 8000);

    INFO("wm stderr: " << fixture.wmStderr());
    INFO("active: " << pumpedActiveWindow(d.get())
         << "  stack: low=" << pumpedStackIndex(d.get(), pair.lowFrame)
         << " high=" << pumpedStackIndex(d.get(), pair.highFrame));
    CHECK(focusedAndRaised);

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());
    XDestroyWindow(d.get(), pair.clientA);
    XDestroyWindow(d.get(), pair.clientB);
    XSync(d.get(), False);
}

// ---------------------------------------------------------------------------
// Behaviour 7: with auto-raise off the event loop consumes no CPU while idle
//
// The compiled-behaviour checklist's no-idle-CPU-spin item, asserted directly on
// the running process rather than inferred from the shape of the poll loop.
//
// The pointer is left resting inside a managed window, so focus tracking is LIVE
// for the whole sample -- an idle WM that never started tracking would prove
// nothing. And nothing pokes the WM during the sample: pumpWm() is deliberately
// not called between the two readings.
//
// WHAT THIS DOES AND DOES NOT DISCRIMINATE, measured rather than assumed:
//
//   It IS a real spin detector. The same measurement, applied to the circulate()
//   defect in tests/test_wm_process.cpp, read 595 ticks before that fix and 0
//   after -- so a wedged or busy-waiting event loop cannot pass this case.
//
//   It does NOT distinguish WHERE the auto-raise gate sits. The obvious worry
//   about gating the expiry branch instead of the arming site is that the
//   deadline would stay active in the past, computePollTimeout() would return 0
//   forever and the loop would spin. That was implemented and measured: the CPU
//   delta was still 0, so something else clears the deadline on that path and
//   the worry does not materialise. The arming site remains the right place --
//   it is where the plan puts it and it is the only form in which the poll loop
//   provably blocks with no timeout -- but this case is not the evidence for
//   that choice, and saying so here is cheaper than someone re-deriving it.
// ---------------------------------------------------------------------------

TEST_CASE("With auto-raise off the WM burns no CPU while focus tracking is live",
          "[wm_focus]")
{
    WmFixture fixture(focusFixture({ "--no-auto-raise", "--no-click-to-focus" }));

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    const std::string log = fixture.wmStderr();
    INFO("wm stderr: " << log);
    REQUIRE(log.find("Auto-raise off.") != std::string::npos);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d.get(), 200, 160, 300, 220, client);
    REQUIRE(client != None);
    REQUIRE(frame != None);

    Rect clientRect;
    REQUIRE(serverRect(d.get(), client, clientRect));

    // Enter the window: this is what puts the WM into the focus-tracking state
    // whose poll timeout the case is about.
    driver.moveTo(clientRect.x + clientRect.w / 2, clientRect.y + clientRect.h / 2);
    waitPastFocusDelays();

    unsigned long long before = 0;
    REQUIRE(processCpuTicks(fixture.wm().pid(), before));

    // Three seconds of complete quiet. No pump, no input, no property reads.
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));

    unsigned long long after = 0;
    REQUIRE(processCpuTicks(fixture.wm().pid(), after));

    const long long delta = static_cast<long long>(after - before);
    INFO("CPU ticks over 3s of idleness with focus tracking live: " << delta);
    INFO("(a loop spinning on an expired deadline accrues ~300 over this interval)");
    CHECK(delta < 20);

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());
    XDestroyWindow(d.get(), client);
    XSync(d.get(), False);
}
