// The configuration socket's display-free half (CGUI-02, plan 09-03).
//
// DISPLAY-FREE BY CONSTRUCTION, and that is the point rather than a convenience:
// this binary links Catch2 plus src/SocketServer.cpp and nothing else, with no
// PkgConfig::X11, no Xft include path and no display fixture. If it ever needs
// an X server, the socket module has grown a dependency the window manager,
// wm2-ctl and wm2-config would all have inherited -- and wm2-ctl in particular
// must stay GTK-free and display-agnostic (D-17).
//
// Tags are registered as ctest LABELS via ADD_TAGS_AS_LABELS (D-33), so this
// group is selectable with `ctest -L '^config_socket$' --no-tests=error`.
//
// The rules this file inherits:
//
//   NO NAME-PATTERN PROCESS CONTROL. Nothing here starts a second process at
//   all; every socket it creates it also closes by the descriptor it opened.
//
//   NO FIXED /tmp NAME (T-8-TMP). Every temporary directory comes from
//   mkdtemp(), so two concurrent runs cannot collide and no path is guessable.
//
//   THE ENVIRONMENT IS RESTORED. configSocketDirectory() reads XDG_RUNTIME_DIR,
//   and Catch2 runs every case in one process, so a case that changed it and
//   walked away would silently decide the next case's answer.

#include <catch2/catch_test_macros.hpp>

#include "SocketServer.h"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Saves XDG_RUNTIME_DIR on construction and puts it back on destruction,
// whether the case passed, failed or threw.
class RuntimeDirEnv {
public:
    RuntimeDirEnv()
    {
        const char* v = std::getenv("XDG_RUNTIME_DIR");
        m_had = (v != nullptr);
        if (m_had) m_saved = v;
    }
    ~RuntimeDirEnv()
    {
        if (m_had) ::setenv("XDG_RUNTIME_DIR", m_saved.c_str(), 1);
        else       ::unsetenv("XDG_RUNTIME_DIR");
    }
    RuntimeDirEnv(const RuntimeDirEnv&) = delete;
    RuntimeDirEnv& operator=(const RuntimeDirEnv&) = delete;

    static void set(const std::string& v) { ::setenv("XDG_RUNTIME_DIR", v.c_str(), 1); }
    static void clear()                   { ::unsetenv("XDG_RUNTIME_DIR"); }

private:
    bool m_had = false;
    std::string m_saved;
};


// A private directory with an unguessable name, removed on destruction along
// with everything this file put in it.
class TempDir {
public:
    TempDir()
    {
        char tmpl[] = "/tmp/wm2-socket-test-XXXXXX";
        const char* made = ::mkdtemp(tmpl);
        if (made != nullptr) m_path = made;
    }
    ~TempDir()
    {
        for (const std::string& child : m_children) ::unlink(child.c_str());
        if (!m_path.empty()) ::rmdir(m_path.c_str());
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    bool valid() const { return !m_path.empty(); }
    const std::string& path() const { return m_path; }

    std::string child(const std::string& name)
    {
        const std::string p = m_path + "/" + name;
        m_children.push_back(p);
        return p;
    }

private:
    std::string m_path;
    std::vector<std::string> m_children;
};


// Bind a listening socket at `path`. Returns the descriptor, or -1.
int bindListener(const std::string& path)
{
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.c_str(), path.size());

    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    if (::listen(fd, 4) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

}  // namespace


// -----------------------------------------------------------------------------
// The decision
// -----------------------------------------------------------------------------

TEST_CASE("A readable connection is read and a readable listener accepts",
          "[config_socket][decide]")
{
    CHECK(socketServerDecide(POLLIN) == SocketAction::ReadClient);
    CHECK(socketServerDecide(POLLIN, SocketRole::Client) == SocketAction::ReadClient);
    CHECK(socketServerDecide(POLLIN, SocketRole::Listener) == SocketAction::Accept);
}

TEST_CASE("Every failure revent closes, for both roles", "[config_socket][decide]")
{
    for (short bit : { static_cast<short>(POLLERR),
                       static_cast<short>(POLLHUP),
                       static_cast<short>(POLLNVAL) }) {
        CHECK(socketServerDecide(bit) == SocketAction::CloseClient);
        CHECK(socketServerDecide(bit, SocketRole::Listener) == SocketAction::CloseClient);
    }
}

TEST_CASE("Nothing reported is nothing to do", "[config_socket][decide]")
{
    CHECK(socketServerDecide(0) == SocketAction::Idle);
    CHECK(socketServerDecide(0, SocketRole::Listener) == SocketAction::Idle);
    // POLLOUT alone is not this function's business -- the caller flushes on it
    // directly -- so it must not be mistaken for readability.
    CHECK(socketServerDecide(POLLOUT) == SocketAction::Idle);
}

TEST_CASE("A peer that wrote and hung up is read before it is closed",
          "[config_socket][decide]")
{
    // The ORDER inside the decision, asserted rather than assumed. A client
    // that sends a final request and closes reports both bits at once; reading
    // first is what makes its last message answerable.
    const short both = static_cast<short>(POLLIN | POLLHUP);
    CHECK(socketServerDecide(both) == SocketAction::ReadClient);

    // For the listener there is no payload to rescue, so the failure wins.
    CHECK(socketServerDecide(both, SocketRole::Listener) == SocketAction::CloseClient);
}

TEST_CASE("No revents value produces an unnamed outcome", "[config_socket][decide]")
{
    // Exhaustive over every bit combination poll() can deliver in the low byte,
    // for both roles: the claim is that the decision is TOTAL, and a claim
    // about all inputs is worth checking over all inputs.
    for (int bits = 0; bits < 256; ++bits) {
        const short revents = static_cast<short>(bits);
        for (SocketRole role : { SocketRole::Client, SocketRole::Listener }) {
            const SocketAction a = socketServerDecide(revents, role);
            const bool named = (a == SocketAction::Accept) ||
                               (a == SocketAction::ReadClient) ||
                               (a == SocketAction::CloseClient) ||
                               (a == SocketAction::Idle);
            CHECK(named);
        }
    }
}


// -----------------------------------------------------------------------------
// The path (DISC-02)
// -----------------------------------------------------------------------------

TEST_CASE("An absolute XDG_RUNTIME_DIR names the socket directory",
          "[config_socket][path]")
{
    RuntimeDirEnv guard;
    RuntimeDirEnv::set("/run/user/4242");
    CHECK(configSocketDirectory() == "/run/user/4242/wm2-born-again");
}

TEST_CASE("A relative or absent XDG_RUNTIME_DIR takes the one documented fallback",
          "[config_socket][path]")
{
    RuntimeDirEnv guard;

    const std::string expected =
        "/tmp/wm2-born-again-" + std::to_string(static_cast<unsigned long>(::geteuid()));

    // Relative: rejected exactly as xdgConfigHome() rejects a relative
    // XDG_CONFIG_HOME -- the variable must be absolute or it is not honoured.
    RuntimeDirEnv::set("run/user/4242");
    CHECK(configSocketDirectory() == expected);

    // Empty is not absolute either.
    RuntimeDirEnv::set("");
    CHECK(configSocketDirectory() == expected);

    RuntimeDirEnv::clear();
    CHECK(configSocketDirectory() == expected);
}

TEST_CASE("The display name is sanitised into the socket file name",
          "[config_socket][path]")
{
    RuntimeDirEnv guard;
    RuntimeDirEnv::set("/run/user/4242");
    const std::string dir = "/run/user/4242/wm2-born-again";

    // The ordinary case, and the reason this exists: two displays must not
    // contend for one socket, which is what lets the fixtures run in parallel.
    CHECK(configSocketPath(":1") == dir + "/socket_1");
    CHECK(configSocketPath(":0") == dir + "/socket_0");
    CHECK(configSocketPath(":123") != configSocketPath(":124"));

    // A remote-style display, and a name carrying a path separator: neither may
    // escape the directory.
    CHECK(configSocketPath("host:0.0") == dir + "/sockethost_0.0");
    // '.' is preserved and '/' is not, which is what makes escape impossible:
    // the result is always a leaf name inside the directory.
    CHECK(configSocketPath("../../etc/passwd") == dir + "/socket.._.._etc_passwd");

    // Total: an absent or empty display still names a socket rather than
    // yielding the directory itself.
    CHECK(configSocketPath(nullptr) == dir + "/socket_");
    CHECK(configSocketPath("") == dir + "/socket_");
}

TEST_CASE("A path too long for the address structure is refused, not truncated",
          "[config_socket][path]")
{
    struct sockaddr_un probe;
    const std::size_t limit = sizeof(probe.sun_path);   // 108 on Linux

    CHECK(configSocketPathFits("/run/user/1000/wm2-born-again/socket_1"));

    // The boundary itself, both sides of it: `limit - 1` bytes plus the
    // terminator exactly fills the field; one more does not (RESEARCH Pitfall
    // 4). Asserted here because bind() reports neither -- it truncates.
    CHECK(configSocketPathFits(std::string(limit - 1, 'a')));
    CHECK_FALSE(configSocketPathFits(std::string(limit, 'a')));
    CHECK_FALSE(configSocketPathFits(std::string(limit + 200, 'a')));

    // And the refusal is reachable through the real path builder, not only
    // through a hand-made string.
    RuntimeDirEnv guard;
    RuntimeDirEnv::set("/run/" + std::string(200, 'x'));
    CHECK_FALSE(configSocketPathFits(configSocketPath(":1")));
}


// -----------------------------------------------------------------------------
// The peer verdict (D-16, T-9-11 / T-9-12)
// -----------------------------------------------------------------------------

TEST_CASE("A peer with this uid is admitted and any other uid is not",
          "[config_socket][peer]")
{
    int sv[2] = { -1, -1 };
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    const uid_t self = ::geteuid();

    uid_t peer = static_cast<uid_t>(-1);
    CHECK(configSocketPeerUid(sv[0], peer));
    CHECK(peer == self);

    CHECK(configSocketPeerVerdict(sv[0], self) == PeerVerdict::SameUid);

    // ANY other uid is foreign. Asserted from both directions, because the
    // comparison is an equality and D-16 turns on that: uid 0 is root, and a
    // window manager running as root refuses this user exactly as this user's
    // window manager refuses root (T-9-12).
    CHECK(configSocketPeerVerdict(sv[0], self + 1) == PeerVerdict::ForeignUid);
    if (self != 0) {
        CHECK(configSocketPeerVerdict(sv[0], 0) == PeerVerdict::ForeignUid);
    }

    ::close(sv[0]);
    ::close(sv[1]);
}

TEST_CASE("A descriptor the kernel will not vouch for is not admitted",
          "[config_socket][peer]")
{
    int pipefd[2] = { -1, -1 };
    REQUIRE(::pipe(pipefd) == 0);

    uid_t peer = 0;
    CHECK_FALSE(configSocketPeerUid(pipefd[0], peer));

    // Unknown is a distinct verdict from ForeignUid so a refusal can say why,
    // but it is emphatically NOT SameUid -- an unanswerable question is never
    // resolved in the connection's favour.
    const PeerVerdict v = configSocketPeerVerdict(pipefd[0], ::geteuid());
    CHECK(v == PeerVerdict::Unknown);
    CHECK(v != PeerVerdict::SameUid);

    ::close(pipefd[0]);
    ::close(pipefd[1]);
}


// -----------------------------------------------------------------------------
// The stale verdict (T-9-17)
// -----------------------------------------------------------------------------

TEST_CASE("Nothing at the path is nothing to reclaim", "[config_socket][stale]")
{
    TempDir dir;
    REQUIRE(dir.valid());
    CHECK(configSocketStaleVerdict(dir.path() + "/absent") == StaleVerdict::NoFile);
}

TEST_CASE("An abandoned socket is stale and a live one is not",
          "[config_socket][stale]")
{
    TempDir dir;
    REQUIRE(dir.valid());

    // LIVE: bound, listening, and the descriptor still held. This is a running
    // window manager, and its socket must survive a second one starting.
    const std::string livePath = dir.child("live");
    const int live = bindListener(livePath);
    REQUIRE(live >= 0);
    CHECK(configSocketStaleVerdict(livePath) == StaleVerdict::Live);

    // STALE: bound, then the process went away without unlinking -- which is
    // exactly what a crashed window manager leaves behind. The file is still
    // there; nothing answers on it.
    const std::string stalePath = dir.child("stale");
    const int abandoned = bindListener(stalePath);
    REQUIRE(abandoned >= 0);
    ::close(abandoned);
    struct stat st;
    REQUIRE(::lstat(stalePath.c_str(), &st) == 0);   // the node really did survive
    CHECK(configSocketStaleVerdict(stalePath) == StaleVerdict::Stale);

    // The live one is STILL live after all that: the two verdicts are read from
    // the sockets themselves, not from the order they were created in.
    CHECK(configSocketStaleVerdict(livePath) == StaleVerdict::Live);

    ::close(live);
}

TEST_CASE("A file that is not a socket is never reclaimed", "[config_socket][stale]")
{
    TempDir dir;
    REQUIRE(dir.valid());

    const std::string filePath = dir.child("regular");
    const int fd = ::open(filePath.c_str(), O_CREAT | O_WRONLY, 0600);
    REQUIRE(fd >= 0);
    ::close(fd);

    // Live, meaning "not mine to remove". Unlinking whatever happens to be in
    // the way is how a pre-placed path gets quietly replaced (T-9-17), so the
    // conservative verdict is the correct one even though nothing is listening.
    CHECK(configSocketStaleVerdict(filePath) == StaleVerdict::Live);

    // And the file is still there: the verdict function never unlinks anything.
    struct stat st;
    CHECK(::lstat(filePath.c_str(), &st) == 0);
}


// -----------------------------------------------------------------------------
// Re-entrancy: a handler that calls back into the server (CR-01)
// -----------------------------------------------------------------------------
//
// The window manager's own handler does exactly this. `reload` reaches
// WindowManager::reloadConfigFromDisk(), which ends by broadcasting D-08's
// notice on the very server that is in the middle of calling it -- and
// broadcast() reaps the connection vector. The transport therefore has to be
// re-entrancy-safe on its own account rather than by a contract the one handler
// that exists does not honour.
//
// These cases drive the server directly, with no window manager and no display:
// a scripted handler that broadcasts from inside a `reload` is the whole
// reproduction.

namespace {

// The server's own directory, rooted in a temporary XDG_RUNTIME_DIR so nothing
// touches the real per-user one. Removes the directory listen() created.
class ServerHome {
public:
    explicit ServerHome(TempDir& dir)
    {
        RuntimeDirEnv::set(dir.path());
        m_directory = configSocketDirectory();
    }
    ~ServerHome() { ::rmdir(m_directory.c_str()); }

    ServerHome(const ServerHome&) = delete;
    ServerHome& operator=(const ServerHome&) = delete;

    const std::string& directory() const { return m_directory; }

private:
    std::string m_directory;
};


// A client end of the socket, closed by the descriptor it opened.
class ClientEnd {
public:
    explicit ClientEnd(const std::string& path)
    {
        m_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (m_fd < 0) return;

        struct sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        if (path.size() + 1 > sizeof(addr.sun_path)) { close(); return; }
        std::memcpy(addr.sun_path, path.c_str(), path.size());

        if (::connect(m_fd, reinterpret_cast<struct sockaddr*>(&addr),
                      sizeof(addr)) != 0) {
            close();
        }
    }
    ~ClientEnd() { close(); }

    ClientEnd(const ClientEnd&) = delete;
    ClientEnd& operator=(const ClientEnd&) = delete;

    bool open() const { return m_fd >= 0; }
    int  fd() const { return m_fd; }

    void close()
    {
        if (m_fd >= 0) ::close(m_fd);
        m_fd = -1;
    }

    bool send(const std::string& bytes) const
    {
        std::size_t sent = 0;
        while (sent < bytes.size()) {
            const ssize_t n = ::send(m_fd, bytes.data() + sent, bytes.size() - sent,
                                     MSG_NOSIGNAL);
            if (n <= 0) return false;
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    // Everything readable right now, appended to `out`. Never blocks.
    void drain(std::string& out) const
    {
        for (;;) {
            char buf[4096];
            const ssize_t n = ::recv(m_fd, buf, sizeof(buf), MSG_DONTWAIT);
            if (n <= 0) return;
            out.append(buf, static_cast<std::size_t>(n));
        }
    }

private:
    int m_fd = -1;
};


std::string encode(ConfigMessageType type)
{
    ConfigMessage m;
    m.type = type;
    return configProtocolEncode(m);
}


std::string helloLine()
{
    ConfigMessage m;
    m.type = ConfigMessageType::Hello;
    m.program = "test";
    m.protocol = kConfigProtocolVersion;
    return configProtocolEncode(m);
}


std::string getLine(const std::string& key)
{
    ConfigMessage m;
    m.type = ConfigMessageType::Get;
    m.key = key;
    return configProtocolEncode(m);
}


// The window manager's handler, reduced to the one property that matters here:
// a `reload` broadcasts on the server that is calling it (src/Manager.cpp's
// reloadConfigFromDisk does precisely this, D-08).
class ReentrantHandler {
public:
    explicit ReentrantHandler(ConfigSocketServer& server) : m_server(server) {}

    ConfigSocketReply operator()(const ConfigSocketRequest& request) const
    {
        ConfigSocketReply out;
        ConfigMessage in;
        if (configProtocolDecode(request.line, in) != ConfigDecodeResult::Ok) {
            ConfigMessage error;
            error.type = ConfigMessageType::Error;
            error.reason = "undecodable";
            out.line = configProtocolEncode(error);
            return out;
        }

        switch (in.type) {
        case ConfigMessageType::Hello: {
            ConfigMessage ack;
            ack.type = ConfigMessageType::HelloAck;
            ack.program = "wm2-born-again";
            ack.protocol = kConfigProtocolVersion;
            out.line = configProtocolEncode(ack);
            out.helloAccepted = true;
            return out;
        }
        case ConfigMessageType::Reload:
            // THE RE-ENTRY. Everything CR-01 is about happens inside this call.
            m_server.broadcast(encode(ConfigMessageType::Reloaded));
            out.line = encode(ConfigMessageType::Reloaded);
            return out;
        case ConfigMessageType::Get: {
            ConfigMessage value;
            value.type = ConfigMessageType::Value;
            value.key = in.key;
            value.value = "answered";
            out.line = configProtocolEncode(value);
            return out;
        }
        default: {
            ConfigMessage error;
            error.type = ConfigMessageType::Error;
            error.reason = "unexpected";
            out.line = configProtocolEncode(error);
            return out;
        }
        }
    }

private:
    ConfigSocketServer& m_server;
};


// One servicing pass, exactly as WindowManager::nextEvent() performs it: build
// the set the server asked for, poll it, hand it back at the same index.
void pump(ConfigSocketServer& server, const ConfigSocketServer::Handler& handler,
          int timeoutMs = 50)
{
    std::vector<struct pollfd> fds;
    server.appendPollFds(fds);
    if (fds.empty()) return;
    ::poll(fds.data(), fds.size(), timeoutMs);
    server.service(fds, 0, handler);
}


std::size_t countLines(const std::string& text, const std::string& needle)
{
    std::size_t found = 0;
    std::size_t start = 0;
    for (;;) {
        const std::size_t nl = text.find('\n', start);
        if (nl == std::string::npos) break;
        if (text.compare(start, nl - start, needle) == 0) ++found;
        start = nl + 1;
    }
    return found;
}

}  // namespace


TEST_CASE("A broadcast from inside a handler does not disturb the connection "
          "being served",
          "[config_socket][reentrancy]")
{
    RuntimeDirEnv guard;
    TempDir home;
    REQUIRE(home.valid());
    ServerHome server_home(home);

    ConfigSocketServer server;
    REQUIRE(server.listen(":reentry"));

    ReentrantHandler handler(server);
    const ConfigSocketServer::Handler fn =
        [&handler](const ConfigSocketRequest& r) { return handler(r); };

    // TWO connections, and the ORDER matters: A is accepted first and therefore
    // sits at index 0, ahead of the connection whose request does the work.
    ClientEnd a(server.path());
    ClientEnd b(server.path());
    REQUIRE(a.open());
    REQUIRE(b.open());

    pump(server, fn);
    REQUIRE(server.clientCount() == 2);

    // Both complete the handshake: broadcast() writes to hello-completed
    // connections only, so a silent A would never be in the set at all.
    REQUIRE(a.send(helloLine()));
    REQUIRE(b.send(helloLine()));
    pump(server, fn);

    std::string fromA;
    std::string fromB;
    a.drain(fromA);
    b.drain(fromB);
    REQUIRE(countLines(fromA, "{\"type\":\"hello-ack\",\"program\":\"wm2-born-again\","
                              "\"protocol\":1}") == 1);
    fromA.clear();
    fromB.clear();

    // A goes away without the server having noticed yet -- a wm2-ctl that
    // exited, a settings window that was killed. Its Connection is still at
    // index 0 with nothing reaped.
    a.close();

    // B pipelines two frames in ONE write, which is what makes the damage
    // visible rather than merely latent: the reload's broadcast reaps A from
    // under the framing loop that is still holding B's buffer, and the second
    // frame is what that loop was about to read.
    REQUIRE(b.send(encode(ConfigMessageType::Reload) + getLine("tab-font")));

    pump(server, fn);
    pump(server, fn);
    b.drain(fromB);

    // The reply to the reload, and the reply to the request that followed it in
    // the same write. Both, or the framing loop lost its place.
    CHECK(countLines(fromB, "{\"type\":\"reloaded\"}") >= 1);
    CHECK(countLines(fromB, "{\"type\":\"value\",\"key\":\"tab-font\","
                            "\"value\":\"answered\"}") == 1);

    // And the server is still serving: another request is answered normally.
    REQUIRE(b.send(getLine("menu-font")));
    pump(server, fn);
    fromB.clear();
    b.drain(fromB);
    CHECK(countLines(fromB, "{\"type\":\"value\",\"key\":\"menu-font\","
                            "\"value\":\"answered\"}") == 1);

    server.close();
}


// -----------------------------------------------------------------------------
// The socket DIRECTORY is not followed through a symlink (CR-03)
// -----------------------------------------------------------------------------
//
// The socket NODE's handling has always been careful about exactly this class
// of trick: configSocketStaleVerdict() lstats, requires S_ISSOCK, and refuses to
// unlink anything this process did not create (T-9-17). The directory's was
// not. mkdir() on an existing symlink-to-directory returns EEXIST, and chmod()
// and stat() both FOLLOW symlinks -- so every check inspected the link's
// target rather than the path the socket would live under.
//
// D-16's documented fallback is /tmp/wm2-born-again-<uid>: a name in a
// world-writable directory, predictable from the uid, and the ordinary state
// under `su`, a bare startx, and the minimal VNC session scripts this project
// targets. An attacker who plants that name as a symlink to a directory the
// victim owns gets an attacker-directed `chmod 0700` on it, plus a socket node
// inside it.
//
// The case drives the XDG_RUNTIME_DIR route rather than the /tmp one, and that
// costs nothing: listen() resolves ONE directory string and applies ONE
// sequence of checks to it whichever branch configSocketDirectory() took, so
// the code under test is identical and no internal lever has to be invented to
// reach it.

TEST_CASE("A socket directory that is a symlink is refused, and its target is "
          "left alone",
          "[config_socket][directory]")
{
    RuntimeDirEnv guard;
    TempDir home;
    REQUIRE(home.valid());
    RuntimeDirEnv::set(home.path());

    // The victim: a directory this user owns, with a mode the window manager
    // has no business changing.
    const std::string victim = home.path() + "/victim";
    REQUIRE(::mkdir(victim.c_str(), 0755) == 0);
    REQUIRE(::chmod(victim.c_str(), 0755) == 0);

    // The plant, at exactly the name the window manager is about to use.
    const std::string planted = configSocketDirectory();
    REQUIRE(planted == home.path() + "/wm2-born-again");
    REQUIRE(::symlink(victim.c_str(), planted.c_str()) == 0);

    {
        ConfigSocketServer server;
        CHECK_FALSE(server.listen(":symlink"));
        CHECK_FALSE(server.isListening());
    }

    // The refusal is only worth anything if nothing happened on the way to it.
    struct stat vst;
    REQUIRE(::lstat(victim.c_str(), &vst) == 0);
    CHECK(S_ISDIR(vst.st_mode));
    CHECK((vst.st_mode & 07777) == 0755);   // NOT chmod'ed to 0700 through the link

    // And no socket node was planted inside the victim's directory.
    struct stat sst;
    CHECK(::lstat((victim + "/socket_symlink").c_str(), &sst) != 0);

    // The link itself is still a link -- nothing replaced it either.
    struct stat pst;
    REQUIRE(::lstat(planted.c_str(), &pst) == 0);
    CHECK(S_ISLNK(pst.st_mode));

    ::unlink((victim + "/socket_symlink").c_str());
    ::unlink(planted.c_str());
    ::rmdir(victim.c_str());
}


TEST_CASE("A directory owned by somebody else is refused",
          "[config_socket][directory]")
{
    // The ownership half of the same check, driven where it can be driven
    // without becoming another user: /tmp itself is a directory this process
    // does not own and whose mode is not 0700, and pointing the resolver at a
    // path whose FINAL component is one of those is enough to reach the
    // refusal. (`/` is chosen over `/tmp` so the name the server would use is
    // "/wm2-born-again", which does not exist and cannot be created -- the
    // mkdir refusal and the ownership refusal are both correct outcomes and
    // both are "no socket".)
    RuntimeDirEnv guard;
    RuntimeDirEnv::set("/");

    ConfigSocketServer server;
    CHECK_FALSE(server.listen(":notmine"));
    CHECK_FALSE(server.isListening());
}


TEST_CASE("An ordinary directory is still accepted and its mode corrected",
          "[config_socket][directory]")
{
    // The other side of the refusal: the guard must not have made the ordinary
    // path unreachable, and it must still be what SETS the mode when the
    // directory was created by something with a looser umask.
    RuntimeDirEnv guard;
    TempDir home;
    REQUIRE(home.valid());
    RuntimeDirEnv::set(home.path());

    const std::string directory = configSocketDirectory();
    REQUIRE(::mkdir(directory.c_str(), 0755) == 0);
    REQUIRE(::chmod(directory.c_str(), 0755) == 0);

    {
        ConfigSocketServer server;
        REQUIRE(server.listen(":ordinary"));
        CHECK(server.isListening());
        server.close();
    }

    struct stat st;
    REQUIRE(::lstat(directory.c_str(), &st) == 0);
    CHECK(S_ISDIR(st.st_mode));
    CHECK((st.st_mode & 07777) == 0700);

    ::rmdir(directory.c_str());
}
