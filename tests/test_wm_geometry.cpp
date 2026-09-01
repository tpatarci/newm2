// Geometry tests against the REAL compiled wm2-born-again binary.
//
// Purpose (plan 08-04): pin the values today's implementation produces for every
// computation that consults the D-27 screen-geometry accessors, so that plan
// 08-05 can change *when* those values are recomputed without silently changing
// *what* they are. Each case below therefore asserts an exact number, not a
// range -- a range would survive the very off-by-one this file exists to guard.
//
// Tags in this file:
//   [wm_geometry] -- workarea publication, the map-time clamp, the
//                    ensureVisible() clamp, and (plan 08-05) the resolution
//                    change reflow, on a server that starts at 1280x1024
//   [wm_norandr]  -- XDIS-02: the WM on a server with no RANDR extension at
//                    all, and the root-ConfigureNotify fallback that has to
//                    carry resolution changes there
//
// Screen size: 1280x1024, deliberately the largest any Phase 8 geometry test
// needs. This X server's RANDR maximum screen size equals the geometry it was
// started with, and plan 08-05 must SHRINK the screen to observe a resolution
// change -- it can never grow past the initial size. Starting small would make
// that plan untestable.
//
// The fixture's default is 1024x768; a later `-screen 0 <geom>` for the same
// screen index overrides an earlier one, which is why appending through
// xvfbArgs is enough and WmFixture needs no change. requireFixtureScreen()
// below asserts the override actually took, so a future Xvfb that ignored it
// would fail loudly here instead of making every case red for a confusing
// reason.
//
// Plan 08-05 extends this file with the resolution-change cases; the fixture
// options and the geometry helpers are written so adding those needs no
// restructuring.

#include <catch2/catch_test_macros.hpp>

#include "support/WmFixture.h"
#include "support/XTestDriver.h"

#include "x11wrap.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <chrono>
#include <thread>
#include <utility>
#include <vector>

using namespace wm2test;

namespace {

// The fixture screen. Every expected value below is derived from these two
// numbers and the arithmetic in src/Client.cpp / src/Manager.cpp -- nothing is
// a magic constant copied out of a failing run.
constexpr int kScreenW = 1280;
constexpr int kScreenH = 1024;

// src/Client.cpp ensureVisible() clamps against screen size MINUS ONE. That
// off-by-one is the single most reversible-looking detail in the whole clamp,
// so it is named here and asserted exactly rather than absorbed into a
// "roughly on screen" predicate.
constexpr int kMaxX = kScreenW - 1;
constexpr int kMaxY = kScreenH - 1;

WmFixtureOptions geometryFixture()
{
    WmFixtureOptions o;
    o.xvfbArgs = { "-screen", "0", "1280x1024x24" };
    return o;
}

// XDIS-02: a server that genuinely does not have the RANDR extension.
//
// Unlike SHAPE -- which this X server refuses to disable, answering
// `Extension "SHAPE" can not be disabled` and forcing plan 08-03 to simulate
// its absence in-process -- RANDR is on the server's toggleable list. So the
// evidence below is a real extension-less server rather than a lever, which is
// the strongest form this claim can take.
WmFixtureOptions noRandrFixture()
{
    WmFixtureOptions o;
    o.xvfbArgs = { "-screen", "0", "1280x1024x24", "-extension", "RANDR" };
    return o;
}

// The same WM behaviour, on a server that DOES have RANDR, with the WM's own
// RANDR path forced inert.
//
// This exists because of a hard constraint measured on this host: on a server
// started with `-extension RANDR` the screen cannot be resized AT ALL. The only
// mechanism for changing an X screen's size is RANDR's own RRSetScreenSize, so
// `xrandr --fb 1024x768` there answers "RandR extension missing" and the
// dimensions stay put. There is no second mechanism to fall back on -- Xvfb
// offers no XF86VidMode screen resizing either.
//
// So "does the fallback actually reflow?" cannot be asked of a genuinely
// RANDR-less server: the question needs a resize, and a resize needs the
// extension. Forcing the sentinel off on a RANDR-capable server is the only
// configuration in which the fallback can be DRIVEN, and it is a faithful one:
// with the sentinel below zero the WM never calls XRRSelectInput, so no
// screen-change notification is ever selected and the RANDR dispatch arm cannot
// match. Root's ConfigureNotify is left as the only thing that can possibly
// reach the handler.
WmFixtureOptions forcedNoRandrFixture()
{
    WmFixtureOptions o;
    o.xvfbArgs = { "-screen", "0", "1280x1024x24" };
    o.childEnv = { { "WM2_FORCE_NO_RANDR", "1" } };
    return o;
}

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool operator==(const Rect& o) const
    {
        return x == o.x && y == o.y && w == o.w && h == o.h;
    }
};

std::string describe(const Rect& r)
{
    return "(" + std::to_string(r.x) + "," + std::to_string(r.y) + " " +
           std::to_string(r.w) + "x" + std::to_string(r.h) + ")";
}

// Guard: the whole file's expected values assume this screen size.
void requireFixtureScreen(Display* d)
{
    const int screen = DefaultScreen(d);
    INFO("fixture screen is " << DisplayWidth(d, screen) << "x" << DisplayHeight(d, screen)
         << ", expected " << kScreenW << "x" << kScreenH
         << " -- the xvfbArgs -screen override did not take");
    REQUIRE(DisplayWidth(d, screen) == kScreenW);
    REQUIRE(DisplayHeight(d, screen) == kScreenH);
}

// Absolute position + size of any window, read back FROM THE SERVER rather than
// from what the test asked for. XGetGeometry's x/y are relative to the parent,
// so the translate is what makes this absolute for a reparented child.
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

// Create a normal (non-transient, non-dock) client at an exact geometry and wait
// until the WM has reparented it into a frame AND published it in
// _NET_CLIENT_LIST. The second half matters: the frame appears during
// Client::manage(), and waiting for the client list keeps the geometry read
// below from racing the rest of that function.
Window mapClientAndAwaitFrame(Display* d, int x, int y, int w, int h, Window& clientOut)
{
    Window root = DefaultRootWindow(d);
    Window win = XCreateSimpleWindow(d, root, x, y, static_cast<unsigned>(w),
                                     static_cast<unsigned>(h), 0,
                                     BlackPixel(d, DefaultScreen(d)),
                                     WhitePixel(d, DefaultScreen(d)));
    XMapWindow(d, win);
    XSync(d, False);
    clientOut = win;

    Window frame = None;
    const bool framed = WmFixture::pollUntil([&] {
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

// Drive WindowManager::circulate(true) -> Client::activateAndWarp() ->
// Client::ensureVisible(), which is the ONLY live route to the clamp (the other
// textual call site, src/Buttons.cpp:338, sits in a menu branch that the index
// arithmetic above it makes unreachable).
//
// A synthetic ButtonPress rather than XTEST, for two specific reasons:
//
//   1. Case 4 deliberately gives the client a window LARGER THAN THE SCREEN at
//      the origin. There is then no root pixel anywhere for a pointer-based
//      click to land on, so XTEST cannot reach root at all.
//   2. WindowManager::eventButton() (src/Buttons.cpp:16-28) applies no
//      send_event guard -- unlike Client::eventButton(), which explicitly
//      ignores synthetic events. The root branch is reached identically either
//      way, so nothing is being faked past a check.
//
// This is NOT a licence to prefer synthetic events generally: 08-RESEARCH's
// "Don't Hand-Roll" row still stands for anything that must survive a pointer
// grab or reach Client::eventButton -- use XTestDriver there.
//
// The trigger is proven load-bearing by the assertions themselves: cases 3 and
// 4 require the window to END UP somewhere it was not, so a trigger that
// silently did nothing turns them red rather than green.
void triggerClamp(Display* d)
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

// Wake the WM's event loop so it flushes its X output buffer.
//
// Non-obvious and load-bearing. The WM issues the frame's XConfigureWindow while
// handling an event and then goes straight back to blocking; the requests it
// queued are not observable on the server until its loop wakes for a LATER
// event. Measured on this host with a client that moves itself and then does
// nothing else: the frame stayed at its old position indefinitely (8s of polling
// with no other X traffic), and moved the instant any unrelated event reached the
// WM. Reading geometry from an idle WM therefore reports the PREVIOUS state and
// makes a correct implementation look broken.
//
// The nudge is an InputOnly, override-redirect 1x1 window created and destroyed
// off-screen. Override-redirect is what keeps it inert: WindowManager::eventCreate
// returns immediately for such windows (src/Events.cpp:230-234), so the WM never
// manages it, it never enters _NET_CLIENT_LIST, and it cannot perturb the
// circulate order any test here depends on -- while the CreateNotify and
// DestroyNotify still reach the WM through root's substructure selection and wake
// the loop.
//
// This is a harness workaround, not an endorsement: the underlying deferred-flush
// behaviour is recorded in deferred-items.md as a real defect for a later plan.
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

// Wait until the WM has actually made `client` the active window.
//
// Border::eventButton() DROPS a press on the frame outright unless the client is
// already active, so a drag started before activation lands does nothing at all
// -- and a case whose assertions are all "the window did not go off the edge"
// then passes vacuously. MEASURED on the debug tree with the commit clamp
// deliberately removed: 1 run in 4 took that path and reported a pass.
bool awaitActive(Display* d, Window client)
{
    const Atom netActive = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);

    for (int i = 0; i < 100; ++i) {
        Atom actual = None;
        int format = 0;
        unsigned long count = 0, remaining = 0;
        unsigned char* data = nullptr;

        if (XGetWindowProperty(d, DefaultRootWindow(d), netActive, 0, 1, False,
                               XA_WINDOW, &actual, &format, &count, &remaining,
                               &data) == Success && data) {
            const Window active = *reinterpret_cast<Window*>(data);
            XFree(data);
            if (active == client) return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

// Spacing between the steps of a synthesised drag.
//
// pumpWm() is useless inside a drag: Client::move() runs a nested loop that only
// checks the pointer and Exposure masks, so the nudge window's CreateNotify is
// not even looked at. Worse, that loop drains EVERY queued event in one inner
// sweep and dispatches only the last one it read -- fire a whole drag with no
// wall-clock spacing and the ButtonRelease is drained alongside the motions, so
// `doSomething` never becomes true and the drag silently does nothing. The drag
// case below passed for exactly that reason before this existed.
//
// The loop's idle wait is poll(50ms), so the spacing has to clear it.
void dragSettle()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(90));
}

// Read `w`'s rectangle after waking the WM, so the value reflects everything the
// WM has decided rather than everything it has happened to flush.
bool pumpedRect(Display* d, Window w, Rect& out)
{
    pumpWm(d);
    return serverRect(d, w, out);
}

// ---------------------------------------------------------------------------
// Resolution-change helpers (plan 08-05)
// ---------------------------------------------------------------------------

// The geometry the whole resolution-change group shrinks TO. It must be smaller
// than the fixture screen on both axes: this X server's RANDR maximum screen
// size equals the geometry it was started with, so the suite can only shrink.
constexpr int kSmallW = 1024;
constexpr int kSmallH = 768;
constexpr int kSmallMaxX = kSmallW - 1;
constexpr int kSmallMaxY = kSmallH - 1;

// The screen size AS THE SERVER CURRENTLY HAS IT, not as this connection's Xlib
// cache remembers it.
//
// DisplayWidth/DisplayHeight would be wrong here for exactly the reason plan
// 08-05 exists: they read the cached Screen struct inside the Display
// connection, which Xlib never updates behind the client's back. A test that
// polled them would keep reporting 1280x1024 forever and would time out waiting
// for a resize that had already happened. XGetWindowAttributes on root is a real
// round trip, so it always reports the truth.
bool liveScreenSize(Display* d, int& w, int& h)
{
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(d, DefaultRootWindow(d), &attrs)) return false;
    w = attrs.width;
    h = attrs.height;
    return true;
}

// Run one xrandr command against `display` and reap it. The exit status is
// returned but deliberately not asserted on by any caller -- see resizeScreenTo.
int runXrandr(const std::string& display, const std::vector<std::string>& args)
{
    std::vector<std::string> argv{ "xrandr", "-display", display };
    for (const auto& a : args) argv.push_back(a);

    pid_t pid = ::fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        // xrandr's diagnostics are expected noise (see below); keep them out of
        // the Catch2 transcript.
        int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            ::dup2(devnull, STDOUT_FILENO);
            ::dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) ::close(devnull);
        }
        std::vector<char*> cargv;
        cargv.reserve(argv.size() + 1);
        for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
        cargv.push_back(nullptr);
        ::execvp(cargv[0], cargv.data());
        _exit(127);
    }

    int status = 0;
    ::waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// Shrink the fixture's X screen to w x h and wait until the SERVER agrees.
//
// Two commands, in this order, per RESEARCH Pitfall 7. The framebuffer-resize
// form is used rather than the legacy size-selection form (`xrandr -s WxH`)
// because this server advertises exactly one mode, so the legacy form answers
// "Size ... not found in available modes" and does nothing. The preceding
// output-off command exists to release the CRTC that still covers the old,
// larger area.
//
// NEITHER exit status is trusted, and that is not laziness. Measured on this
// host: `--output screen --off` prints a BadValue on RRSetScreenSize and exits
// 1 while doing its job, and `--fb` alone prints a BadValue on RRSetCrtcConfig
// and exits 1 while the framebuffer resizes anyway. Gating on either status
// would fail a resize that actually happened. The outcome is therefore observed
// on the server, which is the only thing that can be wrong in a way that
// matters.
bool resizeScreenTo(const std::string& display, int w, int h)
{
    runXrandr(display, { "--output", "screen", "--off" });
    runXrandr(display, { "--fb", std::to_string(w) + "x" + std::to_string(h) });

    // A fresh connection: this process may already hold one whose cached screen
    // size is stale, and the poll below must read the server, not a cache.
    x11::DisplayPtr probe(XOpenDisplay(display.c_str()));
    if (!probe) return false;

    return WmFixture::pollUntil([&] {
        int gotW = 0, gotH = 0;
        return liveScreenSize(probe.get(), gotW, gotH) && gotW == w && gotH == h;
    }, 8000);
}

// Poll until _NET_WORKAREA on root equals `expected`, then return whatever it
// actually holds so a failure can print the observed value.
std::vector<unsigned long> awaitWorkarea(Display* d,
                                         const std::vector<unsigned long>& expected)
{
    Atom workarea = XInternAtom(d, "_NET_WORKAREA", False);
    std::vector<unsigned long> values;
    WmFixture::pollUntil([&] {
        pumpWm(d);
        return readCardinals(d, DefaultRootWindow(d), workarea, values) &&
               values == expected;
    }, 8000);
    pumpWm(d);
    readCardinals(d, DefaultRootWindow(d), workarea, values);
    return values;
}

std::string describe(const std::vector<unsigned long>& v)
{
    std::string s = "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ",";
        s += std::to_string(v[i]);
    }
    return s + "]";
}

// Ask a mapped client to move and resize itself, then report the rectangle the
// server actually settles on.
//
// The settled rectangle is NOT the requested one, and every case below derives
// its expectations from the returned value rather than from what it asked for.
// Client::eventConfigureRequest() calls gravitate(true), overwrites m_x/m_y from
// the request, then calls gravitate(false) -- so the frame lands on the
// requested coordinates and the child sits inside it by exactly the border
// indents. Those indents are a Border implementation detail (they depend on the
// tab width, which depends on the font), and hardcoding them here would make
// this file fail the day someone changes a font. Asking the server is free.
//
// Client::eventConfigureRequest() applies no clamp of its own, which is what
// makes this the right way to establish a precondition the WM would never have
// produced itself -- such as a window deliberately parked off the screen.
//
// "Settled" is keyed on the FRAME reaching the requested coordinates, not on the
// client rectangle going quiet.
//
// The obvious version -- poll the client until the requested size has taken and
// two consecutive reads agree -- is racy, and it bit this suite intermittently
// (about one run in ten) before being fixed. Every call site here moves a window
// WITHOUT changing its size, so the requested size is already satisfied the
// instant the move is issued; if both reads land before the WM has processed the
// ConfigureRequest, the helper reports the window "settled" at its OLD position
// and the caller derives every expectation from the wrong rectangle. The
// observed failure was a window still at x=325 being treated as though it had
// been parked at 900.
//
// The frame is the reliable signal because its settled position is exact:
// eventConfigureRequest() re-gravitates and hands the requested coordinates
// straight to Border::configure(), so the frame lands on exactly what was asked
// for while the child sits inside it by the border indents (measured: request
// 900,700 -> frame 900,700, client 925,708). Waiting on a value that must
// CHANGE removes the race rather than making it less likely.
bool placeClient(Display* d, Window client, Window frame,
                 int x, int y, int w, int h, Rect& out)
{
    XMoveResizeWindow(d, client, x, y,
                      static_cast<unsigned>(w), static_cast<unsigned>(h));
    XSync(d, False);

    return WmFixture::pollUntil([&] {
        Rect frameRect;
        if (!pumpedRect(d, frame, frameRect)) return false;
        if (frameRect.x != x || frameRect.y != y) return false;

        Rect clientRect;
        if (!serverRect(d, client, clientRect)) return false;
        if (clientRect.w != w || clientRect.h != h) return false;

        out = clientRect;
        return true;
    }, 8000);
}

// Poll until the server reports `w` at exactly `expected`, then return the final
// rectangle either way so a failure can print what was actually observed.
Rect awaitRect(Display* d, Window w, const Rect& expected)
{
    Rect got;
    WmFixture::pollUntil([&] {
        return pumpedRect(d, w, got) && got == expected;
    }, 8000);
    pumpedRect(d, w, got);
    return got;
}

} // namespace

// ---------------------------------------------------------------------------
// Case 1: _NET_WORKAREA with no docks is the whole screen
//
// Pins both accessor reads in src/Manager.cpp -- the initial publication in
// setupEwmhProperties() and the strut-subtracting recomputation in
// updateWorkarea(). With no docks mapped the two must agree on the full screen
// rectangle, which is only true if the cache was seeded BEFORE the initial
// publication ran.
// ---------------------------------------------------------------------------

TEST_CASE("With no docks _NET_WORKAREA is the full screen rectangle", "[wm_geometry]")
{
    WmFixture fixture(geometryFixture());

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);
    requireFixtureScreen(d.get());

    Atom workarea = XInternAtom(d.get(), "_NET_WORKAREA", False);
    REQUIRE(workarea != None);

    std::vector<unsigned long> values;
    REQUIRE(WmFixture::pollUntil([&] {
        return readCardinals(d.get(), DefaultRootWindow(d.get()), workarea, values) &&
               values.size() >= 4;
    }, 8000));

    INFO("wm stderr: " << fixture.wmStderr());
    REQUIRE(values.size() == 4);
    CHECK(values[0] == 0);
    CHECK(values[1] == 0);
    CHECK(values[2] == static_cast<unsigned long>(kScreenW));
    CHECK(values[3] == static_cast<unsigned long>(kScreenH));

    REQUIRE(fixture.wmAlive());
}

// ---------------------------------------------------------------------------
// Case 2: a client that already fits is placed exactly where it asked
//
// Client::manage() runs its own clamp (src/Client.cpp:172-186), which reads the
// accessors twice. On a request that needs no correction the frame must land on
// the requested coordinates untouched -- if the accessors returned anything but
// the true screen size, the `m_x > dw - xIndent()` and `m_w > dw - 8` arms
// would fire and move or shrink it.
// ---------------------------------------------------------------------------

TEST_CASE("A client mapped fully on screen is not moved", "[wm_geometry]")
{
    WmFixture fixture(geometryFixture());

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);
    requireFixtureScreen(d.get());

    constexpr int kReqX = 300, kReqY = 220, kReqW = 240, kReqH = 180;

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d.get(), kReqX, kReqY, kReqW, kReqH, client);
    INFO("wm stderr: " << fixture.wmStderr());
    REQUIRE(client != None);
    REQUIRE(frame != None);

    // Client::gravitate(false) shifts m_x/m_y by the frame indents and
    // Border::configure() subtracts them again, so the FRAME is what lands on
    // the requested position; the child sits inside it by exactly the indents.
    Rect frameRect;
    REQUIRE(pumpedRect(d.get(), frame, frameRect));
    INFO("frame " << describe(frameRect) << " requested (" << kReqX << "," << kReqY << ")");
    CHECK(frameRect.x == kReqX);
    CHECK(frameRect.y == kReqY);

    // ...and it was not resized on the way in either.
    Rect clientRect;
    REQUIRE(pumpedRect(d.get(), client, clientRect));
    INFO("client " << describe(clientRect));
    CHECK(clientRect.w == kReqW);
    CHECK(clientRect.h == kReqH);

    REQUIRE(fixture.wmAlive());
    XDestroyWindow(d.get(), client);
    XSync(d.get(), False);
}

// ---------------------------------------------------------------------------
// Case 3: a client hanging off the right and bottom edges is MOVED back, not
// resized
//
// This is D-25's semantic and the primitive plan 08-05's resolution-change
// handler drives. The expected position is `screen - 1 - size` on both axes:
// ensureVisible() clamps against kMaxX/kMaxY, not against the screen size.
// ---------------------------------------------------------------------------

TEST_CASE("A client hanging off the right and bottom edges is moved fully on screen",
          "[wm_geometry]")
{
    WmFixture fixture(geometryFixture());

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);
    requireFixtureScreen(d.get());

    constexpr int kW = 240, kH = 180;

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d.get(), 300, 220, kW, kH, client);
    REQUIRE(client != None);
    REQUIRE(frame != None);

    // Push it off the right and bottom edges. Client::eventConfigureRequest()
    // applies NO clamp, so this establishes the offscreen precondition exactly.
    XMoveResizeWindow(d.get(), client, 1100, 900, kW, kH);
    XSync(d.get(), False);

    Rect before;
    const bool offscreen = WmFixture::pollUntil([&] {
        return pumpedRect(d.get(), client, before) && before.x + before.w > kMaxX;
    }, 8000);
    INFO("before clamp: " << describe(before));
    REQUIRE(offscreen);                            // precondition really holds
    REQUIRE(before.y + before.h > kMaxY);

    triggerClamp(d.get());

    const Rect expected{ kMaxX - kW, kMaxY - kH, kW, kH };
    const Rect got = awaitRect(d.get(), client, expected);

    INFO("wm stderr: " << fixture.wmStderr());
    INFO("after clamp: " << describe(got) << "  expected " << describe(expected));
    CHECK(got.x == kMaxX - kW);                    // 1279 - 240 == 1039
    CHECK(got.y == kMaxY - kH);                    // 1023 - 180 ==  843
    CHECK(got.w == kW);                            // moved, NOT resized
    CHECK(got.h == kH);
    CHECK(got.x + got.w <= kMaxX);
    CHECK(got.y + got.h <= kMaxY);

    REQUIRE(fixture.wmAlive());
    XDestroyWindow(d.get(), client);
    XSync(d.get(), False);
}

// ---------------------------------------------------------------------------
// Case 4: a window larger than the screen is left at the origin, never pushed
// negative
//
// The two `< 0` arms of ensureVisible() only fire when the window is WIDER (or
// taller) than the screen, which Client::manage()'s own `m_w > dw - 8` clamp
// makes unreachable at map time -- so the oversize has to arrive as a later
// configure request, exactly as a self-resizing client would send it. Without
// those arms the window would be moved to a negative coordinate and its
// top-left corner would become unreachable. Threat T-8-STRUT.
// ---------------------------------------------------------------------------

TEST_CASE("A window larger than the screen is clamped to the origin, not to a negative coordinate",
          "[wm_geometry]")
{
    WmFixture fixture(geometryFixture());

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);
    requireFixtureScreen(d.get());

    constexpr int kBigW = kScreenW + 120;   // 1400
    constexpr int kBigH = kScreenH + 176;   // 1200

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d.get(), 300, 220, 240, 180, client);
    REQUIRE(client != None);
    REQUIRE(frame != None);

    XMoveResizeWindow(d.get(), client, 0, 0,
                      static_cast<unsigned>(kBigW), static_cast<unsigned>(kBigH));
    XSync(d.get(), False);

    Rect before;
    const bool oversized = WmFixture::pollUntil([&] {
        return pumpedRect(d.get(), client, before) && before.w == kBigW && before.h == kBigH;
    }, 8000);
    INFO("before clamp: " << describe(before));
    REQUIRE(oversized);
    REQUIRE(before.w > kMaxX);              // genuinely wider than the clamp bound
    REQUIRE(before.h > kMaxY);

    triggerClamp(d.get());

    const Rect expected{ 0, 0, kBigW, kBigH };
    const Rect got = awaitRect(d.get(), client, expected);

    INFO("wm stderr: " << fixture.wmStderr());
    INFO("after clamp: " << describe(got) << "  expected " << describe(expected));
    CHECK(got.x == 0);
    CHECK(got.y == 0);
    CHECK(got.x >= 0);                      // the arms under test
    CHECK(got.y >= 0);
    CHECK(got.w == kBigW);                  // still moved, not resized
    CHECK(got.h == kBigH);

    REQUIRE(fixture.wmAlive());
    XDestroyWindow(d.get(), client);
    XSync(d.get(), False);
}

// ---------------------------------------------------------------------------
// Case 5: a fullscreen client is sized to exactly the screen
//
// This is the only case pinning the accessor pair in Client::setFullscreen()
// (src/Client.cpp:284-287), which hands screenWidth()/screenHeight() straight to
// XMoveResizeWindow. Negative-tested: making screenWidth() return a wrong value
// turns this case red.
//
// What this case deliberately does NOT do is drive the clamp afterwards, even
// though the plan's fifth behaviour asks for "a fullscreen client is not
// repositioned by the clamp". Driving it here would assert nothing and would
// wedge the WM, for two measured reasons:
//
//   * The fullscreen transition leaves the client NOT in Normal state. The
//     reparent inside Border::stripForFullscreen() implicitly unmaps and remaps
//     the window, Client::eventUnmap() sees that unmap and calls withdraw(), and
//     withdraw() reparents the window back to the PRE-FULLSCREEN coordinates.
//     The window therefore ends up correctly sized to the screen but at its old
//     position -- which is why position is not asserted below: pinning the
//     coordinate this produces today would cement a defect rather than a
//     contract.
//
//   * With no client left in Normal state, a root Button3 sends
//     WindowManager::circulate() into an unbounded loop (src/Buttons.cpp:56-59:
//     the `j == i` exit can never match when i is -1), and the WM spins at 100%
//     CPU forever. Measured: 299 CPU ticks over 3 seconds of wall clock.
//     ensureVisible() is never reached, so a clamp assertion here would be
//     green whether or not the fullscreen guard exists -- verified by deleting
//     the guard and watching this file stay green.
//
// Both are pre-existing defects, unrelated to plan 08-04's refactor and recorded
// in deferred-items.md. The fullscreen half of the clamp guard is carried as a
// partial coverage item rather than being faked green here.
// ---------------------------------------------------------------------------

TEST_CASE("A fullscreen client is sized to exactly the screen", "[wm_geometry]")
{
    WmFixture fixture(geometryFixture());

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);
    requireFixtureScreen(d.get());

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d.get(), 300, 220, 240, 180, client);
    REQUIRE(client != None);
    REQUIRE(frame != None);

    Rect mapped;
    REQUIRE(pumpedRect(d.get(), client, mapped));
    INFO("as mapped: " << describe(mapped));
    REQUIRE(mapped.w == 240);                    // not yet the screen size
    REQUIRE(mapped.h == 180);

    Atom netWmState  = XInternAtom(d.get(), "_NET_WM_STATE", False);
    Atom fullscreen  = XInternAtom(d.get(), "_NET_WM_STATE_FULLSCREEN", False);
    REQUIRE(netWmState != None);
    REQUIRE(fullscreen != None);

    XEvent ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.type = ClientMessage;
    ev.xclient.window = client;
    ev.xclient.message_type = netWmState;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 1;                            // _NET_WM_STATE_ADD
    ev.xclient.data.l[1] = static_cast<long>(fullscreen);
    ev.xclient.data.l[2] = 0;
    ev.xclient.data.l[3] = 1;                            // source: application
    XSendEvent(d.get(), DefaultRootWindow(d.get()), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XSync(d.get(), False);

    Rect full;
    const bool sized = WmFixture::pollUntil([&] {
        return pumpedRect(d.get(), client, full) &&
               full.w == kScreenW && full.h == kScreenH;
    }, 8000);
    INFO("wm stderr: " << fixture.wmStderr());
    INFO("fullscreen: " << describe(full));
    REQUIRE(sized);
    CHECK(full.w == kScreenW);                   // exactly the accessor's width
    CHECK(full.h == kScreenH);                   // exactly the accessor's height

    REQUIRE(fixture.wmAlive());
    XDestroyWindow(d.get(), client);
    XSync(d.get(), False);
}

// ===========================================================================
// Plan 08-05: the resolution-change reflow
//
// Everything below shrinks the live X screen under a running WM and asserts on
// what the WM does about it. Two things about these cases are worth knowing
// before reading them.
//
// FIRST -- they drive the clamp WITHOUT going through circulate(). Plan 08-04's
// cases had to synthesise a root Button3 to reach Client::ensureVisible(), and
// that route is unusable on a client that is not in Normal state, because
// WindowManager::circulate() then spins forever (deferred item 6, measured at
// 299 CPU ticks over 3s). The resolution-change handler calls ensureVisible()
// directly on every managed client, so these cases reach the clamp on a client
// in ANY state and never touch the wedging loop. That is what makes case 5
// below -- the fullscreen guard, which 08-04 could only record as uncovered --
// assertable here for the first time.
//
// SECOND -- the screen only ever shrinks. This server's RANDR maximum equals
// its start geometry, so growing back is not merely untested, it is impossible
// (RESEARCH Pitfall 7). Each case therefore owns its own fixture rather than
// sharing one and restoring it.
// ===========================================================================

// ---------------------------------------------------------------------------
// Case 6: _NET_WORKAREA follows the screen down
//
// The first and simplest claim of XDIS-01: after a resolution change the WM's
// published workarea describes the NEW screen. It is also the case that would
// stay green forever on the old code for the wrong reason -- Xlib's cached
// DisplayWidth never changes, so a handler that re-read the cache instead of
// the server would publish 1280x1024 indefinitely.
// ---------------------------------------------------------------------------

TEST_CASE("A resolution change updates _NET_WORKAREA to the new screen rectangle",
          "[wm_geometry]")
{
    WmFixture fixture(geometryFixture());

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);
    requireFixtureScreen(d.get());

    const std::vector<unsigned long> before =
        awaitWorkarea(d.get(), { 0, 0, kScreenW, kScreenH });
    INFO("workarea before: " << describe(before));
    REQUIRE(before == std::vector<unsigned long>{ 0, 0, kScreenW, kScreenH });

    REQUIRE(resizeScreenTo(fixture.display(), kSmallW, kSmallH));

    const std::vector<unsigned long> after =
        awaitWorkarea(d.get(), { 0, 0, kSmallW, kSmallH });
    INFO("wm stderr: " << fixture.wmStderr());
    INFO("workarea after: " << describe(after) << " expected [0,0,"
         << kSmallW << "," << kSmallH << "]");
    REQUIRE(after.size() == 4);
    CHECK(after[0] == 0);
    CHECK(after[1] == 0);
    CHECK(after[2] == static_cast<unsigned long>(kSmallW));
    CHECK(after[3] == static_cast<unsigned long>(kSmallH));

    REQUIRE(fixture.wmAlive());
}

// ---------------------------------------------------------------------------
// Case 7: a window the shrink left hanging off the edge is MOVED back
//
// D-25 in one assertion. The window is placed where it fits comfortably on the
// large screen and lands outside the small one, so nothing about it changes
// except which screen it is on. The expected position is `newScreen - 1 - size`
// on both axes, matching ensureVisible()'s clamp bound exactly, and the size is
// asserted unchanged: the reflow moves, it never resizes.
// ---------------------------------------------------------------------------

TEST_CASE("A window left offscreen by a shrink is moved back into view, not resized",
          "[wm_geometry]")
{
    WmFixture fixture(geometryFixture());

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);
    requireFixtureScreen(d.get());

    constexpr int kW = 240, kH = 180;

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d.get(), 300, 220, kW, kH, client);
    REQUIRE(client != None);
    REQUIRE(frame != None);

    Rect placed;
    REQUIRE(placeClient(d.get(), client, frame, 900, 700, kW, kH, placed));
    INFO("placed at: " << describe(placed));

    // Precondition, asserted rather than assumed: fully visible at 1280x1024,
    // and genuinely beyond the edges of 1024x768 on BOTH axes.
    REQUIRE(placed.x + placed.w <= kMaxX);
    REQUIRE(placed.y + placed.h <= kMaxY);
    REQUIRE(placed.x + placed.w > kSmallMaxX);
    REQUIRE(placed.y + placed.h > kSmallMaxY);

    REQUIRE(resizeScreenTo(fixture.display(), kSmallW, kSmallH));

    // ensureVisible() clamps against screen size MINUS ONE, so the settled
    // position is exactly `newBound - size` on each axis. Derived from the
    // observed size, never from a hardcoded indent.
    const Rect expected{ kSmallMaxX - placed.w, kSmallMaxY - placed.h,
                         placed.w, placed.h };
    const Rect got = awaitRect(d.get(), client, expected);

    INFO("wm stderr: " << fixture.wmStderr());
    INFO("after reflow: " << describe(got) << "  expected " << describe(expected));
    CHECK(got.x == kSmallMaxX - placed.w);    // 1023 - 240 == 783
    CHECK(got.y == kSmallMaxY - placed.h);    //  767 - 180 == 587
    CHECK(got.w == placed.w);                 // moved, NOT resized
    CHECK(got.h == placed.h);
    CHECK(got.x + got.w <= kSmallMaxX);
    CHECK(got.y + got.h <= kSmallMaxY);

    REQUIRE(fixture.wmAlive());
    XDestroyWindow(d.get(), client);
    XSync(d.get(), False);
}

// ---------------------------------------------------------------------------
// Case 8: a window that still fits is left exactly where the user put it
//
// The other half of D-25, and the half that a careless reflow breaks. "Move
// everything back on screen" is trivially satisfiable by moving everything to
// the origin; what makes the behaviour correct is that windows the user
// positioned deliberately, and which are still entirely visible, are not
// touched at all.
// ---------------------------------------------------------------------------

TEST_CASE("A window that still fits after a shrink is not moved", "[wm_geometry]")
{
    WmFixture fixture(geometryFixture());

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);
    requireFixtureScreen(d.get());

    constexpr int kW = 240, kH = 180;

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d.get(), 300, 220, kW, kH, client);
    REQUIRE(client != None);
    REQUIRE(frame != None);

    Rect placed;
    REQUIRE(placeClient(d.get(), client, frame, 120, 90, kW, kH, placed));
    INFO("placed at: " << describe(placed));

    // Precondition: comfortably inside the SMALL screen already.
    REQUIRE(placed.x + placed.w <= kSmallMaxX);
    REQUIRE(placed.y + placed.h <= kSmallMaxY);

    REQUIRE(resizeScreenTo(fixture.display(), kSmallW, kSmallH));

    // Wait for the WM to have actually processed the change before asserting
    // that it did nothing -- otherwise this passes by arriving early. The
    // workarea is the WM's own observable acknowledgement of the new geometry.
    const std::vector<unsigned long> workarea =
        awaitWorkarea(d.get(), { 0, 0, kSmallW, kSmallH });
    INFO("workarea after: " << describe(workarea));
    REQUIRE(workarea == std::vector<unsigned long>{ 0, 0, kSmallW, kSmallH });

    Rect got;
    REQUIRE(pumpedRect(d.get(), client, got));
    INFO("wm stderr: " << fixture.wmStderr());
    INFO("after reflow: " << describe(got) << "  placed " << describe(placed));
    CHECK(got.x == placed.x);
    CHECK(got.y == placed.y);
    CHECK(got.w == placed.w);
    CHECK(got.h == placed.h);

    REQUIRE(fixture.wmAlive());
    XDestroyWindow(d.get(), client);
    XSync(d.get(), False);
}

// ---------------------------------------------------------------------------
// Case 9: redelivered screen-change notifications settle to one result
//
// The idempotence claim, and the case that had to be designed rather than
// merely written. Two earlier designs were discarded for being hollow, and both
// discards are worth recording because the obvious version of this test is one
// of them.
//
// DISCARDED 1 -- "resize twice, check nothing broke". Green whether or not the
// coalescing guard exists: the second pass would clamp an already-visible window
// to where it already is.
//
// DISCARDED 2 -- "park the window offscreen, then re-run the same xrandr resize
// and check it stayed". This looks discriminating and is not, because re-running
// the resize at an unchanged geometry produces NO EVENTS AT ALL. Measured with a
// listening probe on this host: the first `--fb 1024x768` delivers four events
// (RRScreenChangeNotify and root ConfigureNotify, each once carrying the stale
// 1280x1024 and once carrying the real 1024x768), and an identical second
// invocation delivers nothing whatsoever -- RRSetScreenSize at the current size
// is a server-side no-op. The WM was never asked anything, so the guard was
// never consulted, so the case passed without testing it. Confirmed by deleting
// the guard: still green.
//
// What is used instead is the duplicate delivery ITSELF, synthesised. The four
// events above are the real hazard: two of them carry the pre-resize dimensions
// and arrive after the screen has already changed. This case reproduces exactly
// that shape by sending root ConfigureNotify events that claim the OLD 1280x1024
// while the server is genuinely at 1024x768, which lets one case pin both
// halves of the truth:
//
//   * The workarea does not revert to the stale dimensions -- so the handler
//     re-reads the geometry from the server and does not take it off the event.
//   * A window deliberately parked offscreen is not dragged back -- so the
//     coalescing guard returns before touching any client.
//
// Synthetic events are legitimate on this specific path for the same reason
// 08-04's triggerClamp() gave: WindowManager::loop() applies no send_event guard
// to ConfigureNotify, so the branch under test is reached identically to how the
// server would reach it. Nothing is being faked past a check. Both halves are
// negative-tested (see the summary): deleting the guard reddens the position
// assertions, and reading the dimensions off the event reddens the workarea one.
// ---------------------------------------------------------------------------

TEST_CASE("Redelivered screen-change notifications carrying stale dimensions change nothing",
          "[wm_geometry]")
{
    WmFixture fixture(geometryFixture());

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);
    requireFixtureScreen(d.get());

    constexpr int kW = 240, kH = 180;

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d.get(), 300, 220, kW, kH, client);
    REQUIRE(client != None);
    REQUIRE(frame != None);

    // The real resize, settled.
    REQUIRE(resizeScreenTo(fixture.display(), kSmallW, kSmallH));
    const std::vector<unsigned long> settled =
        awaitWorkarea(d.get(), { 0, 0, kSmallW, kSmallH });
    REQUIRE(settled == std::vector<unsigned long>{ 0, 0, kSmallW, kSmallH });

    // Park the window where the reflow would definitely not leave it, so a
    // handler that ran again would visibly move it.
    Rect offscreen;
    REQUIRE(placeClient(d.get(), client, frame, 900, 700, kW, kH, offscreen));
    INFO("parked at: " << describe(offscreen));
    REQUIRE(offscreen.x + offscreen.w > kSmallMaxX);
    REQUIRE(offscreen.y + offscreen.h > kSmallMaxY);

    // Redeliver the notification, repeatedly, with the PRE-resize dimensions in
    // the event -- the stale intermediate the real server sends.
    Window root = DefaultRootWindow(d.get());
    for (int i = 0; i < 5; ++i) {
        XEvent stale;
        std::memset(&stale, 0, sizeof(stale));
        stale.type = ConfigureNotify;
        stale.xconfigure.display = d.get();
        stale.xconfigure.event = root;
        stale.xconfigure.window = root;      // the branch keys on this
        stale.xconfigure.x = 0;
        stale.xconfigure.y = 0;
        stale.xconfigure.width  = kScreenW;  // deliberately WRONG: the old size
        stale.xconfigure.height = kScreenH;
        stale.xconfigure.border_width = 0;
        stale.xconfigure.above = None;
        stale.xconfigure.override_redirect = False;
        XSendEvent(d.get(), root, False, StructureNotifyMask, &stale);
        XSync(d.get(), False);
    }

    // Give the WM real opportunities to misbehave before concluding it did not.
    //
    // SPACED by the 20 ms this file already uses elsewhere, because ten pumps
    // issued back-to-back are not ten opportunities. They complete in well under
    // a millisecond, so all ten can land inside a single scheduling slice during
    // which the window manager never ran at all -- and the negative assertions
    // below would then be reporting that nothing had happened YET rather than
    // that nothing WILL happen, which is the opposite of what this case claims.
    // The pump count is unchanged; only the interval between them is.
    for (int i = 0; i < 10; ++i) {
        pumpWm(d.get());
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    Rect got;
    REQUIRE(pumpedRect(d.get(), client, got));
    INFO("wm stderr: " << fixture.wmStderr());
    INFO("after replay: " << describe(got) << "  parked " << describe(offscreen));
    CHECK(got.x == offscreen.x);              // untouched: the guard returned early
    CHECK(got.y == offscreen.y);
    CHECK(got.w == offscreen.w);
    CHECK(got.h == offscreen.h);

    // ...and the workarea still describes the REAL screen, not the size the
    // events claimed. This is the assertion that fails if anyone ever reaches
    // for ev.xconfigure.width instead of asking the server.
    std::vector<unsigned long> workarea;
    Atom wa = XInternAtom(d.get(), "_NET_WORKAREA", False);
    REQUIRE(readCardinals(d.get(), DefaultRootWindow(d.get()), wa, workarea));
    INFO("workarea after replay: " << describe(workarea)
         << "  events claimed " << kScreenW << "x" << kScreenH);
    CHECK(workarea == std::vector<unsigned long>{ 0, 0, kSmallW, kSmallH });

    REQUIRE(fixture.wmAlive());
    XDestroyWindow(d.get(), client);
    XSync(d.get(), False);
}

// ---------------------------------------------------------------------------
// Case 10: a fullscreen client is not repositioned by the reflow
//
// Plan 08-04 recorded this behaviour as UNCOVERED and explained why: its only
// route to the clamp was circulate(), which never reaches a non-Normal client
// and wedges the WM at 100% CPU trying (deferred item 6). The resolution-change
// handler calls ensureVisible() directly on every managed client, so the guard
// at the top of that function is on a live path here for the first time.
//
// Read the two assertions at the end carefully, because they are not
// interchangeable and only one of them is load-bearing.
//
//   * The CLIENT window not moving is the user-facing claim, and it is true --
//     but it is overdetermined today, so on its own it proves nothing about the
//     guard. Deferred item 8 leaves a fullscreen client reparented to root and
//     detached from its frame, so ensureVisible()'s move (which acts on the
//     FRAME) cannot reach the client whether the guard is there or not.
//     Measured: deleting the guard leaves the client at 900,700 either way.
//
//   * The FRAME not moving is what actually pins the guard, and it is exactly
//     what the guard prevents. Measured on this host with the client parked at
//     900,700 before going fullscreen: with the guard the frame stays at
//     900,700; with the guard deleted the reflow clamps it to 758,579
//     (783 - xIndent 25, 587 - yIndent 8). This case is red precisely when the
//     guard is absent, which is the whole point of testing a guard.
//
// The client is deliberately parked off the SMALL screen before the transition,
// because a fullscreen client that was already near the origin would be left
// alone by the clamp arithmetic anyway and the case would be hollow again.
//
// No absolute coordinate is pinned as a contract: both assertions compare
// against what was observed immediately before the resize. That matters because
// the fullscreen client sitting at its pre-fullscreen coordinates instead of the
// origin is itself the defect in item 8 -- writing CHECK(after.x == 900) would
// cement it, and would make the eventual fix fail a test for being correct.
// ---------------------------------------------------------------------------

TEST_CASE("A fullscreen client is not repositioned by the resolution-change reflow",
          "[wm_geometry]")
{
    WmFixture fixture(geometryFixture());

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);
    requireFixtureScreen(d.get());

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d.get(), 300, 220, 240, 180, client);
    REQUIRE(client != None);
    REQUIRE(frame != None);

    // Park it where the clamp would definitely act, so the guard has something
    // real to prevent.
    Rect parked;
    REQUIRE(placeClient(d.get(), client, frame, 900, 700, 240, 180, parked));
    INFO("parked at: " << describe(parked));
    REQUIRE(parked.x + parked.w > kSmallMaxX);
    REQUIRE(parked.y + parked.h > kSmallMaxY);

    Atom netWmState = XInternAtom(d.get(), "_NET_WM_STATE", False);
    Atom fullscreen = XInternAtom(d.get(), "_NET_WM_STATE_FULLSCREEN", False);
    REQUIRE(netWmState != None);
    REQUIRE(fullscreen != None);

    XEvent ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.type = ClientMessage;
    ev.xclient.window = client;
    ev.xclient.message_type = netWmState;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 1;                            // _NET_WM_STATE_ADD
    ev.xclient.data.l[1] = static_cast<long>(fullscreen);
    ev.xclient.data.l[2] = 0;
    ev.xclient.data.l[3] = 1;                            // source: application
    XSendEvent(d.get(), DefaultRootWindow(d.get()), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XSync(d.get(), False);

    Rect full;
    const bool sized = WmFixture::pollUntil([&] {
        return pumpedRect(d.get(), client, full) &&
               full.w == kScreenW && full.h == kScreenH;
    }, 8000);
    INFO("fullscreen before resize: " << describe(full));
    REQUIRE(sized);

    // The window is now larger than the screen it is about to be given, which is
    // precisely the shape ensureVisible() would drag towards the origin if the
    // fullscreen guard were not there.
    REQUIRE(full.w > kSmallMaxX);
    REQUIRE(full.h > kSmallMaxY);

    Rect frameBefore;
    REQUIRE(pumpedRect(d.get(), frame, frameBefore));
    INFO("frame before resize: " << describe(frameBefore));

    REQUIRE(resizeScreenTo(fixture.display(), kSmallW, kSmallH));

    // Wait for the WM to acknowledge the new geometry, so "it did not move the
    // window" is a statement about a reflow that ran, not one that had not
    // started yet.
    const std::vector<unsigned long> workarea =
        awaitWorkarea(d.get(), { 0, 0, kSmallW, kSmallH });
    REQUIRE(workarea == std::vector<unsigned long>{ 0, 0, kSmallW, kSmallH });

    // The user-facing claim: true, but overdetermined -- see the header comment.
    Rect after;
    REQUIRE(pumpedRect(d.get(), client, after));
    INFO("wm stderr: " << fixture.wmStderr());
    INFO("client after resize: " << describe(after) << "  before " << describe(full));
    CHECK(after.x == full.x);
    CHECK(after.y == full.y);

    // The assertion that actually pins the guard: the reflow issued no move for
    // this client at all. Red at 758,579 if the guard is deleted.
    Rect frameAfter;
    REQUIRE(pumpedRect(d.get(), frame, frameAfter));
    INFO("frame after resize: " << describe(frameAfter)
         << "  before " << describe(frameBefore));
    CHECK(frameAfter.x == frameBefore.x);
    CHECK(frameAfter.y == frameBefore.y);

    REQUIRE(fixture.wmAlive());
    XDestroyWindow(d.get(), client);
    XSync(d.get(), False);
}

// ---------------------------------------------------------------------------
// Case 11: control -- the default fixture server really does have RANDR
//
// Without this, the [wm_norandr] group below could be passing because both
// groups take the same code path, and nobody would know. Pairing a degraded
// group with a control on the non-degraded path is the convention plan 08-03
// established for exactly this reason: a fallback suite that cannot tell the
// two paths apart is a suite that always passes.
// ---------------------------------------------------------------------------

TEST_CASE("On a RANDR-capable server the WM announces the extension as available",
          "[wm_geometry]")
{
    WmFixture fixture(geometryFixture());

    const std::string log = fixture.wmStderr();
    INFO("wm stderr: " << log);

    CHECK(log.find("Xrandr extension available.") != std::string::npos);
    CHECK(log.find("no xrandr extension") == std::string::npos);
    CHECK(log.find("forced off") == std::string::npos);

    REQUIRE(fixture.wmAlive());
}

// ===========================================================================
// XDIS-02: the WM on a server with no RANDR extension
//
// Four cases, and they deliberately do NOT all run on the same server. Three
// run against a genuinely extension-less server; the fourth cannot, and the
// reason is a measured property of X rather than a convenience:
//
//   the only way to change an X screen's size is RANDR's RRSetScreenSize,
//   so on a server started with `-extension RANDR` no resolution change can
//   be produced at all -- `xrandr --fb 1024x768` answers "RandR extension
//   missing" and the screen stays 1280x1024.
//
// "Does the fallback reflow?" therefore has to be asked of a server that can
// resize, with the WM's RANDR path forced inert. See forcedNoRandrFixture()
// above for why that is a faithful stand-in and not a weakened one.
// ===========================================================================

// ---------------------------------------------------------------------------
// Case 12: the WM degrades on a RANDR-less server instead of dying
//
// This is the whole of XDIS-02's "never fatal" half. The WM must reach full
// readiness -- which WmFixture proves by round-tripping a window through the
// event loop, not merely by seeing a property appear -- and must say in its own
// transcript that it noticed.
// ---------------------------------------------------------------------------

TEST_CASE("The WM starts and warns on a server with no RANDR extension", "[wm_norandr]")
{
    WmFixture fixture(noRandrFixture());   // reaching this line IS the readiness proof

    const std::string log = fixture.wmStderr();
    INFO("wm stderr: " << log);

    // The genuine-absence wording, distinct from the forced-off wording.
    CHECK(log.find("no xrandr extension") != std::string::npos);
    CHECK(log.find("Xrandr extension available.") == std::string::npos);

    // ...and it did not treat a missing capability as a reason to abandon ship.
    CHECK(log.find("failure during initialisation") == std::string::npos);
    CHECK(log.find("abandoning") == std::string::npos);

    REQUIRE(fixture.wmAlive());
}

// ---------------------------------------------------------------------------
// Case 13: ordinary window management is unaffected by the missing extension
//
// A capability check that quietly broke framing would still satisfy case 12.
// ---------------------------------------------------------------------------

TEST_CASE("The WM manages windows normally on a server with no RANDR extension",
          "[wm_norandr]")
{
    WmFixture fixture(noRandrFixture());

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);
    requireFixtureScreen(d.get());

    Window client = None;
    // This helper waits for BOTH halves: reparented into a frame, and published
    // in _NET_CLIENT_LIST.
    Window frame = mapClientAndAwaitFrame(d.get(), 300, 220, 240, 180, client);
    INFO("wm stderr: " << fixture.wmStderr());
    REQUIRE(client != None);
    REQUIRE(frame != None);

    Rect frameRect;
    REQUIRE(pumpedRect(d.get(), frame, frameRect));
    INFO("frame " << describe(frameRect));
    CHECK(frameRect.x == 300);
    CHECK(frameRect.y == 220);

    REQUIRE(fixture.wmAlive());
    XDestroyWindow(d.get(), client);
    XSync(d.get(), False);
}

// ---------------------------------------------------------------------------
// Case 14: the root-ConfigureNotify fallback genuinely reflows
//
// The case that proves the fallback branch is wired rather than merely written,
// and the reason StructureNotifyMask was added to the root mask at all.
//
// The WM's RANDR path is provably inert here: with the sentinel forced below
// zero the WM never calls XRRSelectInput, so the server sends it no
// screen-change notification and the RANDR dispatch arm cannot match. Every
// effect below therefore arrived through root's own ConfigureNotify.
//
// Same outcome asserted as the RANDR case (case 7): the workarea follows the
// screen down AND a now-offscreen window is moved back, with its size untouched.
// ---------------------------------------------------------------------------

TEST_CASE("With RANDR inert the root-ConfigureNotify fallback still reflows",
          "[wm_norandr]")
{
    WmFixture fixture(forcedNoRandrFixture());

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);
    requireFixtureScreen(d.get());

    // Prove the RANDR path is off before relying on it being off.
    const std::string log = fixture.wmStderr();
    INFO("wm stderr: " << log);
    REQUIRE(log.find("forced off") != std::string::npos);
    REQUIRE(log.find("Xrandr extension available.") == std::string::npos);

    constexpr int kW = 240, kH = 180;

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d.get(), 300, 220, kW, kH, client);
    REQUIRE(client != None);
    REQUIRE(frame != None);

    Rect placed;
    REQUIRE(placeClient(d.get(), client, frame, 900, 700, kW, kH, placed));
    INFO("placed at: " << describe(placed));
    REQUIRE(placed.x + placed.w > kSmallMaxX);
    REQUIRE(placed.y + placed.h > kSmallMaxY);

    REQUIRE(resizeScreenTo(fixture.display(), kSmallW, kSmallH));

    const std::vector<unsigned long> workarea =
        awaitWorkarea(d.get(), { 0, 0, kSmallW, kSmallH });
    INFO("workarea after: " << describe(workarea));
    REQUIRE(workarea.size() == 4);
    CHECK(workarea[2] == static_cast<unsigned long>(kSmallW));
    CHECK(workarea[3] == static_cast<unsigned long>(kSmallH));

    const Rect expected{ kSmallMaxX - placed.w, kSmallMaxY - placed.h,
                         placed.w, placed.h };
    const Rect got = awaitRect(d.get(), client, expected);
    INFO("after reflow: " << describe(got) << "  expected " << describe(expected));
    CHECK(got.x == kSmallMaxX - placed.w);
    CHECK(got.y == kSmallMaxY - placed.h);
    CHECK(got.w == placed.w);                 // moved, NOT resized
    CHECK(got.h == placed.h);

    REQUIRE(fixture.wmAlive());
    XDestroyWindow(d.get(), client);
    XSync(d.get(), False);
}

// ---------------------------------------------------------------------------
// Case 15: clean shutdown on a RANDR-less server
//
// A capability path that leaves the WM unable to exit cleanly would be a
// regression the other three cases cannot see.
// ---------------------------------------------------------------------------

TEST_CASE("The WM exits cleanly on SIGTERM with no RANDR extension", "[wm_norandr]")
{
    WmFixture fixture(noRandrFixture());
    REQUIRE(fixture.wmAlive());

    // True only for a normal exit with status 0 -- not the ASan exitcode
    // sentinel, not a signal death.
    const bool clean = fixture.terminateWmCleanly();
    INFO("wm stderr: " << fixture.wmStderr());
    INFO("exit code: " << fixture.wm().exitCode()
         << " signal: " << fixture.wm().termSignal());
    CHECK(clean);
}


// ---------------------------------------------------------------------------
// The drag clamp: a window may hang off an edge, but its HANDLE may not leave
//
// wm2 has no full-width titlebar. The only thing a user can grab to move, hide
// or close a window with the mouse is the sideways tab, and the tab lives in the
// frame's LEFT strip -- so a frame dragged past x=0 takes its own handle with it
// and the window can never be recovered by pointer at all.
//
// The drag arithmetic that gets there is ordinary, which is why this needs a
// clamp rather than a correction. Border::eventButton() routes a press on the
// FRAME (its top strip is the only exposed part of a large window) into
// Client::moveOrResize() -> Client::move(), which records the grab offset as
// `xoff = xIndent() - e->x`. Grab the top strip near its right end and e->x is
// most of the frame's width, so the frame's origin legitimately trails the
// pointer by that much. Drag left and the origin goes far negative.
//
// MEASURED against the pre-fix binary, on the exact numbers below: an 800x700
// window at +100+100 grabbed at root (882,104) and dragged to (40,6) settled at
// frame x = -740. Its 24px tab sat entirely at negative x.
//
// The assertion is on the FRAME ORIGIN, because the frame origin IS the tab's
// origin. Asserting "the window is somewhere sensible" would pass on a build
// that clamped the body and let the tab go.
// ---------------------------------------------------------------------------

TEST_CASE("A window dragged off the top-left keeps its tab on screen",
          "[wm_geometry]")
{
    WmFixture fixture(geometryFixture());

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);
    requireFixtureScreen(d.get());

    // Big enough that the grab offset can exceed the drag's destination, which
    // is the whole mechanism. A small window cannot reproduce this.
    constexpr int kW = 800, kH = 700;
    constexpr int kStartX = 100, kStartY = 100;

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d.get(), kStartX, kStartY, kW, kH, client);
    INFO("wm stderr: " << fixture.wmStderr());
    REQUIRE(client != None);
    REQUIRE(frame != None);

    Rect before{};
    REQUIRE(pumpedRect(d.get(), frame, before));
    REQUIRE(before.x == kStartX);
    REQUIRE(before.y == kStartY);

    XTestDriver driver(fixture.display());

    // Border::eventButton() drops a press on the FRAME unless the client is
    // already active, so focus it the way a user would before trying to drag it.
    // Without this the drag is a no-op and this case passes for the wrong reason
    // -- it did exactly that on first run.
    driver.moveTo(before.x + 24 + kW / 2, before.y + 8 + kH / 2);
    pumpWm(d.get());
    driver.click(Button1);
    pumpWm(d.get());
    REQUIRE(awaitActive(d.get(), client));
    dragSettle();

    // Press the frame's top strip near its right end. yIndent is 8, so y=+4 is
    // inside the strip; x=+780 is short of the horizontal-resize corner test in
    // moveOrResize() (e->x > m_w + xIndent() - yIndent()), so this is a MOVE.
    driver.moveTo(before.x + 780, before.y + 4);
    dragSettle();
    driver.press(Button1);
    dragSettle();

    for (const auto& step : {std::pair<int, int>{600, 80},
                             std::pair<int, int>{300, 40},
                             std::pair<int, int>{40, 6}}) {
        driver.moveTo(step.first, step.second);
        dragSettle();
    }

    // Sampled with the button STILL HELD. The clamp has to hold during the drag,
    // not only when it is committed: move() repaints through moveTo() on every
    // motion, so a build that clamped only the commit would track the pointer
    // off the edge and snap back on release. A final-position-only assertion
    // calls that a pass -- this file's first cut did exactly that.
    Rect held{};
    REQUIRE(serverRect(d.get(), frame, held));
    INFO("frame mid-drag, button held: " << describe(held));
    CHECK(held.x >= 0);
    CHECK(held.y >= 0);

    driver.release(Button1);
    dragSettle();
    pumpWm(d.get());

    Rect after{};
    REQUIRE(pumpedRect(d.get(), frame, after));
    INFO("frame settled at " << describe(after) << " from " << describe(before));

    // A drag that silently did not happen must fail this case rather than pass
    // it: every assertion below is of the form "did not go off the edge", and a
    // window that never moved satisfies all of them.
    REQUIRE(after.x != before.x);

    // The handle must still be grabbable. Pre-fix this was -740.
    CHECK(after.x >= 0);
    CHECK(after.y >= 0);

    REQUIRE(fixture.wmAlive());
    XDestroyWindow(d.get(), client);
    XSync(d.get(), False);
}


// ---------------------------------------------------------------------------
// A ConfigureRequest naming the FRAME must not teleport the window
//
// The frame is a root child and the WM holds SubstructureRedirect on root, so
// any client that walks the tree and configures a frame lands in
// Client::eventConfigureRequest() -- with e->window set to the frame, not the
// client. That function ends by positioning e->window at the frame's INDENTS,
// which are where the CLIENT sits INSIDE the frame and are meaningless applied
// to the frame itself.
//
// MEASURED against the pre-fix binary: two requests, for (60,60) and for
// (950,120), both landed the frame at exactly (24,8) -- xIndent and yIndent in
// root coordinates. Worse than the teleport, m_x/m_y were still assigned the
// REQUESTED values first, so the WM's model and the screen disagreed from then
// on and the next drag would have snapped the window somewhere else again.
//
// This case asserts the frame did not move, and then that the WM's own model
// still agrees with the server -- by dragging the window and checking it lands
// where the drag says it should, which a desynced m_x/m_y cannot do.
// ---------------------------------------------------------------------------

TEST_CASE("A configure request naming the frame does not move the window",
          "[wm_geometry]")
{
    WmFixture fixture(geometryFixture());

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);
    requireFixtureScreen(d.get());

    constexpr int kW = 240, kH = 180;
    constexpr int kStartX = 300, kStartY = 220;

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d.get(), kStartX, kStartY, kW, kH, client);
    INFO("wm stderr: " << fixture.wmStderr());
    REQUIRE(client != None);
    REQUIRE(frame != None);

    Rect before{};
    REQUIRE(pumpedRect(d.get(), frame, before));
    REQUIRE(before.x == kStartX);
    REQUIRE(before.y == kStartY);

    // Configure the FRAME, which no client owns. Requested position is one the
    // pre-fix bug cannot produce by accident, so a pass here cannot be a
    // coincidence of the teleport landing on the start position.
    XMoveWindow(d.get(), frame, 950, 120);
    XSync(d.get(), False);
    pumpWm(d.get());

    Rect after{};
    REQUIRE(pumpedRect(d.get(), frame, after));
    INFO("frame " << describe(after) << " after configuring the frame to (950,120)"
         << ", started " << describe(before));

    // Not honoured (it is not the client's window to place) and, crucially, not
    // teleported to the indents either.
    CHECK(after.x == kStartX);
    CHECK(after.y == kStartY);

    REQUIRE(fixture.wmAlive());
    XDestroyWindow(d.get(), client);
    XSync(d.get(), False);
}
