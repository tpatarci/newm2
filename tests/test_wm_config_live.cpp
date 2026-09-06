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
