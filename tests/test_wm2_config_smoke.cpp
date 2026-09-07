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
#include "../apps/wm2-config/MenuModel.h"
#include "../apps/wm2-config/ProtocolClient.h"

// The Appearance page's COLOUR VOCABULARY, compiled into this binary only where
// the GUI itself was built (X1). Nothing here opens a display: gdk_rgba_parse()
// is a string parser, and the canonicaliser over it is a pure function. Every
// case that names it says so with a SKIP where the toolkit is absent, so the
// nogtk tree registers the same suite this one does.
#ifdef WM2_CONFIG_PATH
#include "../apps/wm2-config/AppearancePage.h"
#endif

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
        WrongVersion,  // a well-formed hello-ack naming a version nobody speaks
        WrongProgram   // a well-formed hello-ack at OUR version, from something
                       // that is not the window manager (T-9-34)
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

            if (m_reply == FakeServer::Reply::WrongVersion ||
                m_reply == FakeServer::Reply::WrongProgram) {
                ConfigMessage ack;
                ack.type = ConfigMessageType::HelloAck;
                ack.program = "not-really-a-window-manager";
                // WrongProgram speaks OUR version: the only thing wrong with
                // it is who it says it is, which is what T-9-34 is about.
                ack.protocol = m_reply == FakeServer::Reply::WrongVersion
                                   ? 99 : kConfigProtocolVersion;
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

// Source with every `//` comment removed, for a guard that is about what the
// code does rather than about what its author wrote next to it.
std::string withoutLineComments(const std::string& source)
{
    std::string out;
    std::size_t line = 0;
    while (line < source.size()) {
        const std::size_t eol = source.find('\n', line);
        const std::size_t stop = (eol == std::string::npos) ? source.size() : eol;
        const std::string text = source.substr(line, stop - line);
        const std::size_t comment = text.find("//");
        out += (comment == std::string::npos) ? text : text.substr(0, comment);
        out += "\n";
        if (eol == std::string::npos) break;
        line = eol + 1;
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
// The root menu, opened with a real button press
// ---------------------------------------------------------------------------
//
// WindowManager::menu() runs a NESTED event loop with its own pointer grab, so
// while the menu is up the window manager is not in loop() and cannot be
// pumped. Everything observed here is read straight from the server while the
// press is still held.
//
// The painted-pixel stage test_wm_config_live.cpp adds is deliberately NOT
// ported: what a menu-entry case here needs is that the popup grew by the row
// the entry brought with it, which is geometry. Colour is that suite's
// question.

std::vector<Window> childrenOf(Display* d, Window w)
{
    Window wroot = None, parent = None, *children = nullptr;
    unsigned int n = 0;
    std::vector<Window> out;
    if (!XQueryTree(d, w, &wroot, &parent, &children, &n)) return out;
    if (children) { out.assign(children, children + n); XFree(children); }
    return out;
}

// Identified by the press point it is anchored on, never by "the first viewable
// child of root" -- 08.5-13 removed exactly that fallback after it silently
// sampled a client frame.
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

bool openRootMenuGeometry(Display* d, XTestDriver& driver, Rect& rectOut)
{
    driver.moveTo(kMenuPressX, kMenuPressY);
    driver.press(Button1);

    Window menu = None;
    if (!WmFixture::pollUntil([&] {
            menu = findOpenMenu(d, kMenuPressX, kMenuPressY);
            return menu != None;
        }, 20000)) {
        return false;
    }
    return WmFixture::pollUntil([&] {
        return serverRect(d, menu, rectOut) && rectOut.w > 1 && rectOut.h > 1;
    }, 20000);
}

// Close a menu the press above left open, by releasing outside every row.
void closeRootMenu(Display* d, XTestDriver& driver)
{
    driver.moveTo(kParkX, kParkY);
    driver.release(Button1);
    XSync(d, False);
    settleWm(d);
}



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


// The manual menu entries the window manager currently holds, and the
// categories it says its root menu shows.
std::vector<AppEntry> wmMenuEntries(ProtocolClient& client)
{
    std::vector<AppEntry> out;
    std::string ignored;
    parseMenuEntriesValue(wmValue(client, kMenuEntriesKey), out, ignored);
    return out;
}

// Send the whole list, wholesale, exactly as the page's Add / Edit / Remove do.
std::string commitMenuEntries(ProtocolClient& client,
                              const std::vector<AppEntry>& entries)
{
    Config rendered;
    rendered.manualMenuEntries = entries;
    const std::string value = configMenuEntriesValue(rendered);

    bool acked = false;
    std::string refusal;
    if (!client.sendSet(kMenuEntriesKey, value, [&](const ConfigMessage& reply) {
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

TEST_CASE("a key the user file sets is shown as coming from the user file, whatever its value",
          "[wm2_config_smoke]")
{
    // C5 (Codex pass 4). Provenance was decided by COMPARING the value with the
    // user layer against the value without it, so a user file that explicitly
    // set a key to the value the layer below already produced was classified as
    // built-in or system-provided. The tooltip then named the wrong source, and
    // a later change to the system layer would have made that explicit override
    // baffling. Whether a file CONTAINS a key is a question about the file, not
    // about its value.
    const std::string tree = makeTree("userprovenance");
    const std::string systemFile = tree + "/system/wm2-born-again/config";
    const std::string userFile   = tree + "/user/wm2-born-again/config";

    const Config builtIn;
    writeFile(systemFile, "tab-background=#123456\n");
    writeFile(userFile,
              // ...the same value the system layer already produces...
              "tab-background = #123456\n"
              // ...and the built-in default, spelled out.
              "frame-thickness=" + std::to_string(builtIn.frameThickness) + "\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    const ConfigLayers layers = configLayersFromDisk();
    form.seedFromLayers(layers);

    const FormField* shadowed = form.field("tab-background");
    REQUIRE(shadowed != nullptr);
    CHECK(shadowed->effective == "#123456");
    CHECK(shadowed->source == ValueSource::UserFile);
    CHECK(shadowed->sourceDetail == userFile);

    const FormField* defaulted = form.field("frame-thickness");
    REQUIRE(defaulted != nullptr);
    CHECK(defaulted->effective == std::to_string(builtIn.frameThickness));
    CHECK(defaulted->source == ValueSource::UserFile);
    CHECK(defaulted->sourceDetail == userFile);

    // The layered read is what knows this, and it knows it by reading the
    // file's key NAMES with the parser's own line rules -- not by re-deriving
    // it from a value.
    CHECK(std::find(layers.userFileKeys.begin(), layers.userFileKeys.end(),
                    "tab-background") != layers.userFileKeys.end());
    CHECK(std::find(layers.userFileKeys.begin(), layers.userFileKeys.end(),
                    "menu-background") == layers.userFileKeys.end());

    // ...and the other two verdicts are untouched: a key only the system file
    // sets still names the system file, and a key nobody sets is still
    // built-in.
    const FormField* systemOnly = form.field("menu-background");
    REQUIRE(systemOnly != nullptr);
    CHECK(systemOnly->source == ValueSource::BuiltIn);
    CHECK(systemOnly->sourceDetail.empty());
}

TEST_CASE("the user file's key names are read with the config parser's own line rules",
          "[wm2_config_smoke]")
{
    // The helper the layered read leans on, exercised where its edge cases are
    // visible: a comment, a blank line, a line with no '=', and the whitespace
    // the parser trims off a key.
    const std::string tree = makeTree("userkeys");
    const std::string userFile = tree + "/user/wm2-born-again/config";
    writeFile(userFile,
              "# tab-foreground=#FFFFFF\n"
              "\n"
              "   \t  borders   =   #00FF00\n"
              "this line has no equals sign\n"
              "menu-entry-name=Mail\n");

    const std::vector<std::string> keys = configFileKeysIn(userFile);
    INFO("keys: " << keys.size());
    CHECK(std::find(keys.begin(), keys.end(), "borders") != keys.end());
    CHECK(std::find(keys.begin(), keys.end(), "menu-entry-name") != keys.end());
    // The commented-out key is not set by this file...
    CHECK(std::find(keys.begin(), keys.end(), "tab-foreground") == keys.end());
    // ...and neither the blank line nor the '='-less one contributed anything.
    CHECK(keys.size() == 2u);

    // A file that is not there sets nothing, which is the ordinary state of a
    // machine with no user configuration.
    CHECK(configFileKeysIn(tree + "/user/wm2-born-again/absent").empty());
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

TEST_CASE("putting a refused value back leaves nothing for a save to write",
          "[wm2_config_smoke]")
{
    // X1's second half, at the level where it is decidable without a screen.
    //
    // A live `set` the window manager REFUSES used to write a line into the
    // status bar and change nothing else, so the refused value stayed in the
    // form, stayed dirty, and the next Save wrote it into the user's file. For
    // a tab colour that is not a file disagreeing with a desktop: the window
    // manager refuses a colour the X server cannot allocate, and
    // Border::allocateXftColors() calls fatal() on the same colour at the next
    // startup, so the refusal-then-save produced a desktop that would not come
    // up.
    //
    // The window's answer is to put the field back to the value IN FORCE, which
    // is setValue(key, effective). This is the property that makes that work:
    // the model's own dirty rule is "a save would write something", so putting
    // a field back to the effective value leaves the edit list EMPTY -- there is
    // then nothing for a save to write, whatever the user typed a moment ago.
    const std::string tree = makeTree("refusedputback");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());

    const FormField* field = form.field("tab-background");
    REQUIRE(field != nullptr);
    const std::string inForce = field->effective;

    // The user types something. It is dirty, and a save would write it -- which
    // is exactly the state the window manager's refusal arrives in.
    REQUIRE(form.setValue("tab-background", "#010203"));
    REQUIRE(form.field("tab-background")->dirty);
    REQUIRE(form.edits().size() == 1);

    // ...and the refusal puts it back.
    REQUIRE(form.setValue("tab-background", inForce));
    CHECK(form.value("tab-background") == inForce);
    CHECK_FALSE(form.field("tab-background")->dirty);
    CHECK(form.edits().empty());
    CHECK_FALSE(form.dirty());
}


TEST_CASE("a saved field's tooltip names the file it is now set in",
          "[wm2_config_smoke]")
{
    // A2 (CodeRabbit, apps chunk). markSaved() reassigned each saved field's
    // `source` -- UserFile after an edit, BuiltIn or SystemFile after a reset --
    // and left `sourceDetail` exactly as it was. So the DISC-08 tooltip read
    // "Set in your own configuration file, ." with an empty path after the
    // first save of a fresh edit, and went on naming the USER file after a
    // reset that had just removed the key from it.
    const std::string tree = makeTree("savedorigin");
    const std::string systemFile = tree + "/system/wm2-born-again/config";
    const std::string userFile   = tree + "/user/wm2-born-again/config";

    const Config builtIn;
    writeFile(systemFile, "tab-background=#123456\n");
    writeFile(userFile,
              "tab-background=#ABCDEF\n"
              "borders=#00FF00\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());

    // A FRESH EDIT of a key nothing sets: BuiltIn with no detail before, and
    // the user file with its path after.
    const FormField* fresh = form.field("menu-highlight");
    REQUIRE(fresh != nullptr);
    REQUIRE(fresh->source == ValueSource::BuiltIn);
    REQUIRE(fresh->sourceDetail.empty());
    REQUIRE(form.setValue("menu-highlight", "#010203"));

    // A RESET of a key the system layer also sets: the system file shows
    // through, so the tooltip must name the system file.
    REQUIRE(form.requestReset("tab-background"));

    // A RESET of a key only the user file sets: the built-in default shows
    // through, and nothing sets it any more.
    REQUIRE(form.requestReset("borders"));

    form.markSaved();

    const FormField* edited = form.field("menu-highlight");
    REQUIRE(edited != nullptr);
    CHECK(edited->source == ValueSource::UserFile);
    CHECK(edited->sourceDetail == userFile);

    const FormField* toSystem = form.field("tab-background");
    REQUIRE(toSystem != nullptr);
    CHECK(toSystem->effective == "#123456");
    CHECK(toSystem->source == ValueSource::SystemFile);
    CHECK(toSystem->sourceDetail == systemFile);

    const FormField* toBuiltIn = form.field("borders");
    REQUIRE(toBuiltIn != nullptr);
    CHECK(toBuiltIn->effective == builtIn.borders);
    CHECK(toBuiltIn->source == ValueSource::BuiltIn);
    CHECK(toBuiltIn->sourceDetail.empty());
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

TEST_CASE("a colour is canonicalised to the X11 spelling before it enters the form",
          "[wm2_config_smoke]")
{
#ifndef WM2_CONFIG_PATH
    SKIP(kGuiNotBuilt);
#else
    // X1 (Codex pass 5). The raw field accepted any spelling gdk_rgba_parse()
    // accepted and then committed it AS TYPED. GDK's grammar is not
    // XParseColor's: it takes CSS -- rgb(200,202,204), rgba(...) and more --
    // and the X server takes none of it. So a CSS colour was accepted by the
    // form, refused by the running window manager (whose refusal only wrote a
    // line into the status bar), kept by the form regardless, written to the
    // user's file by Save, and then handed at the next startup to
    // Border::allocateXftColors(), which calls fatal() on a tab colour the
    // server cannot parse. A colour somebody typed could stop the desktop from
    // starting at all.
    //
    // The gate is a pure function, which is why this case can call it on a host
    // with no X server: string in, the X11 spelling of the same colour out.

    struct Case { const char* typed; const char* canonical; };

    // CSS, which is the whole point: GDK reads it and XParseColor never will.
    // 200/202/204 are exactly 0xC8/0xCA/0xCC, so the expected answer is
    // arithmetic rather than a value read off a run.
    const Case accepted[] = {
        {"rgb(200,202,204)", "#C8CACC"},
        // Already the right SHAPE, but lower case; canonicalising means one
        // spelling of one colour, so the form and the file cannot disagree
        // about a value neither of them changed.
        {"#c8cacc",          "#C8CACC"},
        // The three-digit form XParseColor reads as 4-bit channels: #abc is
        // #AABBCC, not #0A0B0C.
        {"#abc",             "#AABBCC"},
        // A NAME. Deciding a name is safe to keep as a name would mean asking
        // XParseColor, which needs a display this program has no reason to
        // open -- so names are canonicalised too. The conversion is exact:
        // SteelBlue is 70/130/180 in rgb.txt, and 0x46/0x82/0xB4 is the same
        // colour.
        {"SteelBlue",        "#4682B4"},
        // ...and one whose channels are equal, so a transposition in the
        // formatter could not hide behind them.
        {"gray20",           "#333333"},
    };

    for (const Case& c : accepted) {
        INFO("typed: " << c.typed);
        std::string canonical;
        REQUIRE(configCanonicalColour(c.typed, canonical));
        CHECK(canonical == c.canonical);

        // AND IT IS A FIXED POINT. Committing a colour, reading it back and
        // committing it again must not walk the value anywhere.
        std::string again;
        REQUIRE(configCanonicalColour(canonical, again));
        CHECK(again == canonical);
    }

    // A spelling neither grammar knows is refused, and `out` is left alone --
    // the raw field tells "the user is mid-word" from "the user meant black"
    // by exactly that.
    for (const char* rejected : {"not a colour", "", "#gg0000", "rgb(200,202"}) {
        INFO("typed: '" << rejected << "'");
        std::string out = "untouched";
        CHECK_FALSE(configCanonicalColour(rejected, out));
        CHECK(out == "untouched");
    }

    // The chooser's own path already spelled its answer this way, and the two
    // paths must agree or the button and the field would write different files
    // for the same colour.
    GdkRGBA picked;
    REQUIRE(rgbaFromConfigColour("rgb(200,202,204)", picked));
    CHECK(configColourFromRgba(picked) == "#C8CACC");
#endif
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

TEST_CASE("the thickness slider keeps its own tooltip when the origin line is added",
          "[wm2_config_smoke]")
{
    // A3 (CodeRabbit, apps chunk). Every Appearance row but one has a RAW
    // field, and DISC-08's origin line is appended to that field's own
    // sentence. The thickness row has no raw field, so the origin line went
    // onto the slider itself -- with gtk_widget_set_tooltip_text(), which
    // REPLACES -- and the sentence addThicknessRow() had put there ("How thick
    // a window's frame is, in pixels...") was gone the first time the row was
    // rendered, which is before the window is ever shown.
    //
    // A GTK tooltip cannot be read on a host with no toolkit, so the guard is
    // on the source, comment-stripped, in the shape this file already uses for
    // the reload path.
    const std::string page = sourceOf("apps/wm2-config/AppearancePage.cpp");
    REQUIRE_FALSE(page.empty());

    // The row's own sentence is STORED where it is set, so there is one copy of
    // it rather than one in the constructor and another in the renderer.
    const std::size_t addAt = page.find("void AppearancePage::addThicknessRow(");
    REQUIRE(addAt != std::string::npos);
    const std::size_t addEnd = page.find("\n}\n", addAt);
    REQUIRE(addEnd != std::string::npos);
    const std::string addBody = withoutLineComments(page.substr(addAt, addEnd - addAt));
    INFO("addThicknessRow, comments stripped:\n" << addBody);
    CHECK(addBody.find("baseTooltip") != std::string::npos);

    // ...and the renderer APPENDS the origin to it rather than replacing it.
    const std::size_t renderAt = page.find("void AppearancePage::renderRow(");
    REQUIRE(renderAt != std::string::npos);
    const std::size_t renderEnd = page.find("\n}\n", renderAt);
    REQUIRE(renderEnd != std::string::npos);
    const std::string renderBody =
        withoutLineComments(page.substr(renderAt, renderEnd - renderAt));
    INFO("renderRow, comments stripped:\n" << renderBody);
    CHECK(renderBody.find("baseTooltip") != std::string::npos);
    // The bare replacement is named explicitly, so the loser is part of the
    // assertion rather than merely absent from it.
    CHECK(renderBody.find("gtk_widget_set_tooltip_text(row.chooser, origin.c_str())") ==
          std::string::npos);
}

TEST_CASE("a colour reaches the form canonicalised, and a refused one is put back",
          "[wm2_config_smoke]")
{
    // X1's OTHER HALF, which is GTK wiring rather than a value, and so is
    // guarded on the source in the shape this file already uses for the
    // thickness tooltip. Comment-stripped, because a comment that quotes the
    // token a grep-shaped guard forbids defeats the guard (D-33).
    const std::string page = sourceOf("apps/wm2-config/AppearancePage.cpp");
    REQUIRE_FALSE(page.empty());

    const std::size_t commitAt = page.find("void AppearancePage::commitRawField(");
    REQUIRE(commitAt != std::string::npos);
    const std::size_t commitEnd = page.find("\n}\n", commitAt);
    REQUIRE(commitEnd != std::string::npos);
    const std::string commitBody =
        withoutLineComments(page.substr(commitAt, commitEnd - commitAt));
    INFO("commitRawField, comments stripped:\n" << commitBody);

    // A TYPED COLOUR GOES THROUGH THE GATE...
    CHECK(commitBody.find("configCanonicalColour(") != std::string::npos);
    // ...and the value that reaches the form is the canonical one. The loser is
    // named explicitly, so it is part of the assertion rather than merely
    // absent from it: `commit(*row, typed)` on the colour arm is exactly what
    // let a CSS spelling through.
    CHECK(commitBody.find("commit(*row, canonical)") != std::string::npos);

    // The window's own half: a live `set` the window manager REFUSED must put
    // the form back to what is actually in force, or Save would write a value
    // the desktop has already rejected -- and for a tab colour that is a
    // desktop that will not start next time.
    const std::string window = sourceOf("apps/wm2-config/main.cpp");
    REQUIRE_FALSE(window.empty());

    const std::size_t applyAt = window.find("void applyLive(");
    REQUIRE(applyAt != std::string::npos);
    const std::size_t applyEnd = window.find("\n    }\n", applyAt);
    REQUIRE(applyEnd != std::string::npos);
    const std::string applyBody =
        withoutLineComments(window.substr(applyAt, applyEnd - applyAt));
    INFO("applyLive, comments stripped:\n" << applyBody);

    CHECK(applyBody.find("restoreRefused(") != std::string::npos);

    // ...and what "put back" means is the value that was IN FORCE WHEN THE SET
    // WAS SENT, captured here and carried to the handler (Y1). Reading
    // `effective` inside the handler instead was the defect: a Save landing
    // while the request was outstanding makes `effective` the refused value.
    CHECK(applyBody.find("->effective") != std::string::npos);
    CHECK(applyBody.find("restoreRefused(k, inForceAtSend)") != std::string::npos);

    const std::size_t restoreAt = window.find("void restoreRefused(");
    REQUIRE(restoreAt != std::string::npos);
    const std::size_t restoreEnd = window.find("\n    }\n", restoreAt);
    REQUIRE(restoreEnd != std::string::npos);
    const std::string restoreBody =
        withoutLineComments(window.substr(restoreAt, restoreEnd - restoreAt));
    INFO("restoreRefused, comments stripped:\n" << restoreBody);

    // The value comes from the caller, and the loser is named explicitly so it
    // is part of the assertion rather than merely absent from it: reading the
    // field's `effective` here is exactly what restored a refused colour.
    CHECK(restoreBody.find("inForceAtSend") != std::string::npos);
    CHECK(restoreBody.find("->effective") == std::string::npos);
    CHECK(restoreBody.find("refreshPages()") != std::string::npos);
}


TEST_CASE("the window's own reload request refreshes the window that made it",
          "[wm2_config_smoke]")
{
    // The server leaves the REQUESTER out of the `reloaded` broadcast and
    // answers it directly instead, and ProtocolClient hands that answer to the
    // request's own callback rather than to the notice path. So the one
    // window that asked for the re-read is the one window that would not hear
    // about it: a callback that handles only the error arm leaves every field
    // showing what the files said before the click (Codex pass 6, P2).
    const std::string window = sourceOf("apps/wm2-config/main.cpp");
    REQUIRE_FALSE(window.empty());

    const std::size_t at = window.find("void reloadWindowManager(");
    REQUIRE(at != std::string::npos);
    const std::size_t end = window.find("\n    }\n", at);
    REQUIRE(end != std::string::npos);
    const std::string body = withoutLineComments(window.substr(at, end - at));
    INFO("reloadWindowManager, comments stripped:\n" << body);

    // The error arm stays...
    CHECK(body.find("ConfigMessageType::Error") != std::string::npos);
    // ...and the success arm walks the same path a notice from another
    // window's reload walks, which is the one that re-reads the layers and
    // re-fetches every effective value.
    CHECK(body.find("onReloadNotice()") != std::string::npos);
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

TEST_CASE("a peer that speaks our version but is not the window manager is refused by name",
          "[wm2_config_smoke]")
{
    // T-9-34. The peer-uid check on the SERVER side keeps other users out; it
    // says nothing about a same-uid process squatting the socket path, or
    // about whatever `--socket <path>` points at. The handshake's `program`
    // member is the client's half of that mitigation, and a hello-ack that
    // gets everything right except who it claims to be must not be handed a
    // single `set`.
    FakeServer server(shortSocketPath("wrongprog"), FakeServer::Reply::WrongProgram);
    REQUIRE(server.listening());

    ProtocolClient client;
    CHECK_FALSE(client.connect(server.path()));
    CHECK(client.state() == ProtocolClient::State::Refused);
    INFO("reason: " << client.reason());
    CHECK(client.reason().find("not-really-a-window-manager") != std::string::npos);
    CHECK(client.reason().find(kConfigProtocolWindowManagerProgram) != std::string::npos);
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

    // A1 (CodeRabbit, apps chunk). The fallback for a key the table does not
    // name was lo == 1, hi == 1 -- a spin button whose range holds exactly one
    // number, so a control the user cannot use at all was the answer to bounds
    // that were merely unknown. A range widget cannot be driven on a host with
    // no toolkit, so the guard is on the source, comment-stripped, in the shape
    // this file already uses for the Appearance page.
    const std::size_t at = page.find("void BehaviourPage::addDelayRow(");
    REQUIRE(at != std::string::npos);
    const std::size_t end = page.find("\n}\n", at);
    REQUIRE(end != std::string::npos);
    const std::string body = withoutLineComments(page.substr(at, end - at));
    INFO("addDelayRow, comments stripped:\n" << body);
    CHECK(body.find("spec->maxValue : 1") == std::string::npos);
    CHECK(body.find("->minValue") != std::string::npos);
    CHECK(body.find("->maxValue") != std::string::npos);
    // The fallback is read from the same table, not spelled out a second time:
    // the range the table gives the delay keys this page carries.
    CHECK(body.find("configKeySpecFor(\"auto-raise-delay\")") != std::string::npos);
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


// =============================================================================
// The Menu page (D-12, T-9-40, T-9-44, plan 09-07)
// =============================================================================

TEST_CASE("the Menu page tokenises a command exactly as the configuration parser does",
          "[wm2_config_smoke]")
{
    // Not "the same way": THE SAME FUNCTION. The parser's menu-entry-command
    // arm and the dialog both call configTokeniseCommand(), so a user who
    // types a command into the window and a user who writes the same line into
    // the file cannot end up running two different argument vectors.
    const std::string typed = "/usr/bin/vim   -p    notes.txt";

    const std::string tree = makeTree("tokenise");
    const std::string userFile = tree + "/user/wm2-born-again/config";
    writeFile(userFile,
              "menu-entry-name=Editor\n"
              "menu-entry-command=" + typed + "\n"
              "menu-entry-category=Custom\n");

    Config parsed;
    parsed.applyFile(userFile);
    REQUIRE(parsed.manualMenuEntries.size() == 1);

    CHECK(configTokeniseCommand(typed) == parsed.manualMenuEntries[0].execArgv);
    CHECK(configTokeniseCommand(typed) ==
          std::vector<std::string>({"/usr/bin/vim", "-p", "notes.txt"}));
}

TEST_CASE("a shell metacharacter in a command is one literal argument, and the dialog shows it as one",
          "[wm2_config_smoke]")
{
    // T-9-40. The guarantee is that a manual entry is never shell-evaluated;
    // the honest version of that guarantee is showing the user what their
    // command actually became, so a semicolon they typed is visibly ONE
    // argument rather than the start of a second command.
    MenuEntryDraft draft;
    draft.name = "Risky";
    draft.command = "/bin/echo hello;rm -rf /tmp/nothing";

    const std::vector<std::string> argv = draft.argv();
    REQUIRE(argv.size() == 4);
    CHECK(argv[1] == "hello;rm");

    const std::string shown = draft.argvDisplay();
    INFO("argument display: " << shown);
    CHECK(shown == "[/bin/echo] [hello;rm] [-rf] [/tmp/nothing]");

    // And the entry the dialog would produce carries exactly those tokens, so
    // what is displayed and what is stored are the same list.
    CHECK(draft.toEntry().execArgv == argv);
    CHECK(draft.toEntry().source == AppEntry::Source::Manual);
}

TEST_CASE("a draft with no name or no command is refused with a reason",
          "[wm2_config_smoke]")
{
    std::string reason;
    MenuEntryDraft empty;
    CHECK_FALSE(empty.complete(reason));
    CHECK_FALSE(reason.empty());

    MenuEntryDraft named;
    named.name = "Editor";
    reason.clear();
    CHECK_FALSE(named.complete(reason));
    CHECK_FALSE(reason.empty());

    MenuEntryDraft whole;
    whole.name = "Editor";
    whole.command = "/usr/bin/vim";
    CHECK(whole.complete(reason));

    // D-07 of phase 7: a row with no category is a Custom row, applied at
    // creation time rather than left empty for the menu to guess at.
    CHECK(whole.toEntry().category == "Custom");
}

TEST_CASE("the file-only category list is the file's own categories plus the Custom default",
          "[wm2_config_smoke]")
{
    // With no window manager to ask there is no second source of truth to
    // invent one from, so the dropdown offers what the file already contains
    // and says why it is offering only that.
    std::vector<AppEntry> entries;
    AppEntry a; a.name = "Editor";  a.category = "Development"; entries.push_back(a);
    AppEntry b; b.name = "Mail";    b.category = "Internet";    entries.push_back(b);
    AppEntry c; c.name = "Another"; c.category = "Development"; entries.push_back(c);

    const std::vector<std::string> offered = menuCategoriesFrom(entries);
    CHECK(offered == std::vector<std::string>({"Development", "Internet", "Custom"}));

    // Custom is present even when nothing in the file uses it, and it is last
    // -- the same place the root menu puts it.
    CHECK(menuCategoriesFrom({}) == std::vector<std::string>({"Custom"}));

    CHECK_FALSE(std::string(kMenuCategoryFileOnlyReason).empty());
}

TEST_CASE("a menu-entry list too long for one protocol line is refused before it is sent, and the refusal names the limit",
          "[wm2_config_smoke]")
{
    // T-9-44. The whole list travels as ONE value, so a long enough list makes
    // a line the window manager will reject with an opaque framing error. The
    // page refuses it first and says what the limit is, because "your entry
    // list is too long" is a sentence a user can act on and "malformed
    // message" is not.
    std::vector<AppEntry> entries;
    for (int i = 0; i < 60; ++i) {
        AppEntry e;
        e.name = "Entry" + std::to_string(i);
        e.execArgv = {"/usr/local/libexec/a-command-with-a-fairly-long-path-" +
                      std::to_string(i)};
        e.category = "AReasonablyLongCategoryName";
        e.source = AppEntry::Source::Manual;
        entries.push_back(e);
    }

    Config rendered;
    rendered.manualMenuEntries = entries;
    const std::string tooLong = configMenuEntriesValue(rendered);

    std::string reason;
    REQUIRE_FALSE(menuEntriesValueFits(tooLong, reason));
    INFO("refusal: " << reason);
    CHECK(reason.find(std::to_string(kConfigProtocolMaxLine)) != std::string::npos);

    // ...and an ordinary list is not refused, or the guard would be a wall.
    Config small;
    AppEntry one; one.name = "Editor"; one.execArgv = {"/usr/bin/vim"};
    small.manualMenuEntries.push_back(one);
    std::string ok;
    CHECK(menuEntriesValueFits(configMenuEntriesValue(small), ok));
    CHECK(ok.empty());
}

TEST_CASE("the Menu page's rows survive a save and come back as the same list",
          "[wm2_config_smoke]")
{
    // D-12: rows map one to one onto the three-key groups, in the order the
    // accumulator requires. The proof is a round trip through the real writer
    // and the real parser rather than a comparison against a spelling.
    const std::string tree = makeTree("menusave");
    const std::string userFile = tree + "/user/wm2-born-again/config";
    writeFile(userFile, "# a comment the writer must preserve\nborders=#00FF00\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    const ConfigLayers layers = configLayersFromDisk();
    form.seedFromLayers(layers);
    REQUIRE(form.menuEntries().empty());

    std::vector<AppEntry> rows;
    MenuEntryDraft first;
    first.name = "Editor"; first.command = "/usr/bin/vim -p"; first.category = "Development";
    rows.push_back(first.toEntry());
    MenuEntryDraft second;
    second.name = "Mail"; second.command = "/usr/bin/mutt";
    rows.push_back(second.toEntry());

    REQUIRE(form.setMenuEntries(rows));
    CHECK(form.dirty());

    std::string error;
    REQUIRE(configFileWrite(layers.userFilePath, form.edits(), form.menuEntries(),
                            form.menuEntriesChanged(), error) == ConfigWriteResult::Ok);
    form.markSaved();
    CHECK_FALSE(form.dirty());

    Config readBack;
    readBack.applyFile(userFile);
    REQUIRE(readBack.manualMenuEntries.size() == 2);
    CHECK(readBack.manualMenuEntries[0].name == "Editor");
    CHECK(readBack.manualMenuEntries[0].execArgv ==
          std::vector<std::string>({"/usr/bin/vim", "-p"}));
    CHECK(readBack.manualMenuEntries[0].category == "Development");
    CHECK(readBack.manualMenuEntries[1].name == "Mail");
    CHECK(readBack.manualMenuEntries[1].category == "Custom");

    // Everything else in the file is where it was.
    const std::string written = readFileOrEmpty(userFile);
    CHECK(written.find("# a comment the writer must preserve") != std::string::npos);
    CHECK(written.find("borders=#00FF00") != std::string::npos);
}

TEST_CASE("the writer is handed the user file's own menu entries, never the system's",
          "[wm2_config_smoke]")
{
    // C1 (Codex pass 4). Config::applyFile() APPENDS menu entries across
    // layers -- the accumulator is never cleared per file -- so the list the
    // form shows is the system file's entries FOLLOWED BY the user file's.
    // Handing that merged list to configFileWrite() as the user file's block
    // copies the system entries into the user file, and the very next layered
    // load reads them twice. Reset had the same shape: it wrote the inherited
    // entries back out instead of removing the block.
    const std::string tree = makeTree("menulayers");
    const std::string systemFile = tree + "/system/wm2-born-again/config";
    const std::string userFile   = tree + "/user/wm2-born-again/config";
    writeFile(systemFile,
              "menu-entry-name=SystemMail\n"
              "menu-entry-command=/usr/bin/mutt\n"
              "menu-entry-category=Internet\n");
    writeFile(userFile,
              "menu-entry-name=UserEditor\n"
              "menu-entry-command=/usr/bin/vim\n"
              "menu-entry-category=Development\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    const ConfigLayers layers = configLayersFromDisk();
    form.seedFromLayers(layers);

    // What the page SHOWS is both layers, in file order, because that is what
    // the running window manager holds and what `set menu-entries` must carry.
    REQUIRE(form.menuEntries().size() == 2);
    CHECK(form.menuEntries()[0].name == "SystemMail");
    CHECK(form.menuEntries()[1].name == "UserEditor");

    // What the WRITER is handed is the user layer alone.
    REQUIRE(form.userMenuEntries().size() == 1);
    CHECK(form.userMenuEntries()[0].name == "UserEditor");

    // The end of the argument, through the real writer and the real parser: a
    // save of an unchanged Menu page leaves the layered read saying two, not
    // three.
    std::string error;
    REQUIRE(configFileWrite(layers.userFilePath, form.edits(),
                            form.userMenuEntries(), true, error) ==
            ConfigWriteResult::Ok);
    Config userOnly;
    userOnly.applyFile(userFile);
    INFO("user file after the save:\n" << readFileOrEmpty(userFile));
    REQUIRE(userOnly.manualMenuEntries.size() == 1);
    CHECK(userOnly.manualMenuEntries[0].name == "UserEditor");
    CHECK(configLayersFromDisk().withUser.manualMenuEntries.size() == 2);

    // And a Reset writes an EMPTY block, so the inherited entry shows through
    // rather than being copied into the file that was meant to stop naming it.
    form.requestMenuReset();
    CHECK(form.menuEntries().size() == 1);          // what removal will produce
    CHECK(form.userMenuEntries().empty());          // what the writer will write

    REQUIRE(configFileWrite(layers.userFilePath, form.edits(),
                            form.userMenuEntries(), true, error) ==
            ConfigWriteResult::Ok);
    Config afterReset;
    afterReset.applyFile(userFile);
    INFO("user file after the reset:\n" << readFileOrEmpty(userFile));
    CHECK(afterReset.manualMenuEntries.empty());
    CHECK(configLayersFromDisk().withUser.manualMenuEntries.size() == 1);
}

TEST_CASE("the window's save hands the writer the user layer's entries",
          "[wm2_config_smoke]")
{
    // The wiring, where a display-free case can reach it: the model can be
    // right and the window still pass the merged list, which is the shape the
    // defect had.
    const std::string windowSource = sourceOf("apps/wm2-config/main.cpp");
    REQUIRE_FALSE(windowSource.empty());
    const std::size_t at = windowSource.find("void save()");
    REQUIRE(at != std::string::npos);
    const std::size_t end = windowSource.find("\n    }\n", at);
    REQUIRE(end != std::string::npos);
    const std::string body = withoutLineComments(windowSource.substr(at, end - at));
    INFO("save(), comments stripped:\n" << body);
    CHECK(body.find("m_form.userMenuEntries()") != std::string::npos);
    // ...and NOT the merged list, named explicitly so the loser is part of the
    // assertion rather than merely absent from it.
    CHECK(body.find("m_form.menuEntries()") == std::string::npos);
}

TEST_CASE("removing a row and then reverting brings it back, which is why Remove asks nothing",
          "[wm2_config_smoke]")
{
    // D-12 chose no confirmation on Remove. That choice is only safe because
    // the removal is unsaved and Revert undoes it, so this is the case that
    // makes the choice defensible rather than merely convenient.
    const std::string tree = makeTree("menuremove");
    writeFile(tree + "/user/wm2-born-again/config",
              "menu-entry-name=Editor\n"
              "menu-entry-command=/usr/bin/vim\n"
              "menu-entry-category=Development\n"
              "menu-entry-name=Mail\n"
              "menu-entry-command=/usr/bin/mutt\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());
    REQUIRE(form.menuEntries().size() == 2);

    std::vector<AppEntry> withoutFirst(form.menuEntries().begin() + 1,
                                       form.menuEntries().end());
    REQUIRE(form.setMenuEntries(withoutFirst));
    CHECK(form.menuEntries().size() == 1);
    CHECK(form.dirty());

    form.revert();
    REQUIRE(form.menuEntries().size() == 2);
    CHECK(form.menuEntries()[0].name == "Editor");
    CHECK_FALSE(form.dirty());
}

TEST_CASE("editing the second of two identical rows replaces the second, not the first",
          "[wm2_config_smoke]")
{
    // C4 (Codex pass 4). The edit path found the row it started on by an
    // IDENTITY SEARCH FROM THE BEGINNING, so with two rows that are equal
    // field for field, editing the second always rewrote the first. Menu order
    // is significant -- it is the order the root menu shows -- so that is the
    // wrong row.
    //
    // The captured INDEX is consulted first and the identity search kept as the
    // fallback, because the index alone is not safe either: gtk_dialog_run()
    // spins a nested main loop, the socket source keeps firing, and a reload
    // notice can move the list underneath the open dialog.
    const auto row = [](const std::string& name, const std::string& command,
                        const std::string& category) {
        MenuEntryDraft draft;
        draft.name = name;
        draft.command = command;
        draft.category = category;
        return draft.toEntry();
    };

    const AppEntry twin = row("Terminal", "/usr/bin/xterm", "Custom");

    SECTION("the selected occurrence wins over an earlier identical row") {
        const std::vector<AppEntry> rows = {twin, twin, row("Mail", "mutt", "Custom")};
        CHECK(menuEntryReplacementIndex(rows, twin, 1) == 1u);
        // ...and the first occurrence is still found when that is the one the
        // edit began on, so this is not merely "always return the index".
        CHECK(menuEntryReplacementIndex(rows, twin, 0) == 0u);
    }

    SECTION("a list that moved under the dialog falls back to identity") {
        // A reload inserted a row above the one being edited, so the captured
        // index now names a different entry entirely.
        const std::vector<AppEntry> reloaded = {row("Mail", "mutt", "Custom"), twin};
        CHECK(menuEntryReplacementIndex(reloaded, twin, 0) == 1u);
    }

    SECTION("a row the reload removed is reported as gone") {
        const std::vector<AppEntry> reloaded = {row("Mail", "mutt", "Custom")};
        CHECK(menuEntryReplacementIndex(reloaded, twin, 0) == reloaded.size());
        CHECK(menuEntryReplacementIndex({}, twin, 3) == 0u);
    }

    SECTION("an index past the end is not read") {
        const std::vector<AppEntry> rows = {twin};
        CHECK(menuEntryReplacementIndex(rows, twin, 99) == 0u);
    }
}

TEST_CASE("the Menu page's edit path finds its row through the shared rule",
          "[wm2_config_smoke]")
{
    // The wiring, where a display-free case can reach it: the pure function can
    // be right and the page still search from the beginning.
    const std::string page = sourceOf("apps/wm2-config/MenuPage.cpp");
    REQUIRE_FALSE(page.empty());
    const std::size_t at = page.find("void MenuPage::edit()");
    REQUIRE(at != std::string::npos);
    const std::size_t end = page.find("\n}\n", at);
    REQUIRE(end != std::string::npos);
    const std::string body = withoutLineComments(page.substr(at, end - at));
    INFO("MenuPage::edit(), comments stripped:\n" << body);
    CHECK(body.find("menuEntryReplacementIndex") != std::string::npos);
    // The captured index is carried into it rather than discarded.
    CHECK(body.find("row") != std::string::npos);
}

TEST_CASE("the Menu page asks the window manager for its categories rather than listing any",
          "[wm2_config_smoke]")
{
    const std::string page = sourceOf("apps/wm2-config/MenuPage.cpp");
    REQUIRE_FALSE(page.empty());
    // The ASK lives where the socket client lives, which is the window; the
    // page is handed the answer. Both halves are checked, because either one
    // alone would let the other quietly grow a scan of its own.
    const std::string window = sourceOf("apps/wm2-config/main.cpp");
    REQUIRE_FALSE(window.empty());

    // The categories the root menu shows are the window manager's answer. A
    // second implementation of the discovery scan in the GUI would be a second
    // answer, and the two would disagree the first time a .desktop file
    // changed.
    CHECK(window.find("kMenuCategoriesKey") != std::string::npos);
    CHECK(page.find("menuCategoriesFrom(") != std::string::npos);   // the fallback
    for (const std::string& source : {page, window}) {
        CHECK(source.find("DesktopEntry") == std::string::npos);
        CHECK(source.find("scanAll") == std::string::npos);
        CHECK(source.find("AppCache") == std::string::npos);
    }

    // And it carries no single setting: D-09 puts every one of those on the
    // other two pages.
    CHECK(keysDeclaredIn(page).empty());
}


// =============================================================================
// The Menu page, against a running desktop
// =============================================================================

TEST_CASE("a row added through the page's model appears in the next root menu",
          "[wm2_config_smoke]")
{
    const std::string home = makeConfigHome("frame-thickness=7\n");
    WmFixture fixture(fixtureWithConfigHome(home));
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();
    XTestDriver driver(fixture.display());
    driver.moveTo(kParkX, kParkY);

    ProtocolClient client;
    REQUIRE(client.connect(configSocketPath(fixture.display().c_str())));
    REQUIRE(wmMenuEntries(client).empty());

    Rect before;
    REQUIRE(openRootMenuGeometry(d, driver, before));
    closeRootMenu(d, driver);

    // Exactly what Add does: build the row from the dialog's draft, append it
    // to the list the page holds, and send the whole list.
    MenuEntryDraft draft;
    draft.name = "AddedFromTheSettingsWindow";
    draft.command = "/bin/true";
    draft.category = "ZzzACategoryNothingElseUses";

    std::vector<AppEntry> rows = wmMenuEntries(client);
    rows.push_back(draft.toEntry());
    const std::string refusal = commitMenuEntries(client, rows);
    INFO("refusal: " << refusal);
    REQUIRE(refusal.empty());

    Rect after;
    const bool reopened = openRootMenuGeometry(d, driver, after);
    closeRootMenu(d, driver);

    INFO("wm stderr:\n" << fixture.wmStderr());
    INFO("menu before " << before.w << "x" << before.h
                        << " after " << after.w << "x" << after.h);
    CHECK(reopened);

    // The menu the USER sees changed: one row taller for the category the
    // entry brought with it, and wider because WindowManager::menu() sizes the
    // popup to its widest label.
    CHECK(after.h > before.h);
    CHECK(after.w > before.w);

    // ...and the window manager reports the row back in the grammar it was
    // sent in, so the two ends are checked against each other.
    const std::vector<AppEntry> reported = wmMenuEntries(client);
    REQUIRE(reported.size() == 1);
    CHECK(reported[0].name == "AddedFromTheSettingsWindow");
    CHECK(reported[0].category == "ZzzACategoryNothingElseUses");
}

TEST_CASE("the category list offered by the page is the one the window manager reports",
          "[wm2_config_smoke]")
{
    // D-12's dropdown. Asserted against what the window manager ANSWERS rather
    // than against a list written here: the categories its root menu shows are
    // its own, and a literal in this file would be a third opinion.
    WmFixture fixture;
    ProtocolClient client;
    REQUIRE(client.connect(configSocketPath(fixture.display().c_str())));

    const std::string reported = wmValue(client, kMenuCategoriesKey);
    INFO("categories reported: '" << reported << "'");
    const std::vector<std::string> offered = menuCategoriesFromValue(reported);
    REQUIRE_FALSE(offered.empty());
    // Custom is last, exactly where the root menu puts it.
    CHECK(offered.back() == "Custom");

    // A row in a category nothing else uses appears in the answer, which is
    // what makes the answer the running menu's rather than a static list.
    MenuEntryDraft draft;
    draft.name = "CategoryProbe";
    draft.command = "/bin/true";
    draft.category = "ZzzProbeCategory";
    std::vector<AppEntry> rows = wmMenuEntries(client);
    rows.push_back(draft.toEntry());
    REQUIRE(commitMenuEntries(client, rows).empty());

    const std::vector<std::string> after =
        menuCategoriesFromValue(wmValue(client, kMenuCategoriesKey));
    INFO("categories after: " << wmValue(client, kMenuCategoriesKey));
    CHECK(contains(after, "ZzzProbeCategory"));
    CHECK(after.back() == "Custom");

    // The key is READ-ONLY: a settings window may ask what the menu shows and
    // may not dictate it, because the answer is derived from discovery plus
    // the entry list and setting it would be setting a view of two things.
    bool acked = false;
    std::string refusal;
    REQUIRE(client.sendSet(kMenuCategoriesKey, "Anything",
                           [&](const ConfigMessage& reply) {
                               if (reply.type == ConfigMessageType::Ack) acked = true;
                               if (reply.type == ConfigMessageType::Error) refusal = reply.reason;
                           }));
    REQUIRE(client.pumpUntil([&]() { return acked || !refusal.empty(); }, 15000));
    CHECK_FALSE(acked);
    CHECK_FALSE(refusal.empty());
}


// =============================================================================
// The three moments the file and the desktop can disagree (D-07, D-08, D-13)
// =============================================================================
//
// These are the cases the plan exists for. Each of the three is a place where a
// settings window usually goes wrong quietly: it closes leaving a desktop that
// matches no file, it throws away what somebody was typing because something
// else reloaded, or it "resets" by writing the defaults into the file it was
// supposed to take them out of.

TEST_CASE("closing is silent with nothing unsaved and asks when there is something",
          "[wm2_config_smoke]")
{
    // D-07's precondition, in the model rather than in the widget: the window's
    // delete-event handler asks the form whether a save would write anything,
    // and prompts only then. A prompt on every close would train the user to
    // dismiss it without reading, which is the failure mode the prompt exists
    // to avoid.
    const std::string tree = makeTree("closeclean");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());
    CHECK_FALSE(form.dirty());

    REQUIRE(form.setValue("frame-thickness", "13"));
    CHECK(form.dirty());

    form.revert();
    CHECK_FALSE(form.dirty());

    // A Menu-page-only change counts too, and it produces no ConfigEdit at all
    // -- the writer rewrites those lines as a block. A close prompt keyed on
    // the edit list would let a user close over unsaved menu rows.
    MenuEntryDraft draft;
    draft.name = "Editor";
    draft.command = "/usr/bin/vim";
    REQUIRE(form.setMenuEntries({draft.toEntry()}));
    CHECK(form.dirty());
    CHECK(form.edits().empty());
}

TEST_CASE("the window's close path offers Save, Discard and Cancel and nothing else",
          "[wm2_config_smoke]")
{
    const std::string window = sourceOf("apps/wm2-config/main.cpp");
    REQUIRE_FALSE(window.empty());

    // Wired to the DELETE EVENT, so the window manager's own close path and the
    // title-bar button both go through it rather than only one of them.
    CHECK(window.find("delete-event") != std::string::npos);
    CHECK(window.find("Discard") != std::string::npos);
    CHECK(window.find("Cancel") != std::string::npos);
}

TEST_CASE("Discard returns the running window manager to the saved file's values",
          "[wm2_config_smoke]")
{
    // D-07's whole point. Without the send-back, closing the settings window
    // can leave a desktop that matches no file at all -- a state the user
    // cannot reason about the next time they open anything.
    WmFixture fixture;
    ProtocolClient client;
    REQUIRE(client.connect(configSocketPath(fixture.display().c_str())));

    const std::string tree = makeTree("discard");
    ScopedXdg xdg(tree + "/user", tree + "/system");
    FormState form;
    form.seedFromLayers(configLayersFromDisk());

    const std::string saved = wmValue(client, "frame-thickness");
    REQUIRE_FALSE(saved.empty());
    const std::string edited = (saved == "13") ? "17" : "13";

    REQUIRE(commitThroughForm(form, client, "frame-thickness", edited).empty());
    REQUIRE(wmValue(client, "frame-thickness") == edited);

    // Discard, exactly as the close prompt performs it: put the form back and
    // send every value that moved BACK to the desktop.
    const std::vector<std::pair<std::string, std::string>> restores =
        revertAndCollectRestores(form);
    REQUIRE_FALSE(restores.empty());
    for (const auto& kv : restores) {
        bool acked = false;
        std::string refusal;
        REQUIRE(client.sendSet(kv.first, kv.second, [&](const ConfigMessage& reply) {
            if (reply.type == ConfigMessageType::Ack) acked = true;
            if (reply.type == ConfigMessageType::Error) refusal = reply.reason;
        }));
        REQUIRE(client.pumpUntil([&]() { return acked || !refusal.empty(); }, 15000));
        INFO("refusal for " << kv.first << ": " << refusal);
        CHECK(acked);
    }

    INFO("wm stderr:\n" << fixture.wmStderr());
    CHECK(wmValue(client, "frame-thickness") == saved);
    CHECK_FALSE(form.dirty());
}

TEST_CASE("Discard in file-only mode puts the form back and sends nothing",
          "[wm2_config_smoke]")
{
    // There is nothing running to return, so Discard is a revert and a close.
    // Asserted through the client rather than by inspection: a disconnected
    // client refuses to send at all, so no value can leave this process.
    const std::string tree = makeTree("discardfileonly");
    writeFile(tree + "/user/wm2-born-again/config", "frame-thickness=9\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());
    REQUIRE(form.setValue("frame-thickness", "21"));

    ProtocolClient client;
    CHECK_FALSE(client.connect(tree + "/there-is-no-socket-here"));

    const std::vector<std::pair<std::string, std::string>> restores =
        revertAndCollectRestores(form);
    CHECK(form.value("frame-thickness") == "9");
    CHECK_FALSE(form.dirty());

    for (const auto& kv : restores) {
        CHECK_FALSE(client.sendSet(kv.first, kv.second, nullptr));
    }
}

TEST_CASE("a reload notice keeps an unsaved edit and marks it rather than replacing it",
          "[wm2_config_smoke]")
{
    // D-08 and this plan's standing prohibition. Replacing a value under
    // somebody's cursor because a file changed elsewhere is the failure this
    // decision exists to prevent; the honest alternative is to keep the edit
    // and SAY that the file underneath has moved.
    const std::string tree = makeTree("reloadmark");
    writeFile(tree + "/user/wm2-born-again/config", "frame-thickness=9\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());
    REQUIRE(form.setValue("frame-thickness", "21"));

    const FormField* edited = form.field("frame-thickness");
    REQUIRE(edited != nullptr);
    CHECK(edited->dirty);
    CHECK_FALSE(edited->staleUnderEdit);

    // The reload brought a different value for a key the user is editing.
    form.adoptEffective("frame-thickness", "31", ValueSource::WindowManager, "");
    edited = form.field("frame-thickness");
    REQUIRE(edited != nullptr);
    CHECK(edited->current == "21");          // NOT replaced
    CHECK(edited->dirty);
    CHECK(edited->staleUnderEdit);           // ...and marked as differing
    CHECK(edited->effective == "31");

    // An UNTOUCHED key follows the new value and is not marked, or every
    // control on the page would light up after any reload.
    form.adoptEffective("tab-background", "#010203", ValueSource::WindowManager, "");
    const FormField* untouched = form.field("tab-background");
    REQUIRE(untouched != nullptr);
    CHECK(untouched->current == "#010203");
    CHECK_FALSE(untouched->staleUnderEdit);

    // Touching the control again clears the mark: the user has now seen it and
    // decided, so leaving the mark up would be nagging about old news.
    REQUIRE(form.setValue("frame-thickness", "22"));
    CHECK_FALSE(form.field("frame-thickness")->staleUnderEdit);

    // And a revert clears it too, because there is no longer an edit to mark.
    REQUIRE(form.setValue("frame-thickness", "23"));
    form.adoptEffective("frame-thickness", "41", ValueSource::WindowManager, "");
    REQUIRE(form.field("frame-thickness")->staleUnderEdit);
    form.revert();
    CHECK_FALSE(form.field("frame-thickness")->staleUnderEdit);
}

TEST_CASE("a reload notice arriving with an entry dialog open leaves the dialog's contents alone",
          "[wm2_config_smoke]")
{
    // gtk_dialog_run() spins a NESTED main loop, so the socket source keeps
    // firing and a reload notice really can arrive with the dialog up. The
    // dialog's contents are a MenuEntryDraft owned by the call that opened it;
    // the reload path writes into FormState and the row list and has no route
    // to it. That is the structural half.
    const std::string tree = makeTree("reloaddialog");
    writeFile(tree + "/user/wm2-born-again/config",
              "menu-entry-name=Editor\nmenu-entry-command=/usr/bin/vim\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());
    REQUIRE(form.menuEntries().size() == 1);

    // What somebody is halfway through typing.
    MenuEntryDraft open;
    open.name = "HalfTyped";
    open.command = "/usr/bin/some-editor --flag";
    open.category = "Development";

    // ...and a reload replaces the whole list underneath.
    AppEntry arrived;
    arrived.name = "SomethingElse";
    arrived.execArgv = {"/bin/true"};
    arrived.category = "Custom";
    form.adoptEffectiveMenuEntries({arrived});

    CHECK(open.name == "HalfTyped");
    CHECK(open.command == "/usr/bin/some-editor --flag");
    CHECK(open.category == "Development");
    CHECK(open.toEntry().execArgv ==
          std::vector<std::string>({"/usr/bin/some-editor", "--flag"}));

    // And the source half: the page's refresh touches the row list only.
    const std::string page = sourceOf("apps/wm2-config/MenuPage.cpp");
    REQUIRE_FALSE(page.empty());
    const std::size_t at = page.find("void MenuPage::refreshFromForm()");
    REQUIRE(at != std::string::npos);
    const std::size_t end = page.find("\n}\n", at);
    REQUIRE(end != std::string::npos);
    // COMMENTS STRIPPED FIRST. The guard is about what the code touches, and
    // the function's own comment explains that it touches no dialog -- which,
    // left in, is a mention of the very token the guard forbids. The project
    // already learned this once (CMakeLists.txt, D-33): a comment quoting the
    // token a grep-shaped guard forbids defeats the guard.
    const std::string body = withoutLineComments(page.substr(at, end - at));
    INFO("refreshFromForm, comments stripped:\n" << body);
    CHECK(body.find("dialog") == std::string::npos);
    CHECK(body.find("Dialog") == std::string::npos);
    CHECK(body.find("Draft") == std::string::npos);
}

TEST_CASE("an unsaved edit that a reload made agree with the file is no longer marked",
          "[wm2_config_smoke]")
{
    // The mutation case for the one above. "The edit is kept and marked" would
    // pass just as happily against a form that marked EVERY edited key
    // forever, which would make the mark meaningless. A reload that happens to
    // bring exactly what the user typed is not a disagreement.
    const std::string tree = makeTree("reloadagrees");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());
    REQUIRE(form.setValue("frame-thickness", "21"));

    form.adoptEffective("frame-thickness", "21", ValueSource::WindowManager, "");
    const FormField* field = form.field("frame-thickness");
    REQUIRE(field != nullptr);
    CHECK(field->current == "21");
    CHECK_FALSE(field->staleUnderEdit);
}

TEST_CASE("a file re-read moves what Reset will produce, and keeps the edits it finds",
          "[wm2_config_smoke]")
{
    // D-08's other half, and the one a reload notice used to miss. Re-reading
    // the files updates each field's EFFECTIVE value, because the window
    // manager is asked for every key straight afterwards -- but `belowUser`,
    // the value D-13's Reset shows and live-applies, is a snapshot taken at
    // seed time and nothing was refreshing it. A system file that changed under
    // an open settings window therefore left Reset offering, and applying, the
    // value that file used to hold.
    const std::string tree = makeTree("rereadbelow");
    const std::string systemFile = tree + "/system/wm2-born-again/config";
    const std::string userFile   = tree + "/user/wm2-born-again/config";
    writeFile(systemFile, "frame-thickness=9\n");
    writeFile(userFile,   "frame-thickness=23\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());
    REQUIRE(form.value("frame-thickness") == "23");
    const FormField* seeded = form.field("frame-thickness");
    REQUIRE(seeded != nullptr);
    REQUIRE(seeded->belowUser == "9");

    // Somebody is halfway through an unsaved edit on a different setting, which
    // the refresh must not disturb -- the same prohibition adoptEffective()
    // carries.
    REQUIRE(form.setValue("tab-background", "#FF0000"));

    // The system file changes on disk and the window manager re-reads it. This
    // is exactly what onReloadNotice() does: a fresh layered read, then a `get`
    // for every key.
    writeFile(systemFile, "frame-thickness=13\n");
    const ConfigLayers reread = configLayersFromDisk();
    form.refreshLowerLayers(reread);
    form.adoptEffective("frame-thickness", "23", ValueSource::WindowManager, "");

    // Reset removes the key from the USER file, so what it shows -- and what it
    // live-applies to the desktop -- is the layer below, which the re-read just
    // moved from 9 to 13.
    REQUIRE(form.requestReset("frame-thickness"));
    CHECK(form.value("frame-thickness") == "13");
    CHECK(form.value("frame-thickness") != std::to_string(Config().frameThickness));

    // ...and the unsaved edit elsewhere came through the refresh untouched.
    const FormField* edited = form.field("tab-background");
    REQUIRE(edited != nullptr);
    CHECK(edited->current == "#FF0000");
    CHECK(edited->dirty);

    // The wiring, asserted where a display-free case can reach it: the window's
    // reload path performs the refresh. Without this the model could be correct
    // and the window still stale, which is the shape the defect had.
    const std::string windowSource = sourceOf("apps/wm2-config/main.cpp");
    REQUIRE_FALSE(windowSource.empty());
    const std::size_t at = windowSource.find("void onReloadNotice()");
    REQUIRE(at != std::string::npos);
    const std::size_t end = windowSource.find("\n    }\n", at);
    REQUIRE(end != std::string::npos);
    const std::string body = withoutLineComments(windowSource.substr(at, end - at));
    INFO("onReloadNotice, comments stripped:\n" << body);
    CHECK(body.find("refreshLowerLayers") != std::string::npos);
}

TEST_CASE("Reset all on a page removes that page's keys from the user file and writes no defaults",
          "[wm2_config_smoke]")
{
    // D-13's per-page half, built from the per-setting reset so there is one
    // meaning of reset. What "reset" must NOT do is write the built-in default
    // into the file, because that would permanently hide a system-wide value
    // the user was trying to fall back to.
    const std::string tree = makeTree("resetall");
    const std::string systemFile = tree + "/system/wm2-born-again/config";
    const std::string userFile   = tree + "/user/wm2-born-again/config";
    writeFile(systemFile, "auto-raise-delay=250\n");
    writeFile(userFile,
              "# a comment the writer must preserve\n"
              "click-to-focus=true\n"
              "auto-raise-delay=900\n"
              "new-window-command=/usr/bin/xterm\n"
              "tab-background=#010203\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    const ConfigLayers layers = configLayersFromDisk();
    form.seedFromLayers(layers);
    REQUIRE(form.value("auto-raise-delay") == "900");

    const std::vector<std::string> pageKeys = {
        "click-to-focus", "raise-on-focus", "auto-raise",
        "focus-stealing-prevention", "auto-raise-delay",
        "pointer-stopped-delay", "destroy-window-delay",
        "new-window-command", "exec-using-shell",
    };
    const std::vector<std::string> moved = form.requestResetAll(pageKeys);

    // Only the keys the file actually set can move; the rest were already
    // showing what removal produces.
    INFO("keys that moved: " << moved.size());
    CHECK(contains(moved, "click-to-focus"));
    CHECK(contains(moved, "auto-raise-delay"));
    CHECK(contains(moved, "new-window-command"));

    // The form shows the layer BELOW the user file at once, not the built-in
    // default -- 250 here, which is neither 900 nor the compiled-in value.
    CHECK(form.value("auto-raise-delay") == "250");
    CHECK(form.value("auto-raise-delay") != std::to_string(Config().autoRaiseDelay));

    std::string error;
    REQUIRE(configFileWrite(layers.userFilePath, form.edits(), form.menuEntries(),
                            form.menuEntriesChanged(), error) == ConfigWriteResult::Ok);
    form.markSaved();

    const std::string written = readFileOrEmpty(userFile);
    INFO("user file after the reset:\n" << written);
    for (const std::string& key : pageKeys) {
        INFO("key: " << key);
        CHECK(written.find(key) == std::string::npos);
    }
    // No default took their place, and the other page's key is untouched.
    CHECK(written.find("900") == std::string::npos);
    CHECK(written.find("250") == std::string::npos);
    CHECK(written.find("tab-background=#010203") != std::string::npos);
    CHECK(written.find("# a comment the writer must preserve") != std::string::npos);
}

TEST_CASE("Revert before Save undoes a Reset all", "[wm2_config_smoke]")
{
    const std::string tree = makeTree("resetallrevert");
    writeFile(tree + "/user/wm2-born-again/config", "click-to-focus=true\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());
    REQUIRE(form.value("click-to-focus") == "true");

    form.requestResetAll({"click-to-focus"});
    CHECK(form.dirty());
    CHECK(form.value("click-to-focus") == "false");

    form.revert();
    CHECK_FALSE(form.dirty());
    CHECK(form.value("click-to-focus") == "true");
    CHECK(form.edits().empty());
}

TEST_CASE("the window shows the reload notice where the banner is, not in a second status region",
          "[wm2_config_smoke]")
{
    const std::string window = sourceOf("apps/wm2-config/main.cpp");
    REQUIRE_FALSE(window.empty());

    // One status region. A settings window with a notice line at the top and
    // another at the bottom makes a user check two places for one answer.
    CHECK(window.find("notice(") != std::string::npos);
    CHECK(window.find("m_notice") != std::string::npos);
}


// ---------------------------------------------------------------------------
// D-20 / D-32: the settings window's resident memory, against a stated budget
//
// This project constrains itself to running comfortably on a 512 MB VPS
// alongside a VNC server, and Phase 8 made that a test the suite enforces
// rather than a sentence in a document. The window manager's own figure has
// been asserted since plan 08-14 ([wm_resource_budget], 11.8 MB against a
// 24 MB budget). The settings window had nothing.
//
// 09-RESEARCH's assumption log named it "the single most consequential
// unverified number in the document" and instructed that no plan skip the
// measurement on the strength of the 30-60 MB estimate beside it. Plan 09-09
// took the measurement. The estimate was low.
//
// THE BUDGET BELOW IS CHOSEN FROM THE MEASUREMENT, NOT BEFORE IT.
//
// Measured on the reference host, release tree, Xvfb 1280x1024x24, three
// repeats of each:
//
//   the FIRST wm2-config launched on a freshly started X server   ~101 MB
//   every LATER wm2-config on that same server                     ~51 MB
//
// That split is reproducible and is NOT explained here, because it was not
// determined: it is not the per-user fontconfig cache (a fresh HOME on each
// run reproduces the ~51 MB figure from the second launch onward) and it is not
// "being the first client on the server" (an xclock connected first does not
// change it). What it is remains open. What matters for a budget is that a real
// session opens the settings window once, so ~101 MB is the figure a user
// actually pays, and it is the figure this budget is set against.
//
// 160 MB is roughly 1.6x the highest measured value -- tighter than the
// window manager's own ~2x, because at this size a 2x budget would be most of
// a quarter of the whole 512 MB machine and would stop being a detector.
//
// WHAT THIS CASE IS FOR. It is a regression detector, not the release figure.
// The release figure comes from the once-per-release remote-desktop pass D-20
// requires, recorded under
// .planning/phases/09-config-gui-ipc/evidence/remote-desktop/.
// ---------------------------------------------------------------------------

TEST_CASE("wm2-config's resident memory is measured against a stated budget",
          "[wm2_config_smoke]")
{
#ifndef WM2_CONFIG_PATH
    SKIP(kGuiNotBuilt);
#else
    // ONE BUDGET PER TREE, in the shape tests/test_wm_resource.cpp already
    // established for the window manager's own figure and for the same reason:
    // in the sanitizer tree every allocation gains redzones, freed memory sits
    // in a quarantine and the shadow map is charged to RSS, so one loose number
    // covering both trees would be meaningless in the debug one. That build is
    // a diagnostic tool and never ships, so its budget exists only to catch a
    // REGRESSION in that tree, not to certify a VPS.
    //
    // Debug: measured 101 MB, budget 160 MB -- see the note above for how the
    // measurement was taken.
    // ASan:  measured 172 MB over three runs, stable to a few hundred kB;
    //        budget 272 MB, the same ~1.6x headroom the debug figure carries.
    //
    // The ASan constant was MISSING rather than wrong before this: the case
    // shipped with the debug number alone, so the asan tree failed it on the
    // instrumentation rather than on anything wm2-config does. Confirmed by
    // rebuilding the settings window from the pre-review sources in the same
    // tree: 171.9 to 172.5 MB, the same figure.
    //
    // If a run exceeds these, that is a FINDING. It is not a budget to raise.
    constexpr long kBudgetKbDebug = 160L * 1024L;
    constexpr long kBudgetKbAsan  = 272L * 1024L;

#if defined(__SANITIZE_ADDRESS__)
    constexpr long kBudgetKb = kBudgetKbAsan;
    constexpr const char* kBudgetTreeName = "asan";
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
    constexpr long kBudgetKb = kBudgetKbAsan;
    constexpr const char* kBudgetTreeName = "asan";
#  else
    constexpr long kBudgetKb = kBudgetKbDebug;
    constexpr const char* kBudgetTreeName = "debug";
#  endif
#else
    constexpr long kBudgetKb = kBudgetKbDebug;
    constexpr const char* kBudgetTreeName = "debug";
#endif

    WmFixture fixture;
    const std::string home = makeTree("guihome-rss");

    ChildProcess gui = spawnConfigGui(fixture.display(), {}, home);
    REQUIRE(gui.pid() > 0);

    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();

    // Measure only once the window is up and the connection state has been
    // decided: a process still linking and parsing has not finished allocating,
    // and a figure taken then would be an under-report that drifts with how
    // busy the host is.
    Window win = None;
    std::string state;
    const bool appeared = WmFixture::pollUntil([&]() {
        state.clear();
        win = findConfigWindow(d, DefaultRootWindow(d), state);
        return win != None && isViewable(d, win) && !state.empty();
    }, 30000);
    REQUIRE(appeared);

    long guiKb = 0;
    REQUIRE(wm2test::residentKb(gui.pid(), guiKb));

    long wmKb = 0;
    REQUIRE(wm2test::residentKb(fixture.wm().pid(), wmKb));

    INFO("wm2-config resident " << guiKb << " kB against a " << kBudgetKb
         << " kB " << kBudgetTreeName
         << " budget; the window manager beside it holds " << wmKb << " kB");

    // Anti-vacuity: a reader that returned 0 for a live process would satisfy
    // any budget. The settings window links GTK; it cannot be under a megabyte.
    CHECK(guiKb > 1024);
    CHECK(guiKb <= kBudgetKb);

    // The two figures are read through ONE reader, which is the whole reason it
    // was moved into the shared fixture header: the release notes put them side
    // by side against one 512 MB budget, and that comparison is only honest if
    // the same field in the same units produced both.
    CHECK(wmKb > 0);
    CHECK(wmKb < guiKb);

    gui.shutdown();   // by the PID spawnConfigGui() created, never by name
#endif
}


// ---------------------------------------------------------------------------
// Reply correlation across an unsolicited notice (CR-02)
// ---------------------------------------------------------------------------
//
// D-08 reuses `reloaded` as BOTH the reply to a client's own `reload` and an
// unsolicited broadcast to every hello-completed connection, which is what let
// the phase avoid a twelfth message type. The stream is therefore not a pure
// request/response sequence, and a client that pops the head of its pending
// queue for any decodable line desynchronises the moment a broadcast lands
// while requests are outstanding.
//
// The consequence is not cosmetic. wm2-config pushes 23 pending gets in one
// main-loop turn at startup; one interposed `reloaded` shifts every reply by
// one, and the per-key handlers adopt colours under font keys and integers
// under colour keys -- which the user then saves to their configuration file.

namespace {

// A peer that completes the handshake on a thread and then hands the accepted
// descriptor to the case, so the case can script exactly what arrives and in
// exactly what order. Distinct from FakeServer above, which exists to be a
// STRANGER; this one is a well-behaved window manager whose timing is the
// point.
class ScriptedPeer {
public:
    explicit ScriptedPeer(const std::string& path) : m_path(path)
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

    ~ScriptedPeer()
    {
        m_stop = true;
        if (m_listenFd >= 0) ::shutdown(m_listenFd, SHUT_RDWR);
        if (m_thread.joinable()) m_thread.join();
        if (m_connFd >= 0) ::close(m_connFd);
        if (m_listenFd >= 0) ::close(m_listenFd);
        ::unlink(m_path.c_str());
    }

    ScriptedPeer(const ScriptedPeer&) = delete;
    ScriptedPeer& operator=(const ScriptedPeer&) = delete;

    bool listening() const { return m_listening; }
    const std::string& path() const { return m_path; }

    // The accepted descriptor, once the handshake thread has answered and
    // retired. Waits up to `timeoutMs`; -1 on the deadline.
    int connection(int timeoutMs)
    {
        for (int waited = 0; waited < timeoutMs; waited += 10) {
            if (m_handshakeDone) {
                if (m_thread.joinable()) m_thread.join();
                return m_connFd;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return -1;
    }

    static bool writeLine(int fd, const ConfigMessage& message)
    {
        const std::string bytes = configProtocolEncode(message);
        std::size_t sent = 0;
        while (sent < bytes.size()) {
            const ssize_t n = ::send(fd, bytes.data() + sent, bytes.size() - sent,
                                     MSG_NOSIGNAL);
            if (n <= 0) return false;
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    // One frame from the client, or "" on the deadline.
    static std::string readLine(int fd, int timeoutMs)
    {
        // THE BUDGET IS MILLISECONDS, NOT ITERATIONS (I-07). `waited` used to
        // advance on every turn of the loop, including the ones that consumed
        // a byte without sleeping, so the effective budget was timeoutMs/10
        // BYTES -- 200 of them at the 2000 ms every call site passes. The
        // frames read today are about forty bytes, so nothing was affected;
        // a case reading a `value` reply carrying a menu-entries list would
        // have got a spurious "" and a misleading REQUIRE_FALSE(...empty())
        // failure. Advanced only where time actually passes.
        std::string line;
        int waited = 0;
        while (waited < timeoutMs) {
            char c = 0;
            const ssize_t n = ::recv(fd, &c, 1, MSG_DONTWAIT);
            if (n == 1) {
                line += c;
                if (c == '\n') return line;
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            waited += 10;
        }
        return std::string();
    }

private:
    void serve()
    {
        struct pollfd p;
        p.fd = m_listenFd;
        p.events = POLLIN;
        p.revents = 0;
        for (int waited = 0; waited < 5000 && !m_stop; waited += 50) {
            const int r = ::poll(&p, 1, 50);
            if (r <= 0) continue;

            const int fd = ::accept(m_listenFd, nullptr, nullptr);
            if (fd < 0) continue;

            // The hello, then the ack. Nothing here is dishonest: the point of
            // this peer is the ORDER of what comes afterwards.
            (void)readLine(fd, 2000);

            ConfigMessage ack;
            ack.type = ConfigMessageType::HelloAck;
            ack.program = "wm2-born-again";
            ack.protocol = kConfigProtocolVersion;
            (void)writeLine(fd, ack);

            m_connFd = fd;
            m_handshakeDone = true;
            return;
        }
        m_handshakeDone = true;
    }

    std::string       m_path;
    int               m_listenFd = -1;
    int               m_connFd = -1;
    bool              m_listening = false;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_handshakeDone{false};
    std::thread       m_thread;
};

}  // namespace


TEST_CASE("An unsolicited reload notice does not shift the replies behind it",
          "[wm2_config_smoke][protocol][notice]")
{
    ScriptedPeer peer(shortSocketPath("cr02notice"));
    REQUIRE(peer.listening());

    ProtocolClient client;
    REQUIRE(client.connect(peer.path()));
    REQUIRE(client.state() == ProtocolClient::State::Connected);

    const int fd = peer.connection(5000);
    REQUIRE(fd >= 0);

    int notices = 0;
    client.setNoticeHandler([&notices]() { ++notices; });

    // TWO gets in one turn, which is the shape wm2-config's startup has: 23 of
    // them, pushed before a single reply has come back.
    std::string keyA, valueA, keyB, valueB;
    bool doneA = false;
    bool doneB = false;
    REQUIRE(client.sendGet("tab-foreground", [&](const ConfigMessage& reply) {
        keyA = reply.key;
        if (reply.type == ConfigMessageType::Value) valueA = reply.value;
        doneA = true;
    }));
    REQUIRE(client.sendGet("tab-background", [&](const ConfigMessage& reply) {
        keyB = reply.key;
        if (reply.type == ConfigMessageType::Value) valueB = reply.value;
        doneB = true;
    }));

    // Both requests really did reach the peer, so what follows is a reply
    // ordering question and not a lost write.
    REQUIRE_FALSE(ScriptedPeer::readLine(fd, 2000).empty());
    REQUIRE_FALSE(ScriptedPeer::readLine(fd, 2000).empty());

    // SOMEBODY ELSE'S reload lands first: a second settings window pressing
    // "Re-read files", or `wm2-ctl reload` from a shell.
    ConfigMessage reloaded;
    reloaded.type = ConfigMessageType::Reloaded;
    REQUIRE(ScriptedPeer::writeLine(fd, reloaded));

    ConfigMessage valueOne;
    valueOne.type = ConfigMessageType::Value;
    valueOne.key = "tab-foreground";
    valueOne.value = "#111111";
    REQUIRE(ScriptedPeer::writeLine(fd, valueOne));

    ConfigMessage valueTwo;
    valueTwo.type = ConfigMessageType::Value;
    valueTwo.key = "tab-background";
    valueTwo.value = "#222222";
    REQUIRE(ScriptedPeer::writeLine(fd, valueTwo));

    CHECK(client.pumpUntil([&]() { return doneA && doneB; }, 5000));

    // Each reply reaches the request that asked for it. A colour adopted under
    // the wrong key is what ends up in the user's configuration file.
    CHECK(keyA == "tab-foreground");
    CHECK(valueA == "#111111");
    CHECK(keyB == "tab-background");
    CHECK(valueB == "#222222");

    // And D-08's whole point survives: the notice fired, exactly once, rather
    // than being eaten as somebody's reply.
    CHECK(notices == 1);
}


TEST_CASE("A reload this client asked for is its reply and not a notice",
          "[wm2_config_smoke][protocol][notice]")
{
    ScriptedPeer peer(shortSocketPath("cr02reply"));
    REQUIRE(peer.listening());

    ProtocolClient client;
    REQUIRE(client.connect(peer.path()));

    const int fd = peer.connection(5000);
    REQUIRE(fd >= 0);

    int notices = 0;
    client.setNoticeHandler([&notices]() { ++notices; });

    bool answered = false;
    ConfigMessageType seen = ConfigMessageType::Unknown;
    REQUIRE(client.sendReload([&](const ConfigMessage& reply) {
        seen = reply.type;
        answered = true;
    }));
    REQUIRE_FALSE(ScriptedPeer::readLine(fd, 2000).empty());

    ConfigMessage reloaded;
    reloaded.type = ConfigMessageType::Reloaded;
    REQUIRE(ScriptedPeer::writeLine(fd, reloaded));

    CHECK(client.pumpUntil([&]() { return answered; }, 5000));
    CHECK(seen == ConfigMessageType::Reloaded);
    // The reply is not ALSO a notice: telling them apart by type alone would
    // have made every reload fire the window's "somebody else changed things"
    // banner at itself.
    CHECK(notices == 0);
}


TEST_CASE("An error answers whichever request is at the head",
          "[wm2_config_smoke][protocol][notice]")
{
    // The window manager answers a refused `get` or `set` with `error`, so the
    // head-matching rule has to accept it for ANY expected type or a refusal
    // would be dropped and the request's handler would never run -- which is
    // how a control ends up stuck in a pending state nothing clears.
    ScriptedPeer peer(shortSocketPath("cr02error"));
    REQUIRE(peer.listening());

    ProtocolClient client;
    REQUIRE(client.connect(peer.path()));

    const int fd = peer.connection(5000);
    REQUIRE(fd >= 0);

    bool answered = false;
    std::string reason;
    REQUIRE(client.sendSet("tab-foreground", "nonsense",
                           [&](const ConfigMessage& reply) {
                               if (reply.type == ConfigMessageType::Error) {
                                   reason = reply.reason;
                               }
                               answered = true;
                           }));
    REQUIRE_FALSE(ScriptedPeer::readLine(fd, 2000).empty());

    ConfigMessage error;
    error.type = ConfigMessageType::Error;
    error.key = "tab-foreground";
    error.reason = "the X server cannot parse that colour";
    REQUIRE(ScriptedPeer::writeLine(fd, error));

    CHECK(client.pumpUntil([&]() { return answered; }, 5000));
    CHECK(reason == "the X server cannot parse that colour");
}


TEST_CASE("a save is held back while a live set is unanswered, and a refusal "
          "puts back the value that was in force when it was sent",
          "[wm2_config_smoke][protocol][notice]")
{
    // Y1 (Codex pass 7, P1). applyLive() sends its `set` and returns; the
    // refusal comes back later, on a turn of the main loop. A Save pressed in
    // that gap has already written the typed value and markSaved() has made it
    // the field's `effective` -- so a restore that puts the field back to
    // `effective` puts back the very value the window manager refused, and the
    // user's file keeps it. For a tab colour that is not a file disagreeing
    // with a desktop: the window manager refuses a colour the X server cannot
    // allocate, Border::allocateXftColors() calls fatal() on the same colour at
    // the next startup, and the desktop does not come up.
    //
    // Two halves, both driven here. The window has something to ASK before it
    // saves -- whether anything it sent is still unanswered -- and the value a
    // refusal restores is the one captured when the request was SENT rather
    // than whatever `effective` has become since.
    const std::string tree = makeTree("pendingsave");
    writeFile(tree + "/user/wm2-born-again/config", "tab-background=#111111\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());
    REQUIRE(form.value("tab-background") == "#111111");

    ScriptedPeer peer(shortSocketPath("y1pending"));
    REQUIRE(peer.listening());

    ProtocolClient client;
    REQUIRE(client.connect(peer.path()));
    const int fd = peer.connection(5000);
    REQUIRE(fd >= 0);

    // The user types a colour. The form shows it, and the window sends it live
    // -- capturing, at that moment, the value in force. That capture is the
    // fix: the handler closes over the value rather than over the field.
    REQUIRE(form.setValue("tab-background", "#010203"));
    const std::string inForceAtSend = form.field("tab-background")->effective;
    REQUIRE(inForceAtSend == "#111111");

    bool answered = false;
    REQUIRE(client.sendSet("tab-background", "#010203",
                           [&](const ConfigMessage& reply) {
                               if (reply.type == ConfigMessageType::Error) {
                                   form.setValue("tab-background", inForceAtSend);
                               }
                               answered = true;
                           }));
    REQUIRE_FALSE(ScriptedPeer::readLine(fd, 2000).empty());

    // THE SAVE GATE, asked of the client exactly as the window's save() asks
    // it: the window manager has not answered, so there is something
    // outstanding and the save must not reach markSaved().
    CHECK(client.pendingRequestCount() == 1);

    // ...and this is what a save let through in that gap does, shown on a copy
    // so the case can state the reason the gate exists without leaving the
    // model in that state: markSaved() makes the TYPED value the effective one,
    // so `effective` is then the value that is about to be refused, and a
    // restore that reads it restores the refusal.
    {
        FormState raced = form;
        raced.markSaved();
        CHECK(raced.field("tab-background")->effective == "#010203");
    }

    // The refusal arrives.
    ConfigMessage error;
    error.type = ConfigMessageType::Error;
    error.key = "tab-background";
    error.reason = "the X server cannot parse that colour";
    REQUIRE(ScriptedPeer::writeLine(fd, error));
    CHECK(client.pumpUntil([&]() { return answered; }, 5000));

    // The field is back at the value that was in force before the set, there is
    // nothing left for a save to write, and the client has nothing outstanding
    // -- which is what releases the save the window held back.
    CHECK(form.value("tab-background") == "#111111");
    REQUIRE(form.field("tab-background") != nullptr);
    CHECK_FALSE(form.field("tab-background")->dirty);
    CHECK(form.edits().empty());
    CHECK(client.pendingRequestCount() == 0);
}


TEST_CASE("the window holds a save back while a live set is outstanding",
          "[wm2_config_smoke]")
{
    // Y1's GTK half, which is wiring rather than a value, guarded on the source
    // in the shape this file already uses for applyLive and the thickness
    // tooltip. Comment-stripped, so this is about what the code does rather
    // than about what its author wrote beside it (D-33).
    const std::string window = sourceOf("apps/wm2-config/main.cpp");
    REQUIRE_FALSE(window.empty());

    const std::size_t saveAt = window.find("void save()");
    REQUIRE(saveAt != std::string::npos);
    const std::size_t saveEnd = window.find("\n    }\n", saveAt);
    REQUIRE(saveEnd != std::string::npos);
    const std::string saveBody =
        withoutLineComments(window.substr(saveAt, saveEnd - saveAt));
    INFO("save, comments stripped:\n" << saveBody);

    // The gate is there...
    const std::size_t gateAt = saveBody.find("pendingRequestCount()");
    CHECK(gateAt != std::string::npos);
    // ...and it is BEFORE the write and before markSaved(), which is the whole
    // of it: a gate after either one has already let the refusal into the file.
    const std::size_t writeAt = saveBody.find("configFileWrite(");
    const std::size_t markAt = saveBody.find("markSaved()");
    REQUIRE(writeAt != std::string::npos);
    REQUIRE(markAt != std::string::npos);
    CHECK(gateAt < writeAt);
    CHECK(gateAt < markAt);

    // A save the window swallowed would be worse than one it refused, so the
    // deferred save is remembered and run when the last reply lands.
    CHECK(saveBody.find("m_saveWhenIdle = true") != std::string::npos);

    const std::size_t queuedAt = window.find("void runQueuedSaveIfIdle()");
    REQUIRE(queuedAt != std::string::npos);
    const std::size_t queuedEnd = window.find("\n    }\n", queuedAt);
    REQUIRE(queuedEnd != std::string::npos);
    const std::string queuedBody =
        withoutLineComments(window.substr(queuedAt, queuedEnd - queuedAt));
    INFO("runQueuedSaveIfIdle, comments stripped:\n" << queuedBody);
    CHECK(queuedBody.find("pendingRequestCount() != 0") != std::string::npos);
    CHECK(queuedBody.find("save()") != std::string::npos);

    // ...and it is reached from the socket callback, which is the only place a
    // reply can arrive.
    const std::size_t readableAt = window.find("static gboolean onSocketReadable(");
    REQUIRE(readableAt != std::string::npos);
    const std::size_t readableEnd = window.find("\n    }\n", readableAt);
    REQUIRE(readableEnd != std::string::npos);
    const std::string readableBody =
        withoutLineComments(window.substr(readableAt, readableEnd - readableAt));
    INFO("onSocketReadable, comments stripped:\n" << readableBody);
    CHECK(readableBody.find("runQueuedSaveIfIdle()") != std::string::npos);
}


TEST_CASE("an error that answers nothing outstanding is surfaced, not dropped",
          "[wm2_config_smoke][protocol][notice]")
{
    // W-04. `A line that matches no outstanding request is a notice or is
    // dropped` had a hole in it: an `error` is neither. The reachable route is
    // the reload correlation -- a foreign `reloaded` popped this client's own
    // pending reload, and the `error` that then refused that reload arrived
    // with an empty queue and was dropped by the bare `return`. The user
    // pressed "Re-read files", it failed, and the settings window said nothing
    // at all.
    //
    // The window manager no longer sends a requester both lines, so this is the
    // second half of the same fix rather than the whole of it: an `error` that
    // matches nothing is still a refusal of something this client asked for,
    // and the user has to be told.
    ScriptedPeer peer(shortSocketPath("w04error"));
    REQUIRE(peer.listening());

    ProtocolClient client;
    REQUIRE(client.connect(peer.path()));

    const int fd = peer.connection(5000);
    REQUIRE(fd >= 0);

    int notices = 0;
    std::vector<std::string> surfaced;
    client.setNoticeHandler([&notices]() { ++notices; });
    client.setProtocolErrorHandler(
        [&surfaced](const std::string& reason) { surfaced.push_back(reason); });

    // Nothing outstanding at all: the queue is empty.
    ConfigMessage error;
    error.type = ConfigMessageType::Error;
    error.reason = "cannot read /home/somebody/.config/wm2-born-again/config";
    REQUIRE(ScriptedPeer::writeLine(fd, error));

    CHECK(client.pumpUntil([&]() { return !surfaced.empty(); }, 5000));
    REQUIRE(surfaced.size() == 1);
    CHECK(surfaced.front() ==
          "cannot read /home/somebody/.config/wm2-born-again/config");

    // It is an ERROR, not a notice: the two go to different places in the
    // window, and reporting a refusal as D-08's "the files were re-read"
    // banner would be worse than silence.
    CHECK(notices == 0);

    // And the connection is still usable afterwards -- an unmatched error is a
    // refusal to report, not a protocol violation to disconnect over.
    CHECK(client.connected());

    // A `reloaded` with an empty queue is still a notice, unchanged.
    ConfigMessage notice;
    notice.type = ConfigMessageType::Reloaded;
    REQUIRE(ScriptedPeer::writeLine(fd, notice));
    CHECK(client.pumpUntil([&]() { return notices == 1; }, 5000));
    CHECK(surfaced.size() == 1);
}


// ---------------------------------------------------------------------------
// Nothing this client does can freeze the settings window (WR-09, WR-10)
// ---------------------------------------------------------------------------

TEST_CASE("the client's descriptor is non-blocking once connected",
          "[wm2_config_smoke][protocol][nonblocking]")
{
    ScriptedPeer peer(shortSocketPath("wr09flags"));
    REQUIRE(peer.listening());

    ProtocolClient client;
    REQUIRE(client.connect(peer.path()));
    REQUIRE(peer.connection(5000) >= 0);

    // The descriptor belongs to a GTK main loop. Every blocking operation on
    // it is a frozen window with no repaint and no way out.
    const int fd = client.fileDescriptor();
    REQUIRE(fd >= 0);
    const int flags = ::fcntl(fd, F_GETFL, 0);
    REQUIRE(flags >= 0);
    CHECK((flags & O_NONBLOCK) != 0);
}

TEST_CASE("a peer that stops reading does not hang the client for ever",
          "[wm2_config_smoke][protocol][nonblocking]")
{
    ScriptedPeer peer(shortSocketPath("wr09wedge"));
    REQUIRE(peer.listening());

    ProtocolClient client;
    REQUIRE(client.connect(peer.path()));
    REQUIRE(peer.connection(5000) >= 0);

    // The peer never reads another byte. Sends fill the socket buffer and then
    // have nowhere to go -- a window manager inside a long modal grab, stopped
    // with SIGSTOP, or simply wedged.
    const std::string big(kConfigProtocolMaxLine / 2, 'x');

    const auto started = std::chrono::steady_clock::now();
    bool refused = false;
    for (int i = 0; i < 4000 && !refused; ++i) {
        if (!client.sendSet("new-window-command", big, nullptr)) refused = true;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();

    INFO("gave up after " << elapsed << " ms");

    // It CAME BACK, and it came back having said so rather than having
    // silently succeeded. With a blocking descriptor this loop never returns.
    CHECK(refused);
    CHECK(elapsed < 30000);
}


TEST_CASE("one readable notification does not license an unbounded read",
          "[wm2_config_smoke][protocol][nonblocking]")
{
    ScriptedPeer peer(shortSocketPath("wr10drain"));
    REQUIRE(peer.listening());

    ProtocolClient client;
    REQUIRE(client.connect(peer.path()));

    const int fd = peer.connection(5000);
    REQUIRE(fd >= 0);

    int notices = 0;
    client.setNoticeHandler([&notices]() { ++notices; });

    // A peer streaming well-formed short lines: every one of them decodes, so
    // the old growth guard -- which required the ABSENCE of a newline -- never
    // fired, and the drain loop stayed inside onReadable() with m_in growing.
    ConfigMessage reloaded;
    reloaded.type = ConfigMessageType::Reloaded;
    const std::string line = configProtocolEncode(reloaded);
    REQUIRE(line.size() < 64);

    // Written non-blocking, so filling the socket buffer cannot hang the case.
    const int peerFlags = ::fcntl(fd, F_GETFL, 0);
    REQUIRE(peerFlags >= 0);
    REQUIRE(::fcntl(fd, F_SETFL, peerFlags | O_NONBLOCK) == 0);

    // Queued in BATCHES rather than one line per send(). An AF_UNIX stream
    // socket charges its buffer per skb, not per byte, so a few hundred
    // twenty-byte writes fill it long before the byte count gets anywhere near
    // the drain bound -- which would make the case measure the kernel's
    // accounting instead of the client's loop.
    constexpr int kLinesPerBatch = 100;
    std::string batch;
    for (int i = 0; i < kLinesPerBatch; ++i) batch += line;

    int sent = 0;
    for (;;) {
        const ssize_t n = ::send(fd, batch.data(), batch.size(), MSG_NOSIGNAL);
        if (n != static_cast<ssize_t>(batch.size())) break;
        sent += kLinesPerBatch;
        if (sent > 100000) break;
    }
    // More than one callback's worth, or the case proves nothing. The queue
    // depth is the kernel's to decide, so a host whose AF_UNIX buffers are too
    // small to hold more than the bound SKIPS with the reason rather than
    // passing vacuously.
    const int needed =
        static_cast<int>(kProtocolClientMaxDrainPerCallback / line.size()) + 1;
    if (sent < needed) {
        SKIP("this host's AF_UNIX socket buffer held only " +
             std::to_string(sent) + " frames, fewer than the " +
             std::to_string(needed) + " one drain bound allows, so a bounded "
             "drain cannot be told from an unbounded one here");
    }

    client.onReadable();
    const int afterOne = notices;

    INFO("peer queued " << sent << " lines; one callback consumed " << afterOne);

    // Progress was made, and the callback RETURNED with work still waiting --
    // which is what keeps the main loop repainting instead of the process
    // growing until the OOM killer takes it.
    CHECK(afterOne > 0);
    CHECK(afterOne < sent);

    // And nothing is lost: the rest arrives on the callbacks that follow.
    CHECK(client.pumpUntil([&]() { return notices >= sent; }, 10000));
    CHECK(client.connected());
}


TEST_CASE("a disconnect from a request path still announces itself",
          "[wm2_config_smoke][protocol][nonblocking]")
{
    // The hook WR-11's fix hangs off, proved where it can be proved without a
    // toolkit. apps/wm2-config/main.cpp removes its GLib source from the state
    // handler, because ProtocolClient::disconnect() close()s the descriptor
    // and is reachable from request() -- a path that runs from a GTK button
    // handler, not from the source callback. If the state handler did NOT fire
    // on that path, the source would go on polling a closed descriptor number
    // that GDK, cairo or fontconfig may since have reused.
    ScriptedPeer peer(shortSocketPath("wr11state"));
    REQUIRE(peer.listening());

    ProtocolClient client;
    REQUIRE(client.connect(peer.path()));

    const int fd = peer.connection(5000);
    REQUIRE(fd >= 0);

    int transitions = 0;
    bool sawDisconnected = false;
    client.setStateHandler([&]() {
        ++transitions;
        if (!client.connected()) sawDisconnected = true;
    });

    // The window manager goes away. The next send from a handler fails, and
    // request() disconnects.
    ::shutdown(fd, SHUT_RDWR);
    ::close(fd);

    // The first send may still be swallowed by the socket buffer, so the loop
    // is bounded rather than assuming which one fails.
    for (int i = 0; i < 100 && client.connected(); ++i) {
        client.sendSet("frame-thickness", "9", nullptr);
    }

    CHECK_FALSE(client.connected());
    CHECK(sawDisconnected);
    CHECK(transitions >= 1);
    // And the descriptor really is gone, which is what makes leaving a source
    // attached to its number dangerous.
    CHECK(client.fileDescriptor() < 0);
}


TEST_CASE("a menu entry field with an outer space is refused where it is typed "
          "and on the wire",
          "[wm2_config_smoke][menu]")
{
    // W-03. The FILE grammar trims the value it reads
    // (Config::applyFile -> trim), and the WIRE grammar does not
    // (parseMenuEntriesValue calls applyKeyValue directly). So a name of
    // " Mail" is accepted live, shows in the root menu with its space, is
    // saved as `menu-entry-name =  Mail`, and comes back as "Mail" the next
    // time anything reloads -- at which point `get menu-entries` disagrees with
    // what the window manager held a moment ago and the Menu page silently
    // rewrites the user's row.
    //
    // Both directions are asserted here, because agreement between them is the
    // property: the dialog refuses it where it is typed, and the wire refuses
    // it for anything that reaches the window manager by another route.
    std::string reason;

    MenuEntryDraft leadingName;
    leadingName.name = " Mail";
    leadingName.command = "thunderbird";
    CHECK_FALSE(leadingName.complete(reason));
    CHECK(reason.find("space") != std::string::npos);

    MenuEntryDraft trailingName;
    trailingName.name = "Mail ";
    trailingName.command = "thunderbird";
    CHECK_FALSE(trailingName.complete(reason));

    MenuEntryDraft outerCategory;
    outerCategory.name = "Mail";
    outerCategory.command = "thunderbird";
    outerCategory.category = "Network ";
    CHECK_FALSE(outerCategory.complete(reason));

    // The command is exempt, in the dialog and everywhere else: it is tokenised
    // on whitespace on every route, so an outer space in it is not carried by
    // either representation. A refusal there would be a sentence about the file
    // format that is not true of that field.
    MenuEntryDraft paddedCommand;
    paddedCommand.name = "Mail";
    paddedCommand.command = "  thunderbird  ";
    paddedCommand.category = "Network";
    CHECK(paddedCommand.complete(reason));

    // The wire direction. This is the value a `set menu-entries` carries and
    // the value `get menu-entries` returns.
    std::vector<AppEntry> parsed;
    std::string parseReason;
    CHECK_FALSE(parseMenuEntriesValue(
        "menu-entry-name= Mail;menu-entry-command=thunderbird;"
        "menu-entry-category=Network", parsed, parseReason));
    CHECK(parseReason.find("space") != std::string::npos);

    parseReason.clear();
    CHECK_FALSE(parseMenuEntriesValue(
        "menu-entry-name=Mail;menu-entry-command=thunderbird;"
        "menu-entry-category=Network ", parsed, parseReason));

    // And the ordinary value still parses, with the command's own spacing
    // tokenised away exactly as before.
    parseReason.clear();
    REQUIRE(parseMenuEntriesValue(
        "menu-entry-name=Mail;menu-entry-command=  thunderbird  --safe-mode;"
        "menu-entry-category=Network", parsed, parseReason));
    REQUIRE(parsed.size() == 1);
    CHECK(parsed[0].name == "Mail");
    CHECK(parsed[0].category == "Network");
    REQUIRE(parsed[0].execArgv.size() == 2);
    CHECK(parsed[0].execArgv[0] == "thunderbird");
}


TEST_CASE("a menu entry containing a ';' is refused where it is typed",
          "[wm2_config_smoke][menu]")
{
    // The `menu-entries` WIRE grammar separates records with ';' and has no
    // escape. The FILE grammar has no such separator, so
    // `menu-entry-name = Mail; News` is a perfectly valid line -- which means
    // an entry saved with one is read back by the window manager, returned by
    // `get menu-entries`, and then fails to parse in this program. The Menu
    // page stops tracking what the window manager holds, for the rest of the
    // session and every session after it.
    std::string reason;

    MenuEntryDraft inName;
    inName.name = "Mail; News";
    inName.command = "thunderbird";
    CHECK_FALSE(inName.complete(reason));
    CHECK(reason.find("';'") != std::string::npos);

    MenuEntryDraft inCommand;
    inCommand.name = "Mail";
    inCommand.command = "sh -c thunderbird;true";
    CHECK_FALSE(inCommand.complete(reason));

    MenuEntryDraft inCategory;
    inCategory.name = "Mail";
    inCategory.command = "thunderbird";
    inCategory.category = "Net;work";
    CHECK_FALSE(inCategory.complete(reason));

    // And an entry without one is still accepted: the refusal is about the one
    // character the wire grammar reserves, not about punctuation in general.
    MenuEntryDraft fine;
    fine.name = "Mail & News";
    fine.command = "thunderbird --safe-mode";
    fine.category = "Network";
    CHECK(fine.complete(reason));

    // The round trip the refusal protects: what the dialog accepted survives
    // the wire encoding it is about to travel over.
    std::vector<AppEntry> entries{fine.toEntry()};
    Config carrier;
    carrier.manualMenuEntries = entries;
    std::vector<AppEntry> back;
    std::string parseReason;
    REQUIRE(parseMenuEntriesValue(configMenuEntriesValue(carrier), back, parseReason));
    REQUIRE(back.size() == 1);
    CHECK(back[0].name == "Mail & News");
    CHECK(back[0].category == "Network");
}
