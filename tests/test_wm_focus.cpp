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
#include <cstring>
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
// The waiting half of mapClientAndAwaitFrame, split out so the FOCUS-01 cases
// below can set properties on a window BETWEEN creating it and mapping it --
// which is the only correct order for _NET_WM_USER_TIME, since the WM reads it
// during Client::manage() and a property written after the map request would
// race the read it is supposed to govern.
Window awaitFrameFor(Display* d, Window win)
{
    Window root = DefaultRootWindow(d);

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

    return awaitFrameFor(d, win);
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

// ===========================================================================
// FOCUS-01 (plan 08-08) helpers: focus-stealing prevention
// ===========================================================================

// A timestamp from the SERVER's clock, obtained the same way
// WindowManager::timestamp() obtains one: append zero bytes to a property and
// read the time off the resulting PropertyNotify. The tests cannot invent
// timestamps, because "stale" and "fresh" are only meaningful relative to the
// same clock the WM is comparing against.
Time serverTime(Display* d)
{
    XSetWindowAttributes attr;
    attr.override_redirect = True;
    Window w = XCreateWindow(d, DefaultRootWindow(d), -50, -50, 1, 1, 0,
                             CopyFromParent, InputOutput, CopyFromParent,
                             CWOverrideRedirect, &attr);
    XSelectInput(d, w, PropertyChangeMask);

    Atom probe = XInternAtom(d, "_WM2TEST_TIME_PROBE", False);
    XChangeProperty(d, w, probe, XA_STRING, 8, PropModeAppend, nullptr, 0);

    XEvent ev;
    XWindowEvent(d, w, PropertyChangeMask, &ev);
    const Time t = ev.xproperty.time;

    XDestroyWindow(d, w);
    XSync(d, False);
    return t;
}

void setUserTimeProp(Display* d, Window w, Time t)
{
    static Atom a = None;
    if (a == None) a = XInternAtom(d, "_NET_WM_USER_TIME", False);
    unsigned long v = static_cast<unsigned long>(t);
    XChangeProperty(d, w, a, XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&v), 1);
}

void setUserTimeWindowProp(Display* d, Window w, Window proxy)
{
    static Atom a = None;
    if (a == None) a = XInternAtom(d, "_NET_WM_USER_TIME_WINDOW", False);
    unsigned long v = static_cast<unsigned long>(proxy);
    XChangeProperty(d, w, a, XA_WINDOW, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&v), 1);
}

// True when `w` currently advertises _NET_WM_STATE_DEMANDS_ATTENTION. This is
// the observable half of "the refusal was not silent": a refused window must be
// discoverable as wanting the user, not merely absent from the focus.
bool hasDemandsAttention(Display* d, Window w)
{
    static Atom stateAtom = None, demands = None;
    if (stateAtom == None) stateAtom = XInternAtom(d, "_NET_WM_STATE", False);
    if (demands == None) demands = XInternAtom(d, "_NET_WM_STATE_DEMANDS_ATTENTION", False);

    Atom actualType = None;
    int actualFormat = 0;
    unsigned long nItems = 0, bytesAfter = 0;
    unsigned char* raw = nullptr;
    if (XGetWindowProperty(d, w, stateAtom, 0, 64, False, XA_ATOM,
                           &actualType, &actualFormat, &nItems, &bytesAfter,
                           &raw) != Success) {
        return false;
    }
    bool found = false;
    if (raw && actualType == XA_ATOM && actualFormat == 32) {
        Atom* vals = reinterpret_cast<Atom*>(raw);
        for (unsigned long i = 0; i < nItems; ++i) if (vals[i] == demands) found = true;
    }
    if (raw) XFree(raw);
    return found;
}

// The ICCCM half of the same signal. Checked alongside the EWMH state because
// they are set and cleared together and a implementation that published one
// without the other would leave half the desktop unable to see the hint.
bool hasUrgencyHint(Display* d, Window w)
{
    XWMHints* h = XGetWMHints(d, w);
    if (!h) return false;
    const bool urgent = (h->flags & XUrgencyHint) != 0;
    XFree(h);
    return urgent;
}

// Advance the WM's last-user-interaction clock with a REAL button press, and
// return a server timestamp taken afterwards.
//
// Button2 on the root window is chosen deliberately: WindowManager::eventButton
// feeds the clock at its very top, before dispatch, and Button2 is the one root
// button with nothing bound to it (Button1 opens the root menu and takes a modal
// pointer grab; Button3 circulates). So this advances the clock and perturbs
// nothing else. It must be called while the pointer is parked on root.
Time clickRootAndReadClock(Display* d, XTestDriver& driver)
{
    driver.click(Button2);
    settleWm(d);
    return serverTime(d);
}

struct UserTime {
    enum class Mode { Absent, OnToplevel, OnProxy };
    Mode mode = Mode::Absent;
    Time value = 0;
};

// Create a client, publish its user-time the way a real application would
// (BEFORE mapping), map it, and wait for the WM to frame it.
//
// A refused window is still mapped, still framed and still fully managed -- that
// is what "refusal" means here, per the plan: mapped in the background with a
// hint, never "not shown". So this helper is correct for the granted and the
// refused case alike, and a failure to frame is a real failure either way.
Window mapClientWithUserTime(Display* d, int x, int y, int w, int h,
                             const UserTime& ut, Window& clientOut,
                             Window* proxyOut = nullptr)
{
    Window root = DefaultRootWindow(d);
    Window win = XCreateSimpleWindow(d, root, x, y,
                                     static_cast<unsigned>(w), static_cast<unsigned>(h), 0,
                                     BlackPixel(d, DefaultScreen(d)),
                                     WhitePixel(d, DefaultScreen(d)));
    clientOut = win;

    if (ut.mode == UserTime::Mode::OnToplevel) {
        setUserTimeProp(d, win, ut.value);
    } else if (ut.mode == UserTime::Mode::OnProxy) {
        // A real client's user-time window is a never-mapped window it owns, so
        // it can rewrite the timestamp without generating PropertyNotify traffic
        // on the toplevel. Override-redirect and never mapped: the WM sees a
        // CreateNotify and nothing else, so it is never managed.
        XSetWindowAttributes attr;
        attr.override_redirect = True;
        Window proxy = XCreateWindow(d, root, -60, -60, 1, 1, 0,
                                     CopyFromParent, InputOutput, CopyFromParent,
                                     CWOverrideRedirect, &attr);
        setUserTimeProp(d, proxy, ut.value);
        setUserTimeWindowProp(d, win, proxy);
        if (proxyOut) *proxyOut = proxy;
    }

    XMapWindow(d, win);
    XSync(d, False);

    return awaitFrameFor(d, win);
}

// ---------------------------------------------------------------------------
// FOCUS-01 activation-message helpers (plan 08-08, Task 4)
// ---------------------------------------------------------------------------

// Send a real _NET_ACTIVE_WINDOW request the way a pager or an application does:
// to the ROOT window, with the two substructure masks the EWMH requires, so it
// arrives through the WM's SubstructureRedirect selection rather than being
// delivered straight to the target.
//
// `format` is a parameter only so the malformed-message case can send a wrong
// one; every legitimate caller passes 32.
void sendActivation(Display* d, Window target, long source, Time stamp,
                    int format = 32)
{
    static Atom a = None;
    if (a == None) a = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);

    XEvent ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.xclient.type         = ClientMessage;
    ev.xclient.window       = target;
    ev.xclient.message_type = a;
    ev.xclient.format       = format;
    ev.xclient.data.l[0]    = source;                       // source indication
    ev.xclient.data.l[1]    = static_cast<long>(stamp);     // timestamp
    ev.xclient.data.l[2]    = 0;                            // requestor's active window

    XSendEvent(d, DefaultRootWindow(d), False,
               SubstructureNotifyMask | SubstructureRedirectMask, &ev);
    XSync(d, False);
}

// The zero-initialized five-slot message a pre-EWMH client sends: source 0,
// timestamp 0, no requestor. Spelled out separately from sendActivation()
// because the point of the case using it is that EVERY field is zero, and
// reaching that through a call with three explicit zero arguments reads as a
// coincidence rather than as the intent.
void sendLegacyActivation(Display* d, Window target)
{
    static Atom a = None;
    if (a == None) a = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);

    XEvent ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.xclient.type         = ClientMessage;
    ev.xclient.window       = target;
    ev.xclient.message_type = a;
    ev.xclient.format       = 32;
    // data.l[0..4] all remain zero.

    XSendEvent(d, DefaultRootWindow(d), False,
               SubstructureNotifyMask | SubstructureRedirectMask, &ev);
    XSync(d, False);
}

// The starting position every activation case needs: a managed target that is
// mapped, framed, NOT focused and NOT flagged, with a different window holding
// the focus so that "was not granted" is distinguishable from "nothing is
// focused at all".
//
// Both windows are mapped with a fresh user-time so the map-time path grants
// each in turn -- the target loses the focus to the incumbent simply by being
// mapped first. Refusing the target at map time would work too, but would leave
// it already carrying the demands-attention state that two of these cases exist
// to observe the ARRIVAL of.
struct ActivationScene {
    Window target = None;
    Window incumbent = None;
    Time now = 0;          // the interaction clock, established by a real click
    Time stale = 0;        // comfortably older than `now`
};

bool buildActivationScene(Display* d, XTestDriver& driver, ActivationScene& out)
{
    out.now = clickRootAndReadClock(d, driver);
    out.stale = out.now / 2;
    if (out.stale == 0) return false;

    if (mapClientWithUserTime(d, 200, 160, 300, 220,
                              { UserTime::Mode::OnToplevel, out.now }, out.target) == None) {
        return false;
    }
    if (!WmFixture::pollUntil([&] { return pumpedActiveWindow(d) == out.target; }, 6000)) {
        return false;
    }

    if (mapClientWithUserTime(d, 520, 380, 260, 200,
                              { UserTime::Mode::OnToplevel, out.now }, out.incumbent) == None) {
        return false;
    }
    if (!WmFixture::pollUntil([&] { return pumpedActiveWindow(d) == out.incumbent; }, 6000)) {
        return false;
    }

    settleWm(d);
    return !hasDemandsAttention(d, out.target);
}

} // namespace

// ---------------------------------------------------------------------------
// Behaviour 1: click-to-focus ON -- the pointer does not focus, a click does
//
// Both halves are in one case on purpose. The negative half alone would pass on
// a WM that had simply stopped focusing anything at all; the positive half is
// what proves the gate suppressed one focus route rather than every route.
//
// WHY THIS MAPS WITH AN EXPLICIT ZERO USER-TIME (changed by plan 08-08).
//
// This case needs a window that is not focused, so that it can then ask whether
// pointer entry focuses it. Until 08-08 that came free: the WM focused nothing
// at map time, so a plain XCreateSimpleWindow was unfocused by default. FOCUS-01
// removed that default -- a window with no _NET_WM_USER_TIME is now granted
// focus on map, deliberately (D-19, legacy clients), and the plain window this
// case used to map is exactly such a client.
//
// So the precondition is now constructed rather than assumed, using the one
// mechanism the spec provides for it: a user-time of exactly zero is the
// client's explicit "do not focus me on map". Every assertion below is
// unchanged and tests the same thing at the same strength; only the way the
// unfocused starting state is reached has moved from implicit to explicit.
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
    Window frame = mapClientWithUserTime(d.get(), 200, 160, 300, 220,
                                         { UserTime::Mode::OnToplevel, 0 }, client);
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
//
// Maps with an explicit zero user-time for the reason behaviour 1 documents at
// length, and here the change is load-bearing rather than merely necessary: with
// a plain window this case would now be VACUOUS. FOCUS-01 grants map-time focus
// to a window that publishes no user-time, so the client would already be active
// before the pointer ever moved, and the poll below would report success without
// the pointer route having run at all. Constructing the unfocused precondition
// is what keeps this case a test of pointer focus rather than of map focus.
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
    Window frame = mapClientWithUserTime(d.get(), 200, 160, 300, 220,
                                         { UserTime::Mode::OnToplevel, 0 }, client);
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

// ===========================================================================
// FOCUS-01 (plan 08-08): map-time focus arbitration
//
// Every case below establishes the WM's last-user-interaction clock with a REAL
// button press first, then maps a window whose _NET_WM_USER_TIME is either newer
// or older than that press. "Stale" and "fresh" have no meaning except relative
// to that clock, which is why none of these cases invent a timestamp.
//
// The pointer is parked on root throughout, clear of every window mapped, so
// nothing here is focused by a pointer crossing. That matters: with the default
// pointer-focus policy a crossing would grant the focus these cases are trying
// to prove was withheld.
// ===========================================================================

// ---------------------------------------------------------------------------
// Behaviour 8: a fresh user-time is focused, and does NOT demand attention
// ---------------------------------------------------------------------------

TEST_CASE("A window mapped with a user-time newer than the last interaction is focused",
          "[wm_focus]")
{
    WmFixture fixture(focusFixture({}));

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    const Time now = clickRootAndReadClock(d.get(), driver);

    Window client = None;
    Window frame = mapClientWithUserTime(d.get(), 200, 160, 300, 220,
                                         { UserTime::Mode::OnToplevel, now }, client);
    REQUIRE(client != None);
    REQUIRE(frame != None);

    const bool focused = WmFixture::pollUntil([&] {
        return pumpedActiveWindow(d.get()) == client;
    }, 6000);
    INFO("active window: " << pumpedActiveWindow(d.get()) << " expected " << client);
    REQUIRE(focused);

    // The other half: a granted window must not also be flagged. A refusal that
    // fired on every window would satisfy the attention assertions below while
    // making the feature useless.
    settleWm(d.get());
    CHECK_FALSE(hasDemandsAttention(d.get(), client));
    CHECK_FALSE(hasUrgencyHint(d.get(), client));

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());
}

// ---------------------------------------------------------------------------
// Behaviour 9: a stale user-time is refused, visibly, and does not steal focus
//
// This is the case FOCUS-01 exists for. All three halves are asserted together:
// the new window is not focused, the window that HAD focus still has it, and the
// refusal is advertised. Asserting only the first would pass on a WM that had
// simply stopped focusing anything.
// ---------------------------------------------------------------------------

TEST_CASE("A window mapped with a stale user-time is refused focus and demands attention",
          "[wm_focus]")
{
    WmFixture fixture(focusFixture({}));

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    const Time now = clickRootAndReadClock(d.get(), driver);

    // The incumbent: mapped with a fresh timestamp, so it legitimately holds the
    // focus the stale window will try to take.
    Window incumbent = None;
    REQUIRE(mapClientWithUserTime(d.get(), 120, 120, 260, 200,
                                  { UserTime::Mode::OnToplevel, now }, incumbent) != None);
    REQUIRE(WmFixture::pollUntil([&] {
        return pumpedActiveWindow(d.get()) == incumbent;
    }, 6000));

    // Older than the button press above: a background application popping a
    // window off a user action that happened long ago.
    const Time stale = now / 2;
    REQUIRE(stale > 0);

    Window thief = None;
    REQUIRE(mapClientWithUserTime(d.get(), 420, 300, 260, 200,
                                  { UserTime::Mode::OnToplevel, stale }, thief) != None);

    // A non-event assertion: it must be settled, repeatedly and SPACED, or it
    // reads the value from before the map (deferred item 9).
    settleWm(d.get());

    INFO("stale user-time " << stale << " vs interaction clock ~" << now);
    CHECK(activeWindow(d.get()) != thief);
    CHECK(activeWindow(d.get()) == incumbent);

    // Not silent. This is the difference between prevention and a black hole.
    CHECK(hasDemandsAttention(d.get(), thief));
    CHECK(hasUrgencyHint(d.get(), thief));

    // And the incumbent was not collaterally flagged.
    CHECK_FALSE(hasDemandsAttention(d.get(), incumbent));

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());
}

// ---------------------------------------------------------------------------
// Behaviour 10: no user-time property at all -> focused (D-19, legacy clients)
// ---------------------------------------------------------------------------

TEST_CASE("A window mapped with no user-time property is focused", "[wm_focus]")
{
    WmFixture fixture(focusFixture({}));

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    clickRootAndReadClock(d.get(), driver);

    // A plain X client of the kind that predates the EWMH entirely: it publishes
    // no timestamp, so the WM has no evidence against it and must not invent any.
    Window client = None;
    REQUIRE(mapClientWithUserTime(d.get(), 200, 160, 300, 220,
                                  { UserTime::Mode::Absent, 0 }, client) != None);

    const bool focused = WmFixture::pollUntil([&] {
        return pumpedActiveWindow(d.get()) == client;
    }, 6000);
    REQUIRE(focused);
    CHECK_FALSE(hasDemandsAttention(d.get(), client));

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());
}

// ---------------------------------------------------------------------------
// Behaviour 11: a user-time of exactly zero -> not focused
//
// Zero is not "very old", it is the spec's explicit "do not focus me on map".
// A client that maps a window it does not want raised into the user's way says
// so this way, and the WM must honour it rather than treating it as a stale
// timestamp that happens to compare small.
// ---------------------------------------------------------------------------

TEST_CASE("A window mapped with a user-time of exactly zero is not focused", "[wm_focus]")
{
    WmFixture fixture(focusFixture({}));

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    const Time now = clickRootAndReadClock(d.get(), driver);

    Window incumbent = None;
    REQUIRE(mapClientWithUserTime(d.get(), 120, 120, 260, 200,
                                  { UserTime::Mode::OnToplevel, now }, incumbent) != None);
    REQUIRE(WmFixture::pollUntil([&] {
        return pumpedActiveWindow(d.get()) == incumbent;
    }, 6000));

    Window quiet = None;
    REQUIRE(mapClientWithUserTime(d.get(), 420, 300, 260, 200,
                                  { UserTime::Mode::OnToplevel, 0 }, quiet) != None);

    settleWm(d.get());

    CHECK(activeWindow(d.get()) != quiet);
    CHECK(activeWindow(d.get()) == incumbent);

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());
}

// ---------------------------------------------------------------------------
// Behaviour 11b: zero is honoured BEFORE the user has interacted at all
//
// Added after mutation testing showed behaviour 11 does not actually pin the
// rule it is named for. Behaviour 11 advances the interaction clock with a real
// button press first, so its zero timestamp is ALSO stale -- deleting the
// explicit `userTime == 0` branch from the WM left behaviour 11 green, because
// the ordinary staleness comparison refused the window anyway.
//
// The zero rule is load-bearing in exactly one window of time, which this case
// occupies: before the first user interaction the clock is still the X
// CurrentTime sentinel of 0, so a zero timestamp compares as NOT stale (delta
// zero) and the ordinary comparison would GRANT it. Only the explicit branch
// refuses it. Deleting that branch turns this case red.
//
// It also pins the planner's flagged assumption that the very first window
// mapped before any user input is granted focus: the incumbent below is mapped
// into a freshly started WM with no interaction whatsoever, and must be focused.
// ---------------------------------------------------------------------------

TEST_CASE("A user-time of zero is refused even before any user interaction",
          "[wm_focus]")
{
    WmFixture fixture(focusFixture({}));

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    // NO clickRootAndReadClock() here -- that is the entire point. The WM's
    // interaction clock is still CurrentTime.

    // The flagged assumption, asserted rather than assumed: with no interaction
    // yet recorded, a legacy client is still granted focus on map.
    Window incumbent = None;
    REQUIRE(mapClientWithUserTime(d.get(), 120, 120, 260, 200,
                                  { UserTime::Mode::Absent, 0 }, incumbent) != None);
    REQUIRE(WmFixture::pollUntil([&] {
        return pumpedActiveWindow(d.get()) == incumbent;
    }, 6000));

    // And against that same untouched clock, an explicit zero is still refused.
    Window quiet = None;
    REQUIRE(mapClientWithUserTime(d.get(), 420, 300, 260, 200,
                                  { UserTime::Mode::OnToplevel, 0 }, quiet) != None);

    settleWm(d.get());

    CHECK(activeWindow(d.get()) != quiet);
    CHECK(activeWindow(d.get()) == incumbent);
    CHECK(hasDemandsAttention(d.get(), quiet));

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());
}

// ---------------------------------------------------------------------------
// Behaviour 12: the user-time WINDOW proxy is consulted
//
// The toplevel here carries NO _NET_WM_USER_TIME of its own. If the WM read only
// the toplevel it would see "absent" and grant focus, which is exactly the
// bypass this case pins shut: a client that uses the proxy mechanism must not be
// exempt from arbitration.
// ---------------------------------------------------------------------------

TEST_CASE("A stale user-time on the user-time-window proxy is arbitrated", "[wm_focus]")
{
    WmFixture fixture(focusFixture({}));

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    const Time now = clickRootAndReadClock(d.get(), driver);

    Window incumbent = None;
    REQUIRE(mapClientWithUserTime(d.get(), 120, 120, 260, 200,
                                  { UserTime::Mode::OnToplevel, now }, incumbent) != None);
    REQUIRE(WmFixture::pollUntil([&] {
        return pumpedActiveWindow(d.get()) == incumbent;
    }, 6000));

    const Time stale = now / 2;
    REQUIRE(stale > 0);

    Window proxied = None;
    Window proxy = None;
    REQUIRE(mapClientWithUserTime(d.get(), 420, 300, 260, 200,
                                  { UserTime::Mode::OnProxy, stale }, proxied, &proxy) != None);
    REQUIRE(proxy != None);

    settleWm(d.get());

    INFO("proxy window " << proxy << " carries user-time " << stale);
    CHECK(activeWindow(d.get()) != proxied);
    CHECK(activeWindow(d.get()) == incumbent);
    CHECK(hasDemandsAttention(d.get(), proxied));

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());
}

// ---------------------------------------------------------------------------
// Behaviour 13: the off switch genuinely switches it off
//
// Both windows that behaviours 9 and 11 proved are refused are mapped here under
// --no-focus-stealing-prevention, and each must take the focus in turn. If this
// case passes while 9 and 11 also pass, the switch controls the feature rather
// than merely existing.
// ---------------------------------------------------------------------------

TEST_CASE("With focus-stealing prevention off a stale window is focused anyway",
          "[wm_focus]")
{
    WmFixture fixture(focusFixture({ "--no-focus-stealing-prevention" }));

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    const Time now = clickRootAndReadClock(d.get(), driver);
    const Time stale = now / 2;
    REQUIRE(stale > 0);

    // Refused in behaviour 9 -- granted here.
    Window staleWin = None;
    REQUIRE(mapClientWithUserTime(d.get(), 120, 120, 260, 200,
                                  { UserTime::Mode::OnToplevel, stale }, staleWin) != None);
    REQUIRE(WmFixture::pollUntil([&] {
        return pumpedActiveWindow(d.get()) == staleWin;
    }, 6000));
    CHECK_FALSE(hasDemandsAttention(d.get(), staleWin));

    // Refused in behaviour 11 -- granted here.
    Window zeroWin = None;
    REQUIRE(mapClientWithUserTime(d.get(), 420, 300, 260, 200,
                                  { UserTime::Mode::OnToplevel, 0 }, zeroWin) != None);
    REQUIRE(WmFixture::pollUntil([&] {
        return pumpedActiveWindow(d.get()) == zeroWin;
    }, 6000));
    CHECK_FALSE(hasDemandsAttention(d.get(), zeroWin));

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());
}

// ---------------------------------------------------------------------------
// Behaviour 14: activating a refused window clears the attention state
//
// The spec is explicit that the WM should unset the state once the window has
// had the attention it asked for. A hint that never clears is worse than none:
// it degrades into permanent decoration the user learns to ignore.
// ---------------------------------------------------------------------------

TEST_CASE("Activating a refused window clears its demands-attention state", "[wm_focus]")
{
    WmFixture fixture(focusFixture({}));

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    const Time now = clickRootAndReadClock(d.get(), driver);
    const Time stale = now / 2;
    REQUIRE(stale > 0);

    Window refused = None;
    REQUIRE(mapClientWithUserTime(d.get(), 200, 160, 320, 240,
                                  { UserTime::Mode::OnToplevel, stale }, refused) != None);

    settleWm(d.get());
    REQUIRE(hasDemandsAttention(d.get(), refused));
    REQUIRE(hasUrgencyHint(d.get(), refused));

    // Now the user gives it the attention it asked for. A Button1 press inside
    // the client area is not a border window, so Client::eventButton falls
    // through to activate() -- the real user-activation path, driven through
    // XTEST because the WM refuses send_event on this route.
    Rect r{};
    REQUIRE(serverRect(d.get(), refused, r));
    driver.moveTo(r.x + r.w / 2, r.y + r.h / 2);
    driver.click(Button1);

    const bool focused = WmFixture::pollUntil([&] {
        return pumpedActiveWindow(d.get()) == refused;
    }, 6000);
    REQUIRE(focused);

    settleWm(d.get());
    CHECK_FALSE(hasDemandsAttention(d.get(), refused));
    CHECK_FALSE(hasUrgencyHint(d.get(), refused));

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());
}

// ===========================================================================
// FOCUS-01 (plan 08-08, Task 4): _NET_ACTIVE_WINDOW arbitration
//
// The map-time half above is only half a mitigation. An application that wants
// focus need not rely on being focused when its window appears -- it can simply
// ask, by sending an activation request. Until this plan the WM granted every
// such request unconditionally (Phase 6 D-10), which made the map-time
// arbitration bypassable by any client that read the spec.
//
// The resolution recorded at this plan's checkpoint arbitrates by SOURCE
// INDICATION, which is the discriminator the EWMH itself provides:
//
//   source 2 (pager)       -> granted unconditionally. Taskbars and window
//                             switchers act on the user's direct behalf and
//                             know more about the user's intent than the WM.
//   source 1 (application) -> arbitrated against the message timestamp through
//                             the SAME shared helper the map-time path uses.
//   source 0 (no source)   -> granted. A legacy client sends none, and refusing
//                             would break it -- D-19's reasoning, applied to the
//                             second entry point.
//
// The ceiling is honesty: a client that lies about its source is granted. But
// such a client could equally forge a fresh timestamp, so arbitrating all
// sources would not raise the ceiling, and would break every pager.
// ===========================================================================

// ---------------------------------------------------------------------------
// Behaviour 15: a pager request is granted whatever its timestamp says
// ---------------------------------------------------------------------------

TEST_CASE("An activation request from a pager is granted despite a stale timestamp",
          "[wm_focus]")
{
    WmFixture fixture(focusFixture({}));

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    ActivationScene scene;
    REQUIRE(buildActivationScene(d.get(), driver, scene));

    // Source 2 with a deliberately stale timestamp: if the WM arbitrated pager
    // requests, this is the message it would refuse.
    sendActivation(d.get(), scene.target, 2, scene.stale);

    const bool granted = WmFixture::pollUntil([&] {
        return pumpedActiveWindow(d.get()) == scene.target;
    }, 6000);
    INFO("active: " << pumpedActiveWindow(d.get()) << "  target: " << scene.target);
    CHECK(granted);
    CHECK_FALSE(hasDemandsAttention(d.get(), scene.target));

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());
}

// ---------------------------------------------------------------------------
// Behaviour 16: an application request with a stale timestamp is refused
//
// This is the bypass being closed. Without it the map-time arbitration is
// decorative: any application refused at map time could immediately ask for the
// focus it was just denied and be handed it.
// ---------------------------------------------------------------------------

TEST_CASE("A stale application activation request is refused and demands attention",
          "[wm_focus]")
{
    WmFixture fixture(focusFixture({}));

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    ActivationScene scene;
    REQUIRE(buildActivationScene(d.get(), driver, scene));

    sendActivation(d.get(), scene.target, 1, scene.stale);

    settleWm(d.get());

    INFO("stale stamp " << scene.stale << " vs interaction clock ~" << scene.now);
    INFO("active: " << activeWindow(d.get()) << "  target: " << scene.target
         << "  incumbent: " << scene.incumbent);
    CHECK(activeWindow(d.get()) != scene.target);
    CHECK(activeWindow(d.get()) == scene.incumbent);

    // Refused, not dropped on the floor.
    CHECK(hasDemandsAttention(d.get(), scene.target));
    CHECK(hasUrgencyHint(d.get(), scene.target));

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());
}

// ---------------------------------------------------------------------------
// Behaviour 17: an application request with a fresh timestamp is granted
//
// The control for behaviour 16. Without it, a WM that had simply stopped
// honouring application activation altogether would pass 16 -- and would break
// every application that legitimately raises its own window on a user action.
// ---------------------------------------------------------------------------

TEST_CASE("A fresh application activation request is granted", "[wm_focus]")
{
    WmFixture fixture(focusFixture({}));

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    ActivationScene scene;
    REQUIRE(buildActivationScene(d.get(), driver, scene));

    // Taken from the server AFTER the interaction clock was last advanced, so it
    // is newer than it by construction rather than by arithmetic.
    const Time fresh = serverTime(d.get());
    REQUIRE(fresh >= scene.now);

    sendActivation(d.get(), scene.target, 1, fresh);

    const bool granted = WmFixture::pollUntil([&] {
        return pumpedActiveWindow(d.get()) == scene.target;
    }, 6000);
    INFO("fresh stamp " << fresh << " vs interaction clock ~" << scene.now);
    CHECK(granted);
    CHECK_FALSE(hasDemandsAttention(d.get(), scene.target));

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());
}

// ---------------------------------------------------------------------------
// Behaviour 18: a request carrying no source indication is granted
//
// D-19's reasoning at the second entry point. The timestamp is stale, so the
// only thing that can be granting this request is the source-0 rule.
// ---------------------------------------------------------------------------

TEST_CASE("An activation request with no source indication is granted", "[wm_focus]")
{
    WmFixture fixture(focusFixture({}));

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    ActivationScene scene;
    REQUIRE(buildActivationScene(d.get(), driver, scene));

    sendActivation(d.get(), scene.target, 0, scene.stale);

    const bool granted = WmFixture::pollUntil([&] {
        return pumpedActiveWindow(d.get()) == scene.target;
    }, 6000);
    INFO("active: " << pumpedActiveWindow(d.get()) << "  target: " << scene.target);
    CHECK(granted);

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());
}

// ---------------------------------------------------------------------------
// Behaviour 19: the off switch restores unconditional granting
//
// Exactly the message behaviour 16 refuses, under --no-focus-stealing-prevention.
// If this passes while 16 also passes, the switch controls the activation path
// too rather than only the map-time one.
// ---------------------------------------------------------------------------

TEST_CASE("With focus-stealing prevention off a stale activation request is granted",
          "[wm_focus]")
{
    WmFixture fixture(focusFixture({ "--no-focus-stealing-prevention" }));

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    ActivationScene scene;
    REQUIRE(buildActivationScene(d.get(), driver, scene));

    sendActivation(d.get(), scene.target, 1, scene.stale);

    const bool granted = WmFixture::pollUntil([&] {
        return pumpedActiveWindow(d.get()) == scene.target;
    }, 6000);
    INFO("active: " << pumpedActiveWindow(d.get()) << "  target: " << scene.target);
    CHECK(granted);
    CHECK_FALSE(hasDemandsAttention(d.get(), scene.target));

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());
}

// ---------------------------------------------------------------------------
// Behaviour 20: malformed requests neither crash the WM nor grant focus
//
// Four shapes, in one case because they share an expensive fixture and because
// the final assertion -- that the WM is still alive and unpoisoned after all of
// them -- is about the sequence rather than any one message.
//
// Note what is NOT tested here. XClientMessageEvent always carries five `long`
// slots; there is no wire-level "field count", so a "too few fields" message
// cannot be constructed and asserting on one would be theatre. The zero-filled
// five-slot message below is the real legacy shape, and it is GRANTED, because
// all-zero data means source 0 -- the case behaviour 18 covers deliberately.
// ---------------------------------------------------------------------------

TEST_CASE("Malformed activation requests are ignored without crashing the WM",
          "[wm_focus]")
{
    WmFixture fixture(focusFixture({}));

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    ActivationScene scene;
    REQUIRE(buildActivationScene(d.get(), driver, scene));

    // --- 1. wrong format ----------------------------------------------------
    // Format 8 means the five `long` slots are not there to be read. A WM that
    // read data.l[0] anyway would be reading fields the sender never wrote.
    sendActivation(d.get(), scene.target, 1, scene.now, 8);
    settleWm(d.get());
    INFO("after wrong format, active: " << activeWindow(d.get()));
    CHECK(activeWindow(d.get()) == scene.incumbent);

    // --- 2. unknown source indication ---------------------------------------
    // The EWMH defines 0, 1 and 2. A WM that treated "not 1" as "trusted" would
    // grant this, which is the failure mode a bare `source == 1` test invites.
    sendActivation(d.get(), scene.target, 7, scene.now);
    settleWm(d.get());
    INFO("after unknown source, active: " << activeWindow(d.get()));
    CHECK(activeWindow(d.get()) == scene.incumbent);

    // --- 3. target that is not a managed window -----------------------------
    // A real, live window the WM never managed: override-redirect and never
    // mapped. Using a fabricated id would test the X error path instead.
    XSetWindowAttributes attr;
    attr.override_redirect = True;
    Window unmanaged = XCreateWindow(d.get(), DefaultRootWindow(d.get()), -70, -70, 1, 1, 0,
                                     CopyFromParent, InputOutput, CopyFromParent,
                                     CWOverrideRedirect, &attr);
    XSync(d.get(), False);

    sendActivation(d.get(), unmanaged, 2, scene.now);
    settleWm(d.get());
    INFO("after unmanaged target, active: " << activeWindow(d.get()));
    CHECK(activeWindow(d.get()) != unmanaged);
    CHECK(activeWindow(d.get()) == scene.incumbent);

    // --- 4. the zero-initialized legacy message -----------------------------
    // Last, because unlike the three above this one is VALID and is granted.
    sendLegacyActivation(d.get(), scene.target);

    const bool granted = WmFixture::pollUntil([&] {
        return pumpedActiveWindow(d.get()) == scene.target;
    }, 6000);
    INFO("after legacy zero message, active: " << pumpedActiveWindow(d.get()));
    CHECK(granted);

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());
    XDestroyWindow(d.get(), unmanaged);
    XSync(d.get(), False);
}
