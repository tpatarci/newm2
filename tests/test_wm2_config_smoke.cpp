// wm2-config: the settings window's model, and that the window opens at all
// (CGUI-01, CGUI-03, D-20, plan 09-06).
//
// TWO HALVES, and the split is deliberate.
//
// The DISPLAY-FREE half exercises FormState and the config file writer with no
// X server, no toolkit and no window manager. It runs on every host, including
// one where pkg-config never found gtk+-3.0 and the GUI was therefore not
// built. That is what stops D-20's "skipped with a reason" from becoming
// "nothing was checked": the reset semantics, the save semantics and the banner
// sentence are all proven even where no window can be opened.
//
// The SMOKE half spawns the real wm2-config on the fixture's Xvfb display and
// asserts its window maps. When the GUI was not built there is no binary to
// spawn, and those cases SKIP with the reason stated (Catch2's own SKIP, not a
// silent return) -- because a silent pass would let a GTK-less machine certify
// CGUI-01.
//
// DISC-09: the smoke half asserts the window MAPS. It deliberately does not
// assert clean stderr. GTK's accessibility bridge is a hard dependency of
// libgtk-3-0 and reaches for a session bus that a headless fixture does not
// have; the warning it prints is expected, is harmless, and tightening this
// assertion to catch it would make the suite fail on precisely the bare droplet
// this project targets (09-RESEARCH.md Pitfall 3).
//
// Standing rules honoured here: every X server is the fixture's own
// (-nolisten tcp, no access-control flag), every child is terminated by a PID
// this file created, nothing reads ~/.xsession-errors, and no case reads or
// writes the developer's real configuration directory -- the layered reads all
// run with XDG_CONFIG_HOME and XDG_CONFIG_DIRS pointed at a tree under the
// build directory.

#include <catch2/catch_test_macros.hpp>

#include "support/WmFixture.h"
#include "support/XTestDriver.h"

#include "../apps/wm2-config/ConnectionState.h"
#include "../apps/wm2-config/FormState.h"
#include "../apps/wm2-config/ProtocolClient.h"

#include "Config.h"
#include "ConfigFileWriter.h"
#include "ConfigProtocol.h"
#include "SocketServer.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace wm2test;

namespace {

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// A configuration tree of this case's own
// ---------------------------------------------------------------------------

// Under the CMake binary directory rather than a bare /tmp name (threat
// T-8-TMP), unique per process and per call so two ctest workers cannot read
// each other's files.
std::string makeTree(const std::string& suffix)
{
    static int counter = 0;
    const std::string base = std::string(WM2_TEST_WORKDIR) + "/gui-" +
                             std::to_string(::getpid()) + "-" +
                             std::to_string(++counter) + "-" + suffix;
    ::mkdir(base.c_str(), 0700);
    return base;
}

// Create every missing component of `path`'s parent. mkdir(2) makes ONE
// directory, so a two-deep leaf such as .../user/wm2-born-again/config needs
// this walk -- and a single mkdir failing silently is how these cases first
// went green against a config file that had never been written.
void makeParents(const std::string& path)
{
    for (std::size_t i = 1; i < path.size(); ++i) {
        if (path[i] != '/') continue;
        ::mkdir(path.substr(0, i).c_str(), 0700);
    }
}

void writeFile(const std::string& path, const std::string& contents)
{
    makeParents(path);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out.good());
    out << contents;
    out.close();
    REQUIRE(out.good());
}

std::string readFileOrEmpty(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// Nanosecond modification time, so "the file was not touched" is a real
// assertion rather than one that a second-granularity stamp would pass by
// accident on any run shorter than a second.
bool modificationTime(const std::string& path, struct timespec& out)
{
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return false;
    out = st.st_mtim;
    return true;
}

bool sameTime(const struct timespec& a, const struct timespec& b)
{
    return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec;
}

// Point the layered read at a tree of this case's own, and put the environment
// back afterwards. The standing rule is that no case may read or write the
// developer's real configuration directory; this is what enforces it, rather
// than a comment hoping nobody calls Config::load().
class ScopedXdg {
public:
    ScopedXdg(const std::string& home, const std::string& dirs)
        : m_home(saved("XDG_CONFIG_HOME")), m_dirs(saved("XDG_CONFIG_DIRS"))
    {
        ::setenv("XDG_CONFIG_HOME", home.c_str(), 1);
        ::setenv("XDG_CONFIG_DIRS", dirs.c_str(), 1);
    }

    ~ScopedXdg()
    {
        restore("XDG_CONFIG_HOME", m_home);
        restore("XDG_CONFIG_DIRS", m_dirs);
    }

    ScopedXdg(const ScopedXdg&) = delete;
    ScopedXdg& operator=(const ScopedXdg&) = delete;

private:
    static std::pair<bool, std::string> saved(const char* name)
    {
        const char* v = std::getenv(name);
        return v ? std::make_pair(true, std::string(v))
                 : std::make_pair(false, std::string());
    }

    static void restore(const char* name, const std::pair<bool, std::string>& v)
    {
        if (v.first) ::setenv(name, v.second.c_str(), 1);
        else         ::unsetenv(name);
    }

    std::pair<bool, std::string> m_home;
    std::pair<bool, std::string> m_dirs;
};

// ---------------------------------------------------------------------------
// The window, observed from outside
// ---------------------------------------------------------------------------

// X protocol errors are a RACE while walking a live window tree, not a failure:
// a window listed by XQueryTree can be gone before the property read names it,
// and Xlib's default handler would kill this process with no assertion output.
int g_xErrors = 0;
int quietXErrorHandler(Display*, XErrorEvent*) { ++g_xErrors; return 0; }
struct QuietXErrors { QuietXErrors() { XSetErrorHandler(quietXErrorHandler); } };
const QuietXErrors g_quiet;

// The value of _WM2_CONFIG_STATE on `w`, or "" if it does not carry one.
std::string configStateOf(Display* d, Window w)
{
    const Atom prop = XInternAtom(d, kConfigStateProperty, True);
    if (prop == None) return std::string();

    Atom actualType = None;
    int actualFormat = 0;
    unsigned long items = 0, after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(d, w, prop, 0, 64, False, AnyPropertyType,
                           &actualType, &actualFormat, &items, &after,
                           &data) != Success) {
        return std::string();
    }
    std::string value;
    if (data) {
        if (actualFormat == 8) value.assign(reinterpret_cast<char*>(data), items);
        XFree(data);
    }
    return value;
}

// Depth-first search of the whole tree for the one window carrying the
// property. Recursive because a running window manager REPARENTS the GUI into
// a frame, so it is not a child of the root by the time it is mapped.
Window findConfigWindow(Display* d, Window from, std::string& stateOut)
{
    if (!configStateOf(d, from).empty() && from != DefaultRootWindow(d)) {
        stateOut = configStateOf(d, from);
        return from;
    }

    Window root = None, parent = None, *children = nullptr;
    unsigned int n = 0;
    if (!XQueryTree(d, from, &root, &parent, &children, &n)) return None;
    Window found = None;
    for (unsigned int i = 0; i < n && found == None; ++i) {
        found = findConfigWindow(d, children[i], stateOut);
    }
    if (children) XFree(children);
    return found;
}

bool isViewable(Display* d, Window w)
{
    XWindowAttributes attr;
    if (!XGetWindowAttributes(d, w, &attr)) return false;
    return attr.map_state == IsViewable;
}

#ifdef WM2_CONFIG_PATH
// Fork and exec the built wm2-config with its output bound to files (never a
// pipe that could fill and deadlock the child). The caller owns the returned
// ChildProcess and terminates it by that PID.
ChildProcess spawnConfigGui(const std::string& display,
                            const std::vector<std::string>& args,
                            const std::string& home)
{
    static int counter = 0;
    const std::string stem = std::string(WM2_TEST_WORKDIR) + "/wm2-config-" +
                             std::to_string(::getpid()) + "-" +
                             std::to_string(++counter);

    const pid_t pid = ::fork();
    if (pid < 0) return ChildProcess();
    if (pid == 0) {
        const int outFd = ::open((stem + ".out").c_str(),
                                 O_CREAT | O_WRONLY | O_TRUNC, 0600);
        const int errFd = ::open((stem + ".err").c_str(),
                                 O_CREAT | O_WRONLY | O_TRUNC, 0600);
        if (outFd >= 0) { ::dup2(outFd, STDOUT_FILENO); ::close(outFd); }
        if (errFd >= 0) { ::dup2(errFd, STDERR_FILENO); ::close(errFd); }

        ::setenv("DISPLAY", display.c_str(), 1);
        ::setenv("GDK_BACKEND", "x11", 1);
        // GTK wants somewhere writable for its own state, and the standing rule
        // is that no test touches the developer's real directories.
        ::setenv("HOME", home.c_str(), 1);
        ::setenv("XDG_CONFIG_HOME", (home + "/.config").c_str(), 1);
        ::setenv("XDG_CACHE_HOME", (home + "/.cache").c_str(), 1);
        // XDG_RUNTIME_DIR is deliberately INHERITED, not redirected: it is what
        // configSocketDirectory() resolves the socket path from, and the window
        // manager this GUI must find was started by the fixture with the
        // ambient value. Pointing the child somewhere private would make it
        // look for the socket in a directory nothing ever bound in -- which is
        // a real file-only session, and would quietly turn the connected case
        // into a second copy of the no-socket one.
        // No session bus on a fixture display; saying so up front is quieter
        // than letting GTK discover it, and changes nothing about the assertion
        // (DISC-09).
        ::setenv("NO_AT_BRIDGE", "1", 1);

        std::vector<std::string> owned;
        owned.push_back(WM2_CONFIG_PATH);
        for (const std::string& a : args) owned.push_back(a);
        std::vector<char*> argv;
        argv.reserve(owned.size() + 1);
        for (std::string& a : owned) argv.push_back(&a[0]);
        argv.push_back(nullptr);

        ::execv(WM2_CONFIG_PATH, argv.data());
        ::_exit(127);
    }
    return ChildProcess(pid);
}
#endif

// ---------------------------------------------------------------------------
// A peer on the socket path that is NOT a window manager
// ---------------------------------------------------------------------------

// A socket path SHORT enough to bind.
//
// sockaddr_un.sun_path is 108 bytes on Linux (RESEARCH Pitfall 4), and this
// repository is checked out several directories deep inside a worktree, so a
// socket under WM2_TEST_WORKDIR does not fit -- bind() answers a path that is
// too long with a confusing EINVAL rather than with the truth. The fake peer
// therefore lives beside where the real one would: the same mode-0700
// per-user directory configSocketDirectory() resolves, with a name unique to
// this process and this call so two ctest workers cannot collide (T-8-TMP,
// which forbids a fixed /tmp name, not a per-uid directory).
std::string shortSocketPath(const std::string& suffix)
{
    static int counter = 0;
    const std::string dir = configSocketDirectory();
    makeParents(dir + "/x");
    ::mkdir(dir.c_str(), 0700);
    return dir + "/test-" + std::to_string(::getpid()) + "-" +
           std::to_string(++counter) + "-" + suffix;
}


// Listens on a unix socket of this case's own and answers the hello in one of
// two dishonest ways. Its whole purpose is to be a stranger: D-15 says the GUI
// never sends settings to one, and the only way to prove that is to be one.
//
// The accept loop runs on a thread this class owns and joins in its destructor;
// nothing here is terminated by name or left behind.
class FakeServer {
public:
    enum class Reply {
        Nothing,       // accepts, reads, and says nothing at all
        WrongVersion   // a well-formed hello-ack naming a version nobody speaks
    };

    FakeServer(const std::string& path, Reply reply)
        : m_path(path), m_reply(reply)
    {
        makeParents(path);

        m_listenFd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (m_listenFd < 0) return;

        struct sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        if (path.size() + 1 > sizeof(addr.sun_path)) return;
        std::memcpy(addr.sun_path, path.c_str(), path.size());

        ::unlink(path.c_str());
        if (::bind(m_listenFd, reinterpret_cast<struct sockaddr*>(&addr),
                   sizeof(addr)) != 0) {
            return;
        }
        if (::listen(m_listenFd, 4) != 0) return;

        m_listening = true;
        m_thread = std::thread([this]() { serve(); });
    }

    ~FakeServer()
    {
        m_stop = true;
        if (m_listenFd >= 0) ::shutdown(m_listenFd, SHUT_RDWR);
        if (m_thread.joinable()) m_thread.join();
        if (m_listenFd >= 0) ::close(m_listenFd);
        ::unlink(m_path.c_str());
    }

    FakeServer(const FakeServer&) = delete;
    FakeServer& operator=(const FakeServer&) = delete;

    bool listening() const { return m_listening; }
    const std::string& path() const { return m_path; }

private:
    void serve()
    {
        while (!m_stop) {
            struct pollfd p;
            p.fd = m_listenFd;
            p.events = POLLIN;
            p.revents = 0;
            const int r = ::poll(&p, 1, 100);
            if (r <= 0) continue;

            const int fd = ::accept(m_listenFd, nullptr, nullptr);
            if (fd < 0) continue;

            if (m_reply == FakeServer::Reply::WrongVersion) {
                ConfigMessage ack;
                ack.type = ConfigMessageType::HelloAck;
                ack.program = "not-really-a-window-manager";
                ack.protocol = 99;
                const std::string bytes = configProtocolEncode(ack);
                ssize_t ignored = ::send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL);
                (void)ignored;
            }

            // Held open either way, so the client's own deadline is what ends
            // the exchange rather than a peer hanging up and looking like an
            // absent window manager.
            for (int i = 0; i < 80 && !m_stop; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            ::close(fd);
        }
    }

    std::string       m_path;
    Reply             m_reply;
    int               m_listenFd = -1;
    bool              m_listening = false;
    std::atomic<bool> m_stop{false};
    std::thread       m_thread;
};

const char* kGuiNotBuilt =
    "wm2-config was not built in this tree (BUILD_CONFIG_GUI resolved to OFF, "
    "or pkg-config could not find gtk+-3.0), so there is no binary to open a "
    "window with. The display-free cases in this file still ran.";


// ---------------------------------------------------------------------------
// Reading a page's source (plan 09-07)
// ---------------------------------------------------------------------------
//
// The same shape 09-06 used for the Appearance page: a page full of widgets
// cannot be linked into a display-free binary, so what a page CARRIES is
// asserted by reading its source. What 09-07 adds is that the key list is
// DERIVED from the page rather than written out here -- every key the page
// declares gets its live case automatically, and a key added to a page with no
// live coverage cannot slip past by not being on a list in this file.

std::string sourceOf(const std::string& relativePath)
{
    return readFileOrEmpty(std::string(WM2_SOURCE_DIR) + "/" + relativePath);
}

// Every settable key whose quoted spelling appears in `source`, in
// configKeySpecs() order. `menu-entries` is not one of those specs (it is an
// ordered group rather than a single setting), so the Menu page is asked about
// separately.
std::vector<std::string> keysDeclaredIn(const std::string& source)
{
    std::vector<std::string> out;
    for (const ConfigKeySpec& spec : configKeySpecs()) {
        if (source.find("\"" + spec.name + "\"") != std::string::npos) {
            out.push_back(spec.name);
        }
    }
    return out;
}

bool contains(const std::vector<std::string>& haystack, const std::string& needle)
{
    return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

// The nine settings D-09 puts on the Behaviour page, in the order the page
// presents them. Written out ONCE, here, because "the page carries exactly
// these" is the assertion.
const std::vector<std::string>& behaviourKeys()
{
    static const std::vector<std::string> keys = {
        "click-to-focus", "raise-on-focus", "auto-raise",
        "focus-stealing-prevention",
        "auto-raise-delay", "pointer-stopped-delay", "destroy-window-delay",
        "new-window-command", "exec-using-shell",
    };
    return keys;
}


// ---------------------------------------------------------------------------
// An isolated configuration for a window manager fixture
// ---------------------------------------------------------------------------

// XDG_CONFIG_DIRS is overridden as well as XDG_CONFIG_HOME: xdgConfigDirs()
// falls back to /etc/xdg when the variable is unset, so on a machine that has a
// system-wide config the host's settings would layer under every case here.
std::string makeConfigHome(const std::string& contents)
{
    const std::string base = makeTree("wmcfg");
    ::mkdir((base + "/wm2-born-again").c_str(), 0700);
    std::ofstream out(base + "/wm2-born-again/config");
    out << contents;
    out.close();
    return base;
}

WmFixtureOptions fixtureWithConfigHome(const std::string& home)
{
    WmFixtureOptions o;
    o.childEnv["XDG_CONFIG_HOME"] = home;
    o.childEnv["XDG_CONFIG_DIRS"] = home + "/no-system-config";
    return o;
}


// ---------------------------------------------------------------------------
// The running desktop, observed through the server
// ---------------------------------------------------------------------------
//
// Ported from tests/test_wm_config_live.cpp rather than shared through a header
// on purpose: this suite needs four of that file's forty helpers, and a shared
// test-support header carrying the other thirty-six would make every one of
// them a dependency of a binary that must keep linking on a host with no
// toolkit. The project's own convention for small helpers is per translation
// unit.

void settleTick() { std::this_thread::sleep_for(std::chrono::milliseconds(20)); }

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

void settleWm(Display* d)
{
    for (int i = 0; i < 15; ++i) { pumpWm(d); settleTick(); }
}

struct Rect { int x = 0, y = 0, w = 0, h = 0; };

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

Rect rectOf(Display* d, Window w) { Rect r; serverRect(d, w, r); return r; }

Window parentOf(Display* d, Window w)
{
    Window wroot = None, parent = None, *children = nullptr;
    unsigned int n = 0;
    if (!XQueryTree(d, w, &wroot, &parent, &children, &n)) return None;
    if (children) XFree(children);
    return parent;
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

// _NET_WM_USER_TIME published as zero BEFORE the map is the spec's explicit
// "do not focus me on map", which is the cheapest way to construct a window
// that is on screen and NOT focused.
Window mapUnfocusedClient(Display* d, int x, int y, int w, int h,
                          Window& clientOut, const char* name)
{
    Window root = DefaultRootWindow(d);
    Window win = XCreateSimpleWindow(d, root, x, y,
                                     static_cast<unsigned>(w), static_cast<unsigned>(h), 0,
                                     BlackPixel(d, DefaultScreen(d)),
                                     WhitePixel(d, DefaultScreen(d)));
    if (name) XStoreName(d, win, name);
    const unsigned long zero = 0;
    XChangeProperty(d, win, XInternAtom(d, "_NET_WM_USER_TIME", False),
                    XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(&zero), 1);
    clientOut = win;
    XMapWindow(d, win);
    XSync(d, False);
    return awaitFrameFor(d, win);
}

Window activeWindow(Display* d)
{
    const Atom atom = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long nItems = 0, bytesAfter = 0;
    unsigned char* raw = nullptr;
    if (XGetWindowProperty(d, DefaultRootWindow(d), atom, 0, 1, False, XA_WINDOW,
                           &actualType, &actualFormat, &nItems, &bytesAfter,
                           &raw) != Success) {
        return None;
    }
    Window out = None;
    if (raw && actualFormat == 32 && nItems >= 1) out = *reinterpret_cast<Window*>(raw);
    if (raw) XFree(raw);
    return out;
}

Window pumpedActiveWindow(Display* d) { pumpWm(d); return activeWindow(d); }

// The one fixed wait here, and it is a wait for a NON-EVENT: an assertion of
// the form "this did not happen" has to give the window manager long enough to
// have done it.
void waitPastFocusDelays()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
}

// Where the pointer is parked so it is over neither a client nor the menu.
constexpr int kParkX = 5;
constexpr int kParkY = 5;


// ---------------------------------------------------------------------------
// Committing through the GUI's own two halves
// ---------------------------------------------------------------------------
//
// FormState decides what the value is; the GUI's own ProtocolClient carries it.
// Nothing here is a hand-rolled socket client, so a change that broke
// wm2-config's client breaks these cases too.

// What the running window manager says it is using for `key`.
std::string wmValue(ProtocolClient& client, const std::string& key)
{
    bool answered = false;
    std::string value;
    if (!client.sendGet(key, [&](const ConfigMessage& reply) {
            answered = true;
            if (reply.type == ConfigMessageType::Value) value = reply.value;
        })) {
        return std::string();
    }
    client.pumpUntil([&]() { return answered; }, 15000);
    return value;
}

// Commit `value` for `key` exactly as a page's control does: through the form,
// then through the client. Returns the window manager's refusal reason, or "".
std::string commitThroughForm(FormState& form, ProtocolClient& client,
                              const std::string& key, const std::string& value)
{
    if (!form.setValue(key, value)) return "the form refused the value";

    bool acked = false;
    std::string refusal;
    if (!client.sendSet(key, form.value(key), [&](const ConfigMessage& reply) {
            if (reply.type == ConfigMessageType::Ack) acked = true;
            if (reply.type == ConfigMessageType::Error) refusal = reply.reason;
        })) {
        return "the client refused to send";
    }
    if (!client.pumpUntil([&]() { return acked || !refusal.empty(); }, 15000)) {
        return "the window manager did not answer";
    }
    return refusal;
}

// A value for `key` that is NOT what the window manager currently reports, so
// "it changed" cannot be true of the starting state.
std::string differentValueFor(const std::string& key, const std::string& current)
{
    const ConfigKeySpec* spec = configKeySpecFor(key);
    if (!spec) return std::string();
    switch (spec->kind) {
    case ConfigValueKind::Boolean:
        return current == "true" ? "false" : "true";
    case ConfigValueKind::Integer: {
        // In range by construction, and different from whatever is in force.
        const int candidate = (current == std::to_string(spec->minValue + 37))
                                  ? spec->minValue + 91
                                  : spec->minValue + 37;
        return std::to_string(candidate);
    }
    case ConfigValueKind::String:
        // Never executed by a `set` -- new-window-command is only run when the
        // root menu's New entry is chosen -- and deliberately not a shell line.
        return current == "/bin/true" ? "/bin/echo" : "/bin/true";
    }
    return std::string();
}

}  // namespace


// =============================================================================
// The model, with no display in sight
// =============================================================================

TEST_CASE("the form manages exactly the keys the config file writer manages",
          "[wm2_config_smoke]")
{
    const std::string tree = makeTree("seed");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());

    REQUIRE(form.fields().size() == configFileManagedKeys().size());
    for (const std::string& key : configFileManagedKeys()) {
        INFO("key: " << key);
        CHECK(form.manages(key));
    }
    // Nothing has been touched, so a save would write nothing at all.
    CHECK_FALSE(form.dirty());
    CHECK(form.edits().empty());
}

TEST_CASE("a seeded field shows the built-in default when no file sets it",
          "[wm2_config_smoke]")
{
    const std::string tree = makeTree("defaults");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());

    const Config builtIn;
    CHECK(form.value("tab-background") == builtIn.tabBackground);
    CHECK(form.value("frame-thickness") == std::to_string(builtIn.frameThickness));

    const FormField* field = form.field("tab-background");
    REQUIRE(field != nullptr);
    CHECK(field->source == ValueSource::BuiltIn);
}

TEST_CASE("a value set only in the system layer is shown as effective and names its layer",
          "[wm2_config_smoke]")
{
    // DISC-08: the GUI shows the EFFECTIVE value, whichever layer produced it,
    // and the raw field's tooltip names that layer. The system file is never
    // written -- editing this key writes an override into the user file, and
    // resetting removes that override so the system value shows through again.
    const std::string tree = makeTree("systemlayer");
    writeFile(tree + "/system/wm2-born-again/config", "tab-background=#123456\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());

    const FormField* field = form.field("tab-background");
    REQUIRE(field != nullptr);
    CHECK(field->current == "#123456");
    CHECK(field->effective == "#123456");
    CHECK(field->source == ValueSource::SystemFile);
    CHECK(field->sourceDetail == tree + "/system/wm2-born-again/config");
}

TEST_CASE("an edit produces one edit and leaves every other key out of the file",
          "[wm2_config_smoke]")
{
    const std::string tree = makeTree("oneedit");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());

    REQUIRE(form.setValue("tab-background", "#ABCDEF"));
    CHECK(form.dirty());

    const std::vector<ConfigEdit> edits = form.edits();
    REQUIRE(edits.size() == 1);
    CHECK(edits[0].key == "tab-background");
    CHECK(edits[0].value == "#ABCDEF");
    CHECK_FALSE(edits[0].remove);

    // Setting the same value again is not a second edit -- which is what stops
    // an idempotent widget signal from sending a redundant set message.
    CHECK_FALSE(form.setValue("tab-background", "#ABCDEF"));
    CHECK(form.edits().size() == 1);
}

TEST_CASE("reset marks a key for removal and shows the layer below, not the built-in default",
          "[wm2_config_smoke]")
{
    // D-13, stated as an observable: "default" is what the user will actually
    // see after the key leaves their file, and on a host with a system-wide
    // configuration that is the SYSTEM value, not the compiled-in one.
    const std::string tree = makeTree("reset");
    writeFile(tree + "/system/wm2-born-again/config", "frame-thickness=11\n");
    writeFile(tree + "/user/wm2-born-again/config", "frame-thickness=23\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());
    REQUIRE(form.value("frame-thickness") == "23");

    REQUIRE(form.requestReset("frame-thickness"));
    CHECK(form.value("frame-thickness") == "11");           // the system layer
    CHECK(form.value("frame-thickness") != std::to_string(Config().frameThickness));

    const std::vector<ConfigEdit> edits = form.edits();
    REQUIRE(edits.size() == 1);
    CHECK(edits[0].key == "frame-thickness");
    CHECK(edits[0].remove);
}

TEST_CASE("typing a value after a reset cancels the reset", "[wm2_config_smoke]")
{
    const std::string tree = makeTree("resetthenedit");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());

    REQUIRE(form.requestReset("frame-thickness"));
    REQUIRE(form.setValue("frame-thickness", "9"));

    const std::vector<ConfigEdit> edits = form.edits();
    REQUIRE(edits.size() == 1);
    CHECK_FALSE(edits[0].remove);
    CHECK(edits[0].value == "9");
}

TEST_CASE("revert restores every effective value and forgets every edit",
          "[wm2_config_smoke]")
{
    const std::string tree = makeTree("revert");
    writeFile(tree + "/user/wm2-born-again/config", "tab-background=#010203\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());

    REQUIRE(form.setValue("tab-background", "#FF0000"));
    REQUIRE(form.requestReset("frame-thickness"));
    REQUIRE(form.divergentKeys().size() >= 1);

    form.revert();
    CHECK_FALSE(form.dirty());
    CHECK(form.edits().empty());
    CHECK(form.value("tab-background") == "#010203");
    CHECK(form.divergentKeys().empty());
}

TEST_CASE("the file-only banner is the sentence the phase decided on",
          "[wm2_config_smoke]")
{
    // Compared against the CONSTANT, never against a copy of the sentence
    // written out here. A test carrying its own literal would keep passing
    // after somebody edited the window's copy, which would make it a test of
    // this file rather than of the program -- and D-03 fixed the sentence
    // precisely so that it cannot quietly become something else.
    CHECK(connectionBannerText(ConnectionState::FileOnlyNoSocket, "") ==
          std::string(kFileOnlyBannerText));

    // A refused handshake says the same thing and then names why (D-15).
    const std::string refused =
        connectionBannerText(ConnectionState::FileOnlyRefused, "protocol version 99");
    CHECK(refused.rfind(kFileOnlyBannerText, 0) == 0);
    CHECK(refused.find("protocol version 99") != std::string::npos);

    // Connected is a different sentence, or the banner would say nothing.
    CHECK(connectionBannerText(ConnectionState::Connected, "") !=
          std::string(kFileOnlyBannerText));
}


// =============================================================================
// Saving: the user's file, and nothing else
// =============================================================================

TEST_CASE("a save in file-only mode writes the user file and leaves the system file alone",
          "[wm2_config_smoke]")
{
    const std::string tree = makeTree("save");
    const std::string systemFile = tree + "/system/wm2-born-again/config";
    const std::string userFile   = tree + "/user/wm2-born-again/config";
    writeFile(systemFile, "tab-background=#123456\n");
    writeFile(userFile, "# a comment the writer must preserve\nborders=#00FF00\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    struct timespec systemBefore;
    REQUIRE(modificationTime(systemFile, systemBefore));

    FormState form;
    const ConfigLayers layers = configLayersFromDisk();
    form.seedFromLayers(layers);
    REQUIRE(layers.userFilePath == userFile);

    REQUIRE(form.setValue("tab-background", "#ABCDEF"));

    std::string error;
    REQUIRE(configFileWrite(layers.userFilePath, form.edits(), {}, false, error) ==
            ConfigWriteResult::Ok);
    form.markSaved();

    // Asserted through the PARSER rather than against a spelling: the writer
    // appends "key = value" and rewrites an existing line in place keeping its
    // own spacing, so a literal match here would be a test of the writer's
    // whitespace rather than of what the window manager will read back.
    Config readBack;
    readBack.applyFile(userFile);
    CHECK(readBack.tabBackground == "#ABCDEF");
    CHECK(readBack.borders == "#00FF00");

    const std::string written = readFileOrEmpty(userFile);
    CHECK(written.find("# a comment the writer must preserve") != std::string::npos);
    CHECK(written.find("borders=#00FF00") != std::string::npos);

    // T-9-38 / D-04: the system-wide file is never opened for writing.
    struct timespec systemAfter;
    REQUIRE(modificationTime(systemFile, systemAfter));
    CHECK(sameTime(systemBefore, systemAfter));
    CHECK(readFileOrEmpty(systemFile) == "tab-background=#123456\n");

    CHECK_FALSE(form.dirty());
}

TEST_CASE("a reset key is gone from the user file after a save, and no default took its place",
          "[wm2_config_smoke]")
{
    const std::string tree = makeTree("resetsave");
    const std::string systemFile = tree + "/system/wm2-born-again/config";
    const std::string userFile   = tree + "/user/wm2-born-again/config";
    writeFile(systemFile, "frame-thickness=11\n");
    writeFile(userFile, "frame-thickness=23\ntab-background=#010203\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    const ConfigLayers layers = configLayersFromDisk();
    form.seedFromLayers(layers);
    REQUIRE(form.requestReset("frame-thickness"));

    std::string error;
    REQUIRE(configFileWrite(layers.userFilePath, form.edits(), {}, false, error) ==
            ConfigWriteResult::Ok);
    form.markSaved();

    const std::string written = readFileOrEmpty(userFile);
    CHECK(written.find("frame-thickness") == std::string::npos);
    // Not the built-in default written in its place, and not the system value
    // pinned into the user file either -- the line is simply gone, which is
    // what lets the system layer show through (D-13).
    CHECK(written.find(std::to_string(Config().frameThickness)) == std::string::npos);
    CHECK(written.find("tab-background=#010203") != std::string::npos);

    // And the window now shows what removal actually produced.
    CHECK(form.value("frame-thickness") == "11");
}

TEST_CASE("a save with nothing changed leaves the user file untouched",
          "[wm2_config_smoke]")
{
    const std::string tree = makeTree("nochange");
    const std::string userFile = tree + "/user/wm2-born-again/config";
    writeFile(userFile, "borders=#00FF00\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    const ConfigLayers layers = configLayersFromDisk();
    form.seedFromLayers(layers);

    struct timespec before;
    REQUIRE(modificationTime(userFile, before));
    // A nanosecond-resolution stamp still needs the two reads to be
    // distinguishable in principle, or "unchanged" would be trivially true.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // The whole assertion: with nothing dirty there is no edit list, and the
    // window must therefore not call the writer at all. Calling it with an
    // empty edit set would rewrite the file byte-identically and still move its
    // modification time, which is exactly the surprise a user does not want
    // from a Save they pressed by reflex.
    REQUIRE(form.edits().empty());

    struct timespec after;
    REQUIRE(modificationTime(userFile, after));
    CHECK(sameTime(before, after));
}


// =============================================================================
// The Appearance page, checked where it can be checked without a toolkit
// =============================================================================

TEST_CASE("the frame-thickness control is built from the parser's own bounds",
          "[wm2_config_smoke]")
{
    // The slider's range is not spelled 1 and 50 in the page's source; it is
    // read from configKeySpecFor("frame-thickness"), which is the same table
    // the window manager validates a `set` against. So asserting the bounds
    // here asserts what the control can ask for -- and a third hand-written
    // copy of the range is what this arrangement exists to prevent.
    const ConfigKeySpec* spec = configKeySpecFor("frame-thickness");
    REQUIRE(spec != nullptr);
    CHECK(spec->kind == ConfigValueKind::Integer);
    CHECK(spec->minValue == 1);
    CHECK(spec->maxValue == 50);
}

TEST_CASE("the font value is read through the chooser interface, not the deprecated getter",
          "[wm2_config_smoke]")
{
    // A SOURCE-LEVEL guard, the same shape the [wm_socket] suite uses to keep
    // per-window data out of the status reply. gtk_font_button_get_font_name()
    // has been deprecated since GTK 3.22 (09-RESEARCH.md Pitfall 5) and still
    // WORKS on the 3.24 both Ubuntu targets ship -- so nothing at runtime would
    // notice its return, and only a reader of the source, or this, would.
    const std::string page =
        readFileOrEmpty(std::string(WM2_SOURCE_DIR) + "/apps/wm2-config/AppearancePage.cpp");
    REQUIRE_FALSE(page.empty());

    CHECK(page.find("gtk_font_chooser_get_font(") != std::string::npos);
    CHECK(page.find("gtk_font_button_get_font_name") == std::string::npos);
    CHECK(page.find("gtk_font_button_get_font(") == std::string::npos);

    // The same rule for the colour half: the chooser interface, not the
    // button's own accessor.
    CHECK(page.find("gtk_color_chooser_get_rgba(") != std::string::npos);
    CHECK(page.find("gtk_color_button_get_rgba") == std::string::npos);
}

TEST_CASE("the Appearance page carries what D-09 assigns to it and nothing else",
          "[wm2_config_smoke]")
{
    const std::string page =
        readFileOrEmpty(std::string(WM2_SOURCE_DIR) + "/apps/wm2-config/AppearancePage.cpp");
    REQUIRE_FALSE(page.empty());

    // The nine colours, both fonts and frame thickness get a control.
    for (const char* key : {"tab-foreground", "tab-background", "frame-background",
                            "button-background", "borders", "menu-foreground",
                            "menu-background", "menu-highlight", "menu-borders",
                            "tab-font", "menu-font", "frame-thickness"}) {
        INFO("key: " << key);
        CHECK(page.find(std::string("\"") + key + "\"") != std::string::npos);
    }

    // And nothing D-09 assigns to the Behaviour page has wandered onto this
    // one. Checked by absence, which is the half a "does it have everything"
    // assertion cannot cover.
    for (const char* elsewhere : {"click-to-focus", "raise-on-focus", "auto-raise",
                                  "focus-stealing-prevention", "auto-raise-delay",
                                  "pointer-stopped-delay", "destroy-window-delay",
                                  "new-window-command", "exec-using-shell"}) {
        INFO("key that belongs on another page: " << elsewhere);
        CHECK(page.find(std::string("\"") + elsewhere + "\"") == std::string::npos);
    }
}


// =============================================================================
// The window itself
// =============================================================================

TEST_CASE("wm2-config opens a window and reports itself connected to the running window manager",
          "[wm2_config_smoke]")
{
#ifndef WM2_CONFIG_PATH
    SKIP(kGuiNotBuilt);
#else
    WmFixture fixture;
    const std::string home = makeTree("guihome");

    ChildProcess gui = spawnConfigGui(fixture.display(), {}, home);
    REQUIRE(gui.pid() > 0);

    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();

    Window win = None;
    std::string state;
    const bool appeared = WmFixture::pollUntil([&]() {
        state.clear();
        win = findConfigWindow(d, DefaultRootWindow(d), state);
        return win != None && isViewable(d, win) && !state.empty();
    }, 30000);

    INFO("wm2-config state property: '" << state << "'");
    CHECK(appeared);
    CHECK(win != None);
    CHECK(state == std::string(connectionStateName(ConnectionState::Connected)));

    gui.shutdown();   // by the PID spawnConfigGui() created, never by name
#endif
}

TEST_CASE("wm2-config opens with no socket to talk to and says so",
          "[wm2_config_smoke]")
{
#ifndef WM2_CONFIG_PATH
    SKIP(kGuiNotBuilt);
#else
    // A window manager IS running on this display -- the fixture started it --
    // but the GUI is pointed at a socket path nothing is listening on. That is
    // the file-only condition expressed without needing a second Xvfb with no
    // window manager on it, and it exercises the same branch: connect() fails,
    // the window opens anyway, and the banner is the fixed sentence.
    WmFixture fixture;
    const std::string home = makeTree("guihome-nosocket");
    const std::string absent = home + "/there-is-no-socket-here";

    ChildProcess gui = spawnConfigGui(fixture.display(),
                                      {"--socket", absent}, home);
    REQUIRE(gui.pid() > 0);

    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();

    Window win = None;
    std::string state;
    const bool appeared = WmFixture::pollUntil([&]() {
        state.clear();
        win = findConfigWindow(d, DefaultRootWindow(d), state);
        return win != None && isViewable(d, win) && !state.empty();
    }, 30000);

    INFO("wm2-config state property: '" << state << "'");
    CHECK(appeared);
    CHECK(state == std::string(connectionStateName(ConnectionState::FileOnlyNoSocket)));

    gui.shutdown();
#endif
}

TEST_CASE("wm2-config points at a stranger and says file-only, never connected",
          "[wm2_config_smoke]")
{
#ifndef WM2_CONFIG_PATH
    SKIP(kGuiNotBuilt);
#else
    // The end-to-end form of the D-15 case above: a real wm2-config, pointed at
    // a real stranger, must end up in the refused state and say so.
    //
    // The sawConnected half is deliberately WEAKER than it looks, and saying so
    // here is more useful than letting a later reader over-trust it. It watches
    // for a window that announces itself connected while the handshake is still
    // unfinished. A mutation putting that premature state back into the client
    // does NOT currently fail this case, because the window only republishes
    // its state when the client's state handler fires and the premature
    // assignment bypasses it -- so the lie is never observable through the
    // property. The assertion is kept as insurance for the day the connection
    // becomes asynchronous or the banner is driven by a poll, which is exactly
    // when it would start being observable.
    WmFixture fixture;
    const std::string home = makeTree("guihome-stranger");
    FakeServer server(shortSocketPath("guistranger"), FakeServer::Reply::Nothing);
    REQUIRE(server.listening());

    ChildProcess gui = spawnConfigGui(fixture.display(),
                                      {"--socket", server.path()}, home);
    REQUIRE(gui.pid() > 0);

    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();

    bool sawConnected = false;
    std::string state;
    const bool refused = WmFixture::pollUntil([&]() {
        std::string seen;
        const Window win = findConfigWindow(d, DefaultRootWindow(d), seen);
        if (win == None || seen.empty()) return false;
        state = seen;
        if (seen == std::string(connectionStateName(ConnectionState::Connected))) {
            sawConnected = true;
        }
        return seen == std::string(connectionStateName(ConnectionState::FileOnlyRefused));
    }, 30000);

    INFO("last state seen: '" << state << "'");
    CHECK(refused);
    CHECK_FALSE(sawConnected);

    gui.shutdown();
#endif
}

TEST_CASE("a colour committed through the form state and the protocol client reaches the desktop",
          "[wm2_config_smoke]")
{
    // The GUI's OWN two halves, driven directly: the form state decides what
    // the new value is, and the GUI's own protocol client is what carries it to
    // the running window manager. Nothing here is a hand-rolled socket client,
    // so a change that broke wm2-config's client would break this case.
    //
    // The widget-to-form-state edge -- a GtkColorButton's "color-set" signal
    // reaching FormState::setValue() -- is NOT covered here, because driving a
    // GTK widget from another process means synthesising input into a toolkit,
    // which tests the toolkit. That edge is covered by the operator's
    // screenshot review at the end of this phase, and the SUMMARY says so.
    WmFixture fixture;

    const std::string path = configSocketPath(fixture.display().c_str());
    ProtocolClient client;
    REQUIRE(client.connect(path));
    REQUIRE(client.state() == ProtocolClient::State::Connected);

    const std::string tree = makeTree("liveclient");
    ScopedXdg xdg(tree + "/user", tree + "/system");
    FormState form;
    form.seedFromLayers(configLayersFromDisk());

    // Not the default, so "it changed" cannot be true of the starting state.
    const std::string chosen = "#A1B2C3";
    REQUIRE(chosen != Config().tabBackground);
    REQUIRE(form.setValue("tab-background", chosen));

    bool acked = false;
    std::string refusal;
    REQUIRE(client.sendSet("tab-background", form.value("tab-background"),
                           [&](const ConfigMessage& reply) {
                               acked = reply.type == ConfigMessageType::Ack;
                               if (reply.type == ConfigMessageType::Error) {
                                   refusal = reply.reason;
                               }
                           }));
    REQUIRE(client.pumpUntil([&]() { return acked || !refusal.empty(); }, 15000));
    INFO("refusal: " << refusal);
    REQUIRE(acked);

    // What the running window manager now says it is drawing tabs in. The
    // pixel-level repaint for this key is proven by the [wm_config_live] suite
    // from plan 09-05; what this case adds is that the GUI's client is what
    // caused it.
    std::string reported;
    REQUIRE(client.sendGet("tab-background", [&](const ConfigMessage& reply) {
        if (reply.type == ConfigMessageType::Value) reported = reply.value;
    }));
    REQUIRE(client.pumpUntil([&]() { return !reported.empty(); }, 15000));
    CHECK(reported == chosen);
}

TEST_CASE("the protocol client reports a reason and stays file-only when nothing is listening",
          "[wm2_config_smoke]")
{
    const std::string tree = makeTree("nosocketclient");
    ProtocolClient client;
    CHECK_FALSE(client.connect(tree + "/not-a-socket"));
    CHECK(client.state() == ProtocolClient::State::NoSocket);
    CHECK_FALSE(client.reason().empty());
    CHECK(client.fileDescriptor() < 0);
}


// =============================================================================
// D-15: the GUI never sends settings to a stranger
// =============================================================================

TEST_CASE("a peer that accepts the connection and says nothing is a stranger, not an absent window manager",
          "[wm2_config_smoke]")
{
    // The two file-only states are not interchangeable. "Nothing is listening"
    // is an ordinary Tuesday on a droplet with no desktop open; "something is
    // listening on the window manager's socket path and will not say what it
    // is" is worth a person's attention, and the banner has to be able to tell
    // them apart (D-15).
    FakeServer server(shortSocketPath("silent"), FakeServer::Reply::Nothing);
    REQUIRE(server.listening());

    ProtocolClient client;
    CHECK_FALSE(client.connect(server.path()));
    CHECK(client.state() == ProtocolClient::State::Refused);
    CHECK_FALSE(client.reason().empty());
    CHECK(client.fileDescriptor() < 0);   // nothing was sent, and nothing will be
}

TEST_CASE("a peer speaking a protocol version this build does not is refused by name",
          "[wm2_config_smoke]")
{
    FakeServer server(shortSocketPath("wrongver"), FakeServer::Reply::WrongVersion);
    REQUIRE(server.listening());

    ProtocolClient client;
    CHECK_FALSE(client.connect(server.path()));
    CHECK(client.state() == ProtocolClient::State::Refused);
    INFO("reason: " << client.reason());
    CHECK(client.reason().find("99") != std::string::npos);

    // And the banner a person actually reads carries that reason on top of the
    // fixed sentence, rather than replacing it.
    const std::string banner =
        connectionBannerText(ConnectionState::FileOnlyRefused, client.reason());
    CHECK(banner.rfind(kFileOnlyBannerText, 0) == 0);
    CHECK(banner.find("99") != std::string::npos);
}

TEST_CASE("writing an empty edit set WOULD move the file's modification time",
          "[wm2_config_smoke]")
{
    // The mutation test for the case above. "Save with nothing changed leaves
    // the file untouched" would pass just as happily against a writer that
    // never touched the file at all, which would make it a test of nothing.
    // This case proves the opposite arm: handing the writer an empty edit set
    // DOES rewrite the file and DOES move its modification time -- so the
    // window's decision not to call it is load-bearing, not incidental.
    const std::string tree = makeTree("emptyedits");
    const std::string userFile = tree + "/user/wm2-born-again/config";
    writeFile(userFile, "borders=#00FF00\n");

    struct timespec before;
    REQUIRE(modificationTime(userFile, before));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    std::string error;
    REQUIRE(configFileWrite(userFile, {}, {}, false, error) == ConfigWriteResult::Ok);

    struct timespec after;
    REQUIRE(modificationTime(userFile, after));
    CHECK_FALSE(sameTime(before, after));
    CHECK(readFileOrEmpty(userFile) == "borders=#00FF00\n");   // same bytes, new stamp
}


// =============================================================================
// The Behaviour page (D-09, D-05, plan 09-07)
// =============================================================================
//
// The page is a file full of widgets, so what it CARRIES is read from its
// source -- the shape 09-06 established. What is different here is that the key
// list the live cases below iterate is DERIVED from that source rather than
// written out in this file: a setting added to the page gets a live case for
// free, and a setting added with no live path cannot slip past by not being on
// a list here.
//
// WHAT THESE CASES DO AND DO NOT PROVE, said plainly. The tracer case proves
// the whole chain for click-to-focus with real pointer input: the page's model,
// the page's socket client, and a focus behaviour that visibly changes. The
// remaining eight settings are proven here as far as the window manager
// ADOPTING the value through the GUI's own two halves; that adopting the value
// changes an observable behaviour is owned, per setting, by the [wm_config_live]
// suite plan 09-05 built, and 09-07's SUMMARY names the case for each. Copying
// those eight XTEST cases into this file would have been a second copy of a
// suite rather than a second proof.

TEST_CASE("the Behaviour page carries what D-09 assigns to it and nothing else",
          "[wm2_config_smoke]")
{
    const std::string page = sourceOf("apps/wm2-config/BehaviourPage.cpp");
    REQUIRE_FALSE(page.empty());

    const std::vector<std::string> declared = keysDeclaredIn(page);
    INFO("keys the page declares: " << declared.size());
    for (const std::string& key : behaviourKeys()) {
        INFO("key D-09 puts on the Behaviour page: " << key);
        CHECK(contains(declared, key));
    }

    // And nothing D-09 assigns elsewhere has wandered onto this page. Checked
    // by absence, which is the half a "does it have everything" assertion
    // cannot cover.
    CHECK(declared.size() == behaviourKeys().size());
    for (const std::string& key : declared) {
        INFO("key the page declares: " << key);
        CHECK(contains(behaviourKeys(), key));
    }
}

TEST_CASE("the Appearance and Behaviour pages partition the settable keys between them",
          "[wm2_config_smoke]")
{
    // The two pages together must account for every single setting the option
    // table declares, or a key exists that no page can edit -- which is how a
    // settings window quietly stops being able to set something. D-09 assigns
    // every one of them to one of the two, and the Menu page carries no single
    // settings at all.
    const std::vector<std::string> appearance =
        keysDeclaredIn(sourceOf("apps/wm2-config/AppearancePage.cpp"));
    const std::vector<std::string> behaviour =
        keysDeclaredIn(sourceOf("apps/wm2-config/BehaviourPage.cpp"));

    for (const ConfigKeySpec& spec : configKeySpecs()) {
        const bool onAppearance = contains(appearance, spec.name);
        const bool onBehaviour  = contains(behaviour, spec.name);
        INFO("settable key: " << spec.name);
        CHECK((onAppearance || onBehaviour));
        CHECK_FALSE((onAppearance && onBehaviour));
    }
}

TEST_CASE("every Behaviour control's wording is built from the option table's own summary",
          "[wm2_config_smoke]")
{
    // The acceptance criterion is that no label contradicts the summary
    // `--help` prints. That is satisfied STRUCTURALLY rather than by comparing
    // two strings: the tooltip is assembled at runtime from
    // configKeySpecFor(key)->summary, so the window and `--help` cannot come to
    // describe one setting in two ways. The label above it is a short human
    // phrase, and the SUMMARY lists every pair.
    const std::string page = sourceOf("apps/wm2-config/BehaviourPage.cpp");
    REQUIRE_FALSE(page.empty());

    CHECK(page.find("configKeySpecFor(") != std::string::npos);
    CHECK(page.find("->summary") != std::string::npos);

    // Every key the page carries HAS a summary to build that tooltip from, or
    // the structural guarantee is empty for it.
    for (const std::string& key : behaviourKeys()) {
        const ConfigKeySpec* spec = configKeySpecFor(key);
        INFO("key: " << key);
        REQUIRE(spec != nullptr);
        CHECK_FALSE(spec->summary.empty());
    }
}

TEST_CASE("the shell flag's label states that the command is shell-evaluated",
          "[wm2_config_smoke]")
{
    // T-9-41: the checkbox is where a user learns what turning this on means.
    // Naming the flag would tell them nothing; the consequence is the label.
    const std::string page = sourceOf("apps/wm2-config/BehaviourPage.cpp");
    REQUIRE_FALSE(page.empty());

    const std::size_t at = page.find("\"exec-using-shell\"");
    REQUIRE(at != std::string::npos);

    // Within the row that declares the key, not merely somewhere in the file.
    const std::size_t from = at;
    const std::size_t to = std::min(page.size(), at + 1400);
    const std::string row = page.substr(from, to - from);
    INFO("exec-using-shell row:\n" << row);
    CHECK(row.find("shell") != std::string::npos);
    CHECK(row.find("/bin/sh") != std::string::npos);
}

TEST_CASE("the delay controls are built from the parser's own bounds, not from a third copy",
          "[wm2_config_smoke]")
{
    // Same arrangement as the frame-thickness slider: the range is read from
    // the table the window manager validates a `set` against, so the control
    // cannot ask for a value the window manager will refuse.
    for (const char* key : {"auto-raise-delay", "pointer-stopped-delay",
                            "destroy-window-delay"}) {
        const ConfigKeySpec* spec = configKeySpecFor(key);
        INFO("delay: " << key);
        REQUIRE(spec != nullptr);
        CHECK(spec->kind == ConfigValueKind::Integer);
        CHECK(spec->minValue == 1);
        CHECK(spec->maxValue == 60000);
    }

    const std::string page = sourceOf("apps/wm2-config/BehaviourPage.cpp");
    REQUIRE_FALSE(page.empty());
    // A fourth hand-written copy of the bound is what this arrangement exists
    // to prevent, so the numbers must not appear in the page at all.
    CHECK(page.find("60000") == std::string::npos);
}


// =============================================================================
// The Behaviour page, against a running desktop
// =============================================================================

TEST_CASE("click-to-focus committed through the page's model changes how the desktop gives focus",
          "[wm2_config_smoke]")
{
    // THE TRACER. The whole chain for one setting: the GUI's own FormState
    // decides the value, the GUI's own ProtocolClient carries it, and real
    // synthesised pointer input shows that focus stopped following the pointer.
    // A value echo would pass against a window manager that stored the setting
    // and never consulted it; this cannot.
    //
    // Bound to the page: if the Behaviour page does not carry this key, this
    // case is proving something about FormState and the protocol client rather
    // than about the control a user actually presses, and it says so instead of
    // passing.
    REQUIRE(contains(keysDeclaredIn(sourceOf("apps/wm2-config/BehaviourPage.cpp")),
                     "click-to-focus"));

    const std::string home = makeConfigHome(
        "click-to-focus=false\nauto-raise=true\nauto-raise-delay=50\n");
    WmFixture fixture(fixtureWithConfigHome(home));

    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    ProtocolClient client;
    REQUIRE(client.connect(configSocketPath(fixture.display().c_str())));

    const std::string tree = makeTree("behaviourtracer");
    ScopedXdg xdg(tree + "/user", tree + "/system");
    FormState form;
    form.seedFromLayers(configLayersFromDisk());

    // --- the control half: with the file's click-to-focus=false the pointer
    // alone focuses. Without this the negative below could pass on a window
    // manager whose pointer focus never worked at all.
    Window first = None;
    REQUIRE(mapUnfocusedClient(d, 160, 140, 280, 200, first, "pointer") != None);
    REQUIRE(pumpedActiveWindow(d) != first);

    const Rect firstRect = rectOf(d, first);
    driver.moveTo(firstRect.x + firstRect.w / 2, firstRect.y + firstRect.h / 2);
    const bool focusedByPointer =
        WmFixture::pollUntil([&] { return pumpedActiveWindow(d) == first; }, 8000);
    INFO("wm stderr:\n" << fixture.wmStderr());
    REQUIRE(focusedByPointer);

    driver.moveTo(kParkX, kParkY);
    settleWm(d);

    // --- the flip, through the page's own two halves ------------------------
    const std::string refusal = commitThroughForm(form, client, "click-to-focus", "true");
    INFO("refusal: " << refusal);
    REQUIRE(refusal.empty());

    Window second = None;
    REQUIRE(mapUnfocusedClient(d, 520, 340, 280, 200, second, "click") != None);
    REQUIRE(pumpedActiveWindow(d) != second);

    const Rect secondRect = rectOf(d, second);
    driver.moveTo(secondRect.x + secondRect.w / 2, secondRect.y + secondRect.h / 2);
    waitPastFocusDelays();
    settleWm(d);
    const Window afterEnter = activeWindow(d);

    // ...and a CLICK on the same spot still focuses it, so what changed is the
    // route and not the window manager's ability to focus anything.
    driver.press(Button1);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    driver.release(Button1);
    const bool focusedByClick =
        WmFixture::pollUntil([&] { return pumpedActiveWindow(d) == second; }, 8000);

    INFO("wm stderr:\n" << fixture.wmStderr());
    INFO("active after pointer entry: " << afterEnter << " second: " << second);
    CHECK(afterEnter != second);
    CHECK(focusedByClick);

    // And the window manager reports what the form asked for, so the page and
    // the desktop agree about what happened.
    CHECK(wmValue(client, "click-to-focus") == "true");
}

TEST_CASE("every Behaviour setting committed through the page's model reaches the running window manager",
          "[wm2_config_smoke]")
{
    // One fixture for all nine, on purpose: nine fixtures would be nine Xvfb
    // servers and nine window managers for one question that is the same
    // question each time. The keys are read from the page's own source, so this
    // covers whatever the page actually carries rather than whatever this file
    // remembers it carrying.
    const std::vector<std::string> keys =
        keysDeclaredIn(sourceOf("apps/wm2-config/BehaviourPage.cpp"));
    REQUIRE(keys.size() == behaviourKeys().size());

    WmFixture fixture;
    ProtocolClient client;
    REQUIRE(client.connect(configSocketPath(fixture.display().c_str())));

    const std::string tree = makeTree("behaviourall");
    ScopedXdg xdg(tree + "/user", tree + "/system");
    FormState form;
    form.seedFromLayers(configLayersFromDisk());

    for (const std::string& key : keys) {
        INFO("setting: " << key);
        const std::string before = wmValue(client, key);
        const std::string wanted = differentValueFor(key, before);
        REQUIRE_FALSE(wanted.empty());
        REQUIRE(wanted != before);

        const std::string refusal = commitThroughForm(form, client, key, wanted);
        INFO("refusal: " << refusal);
        CHECK(refusal.empty());
        CHECK(wmValue(client, key) == wanted);
    }

    INFO("wm stderr:\n" << fixture.wmStderr());
}

TEST_CASE("a delay outside the parser's range is refused, so the control's clamp is a convenience and not the validation",
          "[wm2_config_smoke]")
{
    // The control clamps because a user should not be able to ask for
    // something that will be refused. But the clamp is NOT the validation: the
    // window manager validates every set regardless, and a GUI that were the
    // only validator would be a GUI whose bugs became the window manager's.
    WmFixture fixture;
    ProtocolClient client;
    REQUIRE(client.connect(configSocketPath(fixture.display().c_str())));

    for (const char* key : {"auto-raise-delay", "pointer-stopped-delay",
                            "destroy-window-delay"}) {
        const ConfigKeySpec* spec = configKeySpecFor(key);
        REQUIRE(spec != nullptr);
        const std::string before = wmValue(client, key);

        for (const std::string& outOfRange : {std::to_string(spec->minValue - 1),
                                              std::to_string(spec->maxValue + 1)}) {
            INFO("key: " << key << " value: " << outOfRange);
            bool acked = false;
            std::string refusal;
            REQUIRE(client.sendSet(key, outOfRange, [&](const ConfigMessage& reply) {
                if (reply.type == ConfigMessageType::Ack) acked = true;
                if (reply.type == ConfigMessageType::Error) refusal = reply.reason;
            }));
            REQUIRE(client.pumpUntil([&]() { return acked || !refusal.empty(); }, 15000));
            CHECK_FALSE(acked);
            CHECK_FALSE(refusal.empty());
        }

        CHECK(wmValue(client, key) == before);
    }
}
