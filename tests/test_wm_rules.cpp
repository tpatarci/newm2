// Window-rule application tests against the REAL compiled wm2-born-again binary
// (RULES-02, plan 08-10).
//
// Plan 08-09 proved the rule MODEL: given a WindowRule and three facts, does the
// matcher say yes, and does the fold resolve later-wins. It proved all of that
// with no X server and no window manager, because nothing was wired to anything.
// This file is the other half -- it proves that a user who writes four lines in
// their config file gets a different window on their screen.
//
// Tag: [wm_rules] -- registered as a ctest LABEL via ADD_TAGS_AS_LABELS (D-33),
// selected with `ctest -L '^wm_rules$' --no-tests=error`.
//
// Three rules this file follows throughout:
//
//   RULES REACH THE WM THROUGH A REAL CONFIG FILE. Each case writes an isolated
//   XDG config tree and points the WM's child environment at it. There is no
//   back door: the text goes through the same Config::load() chain a user's
//   ~/.config/wm2-born-again/config goes through. A rule that parses but never
//   reaches a runtime branch fails here, which is precisely the gap that made
//   plan 08-07 necessary for the focus booleans.
//
//   OUTCOMES ARE OBSERVED THROUGH THE SERVER, never through WM internals.
//   Framing is "what is this window's parent" from XQueryTree. Geometry is
//   XGetGeometry plus XTranslateCoordinates. Skip-taskbar is the _NET_WM_STATE
//   atom list on the client window.
//
//   NON-EVENTS ARE PROVEN BY SETTLING FIRST. Deferred item 9: the WM does not
//   flush its X output until its event loop wakes, so a single read after a
//   single nudge reliably observes the PREVIOUS state and a negative assertion
//   passes for the wrong reason. Every "this did NOT happen" read here runs
//   settleWm() first -- fifteen SPACED nudges, the shape tests/test_wm_focus.cpp
//   arrived at the hard way.

#include <catch2/catch_test_macros.hpp>

#include "support/WmFixture.h"

#include "x11wrap.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace wm2test;

namespace {

// The Xvfb geometry WmFixture starts every display with.
constexpr int kScreenW = 1024;
constexpr int kScreenH = 768;

// ---------------------------------------------------------------------------
// Config plumbing
// ---------------------------------------------------------------------------

// Write an isolated XDG config tree and return the directory to hand the child
// as XDG_CONFIG_HOME. Lives under the CMake binary directory rather than a bare
// /tmp name (threat T-8-TMP), and is unique per process and per case so two
// concurrent ctest workers cannot read each other's rules.
std::string makeConfigTree(const std::string& contents)
{
    static int counter = 0;
    const std::string base = std::string(WM2_TEST_WORKDIR) + "/rules-cfg-" +
                             std::to_string(::getpid()) + "-" +
                             std::to_string(++counter);
    ::mkdir(base.c_str(), 0700);
    const std::string dir = base + "/wm2-born-again";
    ::mkdir(dir.c_str(), 0700);

    std::ofstream out(dir + "/config");
    out << contents;
    out.close();
    return base;
}

// A fixture whose WM reads exactly the rules given here and nothing else.
//
// XDG_CONFIG_DIRS is overridden as well as XDG_CONFIG_HOME, and that is not
// belt-and-braces: xdgConfigDirs() falls back to /etc/xdg when the variable is
// unset, so on a machine that happens to have /etc/xdg/wm2-born-again/config the
// host's rules would be layered UNDER every case in this file and quietly change
// what it proves. Pointed at a directory that does not exist, which applyFile()
// skips silently.
WmFixtureOptions rulesFixture(const std::string& configContents)
{
    WmFixtureOptions o;
    const std::string home = makeConfigTree(configContents);
    o.childEnv["XDG_CONFIG_HOME"] = home;
    o.childEnv["XDG_CONFIG_DIRS"] = home + "/no-system-config";
    return o;
}

// ---------------------------------------------------------------------------
// Server observation
// ---------------------------------------------------------------------------

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
};

std::string describe(const Rect& r)
{
    return "(" + std::to_string(r.x) + "," + std::to_string(r.y) + " " +
           std::to_string(r.w) + "x" + std::to_string(r.h) + ")";
}

// Wake the WM's event loop so it flushes its X output buffer (deferred item 9).
// The nudge is override-redirect, so WindowManager::eventCreate returns
// immediately for it and it can never be managed or perturb an assertion.
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

// Fifteen nudges at 20ms intervals. The SPACING is as load-bearing as the count
// -- see the file header and tests/test_wm_focus.cpp, where a tight loop of the
// same length still read the stale value.
void settleWm(Display* d)
{
    for (int i = 0; i < 15; ++i) {
        pumpWm(d);
        pollSleep();
    }
}

Window parentOf(Display* d, Window w)
{
    Window wroot = None, parent = None, *children = nullptr;
    unsigned int n = 0;
    if (!XQueryTree(d, w, &wroot, &parent, &children, &n)) return None;
    if (children) XFree(children);
    return parent;
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

// The _NET_WM_STATE atom list on a client window. Absent property reads as an
// empty list, which is the correct interpretation and keeps the negative
// assertions below from having to distinguish "no property" from "no states".
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
    if (raw && actualType == XA_ATOM && actualFormat == 32) {
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

// ---------------------------------------------------------------------------
// Client creation
// ---------------------------------------------------------------------------

// Create (but do NOT map) a client with the given WM_CLASS.
//
// The class hint MUST be set before the map request. The WM reads it inside
// Client::manage(), so a hint written after the map would race the read it is
// supposed to govern -- the same ordering constraint _NET_WM_USER_TIME has in
// tests/test_wm_focus.cpp.
Window createClient(Display* d, int x, int y, int w, int h,
                    const char* instance, const char* cls)
{
    Window root = DefaultRootWindow(d);
    Window win = XCreateSimpleWindow(d, root, x, y,
                                     static_cast<unsigned>(w), static_cast<unsigned>(h), 0,
                                     BlackPixel(d, DefaultScreen(d)),
                                     WhitePixel(d, DefaultScreen(d)));
    if (instance || cls) {
        XClassHint hint;
        hint.res_name  = const_cast<char*>(instance);
        hint.res_class = const_cast<char*>(cls);
        XSetClassHint(d, win, &hint);
    }
    XSync(d, False);
    return win;
}

void setWindowType(Display* d, Window w, const char* typeAtomName)
{
    Atom prop = XInternAtom(d, "_NET_WM_WINDOW_TYPE", False);
    Atom val  = XInternAtom(d, typeAtomName, False);
    XChangeProperty(d, w, prop, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&val), 1);
    XSync(d, False);
}

// Wait until the WM has published `win` in _NET_CLIENT_LIST.
//
// This is the readiness signal for BOTH framed and unframed windows, which is
// exactly why it is used here rather than "wait until it is reparented": the
// no-decorate case has no frame to wait for, and waiting for one would time out
// on a correct implementation.
bool awaitManaged(Display* d, Window win, int timeoutMs = 8000)
{
    Window root = DefaultRootWindow(d);
    Atom clientList = XInternAtom(d, "_NET_CLIENT_LIST", False);

    return WmFixture::pollUntil([&] {
        pumpWm(d);
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
    }, timeoutMs);
}

// Map a prepared window and wait until the WM has taken it under management.
bool mapAndAwait(Display* d, Window win)
{
    XMapWindow(d, win);
    XSync(d, False);
    if (!awaitManaged(d, win)) return false;
    settleWm(d);
    return true;
}

} // namespace


// ===========================================================================
// Behaviour 1: no-decorate
// ===========================================================================

TEST_CASE("A window matching a no-decorate rule is managed without a frame", "[wm_rules]")
{
    // Position is on the same rule deliberately. The no-decorate early return is
    // the one path that could plausibly skip geometry entirely, and a plan
    // prohibition says it must not -- so the case that exercises the early
    // return is also the case that pins its geometry.
    WmFixture fx(rulesFixture(
        "rule-match-class=WmRulesNoDecorate\n"
        "rule-no-decorate=true\n"
        "rule-position=140,160\n"));

    auto conn = fx.openDisplay();
    REQUIRE(conn != nullptr);
    Display* d = conn.get();
    const Window root = DefaultRootWindow(d);

    Window win = createClient(d, 300, 300, 260, 200, "wmrulesnodecorate", "WmRulesNoDecorate");
    REQUIRE(mapAndAwait(d, win));

    INFO("wm stderr:\n" << fx.wmStderr());

    // No frame: the window's parent is still root, so nothing was reparented.
    REQUIRE(parentOf(d, win) == root);

    // And the rule's position was still honoured. With no frame there is no
    // gravity offset to account for, so the client lands exactly where asked.
    Rect r{};
    REQUIRE(serverRect(d, win, r));
    INFO("client rect " << describe(r));
    REQUIRE(r.x == 140);
    REQUIRE(r.y == 160);
}


// ===========================================================================
// Behaviour 2: position and size
// ===========================================================================

TEST_CASE("A position rule places a window and a size rule sizes it", "[wm_rules]")
{
    // Two independent rules in ONE config file, matching two different windows
    // against the same running WM. That is a stronger arrangement than a fixture
    // per action: it also proves the two rules do not bleed into each other,
    // which a single-rule file cannot show.
    WmFixture fx(rulesFixture(
        "rule-match-class=WmRulesPos\n"
        "rule-position=100,120\n"
        "rule-match-class=WmRulesSize\n"
        "rule-size=400x300\n"));

    auto conn = fx.openDisplay();
    REQUIRE(conn != nullptr);
    Display* d = conn.get();
    const Window root = DefaultRootWindow(d);

    Window posWin = createClient(d, 400, 400, 300, 220, "wmrulespos", "WmRulesPos");
    REQUIRE(mapAndAwait(d, posWin));

    // Framed, so the rule position is the FRAME's outer corner -- the same
    // NorthWest-gravity contract a client-requested position gets, which is what
    // "put this application at 100,120" means to the user who wrote the rule.
    const Window frame = parentOf(d, posWin);
    REQUIRE(frame != root);
    REQUIRE(frame != None);

    Rect fr{};
    REQUIRE(serverRect(d, frame, fr));
    INFO("wm stderr:\n" << fx.wmStderr());
    INFO("frame rect " << describe(fr));
    REQUIRE(fr.x == 100);
    REQUIRE(fr.y == 120);

    // The size half, on a window the position rule does not select.
    Window sizeWin = createClient(d, 200, 200, 260, 180, "wmrulessize", "WmRulesSize");
    REQUIRE(mapAndAwait(d, sizeWin));

    Rect sr{};
    REQUIRE(serverRect(d, sizeWin, sr));
    INFO("sized client rect " << describe(sr) << " (asked for 260x180, rule says 400x300)");
    REQUIRE(sr.w == 400);
    REQUIRE(sr.h == 300);
}


// ===========================================================================
// Behaviour 3: an out-of-range rule position is clamped, by MOVING
// ===========================================================================

TEST_CASE("A position rule that would strand a window offscreen is clamped", "[wm_rules]")
{
    // 980 + 300 is well past the right edge of a 1024-wide screen, and 700 + 220
    // past the bottom of a 768-high one. A window manager that applied this
    // literally would put a window where the user cannot reach it -- and the
    // rule file is exactly the place a typo like this gets made once and then
    // persists across every session.
    // Both geometry paths are pinned here, because there are two of them: the
    // framed one clamps through ensureVisible(), the rule-undecorated one
    // through its own unframed equivalent. Testing only the framed path would
    // leave the no-decorate path free to strand a window with nothing to notice.
    WmFixture fx(rulesFixture(
        "rule-match-class=WmRulesClamp\n"
        "rule-position=980,700\n"
        "rule-match-class=WmRulesClampBare\n"
        "rule-no-decorate=true\n"
        "rule-position=980,700\n"));

    auto conn = fx.openDisplay();
    REQUIRE(conn != nullptr);
    Display* d = conn.get();
    const Window root = DefaultRootWindow(d);

    auto assertOnScreenAndUnshrunk = [&](Window w, const char* what) {
        Rect r{};
        REQUIRE(serverRect(d, w, r));
        INFO(what << " client rect " << describe(r)
                  << " on " << kScreenW << "x" << kScreenH);

        // Fully on screen...
        REQUIRE(r.x >= 0);
        REQUIRE(r.y >= 0);
        REQUIRE(r.x + r.w <= kScreenW);
        REQUIRE(r.y + r.h <= kScreenH);

        // ...and MOVED, not shrunk. Shrinking would also satisfy the four bounds
        // above, which is why the size is asserted alongside them: a clamp that
        // resizes silently rewrites what the application asked for.
        REQUIRE(r.w == 300);
        REQUIRE(r.h == 220);

        // And it genuinely moved -- it is not still sitting at the rule's value.
        REQUIRE(r.x < 980);
    };

    Window framed = createClient(d, 50, 50, 300, 220, "wmrulesclamp", "WmRulesClamp");
    REQUIRE(mapAndAwait(d, framed));

    Window bare = createClient(d, 50, 50, 300, 220, "wmrulesclampbare", "WmRulesClampBare");
    REQUIRE(mapAndAwait(d, bare));

    INFO("wm stderr:\n" << fx.wmStderr());

    REQUIRE(parentOf(d, framed) != root);
    REQUIRE(parentOf(d, bare) == root);

    assertOnScreenAndUnshrunk(framed, "framed");
    assertOnScreenAndUnshrunk(bare, "rule-undecorated");
}


// ===========================================================================
// Behaviour 4: skip-taskbar publishes BOTH states
// ===========================================================================

TEST_CASE("A window matching a skip-taskbar rule carries skip-taskbar and skip-pager", "[wm_rules]")
{
    WmFixture fx(rulesFixture(
        "rule-match-class=WmRulesSkip\n"
        "rule-skip-taskbar=true\n"));

    auto conn = fx.openDisplay();
    REQUIRE(conn != nullptr);
    Display* d = conn.get();

    Window win = createClient(d, 120, 120, 260, 200, "wmrulesskip", "WmRulesSkip");
    REQUIRE(mapAndAwait(d, win));

    INFO("wm stderr:\n" << fx.wmStderr());

    // Both, not one. A panel reads _NET_WM_STATE_SKIP_TASKBAR and a pager reads
    // _NET_WM_STATE_SKIP_PAGER; publishing only the first leaves the window
    // visible in half the places the user asked it to disappear from.
    REQUIRE(hasState(d, win, "_NET_WM_STATE_SKIP_TASKBAR"));
    REQUIRE(hasState(d, win, "_NET_WM_STATE_SKIP_PAGER"));
}


// ===========================================================================
// Behaviour 5: later-wins, per action
// ===========================================================================

TEST_CASE("Two matching rules fold later-wins per action, not first-match", "[wm_rules]")
{
    // Deliberately discriminating in BOTH directions:
    //   - position must come from the SECOND rule (a first-match implementation
    //     gives 60,70 and fails here);
    //   - skip-taskbar must survive from the FIRST rule (a last-matching-rule-
    //     wins-wholesale implementation drops it and fails here).
    // Either half alone would pass under one of the two wrong designs.
    WmFixture fx(rulesFixture(
        "rule-match-class=WmRulesFold\n"
        "rule-skip-taskbar=true\n"
        "rule-position=60,70\n"
        "rule-match-class=WmRulesFold\n"
        "rule-position=210,230\n"));

    auto conn = fx.openDisplay();
    REQUIRE(conn != nullptr);
    Display* d = conn.get();
    const Window root = DefaultRootWindow(d);

    Window win = createClient(d, 400, 400, 280, 200, "wmrulesfold", "WmRulesFold");
    REQUIRE(mapAndAwait(d, win));

    INFO("wm stderr:\n" << fx.wmStderr());

    const Window frame = parentOf(d, win);
    REQUIRE(frame != root);

    Rect fr{};
    REQUIRE(serverRect(d, frame, fr));
    INFO("frame rect " << describe(fr) << " (rule 1 said 60,70; rule 2 said 210,230)");
    REQUIRE(fr.x == 210);
    REQUIRE(fr.y == 230);

    // The action only the FIRST rule set is still in force.
    REQUIRE(hasState(d, win, "_NET_WM_STATE_SKIP_TASKBAR"));
    REQUIRE(hasState(d, win, "_NET_WM_STATE_SKIP_PAGER"));
}


// ===========================================================================
// Behaviour 6: a rule matching on window type alone
// ===========================================================================

TEST_CASE("A rule matching on window type alone applies to every window of that type", "[wm_rules]")
{
    // No class criterion at all -- the rule selects purely on type. Both windows
    // below run against the SAME WM, so the negative half proves the rule is
    // keyed on the type and not simply applied to everything.
    WmFixture fx(rulesFixture(
        "rule-match-type=dialog\n"
        "rule-skip-taskbar=true\n"));

    auto conn = fx.openDisplay();
    REQUIRE(conn != nullptr);
    Display* d = conn.get();

    Window dialogA = createClient(d, 100, 100, 240, 180, "dlga", "WmRulesDialogA");
    setWindowType(d, dialogA, "_NET_WM_WINDOW_TYPE_DIALOG");
    REQUIRE(mapAndAwait(d, dialogA));

    Window dialogB = createClient(d, 380, 100, 240, 180, "dlgb", "WmRulesDialogB");
    setWindowType(d, dialogB, "_NET_WM_WINDOW_TYPE_DIALOG");
    REQUIRE(mapAndAwait(d, dialogB));

    Window plain = createClient(d, 100, 340, 240, 180, "plain", "WmRulesPlain");
    REQUIRE(mapAndAwait(d, plain));

    INFO("wm stderr:\n" << fx.wmStderr());

    // Every window of the type, with no class criterion needed.
    REQUIRE(hasState(d, dialogA, "_NET_WM_STATE_SKIP_TASKBAR"));
    REQUIRE(hasState(d, dialogB, "_NET_WM_STATE_SKIP_TASKBAR"));

    // And nothing else. Settled first: a stale read here would pass for the
    // wrong reason (deferred item 9).
    settleWm(d);
    REQUIRE_FALSE(hasState(d, plain, "_NET_WM_STATE_SKIP_TASKBAR"));
    REQUIRE_FALSE(hasState(d, plain, "_NET_WM_STATE_SKIP_PAGER"));
}


// ===========================================================================
// Behaviour 7: a window that matches a configured rule's KEY but not its VALUE
// ===========================================================================

TEST_CASE("A window that matches no configured rule is untouched by the rules", "[wm_rules]")
{
    // Rules ARE configured here, and they are real -- they simply do not select
    // this window. Distinct from the no-rules control below: this proves the
    // matcher discriminates, rather than proving the machinery is inert when
    // switched off.
    WmFixture fx(rulesFixture(
        "rule-match-class=SomeOtherApplication\n"
        "rule-no-decorate=true\n"
        "rule-skip-taskbar=true\n"
        "rule-position=10,10\n"));

    auto conn = fx.openDisplay();
    REQUIRE(conn != nullptr);
    Display* d = conn.get();
    const Window root = DefaultRootWindow(d);

    Window win = createClient(d, 150, 160, 300, 220, "wmrulesmiss", "WmRulesMiss");
    REQUIRE(mapAndAwait(d, win));

    INFO("wm stderr:\n" << fx.wmStderr());

    const Window frame = parentOf(d, win);
    REQUIRE(frame != root);

    Rect fr{};
    REQUIRE(serverRect(d, frame, fr));
    INFO("frame rect " << describe(fr) << " (asked for 150,160; the rule says 10,10)");
    REQUIRE(fr.x == 150);
    REQUIRE(fr.y == 160);

    settleWm(d);
    REQUIRE_FALSE(hasState(d, win, "_NET_WM_STATE_SKIP_TASKBAR"));
    REQUIRE_FALSE(hasState(d, win, "_NET_WM_STATE_SKIP_PAGER"));
}


// ===========================================================================
// Control: no rules configured at all
// ===========================================================================

TEST_CASE("With no rules configured a window is framed, placed and stated as before", "[wm_rules]")
{
    // The control the whole file rests on. Without it, every assertion above
    // could be describing behaviour the WM had all along, and the suite would
    // prove only that rules coexist with the window manager rather than that
    // they change it.
    WmFixture fx(rulesFixture("# no rules at all\n"));

    auto conn = fx.openDisplay();
    REQUIRE(conn != nullptr);
    Display* d = conn.get();
    const Window root = DefaultRootWindow(d);

    Window win = createClient(d, 150, 160, 300, 220, "wmrulescontrol", "WmRulesControl");
    REQUIRE(mapAndAwait(d, win));

    INFO("wm stderr:\n" << fx.wmStderr());

    // Framed.
    const Window frame = parentOf(d, win);
    REQUIRE(frame != root);
    REQUIRE(frame != None);

    // Placed where the client asked, at the size the client asked for.
    Rect fr{};
    REQUIRE(serverRect(d, frame, fr));
    INFO("frame rect " << describe(fr));
    REQUIRE(fr.x == 150);
    REQUIRE(fr.y == 160);

    Rect cr{};
    REQUIRE(serverRect(d, win, cr));
    INFO("client rect " << describe(cr));
    REQUIRE(cr.w == 300);
    REQUIRE(cr.h == 220);

    // And no skip states.
    settleWm(d);
    REQUIRE_FALSE(hasState(d, win, "_NET_WM_STATE_SKIP_TASKBAR"));
    REQUIRE_FALSE(hasState(d, win, "_NET_WM_STATE_SKIP_PAGER"));
}


// ===========================================================================
// Behaviour 8: matching on the window TITLE (RULES-01, plan 08.5-01)
//
// The half of RULES-01 Phase 8 left undelivered. These drive the REAL binary:
// the matcher's own cases live in tests/test_rules.cpp and run display-free, so
// what these add is the wiring -- that the title the WM reads off a window is
// the string the fold is given.
// ===========================================================================

namespace {

// Set a title the way an ICCCM client does.
void setLegacyTitle(Display* d, Window w, const char* title)
{
    XStoreName(d, w, title);
    XSync(d, False);
}

// Set a title the way an EWMH client does. A window that sets ONLY this is the
// case that fails if the title read of plan 08.5-01 Task 1 is reverted.
void setEwmhTitle(Display* d, Window w, const char* utf8)
{
    Atom netWmName = XInternAtom(d, "_NET_WM_NAME", False);
    Atom utf8String = XInternAtom(d, "UTF8_STRING", False);
    XChangeProperty(d, w, netWmName, utf8String, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(utf8),
                    static_cast<int>(std::strlen(utf8)));
    XSync(d, False);
}

// The sideways tab's length, which tracks the window title (deferred item 11,
// fixed in 08-14). This is the cheapest proof from outside the process that the
// WM has actually READ a title -- nothing publishes a client's title back out.
//
// The client is excluded explicitly. It is reparented into the frame and so is
// one of its children, and on any realistic test window it is taller than the
// tab -- so a naive "tallest child" measures the client and never changes.
// Among what remains, the tab is the long strip and the button is the small
// square that shares its origin (08-14), so height separates them.
int tabLengthOf(Display* d, Window frame, Window client)
{
    Window wroot = None, parent = None, *children = nullptr;
    unsigned int n = 0;
    if (!XQueryTree(d, frame, &wroot, &parent, &children, &n) || !children) return -1;

    int longest = -1;
    for (unsigned int i = 0; i < n; ++i) {
        if (children[i] == client) continue;
        Rect r{};
        if (serverRect(d, children[i], r) && r.h > longest) longest = r.h;
    }
    XFree(children);
    return longest;
}

} // namespace

TEST_CASE("A title rule places a window whose title matches", "[wm_rules]")
{
    WmFixture fx(rulesFixture(
        "rule-match-title=Inbox\n"
        "rule-position=140,160\n"));

    auto conn = fx.openDisplay();
    REQUIRE(conn != nullptr);
    Display* d = conn.get();
    const Window root = DefaultRootWindow(d);

    // The title, like the class hint, must be set BEFORE the map request: the WM
    // reads it inside Client::manage(), so a title written afterwards races the
    // read that is supposed to govern the placement.
    Window win = createClient(d, 400, 400, 300, 220, "mail", "Mail");
    setLegacyTitle(d, win, "Inbox - Mail");
    REQUIRE(mapAndAwait(d, win));

    const Window frame = parentOf(d, win);
    REQUIRE(frame != root);
    REQUIRE(frame != None);

    Rect fr{};
    REQUIRE(serverRect(d, frame, fr));
    INFO("wm stderr:\n" << fx.wmStderr());
    INFO("frame rect " << describe(fr));
    REQUIRE(fr.x == 140);
    REQUIRE(fr.y == 160);

    // NEGATIVE CONTROL, same running WM: a window of the same class whose title
    // does not match is left where it asked to be. Without this the case would
    // pass against an implementation that placed everything at the rule's
    // position regardless of title.
    Window other = createClient(d, 300, 350, 300, 220, "mail", "Mail");
    setLegacyTitle(d, other, "Drafts - Mail");
    REQUIRE(mapAndAwait(d, other));

    Rect orect{};
    REQUIRE(serverRect(d, parentOf(d, other), orect));
    INFO("non-matching frame rect " << describe(orect));
    REQUIRE(orect.x != 140);
}

TEST_CASE("A title rule matches a window that advertises its title only through _NET_WM_NAME",
          "[wm_rules]")
{
    // THE CASE THAT JOINS THE TWO HALVES OF THIS PLAN. Task 1 made the WM read
    // _NET_WM_NAME; this proves a rule keyed on the title sees what that read
    // produced. Revert Task 1 and this reddens while the WM_NAME case above
    // stays green -- which is the whole argument for why the title read had to
    // change before the criterion was worth having.
    WmFixture fx(rulesFixture(
        "rule-match-title=Inbox\n"
        "rule-position=180,200\n"));

    auto conn = fx.openDisplay();
    REQUIRE(conn != nullptr);
    Display* d = conn.get();
    const Window root = DefaultRootWindow(d);

    Window win = createClient(d, 400, 400, 300, 220, "mail", "Mail");
    setEwmhTitle(d, win, "Inbox - Mail");   // and deliberately no WM_NAME
    REQUIRE(mapAndAwait(d, win));

    const Window frame = parentOf(d, win);
    REQUIRE(frame != root);
    REQUIRE(frame != None);

    Rect fr{};
    REQUIRE(serverRect(d, frame, fr));
    INFO("wm stderr:\n" << fx.wmStderr());
    INFO("frame rect " << describe(fr));
    REQUIRE(fr.x == 180);
    REQUIRE(fr.y == 200);
}

TEST_CASE("A title rule and a class rule fold later-wins over one window", "[wm_rules]")
{
    // Two rules, both matching, the second overriding ONE action and leaving the
    // other alone. A first-match implementation gives the broad rule's position;
    // a union implementation loses the broad rule's size.
    WmFixture fx(rulesFixture(
        "rule-match-class=Mail\n"
        "rule-position=100,100\n"
        "rule-size=420x320\n"
        "rule-match-title=Inbox\n"
        "rule-position=260,240\n"));

    auto conn = fx.openDisplay();
    REQUIRE(conn != nullptr);
    Display* d = conn.get();

    Window win = createClient(d, 500, 500, 300, 220, "mail", "Mail");
    setLegacyTitle(d, win, "Inbox - Mail");
    REQUIRE(mapAndAwait(d, win));

    const Window frame = parentOf(d, win);
    REQUIRE(frame != None);

    Rect fr{};
    REQUIRE(serverRect(d, frame, fr));
    Rect cr{};
    REQUIRE(serverRect(d, win, cr));

    INFO("wm stderr:\n" << fx.wmStderr());
    INFO("frame " << describe(fr) << ", client " << describe(cr));

    // The title rule is later, so ITS position wins.
    REQUIRE(fr.x == 260);
    REQUIRE(fr.y == 240);
    // ...and the class rule's size survives, because the title rule never
    // mentioned size. This is the assertion a union or a last-rule-wins-entirely
    // implementation fails.
    REQUIRE(cr.w == 420);
    REQUIRE(cr.h == 320);
}

TEST_CASE("A title changed after mapping does not re-apply the rule", "[wm_rules]")
{
    // D-8.5-03, asserted rather than assumed. Rules fold ONCE, at manage time.
    // Re-folding on a title change would make a window jump every time a
    // document is saved under a new name or a browser tab is switched, which is
    // worse than the gap it closes.
    WmFixture fx(rulesFixture(
        "rule-match-title=Inbox\n"
        "rule-position=300,280\n"));

    auto conn = fx.openDisplay();
    REQUIRE(conn != nullptr);
    Display* d = conn.get();

    // Mapped with a title that does NOT match, so the rule does not fire.
    //
    // The two titles differ sharply in LENGTH as well as in content, and that is
    // not decoration: the vacuity guard below measures the tab, the tab tracks
    // the title's length, and two titles of the same length would leave the
    // guard unable to tell a processed retitle from a dropped one. The first
    // draft of this case used "Drafts - Mail" and "Inbox - Mail" -- thirteen
    // characters each -- and MEASURED: deleting the retitle step entirely left
    // the case green.
    //
    // TALL, and that is also load-bearing. fixTabHeight() bounds the tab by the
    // window it decorates and then shortens the label to fit, so on a 220 px
    // window a six-character title and a forty-four-character one produce 86 px
    // and 113 px -- a real difference, but a thin margin to hang a guard on.
    // MEASURED at 460 px: 86 -> 325.
    Window win = createClient(d, 420, 40, 300, 460, "mail", "Mail");
    setLegacyTitle(d, win, "Drafts");
    REQUIRE(mapAndAwait(d, win));

    const Window frame = parentOf(d, win);
    REQUIRE(frame != None);

    Rect before{};
    REQUIRE(serverRect(d, frame, before));
    const int tabBefore = tabLengthOf(d, frame, win);
    INFO("before retitle: " << describe(before) << ", tab " << tabBefore << " px");
    REQUIRE(before.x != 300);
    REQUIRE(tabBefore > 0);

    // Now retitle it to something the rule DOES match, and much longer.
    setLegacyTitle(d, win, "Inbox - Mail With A Deliberately Longer Title");
    settleWm(d);

    // VACUITY GUARD. Without this the geometry assertion below holds for the
    // wrong reason: it would be asserting that nothing happened because nothing
    // was sent. The tab's length tracks the title (deferred item 11), so a WM
    // that processed the change has a measurably longer tab.
    //
    // Read the WM's own rendering rather than the property just written --
    // reading back our own XStoreName would prove only that the SERVER stored
    // it, which was never in doubt.
    const int tabAfter = tabLengthOf(d, frame, win);
    INFO("tab length " << tabBefore << " -> " << tabAfter);
    REQUIRE(tabAfter > tabBefore * 3 / 2);

    Rect after{};
    REQUIRE(serverRect(d, frame, after));
    INFO("wm stderr:\n" << fx.wmStderr());
    INFO("after retitle: " << describe(after) << " (rule says 300,280)");

    // THE ASSERTION: the window did not move.
    REQUIRE(after.x == before.x);
    REQUIRE(after.y == before.y);
}

TEST_CASE("With no rules configured a titled window is placed where it asked", "[wm_rules]")
{
    // The control for this group. Proves the title rules above CHANGE behaviour
    // rather than merely coexisting with it.
    WmFixture fx(rulesFixture(""));

    auto conn = fx.openDisplay();
    REQUIRE(conn != nullptr);
    Display* d = conn.get();

    Window win = createClient(d, 260, 240, 300, 220, "mail", "Mail");
    setLegacyTitle(d, win, "Inbox - Mail");
    REQUIRE(mapAndAwait(d, win));

    const Window frame = parentOf(d, win);
    REQUIRE(frame != None);

    Rect fr{};
    REQUIRE(serverRect(d, frame, fr));
    INFO("wm stderr:\n" << fx.wmStderr());
    INFO("frame rect " << describe(fr));
    REQUIRE(fr.x == 260);
    REQUIRE(fr.y == 240);
}
