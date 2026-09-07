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
// The shape extension is here for ONE reason: the overlapping-sets case at the
// end of this file counts re-frames, and 09-04 established that ShapeNotify is
// the only counter that sees the WORK -- the server suppresses ConfigureNotify
// for a reconfigure that changes nothing.
#include <X11/extensions/shape.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
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

TEST_CASE("The published socket path is withdrawn when the window manager exits",
          "[wm_socket]")
{
    // Codex pass 3, P2. A root-window property OUTLIVES the process that set
    // it: the X server keeps it until somebody deletes it or the server itself
    // goes away. close() unlinks the socket node on the way out, so a window
    // manager that published a path and then exited cleanly used to leave the
    // property naming a path that no longer exists -- and a discovery client
    // reading it connects to nothing, with no way to tell the difference
    // between "no window manager" and "a window manager that has moved on".
    //
    // The X server here is the fixture's own and outlives the window manager by
    // construction: WmFixture's destructor reaps the WM child first and only
    // then shuts the server down. So the root window is still there to be asked
    // after the window manager is gone, which is exactly the state a later
    // client -- or a later window manager -- would find it in.
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

    // SIGTERM to the PID THIS FIXTURE CREATED, and a normal exit demanded: the
    // deletion lives in release(), which a signal death never reaches.
    const bool exited = fixture.terminateWmCleanly();

    // Bounded observation rather than a single read: the property disappears
    // when the server has processed the departed connection's last requests,
    // which is an event to wait for and not an elapsed time.
    std::string remaining = "unread";
    const bool withdrawn = WmFixture::pollUntil([&] {
        remaining = publishedSocketPath(d);
        return remaining.empty();
    }, 8000);

    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);
    INFO("property after exit: '" << remaining << "'");
    CHECK(exited);

    // THE POINT: the property and the socket node are withdrawn together, so
    // the property's presence keeps meaning what DISC-03 says it means -- there
    // is a socket at this path, right now.
    CHECK(withdrawn);

    struct stat st;
    CHECK(::lstat(path.c_str(), &st) != 0);
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


// -----------------------------------------------------------------------------
// The boundary (D-16, T-9-11 / T-9-12 / T-9-14 / T-9-16 / T-9-17 / T-9-18)
// -----------------------------------------------------------------------------
//
// PRECONDITION, evaluated rather than assumed: these cases read
// XDG_RUNTIME_DIR from the environment they inherit. On this host it is set to
// an absolute path, so the primary branch of DISC-02 is the one under test. The
// fallback branch is exercised by [config_socket]'s path cases, which drive the
// resolver directly and do not need a window manager.

namespace {

std::string directoryOf(const std::string& path)
{
    const std::size_t slash = path.rfind('/');
    return (slash == std::string::npos) ? std::string(".") : path.substr(0, slash);
}

// How many times a line containing `needle` appears in `text`. Counting LINES
// rather than occurrences, because the claim is about log entries.
int countLines(const std::string& text, const std::string& needle)
{
    int n = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t eol = text.find('\n', pos);
        const std::string line = text.substr(pos, (eol == std::string::npos)
                                                  ? std::string::npos : eol - pos);
        if (!line.empty() && line.find(needle) != std::string::npos) ++n;
        if (eol == std::string::npos) break;
        pos = eol + 1;
    }
    return n;
}

// Prove the window manager is still doing its job. Every refusal below ends
// with this: a boundary that protects by crashing is not a mitigation.
bool stillFraming(WmFixture& fixture, Display* d, const char* name)
{
    Window win = None;
    const Window frame = mapClientAndAwaitFrame(d, 260, 200, 200, 150, win, name);
    if (frame == None) return false;
    XDestroyWindow(d, win);
    XSync(d, False);
    return fixture.wmAlive();
}

}  // namespace


TEST_CASE("The socket directory is 0700 and the socket is 0600", "[wm_socket]")
{
    // D-16's FILESYSTEM half, read from the filesystem rather than from what
    // the code says it asked for. It is belt to the peer check's braces: both
    // are required, and neither is asserted by the other's case.
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

    struct stat sockStat;
    struct stat dirStat;
    const std::string dir = directoryOf(path);
    REQUIRE(::lstat(path.c_str(), &sockStat) == 0);
    REQUIRE(::stat(dir.c_str(), &dirStat) == 0);

    INFO("socket " << path << " mode " << std::oct << (sockStat.st_mode & 07777));
    INFO("directory " << dir << " mode " << std::oct << (dirStat.st_mode & 07777));

    CHECK(S_ISSOCK(sockStat.st_mode));
    CHECK((sockStat.st_mode & 07777) == 0600);

    CHECK(S_ISDIR(dirStat.st_mode));
    CHECK((dirStat.st_mode & 07777) == 0700);

    // Owned by the user running the window manager, not merely restrictive:
    // a 0700 directory belonging to someone else protects the wrong person.
    CHECK(sockStat.st_uid == ::geteuid());
    CHECK(dirStat.st_uid == ::geteuid());
}

TEST_CASE("A refused peer is closed before a protocol byte and logged once per uid",
          "[wm_socket]")
{
    // The refusal path, driven end-to-end against the real binary through the
    // strictly-NARROWING internal test lever (the WM2_FORCE_NO_SHAPE
    // precedent). It can only ever REFUSE a connection this build would admit;
    // there is no value of it that admits one the uid comparison would reject,
    // so it cannot widen D-16. The uid comparison ITSELF is asserted directly
    // by [config_socket]'s peer cases over a locally created pair, including
    // the root direction (T-9-12).
    WmFixtureOptions options;
    options.childEnv["WM2_SOCKET_FORCE_FOREIGN"] = "1";
    WmFixture fixture(options);

    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();

    const std::string path = awaitPublishedSocketPath(d);
    {
        const std::string stderrText = fixture.wmStderr();
        INFO("wm stderr:\n" << stderrText);
        REQUIRE_FALSE(path.empty());   // it listens; it just admits nobody
    }

    const std::string uidNeedle =
        "refused configuration socket connection from uid " +
        std::to_string(static_cast<unsigned long>(::geteuid()));

    // FIRST attempt: closed with no reply at all. A hello is sent and nothing
    // ever comes back, because the connection was closed before any byte of it
    // was read.
    bool firstClosed = false;
    {
        Conn c(path);
        if (c.connected()) {
            ConfigMessage hello;
            hello.type = ConfigMessageType::Hello;
            hello.program = "test_wm_socket";
            hello.protocol = kConfigProtocolVersion;
            c.send(hello);
            std::string line;
            const bool got = c.readLine(line, 4000);
            firstClosed = !got;   // expiry or EOF; either way no reply
        }
    }

    // The warning has to have been written before it can be counted.
    WmFixture::pollUntil([&] {
        pumpWm(d);
        return countLines(fixture.wmStderr(), uidNeedle) >= 1;
    }, 8000);
    const int afterFirst = countLines(fixture.wmStderr(), uidNeedle);

    // SECOND attempt from the same uid: refused just as firmly, and NOT logged
    // again. One warning per uid, deliberately not one per attempt, so the log
    // records the event without being floodable (T-9-19).
    {
        Conn c(path);
        if (c.connected()) {
            std::string line;
            c.readLine(line, 2000);
        }
    }
    settleWm(d);
    const int afterSecond = countLines(fixture.wmStderr(), uidNeedle);

    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);
    INFO("warnings after first attempt: " << afterFirst
         << ", after second: " << afterSecond);

    CHECK(firstClosed);
    CHECK(afterFirst == 1);
    CHECK(afterSecond == 1);

    // And a window manager that refuses everybody is still a window manager.
    CHECK(stillFraming(fixture, d, "after-refusal"));
}

TEST_CASE("A line over the protocol bound is refused and the connection closed",
          "[wm_socket]")
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
    REQUIRE(c.connected());

    std::string program;
    int protocol = 0;
    REQUIRE(shakeHands(c, program, protocol));

    // ONE BYTE OVER, counting the newline: kConfigProtocolMaxLine is the bound
    // INCLUDING the terminator, so this is the smallest line the transport must
    // refuse.
    const std::string oversized(kConfigProtocolMaxLine, 'a');
    REQUIRE(c.sendRaw(oversized + "\n"));

    ConfigMessage reply;
    ConfigDecodeResult result = ConfigDecodeResult::Malformed;
    const bool answered = c.receive(reply, result, 8000);
    const bool closed = c.awaitClosed(8000);

    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);

    CHECK(answered);
    CHECK(result == ConfigDecodeResult::Ok);
    CHECK(reply.type == ConfigMessageType::Error);
    CHECK(closed);

    // The window manager did not notice in any way a user would.
    CHECK(stillFraming(fixture, d, "after-oversized"));
}

TEST_CASE("A client that sends bytes and never a newline is dropped at the bound",
          "[wm_socket]")
{
    // The memory question, asked behaviourally: a peer that never frames a
    // message must not be able to make the window manager hold its bytes
    // (T-9-14). Sent in chunks rather than one write, so the refusal has to
    // come from the ACCUMULATED buffer rather than from one oversized recv.
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
    REQUIRE(c.connected());

    std::string program;
    int protocol = 0;
    REQUIRE(shakeHands(c, program, protocol));

    const std::string chunk(512, 'x');   // no newline, ever
    bool dropped = false;
    for (int i = 0; i < 64 && !dropped; ++i) {
        if (!c.sendRaw(chunk)) { dropped = true; break; }
        std::string ignored;
        if (c.readLine(ignored, 200)) {
            // The refusal reply. The close follows it.
            dropped = c.awaitClosed(8000);
        }
        if (c.closedByPeer()) dropped = true;
    }
    if (!dropped) dropped = c.awaitClosed(8000);

    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);
    CHECK(dropped);
    CHECK(stillFraming(fixture, d, "after-nonewline"));
}

TEST_CASE("A socket path too long for the address structure is named, not truncated",
          "[wm_socket]")
{
    // T-9-18. sun_path is 108 bytes; a deep XDG_RUNTIME_DIR overflows it, and
    // bind() answers that by truncating rather than by complaining -- which
    // would bind somewhere else entirely (RESEARCH Pitfall 4).
    //
    // The path is refused BEFORE anything is created, so nothing exists to
    // clean up and the deep directory is never made.
    WmFixtureOptions options;
    options.childEnv["XDG_RUNTIME_DIR"] = "/" + std::string(200, 'x');
    WmFixture fixture(options);

    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    settleWm(d);

    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);

    // A NAMED warning, not a silent degradation.
    CHECK(countLines(stderrText, "configuration socket path is too long") == 1);

    // No socket, and therefore no property: its absence is the honest answer to
    // "is there a socket?", which is exactly why a client reads the property
    // rather than reconstructing the path.
    CHECK(publishedSocketPath(d).empty());

    // And the window manager still manages windows. That is the whole of its
    // job; only the configuration connection was lost.
    CHECK(stillFraming(fixture, d, "no-socket"));
}

TEST_CASE("A socket left by a crashed predecessor is reclaimed", "[wm_socket]")
{
    // The stale-reclamation path, driven through the real binary rather than
    // only through configSocketStaleVerdict(). A window manager killed with
    // SIGKILL never runs release(), so its socket node survives -- and the next
    // one on the same display has to bind anyway or the feature is one crash
    // away from being permanently unavailable.
    std::string path;
    {
        WmFixture first;
        x11::DisplayPtr dp = first.openDisplay();
        REQUIRE(dp != nullptr);
        path = awaitPublishedSocketPath(dp.get());
        REQUIRE_FALSE(path.empty());

        // SIGKILL by the PID THIS FIXTURE CREATED. Never a name pattern.
        REQUIRE(first.wm().pid() > 0);
        ::kill(first.wm().pid(), SIGKILL);
        first.wm().waitForExit(8000);
    }

    // The node outlived the process: that is what makes it stale rather than
    // absent, and it is the precondition the next start has to handle.
    struct stat st;
    const bool survived = (::lstat(path.c_str(), &st) == 0) && S_ISSOCK(st.st_mode);
    INFO("abandoned node at " << path << " survived: " << survived);
    CHECK(survived);
    CHECK(configSocketStaleVerdict(path) == StaleVerdict::Stale);

    // A second window manager, which will land on some display of its own; the
    // reclamation of the abandoned node above is asserted directly, and the new
    // one is asserted to work.
    WmFixture second;
    x11::DisplayPtr dp2 = second.openDisplay();
    REQUIRE(dp2 != nullptr);
    const std::string secondPath = awaitPublishedSocketPath(dp2.get());
    {
        const std::string stderrText = second.wmStderr();
        INFO("wm stderr:\n" << stderrText);
        REQUIRE_FALSE(secondPath.empty());
    }

    Conn c(secondPath);
    REQUIRE(c.connected());
    std::string program;
    int protocol = 0;
    CHECK(shakeHands(c, program, protocol));
    CHECK(program == "wm2-born-again");
}


// -----------------------------------------------------------------------------
// What the socket may say, and to whom (D-14, T-9-13, CGUI-02 concurrency)
// -----------------------------------------------------------------------------

namespace {

// WM_CHANGE_STATE(IconicState) -- the ICCCM route into Client::hide(), sent to
// the ROOT with SubstructureRedirect|SubstructureNotify, which is the route
// WindowManager::eventClient() is written against.
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

bool icccmState(Display* d, Window w, long& out)
{
    static Atom prop = None;
    if (prop == None) prop = XInternAtom(d, "WM_STATE", False);

    Atom actualType = None;
    int actualFormat = 0;
    unsigned long nItems = 0, bytesAfter = 0;
    unsigned char* raw = nullptr;
    if (XGetWindowProperty(d, w, prop, 0, 2, False, AnyPropertyType,
                           &actualType, &actualFormat, &nItems, &bytesAfter,
                           &raw) != Success) {
        return false;
    }
    bool ok = false;
    if (raw && nItems >= 1 && actualFormat == 32) {
        out = reinterpret_cast<long*>(raw)[0];
        ok = true;
    }
    if (raw) XFree(raw);
    return ok;
}

void setClassHint(Display* d, Window win, const char* instance, const char* cls)
{
    XClassHint hint;
    hint.res_name  = const_cast<char*>(instance);
    hint.res_class = const_cast<char*>(cls);
    XSetClassHint(d, win, &hint);
    XSync(d, False);
}

std::string readWholeFile(const std::string& path)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return std::string();
    std::string out;
    char buf[8192];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

}  // namespace


TEST_CASE("The status counts are real and no window's identity is in the reply",
          "[wm_socket]")
{
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

    Conn c(path);
    REQUIRE(c.connected());
    std::string program;
    int protocol = 0;
    REQUIRE(shakeHands(c, program, protocol));

    // The baseline, asserted rather than assumed. The window manager's own
    // popups and its EWMH check window are override-redirect and so are never
    // adopted (deferred item 7's other half), and this fixture has mapped
    // nothing yet -- so the counts before anything is mapped must be zero, and
    // if they are not, this case says so instead of quietly comparing deltas.
    ConfigMessage before;
    std::string beforeRaw;
    REQUIRE(requestStatus(c, before, beforeRaw));
    INFO("baseline status: " << beforeRaw);
    REQUIRE(fieldValue(before, "managed") == "0");
    REQUIRE(fieldValue(before, "hidden") == "0");

    // DELIBERATELY DISTINCTIVE titles and classes. A window called "xterm"
    // would let the negative assertion below pass by accident -- the reply
    // legitimately contains digits and the program name, and a common word
    // would too. These strings appear nowhere else in the protocol.
    static const char* const kTitles[3] = {
        "wm2-socket-secret-alpha",
        "wm2-socket-secret-beta",
        "wm2-socket-secret-gamma"
    };
    static const char* const kClass = "Wm2SocketSecretClass";

    std::vector<Window> wins;
    for (int i = 0; i < 3; ++i) {
        Window win = createClient(d, 120 + i * 220, 90, 200, 150, kTitles[i]);
        setClassHint(d, win, "wm2socketsecret", kClass);
        XMapWindow(d, win);
        XSync(d, False);
        const Window frame = awaitFrameFor(d, win);
        const std::string stderrText = fixture.wmStderr();
        INFO("wm stderr:\n" << stderrText);
        REQUIRE(frame != None);
        wins.push_back(win);
    }
    settleWm(d);

    // Hide exactly one of them.
    requestIconify(d, wins[1]);
    REQUIRE(WmFixture::pollUntil([&] {
        pumpWm(d);
        long state = -1;
        return icccmState(d, wins[1], state) && state == IconicState;
    }, 8000));
    settleWm(d);

    ConfigMessage after;
    std::string afterRaw;
    {
        const std::string stderrText = fixture.wmStderr();
        INFO("wm stderr:\n" << stderrText);
        REQUIRE(requestStatus(c, after, afterRaw));
    }

    INFO("status reply: " << afterRaw);

    // Three under management, one of them hidden. "managed" counts every window
    // the window manager holds, hidden ones included: hiding MOVES a client
    // between two disjoint lists, so a count that dropped to two would report a
    // window manager losing windows as the user hides them.
    CHECK(fieldValue(after, "managed") == "3");
    CHECK(fieldValue(after, "hidden") == "1");

    // THE NEGATIVE ASSERTION, over the WHOLE reply text rather than over the
    // fields this build happens to emit: an eighth field added later would be
    // caught by this even though nothing here names it (T-9-13).
    for (const char* title : kTitles) {
        INFO("looking for leaked title: " << title);
        CHECK(afterRaw.find(title) == std::string::npos);
    }
    CHECK(afterRaw.find(kClass) == std::string::npos);
    CHECK(afterRaw.find("wm2socketsecret") == std::string::npos);

    // And still exactly seven fields.
    CHECK(after.fields.size() == 7);

    CHECK(fixture.wmAlive());
}

TEST_CASE("Two clients connected at once each get their own replies", "[wm_socket]")
{
    // CGUI-02's concurrency probe: "if interrupted or run in parallel, what is
    // guaranteed?" Each connection gets its own replies, in its own order, with
    // no line interleaved into another's.
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

    Conn a(path);
    Conn b(path);
    REQUIRE(a.connected());
    REQUIRE(b.connected());

    // INTERLEAVED ON PURPOSE: both handshakes are in flight before either
    // acknowledgement is read, so the window manager is holding two
    // half-finished conversations at once rather than servicing them in turn.
    ConfigMessage hello;
    hello.type = ConfigMessageType::Hello;
    hello.protocol = kConfigProtocolVersion;

    hello.program = "client-a";
    REQUIRE(a.send(hello));
    hello.program = "client-b";
    REQUIRE(b.send(hello));

    ConfigMessage ackA, ackB;
    ConfigDecodeResult rA = ConfigDecodeResult::Malformed;
    ConfigDecodeResult rB = ConfigDecodeResult::Malformed;
    REQUIRE(a.receive(ackA, rA));
    REQUIRE(b.receive(ackB, rB));
    CHECK(rA == ConfigDecodeResult::Ok);
    CHECK(rB == ConfigDecodeResult::Ok);
    CHECK(ackA.type == ConfigMessageType::HelloAck);
    CHECK(ackB.type == ConfigMessageType::HelloAck);

    ConfigMessage statusA, statusB;
    std::string rawA, rawB;
    REQUIRE(requestStatus(a, statusA, rawA));
    REQUIRE(requestStatus(b, statusB, rawB));

    INFO("client-a reply: " << rawA);
    INFO("client-b reply: " << rawB);

    // Each line is ONE well-formed message. An interleaved write would show up
    // here as a decode failure or a field count that is not seven, because two
    // spliced replies do not parse as one.
    CHECK(statusA.type == ConfigMessageType::StatusReply);
    CHECK(statusB.type == ConfigMessageType::StatusReply);
    CHECK(statusA.fields.size() == 7);
    CHECK(statusB.fields.size() == 7);

    // Neither connection saw the other's reply queued behind its own: each read
    // exactly one line and its buffer is empty.
    std::string leftover;
    CHECK_FALSE(a.readLine(leftover, 300));
    CHECK_FALSE(b.readLine(leftover, 300));

    CHECK(fixture.wmAlive());
}

TEST_CASE("A client vanishing mid-request costs only its own connection",
          "[wm_socket]")
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

    Conn survivor(path);
    REQUIRE(survivor.connected());
    std::string program;
    int protocol = 0;
    REQUIRE(shakeHands(survivor, program, protocol));

    {
        // HALF A MESSAGE, then gone. The window manager is left holding an
        // unterminated frame on a descriptor whose peer no longer exists --
        // and, because the reply is written with MSG_NOSIGNAL, a write to it
        // must not kill the process with SIGPIPE either.
        Conn quitter(path);
        REQUIRE(quitter.connected());
        REQUIRE(quitter.sendRaw("{\"type\":\"hello\",\"program\":\"half"));
        // Destructor closes the descriptor mid-frame.
    }

    settleWm(d);

    // The survivor's NEXT request still succeeds. Nothing about the other
    // connection's death reached it.
    ConfigMessage reply;
    std::string raw;
    {
        const std::string stderrText = fixture.wmStderr();
        INFO("wm stderr:\n" << stderrText);
        REQUIRE(requestStatus(survivor, reply, raw));
    }
    INFO("survivor reply: " << raw);
    CHECK(reply.type == ConfigMessageType::StatusReply);
    CHECK(reply.fields.size() == 7);

    CHECK(fixture.wmAlive());
    CHECK(stillFraming(fixture, d, "after-halfmessage"));
}

TEST_CASE("Asking for status twice returns the same field set", "[wm_socket]")
{
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();

    const std::string path = awaitPublishedSocketPath(d);
    REQUIRE_FALSE(path.empty());

    Conn c(path);
    REQUIRE(c.connected());
    std::string program;
    int protocol = 0;
    REQUIRE(shakeHands(c, program, protocol));

    ConfigMessage first, second;
    std::string firstRaw, secondRaw;
    REQUIRE(requestStatus(c, first, firstRaw));
    REQUIRE(requestStatus(c, second, secondRaw));

    INFO("first:  " << firstRaw);
    INFO("second: " << secondRaw);

    REQUIRE(first.fields.size() == second.fields.size());
    for (std::size_t i = 0; i < first.fields.size(); ++i) {
        // The names, and their ORDER, are identical: the reply carries an
        // ordered list precisely so a display can rely on it.
        CHECK(first.fields[i].first == second.fields[i].first);
        // Every VALUE is identical too, except uptime, which is the one field
        // that is supposed to move.
        if (first.fields[i].first != "uptime") {
            CHECK(first.fields[i].second == second.fields[i].second);
        }
    }

    CHECK(fixture.wmAlive());
}

TEST_CASE("The status assembly does not reach any per-window identity",
          "[wm_socket][source]")
{
    // A SOURCE-LEVEL guard beside the behavioural ones, and not instead of
    // them. The negative assertion over the reply text catches a leak this
    // build actually emits; this catches the EDIT that would introduce one,
    // scoped to the single function D-14's ceiling lives in, so a later change
    // that starts publishing titles is a red test rather than a finding in a
    // security review.
    const std::string source = readWholeFile(std::string(WM2_SOURCE_DIR) + "/src/Manager.cpp");
    REQUIRE_FALSE(source.empty());

    const std::string marker = "ConfigMessage WindowManager::statusReplyMessage() const";
    const std::size_t start = source.find(marker);
    REQUIRE(start != std::string::npos);

    // The function body ends at the first line that is a closing brace in
    // column zero -- the same region the plan's `awk` gate selects.
    const std::size_t end = source.find("\n}\n", start);
    REQUIRE(end != std::string::npos);

    const std::string region = source.substr(start, end - start);
    INFO("status assembly region:\n" << region);

    for (const char* forbidden : { "label()", "className", "instanceName",
                                   "res_class", "res_name" }) {
        INFO("forbidden in the status assembly: " << forbidden);
        CHECK(region.find(forbidden) == std::string::npos);
    }

    // And the region really is the assembly, not an empty match: it must
    // contain the seven field names D-14 permits.
    for (const char* field : { "version", "protocol", "uptime", "screen-width",
                               "screen-height", "managed", "hidden" }) {
        INFO("expected field missing from the region: " << field);
        CHECK(region.find(field) != std::string::npos);
    }
}


// -----------------------------------------------------------------------------
// A BOUNDED modalWait() MUST NOT REPORT Timeout BEFORE ITS BOUND
// (CodeRabbit F2, 2026-09-07)
// -----------------------------------------------------------------------------
//
// THE DEFECT. modalWait() clamps its poll timeout with
// clampPollTimeoutForSocket(), which shortens it to the configuration socket's
// timeout hint -- the time left on a connection that has not sent its hello, or
// on an accept stall. When poll() then returned 0 at the HINT rather than at
// the caller's deadline, the old
//
//     if (r == 0 && bounded) return ModalWait::Timeout;
//
// reported Timeout with the caller's deadline still in the future. The hint is
// a reason to WAKE UP and service the socket, never a reason to tell the caller
// its time is up.
//
// WHY THE FIRST CASE BELOW IS A SOURCE-LEVEL GUARD, and what it is not. This
// file already carries one such guard, for D-14's ceiling, and the reason is
// the same here: the defect has NO externally observable consequence in this
// build, so no behavioural case can be red at the round base. Every bounded
// caller of modalWait() today either measures the time it actually waited
// (Border.cpp's tab-button hold, which was fixed at the CONSUMER by WR-14) or
// simply loops on Timeout (Client.cpp's move and resize drags), and the two
// remaining callers pass -1. The finding is a contract violation whose only
// victim is the next caller written against the contract -- so it is guarded
// where it can be: modalWait() must report Timeout from exactly ONE place, the
// deadline check at the head of its loop.
//
// The behavioural case that follows it is the other half, and it is honestly
// green at the round base: it drives a bounded wait ACROSS the moment a silent
// connection's hello deadline expires -- the exact moment the clamp bites --
// and proves the escalation that counts those waits still happens, and still
// takes at least its configured delay. That is the guard against this fix
// turning a bounded wait into one that never returns Timeout at all.

TEST_CASE("modalWait reports Timeout from its deadline check and nowhere else",
          "[wm_socket][source][modalwait]")
{
    const std::string source = readWholeFile(std::string(WM2_SOURCE_DIR) + "/src/Events.cpp");
    REQUIRE_FALSE(source.empty());

    const std::string marker = "WindowManager::ModalWait WindowManager::modalWait(";
    const std::size_t start = source.find(marker);
    REQUIRE(start != std::string::npos);

    // The body ends at the first closing brace in column zero, the same way the
    // status-assembly guard above selects its region.
    const std::size_t end = source.find("\n}\n", start);
    REQUIRE(end != std::string::npos);

    const std::string region = source.substr(start, end - start);
    INFO("modalWait region:\n" << region);

    // The region really is the one that clamps -- otherwise this guard could be
    // watching a function that never had the defect.
    REQUIRE(region.find("clampPollTimeoutForSocket") != std::string::npos);

    // THE CONTRACT. Exactly one place returns Timeout.
    const std::string ret = "return ModalWait::Timeout;";
    int returns = 0;
    for (std::size_t at = region.find(ret); at != std::string::npos;
         at = region.find(ret, at + ret.size())) {
        ++returns;
    }
    INFO("Timeout returns found in modalWait: " << returns);
    CHECK(returns == 1);

    // ...and it is the DEADLINE check, not the poll's return value. The two
    // spellings are asserted separately so a failure names which half broke.
    const std::size_t only = region.find(ret);
    REQUIRE(only != std::string::npos);
    const std::size_t deadline = region.find("left <= clock::duration::zero()");
    INFO("deadline check at " << deadline << ", Timeout return at " << only);
    CHECK(deadline != std::string::npos);
    CHECK(deadline < only);
    CHECK(only - deadline < 80);   // the same statement, not merely earlier

    // And the poll's zero return is not a Timeout by any spelling: the caller's
    // deadline is the only thing that ends a bounded wait.
    CHECK(region.find("r == 0 && bounded") == std::string::npos);
    CHECK(region.find("bounded && r == 0") == std::string::npos);
}

TEST_CASE("a bounded modal wait that crosses a silent connection's deadline still "
          "takes its full delay", "[wm_socket][modalwait]")
{
    // THE MOMENT THE CLAMP BITES, arranged rather than hoped for. A connection
    // that never sends hello is closed kConfigSocketHelloDeadlineMs after it
    // connects, and timeoutHintMs() reports the time left on it. While that
    // remainder is larger than the 50 ms bound of the tab-button hold's wait,
    // the clamp changes nothing; it is only in the last 50 ms before the
    // deadline that the hint is the shorter of the two and the poll returns
    // early. So the press is started so that the deadline falls INSIDE the
    // hold, and the escalation from "hide" to "delete" -- which is driven
    // entirely by accumulated bounded waits -- has to survive it.
    //
    // GREEN AT THE ROUND BASE, and said so deliberately: Border.cpp measures
    // each wait rather than assuming it lasted its bound (WR-14), so an early
    // Timeout is already absorbed there. This case is the guard on the OTHER
    // side of the fix -- a modalWait() that stopped timing out would hang this
    // hold until the button came up, and the delete would never be sent.
    constexpr int kDelayMs = 250;

    WmFixtureOptions options;
    options.wmArgs = { "--destroy-window-delay=" + std::to_string(kDelayMs) };
    WmFixture fixture(options);

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

    // THE SILENT CONNECTION. Connected and then never spoken through, so it is
    // exactly what timeoutHintMs() reports a deadline for. The clock starts
    // here.
    const std::chrono::steady_clock::time_point connectedAt =
        std::chrono::steady_clock::now();
    Conn silent(path);
    REQUIRE(silent.connected());

    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    Window win = None;
    Window frame = None;
    {
        win = createClient(d, 200, 150, 260, 190, "deadlinecross");
        announceDeleteProtocol(d, win);
        XMapWindow(d, win);
        XSync(d, False);
        frame = awaitFrameFor(d, win);
        const std::string stderrText = fixture.wmStderr();
        INFO("wm stderr:\n" << stderrText);
        REQUIRE(frame != None);
    }
    settleWm(d);

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

    // Hold for three times the delay, starting a little before the deadline, so
    // the deadline is crossed while the hold's bounded waits are running and
    // there is still more than the delay's worth of hold left afterwards.
    const int holdMs = kDelayMs * 3;
    const int startBeforeDeadlineMs = kDelayMs;
    const long long sinceConnect =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - connectedAt).count();
    const long long waitBeforePress =
        kConfigSocketHelloDeadlineMs - startBeforeDeadlineMs - sinceConnect;
    INFO("setup took " << sinceConnect << " ms; sleeping "
         << waitBeforePress << " ms before the press");
    if (waitBeforePress > 0) holdFor(static_cast<int>(waitBeforePress));

    const std::chrono::steady_clock::time_point pressedAt =
        std::chrono::steady_clock::now();
    driver.press(Button1);
    holdFor(holdMs);
    driver.release(Button1);
    XSync(d, False);

    static Atom protocols = XInternAtom(d, "WM_PROTOCOLS", False);
    static Atom del = XInternAtom(d, "WM_DELETE_WINDOW", False);
    long long deletedAfterMs = -1;
    const bool deleted = WmFixture::pollUntil([&] {
        pumpWm(d);
        XEvent ev;
        while (XCheckTypedWindowEvent(d, win, ClientMessage, &ev)) {
            if (ev.xclient.message_type == protocols &&
                static_cast<Atom>(ev.xclient.data.l[0]) == del) {
                deletedAfterMs =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - pressedAt).count();
                return true;
            }
        }
        return false;
    }, 15000);

    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);
    INFO("delete observed " << deletedAfterMs << " ms after the press");

    // The bounded wait still expires, so the hold still escalates.
    CHECK(deleted);

    // ...and it did not escalate before the delay it was given. No upper bound
    // is asserted: this runs under a sanitizer in one tree and on a shared
    // display in every tree, and an upper bound would be a flake, not a claim.
    CHECK(deletedAfterMs >= kDelayMs);

    CHECK(fixture.wmAlive());
}


// -----------------------------------------------------------------------------
// TWO OVERLAPPING `set` MESSAGES (09-04 truth 7, 09-VERIFICATION item 5)
// -----------------------------------------------------------------------------
//
// The claim 09-04 could not observe: two `set` messages that are in flight at
// the same time are both applied, in the order they were written, on the event
// loop's own thread -- so the LATER write is the one the window manager ends up
// using, neither is dropped, and the window manager is still a window manager
// afterwards. 09-04-SUMMARY.md records this as `human_judgment: true`, resting
// on "there is no second thread and no queue to reorder": structural, and
// therefore unable to fail. This case is the behavioural half.
//
// WHAT "ARRIVAL ORDER" CAN HONESTLY MEAN HERE, and why the two connections are
// opened in the order they are written to. `poll()` reports a SET of readable
// descriptors; it does not record which of them became readable first, and
// ConfigSocketServer::service() (src/SocketServer.cpp) walks its connections in
// ACCEPT order within one servicing pass. So when both messages land before the
// window manager next runs -- which is the whole point of writing them back to
// back -- the order the messages are applied in is the accept order, and no
// implementation on a level-triggered poll could make it anything else. The two
// candidate orders are therefore deliberately ALIGNED: the connection that
// writes first is the connection that connected first. Whether the window
// manager wakes once (both pending, accept order) or twice (one pending each
// time, write order), the answer is the same, and the case is deterministic
// instead of being a coin toss over which happened.
//
// THE MIRROR is what stops this passing by coincidence of a constant: the same
// exchange is run twice, once writing 11 then 21 and once writing 21 then 11,
// and the winner is the later write BOTH times. A window manager that ignored
// the second message, or that kept the smaller value, or that had frozen at
// 21 for some unrelated reason, fails one of the two halves.
//
// GREEN ON ITS FIRST RUN, because the implementation is correct, so the
// evidence that it can fail is a MUTATION rather than a red run: reversing the
// order ConfigSocketServer::service() walks its connections in reddens both
// halves, three runs out of three, with the LOSING value in force each time
// (11 where 21 was written second, 21 where 11 was). That also settles the
// dynamics -- both messages really are pending in ONE servicing pass, which is
// exactly why the accept order above is aligned with the write order and not
// left to chance.
//
// MEASURED, and stable across five runs before the mutation and three after:
// four ShapeNotify events per applied `set` on this frame, eight for the pair.
// The assertion is the RATIO against a single `set` measured in the same run,
// so it survives a change to how many rectangle-combining requests one
// re-layout issues.

namespace {

// An XDG tree of this case's own, under the CMake binary directory rather than
// a bare /tmp name (threat T-8-TMP), unique per process and per call. Cloned
// from tests/test_wm_config_live.cpp, which is where the rest of this file's
// frame-thickness knowledge comes from.
std::string makeSocketConfigHome(const std::string& contents)
{
    static int counter = 0;
    const std::string base = std::string(WM2_TEST_WORKDIR) + "/socket-cfg-" +
                             std::to_string(::getpid()) + "-" +
                             std::to_string(++counter);
    ::mkdir(base.c_str(), 0700);
    ::mkdir((base + "/wm2-born-again").c_str(), 0700);

    FILE* f = std::fopen((base + "/wm2-born-again/config").c_str(), "wb");
    if (f) {
        std::fwrite(contents.data(), 1, contents.size(), f);
        std::fclose(f);
    }
    return base;
}

// XDG_CONFIG_DIRS is overridden as well as XDG_CONFIG_HOME: the fallback is
// /etc/xdg, so on a machine with a system-wide config the host's settings would
// be layered under this case and quietly change the baseline it measures from.
WmFixtureOptions socketFixtureWithConfigHome(const std::string& home)
{
    WmFixtureOptions o;
    o.childEnv["XDG_CONFIG_HOME"] = home;
    o.childEnv["XDG_CONFIG_DIRS"] = home + "/no-system-config";
    return o;
}

// The thickness the config file below names, so every geometry expectation is a
// DIFFERENCE from a known starting point rather than a magic number.
constexpr int kBaselineThickness = 7;

// A third value, distinct from both of the overlapping ones, used to calibrate
// the re-frame counter at the end of the exchange.
constexpr int kCalibrationThickness = 15;

// How far the client sits inside its frame, horizontally.
//
// Border::xIndent() is m_tabWidth + FRAME_WIDTH + 1, and only FRAME_WIDTH moves
// when the thickness changes -- the tab width is a font measurement and is the
// same before and after. So the inset changes by EXACTLY the change in
// thickness, and the caller needs to know nothing about how a font is measured.
// Read from the server through XTranslateCoordinates (rectOf), not from
// anything the window manager says about itself.
int clientInset(Display* d, Window frame, Window client)
{
    const Rect f = rectOf(d, frame);
    const Rect c = rectOf(d, client);
    return c.x - f.x;
}

bool sendSet(Conn& c, const std::string& key, const std::string& value)
{
    ConfigMessage m;
    m.type  = ConfigMessageType::Set;
    m.key   = key;
    m.value = value;
    return c.send(m);
}

// The window manager's own answer to "what are you using now?", over the
// socket, as a client would ask it.
bool requestGet(Conn& c, const std::string& key, ConfigMessage& reply,
                std::string& rawLine)
{
    ConfigMessage get;
    get.type = ConfigMessageType::Get;
    get.key  = key;
    if (!c.send(get)) return false;
    if (!c.readLine(rawLine)) return false;
    return configProtocolDecode(rawLine, reply) == ConfigDecodeResult::Ok;
}

// Every ShapeNotify this connection has been sent, counted and removed. The
// window manager re-shapes a frame only when it re-lays it out, so this counts
// RE-FRAMES; other events (this file's own nudge windows) are discarded.
int drainShapeNotify(Display* d, int shapeEventBase)
{
    int shapes = 0;
    while (XPending(d)) {
        XEvent e;
        XNextEvent(d, &e);
        if (e.type == shapeEventBase + ShapeNotify) ++shapes;
    }
    return shapes;
}

// One run of the exchange. `firstValue` is written first, on the connection
// that connected first; `secondValue` is written second, on the connection that
// connected second; `secondValue` is expected to win.
void runOverlappingSets(int firstValue, int secondValue)
{
    INFO("writing " << firstValue << " then " << secondValue
         << ", expecting " << secondValue << " to win");

    const std::string home =
        makeSocketConfigHome("frame-thickness=" + std::to_string(kBaselineThickness) + "\n");
    WmFixture fixture(socketFixtureWithConfigHome(home));
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    parkPointer(d);

    // MAPPED FIRST, for 09-04's reason: a window mapped after the messages
    // would pick the new thickness up at map time even from a window manager
    // that stored the value and never applied it to anything on screen.
    Window client = None;
    Window frame = mapClientAndAwaitFrame(d, 200, 150, 300, 220, client, "overlapping-sets");
    REQUIRE(frame != None);

    const std::string path = awaitPublishedSocketPath(d);
    {
        const std::string stderrText = fixture.wmStderr();
        INFO("wm stderr:\n" << stderrText);
        REQUIRE_FALSE(path.empty());
    }

    // A CONNECTS AND SHAKES HANDS BEFORE B EVEN CONNECTS. That is what makes
    // the accept order known, and the accept order is what the servicing pass
    // walks -- see the note above this namespace.
    Conn a(path);
    REQUIRE(a.connected());
    std::string programA;
    int protocolA = 0;
    REQUIRE(shakeHands(a, programA, protocolA));

    Conn b(path);
    REQUIRE(b.connected());
    std::string programB;
    int protocolB = 0;
    REQUIRE(shakeHands(b, programB, protocolB));

    settleWm(d);
    const Rect frameBefore  = rectOf(d, frame);
    const Rect clientBefore = rectOf(d, client);
    const int insetBefore   = clientBefore.x - frameBefore.x;

    int shapeEventBase = 0, shapeErrorBase = 0;
    REQUIRE(XShapeQueryExtension(d, &shapeEventBase, &shapeErrorBase));
    XShapeSelectInput(d, frame, ShapeNotifyMask);
    XSync(d, False);
    settleWm(d);
    drainShapeNotify(d, shapeEventBase);   // everything before the pair is noise

    // THE OVERLAP. Two separate send() calls, the second issued as soon as the
    // first has returned, and NEITHER reply read in between -- so both messages
    // are outstanding at once and the window manager is holding two
    // conversations rather than being handed one at a time.
    const bool sentA = sendSet(a, "frame-thickness", std::to_string(firstValue));
    const bool sentB = sendSet(b, "frame-thickness", std::to_string(secondValue));

    ConfigMessage ackA, ackB;
    ConfigDecodeResult rA = ConfigDecodeResult::Malformed;
    ConfigDecodeResult rB = ConfigDecodeResult::Malformed;
    const bool gotA = sentA && a.receive(ackA, rA);
    const bool gotB = sentB && b.receive(ackB, rB);

    // The geometry the LATER write asks for, waited for by observation and
    // never by elapsed time.
    const int expectedInset = insetBefore + (secondValue - kBaselineThickness);
    WmFixture::pollUntil([&] {
        pumpWm(d);
        return clientInset(d, frame, client) == expectedInset;
    }, 8000);

    // ...and then settled and read AGAIN, because a poll that stopped at the
    // first sighting would be satisfied by a transient. What is asserted below
    // is the state the exchange came to REST in.
    settleWm(d);
    const int pairShapes    = drainShapeNotify(d, shapeEventBase);
    const Rect frameAfter   = rectOf(d, frame);
    const Rect clientAfter  = rectOf(d, client);
    const int insetAfter    = clientAfter.x - frameAfter.x;

    // The window manager's own answer, on a connection that had nothing to do
    // with either write.
    Conn observer(path);
    const bool observerConnected = observer.connected();
    std::string programC;
    int protocolC = 0;
    const bool observerShook = observerConnected && shakeHands(observer, programC, protocolC);

    ConfigMessage valueReply;
    std::string valueRaw;
    const bool valueRead = observerShook &&
                           requestGet(observer, "frame-thickness", valueReply, valueRaw);

    // THE CALIBRATION, and the reason the re-frame count is not a magic number:
    // one `set`, alone, from the observer connection, counted the same way. How
    // many ShapeNotify events one re-layout produces is an internal detail
    // (shapeParent() combines a bounding and a clipping region, today), so the
    // claim asserted is the one that matters -- the pair did exactly TWICE the
    // work of a single applied `set`, which is one re-frame each and neither
    // message dropped nor applied twice.
    drainShapeNotify(d, shapeEventBase);
    const bool sentCalibration =
        observerShook && sendSet(observer, "frame-thickness",
                                 std::to_string(kCalibrationThickness));
    ConfigMessage ackCalibration;
    ConfigDecodeResult rCalibration = ConfigDecodeResult::Malformed;
    const bool gotCalibration = sentCalibration &&
                                observer.receive(ackCalibration, rCalibration);
    const int calibrationInset = insetBefore + (kCalibrationThickness - kBaselineThickness);
    WmFixture::pollUntil([&] {
        pumpWm(d);
        return clientInset(d, frame, client) == calibrationInset;
    }, 8000);
    settleWm(d);
    const int oneSetShapes = drainShapeNotify(d, shapeEventBase);
    const int insetAfterCalibration = clientInset(d, frame, client);

    // Still a window manager: alive, and still framing something new.
    const bool alive   = fixture.wmAlive();
    const bool framing = stillFraming(fixture, d, "after-overlapping-sets");

    const std::string stderrText = fixture.wmStderr();
    INFO("wm stderr:\n" << stderrText);
    INFO("value reply: " << valueRaw);
    INFO("frame  before " << frameBefore.x << "," << frameBefore.y
         << " " << frameBefore.w << "x" << frameBefore.h
         << " after " << frameAfter.x << "," << frameAfter.y
         << " " << frameAfter.w << "x" << frameAfter.h);
    INFO("inset before " << insetBefore << " after " << insetAfter
         << " expected " << expectedInset);
    INFO("re-frames: pair " << pairShapes << ", single " << oneSetShapes);
    INFO("X errors so far: " << g_xErrorCount);

    // NEITHER MESSAGE WAS DROPPED: both were acknowledged, each on its own
    // connection, and each acknowledgement names the key it answers.
    CHECK(sentA);
    CHECK(sentB);
    CHECK(gotA);
    CHECK(gotB);
    CHECK(rA == ConfigDecodeResult::Ok);
    CHECK(rB == ConfigDecodeResult::Ok);
    CHECK(ackA.type == ConfigMessageType::Ack);
    CHECK(ackB.type == ConfigMessageType::Ack);
    CHECK(ackA.key == "frame-thickness");
    CHECK(ackB.key == "frame-thickness");

    // THE LATER WRITE WON, on the screen: the client's inset grew by exactly
    // the change the second value asks for...
    CHECK(insetAfter == expectedInset);
    // ...and not by the change the FIRST value asks for, named explicitly so
    // the loser is part of the assertion rather than merely absent from it.
    CHECK(insetAfter != insetBefore + (firstValue - kBaselineThickness));

    // The user's own window is where it was, at the size it was: a thickness
    // change moves decoration, not content (09-04's central claim, re-checked
    // here because two overlapping applications are the case most likely to
    // leave a client displaced).
    CHECK(clientAfter.x == clientBefore.x);
    CHECK(clientAfter.y == clientBefore.y);
    CHECK(clientAfter.w == clientBefore.w);
    CHECK(clientAfter.h == clientBefore.h);

    // ...and in the window manager's own words.
    CHECK(observerConnected);
    CHECK(observerShook);
    CHECK(valueRead);
    CHECK(valueReply.type == ConfigMessageType::Value);
    CHECK(valueReply.key == "frame-thickness");
    CHECK(valueReply.value == std::to_string(secondValue));

    // ONE RE-FRAME PER APPLIED `set`, measured against a single `set` in the
    // same run rather than against a constant.
    CHECK(gotCalibration);
    CHECK(ackCalibration.type == ConfigMessageType::Ack);
    CHECK(insetAfterCalibration == calibrationInset);
    CHECK(oneSetShapes > 0);
    CHECK(pairShapes == 2 * oneSetShapes);

    CHECK(alive);
    CHECK(framing);
}

}  // namespace


TEST_CASE("Two overlapping sets are applied in arrival order", "[wm_socket]")
{
    // Written 11 then 21: 21 wins.
    runOverlappingSets(11, 21);

    // THE MIRROR, written 21 then 11: 11 wins. A fresh window manager, so the
    // second run starts from the same baseline as the first and neither value
    // is already in force.
    runOverlappingSets(21, 11);
}
