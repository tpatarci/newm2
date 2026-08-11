// Geometry tests against the REAL compiled wm2-born-again binary.
//
// Purpose (plan 08-04): pin the values today's implementation produces for every
// computation that consults the D-27 screen-geometry accessors, so that plan
// 08-05 can change *when* those values are recomputed without silently changing
// *what* they are. Each case below therefore asserts an exact number, not a
// range -- a range would survive the very off-by-one this file exists to guard.
//
// Tags in this file:
//   [wm_geometry] -- workarea publication, the map-time clamp, and the
//                    ensureVisible() clamp, on a fixed 1280x1024 screen
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

#include "x11wrap.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <cstring>
#include <string>
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

// Read `w`'s rectangle after waking the WM, so the value reflects everything the
// WM has decided rather than everything it has happened to flush.
bool pumpedRect(Display* d, Window w, Rect& out)
{
    pumpWm(d);
    return serverRect(d, w, out);
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
