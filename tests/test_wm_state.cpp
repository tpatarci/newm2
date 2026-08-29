// EWMH state-machine and malformed-property tests against the REAL compiled
// wm2-born-again binary (plan 08-12, TEST-05).
//
// This file closes three of the "Missing Automated Coverage To Add" entries in
// COMPILED_CODE_BEHAVIOR_CHECKLIST.md:
//
//   item 10  EWMH client messages in eventClient()  -> [wm_state]
//   item 11  fullscreen / maximize geometry restore -> [wm_fsmax]
//   item 12  malformed client properties            -> [wm_props]
//
// Tags are registered as ctest LABELS via ADD_TAGS_AS_LABELS (D-33), so each
// group is selectable with `ctest -L '^wm_state$' --no-tests=error`.
//
// The rules this file inherits from 08-04 .. 08-11, all of them arrived at the
// hard way and none of them optional:
//
//   OUTCOMES ARE OBSERVED THROUGH THE SERVER. _NET_WM_STATE on the client
//   window, _NET_CLIENT_LIST and _NET_WORKAREA on root, geometry through
//   XGetGeometry + XTranslateCoordinates. Never a claim about WM internals.
//
//   NON-EVENTS ARE PROVEN BY SETTLING FIRST (deferred item 9). The WM does not
//   flush its X output until its event loop wakes, so a single read after a
//   single nudge reliably observes the PREVIOUS state and a negative assertion
//   passes for the wrong reason. Every "this did NOT happen" read here runs
//   settleWm() first -- fifteen SPACED nudges. The spacing is as load-bearing as
//   the count; a tight loop of the same length still read stale values.
//
//   NO sleep()-BASED SYNCHRONISATION. Every wait is a deadline-bounded poll
//   whose exit condition is a real observation.
//
//   THE WM SURVIVES X PROTOCOL ERRORS. WindowManager::errorHandler() logs and
//   returns 0, so no assertion about windows, properties or geometry can notice
//   one. Plan 08-11 added the stderr assertion below and found a
//   RenderBadPicture on every managed window close that 243 tests across eight
//   plans had walked past. It is copied here for the same reason: this file
//   drives EWMH client messages and reparenting geometry transitions, which is
//   exactly the surface where a silently-swallowed protocol error would hide.
//
// Deferred item 7 is accounted for rather than worked around: the WM adopts its
// own menu, submenu and EWMH check windows as clients, so _NET_CLIENT_LIST
// always carries entries this file did not create. List assertions here filter
// to the windows the case owns.

#include <catch2/catch_test_macros.hpp>

#include "support/WmFixture.h"

#include "x11wrap.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

using namespace wm2test;

namespace {

// The Xvfb geometry WmFixture starts every display with.
constexpr int kScreenW = 1024;
constexpr int kScreenH = 768;

// A parking spot for the pointer clear of every window this file maps. Set
// BEFORE anything is mapped: the pointer starts at the screen centre, and a
// window mapped underneath it would generate an EnterNotify that races the case.
constexpr int kParkX = 5;
constexpr int kParkY = 5;

// ---------------------------------------------------------------------------
// Settling (deferred item 9)
// ---------------------------------------------------------------------------

// Wake the WM's event loop so it flushes its X output buffer. The nudge is
// override-redirect, so WindowManager::eventCreate returns immediately for it
// and it can never be managed or perturb an assertion.
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

// Fifteen nudges at 20ms intervals -- the shape tests/test_wm_focus.cpp arrived
// at after a TIGHT loop of the same length still read the stale value.
void settleWm(Display* d)
{
    for (int i = 0; i < 15; ++i) {
        pumpWm(d);
        pollSleep();
    }
}

// ---------------------------------------------------------------------------
// Server observation
// ---------------------------------------------------------------------------

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;

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

// Absolute rectangle of a window as the SERVER has it. XGetGeometry's x/y are
// parent-relative, so the translate is what makes this absolute for a window the
// WM has reparented into a frame.
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

Rect rectOf(Display* d, Window w)
{
    Rect r;
    serverRect(d, w, r);
    return r;
}

Window parentOf(Display* d, Window w)
{
    Window wroot = None, parent = None, *children = nullptr;
    unsigned int n = 0;
    if (!XQueryTree(d, w, &wroot, &parent, &children, &n)) return None;
    if (children) XFree(children);
    return parent;
}

std::vector<Window> childrenOf(Display* d, Window w)
{
    Window wroot = None, parent = None, *children = nullptr;
    unsigned int n = 0;
    std::vector<Window> out;
    if (!XQueryTree(d, w, &wroot, &parent, &children, &n)) return out;
    if (children) {
        out.assign(children, children + n);
        XFree(children);
    }
    return out;
}

// _NET_CLIENT_LIST, in the order the WM published it.
std::vector<Window> clientList(Display* d)
{
    static Atom prop = None;
    if (prop == None) prop = XInternAtom(d, "_NET_CLIENT_LIST", False);

    std::vector<Window> out;
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long nItems = 0, bytesAfter = 0;
    unsigned char* raw = nullptr;

    if (XGetWindowProperty(d, DefaultRootWindow(d), prop, 0, 256, False, XA_WINDOW,
                           &actualType, &actualFormat, &nItems, &bytesAfter,
                           &raw) != Success) {
        return out;
    }
    if (raw && actualType == XA_WINDOW && actualFormat == 32) {
        Window* vals = reinterpret_cast<Window*>(raw);
        out.assign(vals, vals + nItems);
    }
    if (raw) XFree(raw);
    return out;
}

bool listed(Display* d, Window w)
{
    const std::vector<Window> l = clientList(d);
    return std::find(l.begin(), l.end(), w) != l.end();
}

long occurrences(Display* d, Window w)
{
    const std::vector<Window> l = clientList(d);
    return std::count(l.begin(), l.end(), w);
}

Window activeWindow(Display* d)
{
    static Atom atom = None;
    if (atom == None) atom = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
    Window w = None;
    if (!readWindowProp(d, DefaultRootWindow(d), atom, w)) return None;
    return w;
}

// The _NET_WM_STATE atom list on a client window. An absent property reads as an
// empty list, which is the correct interpretation and keeps the negative
// assertions below from having to distinguish "no property" from "no states".
//
// Read with the full six out-parameter query and the type/format/count checked
// before the data is interpreted -- the same discipline this plan requires of
// the production readers.
std::vector<Atom> wmState(Display* d, Window w)
{
    static Atom prop = None;
    if (prop == None) prop = XInternAtom(d, "_NET_WM_STATE", False);

    std::vector<Atom> out;
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long nItems = 0, bytesAfter = 0;
    unsigned char* raw = nullptr;

    if (XGetWindowProperty(d, w, prop, 0, 64, False, XA_ATOM, &actualType,
                           &actualFormat, &nItems, &bytesAfter, &raw) != Success) {
        return out;
    }
    if (raw && actualType == XA_ATOM && actualFormat == 32 && nItems >= 1) {
        Atom* vals = reinterpret_cast<Atom*>(raw);
        out.assign(vals, vals + nItems);
    }
    if (raw) XFree(raw);
    return out;
}

bool hasState(Display* d, Window w, const char* name)
{
    const Atom a = XInternAtom(d, name, False);
    const std::vector<Atom> states = wmState(d, w);
    return std::find(states.begin(), states.end(), a) != states.end();
}

// A sorted copy, for the byte-identical before/after comparisons the negative
// cases need. Sorted rather than raw because the WM's publication ORDER is not
// what those cases are asserting on -- the SET is.
std::vector<Atom> stateSet(Display* d, Window w)
{
    std::vector<Atom> s = wmState(d, w);
    std::sort(s.begin(), s.end());
    return s;
}

std::string describeStates(Display* d, const std::vector<Atom>& states)
{
    std::string out = "[";
    for (size_t i = 0; i < states.size(); ++i) {
        char* name = XGetAtomName(d, states[i]);
        if (name) { out += name; XFree(name); }
        else      { out += std::to_string(states[i]); }
        if (i + 1 < states.size()) out += ", ";
    }
    out += "]";
    return out;
}

// _NET_WORKAREA as the WM publishes it.
bool workarea(Display* d, Rect& out)
{
    static Atom prop = None;
    if (prop == None) prop = XInternAtom(d, "_NET_WORKAREA", False);

    Atom actualType = None;
    int actualFormat = 0;
    unsigned long nItems = 0, bytesAfter = 0;
    unsigned char* raw = nullptr;

    if (XGetWindowProperty(d, DefaultRootWindow(d), prop, 0, 4, False, XA_CARDINAL,
                           &actualType, &actualFormat, &nItems, &bytesAfter,
                           &raw) != Success) {
        return false;
    }
    bool ok = false;
    if (raw && actualType == XA_CARDINAL && actualFormat == 32 && nItems >= 4) {
        long* vals = reinterpret_cast<long*>(raw);
        out.x = static_cast<int>(vals[0]);
        out.y = static_cast<int>(vals[1]);
        out.w = static_cast<int>(vals[2]);
        out.h = static_cast<int>(vals[3]);
        ok = true;
    }
    if (raw) XFree(raw);
    return ok;
}

Rect workareaOf(Display* d)
{
    Rect r;
    workarea(d, r);
    return r;
}

// WM_STATE's state field (Withdrawn 0, Normal 1, Iconic 3).
bool icccmState(Display* d, Window w, long& out)
{
    static Atom prop = None;
    if (prop == None) prop = XInternAtom(d, "WM_STATE", False);

    Atom actualType = None;
    int actualFormat = 0;
    unsigned long nItems = 0, bytesAfter = 0;
    unsigned char* raw = nullptr;

    if (XGetWindowProperty(d, w, prop, 0, 2, False, AnyPropertyType, &actualType,
                           &actualFormat, &nItems, &bytesAfter, &raw) != Success) {
        return false;
    }
    bool ok = false;
    if (raw && actualFormat == 32 && nItems >= 1) {
        out = reinterpret_cast<long*>(raw)[0];
        ok = true;
    }
    if (raw) XFree(raw);
    return ok;
}

bool isMapped(Display* d, Window w)
{
    XWindowAttributes attr;
    if (!XGetWindowAttributes(d, w, &attr)) return false;
    return attr.map_state != IsUnmapped;
}

// ---------------------------------------------------------------------------
// Protocol errors (copied from tests/test_wm_lifecycle.cpp, plan 08-11)
// ---------------------------------------------------------------------------

// Every X protocol error the WM reports goes through
// WindowManager::errorHandler(), which prints one line shaped
// `wm2: <request> (0xID): <message>`. Nothing else the WM writes carries
// " (0x", so that substring is a precise detector and needs no error-name list
// that would have to be kept in step with Xlib's message database.
std::vector<std::string> xProtocolErrors(const std::string& stderrText)
{
    std::vector<std::string> out;
    std::string line;
    for (char ch : stderrText) {
        if (ch == '\n') {
            if (line.find(" (0x") != std::string::npos) out.push_back(line);
            line.clear();
        } else {
            line.push_back(ch);
        }
    }
    if (line.find(" (0x") != std::string::npos) out.push_back(line);
    return out;
}

// The subset of the above that is NOT a BadWindow.
//
// Deferred item 13: destroying a client necessarily produces one BadWindow
// inside the WM (the resize handle is a child of the CLIENT window, so the
// server reclaims it before the deactivate() on the way down unmaps it), and
// WmFixture destroys its own readiness probe during startup -- so the same
// teardown BadWindow can surface in a case that destroys nothing of its own.
// Excluding one error CODE is a far smaller concession than dropping the
// assertion: RenderBadPicture, BadValue, BadMatch, BadDrawable, BadAtom and the
// rest still fail every case.
std::vector<std::string> xProtocolErrorsExceptBadWindow(const std::string& stderrText)
{
    std::vector<std::string> out;
    for (const auto& line : xProtocolErrors(stderrText)) {
        if (line.find("BadWindow") == std::string::npos) out.push_back(line);
    }
    return out;
}

std::string joined(const std::vector<std::string>& lines)
{
    std::string out;
    for (const auto& l : lines) { out += l; out += "\n"; }
    return out;
}

// ---------------------------------------------------------------------------
// Client creation
// ---------------------------------------------------------------------------

Window createClient(Display* d, int x, int y, int w, int h)
{
    Window root = DefaultRootWindow(d);
    Window win = XCreateSimpleWindow(d, root, x, y,
                                     static_cast<unsigned>(w), static_cast<unsigned>(h), 0,
                                     BlackPixel(d, DefaultScreen(d)),
                                     WhitePixel(d, DefaultScreen(d)));
    XSync(d, False);
    return win;
}

// Wait until the WM has both reparented `win` into a frame AND published it in
// _NET_CLIENT_LIST. The second half keeps later reads from racing the rest of
// Client::manage().
Window awaitFrameFor(Display* d, Window win, int timeoutMs = 8000)
{
    Window root = DefaultRootWindow(d);

    Window frame = None;
    const bool framed = WmFixture::pollUntil([&] {
        pumpWm(d);
        Window parent = parentOf(d, win);
        if (parent == None || parent == root) return false;
        frame = parent;
        return true;
    }, timeoutMs);
    if (!framed) return None;

    WmFixture::pollUntil([&] {
        pumpWm(d);
        return listed(d, win);
    }, timeoutMs);

    return frame;
}

Window mapClientAndAwaitFrame(Display* d, int x, int y, int w, int h, Window& clientOut)
{
    Window win = createClient(d, x, y, w, h);
    clientOut = win;
    XMapWindow(d, win);
    XSync(d, False);
    return awaitFrameFor(d, win);
}

// Park the pointer clear of everything this file maps, before anything is
// mapped. A window that appears under the pointer generates an EnterNotify that
// would race a focus-sensitive assertion.
void parkPointer(Display* d)
{
    XWarpPointer(d, None, DefaultRootWindow(d), 0, 0, 0, 0, kParkX, kParkY);
    XSync(d, False);
}

// ---------------------------------------------------------------------------
// Client messages -- the route a real application uses
// ---------------------------------------------------------------------------

// _NET_WM_STATE, sent the way a real client sends one: to the ROOT window with
// SubstructureRedirect|SubstructureNotify. That is the route
// WindowManager::eventClient() is written against; a message addressed directly
// to the target takes a different path entirely and would be selected for by
// the target's own event mask rather than by the WM's root mask.
void sendStateMessage(Display* d, Window target, long action,
                      Atom prop1, Atom prop2, long source = 1, int format = 32)
{
    static Atom a = None;
    if (a == None) a = XInternAtom(d, "_NET_WM_STATE", False);

    XEvent ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.xclient.type         = ClientMessage;
    ev.xclient.window       = target;
    ev.xclient.message_type = a;
    ev.xclient.format       = format;
    ev.xclient.data.l[0]    = action;                    // 0 remove, 1 add, 2 toggle
    ev.xclient.data.l[1]    = static_cast<long>(prop1);
    ev.xclient.data.l[2]    = static_cast<long>(prop2);
    ev.xclient.data.l[3]    = source;

    XSendEvent(d, DefaultRootWindow(d), False,
               SubstructureNotifyMask | SubstructureRedirectMask, &ev);
    XSync(d, False);
}

constexpr long kStateRemove = 0;
constexpr long kStateAdd    = 1;
constexpr long kStateToggle = 2;

// WM_CHANGE_STATE(IconicState) -- the ICCCM route into Client::hide(), and the
// third message type eventClient() handles.
void requestIconify(Display* d, Window win)
{
    XClientMessageEvent ev{};
    ev.type = ClientMessage;
    ev.window = win;
    ev.message_type = XInternAtom(d, "WM_CHANGE_STATE", False);
    ev.format = 32;
    ev.data.l[0] = IconicState;

    XSendEvent(d, DefaultRootWindow(d), False,
               SubstructureNotifyMask | SubstructureRedirectMask,
               reinterpret_cast<XEvent*>(&ev));
    XSync(d, False);
}

// Wait until `pred` observes the state the message asked for. Every state
// message is asynchronous: the send returns as soon as the request is on the
// wire, long before the WM's loop has run it.
template <typename Pred>
bool awaitState(Display* d, Pred pred, int timeoutMs = 8000)
{
    return WmFixture::pollUntil([&] {
        pumpWm(d);
        return pred();
    }, timeoutMs);
}

// ---------------------------------------------------------------------------
// Docks -- what makes "workarea" and "screen" different observable things
// ---------------------------------------------------------------------------

constexpr int kDockHeight = 40;

// An undecorated dock at the bottom of the screen carrying
// _NET_WM_WINDOW_TYPE_DOCK and a bottom strut. Its whole purpose here is to
// make _NET_WORKAREA differ from the screen rectangle: without a dock the two
// are identical, and every "maximize uses the workarea, fullscreen uses the
// screen" assertion would pass for either implementation.
//
// The type and the strut are written BEFORE the map, because the WM reads the
// window type during Client::manage() and a property written afterwards would
// race the read it is supposed to govern.
Window createDock(Display* d, int strutBottom = kDockHeight)
{
    Window root = DefaultRootWindow(d);
    Window dock = XCreateSimpleWindow(d, root, 0, kScreenH - kDockHeight,
                                      kScreenW, kDockHeight, 0,
                                      BlackPixel(d, DefaultScreen(d)),
                                      WhitePixel(d, DefaultScreen(d)));

    Atom typeProp = XInternAtom(d, "_NET_WM_WINDOW_TYPE", False);
    Atom dockType = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DOCK", False);
    XChangeProperty(d, dock, typeProp, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&dockType), 1);

    // left, right, top, bottom, then the eight start/end values.
    Atom strutPartial = XInternAtom(d, "_NET_WM_STRUT_PARTIAL", False);
    long struts[12] = {0, 0, 0, strutBottom, 0, 0, 0, 0, 0, 0, 0, kScreenW - 1};
    XChangeProperty(d, dock, strutPartial, XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(struts), 12);

    XMapWindow(d, dock);
    XSync(d, False);
    return dock;
}

const char* const kFullscreen = "_NET_WM_STATE_FULLSCREEN";
const char* const kMaxVert    = "_NET_WM_STATE_MAXIMIZED_VERT";
const char* const kMaxHorz    = "_NET_WM_STATE_MAXIMIZED_HORZ";
const char* const kHidden     = "_NET_WM_STATE_HIDDEN";
const char* const kSkipTaskbar = "_NET_WM_STATE_SKIP_TASKBAR";

}  // namespace


// ===========================================================================
// [wm_state] -- the EWMH client-message matrix (checklist coverage item 10)
// ===========================================================================

TEST_CASE("A state message adds and removes fullscreen", "[wm_state]")
{
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    Window win = None;
    REQUIRE(mapClientAndAwaitFrame(d, 120, 90, 300, 200, win) != None);
    REQUIRE_FALSE(hasState(d, win, kFullscreen));

    const Atom fs = XInternAtom(d, kFullscreen, False);

    sendStateMessage(d, win, kStateAdd, fs, None);
    REQUIRE(awaitState(d, [&] { return hasState(d, win, kFullscreen); }));

    // The client must still be MANAGED after entering fullscreen. Deferred
    // item 8: the strip reparents the child to root, the server's implicit
    // unmap came back as an UnmapNotify, and eventUnmap() took the withdraw
    // path -- leaving the window fullscreen-sized, at its old position, and
    // Withdrawn. A client that is no longer managed cannot be un-fullscreened
    // by anyone, which is what makes this a precondition of the next line
    // rather than a separate case.
    settleWm(d);
    long icccm = -1;
    INFO("wm stderr:\n" << fixture.wmStderr());
    INFO("states while fullscreen: " << describeStates(d, wmState(d, win)));
    REQUIRE(icccmState(d, win, icccm));
    CHECK(icccm == NormalState);
    CHECK(listed(d, win));

    sendStateMessage(d, win, kStateRemove, fs, None);
    REQUIRE(awaitState(d, [&] { return !hasState(d, win, kFullscreen); }));

    // The removal must be a removal, not a replacement of the whole array by
    // something that merely happens not to contain the atom while the WM has
    // lost track of everything else.
    settleWm(d);
    INFO("states after remove: " << describeStates(d, wmState(d, win)));
    REQUIRE_FALSE(hasState(d, win, kFullscreen));

    REQUIRE(fixture.wmAlive());
    INFO("wm stderr:\n" << fixture.wmStderr());
    CHECK(joined(xProtocolErrorsExceptBadWindow(fixture.wmStderr())).empty());
}

TEST_CASE("A state message toggles fullscreen in both directions", "[wm_state]")
{
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    Window win = None;
    REQUIRE(mapClientAndAwaitFrame(d, 120, 90, 300, 200, win) != None);

    const Atom fs = XInternAtom(d, kFullscreen, False);

    // OFF -> ON
    REQUIRE_FALSE(hasState(d, win, kFullscreen));
    sendStateMessage(d, win, kStateToggle, fs, None);
    REQUIRE(awaitState(d, [&] { return hasState(d, win, kFullscreen); }));

    // ON -> OFF, from the state the previous toggle left behind. The two halves
    // are what make this a toggle test rather than two add/remove tests wearing
    // action 2: an implementation that treated toggle as "add" passes the first
    // half and fails here.
    sendStateMessage(d, win, kStateToggle, fs, None);
    REQUIRE(awaitState(d, [&] { return !hasState(d, win, kFullscreen); }));

    // And once more ON, so a "toggle means remove" implementation fails too.
    sendStateMessage(d, win, kStateToggle, fs, None);
    REQUIRE(awaitState(d, [&] { return hasState(d, win, kFullscreen); }));

    REQUIRE(fixture.wmAlive());
    INFO("wm stderr:\n" << fixture.wmStderr());
    CHECK(joined(xProtocolErrorsExceptBadWindow(fixture.wmStderr())).empty());
}

TEST_CASE("A single state message naming two properties applies both", "[wm_state]")
{
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    Window win = None;
    REQUIRE(mapClientAndAwaitFrame(d, 120, 90, 300, 200, win) != None);

    const Atom mv = XInternAtom(d, kMaxVert, False);
    const Atom mh = XInternAtom(d, kMaxHorz, False);

    REQUIRE_FALSE(hasState(d, win, kMaxVert));
    REQUIRE_FALSE(hasState(d, win, kMaxHorz));

    // ONE message, both axes. This is the form the EWMH defines for maximize and
    // the one an implementation that only reads data.l[1] silently half-honours.
    sendStateMessage(d, win, kStateAdd, mv, mh);
    REQUIRE(awaitState(d, [&] {
        return hasState(d, win, kMaxVert) && hasState(d, win, kMaxHorz);
    }));

    // And removed the same way, in one message.
    sendStateMessage(d, win, kStateRemove, mv, mh);
    REQUIRE(awaitState(d, [&] {
        return !hasState(d, win, kMaxVert) && !hasState(d, win, kMaxHorz);
    }));

    REQUIRE(fixture.wmAlive());
    INFO("wm stderr:\n" << fixture.wmStderr());
    CHECK(joined(xProtocolErrorsExceptBadWindow(fixture.wmStderr())).empty());
}

TEST_CASE("A two-property toggle flips each property independently", "[wm_state]")
{
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    Window win = None;
    REQUIRE(mapClientAndAwaitFrame(d, 120, 90, 300, 200, win) != None);

    const Atom mv = XInternAtom(d, kMaxVert, False);
    const Atom mh = XInternAtom(d, kMaxHorz, False);

    // Start ASYMMETRIC -- vert on, horz off. A toggle of both must end with vert
    // OFF and horz ON, which no add-all or remove-all implementation produces
    // and which a symmetric starting point could not distinguish.
    sendStateMessage(d, win, kStateAdd, mv, None);
    REQUIRE(awaitState(d, [&] { return hasState(d, win, kMaxVert); }));
    settleWm(d);
    REQUIRE_FALSE(hasState(d, win, kMaxHorz));

    sendStateMessage(d, win, kStateToggle, mv, mh);
    REQUIRE(awaitState(d, [&] {
        return !hasState(d, win, kMaxVert) && hasState(d, win, kMaxHorz);
    }));

    INFO("states after two-property toggle: " << describeStates(d, wmState(d, win)));
    CHECK_FALSE(hasState(d, win, kMaxVert));
    CHECK(hasState(d, win, kMaxHorz));

    REQUIRE(fixture.wmAlive());
    INFO("wm stderr:\n" << fixture.wmStderr());
    CHECK(joined(xProtocolErrorsExceptBadWindow(fixture.wmStderr())).empty());
}

TEST_CASE("A state message naming an unsupported state changes nothing", "[wm_state]")
{
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    Window win = None;
    REQUIRE(mapClientAndAwaitFrame(d, 120, 90, 300, 200, win) != None);

    // Give the window a state the WM DOES support, so "nothing changed" is a
    // claim about preservation rather than about an empty array staying empty.
    const Atom mv = XInternAtom(d, kMaxVert, False);
    sendStateMessage(d, win, kStateAdd, mv, None);
    REQUIRE(awaitState(d, [&] { return hasState(d, win, kMaxVert); }));

    settleWm(d);
    const std::vector<Atom> before = stateSet(d, win);
    const Rect geomBefore = rectOf(d, win);
    REQUIRE_FALSE(before.empty());

    // _NET_WM_STATE_ABOVE and _NET_WM_STATE_STICKY are real EWMH atoms this WM
    // does not implement. A message naming them must be a no-op for everything
    // else, not a rewrite of the array that drops the states it does support.
    const Atom above  = XInternAtom(d, "_NET_WM_STATE_ABOVE", False);
    const Atom sticky = XInternAtom(d, "_NET_WM_STATE_STICKY", False);
    sendStateMessage(d, win, kStateAdd, above, sticky);
    settleWm(d);

    const std::vector<Atom> after = stateSet(d, win);
    INFO("before: " << describeStates(d, before) << "  after: " << describeStates(d, after));
    CHECK(after == before);

    // Not merely absent from the array -- never adopted at all.
    CHECK_FALSE(hasState(d, win, "_NET_WM_STATE_ABOVE"));
    CHECK_FALSE(hasState(d, win, "_NET_WM_STATE_STICKY"));

    // And nothing moved.
    CHECK(rectOf(d, win) == geomBefore);

    REQUIRE(fixture.wmAlive());
    INFO("wm stderr:\n" << fixture.wmStderr());
    CHECK(joined(xProtocolErrorsExceptBadWindow(fixture.wmStderr())).empty());
}

TEST_CASE("A WM_CHANGE_STATE iconify message hides the client and adds hidden",
          "[wm_state]")
{
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    Window win = None;
    const Window frame = mapClientAndAwaitFrame(d, 120, 90, 300, 200, win);
    REQUIRE(frame != None);
    REQUIRE_FALSE(hasState(d, win, kHidden));

    requestIconify(d, win);

    REQUIRE(awaitState(d, [&] { return hasState(d, win, kHidden); }));

    // Three independent observations, because "the WM set an atom" is not the
    // same claim as "the window is iconified".
    settleWm(d);
    CHECK(hasState(d, win, kHidden));
    CHECK_FALSE(isMapped(d, frame));

    long state = -1;
    REQUIRE(icccmState(d, win, state));
    CHECK(state == IconicState);

    // Still exactly one client-list entry -- a hide is a transfer, not a copy.
    CHECK(occurrences(d, win) == 1);

    REQUIRE(fixture.wmAlive());
    INFO("wm stderr:\n" << fixture.wmStderr());
    CHECK(joined(xProtocolErrorsExceptBadWindow(fixture.wmStderr())).empty());
}

TEST_CASE("A state message with the wrong format is ignored", "[wm_state]")
{
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    Window win = None;
    REQUIRE(mapClientAndAwaitFrame(d, 120, 90, 300, 200, win) != None);

    const Atom mv = XInternAtom(d, kMaxVert, False);
    sendStateMessage(d, win, kStateAdd, mv, None);
    REQUIRE(awaitState(d, [&] { return hasState(d, win, kMaxVert); }));

    settleWm(d);
    const std::vector<Atom> before = stateSet(d, win);
    const Rect geomBefore = rectOf(d, win);

    // Format 8 and format 16 both make data.l[] meaningless: the server packs
    // the union as bytes or shorts, so l[1] does not hold the atom the sender
    // wrote there. A WM that reads l[] regardless is acting on garbage.
    const Atom fs = XInternAtom(d, kFullscreen, False);
    sendStateMessage(d, win, kStateAdd, fs, None, /*source=*/1, /*format=*/8);
    settleWm(d);
    CHECK(stateSet(d, win) == before);
    CHECK_FALSE(hasState(d, win, kFullscreen));

    sendStateMessage(d, win, kStateAdd, fs, None, /*source=*/1, /*format=*/16);
    settleWm(d);

    const std::vector<Atom> after = stateSet(d, win);
    INFO("before: " << describeStates(d, before) << "  after: " << describeStates(d, after));
    CHECK(after == before);
    CHECK_FALSE(hasState(d, win, kFullscreen));
    CHECK(rectOf(d, win) == geomBefore);

    REQUIRE(fixture.wmAlive());
    INFO("wm stderr:\n" << fixture.wmStderr());
    CHECK(joined(xProtocolErrorsExceptBadWindow(fixture.wmStderr())).empty());
}

TEST_CASE("A state message naming an unmanaged window is ignored and does not crash",
          "[wm_state]")
{
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    // A managed bystander, so "nothing changed" is observable rather than
    // vacuous: if the WM applied a misaddressed message to whatever it had to
    // hand, this is the window it would land on.
    Window bystander = None;
    REQUIRE(mapClientAndAwaitFrame(d, 400, 300, 200, 150, bystander) != None);
    const Atom mv = XInternAtom(d, kMaxVert, False);
    sendStateMessage(d, bystander, kStateAdd, mv, None);
    REQUIRE(awaitState(d, [&] { return hasState(d, bystander, kMaxVert); }));

    settleWm(d);
    const std::vector<Atom> before = stateSet(d, bystander);
    const Window activeBefore = activeWindow(d);

    const Atom fs = XInternAtom(d, kFullscreen, False);

    // (a) an unmapped, never-managed window that nonetheless exists.
    //
    // This is the case that matters most, and it is NOT hypothetical: the WM
    // builds a Client for every non-override-redirect top-level window at
    // CreateNotify time, long before the window is mapped. Such a window is
    // Withdrawn -- it has no mapped frame, manage() has never run, and it is
    // absent from _NET_CLIENT_LIST -- yet it is reachable through
    // windowToClient(). Any client on the display can name ANOTHER
    // application's not-yet-mapped window here.
    //
    // Two independent damages are asserted, because the published property is
    // the smaller half: the geometry mutation is what a real application would
    // actually notice when it finally maps.
    Window unmanaged = createClient(d, 10, 10, 50, 50);
    REQUIRE(unmanaged != None);
    REQUIRE_FALSE(listed(d, unmanaged));
    const Rect unmanagedBefore = rectOf(d, unmanaged);
    REQUIRE(unmanagedBefore.w == 50);
    sendStateMessage(d, unmanaged, kStateAdd, fs, None);
    settleWm(d);
    INFO("unmanaged geometry: " << describe(unmanagedBefore) << " -> "
         << describe(rectOf(d, unmanaged)));
    CHECK_FALSE(hasState(d, unmanaged, kFullscreen));
    CHECK(rectOf(d, unmanaged) == unmanagedBefore);

    // (b) an override-redirect window, which the WM refuses to manage by policy.
    XSetWindowAttributes attr;
    attr.override_redirect = True;
    Window orWin = XCreateWindow(d, DefaultRootWindow(d), 600, 600, 40, 40, 0,
                                 CopyFromParent, InputOutput, CopyFromParent,
                                 CWOverrideRedirect, &attr);
    XMapWindow(d, orWin);
    XSync(d, False);
    settleWm(d);
    REQUIRE_FALSE(listed(d, orWin));
    sendStateMessage(d, orWin, kStateAdd, fs, None);
    settleWm(d);
    CHECK_FALSE(hasState(d, orWin, kFullscreen));

    // (c) a window id that names nothing at all. The interesting failure mode is
    // a null dereference inside eventClient, which no property read would show.
    sendStateMessage(d, static_cast<Window>(0x7fffffff), kStateAdd, fs, None);
    requestIconify(d, static_cast<Window>(0x7fffffff));
    settleWm(d);

    // The bystander is untouched and the WM is still working.
    CHECK(stateSet(d, bystander) == before);
    CHECK(activeWindow(d) == activeBefore);
    REQUIRE(fixture.wmAlive());

    // Still managing: a window mapped after all that is still framed.
    Window later = None;
    CHECK(mapClientAndAwaitFrame(d, 700, 100, 120, 90, later) != None);

    XDestroyWindow(d, orWin);
    XDestroyWindow(d, unmanaged);
    XSync(d, False);
}


// ===========================================================================
// [wm_fsmax] -- fullscreen and maximize geometry restore, in the awkward
//               orderings (checklist coverage item 11)
// ===========================================================================

namespace {

// The mapped children of a window, sorted. For a frame this is {tab, button,
// client}; the resize handle is a child of the CLIENT, not of the frame.
//
// Captured before a state change and compared after the restore, this is the
// "frame and tab intact" assertion: an implementation that rebuilt the frame
// but forgot to remap the tab, or that left a component behind, fails it. A
// count would not -- the identities are what make it a restore rather than a
// replacement.
std::vector<Window> mappedChildren(Display* d, Window w)
{
    std::vector<Window> out;
    for (Window c : childrenOf(d, w)) {
        if (isMapped(d, c)) out.push_back(c);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool awaitWorkarea(Display* d, const Rect& want, int timeoutMs = 8000)
{
    return WmFixture::pollUntil([&] {
        pumpWm(d);
        return workareaOf(d) == want;
    }, timeoutMs);
}

bool awaitRect(Display* d, Window w, const Rect& want, int timeoutMs = 8000)
{
    return WmFixture::pollUntil([&] {
        pumpWm(d);
        return rectOf(d, w) == want;
    }, timeoutMs);
}

// The frame's content offset, measured at run time rather than hardcoded: it
// depends on the tab width, which depends on the font, which depends on what
// fontconfig resolves on the host. The client sits at exactly this offset
// inside its frame on every geometry path in the WM
// (Border::reparent(), Client::resize()).
Rect frameIndent(Display* d, Window client, Window frame)
{
    const Rect c = rectOf(d, client);
    const Rect f = rectOf(d, frame);
    Rect out;
    out.x = c.x - f.x;
    out.y = c.y - f.y;
    return out;
}

const Rect kScreenRect{0, 0, kScreenW, kScreenH};

}  // namespace


TEST_CASE("Fullscreen covers the whole screen including the dock, and restores exactly",
          "[wm_fsmax]")
{
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    // A dock, so "the whole screen" and "the workarea" are different rectangles
    // and the assertion below can tell them apart.
    const Window dock = createDock(d);
    REQUIRE(dock != None);
    REQUIRE(awaitWorkarea(d, Rect{0, 0, kScreenW, kScreenH - kDockHeight}));

    Window win = None;
    const Window frame = mapClientAndAwaitFrame(d, 150, 120, 300, 220, win);
    REQUIRE(frame != None);

    settleWm(d);
    const Rect clientBefore = rectOf(d, win);
    const Rect frameBefore  = rectOf(d, frame);
    const std::vector<Window> childrenBefore = mappedChildren(d, frame);
    REQUIRE(clientBefore.w == 300);
    REQUIRE(childrenBefore.size() >= 2);   // tab and button, at least

    const Atom fs = XInternAtom(d, kFullscreen, False);
    sendStateMessage(d, win, kStateAdd, fs, None);

    // Per D-05 fullscreen covers the SCREEN, not the workarea -- so the dock's
    // 40 rows are covered too. Asserting the exact screen rect is what
    // distinguishes the two: a workarea-based implementation gives 1024x728.
    INFO("workarea: " << describe(workareaOf(d)));
    REQUIRE(awaitRect(d, win, kScreenRect));
    CHECK(rectOf(d, win).h == kScreenH);
    CHECK(rectOf(d, win).h != workareaOf(d).h);

    // The frame is out of the way while fullscreen -- the client is a direct
    // child of root, not sitting inside a frame that would clip it.
    CHECK(parentOf(d, win) == DefaultRootWindow(d));

    sendStateMessage(d, win, kStateRemove, fs, None);
    REQUIRE(awaitRect(d, win, clientBefore));

    settleWm(d);
    INFO("client " << describe(clientBefore) << " -> " << describe(rectOf(d, win)));
    INFO("frame  " << describe(frameBefore)  << " -> " << describe(rectOf(d, frame)));
    CHECK(rectOf(d, win) == clientBefore);
    CHECK(rectOf(d, frame) == frameBefore);
    CHECK(parentOf(d, win) == frame);
    CHECK(isMapped(d, frame));
    CHECK(mappedChildren(d, frame) == childrenBefore);

    REQUIRE(fixture.wmAlive());
    INFO("wm stderr:\n" << fixture.wmStderr());
    CHECK(joined(xProtocolErrorsExceptBadWindow(fixture.wmStderr())).empty());
}

TEST_CASE("Maximize fills the workarea rather than the screen, and restores exactly",
          "[wm_fsmax]")
{
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    const Window dock = createDock(d);
    REQUIRE(dock != None);
    const Rect wa{0, 0, kScreenW, kScreenH - kDockHeight};
    REQUIRE(awaitWorkarea(d, wa));

    Window win = None;
    const Window frame = mapClientAndAwaitFrame(d, 150, 120, 300, 220, win);
    REQUIRE(frame != None);

    settleWm(d);
    const Rect clientBefore = rectOf(d, win);
    const Rect frameBefore  = rectOf(d, frame);
    const Rect indent       = frameIndent(d, win, frame);
    const std::vector<Window> childrenBefore = mappedChildren(d, frame);
    REQUIRE(indent.x > 0);                 // the sideways tab is on the left
    REQUIRE(indent.y > 0);

    const Atom mv = XInternAtom(d, kMaxVert, False);
    const Atom mh = XInternAtom(d, kMaxHorz, False);
    sendStateMessage(d, win, kStateAdd, mv, mh);
    REQUIRE(awaitState(d, [&] {
        return hasState(d, win, kMaxVert) && hasState(d, win, kMaxHorz);
    }));
    settleWm(d);

    const Rect frameMax  = rectOf(d, frame);
    const Rect clientMax = rectOf(d, win);
    INFO("workarea " << describe(wa) << "  frame " << describe(frameMax)
         << "  client " << describe(clientMax) << "  indent " << describe(indent));

    // Unlike fullscreen, maximize KEEPS the frame -- so what has to fill the
    // workarea is the frame, not the bare client. The whole decorated window
    // must be inside the workarea: a maximized window whose tab has been pushed
    // off the left edge of the screen is not maximized, it is lost.
    CHECK(frameMax.x >= wa.x);
    CHECK(frameMax.y >= wa.y);
    CHECK(frameMax.x + frameMax.w <= wa.x + wa.w);
    CHECK(frameMax.y + frameMax.h <= wa.y + wa.h);

    // It fills it rather than merely fitting in it -- within the one-pixel
    // slack Border::configure() adds to the frame's own width and height.
    CHECK(frameMax.w >= wa.w - 2);
    CHECK(frameMax.h >= wa.h - 2);

    // The workarea, NOT the screen: the dock's rows are still the dock's.
    CHECK(frameMax.y + frameMax.h <= kScreenH - kDockHeight);

    // The client stays at the frame's content offset -- the same relationship
    // every other geometry path in the WM maintains. At (0,0) it would be drawn
    // underneath the sideways tab and the frame border.
    CHECK(clientMax.x - frameMax.x == indent.x);
    CHECK(clientMax.y - frameMax.y == indent.y);

    // Frame and tab intact throughout -- maximize does not strip them.
    CHECK(isMapped(d, frame));
    CHECK(parentOf(d, win) == frame);
    CHECK(mappedChildren(d, frame) == childrenBefore);

    sendStateMessage(d, win, kStateRemove, mv, mh);
    REQUIRE(awaitRect(d, win, clientBefore));
    settleWm(d);
    CHECK(rectOf(d, win) == clientBefore);
    CHECK(rectOf(d, frame) == frameBefore);

    REQUIRE(fixture.wmAlive());
    INFO("wm stderr:\n" << fixture.wmStderr());
    CHECK(joined(xProtocolErrorsExceptBadWindow(fixture.wmStderr())).empty());
}

TEST_CASE("Single-axis maximize changes only that axis, and restores exactly",
          "[wm_fsmax]")
{
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    const Window dock = createDock(d);
    REQUIRE(dock != None);
    const Rect wa{0, 0, kScreenW, kScreenH - kDockHeight};
    REQUIRE(awaitWorkarea(d, wa));

    Window win = None;
    const Window frame = mapClientAndAwaitFrame(d, 150, 120, 300, 220, win);
    REQUIRE(frame != None);
    settleWm(d);

    const Rect clientBefore = rectOf(d, win);
    const Rect frameBefore  = rectOf(d, frame);

    const Atom mv = XInternAtom(d, kMaxVert, False);
    const Atom mh = XInternAtom(d, kMaxHorz, False);

    SECTION("vertical only")
    {
        sendStateMessage(d, win, kStateAdd, mv, None);
        REQUIRE(awaitState(d, [&] { return hasState(d, win, kMaxVert); }));
        settleWm(d);

        const Rect f = rectOf(d, frame);
        const Rect c = rectOf(d, win);
        INFO("frame " << describe(frameBefore) << " -> " << describe(f)
             << "  client " << describe(clientBefore) << " -> " << describe(c));

        // The vertical axis moved to fill the workarea...
        CHECK(f.y >= wa.y);
        CHECK(f.y + f.h <= wa.y + wa.h);
        CHECK(f.h >= wa.h - 2);
        // ...and the horizontal axis did NOT.
        CHECK(c.x == clientBefore.x);
        CHECK(c.w == clientBefore.w);
        CHECK(f.x == frameBefore.x);
        CHECK(f.w == frameBefore.w);
        CHECK_FALSE(hasState(d, win, kMaxHorz));

        // The restore must reach the geometry the window actually had. The
        // saved-geometry slot is only useful if a SINGLE-axis maximize fills
        // it: an implementation that saves only on the both-axes transition
        // restores this window to whatever the slot was initialised with.
        sendStateMessage(d, win, kStateRemove, mv, None);
        REQUIRE(awaitRect(d, win, clientBefore));
        settleWm(d);
        CHECK(rectOf(d, win) == clientBefore);
        CHECK(rectOf(d, frame) == frameBefore);
    }

    SECTION("horizontal only")
    {
        sendStateMessage(d, win, kStateAdd, mh, None);
        REQUIRE(awaitState(d, [&] { return hasState(d, win, kMaxHorz); }));
        settleWm(d);

        const Rect f = rectOf(d, frame);
        const Rect c = rectOf(d, win);
        INFO("frame " << describe(frameBefore) << " -> " << describe(f)
             << "  client " << describe(clientBefore) << " -> " << describe(c));

        CHECK(f.x >= wa.x);
        CHECK(f.x + f.w <= wa.x + wa.w);
        CHECK(f.w >= wa.w - 2);
        CHECK(c.y == clientBefore.y);
        CHECK(c.h == clientBefore.h);
        CHECK(f.y == frameBefore.y);
        CHECK(f.h == frameBefore.h);
        CHECK_FALSE(hasState(d, win, kMaxVert));

        sendStateMessage(d, win, kStateRemove, mh, None);
        REQUIRE(awaitRect(d, win, clientBefore));
        settleWm(d);
        CHECK(rectOf(d, win) == clientBefore);
        CHECK(rectOf(d, frame) == frameBefore);
    }

    REQUIRE(fixture.wmAlive());
    INFO("wm stderr:\n" << fixture.wmStderr());
    CHECK(joined(xProtocolErrorsExceptBadWindow(fixture.wmStderr())).empty());
}

TEST_CASE("Fullscreen requested while hidden takes effect when the client is unhidden",
          "[wm_fsmax]")
{
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    Window win = None;
    const Window frame = mapClientAndAwaitFrame(d, 150, 120, 300, 220, win);
    REQUIRE(frame != None);
    settleWm(d);
    const Rect clientBefore = rectOf(d, win);

    // Hide it. A hidden client is still MANAGED -- which is exactly why the
    // eventClient guard is !isWithdrawn() and not !isNormal().
    requestIconify(d, win);
    REQUIRE(awaitState(d, [&] { return hasState(d, win, kHidden); }));
    settleWm(d);
    REQUIRE_FALSE(isMapped(d, frame));

    const Atom fs = XInternAtom(d, kFullscreen, False);
    sendStateMessage(d, win, kStateAdd, fs, None);
    REQUIRE(awaitState(d, [&] { return hasState(d, win, kFullscreen); }));

    // Now bring it back. The window must come back FULLSCREEN, not at the
    // geometry it had when it was hidden -- and not as a fullscreen client with
    // an abandoned empty frame still on screen behind it.
    XMapWindow(d, win);
    XSync(d, False);
    REQUIRE(awaitState(d, [&] { return !hasState(d, win, kHidden); }));

    INFO("client " << describe(clientBefore) << " -> " << describe(rectOf(d, win)));
    INFO("wm stderr:\n" << fixture.wmStderr());
    REQUIRE(awaitRect(d, win, kScreenRect));
    CHECK(isMapped(d, win));
    CHECK(parentOf(d, win) == DefaultRootWindow(d));

    // No stale frame left mapped over the desktop.
    CHECK_FALSE(isMapped(d, frame));

    // And it can still be un-fullscreened afterwards, back to the geometry it
    // had before any of this.
    sendStateMessage(d, win, kStateRemove, fs, None);
    REQUIRE(awaitRect(d, win, clientBefore));
    CHECK(isMapped(d, frame));

    REQUIRE(fixture.wmAlive());
    CHECK(joined(xProtocolErrorsExceptBadWindow(fixture.wmStderr())).empty());
}

TEST_CASE("Destroying a client while it is fullscreen leaves nothing behind",
          "[wm_fsmax]")
{
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    // A second client, so "the active window is not the dead one" is a real
    // observation rather than a comparison against None either way.
    Window other = None;
    REQUIRE(mapClientAndAwaitFrame(d, 600, 400, 200, 160, other) != None);

    Window win = None;
    const Window frame = mapClientAndAwaitFrame(d, 150, 120, 300, 220, win);
    REQUIRE(frame != None);

    const Atom fs = XInternAtom(d, kFullscreen, False);
    sendStateMessage(d, win, kStateAdd, fs, None);
    REQUIRE(awaitRect(d, win, kScreenRect));
    REQUIRE(listed(d, win));

    // The teardown ordering this case exists for: the fullscreen path has
    // STRIPPED the frame -- the client is reparented to root and the frame
    // components are unmapped -- so ~Client()/~Border() run against a structure
    // the ordinary destroy case never sees.
    XDestroyWindow(d, win);
    XSync(d, False);

    REQUIRE(WmFixture::pollUntil([&] {
        pumpWm(d);
        return !listed(d, win);
    }, 8000));

    settleWm(d);
    CHECK(occurrences(d, win) == 0);
    CHECK(activeWindow(d) != win);
    CHECK(listed(d, other));
    REQUIRE(fixture.wmAlive());

    // Still managing afterwards.
    Window later = None;
    CHECK(mapClientAndAwaitFrame(d, 700, 100, 120, 90, later) != None);

    INFO("wm stderr:\n" << fixture.wmStderr());
    CHECK(joined(xProtocolErrorsExceptBadWindow(fixture.wmStderr())).empty());
    CHECK(fixture.asanReports().empty());
}

TEST_CASE("A dock mapped after a window is maximized leaves its restore geometry intact",
          "[wm_fsmax]")
{
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    // No dock yet: the workarea IS the screen.
    REQUIRE(awaitWorkarea(d, kScreenRect));

    Window win = None;
    const Window frame = mapClientAndAwaitFrame(d, 150, 120, 300, 220, win);
    REQUIRE(frame != None);
    settleWm(d);
    const Rect clientBefore = rectOf(d, win);
    const Rect frameBefore  = rectOf(d, frame);

    const Atom mv = XInternAtom(d, kMaxVert, False);
    const Atom mh = XInternAtom(d, kMaxHorz, False);
    sendStateMessage(d, win, kStateAdd, mv, mh);
    REQUIRE(awaitState(d, [&] {
        return hasState(d, win, kMaxVert) && hasState(d, win, kMaxHorz);
    }));
    settleWm(d);
    const Rect frameMaxBefore = rectOf(d, frame);

    // Now the workarea shrinks UNDERNEATH an already-maximized window.
    const Window dock = createDock(d);
    REQUIRE(dock != None);
    REQUIRE(awaitWorkarea(d, Rect{0, 0, kScreenW, kScreenH - kDockHeight}));
    settleWm(d);

    INFO("frame while maximized: " << describe(frameMaxBefore) << " -> "
         << describe(rectOf(d, frame)));
    INFO("wm stderr:\n" << fixture.wmStderr());

    // The WM stays consistent: the window is still managed, still maximized,
    // still framed, and no protocol error was logged on the way.
    CHECK(occurrences(d, win) == 1);
    CHECK(hasState(d, win, kMaxVert));
    CHECK(hasState(d, win, kMaxHorz));
    CHECK(isMapped(d, frame));
    CHECK(parentOf(d, win) == frame);
    REQUIRE(fixture.wmAlive());

    // And the saved geometry is not collateral damage: unmaximizing returns the
    // window to where the USER left it, not to something derived from whatever
    // the workarea happened to be at the time.
    sendStateMessage(d, win, kStateRemove, mv, mh);
    REQUIRE(awaitRect(d, win, clientBefore));
    settleWm(d);
    CHECK(rectOf(d, win) == clientBefore);
    CHECK(rectOf(d, frame) == frameBefore);

    CHECK(joined(xProtocolErrorsExceptBadWindow(fixture.wmStderr())).empty());
}

TEST_CASE("Maximizing straight after leaving fullscreen uses workarea geometry",
          "[wm_fsmax]")
{
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    const Window dock = createDock(d);
    REQUIRE(dock != None);
    const Rect wa{0, 0, kScreenW, kScreenH - kDockHeight};
    REQUIRE(awaitWorkarea(d, wa));

    Window win = None;
    const Window frame = mapClientAndAwaitFrame(d, 150, 120, 300, 220, win);
    REQUIRE(frame != None);
    settleWm(d);
    const Rect clientBefore = rectOf(d, win);
    const Rect frameBefore  = rectOf(d, frame);

    const Atom fs = XInternAtom(d, kFullscreen, False);
    const Atom mv = XInternAtom(d, kMaxVert, False);
    const Atom mh = XInternAtom(d, kMaxHorz, False);

    // Fullscreen on, then off.
    sendStateMessage(d, win, kStateAdd, fs, None);
    REQUIRE(awaitRect(d, win, kScreenRect));
    sendStateMessage(d, win, kStateRemove, fs, None);
    REQUIRE(awaitRect(d, win, clientBefore));

    // Then maximize, immediately. This is the aliasing check: if the two saved
    // geometry slots were one slot, the fullscreen exit would have left the
    // SCREEN rectangle in it and the maximize below would cover the dock.
    sendStateMessage(d, win, kStateAdd, mv, mh);
    REQUIRE(awaitState(d, [&] {
        return hasState(d, win, kMaxVert) && hasState(d, win, kMaxHorz);
    }));
    settleWm(d);

    const Rect f = rectOf(d, frame);
    INFO("workarea " << describe(wa) << "  frame after fullscreen->maximize "
         << describe(f));
    CHECK_FALSE(hasState(d, win, kFullscreen));
    CHECK(f.y + f.h <= wa.y + wa.h);            // workarea, not screen
    CHECK(f.h >= wa.h - 2);
    CHECK(parentOf(d, win) == frame);           // framed, not stripped

    // And the restore still reaches the geometry from BEFORE the fullscreen
    // round trip -- the slot the fullscreen exit repopulated is the one
    // maximize saved from, so a shared slot shows up here too.
    sendStateMessage(d, win, kStateRemove, mv, mh);
    REQUIRE(awaitRect(d, win, clientBefore));
    settleWm(d);
    CHECK(rectOf(d, win) == clientBefore);
    CHECK(rectOf(d, frame) == frameBefore);

    REQUIRE(fixture.wmAlive());
    INFO("wm stderr:\n" << fixture.wmStderr());
    CHECK(joined(xProtocolErrorsExceptBadWindow(fixture.wmStderr())).empty());
}
