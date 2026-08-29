// Config-to-runtime, terminating-error-path and stress tests against the REAL
// compiled wm2-born-again binary (plan 08-13, TEST-05).
//
// This file closes the last three "Missing Automated Coverage To Add" entries in
// COMPILED_CODE_BEHAVIOR_CHECKLIST.md:
//
//   item 5   config settings reaching observable runtime behaviour -> [wm_config_runtime]
//   item 6   the four terminating X11 error paths                  -> [wm_errors]
//   item 13  100-window create/map/unmap/destroy churn under ASan   -> [wm_stress]
//
// plus a [wm_harness] case for the fixture's own stale-report cleanup
// (deferred item 14), which is test infrastructure rather than WM behaviour and
// is tagged separately for that reason.
//
// Tags are registered as ctest LABELS via ADD_TAGS_AS_LABELS (D-33), so each
// group is selectable with `ctest -L '^wm_config_runtime$' --no-tests=error`.
//
// The rules inherited from 08-04 .. 08-12, none of them optional:
//
//   CONFIG IS OBSERVED THROUGH THE RUNNING BINARY, NEVER THROUGH THE PARSER.
//   That is the entire point of this group. tests/test_config.cpp already proves
//   the parser assigns the right member; a setting that parses and then reaches
//   nothing passes every one of those cases. That is exactly the defect plan
//   08-07 found in the focus booleans, which had been dead config since the
//   Config struct was written.
//
//   OUTCOMES ARE OBSERVED THROUGH THE SERVER. Frame geometry through
//   XGetGeometry + XTranslateCoordinates; colours through a server-side
//   XGetImage over the root window; spawned programs through the class hint of
//   the window that appears; the delete path through the WM_DELETE_WINDOW
//   message the client actually receives.
//
//   NON-EVENTS ARE PROVEN BY SETTLING FIRST (deferred item 9). The WM does not
//   flush its X output until its event loop wakes, so a single read after a
//   single nudge reliably observes the PREVIOUS state and a negative assertion
//   passes for the wrong reason. Every "this did NOT happen" read here runs
//   settleWm() first -- fifteen SPACED nudges. The spacing is as load-bearing as
//   the count; a tight loop of the same length still read stale values.
//
//   NO sleep()-BASED SYNCHRONISATION. Every wait is a deadline-bounded poll
//   whose exit condition is a real observation. The one deliberate wall-clock
//   interval in this file is the long press in the destroy-window-delay case,
//   where the elapsed time IS the thing under test; it is spelled as a deadline
//   loop (holdFor) so it still never blocks the driver's connection.
//
//   THE WM SURVIVES X PROTOCOL ERRORS. WindowManager::errorHandler() logs and
//   returns 0, so no assertion about windows, properties or geometry can notice
//   one. 08-11 added the stderr assertion below and found a RenderBadPicture on
//   every managed window close that 243 tests had walked past.
//
// Deferred item 7 is accounted for rather than worked around: the WM adopts its
// own menu, submenu and EWMH check windows as clients, so _NET_CLIENT_LIST
// always carries entries this file did not create. List assertions here filter
// to the windows the case owns.
//
// The programs this file launches are chosen from what scripts/preflight.sh
// verifies, never from the shipped default command -- the checklist warns
// explicitly that the shipped default may not be installed on a target host.

#include <catch2/catch_test_macros.hpp>

#include "support/WmFixture.h"
#include "support/XTestDriver.h"

#include "x11wrap.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
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

// The shipped default frame thickness (include/Config.h). Named rather than
// spelled 7 at each use so the expectation reads as "the default" where that is
// what is meant.
constexpr int kDefaultFrameThickness = 7;

// A guaranteed-present X client. scripts/preflight.sh fails the whole suite if
// neither this nor its alternate is installed, so depending on it here cannot
// produce a mysterious failure -- the preflight fixture names the cause first.
const char* const kProbeProgram = "xclock";
const char* const kProbeClass   = "XClock";

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

// A deliberate wall-clock interval, used only where the elapsed time is itself
// the thing under test (the long press against destroy-window-delay). Spelled
// as a deadline loop rather than a blocking call so it matches the rest of the
// file's waiting discipline and never blocks the input driver's connection.
void holdFor(int ms)
{
    const auto until = Clock::now() + std::chrono::milliseconds(ms);
    while (Clock::now() < until) pollSleep();
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
    bool operator!=(const Rect& o) const { return !(*this == o); }
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

// Parent-relative geometry, which is what identifies the frame's sub-windows:
// Border::configure() places the tab at (0,0) and the button at
// (TAB_TOP_HEIGHT + 2, TAB_TOP_HEIGHT + 2) = (4,4), square.
bool localRect(Display* d, Window w, Rect& out)
{
    Window rootRet = None;
    int x = 0, y = 0;
    unsigned int width = 0, height = 0, bw = 0, depth = 0;
    if (!XGetGeometry(d, w, &rootRet, &x, &y, &width, &height, &bw, &depth)) return false;
    out.x = x;
    out.y = y;
    out.w = static_cast<int>(width);
    out.h = static_cast<int>(height);
    return true;
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

bool isViewable(Display* d, Window w)
{
    XWindowAttributes attr;
    if (!XGetWindowAttributes(d, w, &attr)) return false;
    return attr.map_state == IsViewable;
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

    if (XGetWindowProperty(d, DefaultRootWindow(d), prop, 0, 1024, False, XA_WINDOW,
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

Window activeWindow(Display* d)
{
    static Atom atom = None;
    if (atom == None) atom = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
    Window w = None;
    if (!readWindowProp(d, DefaultRootWindow(d), atom, w)) return None;
    return w;
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

// ---------------------------------------------------------------------------
// Protocol errors (copied from tests/test_wm_lifecycle.cpp, plan 08-11)
// ---------------------------------------------------------------------------

// Every X protocol error the WM reports goes through
// WindowManager::errorHandler(), which prints one line shaped
// `wm2: <request> (0xID): <message>`. Nothing else the WM writes carries
// " (0x", so that substring is a precise detector.
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

// The subset of the above that is NOT a BadWindow (deferred item 13: destroying
// a client necessarily produces exactly one, and WmFixture destroys its own
// readiness probe during startup).
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

Window createClient(Display* d, int x, int y, int w, int h, const char* name = nullptr)
{
    Window root = DefaultRootWindow(d);
    Window win = XCreateSimpleWindow(d, root, x, y,
                                     static_cast<unsigned>(w), static_cast<unsigned>(h), 0,
                                     BlackPixel(d, DefaultScreen(d)),
                                     WhitePixel(d, DefaultScreen(d)));
    if (name) XStoreName(d, win, name);
    XSync(d, False);
    return win;
}

// Announce WM_DELETE_WINDOW support, so Client::kill() sends a message this
// connection receives instead of calling XKillClient -- which would tear down
// the TEST's own connection and take the assertions with it.
void announceDeleteProtocol(Display* d, Window win)
{
    Atom del = XInternAtom(d, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(d, win, &del, 1);
    XSync(d, False);
}

// Wait until the WM has both reparented `win` into a frame AND published it in
// _NET_CLIENT_LIST.
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

Window mapClientAndAwaitFrame(Display* d, int x, int y, int w, int h, Window& clientOut,
                              const char* name = nullptr)
{
    Window win = createClient(d, x, y, w, h, name);
    clientOut = win;
    XMapWindow(d, win);
    XSync(d, False);
    return awaitFrameFor(d, win);
}

void parkPointer(Display* d)
{
    XWarpPointer(d, None, DefaultRootWindow(d), 0, 0, 0, 0, kParkX, kParkY);
    XSync(d, False);
}

// The frame's tab and button, identified by the parent-relative geometry
// Border::configure() gives them: the tab is the child at (0,0), the button is
// the square child at (4,4). Identified by GEOMETRY rather than by stacking
// order, because XQueryTree's order is a stacking fact and the WM raises and
// lowers these windows as it decorates.
Window findFrameChild(Display* d, Window frame, Window client, bool wantButton)
{
    for (Window child : childrenOf(d, frame)) {
        if (child == client) continue;
        Rect r;
        if (!localRect(d, child, r)) continue;
        if (wantButton) {
            if (r.x == 4 && r.y == 4 && r.w == r.h && r.w > 0) return child;
        } else {
            if (r.x == 0 && r.y == 0) return child;
        }
    }
    return None;
}

// ---------------------------------------------------------------------------
// Config plumbing
// ---------------------------------------------------------------------------

// Write an isolated XDG config tree and return the directory to hand the child
// as XDG_CONFIG_HOME. Lives under the CMake binary directory rather than a bare
// /tmp name (threat T-8-TMP), and is unique per process and per case.
std::string makeConfigTree(const std::string& contents)
{
    static int counter = 0;
    const std::string base = std::string(WM2_TEST_WORKDIR) + "/runtime-cfg-" +
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

// A fixture whose WM reads NO config file at all -- both XDG variables point at
// directories that do not exist, which applyFile() skips silently. Without the
// XDG_CONFIG_DIRS override, xdgConfigDirs() falls back to /etc/xdg, and a host
// that happens to carry /etc/xdg/wm2-born-again/config would layer its settings
// under every case in this file and quietly change what they prove.
WmFixtureOptions cleanFixture(std::vector<std::string> wmArgs = {})
{
    WmFixtureOptions o;
    const std::string home = makeConfigTree("");
    o.childEnv["XDG_CONFIG_HOME"] = home + "/no-user-config";
    o.childEnv["XDG_CONFIG_DIRS"] = home + "/no-system-config";
    o.wmArgs = std::move(wmArgs);
    return o;
}

// As above, but with a real user config file carrying `configContents`.
WmFixtureOptions configuredFixture(const std::string& configContents,
                                   std::vector<std::string> wmArgs = {})
{
    WmFixtureOptions o;
    const std::string home = makeConfigTree(configContents);
    o.childEnv["XDG_CONFIG_HOME"] = home;
    o.childEnv["XDG_CONFIG_DIRS"] = home + "/no-system-config";
    o.wmArgs = std::move(wmArgs);
    return o;
}

// ---------------------------------------------------------------------------
// Colour observation
//
// Colours are read back from the SERVER with XGetImage over the ROOT window,
// covering the rectangle the window of interest occupies. Reading root rather
// than the window itself is deliberate: the frame and the tab are SHAPED, and
// XGetImage's result outside a window's bounding shape is undefined. Root is
// never shaped, so what comes back is exactly what is on the screen.
// ---------------------------------------------------------------------------

using Histogram = std::map<unsigned long, long>;

Histogram captureRoot(Display* d, const Rect& r)
{
    Histogram h;
    if (r.w <= 0 || r.h <= 0) return h;

    const int x = std::max(0, r.x);
    const int y = std::max(0, r.y);
    const int w = std::min(r.w, kScreenW - x);
    const int hh = std::min(r.h, kScreenH - y);
    if (w <= 0 || hh <= 0) return h;

    XImage* img = XGetImage(d, DefaultRootWindow(d), x, y,
                            static_cast<unsigned>(w), static_cast<unsigned>(hh),
                            AllPlanes, ZPixmap);
    if (!img) return h;

    for (int iy = 0; iy < hh; ++iy) {
        for (int ix = 0; ix < w; ++ix) {
            ++h[XGetPixel(img, ix, iy)];
        }
    }
    XDestroyImage(img);
    return h;
}

unsigned long dominantPixel(const Histogram& h)
{
    unsigned long best = 0;
    long bestCount = -1;
    for (const auto& kv : h) {
        if (kv.second > bestCount) { bestCount = kv.second; best = kv.first; }
    }
    return best;
}

long countOf(const Histogram& h, unsigned long pixel)
{
    auto it = h.find(pixel);
    return it == h.end() ? 0 : it->second;
}

// The pixel value the server resolves a colour NAME to on this display. Used to
// turn "the tab is red" into an absolute assertion rather than a comparison
// against another run -- a run comparison alone cannot distinguish "the setting
// changed the colour" from "the setting changed something else about the frame".
unsigned long namedPixel(Display* d, const char* name)
{
    XColor screenColour, exact;
    if (!XAllocNamedColor(d, DefaultColormap(d, DefaultScreen(d)), name,
                          &screenColour, &exact)) {
        return ~0UL;
    }
    return screenColour.pixel;
}

std::string hex(unsigned long v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%06lx", v & 0xffffffUL);
    return buf;
}

std::string describeTop(Display* d, const Histogram& h, size_t n = 4)
{
    std::vector<std::pair<long, unsigned long>> byCount;
    for (const auto& kv : h) byCount.push_back({kv.second, kv.first});
    std::sort(byCount.rbegin(), byCount.rend());
    std::string out = "{";
    for (size_t i = 0; i < byCount.size() && i < n; ++i) {
        out += hex(byCount[i].second) + "x" + std::to_string(byCount[i].first);
        if (i + 1 < byCount.size() && i + 1 < n) out += ", ";
    }
    out += "}";
    (void)d;
    return out;
}

// ---------------------------------------------------------------------------
// The root menu, opened with a real button press
// ---------------------------------------------------------------------------

// WindowManager::menu() runs a NESTED event loop with its own pointer grab, so
// while the menu is up the WM is not in loop() and pumpWm() cannot wake it.
// Everything the menu cases observe is therefore read directly from the server
// while the press is still held.
//
// The menu window is created by initialiseScreen() as a 1x1 unmapped child of
// root and only moved/resized when the menu opens, so with no client mapped it
// is the ONLY viewable child of root larger than 1x1. That is why the menu cases
// deliberately map no client of their own.
Window findOpenMenu(Display* d)
{
    for (Window child : childrenOf(d, DefaultRootWindow(d))) {
        Rect r;
        if (!localRect(d, child, r)) continue;
        if (r.w <= 1 || r.h <= 1) continue;
        if (!isViewable(d, child)) continue;
        return child;
    }
    return None;
}

Window openRootMenu(Display* d, XTestDriver& driver, int x, int y)
{
    driver.moveTo(x, y);
    driver.press(Button1);

    Window menu = None;
    WmFixture::pollUntil([&] {
        menu = findOpenMenu(d);
        return menu != None;
    }, 8000);
    return menu;
}

// Dismiss the menu WITHOUT selecting anything: the grab uses owner_events=False,
// so the release coordinates are menu-relative and a release well outside the
// menu's width sets sel = -1.
void dismissMenu(Display* d, XTestDriver& driver)
{
    driver.moveTo(kScreenW - 5, kScreenH - 5);
    driver.release(Button1);
    XSync(d, False);
    settleWm(d);
}

// ---------------------------------------------------------------------------
// Spawned programs
// ---------------------------------------------------------------------------

// The res_class of a managed window, read from WM_CLASS. This is how a spawned
// program is identified: the WM tells nobody what it launched, but the program
// itself announces its class on the window it maps.
std::string classOf(Display* d, Window w)
{
    XClassHint hint;
    hint.res_name = nullptr;
    hint.res_class = nullptr;
    if (!XGetClassHint(d, w, &hint)) return "";
    std::string out = hint.res_class ? hint.res_class : "";
    if (hint.res_name) XFree(hint.res_name);
    if (hint.res_class) XFree(hint.res_class);
    return out;
}

std::string instanceOf(Display* d, Window w)
{
    XClassHint hint;
    hint.res_name = nullptr;
    hint.res_class = nullptr;
    if (!XGetClassHint(d, w, &hint)) return "";
    std::string out = hint.res_name ? hint.res_name : "";
    if (hint.res_name) XFree(hint.res_name);
    if (hint.res_class) XFree(hint.res_class);
    return out;
}

// Wait for a managed client whose WM_CLASS class matches `cls`, ignoring
// `known`. Returns None on timeout, which is what the negative cases assert.
Window awaitClientWithClass(Display* d, const char* cls,
                            const std::vector<Window>& known, int timeoutMs)
{
    Window found = None;
    WmFixture::pollUntil([&] {
        pumpWm(d);
        for (Window w : clientList(d)) {
            if (std::find(known.begin(), known.end(), w) != known.end()) continue;
            if (classOf(d, w) == cls) { found = w; return true; }
        }
        return false;
    }, timeoutMs);
    return found;
}

// A unique path under the CMake binary directory (threat T-8-TMP, never a bare
// /tmp name) used as the "a shell ran this" witness in the exec-using-shell
// cases.
std::string sentinelPath(const char* tag)
{
    static int counter = 0;
    return std::string(WM2_TEST_WORKDIR) + "/shell-sentinel-" + tag + "-" +
           std::to_string(::getpid()) + "-" + std::to_string(++counter);
}

// ---------------------------------------------------------------------------
// Running the binary as a subprocess that is EXPECTED TO DIE
//
// WindowManager::fatal() prints and calls std::exit (src/Manager.cpp:373), and
// the initialisation-time arm of errorHandler() does the same. An in-process
// test cannot observe either: there is nothing left to assert on. So every
// [wm_errors] case forks, execs, and asserts on the captured exit status and
// stderr -- which is also the only way to distinguish "exited with the wrong
// code" from "did not exit at all", the failure a deadline exists to catch.
// ---------------------------------------------------------------------------

struct RunResult {
    bool timedOut = false;
    bool exitedNormally = false;
    int exitCode = -1;
    int signal = 0;
    long elapsedMs = 0;
    std::string output;
};

std::string readWholeFile(const std::string& path)
{
    std::string out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

RunResult runBinary(const std::vector<std::string>& args,
                    const std::map<std::string, std::string>& env,
                    bool unsetDisplay, int timeoutMs)
{
    static int counter = 0;
    const std::string outPath = std::string(WM2_TEST_WORKDIR) + "/errpath-" +
                                std::to_string(::getpid()) + "-" +
                                std::to_string(++counter) + ".out";

    std::vector<std::string> argv{WM2_BINARY_PATH};
    for (const auto& a : args) argv.push_back(a);

    RunResult result;
    const auto started = Clock::now();

    pid_t pid = ::fork();
    if (pid < 0) return result;
    if (pid == 0) {
        int fd = ::open(outPath.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
        if (fd >= 0) {
            ::dup2(fd, STDOUT_FILENO);
            ::dup2(fd, STDERR_FILENO);
            if (fd > STDERR_FILENO) ::close(fd);
        }
        ::setsid();

        // Sanitizer settings for a process that is SUPPOSED to exit early.
        // detect_leaks is off on purpose: a fatal path exits without unwinding,
        // so every live allocation is reported as a leak and the exit code
        // becomes the leak sentinel instead of the code under test. Turning it
        // off here removes noise from a path whose memory behaviour is not what
        // these cases are about -- the [wm_stress] group below is where leaks
        // are the assertion.
        ::setenv("ASAN_OPTIONS", "detect_leaks=0:abort_on_error=0:handle_segv=1", 1);
        ::setenv("UBSAN_OPTIONS", "print_stacktrace=1:halt_on_error=0", 1);

        for (const auto& kv : env) ::setenv(kv.first.c_str(), kv.second.c_str(), 1);
        if (unsetDisplay) ::unsetenv("DISPLAY");

        std::vector<char*> cargv;
        cargv.reserve(argv.size() + 1);
        for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
        cargv.push_back(nullptr);
        ::execv(cargv[0], cargv.data());
        _exit(127);
    }

    ChildProcess child(pid);
    const bool exited = child.waitForExit(timeoutMs);
    result.elapsedMs = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count());

    if (!exited) {
        result.timedOut = true;
        child.shutdown();          // never leave a hung WM behind
    } else {
        result.exitedNormally = child.exitedNormally();
        result.exitCode = child.exitCode();
        result.signal = child.termSignal();
    }

    result.output = readWholeFile(outPath);
    ::unlink(outPath.c_str());
    return result;
}

bool contains(const std::string& haystack, const char* needle)
{
    return haystack.find(needle) != std::string::npos;
}

// No sanitizer finding hid inside a deliberately-fatal run. Not the point of
// these cases, but a free assertion: the terminating paths run allocation and
// X-connection code like any other, and a report there would otherwise be
// swallowed by the very exit the case is asserting on.
bool sanitizerClean(const std::string& output)
{
    return !contains(output, "AddressSanitizer") &&
           !contains(output, "LeakSanitizer") &&
           !contains(output, "runtime error:");
}

// ---------------------------------------------------------------------------
// A display with NO window manager on it
//
// WmFixture always starts one, and it must: everything else in this file
// asserts against a running WM. But the invalid-colour and unloadable-font
// paths both terminate INSIDE initialiseScreen(), and the root-redirect claim
// happens earlier in that same function -- so on a display that already has a
// WM the second process dies of the redirect conflict first and never reaches
// the path under test.
//
// Built from the same DisplayReservation and ChildProcess the fixture uses (they
// are separate classes for exactly this kind of reuse) rather than by adding a
// "no WM please" mode to shared infrastructure six other suites depend on.
// ---------------------------------------------------------------------------

class BareDisplay {
public:
    BareDisplay()
    {
        constexpr int kBase = 200;
        constexpr int kMaxCandidates = 60;

        for (int n = kBase; n < kBase + kMaxCandidates; ++n) {
            if (!m_reservation.tryReserve(n)) continue;
            m_display = m_reservation.displayString();
            if (spawnXvfb() && waitForServer()) return;
            m_xvfb.shutdown();
            m_reservation.release();
            m_display.clear();
        }
        throw std::runtime_error("BareDisplay: could not reserve a free X display");
    }

    BareDisplay(const BareDisplay&) = delete;
    BareDisplay& operator=(const BareDisplay&) = delete;

    ~BareDisplay()
    {
        m_keepAlive.reset();
        m_xvfb.shutdown();
        m_reservation.release();
    }

    const std::string& display() const { return m_display; }

private:
    bool spawnXvfb()
    {
        const std::string logPath = std::string(WM2_TEST_WORKDIR) + "/bare-xvfb" +
                                    std::to_string(m_reservation.number()) + ".log";
        pid_t pid = ::fork();
        if (pid < 0) return false;
        if (pid == 0) {
            int fd = ::open(logPath.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
            if (fd >= 0) {
                ::dup2(fd, STDOUT_FILENO);
                ::dup2(fd, STDERR_FILENO);
                if (fd > STDERR_FILENO) ::close(fd);
            }
            ::setsid();
            ::execlp("Xvfb", "Xvfb", m_display.c_str(), "-screen", "0", "1024x768x24",
                     "-ac", "+render", "-noreset", "-nolisten", "tcp",
                     static_cast<char*>(nullptr));
            _exit(127);
        }
        m_xvfb = ChildProcess(pid);
        return true;
    }

    bool waitForServer()
    {
        const bool up = WmFixture::pollUntil([this] {
            if (m_xvfb.tryReap()) return false;
            m_keepAlive = x11::DisplayPtr(XOpenDisplay(m_display.c_str()));
            return m_keepAlive != nullptr;
        }, 10000);
        return up && !m_xvfb.reaped();
    }

    DisplayReservation m_reservation;
    std::string m_display;
    ChildProcess m_xvfb;
    x11::DisplayPtr m_keepAlive;
};

// A fontconfig configuration whose ONLY font directory is empty, so no font
// pattern can resolve. Returns the path to hand the child as FONTCONFIG_FILE.
//
// Deliberately NOT described as a config setting: the binary has no
// font-pattern setting of any kind, which is precisely why this path has to be
// provoked through the environment.
std::string makeEmptyFontConfig()
{
    static int counter = 0;
    const std::string base = std::string(WM2_TEST_WORKDIR) + "/nofonts-" +
                             std::to_string(::getpid()) + "-" +
                             std::to_string(++counter);
    ::mkdir(base.c_str(), 0700);
    ::mkdir((base + "/fonts").c_str(), 0700);
    ::mkdir((base + "/cache").c_str(), 0700);

    const std::string path = base + "/fonts.conf";
    std::ofstream out(path);
    out << "<?xml version=\"1.0\"?>\n"
        << "<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n"
        << "<fontconfig>\n"
        << "  <dir>" << base << "/fonts</dir>\n"
        << "  <cachedir>" << base << "/cache</cachedir>\n"
        << "</fontconfig>\n";
    out.close();
    return path;
}

// The four terminating conditions, named once so the deadline case below runs
// exactly the same launches the individual cases do rather than an approximation
// of them.
constexpr int kFatalTimeoutMs = 20000;   // generous: a slow or loaded host
constexpr int kFatalDeadlineMs = 6000;   // tight: what a healthy host really takes

}  // namespace


// ===========================================================================
// [wm_harness] -- the fixture's own stale-report cleanup (deferred item 14)
// ===========================================================================

TEST_CASE("A stale sanitizer report on a reused display prefix is cleared, and "
          "only that prefix is cleared", "[wm_harness]")
{
    // The hazard, stated as a test rather than as a comment: the fixture
    // attributes ANY file matching its display prefix to its own child. The
    // display pool is small and reused, so a report written by an earlier run
    // on the same number reads as a fresh sanitizer finding in an innocent
    // case -- which cost 08-12 roughly twenty minutes chasing a phantom
    // regression in updateWorkarea().
    WmFixture fixture;
    REQUIRE_FALSE(fixture.asanLogPrefix().empty());

    const std::string ours    = fixture.asanLogPrefix() + ".stale-08-13";
    const std::string foreign = std::string(WM2_TEST_WORKDIR) +
                                "/asan-not-this-display." + std::to_string(::getpid());

    { std::ofstream f(ours);    f << "stale\n"; }
    { std::ofstream f(foreign); f << "stale\n"; }

    // Half one: the attribution really is prefix-based, so a stale file really
    // does become someone else's failure. Without this the cleanup below would
    // be a fix for a problem the test never demonstrated.
    INFO("prefix: " << fixture.asanLogPrefix());
    CHECK_FALSE(fixture.asanReports().empty());

    // Half two: the cleanup removes exactly the fixture's own prefix.
    WmFixture::removeReportsWithPrefix(fixture.asanLogPrefix());

    CHECK(fixture.asanReports().empty());
    CHECK_FALSE(pathExists(ours));

    // A neighbouring fixture's reports are NOT collateral. A cleanup that
    // cleared the whole directory would pass every assertion above and would
    // silently delete a concurrent ctest worker's genuine finding.
    CHECK(pathExists(foreign));
    ::unlink(foreign.c_str());
}


// ===========================================================================
// [wm_config_runtime] -- config settings reaching runtime (coverage item 5)
// ===========================================================================

TEST_CASE("Frame thickness changes the measured frame geometry in both directions",
          "[wm_config_runtime]")
{
    // Border::reparent() places the client at (xIndent(), yIndent()) inside the
    // frame, and yIndent() is FRAME_WIDTH + 1 for a non-transient window. So the
    // vertical inset is an EXACT function of the setting and can be asserted
    // absolutely -- no run comparison needed and no dependence on the font.
    //
    // The horizontal inset is xIndent() = m_tabWidth + FRAME_WIDTH + 1, and
    // m_tabWidth depends on whatever font fontconfig resolves on the host. It is
    // therefore asserted as a DIFFERENCE between runs, where the font term
    // cancels exactly.
    struct Measured { int vertical = 0; int horizontal = 0; };

    auto measure = [](int thickness, bool useDefault) {
        std::vector<std::string> args;
        if (!useDefault) args.push_back("--frame-thickness=" + std::to_string(thickness));

        WmFixture fixture(cleanFixture(args));
        x11::DisplayPtr dp = fixture.openDisplay();
        REQUIRE(dp != nullptr);
        Display* d = dp.get();
        parkPointer(d);

        Window win = None;
        Window frame = mapClientAndAwaitFrame(d, 200, 150, 300, 220, win, "thickness");
        REQUIRE(frame != None);
        settleWm(d);

        const Rect frameRect  = rectOf(d, frame);
        const Rect clientRect = rectOf(d, win);

        INFO("frame " << describe(frameRect) << " client " << describe(clientRect));
        INFO("wm stderr:\n" << fixture.wmStderr());
        CHECK(joined(xProtocolErrorsExceptBadWindow(fixture.wmStderr())).empty());

        Measured m;
        m.vertical   = clientRect.y - frameRect.y;
        m.horizontal = clientRect.x - frameRect.x;
        return m;
    };

    const Measured thin    = measure(3, false);
    const Measured shipped = measure(kDefaultFrameThickness, true);
    const Measured thick   = measure(20, false);

    INFO("thin=" << thin.vertical << " shipped=" << shipped.vertical
         << " thick=" << thick.vertical);

    // Absolute: yIndent() == FRAME_WIDTH + 1.
    CHECK(thin.vertical    == 3 + 1);
    CHECK(shipped.vertical == kDefaultFrameThickness + 1);
    CHECK(thick.vertical   == 20 + 1);

    // And both directions from the shipped default really are different, which
    // is the claim "the setting reaches the runtime" actually makes.
    CHECK(thin.vertical  < shipped.vertical);
    CHECK(thick.vertical > shipped.vertical);

    // Relative, with the font-dependent tab width cancelling out.
    CHECK(thin.horizontal  - shipped.horizontal == 3  - kDefaultFrameThickness);
    CHECK(thick.horizontal - shipped.horizontal == 20 - kDefaultFrameThickness);
}

TEST_CASE("Tab foreground and background colours reach the rendered tab",
          "[wm_config_runtime]")
{
    // Captured over the frame's rectangle on ROOT. The tab is shaped, so
    // XGetImage on the tab window itself would return undefined content outside
    // its bounding region; root is never shaped.
    auto capture = [](const std::vector<std::string>& args, Histogram& out,
                      unsigned long& fgWanted, unsigned long& bgWanted,
                      const char* fgName, const char* bgName) {
        WmFixture fixture(cleanFixture(args));
        x11::DisplayPtr dp = fixture.openDisplay();
        REQUIRE(dp != nullptr);
        Display* d = dp.get();
        parkPointer(d);

        fgWanted = namedPixel(d, fgName);
        bgWanted = namedPixel(d, bgName);

        Window win = None;
        // A label is required or Border::drawLabel() returns before it draws
        // anything, and the foreground colour would never reach a pixel.
        Window frame = mapClientAndAwaitFrame(d, 300, 200, 260, 200, win,
                                              "IIIIIIIIIIIIIIII");
        REQUIRE(frame != None);
        settleWm(d);

        out = captureRoot(d, rectOf(d, frame));

        INFO("wm stderr:\n" << fixture.wmStderr());
        CHECK(joined(xProtocolErrorsExceptBadWindow(fixture.wmStderr())).empty());
    };

    Histogram shipped, configured;
    unsigned long shippedFg = 0, shippedBg = 0, configuredFg = 0, configuredBg = 0;

    capture({}, shipped, shippedFg, shippedBg, "black", "gray80");
    capture({"--tab-background=#ff0000", "--tab-foreground=#0000ff"},
            configured, configuredFg, configuredBg, "#0000ff", "#ff0000");

    REQUIRE(configuredFg != ~0UL);
    REQUIRE(configuredBg != ~0UL);

    INFO("shipped tab pixels:    " << describeTop(nullptr, shipped));
    INFO("configured tab pixels: " << describeTop(nullptr, configured));
    INFO("wanted bg " << hex(configuredBg) << " fg " << hex(configuredFg));

    // The configured BACKGROUND is present, in quantity, and was not there
    // before. "In quantity" is what separates a painted tab from a stray pixel.
    CHECK(countOf(configured, configuredBg) > 200);
    CHECK(countOf(shipped, configuredBg) == 0);

    // The configured FOREGROUND reached the label. Xft antialiases, so the count
    // is small -- but the glyph cores are the exact colour, and the shipped run
    // (black on gray80) contains none of it at all.
    CHECK(countOf(configured, configuredFg) > 0);
    CHECK(countOf(shipped, configuredFg) == 0);

    // The shipped defaults are still what the shipped run renders, so this case
    // fails if a future change swaps the defaults out from under it rather than
    // only if the flags stop working.
    CHECK(countOf(shipped, shippedBg) > 200);
    CHECK(shippedFg != configuredFg);
}

TEST_CASE("Menu background colour reaches the menu opened by a real root click",
          "[wm_config_runtime]")
{
    // No client is mapped in this case on purpose: it makes the menu window the
    // only viewable child of root larger than 1x1, which is what findOpenMenu()
    // relies on.
    auto capture = [](const std::vector<std::string>& args, Histogram& out,
                      unsigned long& wanted, const char* colourName) {
        WmFixture fixture(cleanFixture(args));
        x11::DisplayPtr dp = fixture.openDisplay();
        REQUIRE(dp != nullptr);
        Display* d = dp.get();
        parkPointer(d);

        wanted = namedPixel(d, colourName);

        XTestDriver driver(fixture.display());
        driver.moveTo(kParkX, kParkY);

        Window menu = openRootMenu(d, driver, 400, 300);
        REQUIRE(menu != None);

        // The menu is painted by the server from its background pixel on map,
        // then the WM draws entries over it on the Expose it selects for. Poll
        // for a stable capture rather than reading once: the map and the draw
        // are two separate server round trips.
        Rect menuRect{};
        REQUIRE(WmFixture::pollUntil([&] {
            return serverRect(d, menu, menuRect) && menuRect.w > 1 && menuRect.h > 1;
        }, 4000));

        out = captureRoot(d, menuRect);
        dismissMenu(d, driver);

        INFO("menu rect " << describe(menuRect));
        INFO("wm stderr:\n" << fixture.wmStderr());
        CHECK(joined(xProtocolErrorsExceptBadWindow(fixture.wmStderr())).empty());
    };

    Histogram shipped, configured;
    unsigned long shippedWanted = 0, configuredWanted = 0;

    capture({}, shipped, shippedWanted, "gray80");
    capture({"--menu-background=#00cc00"}, configured, configuredWanted, "#00cc00");

    REQUIRE(configuredWanted != ~0UL);

    INFO("shipped menu pixels:    " << describeTop(nullptr, shipped));
    INFO("configured menu pixels: " << describeTop(nullptr, configured));

    CHECK(dominantPixel(configured) == configuredWanted);
    CHECK(dominantPixel(shipped) == shippedWanted);
    CHECK(dominantPixel(shipped) != dominantPixel(configured));
    CHECK(countOf(shipped, configuredWanted) == 0);
}

TEST_CASE("destroy-window-delay decides whether a tab-button press hides or deletes",
          "[wm_config_runtime]")
{
    // Border::eventButton()'s tab-button loop starts with action == 1 (hide) and
    // switches to action == 2 (delete) once the accumulated press time passes
    // config().destroyWindowDelay. A short press must therefore hide and a long
    // press must delete -- and BOTH halves are needed: a build that always hid
    // passes a delete-only test's precondition, and a build that always deleted
    // passes a hide-only one.
    //
    // 250ms rather than the shipped 1500ms so the long press is quick, but still
    // genuinely crossing the configured threshold: the hold below is more than
    // twice it. The loop also abandons entirely past 5000ms, so the hold must
    // stay well under that too.
    constexpr int kDelayMs = 250;

    WmFixture fixture(cleanFixture({"--destroy-window-delay=" + std::to_string(kDelayMs)}));
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    // Two clients, so the two halves do not have to share one window's state.
    //
    // WM_PROTOCOLS is announced BEFORE the map, not after. Client::getProtocols()
    // runs inside Client::manage(), so a protocol announced afterwards is not
    // seen -- and Client::kill() then falls through to XKillClient, which closes
    // the connection of the window's owner. That owner is this test, so the case
    // died mid-run with "X connection broken" and took its own assertions with
    // it. Same ordering constraint as the class hint in tests/test_wm_rules.cpp.
    Window shortWin = createClient(d, 120, 90, 220, 160, "shortpress");
    announceDeleteProtocol(d, shortWin);
    XMapWindow(d, shortWin);
    XSync(d, False);
    Window shortFrame = awaitFrameFor(d, shortWin);
    REQUIRE(shortFrame != None);

    Window longWin = createClient(d, 520, 380, 220, 160, "longpress");
    announceDeleteProtocol(d, longWin);
    XMapWindow(d, longWin);
    XSync(d, False);
    Window longFrame = awaitFrameFor(d, longWin);
    REQUIRE(longFrame != None);
    settleWm(d);

    auto pressButtonOf = [&](Window frame, Window client, int holdMs) {
        // The tab button is only REACHABLE on an active client. Under the
        // shipped pointer-focus policy that means putting the pointer on the
        // window first: setFrameVisibility(false, ...) SUBTRACTS the button's
        // square from the frame's bounding shape for an inactive client, so a
        // press aimed at it would fall through to the root window and open the
        // menu instead. Nothing about that announces itself -- the button's own
        // geometry is unchanged and findFrameChild() still finds it.
        const Rect c = rectOf(d, client);
        driver.moveTo(c.x + c.w / 2, c.y + c.h / 2);
        XSync(d, False);
        REQUIRE(WmFixture::pollUntil([&] {
            pumpWm(d);
            return activeWindow(d) == client;
        }, 8000));

        Window button = findFrameChild(d, frame, client, true);
        REQUIRE(button != None);
        const Rect b = rectOf(d, button);
        REQUIRE(b.w > 0);
        driver.moveTo(b.x + b.w / 2, b.y + b.h / 2);
        XSync(d, False);
        driver.press(Button1);
        if (holdMs > 0) holdFor(holdMs);
        driver.release(Button1);
        XSync(d, False);
    };

    auto sawDelete = [&](Window target) {
        static Atom protocols = XInternAtom(d, "WM_PROTOCOLS", False);
        static Atom del = XInternAtom(d, "WM_DELETE_WINDOW", False);
        XEvent ev;
        while (XCheckTypedWindowEvent(d, target, ClientMessage, &ev)) {
            if (ev.xclient.message_type == protocols &&
                static_cast<Atom>(ev.xclient.data.l[0]) == del) {
                return true;
            }
        }
        return false;
    };

    // --- short press: hide, no delete -------------------------------------
    pressButtonOf(shortFrame, shortWin, 0);

    long state = -1;
    REQUIRE(WmFixture::pollUntil([&] {
        pumpWm(d);
        return icccmState(d, shortWin, state) && state == IconicState;
    }, 8000));

    settleWm(d);
    INFO("wm stderr:\n" << fixture.wmStderr());
    CHECK_FALSE(sawDelete(shortWin));

    // --- long press: delete, no hide --------------------------------------
    pressButtonOf(longFrame, longWin, kDelayMs * 3);

    REQUIRE(WmFixture::pollUntil([&] {
        pumpWm(d);
        return sawDelete(longWin);
    }, 8000));

    settleWm(d);
    long longState = -1;
    REQUIRE(icccmState(d, longWin, longState));
    INFO("long-press client WM_STATE: " << longState);
    CHECK(longState == NormalState);

    REQUIRE(fixture.wmAlive());
    INFO("wm stderr:\n" << fixture.wmStderr());
    CHECK(joined(xProtocolErrorsExceptBadWindow(fixture.wmStderr())).empty());
}

TEST_CASE("new-window-command decides which program the menu's New entry starts",
          "[wm_config_runtime]")
{
    // The identity of the launched program is read from the class hint of the
    // window that appears, because that is the only thing about a spawned
    // process observable from outside the WM.
    WmFixture fixture(cleanFixture({std::string("--new-window-command=") + kProbeProgram}));
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    const std::vector<Window> before = clientList(d);

    Window menu = openRootMenu(d, driver, 400, 300);
    REQUIRE(menu != None);

    Rect menuRect{};
    REQUIRE(WmFixture::pollUntil([&] {
        return serverRect(d, menu, menuRect) && menuRect.w > 1 && menuRect.h > 1;
    }, 4000));

    // Entry 0 ("New") occupies menu-relative y in [11, 11 + entryHeight). The
    // WM computes sel = (y - 11) / entryHeight, so a release a few pixels into
    // the first row selects it regardless of the font's exact entry height.
    driver.moveTo(menuRect.x + menuRect.w / 2, menuRect.y + 14);
    driver.release(Button1);
    XSync(d, False);

    const Window spawned = awaitClientWithClass(d, kProbeClass, before, 15000);
    INFO("wm stderr:\n" << fixture.wmStderr());
    INFO("client list size: " << clientList(d).size());
    REQUIRE(spawned != None);
    CHECK(listed(d, spawned));

    REQUIRE(fixture.wmAlive());
}

TEST_CASE("exec-using-shell gates whether a command with arguments and "
          "metacharacters is evaluated by a shell", "[wm_config_runtime]")
{
    // Two halves that share one command string. The positive half proves the
    // witness mechanism works at all; without it the negative half could pass
    // because the sentinel path was wrong, the menu never opened, or the WM
    // never spawned anything -- none of which is the claim being made.
    //
    // The command carries BOTH an argument and a shell metacharacter, so the two
    // halves separate "arguments require shell mode" (functional) from "with
    // shell mode off nothing is evaluated by a shell" (threat T-8-SHELL).
    auto run = [](bool shellEnabled, const char* tag, bool& sentinelSeen,
                  Window& spawnedOut) {
        const std::string sentinel = sentinelPath(tag);
        ::unlink(sentinel.c_str());

        // The witness comes FIRST. A shell runs the program in the foreground,
        // so a witness placed after it would not be written until the program
        // exits -- and the program is expected to still be running when the
        // case reads for it. Measured: with the two the other way round the
        // positive control failed against a shell that had done exactly what it
        // was asked.
        const std::string command = "touch " + sentinel + "; " +
                                    std::string(kProbeProgram) + " -name shellmarker";

        std::vector<std::string> args{"--new-window-command=" + command};
        args.push_back(shellEnabled ? "--exec-using-shell" : "--no-exec-using-shell");

        WmFixture fixture(cleanFixture(args));
        x11::DisplayPtr dp = fixture.openDisplay();
        REQUIRE(dp != nullptr);
        Display* d = dp.get();
        parkPointer(d);

        XTestDriver driver(fixture.display());
        driver.moveTo(kParkX, kParkY);

        const std::vector<Window> before = clientList(d);

        Window menu = openRootMenu(d, driver, 400, 300);
        REQUIRE(menu != None);
        Rect menuRect{};
        REQUIRE(WmFixture::pollUntil([&] {
            return serverRect(d, menu, menuRect) && menuRect.w > 1 && menuRect.h > 1;
        }, 4000));

        driver.moveTo(menuRect.x + menuRect.w / 2, menuRect.y + 14);
        driver.release(Button1);
        XSync(d, False);

        spawnedOut = awaitClientWithClass(d, kProbeClass, before,
                                          shellEnabled ? 15000 : 6000);

        // The sentinel is written by the SECOND half of the command, after the
        // program starts, so it is polled on its own deadline rather than read
        // once. On the negative side the poll is expected to run out -- which is
        // exactly why it has a deadline and not a single read.
        sentinelSeen = WmFixture::pollUntil([&] {
            return pathExists(sentinel);
        }, shellEnabled ? 10000 : 4000);

        INFO("wm stderr:\n" << fixture.wmStderr());
        CHECK(fixture.wmAlive());

        std::string instance;
        if (spawnedOut != None) instance = instanceOf(d, spawnedOut);
        ::unlink(sentinel.c_str());
        return instance;
    };

    bool shellSentinel = false, plainSentinel = false;
    Window shellWindow = None, plainWindow = None;

    const std::string shellInstance = run(true, "on", shellSentinel, shellWindow);
    const std::string plainInstance = run(false, "off", plainSentinel, plainWindow);

    // Positive control: with shell mode ON the argument took effect (the window
    // carries the instance name the argument set) and the metacharacter really
    // was evaluated (the sentinel exists).
    CHECK(shellWindow != None);
    CHECK(shellInstance == "shellmarker");
    CHECK(shellSentinel);

    // The claim: with shell mode OFF nothing at all was executed through a
    // shell. Not merely "the expected window did not appear" -- the sentinel is
    // the direct evidence that the second command never ran.
    CHECK(plainWindow == None);
    CHECK_FALSE(plainSentinel);
    CHECK(plainInstance.empty());
}

TEST_CASE("A setting given on the command line overrides the same setting in the "
          "config file", "[wm_config_runtime]")
{
    // Observed through the running binary, not the parser: frame thickness is
    // the setting because its effect is an exact, absolute geometry fact
    // (yIndent() == FRAME_WIDTH + 1).
    auto verticalInset = [](const std::string& configContents,
                            const std::vector<std::string>& args) {
        WmFixture fixture(configuredFixture(configContents, args));
        x11::DisplayPtr dp = fixture.openDisplay();
        REQUIRE(dp != nullptr);
        Display* d = dp.get();
        parkPointer(d);

        Window win = None;
        Window frame = mapClientAndAwaitFrame(d, 200, 150, 300, 220, win, "precedence");
        REQUIRE(frame != None);
        settleWm(d);

        INFO("wm stderr:\n" << fixture.wmStderr());
        return rectOf(d, win).y - rectOf(d, frame).y;
    };

    // Control: the config FILE is genuinely read. Without this the precedence
    // assertion below would pass just as well against a binary that ignored the
    // file entirely -- which is the failure mode most worth catching, because it
    // is what "the setting parses but reaches nothing" looks like from outside.
    const int fileOnly = verticalInset("frame-thickness = 3\n", {});
    CHECK(fileOnly == 3 + 1);

    // And the CLI wins over it.
    const int cliWins = verticalInset("frame-thickness = 3\n", {"--frame-thickness=20"});
    CHECK(cliWins == 20 + 1);
    CHECK(cliWins != fileOnly);
}


// ===========================================================================
// [wm_errors] -- the terminating X11 error paths (coverage item 6)
//                and the help flag
// ===========================================================================

TEST_CASE("With no display available the binary exits non-zero and names the display",
          "[wm_errors]")
{
    const RunResult r = runBinary({}, {}, /*unsetDisplay=*/true, kFatalTimeoutMs);

    INFO("output:\n" << r.output);
    CHECK_FALSE(r.timedOut);
    CHECK(r.exitedNormally);
    CHECK(r.exitCode != 0);

    // The message must name the DISPLAY as the problem. A bare non-zero exit
    // would be satisfied by any of the other three conditions in this group, so
    // the exit status alone does not identify the path that was taken.
    CHECK(contains(r.output, "can't open display"));
    CHECK(sanitizerClean(r.output));
}

TEST_CASE("An unparseable colour setting exits non-zero and names the offending setting",
          "[wm_errors]")
{
    BareDisplay server;

    // menu-borders is allocated in initialiseScreen() through
    // WindowManager::allocateColour(), whose failure message carries the
    // caller-supplied description -- so the stderr names WHICH colour setting
    // was wrong rather than reporting a generic allocation failure. The tab and
    // frame colours are allocated later, in the first Border, so a case using
    // one of those would additionally need a client to exist.
    const RunResult r = runBinary({"--menu-borders=definitely-not-a-colour"},
                                  {{"DISPLAY", server.display()}},
                                  false, kFatalTimeoutMs);

    INFO("output:\n" << r.output);
    CHECK_FALSE(r.timedOut);
    CHECK(r.exitedNormally);
    CHECK(r.exitCode != 0);
    CHECK(contains(r.output, "menu border"));
    CHECK(contains(r.output, "colour"));
    CHECK(sanitizerClean(r.output));

    // A valid colour on the same display and the same flag must NOT die, or the
    // case above proves only that the binary dislikes being started at all.
    const RunResult ok = runBinary({"--menu-borders=blue"},
                                   {{"DISPLAY", server.display()}},
                                   false, 4000);
    INFO("control output:\n" << ok.output);
    CHECK(ok.timedOut);          // it started successfully and had to be killed
    CHECK_FALSE(contains(ok.output, "couldn't load"));
}

TEST_CASE("With no font available at all the binary exits non-zero and names the menu font",
          "[wm_errors]")
{
    BareDisplay server;

    const std::string fontConfig = makeEmptyFontConfig();
    const RunResult r = runBinary({},
                                  {{"DISPLAY", server.display()},
                                   {"FONTCONFIG_FILE", fontConfig}},
                                  false, kFatalTimeoutMs);

    INFO("FONTCONFIG_FILE: " << fontConfig);
    INFO("output:\n" << r.output);
    CHECK_FALSE(r.timedOut);
    CHECK(r.exitedNormally);
    CHECK(r.exitCode != 0);

    // The MENU font is the fatal one: initialiseScreen() tries the configured
    // pattern, then a generic fallback, and calls fatal() when neither resolves.
    //
    // This subprocess starts NO CLIENT, so it never constructs a Border and
    // never reaches the rotated-tab font ladder. It must not be read as evidence
    // about that ladder; plan 08-06's RENDER-less group is what proves the tab
    // font degrades without terminating, and the two claims are deliberately
    // kept apart.
    CHECK(contains(r.output, "menu font"));
    CHECK(sanitizerClean(r.output));
}

TEST_CASE("A second window manager fails cleanly and leaves the incumbent working",
          "[wm_errors]")
{
    WmFixture incumbent;
    x11::DisplayPtr dp = incumbent.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    const RunResult r = runBinary({}, {{"DISPLAY", incumbent.display()}},
                                  false, kFatalTimeoutMs);

    INFO("second WM output:\n" << r.output);
    CHECK_FALSE(r.timedOut);
    CHECK(r.exitedNormally);
    CHECK(r.exitCode != 0);

    // It must identify the CONFLICT, not report a generic protocol error and
    // leave the user to work out that another window manager is the cause.
    CHECK(contains(r.output, "another window manager"));
    CHECK(sanitizerClean(r.output));

    // And the incumbent must be untouched. A conflict that takes down the
    // running session would be considerably worse than one that fails cleanly,
    // and "still alive" is not the same claim as "still working" -- so this
    // maps a fresh client and requires it to be framed and published.
    REQUIRE(incumbent.wmAlive());

    Window win = None;
    Window frame = mapClientAndAwaitFrame(d, 240, 180, 260, 190, win, "survivor");
    INFO("incumbent stderr:\n" << incumbent.wmStderr());
    REQUIRE(frame != None);
    CHECK(listed(d, win));
}

TEST_CASE("Every terminating path exits within a deadline rather than hanging",
          "[wm_errors]")
{
    // A path that HANGS instead of exiting is a distinct and worse failure than
    // one that exits with the wrong status -- the process sits there holding a
    // display, and only a deadline tells the two apart. The four cases above use
    // a generous timeout so a loaded host does not flake; this one re-runs the
    // same launches against a tight deadline and reports what each measured.
    BareDisplay server;
    const std::string fontConfig = makeEmptyFontConfig();

    struct Path { const char* name; RunResult result; };
    std::vector<Path> paths;

    paths.push_back({"no display",
                     runBinary({}, {}, true, kFatalDeadlineMs)});

    paths.push_back({"invalid colour",
                     runBinary({"--menu-borders=definitely-not-a-colour"},
                               {{"DISPLAY", server.display()}}, false, kFatalDeadlineMs)});

    paths.push_back({"no font",
                     runBinary({}, {{"DISPLAY", server.display()},
                                    {"FONTCONFIG_FILE", fontConfig}},
                               false, kFatalDeadlineMs)});

    {
        WmFixture incumbent;
        paths.push_back({"root redirect conflict",
                         runBinary({}, {{"DISPLAY", incumbent.display()}},
                                   false, kFatalDeadlineMs)});
    }

    for (const Path& p : paths) {
        INFO("path: " << p.name << " elapsed=" << p.result.elapsedMs << "ms"
             << " exit=" << p.result.exitCode
             << "\noutput:\n" << p.result.output);
        CHECK_FALSE(p.result.timedOut);
        CHECK(p.result.elapsedMs < kFatalDeadlineMs);
        CHECK(p.result.exitCode != 0);
    }
}

TEST_CASE("The help flag prints usage and exits successfully", "[wm_errors]")
{
    // Run with NO DISPLAY, which is the case that matters: help that needs an X
    // server is help you cannot read when you are trying to work out why the
    // window manager will not start.
    const RunResult r = runBinary({"--help"}, {}, true, kFatalTimeoutMs);

    INFO("output:\n" << r.output);
    CHECK_FALSE(r.timedOut);
    REQUIRE(r.exitedNormally);
    CHECK(r.exitCode == 0);

    // At least the string settings and BOTH halves of the boolean pairs. The
    // usage text is generated from the same rows getopt_long() is driven by, so
    // these are spot checks on a generated list rather than a second copy of it.
    CHECK(contains(r.output, "--frame-thickness"));
    CHECK(contains(r.output, "--new-window-command"));
    CHECK(contains(r.output, "--tab-foreground"));
    CHECK(contains(r.output, "--auto-raise"));
    CHECK(contains(r.output, "--no-auto-raise"));
    CHECK(contains(r.output, "--no-click-to-focus"));
    CHECK(contains(r.output, "--no-focus-stealing-prevention"));
    CHECK(contains(r.output, "--destroy-window-delay"));
    CHECK(contains(r.output, "--exec-using-shell"));
    CHECK(sanitizerClean(r.output));
}

TEST_CASE("An unrecognised flag exits non-zero and its advice names a flag that works",
          "[wm_errors]")
{
    const RunResult bad = runBinary({"--nosuchflag"}, {}, true, kFatalTimeoutMs);

    INFO("output:\n" << bad.output);
    CHECK_FALSE(bad.timedOut);
    REQUIRE(bad.exitedNormally);
    CHECK(bad.exitCode != 0);
    CHECK(contains(bad.output, "unrecognized option"));

    // The advice line. Before this plan it pointed at a flag that was not in the
    // option table, so following it produced the same error again -- the binary
    // told the user to do something it then refused to do.
    REQUIRE(contains(bad.output, "--help"));

    // Follow the advice, from the text itself rather than from a literal this
    // test happens to agree with. A future rewording that pointed somewhere else
    // would take this assertion with it.
    const std::string advice = bad.output.substr(bad.output.find("--help"));
    std::string flag;
    for (char ch : advice) {
        if (ch == '\'' || ch == ' ' || ch == '\n') break;
        flag.push_back(ch);
    }
    INFO("following the binary's own advice: " << flag);

    const RunResult advised = runBinary({flag}, {}, true, kFatalTimeoutMs);
    INFO("advised output:\n" << advised.output);
    CHECK_FALSE(advised.timedOut);
    REQUIRE(advised.exitedNormally);
    CHECK(advised.exitCode == 0);
    CHECK_FALSE(contains(advised.output, "unrecognized option"));
}
