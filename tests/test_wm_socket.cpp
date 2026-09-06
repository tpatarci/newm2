// The configuration socket against the REAL compiled wm2-born-again binary
// (CGUI-02, plan 09-03, TEST-05).
//
// The claim this file exists to check, and the one the whole plan turns on:
// the window manager answers on its socket while it is IDLE and while a MODAL
// GRAB IS HELD. Those are two different poll sites in src/Events.cpp -- they
// were two separate stack-local descriptor arrays before this plan -- and a
// change that reaches only the first produces a window manager that services
// the GUI until the user opens the root menu and then appears to freeze
// (09-RESEARCH.md Pitfall 1). The held-grab case below is the one that goes red
// when that happens, and nothing else in the suite does.
//
// Tags are registered as ctest LABELS via ADD_TAGS_AS_LABELS (D-33), so this
// group is selectable with `ctest -L '^wm_socket$' --no-tests=error`.
//
// The rules this file inherits from 08-04 .. 08.5-13, none of them optional:
//
//   OUTCOMES ARE OBSERVED THROUGH THE SERVER OR THROUGH THE SOCKET. Never a
//   claim about window-manager internals.
//
//   NO sleep()-BASED SYNCHRONISATION. Every wait is a deadline-bounded poll
//   whose exit condition is a real observation.
//
//   EVERY PROCESS IS TERMINATED BY A PID THE FIXTURE CREATED. This file opens
//   sockets and would find a name pattern convenient; it uses none.
//
//   THE WINDOW MANAGER'S STDERR IS BOUND TO A LOCAL AND EMITTED AS CONTEXT
//   BEFORE any assertion that may fail, never after (the guard-ordering defect
//   recorded in 08.5-06). A CHECK that fails takes the case with it, and an
//   INFO written afterwards is never reached.

#include <catch2/catch_test_macros.hpp>

#include "support/WmFixture.h"
#include "support/XTestDriver.h"

#include "ConfigProtocol.h"
#include "SocketServer.h"

#include "x11wrap.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>
#include <string>
#include <vector>

using namespace wm2test;

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kParkX = 900;
constexpr int kParkY = 700;

// ---------------------------------------------------------------------------
// X protocol errors are a RACE here, not a failure (08.5-13's QuietXErrors)
// ---------------------------------------------------------------------------
//
// Every helper below walks the root window's children and asks the server for
// each one's geometry, and the window manager creates and destroys windows the
// whole time -- its own popups, this file's nudge windows, the fixture's
// readiness probe. A child listed by XQueryTree can be gone by the time
// XGetGeometry names it, and Xlib's DEFAULT handler answers that by killing the
// test process outright, with no assertion output at all: an unexplained
// failure rather than a visible race.
//
// MEASURED here: 2 of 7 cases died this way on the first run of this file, both
// BadDrawable on X_GetGeometry, in the two cases that poll for the menu without
// pumping between reads.
//
// The helpers already handle it correctly -- XGetGeometry returns 0 and they
// return false -- so this only lets them reach that code. Errors are COUNTED
// rather than merely swallowed, so a helper erroring systematically is still
// visible instead of silently returning nothing.
//
// XSetErrorHandler is global to Xlib rather than per-connection, so it is
// installed once at static initialisation, before Catch2 runs.
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

// Every socket exchange in this file is bounded by this. Generous enough for a
// window manager running under a sanitizer with the whole suite in flight,
// short enough that a wedged poll site fails the case rather than the ctest
// timeout.
constexpr int kSocketDeadlineMs = 15000;

void pollSleep() { std::this_thread::sleep_for(std::chrono::milliseconds(20)); }

void holdFor(int ms)
{
    const auto until = Clock::now() + std::chrono::milliseconds(ms);
    while (Clock::now() < until) pollSleep();
}

// ---------------------------------------------------------------------------
// Server observation
// ---------------------------------------------------------------------------

struct Rect { int x = 0, y = 0, w = 0, h = 0; };

// Make the window manager's event loop run. Creating and destroying an
// override-redirect window is a request it must handle, and handling it is what
// flushes whatever it was holding.
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

void settleWm(Display* d)
{
    for (int i = 0; i < 15; ++i) { pumpWm(d); pollSleep(); }
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

bool isViewable(Display* d, Window w)
{
    XWindowAttributes attr;
    if (!XGetWindowAttributes(d, w, &attr)) return false;
    return attr.map_state == IsViewable;
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

Window parentOf(Display* d, Window w)
{
    Window wroot = None, parent = None, *children = nullptr;
    unsigned int n = 0;
    if (!XQueryTree(d, w, &wroot, &parent, &children, &n)) return None;
    if (children) XFree(children);
    return parent;
}

Window activeWindow(Display* d)
{
    static Atom atom = None;
    if (atom == None) atom = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
    Window w = None;
    if (!readWindowProp(d, DefaultRootWindow(d), atom, w)) return None;
    return w;
}

void parkPointer(Display* d)
{
    XWarpPointer(d, None, DefaultRootWindow(d), 0, 0, 0, 0, kParkX, kParkY);
    XSync(d, False);
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

// Announce WM_DELETE_WINDOW BEFORE the map: Client::getProtocols() runs inside
// Client::manage(), so a protocol announced afterwards is never seen and
// Client::kill() falls through to XKillClient -- which would close this test's
// own connection.
void announceDeleteProtocol(Display* d, Window win)
{
    Atom del = XInternAtom(d, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(d, win, &del, 1);
    XSync(d, False);
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

// The tab's small square button, separated from the long tab strip by SIZE
// rather than by origin (the distinction tests/test_wm_runtime.cpp arrived at).
Window findTabButton(Display* d, Window frame, Window client)
{
    Window button = None;
    for (Window child : childrenOf(d, frame)) {
        if (child == client) continue;
        Rect r;
        if (!localRect(d, child, r)) continue;
        if (r.w <= 0 || r.h <= 0) continue;
        if (r.w == r.h && r.w <= 64) button = child;
    }
    return button;
}

// The root menu is identified by the press point it is anchored on, and by
// nothing else. WindowManager::menu() places it at (pressX - width/2,
// pressY - 2), so the press point lies inside it; the category submenu is
// placed to the SIDE and does not contain it. Returning None rather than any
// fallback is what makes the enclosing poll actually poll (08.5-13).
Window findOpenMenu(Display* d, int pressX, int pressY)
{
    for (Window child : childrenOf(d, DefaultRootWindow(d))) {
        Rect r;
        if (!serverRect(d, child, r)) continue;
        if (r.w <= 1 || r.h <= 1) continue;
        if (!isViewable(d, child)) continue;
        if (pressX >= r.x && pressX < r.x + r.w &&
            pressY >= r.y && pressY < r.y + r.h) {
            return child;
        }
    }
    return None;
}

// Chosen so WindowManager::menu() does not need to clamp the menu to a screen
// edge: a clamp warps the pointer, the warp is a MotionNotify, and a
// MotionNotify over a category row opens a submenu nobody asked for.
constexpr int kMenuPressX = 300;
constexpr int kMenuPressY = 5;

// ---------------------------------------------------------------------------
// The socket
// ---------------------------------------------------------------------------

// The path the window manager published on the root window (DISC-03). Empty
// when the property is absent, which is itself the answer to "is there a
// socket?" -- a client never has to connect to find out.
std::string publishedSocketPath(Display* d)
{
    static Atom prop = None;
    if (prop == None) prop = XInternAtom(d, "_WM2_CONFIG_SOCKET", False);

    Atom actualType = None;
    int actualFormat = 0;
    unsigned long nItems = 0, bytesAfter = 0;
    unsigned char* raw = nullptr;

    if (XGetWindowProperty(d, DefaultRootWindow(d), prop, 0, 256, False, XA_STRING,
                           &actualType, &actualFormat, &nItems, &bytesAfter,
                           &raw) != Success) {
        return std::string();
    }
    std::string out;
    if (raw && actualType == XA_STRING && actualFormat == 8) {
        out.assign(reinterpret_cast<char*>(raw), nItems);
    }
    if (raw) XFree(raw);
    return out;
}

std::string awaitPublishedSocketPath(Display* d, int timeoutMs = 10000)
{
    std::string path;
    WmFixture::pollUntil([&] {
        pumpWm(d);
        path = publishedSocketPath(d);
        return !path.empty();
    }, timeoutMs);
    return path;
}

// One client connection. Owns its descriptor and closes it, so nothing here
// ever reaches for a name pattern or a stray kill.
class Conn {
public:
    explicit Conn(const std::string& path)
    {
        m_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (m_fd < 0) return;

        struct sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        if (path.size() + 1 > sizeof(addr.sun_path)) { closeFd(); return; }
        std::memcpy(addr.sun_path, path.c_str(), path.size());

        if (::connect(m_fd, reinterpret_cast<struct sockaddr*>(&addr),
                      sizeof(addr)) != 0) {
            m_error = errno;
            closeFd();
        }
    }
    ~Conn() { closeFd(); }
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    bool connected() const { return m_fd >= 0; }
    int  error() const { return m_error; }
    int  fd() const { return m_fd; }

    bool sendRaw(const std::string& bytes)
    {
        std::size_t sent = 0;
        const auto until = Clock::now() + std::chrono::milliseconds(kSocketDeadlineMs);
        while (sent < bytes.size()) {
            const ssize_t n = ::send(m_fd, bytes.data() + sent, bytes.size() - sent,
                                     MSG_NOSIGNAL);
            if (n > 0) { sent += static_cast<std::size_t>(n); continue; }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                if (Clock::now() >= until) return false;
                pollSleep();
                continue;
            }
            return false;
        }
        return true;
    }

    bool send(const ConfigMessage& m) { return sendRaw(configProtocolEncode(m)); }

    // One complete line, or false on a bounded expiry / a closed peer. Never a
    // blocking read: the descriptor is polled first and every wait has a
    // deadline.
    bool readLine(std::string& out, int timeoutMs = kSocketDeadlineMs)
    {
        const auto until = Clock::now() + std::chrono::milliseconds(timeoutMs);
        for (;;) {
            const std::size_t nl = m_in.find('\n');
            if (nl != std::string::npos) {
                out = m_in.substr(0, nl + 1);
                m_in.erase(0, nl + 1);
                return true;
            }
            const auto left = until - Clock::now();
            if (left <= std::chrono::steady_clock::duration::zero()) return false;

            struct pollfd p;
            p.fd = m_fd;
            p.events = POLLIN;
            p.revents = 0;
            const int ms = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(left).count());
            const int r = ::poll(&p, 1, ms > 0 ? ms : 1);
            if (r < 0) { if (errno == EINTR) continue; return false; }
            if (r == 0) return false;

            char buf[4096];
            const ssize_t n = ::recv(m_fd, buf, sizeof(buf), 0);
            if (n == 0) { m_closed = true; return false; }
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
                return false;
            }
            m_in.append(buf, static_cast<std::size_t>(n));
        }
    }

    bool receive(ConfigMessage& m, ConfigDecodeResult& result,
                 int timeoutMs = kSocketDeadlineMs)
    {
        std::string line;
        if (!readLine(line, timeoutMs)) return false;
        result = configProtocolDecode(line, m);
        return true;
    }

    // Did the window manager close this connection? Proven by a read that
    // reaches end-of-file, never by a timeout.
    bool awaitClosed(int timeoutMs = kSocketDeadlineMs)
    {
        std::string ignored;
        const auto until = Clock::now() + std::chrono::milliseconds(timeoutMs);
        while (Clock::now() < until) {
            if (m_closed) return true;
            if (readLine(ignored, 200)) continue;   // a reply first is fine
            if (m_closed) return true;
        }
        return m_closed;
    }

    bool closedByPeer() const { return m_closed; }

private:
    void closeFd() { if (m_fd >= 0) ::close(m_fd); m_fd = -1; }

    int m_fd = -1;
    int m_error = 0;
    bool m_closed = false;
    std::string m_in;
};

// The handshake, as D-15 requires it: program name and protocol version, both
// ways, before anything else.
bool shakeHands(Conn& c, std::string& program, int& protocol)
{
    ConfigMessage hello;
    hello.type = ConfigMessageType::Hello;
    hello.program = "test_wm_socket";
    hello.protocol = kConfigProtocolVersion;
    if (!c.send(hello)) return false;

    ConfigMessage ack;
    ConfigDecodeResult r = ConfigDecodeResult::Malformed;
    if (!c.receive(ack, r)) return false;
    if (r != ConfigDecodeResult::Ok) return false;
    if (ack.type != ConfigMessageType::HelloAck) return false;

    program = ack.program;
    protocol = ack.protocol;
    return true;
}

bool requestStatus(Conn& c, ConfigMessage& reply, std::string& rawLine,
                   int timeoutMs = kSocketDeadlineMs)
{
    ConfigMessage status;
    status.type = ConfigMessageType::Status;
    if (!c.send(status)) return false;
    if (!c.readLine(rawLine, timeoutMs)) return false;
    return configProtocolDecode(rawLine, reply) == ConfigDecodeResult::Ok;
}

std::string fieldValue(const ConfigMessage& m, const std::string& name)
{
    for (const auto& kv : m.fields) if (kv.first == name) return kv.second;
    return std::string();
}

bool hasField(const ConfigMessage& m, const std::string& name)
{
    for (const auto& kv : m.fields) if (kv.first == name) return true;
    return false;
}

}  // namespace


// -----------------------------------------------------------------------------
// Discovery and the idle round trip
// -----------------------------------------------------------------------------

TEST_CASE("The socket path is published on the root window and is a socket",
          "[wm_socket]")
{
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();

    const std::string path = awaitPublishedSocketPath(d);
    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);
    INFO("published path: '" << path << "'");

    // DISC-03: a client DISCOVERS the path rather than reconstructing it, so a
    // window manager started with an unusual XDG_RUNTIME_DIR is still findable.
    REQUIRE_FALSE(path.empty());

    struct stat st;
    REQUIRE(::lstat(path.c_str(), &st) == 0);
    CHECK(S_ISSOCK(st.st_mode));

    // And the path really is the one this build would compute for this display,
    // so the property and DISC-02 agree rather than merely both existing.
    CHECK(path == configSocketPath(fixture.display().c_str()));
}

TEST_CASE("An idle window manager answers hello and status", "[wm_socket]")
{
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();

    const std::string path = awaitPublishedSocketPath(d);
    {
        const std::string stderrText = fixture.wmStderr();
        INFO("wm stderr:\n" << stderrText);
        REQUIRE_FALSE(path.empty());
    }

    Conn c(path);
    {
        const std::string stderrText = fixture.wmStderr();
        INFO("wm stderr:\n" << stderrText);
        INFO("connect errno: " << c.error());
        REQUIRE(c.connected());
    }

    std::string program;
    int protocol = 0;
    {
        const std::string stderrText = fixture.wmStderr();
        INFO("wm stderr:\n" << stderrText);
        REQUIRE(shakeHands(c, program, protocol));
    }
    CHECK(program == "wm2-born-again");
    CHECK(protocol == kConfigProtocolVersion);

    ConfigMessage reply;
    std::string raw;
    {
        const std::string stderrText = fixture.wmStderr();
        INFO("wm stderr:\n" << stderrText);
        REQUIRE(requestStatus(c, reply, raw));
    }

    INFO("status reply: " << raw);
    CHECK(reply.type == ConfigMessageType::StatusReply);

    // D-14's SEVEN FIELDS, all of them present and none of them anything else.
    CHECK(reply.fields.size() == 7);
    CHECK(hasField(reply, "version"));
    CHECK(hasField(reply, "protocol"));
    CHECK(hasField(reply, "uptime"));
    CHECK(hasField(reply, "screen-width"));
    CHECK(hasField(reply, "screen-height"));
    CHECK(hasField(reply, "managed"));
    CHECK(hasField(reply, "hidden"));

    CHECK_FALSE(fieldValue(reply, "version").empty());
    CHECK(fieldValue(reply, "protocol") == std::to_string(kConfigProtocolVersion));

    // The geometry is the server's own, so a wrong answer is detectable rather
    // than merely plausible.
    CHECK(fieldValue(reply, "screen-width") ==
          std::to_string(DisplayWidth(d, DefaultScreen(d))));
    CHECK(fieldValue(reply, "screen-height") ==
          std::to_string(DisplayHeight(d, DefaultScreen(d))));

    CHECK(fixture.wmAlive());
}


// -----------------------------------------------------------------------------
// THE CASE THIS PLAN EXISTS FOR
// -----------------------------------------------------------------------------

TEST_CASE("The socket answers while a modal pointer grab is held", "[wm_socket]")
{
    // With the root menu open, WindowManager::menu() is inside a nested loop
    // with its own pointer grab and the main loop is not running at all. Every
    // modal interaction in this codebase -- the menu, move, resize, the
    // tab-button hold, the gesture recogniser -- waits in modalWait(), which is
    // the SECOND of the two poll sites. If the socket reached only nextEvent(),
    // this round trip expires and everything else in the file still passes.
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    const std::string path = awaitPublishedSocketPath(d);
    {
        const std::string stderrText = fixture.wmStderr();
        INFO("wm stderr:\n" << stderrText);
        REQUIRE_FALSE(path.empty());
    }

    XTestDriver driver(fixture.display());
    driver.moveTo(kMenuPressX, kMenuPressY);
    driver.press(Button1);

    Window menu = None;
    const bool opened = WmFixture::pollUntil([&] {
        menu = findOpenMenu(d, kMenuPressX, kMenuPressY);
        return menu != None;
    }, 20000);

    if (!opened) {
        driver.release(Button1);
        const std::string stderrText = fixture.wmStderr();
        INFO("wm stderr:\n" << stderrText);
        FAIL("the root menu never mapped, so nothing held a grab");
    }

    // THE ROUND TRIP, with the button still down and the grab still held.
    Conn c(path);
    std::string program;
    int protocol = 0;
    ConfigMessage reply;
    std::string raw;

    const bool connected = c.connected();
    const bool shook     = connected && shakeHands(c, program, protocol);
    const bool answered  = shook && requestStatus(c, reply, raw);

    // The grab is released BEFORE any assertion. A failed CHECK aborts the
    // case, and a case that aborted with Button1 still down would leave the
    // fixture's window manager holding a pointer grab for its whole teardown.
    driver.release(Button1);
    XSync(d, False);

    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);
    INFO("status reply through the grab: " << raw);

    CHECK(connected);
    CHECK(shook);
    CHECK(program == "wm2-born-again");
    CHECK(answered);
    CHECK(reply.type == ConfigMessageType::StatusReply);
    CHECK(reply.fields.size() == 7);

    // Still a window manager afterwards: the menu closes on the release and the
    // loop carries on.
    settleWm(d);
    CHECK(fixture.wmAlive());
}


// -----------------------------------------------------------------------------
// modalWait()'s own contract, unchanged
// -----------------------------------------------------------------------------

TEST_CASE("modalWait still returns an event when a matching event arrives",
          "[wm_socket][modalwait]")
{
    // Event: the root menu's loop waits in modalWait(MenuMask, ..., -1) and acts
    // on what it returns. A press that opens the menu and a release that closes
    // it is that outcome, observed from outside as the menu unmapping.
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    XTestDriver driver(fixture.display());
    driver.moveTo(kMenuPressX, kMenuPressY);
    driver.press(Button1);

    Window menu = None;
    const bool opened = WmFixture::pollUntil([&] {
        menu = findOpenMenu(d, kMenuPressX, kMenuPressY);
        return menu != None;
    }, 20000);

    driver.release(Button1);
    XSync(d, False);

    const bool closed = opened && WmFixture::pollUntil([&] {
        pumpWm(d);
        return !isViewable(d, menu);
    }, 20000);

    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);
    CHECK(opened);
    CHECK(closed);
    CHECK(fixture.wmAlive());
}

TEST_CASE("modalWait still reports an interruption while a grab is held",
          "[wm_socket][modalwait]")
{
    // Interrupted: WINDOWS.md ledger 8's guarantee. SIGTERM delivered while a
    // button is held must be honoured immediately, not when the button comes
    // up -- and it reaches the grab through the self-pipe at the SAME poll site
    // this plan changed.
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    XTestDriver driver(fixture.display());
    driver.moveTo(kMenuPressX, kMenuPressY);
    driver.press(Button1);

    const bool opened = WmFixture::pollUntil([&] {
        return findOpenMenu(d, kMenuPressX, kMenuPressY) != None;
    }, 20000);

    // The button stays DOWN across the signal. That is the whole point.
    const bool exited = opened && fixture.terminateWmCleanly();
    driver.release(Button1);

    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);
    CHECK(opened);
    CHECK(exited);
}

TEST_CASE("modalWait still times out, so a held tab button still reaches the delay",
          "[wm_socket][modalwait]")
{
    // Timeout: the ONLY externally observable consumer of it. Border's
    // tab-button loop starts at "hide" and becomes "delete" once accumulated
    // press time passes destroy-window-delay -- and that time is accumulated
    // ENTIRELY by counting modalWait() timeouts. A build whose bounded wait
    // stopped timing out would hide on a long press instead of deleting.
    constexpr int kDelayMs = 250;

    WmFixtureOptions options;
    options.wmArgs = { "--destroy-window-delay=" + std::to_string(kDelayMs) };
    WmFixture fixture(options);

    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    Window win = None;
    Window frame = None;
    {
        win = createClient(d, 200, 150, 260, 190, "longpress");
        announceDeleteProtocol(d, win);
        XMapWindow(d, win);
        XSync(d, False);
        frame = awaitFrameFor(d, win);
        const std::string stderrText = fixture.wmStderr();
        INFO("wm stderr:\n" << stderrText);
        REQUIRE(frame != None);
    }
    settleWm(d);

    // The button is only REACHABLE on an ACTIVE client: an inactive client's
    // frame has the button's square subtracted from its bounding shape, so a
    // press aimed at it falls through to the root and opens the menu instead.
    const Rect clientRect = rectOf(d, win);
    driver.moveTo(clientRect.x + clientRect.w / 2, clientRect.y + clientRect.h / 2);
    XSync(d, False);
    REQUIRE(WmFixture::pollUntil([&] {
        pumpWm(d);
        return activeWindow(d) == win;
    }, 8000));

    Window button = findTabButton(d, frame, win);
    {
        const std::string stderrText = fixture.wmStderr();
        INFO("wm stderr:\n" << stderrText);
        REQUIRE(button != None);
    }
    const Rect b = rectOf(d, button);
    REQUIRE(b.w > 0);

    driver.moveTo(b.x + b.w / 2, b.y + b.h / 2);
    XSync(d, False);
    driver.press(Button1);
    holdFor(kDelayMs * 3);
    driver.release(Button1);
    XSync(d, False);

    static Atom protocols = XInternAtom(d, "WM_PROTOCOLS", False);
    static Atom del = XInternAtom(d, "WM_DELETE_WINDOW", False);
    const bool deleted = WmFixture::pollUntil([&] {
        pumpWm(d);
        XEvent ev;
        while (XCheckTypedWindowEvent(d, win, ClientMessage, &ev)) {
            if (ev.xclient.message_type == protocols &&
                static_cast<Atom>(ev.xclient.data.l[0]) == del) {
                return true;
            }
        }
        return false;
    }, 15000);

    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);
    CHECK(deleted);
    CHECK(fixture.wmAlive());
}
