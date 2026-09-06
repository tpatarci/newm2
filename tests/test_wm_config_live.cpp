// Live configuration against the REAL compiled window manager, driven through
// the REAL compiled wm2-ctl (CGUI-02, CGUI-04, plan 09-04, TEST-05).
//
// The claim this file exists to check: a command typed at a shell changes
// windows that are ALREADY OPEN. Not windows mapped afterwards -- those would
// pick up a new value at map time even from a window manager that merely stored
// it and never applied it, which is precisely the defect the central case here
// is shaped to catch. Every live case therefore maps its client FIRST, records
// the geometry, and only then issues the command.
//
// The client is the shipped wm2-ctl binary rather than a socket client written
// here. That is deliberate and is what D-17 asks for: the tool a user runs over
// SSH and the tool this suite proves the protocol with are one program, so the
// suite cannot pass against a client the user does not have.
//
// The rules this file inherits, none of them optional:
//
//   OUTCOMES ARE OBSERVED THROUGH THE SERVER OR THROUGH THE TOOL'S OWN EXIT
//   CODE AND OUTPUT. Never a claim about window-manager internals.
//
//   NO sleep()-BASED SYNCHRONISATION. Every wait is a deadline-bounded poll
//   whose exit condition is a real observation.
//
//   EVERY PROCESS IS TERMINATED BY A PID THIS FILE CREATED, never by a name
//   pattern and never by a scan of the process table.
//
//   NO CASE READS OR WRITES THE DEVELOPER'S REAL CONFIGURATION. Every fixture
//   here overrides XDG_CONFIG_HOME (and XDG_CONFIG_DIRS) into a directory it
//   made under the CMake binary tree.
//
//   THE WINDOW MANAGER'S STDERR IS BOUND TO A LOCAL AND EMITTED AS CONTEXT
//   BEFORE any assertion that may fail, never after (08.5-06's guard-ordering
//   defect: a CHECK that fails takes the case with it, and an INFO written
//   afterwards is never reached).

#include <catch2/catch_test_macros.hpp>

#include "support/WmFixture.h"
#include "support/PixelVerdict.h"
#include "support/XTestDriver.h"

#include "Config.h"
#include "ConfigFileWriter.h"
#include "SocketServer.h"

#include "x11wrap.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifndef WM2_CTL_PATH
#error "WM2_CTL_PATH must be defined by the build system"
#endif

using namespace wm2test;

namespace {

using Clock = std::chrono::steady_clock;

// X protocol errors are a RACE in the geometry helpers below, not a failure
// (08.5-13's QuietXErrors, and the reason two [wm_socket] cases died outright
// on their first run). A child listed by XQueryTree can be destroyed before
// XGetGeometry names it, and Xlib's default handler kills the test process with
// no assertion output at all. Counted rather than merely swallowed, so a helper
// erroring systematically stays visible.
int g_xErrorCount = 0;

int quietXErrorHandler(Display*, XErrorEvent*)
{
    ++g_xErrorCount;
    return 0;
}

struct QuietXErrors {
    QuietXErrors() { XSetErrorHandler(quietXErrorHandler); }
};

const QuietXErrors g_quietXErrors;

// The fixture already defines wm2test::pollSleep(); named differently here
// rather than shadowed, because `using namespace wm2test` above would make
// an identically named local an ambiguity rather than an override.
void settleTick() { std::this_thread::sleep_for(std::chrono::milliseconds(20)); }

// ---------------------------------------------------------------------------
// Isolated configuration
// ---------------------------------------------------------------------------

// An XDG tree of this case's own, under the CMake binary directory rather than
// a bare /tmp name (threat T-8-TMP), unique per process and per call so two
// concurrent ctest workers cannot read each other's files. Returns the
// directory to hand the child as XDG_CONFIG_HOME.
std::string makeConfigHome(const std::string& contents)
{
    static int counter = 0;
    const std::string base = std::string(WM2_TEST_WORKDIR) + "/live-cfg-" +
                             std::to_string(::getpid()) + "-" +
                             std::to_string(++counter);
    ::mkdir(base.c_str(), 0700);
    ::mkdir((base + "/wm2-born-again").c_str(), 0700);

    std::ofstream out(base + "/wm2-born-again/config");
    out << contents;
    out.close();
    return base;
}

std::string configFileIn(const std::string& home)
{
    return home + "/wm2-born-again/config";
}

void writeConfigFile(const std::string& home, const std::string& contents)
{
    std::ofstream out(configFileIn(home));
    out << contents;
    out.close();
}

// XDG_CONFIG_DIRS is overridden as well as XDG_CONFIG_HOME, and that is not
// belt-and-braces: xdgConfigDirs() falls back to /etc/xdg when the variable is
// unset, so on a machine that happens to have a system-wide config the host's
// settings would be layered UNDER every case here and quietly change what it
// proves. Pointed at a directory that does not exist, which applyFile() skips.
WmFixtureOptions fixtureWithConfigHome(const std::string& home)
{
    WmFixtureOptions o;
    o.childEnv["XDG_CONFIG_HOME"] = home;
    o.childEnv["XDG_CONFIG_DIRS"] = home + "/no-system-config";
    return o;
}

// ---------------------------------------------------------------------------
// Running wm2-ctl
// ---------------------------------------------------------------------------

struct CtlResult {
    int exitCode = -1;
    std::string out;
    std::string err;

    std::string describe() const
    {
        return "wm2-ctl exit=" + std::to_string(exitCode) +
               "\n  stdout: " + out + "\n  stderr: " + err;
    }
};

std::string readWholeFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    return content;
}

// Fork and exec the built wm2-ctl, with its stdout and stderr bound to files
// (never a pipe that could fill and deadlock the child), and reap it through
// the fixture's ChildProcess -- which terminates by the PID it created and
// escalates to SIGKILL on a deadline. `display` empty means "run with no
// DISPLAY at all", which is how the no-window-manager case is expressed.
CtlResult runCtl(const std::vector<std::string>& args,
                 const std::string& display,
                 const std::map<std::string, std::string>& extraEnv = {})
{
    static int counter = 0;
    const std::string stem = std::string(WM2_TEST_WORKDIR) + "/ctl-" +
                             std::to_string(::getpid()) + "-" +
                             std::to_string(++counter);
    const std::string outPath = stem + ".out";
    const std::string errPath = stem + ".err";

    CtlResult result;

    const pid_t pid = ::fork();
    if (pid < 0) return result;

    if (pid == 0) {
        const int outFd = ::open(outPath.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
        const int errFd = ::open(errPath.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
        if (outFd >= 0) { ::dup2(outFd, STDOUT_FILENO); ::close(outFd); }
        if (errFd >= 0) { ::dup2(errFd, STDERR_FILENO); ::close(errFd); }

        if (display.empty()) ::unsetenv("DISPLAY");
        else                 ::setenv("DISPLAY", display.c_str(), 1);
        for (const auto& kv : extraEnv) ::setenv(kv.first.c_str(), kv.second.c_str(), 1);

        std::vector<std::string> owned;
        owned.push_back(WM2_CTL_PATH);
        for (const std::string& a : args) owned.push_back(a);

        std::vector<char*> argv;
        argv.reserve(owned.size() + 1);
        for (std::string& a : owned) argv.push_back(&a[0]);
        argv.push_back(nullptr);

        ::execv(WM2_CTL_PATH, argv.data());
        ::_exit(127);
    }

    ChildProcess child(pid);
    if (!child.waitForExit(20000)) {
        child.shutdown();          // by the PID this function created
        result.exitCode = -1;
    } else {
        result.exitCode = child.exitCode();
    }

    result.out = readWholeFile(outPath);
    result.err = readWholeFile(errPath);
    ::unlink(outPath.c_str());
    ::unlink(errPath.c_str());
    return result;
}

CtlResult ctl(const WmFixture& fixture, const std::vector<std::string>& args)
{
    return runCtl(args, fixture.display());
}

// The effective value, straight from the window manager. Trailing newline
// stripped, because `get` prints the bare value for a shell to capture.
std::string ctlGet(const WmFixture& fixture, const std::string& key)
{
    CtlResult r = ctl(fixture, {"get", key});
    if (r.exitCode != 0) return std::string("<get failed: ") + r.describe() + ">";
    std::string value = r.out;
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
    return value;
}

// ---------------------------------------------------------------------------
// Server observation
// ---------------------------------------------------------------------------

struct Rect { int x = 0, y = 0, w = 0, h = 0; };

bool operator==(const Rect& a, const Rect& b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

std::string describe(const Rect& r)
{
    return "(" + std::to_string(r.x) + "," + std::to_string(r.y) + " " +
           std::to_string(r.w) + "x" + std::to_string(r.h) + ")";
}

// Wake the window manager's event loop so it flushes its X output buffer. The
// nudge is override-redirect, so eventCreate() returns immediately for it and
// it can never be managed or perturb an assertion.
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
    out.x = absX; out.y = absY;
    out.w = static_cast<int>(width); out.h = static_cast<int>(height);
    return true;
}

Rect rectOf(Display* d, Window w) { Rect r; serverRect(d, w, r); return r; }

Window parentOf(Display* d, Window w)
{
    Window wroot = None, parent = None, *children = nullptr;
    unsigned int n = 0;
    if (!XQueryTree(d, w, &wroot, &parent, &children, &n)) return None;
    if (children) XFree(children);
    return parent;
}

Window createClient(Display* d, int x, int y, int w, int h, const char* name)
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
    return framed ? frame : None;
}

Window mapClientAndAwaitFrame(Display* d, int x, int y, int w, int h,
                              Window& clientOut, const char* name)
{
    Window win = createClient(d, x, y, w, h, name);
    clientOut = win;
    XMapWindow(d, win);
    XSync(d, False);
    return awaitFrameFor(d, win);
}

// Poll until the frame's geometry stops matching `before`, or the deadline
// expires. Returns the geometry seen last either way, so a failing assertion
// can print what it actually was.
Rect awaitFrameChange(Display* d, Window frame, const Rect& before, int timeoutMs = 8000)
{
    Rect now = before;
    WmFixture::pollUntil([&] {
        pumpWm(d);
        now = rectOf(d, frame);
        return !(now == before);
    }, timeoutMs);
    return now;
}

// The socket path this build resolves for a display, which is what wm2-ctl
// resolves too (DISC-01b). Used only by the cases that need to name a path that
// does NOT exist.
std::string socketPathFor(const std::string& display)
{
    return configSocketPath(display.c_str());
}


// ---------------------------------------------------------------------------
// Colour observation (plan 09-05)
//
// Read back from the SERVER with XGetImage over the ROOT window, covering the
// rectangle the window of interest occupies. Reading root rather than the
// window itself is deliberate and is the same reason tests/test_wm_runtime.cpp
// gives: the frame and the tab are SHAPED, and XGetImage's result outside a
// window's bounding shape is undefined. Root is never shaped, so what comes
// back is exactly what is on the screen.
// ---------------------------------------------------------------------------

// The Xvfb geometry WmFixture starts every display with, so a capture can be
// clamped to the screen rather than erroring on an off-screen rectangle.
constexpr int kScreenW = 1024;
constexpr int kScreenH = 768;

// The alias, rather than a second spelling, so classify() takes what
// captureRoot() returns with no conversion (08.5-13 Task 1).
using Histogram = PixelHistogram;

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
        for (int ix = 0; ix < w; ++ix) ++h[XGetPixel(img, ix, iy)];
    }
    XDestroyImage(img);
    return h;
}

long countOf(const Histogram& h, unsigned long pixel)
{
    auto it = h.find(pixel);
    return it == h.end() ? 0 : it->second;
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

// The share of the sample the dominant value holds. Compared against
// PixelVerdict.h's kDominanceFloor so the colour cases here use the SAME
// positive criterion the menu paint decision uses, rather than a second
// threshold that could drift from it.
double dominantShare(const Histogram& h)
{
    long total = 0, best = -1;
    for (const auto& kv : h) { total += kv.second; if (kv.second > best) best = kv.second; }
    if (total <= 0) return 0.0;
    return static_cast<double>(best) / static_cast<double>(total);
}

// The pixel value the server resolves a colour NAME to on this display, which
// turns "the tab is red" into an absolute assertion rather than a comparison
// against another run.
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

std::string describeTop(const Histogram& h, std::size_t n = 4)
{
    std::vector<std::pair<long, unsigned long>> byCount;
    for (const auto& kv : h) byCount.push_back({kv.second, kv.first});
    std::sort(byCount.rbegin(), byCount.rend());
    std::string out = "{";
    for (std::size_t i = 0; i < byCount.size() && i < n; ++i) {
        out += hex(byCount[i].second) + "x" + std::to_string(byCount[i].first);
        if (i + 1 < byCount.size() && i + 1 < n) out += ", ";
    }
    out += "}";
    return out;
}

std::vector<Window> childrenOf(Display* d, Window w)
{
    Window wroot = None, parent = None, *children = nullptr;
    unsigned int n = 0;
    std::vector<Window> out;
    if (!XQueryTree(d, w, &wroot, &parent, &children, &n)) return out;
    if (children) { out.assign(children, children + n); XFree(children); }
    return out;
}

bool localRect(Display* d, Window w, Rect& out)
{
    Window rootRet = None;
    int x = 0, y = 0;
    unsigned int width = 0, height = 0, bw = 0, depth = 0;
    if (!XGetGeometry(d, w, &rootRet, &x, &y, &width, &height, &bw, &depth)) return false;
    out.x = x; out.y = y;
    out.w = static_cast<int>(width); out.h = static_cast<int>(height);
    return true;
}

// The frame's tab and button, separated by SIZE rather than by origin: the
// button covers the whole top square of the tab and so shares the tab's (0,0)
// origin, and an origin test finds whichever the server happens to list first.
Window findFrameChild(Display* d, Window frame, Window client, bool wantButton)
{
    Window button = None, tab = None;
    for (Window child : childrenOf(d, frame)) {
        if (child == client) continue;
        Rect r;
        if (!localRect(d, child, r)) continue;
        if (r.w <= 0 || r.h <= 0) continue;
        if (r.w == r.h && r.w <= 64) button = child;
        else                         tab = child;
    }
    return wantButton ? button : tab;
}

// The tab COLUMN of a frame, below the square the button occupies.
//
// The tab WINDOW spans the whole width of the frame -- it is the top band as
// well as the side column -- so its own rectangle is mostly the client window
// and says nothing about the tab's colour. The column's thickness is derived
// from the two origins rather than from a magic number: Border::xIndent() is
// m_tabWidth + FRAME_WIDTH + 1 and the client sits at that offset inside the
// frame, so the caller needs to know only the configured frame thickness and
// nothing about how a font is measured.
Rect tabColumnRect(const Rect& frameRect, const Rect& clientRect,
                   int frameThickness, int height = 80)
{
    const int tabW = (clientRect.x - frameRect.x) - frameThickness - 1;
    return Rect{frameRect.x, frameRect.y + tabW + 2, tabW, height};
}

// The tab's own TOP BAND: the strip of tab background that runs across the top
// of the frame above the client, between the one-pixel border rows
// Border::shapeTab() leaves at y=0 and y=TAB_TOP_HEIGHT+1.
//
// This is where the tab background is the ONLY thing on the screen. The column
// carries the label as well, and a long title puts enough ink on it that no
// single value dominates -- which is a fact about the label, not about whether
// the background was repainted. Note also that the band is painted by the
// SERVER from the tab window's background pixel rather than by drawLabel(), so
// a case asserting on it is asserting that the live path re-set that pixel and
// cleared the window, not merely that it redrew the label.
Rect tabBandRect(const Rect& frameRect, const Rect& clientRect, int frameThickness)
{
    const int tabW = (clientRect.x - frameRect.x) - frameThickness - 1;
    return Rect{frameRect.x + tabW + 4, frameRect.y + 1, clientRect.w - 8, 2};
}

// Where the pointer is parked so it is over neither a client nor the menu.
constexpr int kParkX = 5;
constexpr int kParkY = 5;

void parkPointer(Display* d)
{
    XWarpPointer(d, None, DefaultRootWindow(d), 0, 0, 0, 0, kParkX, kParkY);
    XSync(d, False);
}

void settleWm(Display* d)
{
    for (int i = 0; i < 15; ++i) { pumpWm(d); settleTick(); }
}

// Poll until `pixel` appears at least `atLeast` times in the capture of `r`,
// returning the last histogram seen either way -- so a failing assertion can
// print what was actually on the screen rather than only that it was wrong.
Histogram awaitPixelIn(Display* d, const Rect& r, unsigned long pixel,
                       long atLeast, int timeoutMs = 8000)
{
    Histogram last;
    WmFixture::pollUntil([&] {
        pumpWm(d);
        last = captureRoot(d, r);
        return countOf(last, pixel) >= atLeast;
    }, timeoutMs);
    return last;
}

// ---------------------------------------------------------------------------
// The root menu, opened with a real button press
//
// WindowManager::menu() runs a NESTED event loop with its own pointer grab, so
// while the menu is up the window manager is not in loop() and pumpWm() cannot
// wake it. Everything a menu case observes is read straight from the server
// while the press is still held.
// ---------------------------------------------------------------------------

bool isViewable(Display* d, Window w)
{
    XWindowAttributes a;
    if (!XGetWindowAttributes(d, w, &a)) return false;
    return a.map_state == IsViewable;
}

// Identified by the press point it is anchored on, never by "the first viewable
// child of root" -- 08.5-13 removed exactly that fallback after it silently
// sampled a client frame and made two different configurations produce
// byte-identical histograms.
Window findOpenMenu(Display* d, int pressX, int pressY)
{
    for (Window child : childrenOf(d, DefaultRootWindow(d))) {
        Rect r;
        if (!serverRect(d, child, r)) continue;
        if (r.w <= 1 || r.h <= 1) continue;
        if (!isViewable(d, child)) continue;
        if (pressX >= r.x && pressX < r.x + r.w &&
            pressY >= r.y && pressY < r.y + r.h) return child;
    }
    return None;
}

// Chosen so WindowManager::menu() does not need to clamp the menu to a screen
// edge: a clamp warps the pointer, the warp is a MotionNotify, and a
// MotionNotify over a category row opens a submenu nobody asked for.
constexpr int kMenuPressX = 300;
constexpr int kMenuPressY = 5;

// Open the root menu with a real press and return only once the window manager
// has actually DRAWN it in `expectedBg` -- not merely mapped it. The positive
// dominance criterion is PixelVerdict.h's, shared with the menu paint cases
// rather than restated here.
bool openRootMenu(Display* d, XTestDriver& driver, int x, int y,
                  Window& menuOut, Rect& rectOut, unsigned long expectedBg)
{
    driver.moveTo(x, y);
    driver.press(Button1);

    constexpr int kMenuStageMs = 20000;

    menuOut = None;
    if (!WmFixture::pollUntil([&] {
            menuOut = findOpenMenu(d, x, y);
            return menuOut != None;
        }, kMenuStageMs)) {
        UNSCOPED_INFO("openRootMenu: stage 1 (mapped) expired");
        return false;
    }
    if (!WmFixture::pollUntil([&] {
            return serverRect(d, menuOut, rectOut) && rectOut.w > 1 && rectOut.h > 1;
        }, kMenuStageMs)) {
        UNSCOPED_INFO("openRootMenu: stage 2 (geometry) expired");
        return false;
    }

    PaintVerdict last = PaintVerdict::NoPixels;
    Histogram lastSeen;
    const bool painted = WmFixture::pollUntil([&] {
        lastSeen = captureRoot(d, rectOut);
        last = classify(lastSeen, expectedBg);
        return last == PaintVerdict::Painted;
    }, kMenuStageMs);

    if (!painted) {
        UNSCOPED_INFO("openRootMenu: stage 3 (painted) expired -- last verdict "
                      << describeVerdict(last) << ", expected " << hex(expectedBg)
                      << ", pixels " << describeTop(lastSeen));
    }
    return painted;
}

// Close a menu the press above left open, by releasing outside every row.
void closeRootMenu(Display* d, XTestDriver& driver)
{
    driver.moveTo(kParkX, kParkY);
    driver.release(Button1);
    XSync(d, False);
    settleWm(d);
}

}  // namespace


// -----------------------------------------------------------------------------
// The tool alone: no window manager, no display, no toolkit
// -----------------------------------------------------------------------------

TEST_CASE("wm2-ctl exits 2 when there is no window manager to talk to",
          "[wm_config_live]")
{
    // A path under the test work directory that nothing is listening on. Named
    // explicitly rather than relying on an unused display number, so this case
    // is deterministic even on a machine running a real desktop.
    const std::string absent = std::string(WM2_TEST_WORKDIR) + "/no-such-socket-" +
                               std::to_string(::getpid());
    ::unlink(absent.c_str());

    CtlResult r = runCtl({"--socket", absent, "status"}, "");
    INFO(r.describe());

    // DISC-01a: 2 means "nothing to talk to", and it is a DIFFERENT code from
    // the refusal below, which is the entire point of having four of them -- a
    // shell script branches on "not running" separately from "refused".
    CHECK(r.exitCode == 2);
    CHECK(r.err.find("wm2:") != std::string::npos);
    CHECK(r.err.find(absent) != std::string::npos);
}

TEST_CASE("wm2-ctl --help lists every subcommand it accepts",
          "[wm_config_live]")
{
    CtlResult r = runCtl({"--help"}, "");
    INFO(r.describe());

    CHECK(r.exitCode == 0);
    for (const char* verb : {"status", "get", "set", "reload"}) {
        INFO("subcommand: " << verb);
        CHECK(r.out.find(verb) != std::string::npos);
    }
    // And the settable keys it advertises come from the option table, so a key
    // named here is a key the parser accepts.
    CHECK(r.out.find("frame-thickness") != std::string::npos);
}

TEST_CASE("wm2-ctl refuses a malformed invocation with the usage code",
          "[wm_config_live]")
{
    // 3, not 2: nothing was attempted, so "no window manager" would be a lie.
    CHECK(runCtl({}, "").exitCode == 3);
    CHECK(runCtl({"nonsense"}, "").exitCode == 3);
    CHECK(runCtl({"get"}, "").exitCode == 3);
    CHECK(runCtl({"set", "frame-thickness"}, "").exitCode == 3);
    CHECK(runCtl({"status", "extra"}, "").exitCode == 3);
    CHECK(runCtl({"--socket"}, "").exitCode == 3);
}

TEST_CASE("the settable key list and the config file's managed key list agree",
          "[wm_config_live]")
{
    // Two lists exist -- configKeySpecs() (what `set` accepts) and
    // configFileManagedKeys() (what the GUI's file writer owns) -- and they must
    // name exactly the same settings, or a key would be settable over the socket
    // and unsavable to the file, or the reverse. Display-free; no fixture.
    std::vector<std::string> settable;
    for (const ConfigKeySpec& spec : configKeySpecs()) settable.push_back(spec.name);
    std::vector<std::string> managed = configFileManagedKeys();

    std::sort(settable.begin(), settable.end());
    std::sort(managed.begin(), managed.end());

    INFO("settable: " << settable.size() << ", managed: " << managed.size());
    CHECK(settable == managed);
}


// -----------------------------------------------------------------------------
// Asking a running window manager
// -----------------------------------------------------------------------------

TEST_CASE("wm2-ctl status prints the seven fields the window manager reports",
          "[wm_config_live]")
{
    WmFixture fixture;
    CtlResult r = ctl(fixture, {"status"});

    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);
    INFO(r.describe());

    REQUIRE(r.exitCode == 0);
    for (const char* field : {"version", "protocol", "uptime", "screen-width",
                              "screen-height", "managed", "hidden"}) {
        INFO("field: " << field);
        CHECK(r.out.find(field) != std::string::npos);
    }
}

TEST_CASE("wm2-ctl get prints the value the window manager is actually using",
          "[wm_config_live]")
{
    // The file says 9. A window manager answering from its built-in default
    // would say 7, and one answering from the file it read would say 9 -- which
    // is what makes this case able to tell the two apart.
    const std::string home = makeConfigHome("frame-thickness=9\n");
    WmFixture fixture(fixtureWithConfigHome(home));

    const std::string value = ctlGet(fixture, "frame-thickness");
    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);
    INFO("get frame-thickness -> '" << value << "'");

    CHECK(value == "9");

    // And a key nobody has: refused by name, with the connection kept open.
    CtlResult unknown = ctl(fixture, {"get", "no-such-setting"});
    INFO(unknown.describe());
    CHECK(unknown.exitCode == 1);
    CHECK(unknown.err.find("no-such-setting") != std::string::npos);
}


// -----------------------------------------------------------------------------
// The central claim: a command changes windows that are ALREADY OPEN
// -----------------------------------------------------------------------------

TEST_CASE("set frame-thickness re-frames a window that was already mapped",
          "[wm_config_live]")
{
    const std::string home = makeConfigHome("frame-thickness=7\n");
    WmFixture fixture(fixtureWithConfigHome(home));
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();

    // MAPPED FIRST. A window mapped after the command would pick the new
    // thickness up at map time even from a window manager that stored the value
    // and never applied it -- the exact defect this case exists to catch.
    Window client = None;
    Window frame = mapClientAndAwaitFrame(d, 200, 150, 300, 220, client, "live-thickness");
    REQUIRE(frame != None);

    const Rect frameBefore  = rectOf(d, frame);
    const Rect clientBefore = rectOf(d, client);

    CtlResult r = ctl(fixture, {"set", "frame-thickness", "21"});

    const Rect frameAfter = awaitFrameChange(d, frame, frameBefore);
    const Rect clientAfter = rectOf(d, client);
    const std::string stderrText = fixture.wmStderr();

    INFO("wm stderr:\n" << stderrText);
    INFO(r.describe());
    INFO("frame  before " << describe(frameBefore)  << " after " << describe(frameAfter));
    INFO("client before " << describe(clientBefore) << " after " << describe(clientAfter));

    CHECK(r.exitCode == 0);

    // The decoration moved and grew: the frame's origin is further up and left
    // by the extra indent, and it is that much bigger.
    CHECK_FALSE(frameAfter == frameBefore);
    CHECK(frameAfter.w > frameBefore.w);
    CHECK(frameAfter.h > frameBefore.h);

    // And the user's own window is untouched -- same size, same place on the
    // screen. A thickness change moves decoration, not content.
    CHECK(clientAfter.w == clientBefore.w);
    CHECK(clientAfter.h == clientBefore.h);
    CHECK(clientAfter.x == clientBefore.x);
    CHECK(clientAfter.y == clientBefore.y);

    // The window manager agrees with itself about what it is now using.
    CHECK(ctlGet(fixture, "frame-thickness") == "21");
}

TEST_CASE("applying the same set twice does nothing the second time",
          "[wm_config_live]")
{
    const std::string home = makeConfigHome("frame-thickness=7\n");
    WmFixture fixture(fixtureWithConfigHome(home));
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d, 200, 150, 300, 220, client, "live-idempotent");
    REQUIRE(frame != None);

    const Rect before = rectOf(d, frame);
    CtlResult first = ctl(fixture, {"set", "frame-thickness", "17"});
    const Rect afterFirst = awaitFrameChange(d, frame, before);

    // Watch the frame for RE-SHAPING, from this connection, and drain
    // everything the first application produced before the second command is
    // issued.
    //
    // ShapeNotify, not ConfigureNotify, and the distinction is the whole
    // instrument: the X server suppresses a ConfigureNotify for a
    // XConfigureWindow that changes nothing, so counting those would report
    // zero for a window manager that dutifully re-laid every frame out on
    // every message -- a case that cannot fail is not a case. The shape
    // extension makes no such comparison: every rectangle-combining request
    // the window manager issues produces a ShapeNotify whether or not the
    // resulting region differs, so this counter sees the WORK rather than only
    // its visible effect. Confirmed by mutation: with applyConfig()'s diff
    // removed, this count goes to a positive number and the case reddens.
    int shapeEventBase = 0, shapeErrorBase = 0;
    REQUIRE(XShapeQueryExtension(d, &shapeEventBase, &shapeErrorBase));
    XShapeSelectInput(d, frame, ShapeNotifyMask);
    XSync(d, False);
    for (int i = 0; i < 10; ++i) { pumpWm(d); settleTick(); }
    while (XPending(d)) { XEvent e; XNextEvent(d, &e); }

    CtlResult second = ctl(fixture, {"set", "frame-thickness", "17"});
    for (int i = 0; i < 15; ++i) { pumpWm(d); settleTick(); }

    int reconfigurations = 0;
    while (XPending(d)) {
        XEvent e;
        XNextEvent(d, &e);
        if (e.type == shapeEventBase + ShapeNotify) ++reconfigurations;
    }
    const Rect afterSecond = rectOf(d, frame);
    const std::string stderrText = fixture.wmStderr();

    INFO("wm stderr:\n" << stderrText);
    INFO("first: "  << first.describe());
    INFO("second: " << second.describe());
    INFO("frame before " << describe(before)
         << " after first " << describe(afterFirst)
         << " after second " << describe(afterSecond));
    INFO("re-shapes after the second set: " << reconfigurations);

    // Same acknowledgement...
    CHECK(first.exitCode == 0);
    CHECK(second.exitCode == 0);
    // ...identical geometry...
    CHECK(afterSecond == afterFirst);
    // ...and no second re-frame at all. applyConfig() diffs before it acts, so
    // an unchanged value performs no work (T-9-22).
    CHECK(reconfigurations == 0);
}

TEST_CASE("a value the parser would clamp is refused and changes nothing",
          "[wm_config_live]")
{
    const std::string home = makeConfigHome("frame-thickness=11\n");
    WmFixture fixture(fixtureWithConfigHome(home));
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d, 200, 150, 300, 220, client, "live-refusal");
    REQUIRE(frame != None);
    const Rect before = rectOf(d, frame);

    // Both bounds, plus a value that is not a number at all, plus a key that
    // does not exist. The parser would have CLAMPED the first two to 1 and 50
    // and acknowledged them; over the socket a clamp is a refusal, because a
    // client waiting on an answer would otherwise be told "yes" to something it
    // did not ask for (T-9-20).
    CtlResult low     = ctl(fixture, {"set", "frame-thickness", "0"});
    CtlResult high    = ctl(fixture, {"set", "frame-thickness", "500"});
    CtlResult rubbish = ctl(fixture, {"set", "frame-thickness", "seven"});
    CtlResult unknown = ctl(fixture, {"set", "no-such-setting", "x"});

    for (int i = 0; i < 15; ++i) { pumpWm(d); settleTick(); }
    const Rect after = rectOf(d, frame);
    const std::string effective = ctlGet(fixture, "frame-thickness");
    const std::string stderrText = fixture.wmStderr();

    INFO("wm stderr:\n" << stderrText);
    INFO("low: "     << low.describe());
    INFO("high: "    << high.describe());
    INFO("rubbish: " << rubbish.describe());
    INFO("unknown: " << unknown.describe());
    INFO("frame before " << describe(before) << " after " << describe(after));

    CHECK(low.exitCode == 1);
    CHECK(high.exitCode == 1);
    CHECK(rubbish.exitCode == 1);
    CHECK(unknown.exitCode == 1);

    // Each refusal NAMES THE KEY, which is what makes the message actionable
    // when a script sets several things in a row.
    CHECK(low.err.find("frame-thickness") != std::string::npos);
    CHECK(high.err.find("frame-thickness") != std::string::npos);
    CHECK(unknown.err.find("no-such-setting") != std::string::npos);

    // And nothing moved.
    CHECK(after == before);
    CHECK(effective == "11");
}

TEST_CASE("a boolean set accepts the spellings the config file accepts",
          "[wm_config_live]")
{
    WmFixture fixture;

    CtlResult t = ctl(fixture, {"set", "click-to-focus", "true"});
    const std::string afterTrue = ctlGet(fixture, "click-to-focus");
    CtlResult zero = ctl(fixture, {"set", "click-to-focus", "0"});
    const std::string afterZero = ctlGet(fixture, "click-to-focus");
    CtlResult nonsense = ctl(fixture, {"set", "click-to-focus", "perhaps"});
    const std::string afterNonsense = ctlGet(fixture, "click-to-focus");

    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);
    INFO("true: " << t.describe() << " -> " << afterTrue);
    INFO("0: " << zero.describe() << " -> " << afterZero);
    INFO("perhaps: " << nonsense.describe() << " -> " << afterNonsense);

    CHECK(t.exitCode == 0);
    CHECK(afterTrue == "true");
    CHECK(zero.exitCode == 0);
    CHECK(afterZero == "false");

    // The config file maps every unrecognised spelling to false SILENTLY. Over
    // the socket that would acknowledge the opposite of what was typed, so it
    // is refused instead -- stricter than the file, never looser.
    CHECK(nonsense.exitCode == 1);
    CHECK(afterNonsense == "false");
}

TEST_CASE("the largest frame thickness with several windows open leaves the "
          "window manager responsive", "[wm_config_live]")
{
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();

    std::vector<Window> clients;
    for (int i = 0; i < 4; ++i) {
        Window c = None;
        Window f = mapClientAndAwaitFrame(d, 100 + i * 40, 80 + i * 40, 240, 180, c,
                                          "live-stress");
        REQUIRE(f != None);
        clients.push_back(c);
    }

    // The top of the range the parser clamps to (T-9-21). The re-layout walks
    // the client list once and reparents nothing, so this is bounded work.
    CtlResult r = ctl(fixture, {"set", "frame-thickness", "50"});

    // The proof of responsiveness is that the window manager still FRAMES a new
    // window afterwards. A boundary that protects by wedging is not a
    // mitigation.
    Window late = None;
    Window lateFrame = mapClientAndAwaitFrame(d, 500, 400, 200, 160, late, "live-after");

    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);
    INFO(r.describe());

    CHECK(r.exitCode == 0);
    CHECK(lateFrame != None);
    CHECK(ctlGet(fixture, "frame-thickness") == "50");
}


// -----------------------------------------------------------------------------
// reload: the two configurations the window manager holds (DISC-07)
// -----------------------------------------------------------------------------

TEST_CASE("reload re-reads the file and applies it to windows already open",
          "[wm_config_live]")
{
    const std::string home = makeConfigHome("frame-thickness=7\n");
    WmFixture fixture(fixtureWithConfigHome(home));
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d, 200, 150, 300, 220, client, "live-reload");
    REQUIRE(frame != None);
    const Rect before = rectOf(d, frame);

    // The file changes UNDER a running window manager, which is exactly the
    // situation a user is in after editing it by hand.
    writeConfigFile(home, "frame-thickness=23\n");
    CtlResult r = ctl(fixture, {"reload"});

    const Rect after = awaitFrameChange(d, frame, before);
    const std::string stderrText = fixture.wmStderr();

    INFO("wm stderr:\n" << stderrText);
    INFO(r.describe());
    INFO("frame before " << describe(before) << " after " << describe(after));

    CHECK(r.exitCode == 0);
    CHECK_FALSE(after == before);
    CHECK(ctlGet(fixture, "frame-thickness") == "23");
}

TEST_CASE("a reload discards a set, because a set writes no file",
          "[wm_config_live]")
{
    const std::string home = makeConfigHome("frame-thickness=13\n");
    WmFixture fixture(fixtureWithConfigHome(home));

    CtlResult set = ctl(fixture, {"set", "frame-thickness", "31"});
    const std::string afterSet = ctlGet(fixture, "frame-thickness");
    CtlResult reload = ctl(fixture, {"reload"});
    const std::string afterReload = ctlGet(fixture, "frame-thickness");
    const std::string fileText = readWholeFile(configFileIn(home));

    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);
    INFO("set: " << set.describe());
    INFO("reload: " << reload.describe());
    INFO("config file now:\n" << fileText);

    CHECK(set.exitCode == 0);
    CHECK(afterSet == "31");
    CHECK(reload.exitCode == 0);

    // D-01: `set` changes the running desktop and nothing else. The file still
    // says what it said, and the reload therefore puts the window manager back
    // on it.
    CHECK(afterReload == "13");
    CHECK(fileText.find("frame-thickness=13") != std::string::npos);
    CHECK(fileText.find("31") == std::string::npos);
}

TEST_CASE("a command-line override still wins after a reload",
          "[wm_config_live]")
{
    // The file and the command line disagree, and the command line is the top
    // layer. A reload that merged the file onto current state instead of
    // re-running the whole layered load would let the file win here -- silently
    // undoing a flag the user started the window manager with.
    const std::string home = makeConfigHome("frame-thickness=9\n");
    WmFixtureOptions options = fixtureWithConfigHome(home);
    options.wmArgs.push_back("--frame-thickness=29");
    WmFixture fixture(options);

    const std::string beforeReload = ctlGet(fixture, "frame-thickness");
    writeConfigFile(home, "frame-thickness=11\n");
    CtlResult r = ctl(fixture, {"reload"});
    const std::string afterReload = ctlGet(fixture, "frame-thickness");

    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);
    INFO(r.describe());
    INFO("before reload: " << beforeReload << ", after: " << afterReload);

    CHECK(beforeReload == "29");
    CHECK(r.exitCode == 0);
    CHECK(afterReload == "29");
}

TEST_CASE("a reload that cannot read the user file changes nothing and names it",
          "[wm_config_live]")
{
    if (::geteuid() == 0) {
        // root can read a mode-000 file, so the condition under test cannot be
        // created. Skipped by construction rather than asserted falsely.
        WARN("running as root: the unreadable-file case cannot be constructed");
        return;
    }

    const std::string home = makeConfigHome("frame-thickness=15\n");
    WmFixture fixture(fixtureWithConfigHome(home));

    const std::string before = ctlGet(fixture, "frame-thickness");

    const std::string path = configFileIn(home);
    writeConfigFile(home, "frame-thickness=41\n");
    REQUIRE(::chmod(path.c_str(), 0000) == 0);

    CtlResult r = ctl(fixture, {"reload"});
    const std::string after = ctlGet(fixture, "frame-thickness");

    // Restored immediately, so a failure later in this case cannot leave an
    // unreadable file behind for the fixture's teardown to trip over.
    ::chmod(path.c_str(), 0600);

    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);
    INFO(r.describe());
    INFO("before " << before << ", after " << after);

    CHECK(before == "15");
    CHECK(r.exitCode == 1);
    CHECK(r.err.find(path) != std::string::npos);
    // The window manager kept running on the configuration it already had --
    // it did not fall back to defaults and did not half-apply the new file.
    CHECK(after == "15");
}


// -----------------------------------------------------------------------------
// Discovery, and the boundary the tool inherits
// -----------------------------------------------------------------------------

TEST_CASE("wm2-ctl finds the socket from DISPLAY, and honours --socket",
          "[wm_config_live]")
{
    WmFixture fixture;

    // DISC-01b: the path is RESOLVED from the display name rather than read off
    // the root window, because reading a property would mean linking X11 into a
    // tool whose whole value is that it does not. This case asserts the two
    // spellings agree rather than merely both working.
    const std::string resolved = socketPathFor(fixture.display());
    CtlResult viaDisplay = ctl(fixture, {"status"});
    CtlResult viaPath = runCtl({"--socket", resolved, "status"}, "");

    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);
    INFO("resolved path: " << resolved);
    INFO("via DISPLAY: " << viaDisplay.describe());
    INFO("via --socket: " << viaPath.describe());

    CHECK(viaDisplay.exitCode == 0);
    CHECK(viaPath.exitCode == 0);
    CHECK(viaPath.out.find("protocol") != std::string::npos);

    // With neither a display nor a path there is nothing to resolve, and that
    // is a usage error rather than a claim that no window manager is running.
    CtlResult blind = runCtl({"status"}, "");
    INFO("no DISPLAY: " << blind.describe());
    CHECK(blind.exitCode == 3);
}


// -----------------------------------------------------------------------------
// Colours change the screen, now (CGUI-04, plan 09-05)
//
// The claim: every one of the nine colour keys repaints what is already on the
// desktop the moment it is set, with no window closing and no restart. Each
// case therefore MAPS FIRST and captures BEFORE, exactly as the frame-thickness
// case above does, so a window manager that merely stored the value could not
// pass.
//
// Every colour a case sets is FAR FROM THE DEFAULT IN EVERY CHANNEL. A colour
// close to the shipped silver would let an assertion pass on a repaint that
// never happened, because the sample would contain the wanted value anyway.
// -----------------------------------------------------------------------------

TEST_CASE("every frame colour set over the socket repaints a window already on screen",
          "[wm_config_live]")
{
    // The starting palette is written into the config file rather than left at
    // the shipped defaults, so each "was not there before" assertion is against
    // a value this case chose. tab-foreground in particular starts at a colour
    // nothing else in the frame uses, which is what lets the label's OLD ink be
    // asserted absent afterwards -- the shipped #000000 is also the border, and
    // black disappearing from a tab would mean nothing.
    const std::string home = makeConfigHome(
        "frame-thickness=7\n"
        "tab-foreground=#ff00ff\n"
        "tab-background=#c8cacc\n"
        "frame-background=#dcdee0\n"
        "button-background=#dcdee0\n"
        "borders=#ff8000\n");
    WmFixture fixture(fixtureWithConfigHome(home));
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    // A long run of narrow glyphs, so the label puts a usable amount of ink on
    // the tab: Border::drawLabel() returns before drawing anything at all for a
    // window with no name, and the foreground colour would never reach a pixel.
    Window client = None;
    Window frame = mapClientAndAwaitFrame(d, 260, 180, 320, 240, client,
                                          "IIIIIIIIIIIIIIIIIIII");
    REQUIRE(frame != None);
    settleWm(d);

    const Window tab    = findFrameChild(d, frame, client, false);
    const Window button = findFrameChild(d, frame, client, true);
    REQUIRE(tab != None);
    REQUIRE(button != None);

    const Rect frameRect  = rectOf(d, frame);
    const Rect clientRect = rectOf(d, client);
    const Rect buttonRect = rectOf(d, button);
    (void)tab;

    // EACH KEY'S OWN RECTANGLE, derived from the two geometries above and the
    // frame thickness this case's config file sets -- never from a magic
    // number. The whole frame rectangle is dominated by the CLIENT's own white,
    // which says nothing about any of these colours, so each one is sampled
    // where it is the only thing on the screen.
    //
    // Border::xIndent() is m_tabWidth + FRAME_WIDTH + 1 and the client sits at
    // that offset inside the frame, so the tab's thickness follows from the two
    // origins without the test knowing how a font is measured.
    const int kThickness = 7;                                   // the file's value
    const Rect tabBand   = tabBandRect(frameRect, clientRect, kThickness);
    const Rect tabColumn = tabColumnRect(frameRect, clientRect, kThickness);
    REQUIRE(tabColumn.w > 4);

    // The frame background's own rectangle: the vertical strip the frame's
    // clip region leaves between the tab and the client -- FRAME_WIDTH - 2
    // pixels wide, running the client's height (Border::setFrameVisibility).
    Rect frameStrip{clientRect.x - (kThickness - 1), clientRect.y + 20,
                    kThickness - 2, 100};

    // The button's own rectangle is its INTERIOR. The one-pixel ring around it
    // is the difference between the parent's bounding and clip regions, which
    // the server paints from the parent's BORDER pixel -- that ring belongs to
    // `borders`, not to `button-background`.
    Rect buttonInner{buttonRect.x + 1, buttonRect.y + 1,
                     buttonRect.w - 2, buttonRect.h - 2};

    // The border's own rectangle: the tab window's top row. A shaped window's
    // area between its bounding and its clip region is painted by the server
    // from the window's BORDER pixel, and Border::shapeTab() makes that row
    // exactly one pixel tall across the whole width of the frame.
    Rect borderStrip{frameRect.x, frameRect.y, frameRect.w - 2, 1};

    const unsigned long oldInk   = namedPixel(d, "#ff00ff");
    const unsigned long newTabBg = namedPixel(d, "#ff0000");
    const unsigned long newFrmBg = namedPixel(d, "#00cc00");
    const unsigned long newBtnBg = namedPixel(d, "#0000ff");
    const unsigned long newBord  = namedPixel(d, "#00ffff");
    const unsigned long newInk   = namedPixel(d, "#ffff00");
    REQUIRE(oldInk   != ~0UL);
    REQUIRE(newTabBg != ~0UL);

    const Histogram bandBefore   = captureRoot(d, tabBand);
    const Histogram tabBefore    = captureRoot(d, tabColumn);
    const Histogram frameBefore  = captureRoot(d, frameStrip);
    const Histogram buttonBefore = captureRoot(d, buttonInner);
    const Histogram borderBefore = captureRoot(d, borderStrip);

    // The label's ink is on the tab before anything is set. Without this the
    // "old ink is gone" assertion below could pass on a tab that never had any.
    INFO("tab before: " << describeTop(tabBefore));
    REQUIRE(countOf(tabBefore, oldInk) > 0);

    CtlResult setTabBg = ctl(fixture, {"set", "tab-background", "#ff0000"});
    CtlResult setFrmBg = ctl(fixture, {"set", "frame-background", "#00cc00"});
    CtlResult setBtnBg = ctl(fixture, {"set", "button-background", "#0000ff"});
    CtlResult setBord  = ctl(fixture, {"set", "borders", "#00ffff"});
    CtlResult setInk   = ctl(fixture, {"set", "tab-foreground", "#ffff00"});

    const Histogram bandAfter   = awaitPixelIn(d, tabBand, newTabBg, 100);
    const Histogram tabAfter    = awaitPixelIn(d, tabColumn, newInk, 1);
    const Histogram frameAfter  = awaitPixelIn(d, frameStrip, newFrmBg, 50);
    const Histogram buttonAfter = awaitPixelIn(d, buttonInner, newBtnBg, 15);
    const Histogram borderAfter = awaitPixelIn(d, borderStrip, newBord, 5);

    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);
    INFO("tab-background:    " << setTabBg.describe());
    INFO("frame-background:  " << setFrmBg.describe());
    INFO("button-background: " << setBtnBg.describe());
    INFO("borders:           " << setBord.describe());
    INFO("tab-foreground:    " << setInk.describe());
    INFO("band   before " << describeTop(bandBefore)   << " after " << describeTop(bandAfter));
    INFO("tab    before " << describeTop(tabBefore)    << " after " << describeTop(tabAfter));
    INFO("frame  before " << describeTop(frameBefore)  << " after " << describeTop(frameAfter));
    INFO("button before " << describeTop(buttonBefore) << " after " << describeTop(buttonAfter));
    INFO("border before " << describeTop(borderBefore) << " after " << describeTop(borderAfter));

    CHECK(setTabBg.exitCode == 0);
    CHECK(setFrmBg.exitCode == 0);
    CHECK(setBtnBg.exitCode == 0);
    CHECK(setBord.exitCode == 0);
    CHECK(setInk.exitCode == 0);

    // Each key's own rectangle is now DOMINATED by the value that key names,
    // at or above the share PixelVerdict.h already uses as its positive paint
    // criterion -- not merely "the colour appears somewhere".
    CHECK(dominantPixel(bandAfter) == newTabBg);
    CHECK(dominantShare(bandAfter) >= kDominanceFloor);
    CHECK(dominantPixel(frameAfter) == newFrmBg);
    CHECK(dominantShare(frameAfter) >= kDominanceFloor);
    CHECK(dominantPixel(buttonAfter) == newBtnBg);
    CHECK(dominantShare(buttonAfter) >= kDominanceFloor);
    CHECK(dominantPixel(borderAfter) == newBord);
    CHECK(dominantShare(borderAfter) >= kDominanceFloor);

    // ...and none of them was there before, so the assertion cannot be passing
    // on a colour the frame already happened to contain.
    CHECK(countOf(bandBefore, newTabBg) == 0);
    CHECK(countOf(frameBefore, newFrmBg) == 0);
    CHECK(countOf(buttonBefore, newBtnBg) == 0);
    CHECK(countOf(borderBefore, newBord) == 0);

    // The label was redrawn in the new ink and none of the old ink survives
    // inside the tab. Xft antialiases, so the count is small; the glyph cores
    // are the exact colour.
    CHECK(countOf(tabAfter, newInk) > 0);
    CHECK(countOf(tabAfter, oldInk) == 0);

    // And the window manager agrees with itself about what it is drawing with.
    CHECK(ctlGet(fixture, "tab-background") == "#ff0000");
    CHECK(ctlGet(fixture, "tab-foreground") == "#ffff00");
}

TEST_CASE("the tab bevel is re-derived from the new tab background",
          "[wm_config_live]")
{
    // The bevel shades are DERIVED from the configured tab background (plan
    // 08.5-02), which is what makes a dark palette get bevels that belong to it
    // rather than a fixed near-white line reading as a rendering fault. A
    // live-applied tab background that left the bevel GCs alone would keep the
    // old palette's highlight on the new body colour -- visible, and exactly
    // the defect this case names.
    const std::string home = makeConfigHome(
        "frame-thickness=7\ntab-background=#c8cacc\n");
    WmFixture fixture(fixtureWithConfigHome(home));
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d, 260, 180, 320, 240, client,
                                          "IIIIIIIIIIIIIIIIIIII");
    REQUIRE(frame != None);
    settleWm(d);

    // The column, not the tab window: the bevel highlight runs down the
    // column's left edge and the shadow down its right, both inside this
    // rectangle and neither anywhere else on the frame.
    const Rect tabRect = tabColumnRect(rectOf(d, frame), rectOf(d, client), 7);
    REQUIRE(tabRect.w > 4);

    // The shade WindowManager::allocateShadeOf() produces from the SHIPPED
    // #C8CACC at +0.76 toward white, and the one it produces from #400000 at
    // the same fraction. Both MEASURED on this host and written down as
    // literals rather than recomputed here: doing the blend again in the test
    // would let the same arithmetic error pass on both sides of the
    // comparison, which is the whole point of naming an expected value.
    const unsigned long shippedHighlight = namedPixel(d, "#f2f3f3");
    const unsigned long darkHighlight    = namedPixel(d, "#d1c2c2");
    REQUIRE(shippedHighlight != ~0UL);
    REQUIRE(darkHighlight    != ~0UL);

    const Histogram before = captureRoot(d, tabRect);
    INFO("tab before: " << describeTop(before));
    REQUIRE(countOf(before, shippedHighlight) > 0);

    CtlResult r = ctl(fixture, {"set", "tab-background", "#400000"});
    const Histogram after = awaitPixelIn(d, tabRect, darkHighlight, 1);

    INFO("wm stderr:\n" << fixture.wmStderr());
    INFO(r.describe());
    INFO("tab after: " << describeTop(after, 12));

    CHECK(r.exitCode == 0);
    // The highlight now belongs to the NEW background...
    CHECK(countOf(after, darkHighlight) > 0);
    // ...and the shade derived from the old one is gone.
    CHECK(countOf(after, shippedHighlight) == 0);
}

TEST_CASE("every menu colour set over the socket reaches the next menu opened",
          "[wm_config_live]")
{
    const std::string home = makeConfigHome(
        "menu-background=#c8cacc\n"
        "menu-foreground=#000000\n"
        "menu-highlight=#a8acb0\n"
        "menu-borders=#ff00ff\n");
    WmFixture fixture(fixtureWithConfigHome(home));
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    XTestDriver driver(fixture.display());
    parkPointer(d);

    const unsigned long oldBg   = namedPixel(d, "#c8cacc");
    const unsigned long oldBord = namedPixel(d, "#ff00ff");
    const unsigned long newBg   = namedPixel(d, "#00cc00");
    const unsigned long newFg   = namedPixel(d, "#ff0000");
    const unsigned long newHl   = namedPixel(d, "#0000ff");
    const unsigned long newBord = namedPixel(d, "#00ffff");
    REQUIRE(oldBg != ~0UL);

    // --- before: the menu is drawn in the file's colours --------------------
    Window menu = None;
    Rect menuRect;
    REQUIRE(openRootMenu(d, driver, kMenuPressX, kMenuPressY, menu, menuRect, oldBg));
    const Rect borderRect{menuRect.x - 1, menuRect.y - 1, menuRect.w + 2, menuRect.h + 2};
    const Histogram before       = captureRoot(d, menuRect);
    const Histogram borderBefore = captureRoot(d, borderRect);
    closeRootMenu(d, driver);

    INFO("menu before: " << describeTop(before));
    REQUIRE(countOf(borderBefore, oldBord) > 0);

    // --- the four menu colours change on a running desktop ------------------
    CtlResult setBg   = ctl(fixture, {"set", "menu-background", "#00cc00"});
    CtlResult setFg   = ctl(fixture, {"set", "menu-foreground", "#ff0000"});
    CtlResult setHl   = ctl(fixture, {"set", "menu-highlight", "#0000ff"});
    CtlResult setBord = ctl(fixture, {"set", "menu-borders", "#00ffff"});

    // --- after: the NEXT menu is drawn in them ------------------------------
    // The pointer is moved onto the first row before the capture, so the
    // highlight band is actually painted; a menu with no row selected would
    // contain no highlight pixel at all and the assertion would be vacuous.
    Window menu2 = None;
    Rect menuRect2;
    const bool reopened =
        openRootMenu(d, driver, kMenuPressX, kMenuPressY, menu2, menuRect2, newBg);
    driver.moveTo(kMenuPressX, menuRect2.y + 14);
    Histogram after;
    WmFixture::pollUntil([&] {
        after = captureRoot(d, menuRect2);
        return countOf(after, newHl) > 0 && countOf(after, newFg) > 0;
    }, 8000);
    const Rect borderRect2{menuRect2.x - 1, menuRect2.y - 1, menuRect2.w + 2, menuRect2.h + 2};
    const Histogram borderAfter = captureRoot(d, borderRect2);
    closeRootMenu(d, driver);

    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);
    INFO("menu-background: " << setBg.describe());
    INFO("menu-foreground: " << setFg.describe());
    INFO("menu-highlight:  " << setHl.describe());
    INFO("menu-borders:    " << setBord.describe());
    INFO("menu after: " << describeTop(after));
    INFO("border before " << describeTop(borderBefore) << " after " << describeTop(borderAfter));

    CHECK(setBg.exitCode == 0);
    CHECK(setFg.exitCode == 0);
    CHECK(setHl.exitCode == 0);
    CHECK(setBord.exitCode == 0);

    CHECK(reopened);
    CHECK(dominantPixel(after) == newBg);
    CHECK(dominantShare(after) >= kDominanceFloor);
    CHECK(countOf(before, newBg) == 0);
    CHECK(countOf(after, newFg) > 0);
    CHECK(countOf(after, newHl) > 0);
    CHECK(countOf(borderAfter, newBord) > 0);
    CHECK(countOf(borderAfter, oldBord) == 0);
}

TEST_CASE("a colour the X server cannot parse is refused and the screen is unchanged",
          "[wm_config_live]")
{
    // T-9-26. The prohibition this plan carries: a failed resolution must never
    // leave the window manager without a usable colour, which it cannot avoid
    // if the old value was released before the new one was known to succeed.
    const std::string home = makeConfigHome(
        "frame-thickness=7\ntab-background=#c8cacc\n");
    WmFixture fixture(fixtureWithConfigHome(home));
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d, 260, 180, 320, 240, client, "refusal");
    REQUIRE(frame != None);
    settleWm(d);
    const Rect tabRect = tabColumnRect(rectOf(d, frame), rectOf(d, client), 7);
    REQUIRE(tabRect.w > 4);

    const Histogram before = captureRoot(d, tabRect);

    CtlResult bad     = ctl(fixture, {"set", "tab-background", "#nonsense"});
    CtlResult badMenu = ctl(fixture, {"set", "menu-background", "not a colour at all"});
    settleWm(d);
    const Histogram after = captureRoot(d, tabRect);

    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);
    INFO("bad tab-background: " << bad.describe());
    INFO("bad menu-background: " << badMenu.describe());
    INFO("tab before " << describeTop(before) << " after " << describeTop(after));

    CHECK(bad.exitCode == 1);
    CHECK(bad.err.find("tab-background") != std::string::npos);
    CHECK(badMenu.exitCode == 1);
    CHECK(badMenu.err.find("menu-background") != std::string::npos);

    // Nothing moved and nothing was repainted: the window keeps the colour it
    // had, byte for byte.
    CHECK(after == before);
    CHECK(ctlGet(fixture, "tab-background") == "#c8cacc");

    // And the window manager is still framing windows -- a colour path that
    // freed before it allocated would have left it without one.
    Window late = None;
    Window lateFrame = mapClientAndAwaitFrame(d, 600, 400, 200, 160, late, "after-refusal");
    CHECK(lateFrame != None);
    CHECK(fixture.wmAlive());
}

TEST_CASE("setting the same colour twice leaves the capture byte-identical",
          "[wm_config_live]")
{
    // The idempotency guarantee, asserted for a colour rather than for a
    // geometry. There is no counter and no stderr line to read from outside, so
    // the observable is the SCREEN: identical output after a second application
    // is what "no second reallocation" looks like from here.
    const std::string home = makeConfigHome(
        "frame-thickness=7\ntab-background=#c8cacc\n");
    WmFixture fixture(fixtureWithConfigHome(home));
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d, 260, 180, 320, 240, client, "repeat");
    REQUIRE(frame != None);
    settleWm(d);
    const Rect tabRect = tabColumnRect(rectOf(d, frame), rectOf(d, client), 7);
    REQUIRE(tabRect.w > 4);

    const unsigned long wanted = namedPixel(d, "#ff0000");
    REQUIRE(wanted != ~0UL);

    CtlResult first = ctl(fixture, {"set", "tab-background", "#ff0000"});
    const Histogram afterFirst = awaitPixelIn(d, tabRect, wanted, 100);
    settleWm(d);
    const Histogram settled = captureRoot(d, tabRect);

    CtlResult second = ctl(fixture, {"set", "tab-background", "#ff0000"});
    settleWm(d);
    const Histogram afterSecond = captureRoot(d, tabRect);

    INFO("wm stderr:\n" << fixture.wmStderr());
    INFO("first: "  << first.describe());
    INFO("second: " << second.describe());
    INFO("after first " << describeTop(settled) << " after second " << describeTop(afterSecond));

    CHECK(first.exitCode == 0);
    CHECK(second.exitCode == 0);
    CHECK(dominantPixel(afterFirst) == wanted);
    CHECK(afterSecond == settled);
}
