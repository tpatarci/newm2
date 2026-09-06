// wm2-ctl -- the command-line client for the window manager's configuration
// socket (CGUI-02, D-17, plan 09-04).
//
// D-17 puts this tool in the WINDOW MANAGER's package rather than the
// configuration GUI's, and that placement is the whole of its design brief: a
// droplet reached over SSH, with no desktop open and no toolkit installed, must
// still be able to ask the running window manager what it is doing and tell it
// to change. So this file links libc and libstdc++ and nothing else -- no X11,
// no Xft, no GTK, no GLib. A [wm_config_live] acceptance check runs `ldd` over
// the built binary and fails if any of those four appear.
//
// It is also the protocol's REFERENCE CLIENT: tests/test_wm_config_live.cpp
// drives the window manager through this binary as a child process rather than
// through a hand-rolled socket client, so the thing the user runs and the thing
// the suite proves are the same program.
//
// ---------------------------------------------------------------------------
// DISC-01a -- EXIT CODES
// ---------------------------------------------------------------------------
//
//   0  the window manager acknowledged
//   1  the window manager replied with an error (the reason goes to stderr)
//   2  there is no window manager to talk to -- no socket, a refused
//      connection, or a connection that produced no reply
//   3  usage error -- this tool could not work out what was being asked
//
// The split between 1 and 2 is the point of having a CLI at all: a shell script
// can branch on "not running" separately from "running and refused", which a
// single non-zero code would conflate.
//
// ---------------------------------------------------------------------------
// DISC-01b -- HOW THE SOCKET IS FOUND
// ---------------------------------------------------------------------------
//
// The path is RESOLVED, from $DISPLAY and the runtime directory, through the
// very same configSocketPath() the window manager binds with; `--socket <path>`
// overrides it. This tool deliberately does NOT read the _WM2_CONFIG_SOCKET
// root-window property, even though that is the discovery mechanism DISC-03
// blesses for everybody else: reading a property means opening a display, and a
// tool whose entire value is that it needs no display cannot be the one client
// that does.

#include "Config.h"
#include "ConfigProtocol.h"
#include "SocketServer.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

// Exit codes, named so no call site spells a bare integer.
constexpr int kExitOk      = 0;
constexpr int kExitRefused = 1;
constexpr int kExitNoSocket = 2;
constexpr int kExitUsage   = 3;

// Every exchange is bounded by this. Long enough for a window manager under a
// sanitizer with a busy machine behind it, short enough that a wedged event
// loop fails the command rather than hanging a shell script forever.
constexpr int kDeadlineMs = 15000;

void warn(const std::string& text)
{
    // The project's convention, matched exactly: every diagnostic this codebase
    // emits is `wm2: `-prefixed on stderr.
    std::fprintf(stderr, "wm2: %s\n", text.c_str());
}

void printUsage(std::FILE* out)
{
    std::fprintf(out,
        "Usage: wm2-ctl [--socket PATH] COMMAND\n"
        "\n"
        "Talk to a running wm2-born-again window manager over its\n"
        "configuration socket. Needs no X display of its own.\n"
        "\n"
        "Commands:\n"
        "  status              print what the window manager is doing\n"
        "  get KEY             print the value the window manager is using\n"
        "  set KEY VALUE       change a setting on the running desktop\n"
        "  reload              re-read the configuration files from disk\n"
        "\n"
        "Options:\n"
        "  --socket PATH       talk to this socket instead of the one $DISPLAY names\n"
        "  --help              print this message and exit\n"
        "\n"
        "Exit status:\n"
        "  0  acknowledged\n"
        "  1  the window manager refused the request\n"
        "  2  no window manager to talk to\n"
        "  3  usage error\n"
        "\n"
        "A `set` is applied to the running desktop and is NOT written to any\n"
        "file; `reload` therefore discards it. Settable keys:\n");

    // Generated from the same table the window manager's own --help is
    // generated from, so a key this tool advertises is a key the parser
    // accepts, and a key the parser gains appears here without being added.
    for (const ConfigKeySpec& spec : configKeySpecs()) {
        const char* kind = "TEXT";
        if (spec.kind == ConfigValueKind::Boolean) kind = "true|false";
        else if (spec.kind == ConfigValueKind::Integer) kind = "N";
        std::fprintf(out, "  %-28s %-11s %s\n", spec.name.c_str(), kind,
                     spec.summary.c_str());
    }

    // The one key that is NOT in that table, and cannot be: the manual menu
    // entries are an ordered repeated group rather than a single setting, so
    // the whole list travels as one value in the config file's own key order.
    // See include/ConfigProtocol.h for the grammar. Printed here so a user
    // reading --help learns the key exists, spelled from the same constant the
    // window manager compares against.
    std::fprintf(out,
        "  %-28s %-11s %s\n"
        "\n"
        "The %s value carries the WHOLE list and replaces it, in the config\n"
        "file's own key order with ';' between records, for example:\n"
        "  menu-entry-name=Editor;menu-entry-command=/usr/bin/vim\n"
        "An empty value removes every manual entry.\n",
        kMenuEntriesKey, "LIST",
        "every manual root-menu entry, as one value",
        kMenuEntriesKey);
}

// One connection to the window manager. Owns its descriptor; every wait has a
// deadline and no read is ever a blocking one.
class Connection {
public:
    ~Connection() { if (m_fd >= 0) ::close(m_fd); }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection() = default;

    // Returns kExitOk on success, kExitNoSocket otherwise, having warned.
    int open(const std::string& path)
    {
        struct sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        // IN-08: the predicate, not a second spelling of it.
        // include/SocketServer.h explains at length why this test has exactly
        // one home; an open-coded copy here is a copy that can drift from it.
        if (!configSocketPathFits(path)) {
            warn("socket path is too long for a unix socket address: " + path);
            return kExitNoSocket;
        }
        std::memcpy(addr.sun_path, path.c_str(), path.size());

        m_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (m_fd < 0) {
            warn(std::string("cannot create a socket: ") + std::strerror(errno));
            return kExitNoSocket;
        }

        if (::connect(m_fd, reinterpret_cast<struct sockaddr*>(&addr),
                      sizeof(addr)) != 0) {
            const int err = errno;
            if (err == ENOENT || err == ECONNREFUSED) {
                // The two ordinary "nothing is listening" answers: no socket
                // node at all, or a node left behind by a window manager that
                // is gone. Named as one condition because a user does not care
                // which of the two it is.
                warn("no window manager is listening on " + path);
            } else {
                warn("cannot connect to " + path + ": " + std::strerror(err));
            }
            return kExitNoSocket;
        }
        return kExitOk;
    }

    bool send(const ConfigMessage& message)
    {
        const std::string bytes = configProtocolEncode(message);
        if (bytes.empty()) return false;

        std::size_t sent = 0;
        const auto until = Clock::now() + std::chrono::milliseconds(kDeadlineMs);
        while (sent < bytes.size()) {
            const ssize_t n = ::send(m_fd, bytes.data() + sent, bytes.size() - sent,
                                     MSG_NOSIGNAL);
            if (n > 0) { sent += static_cast<std::size_t>(n); continue; }
            if (n < 0 && (errno == EINTR)) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                if (Clock::now() >= until) return false;
                continue;
            }
            return false;
        }
        return true;
    }

    // One complete reply line, decoded. False on a closed peer or an expired
    // deadline -- never on a blocking read, because the descriptor is polled
    // first and the poll itself carries the remaining time.
    bool receive(ConfigMessage& out)
    {
        const auto until = Clock::now() + std::chrono::milliseconds(kDeadlineMs);
        for (;;) {
            const std::size_t nl = m_in.find('\n');
            if (nl != std::string::npos) {
                const std::string line = m_in.substr(0, nl + 1);
                m_in.erase(0, nl + 1);
                return configProtocolDecode(line, out) == ConfigDecodeResult::Ok;
            }

            const auto left = until - Clock::now();
            if (left <= Clock::duration::zero()) return false;

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
            if (n == 0) return false;                 // the window manager closed
            if (n < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
                return false;
            }
            m_in.append(buf, static_cast<std::size_t>(n));

            // A frame longer than the contract allows is refused HERE rather
            // than being buffered until this process runs out of memory. The
            // window manager applies the same bound to what it reads from us.
            if (m_in.size() > kConfigProtocolMaxLine * 2 &&
                m_in.find('\n') == std::string::npos) {
                return false;
            }
        }
    }

private:
    int m_fd = -1;
    std::string m_in;
};

// D-15, from the client's side: never send a setting to a stranger. The hello
// goes first and its acknowledgement is REQUIRED before the real request is
// written, so a process squatting on the socket path cannot be handed a value
// simply because it accepted the connection.
int shakeHands(Connection& conn)
{
    ConfigMessage hello;
    hello.type = ConfigMessageType::Hello;
    hello.program = "wm2-ctl";
    hello.protocol = kConfigProtocolVersion;
    if (!conn.send(hello)) {
        warn("could not send the handshake");
        return kExitNoSocket;
    }

    ConfigMessage ack;
    if (!conn.receive(ack)) {
        warn("no reply to the handshake");
        return kExitNoSocket;
    }
    if (ack.type == ConfigMessageType::Error) {
        warn(ack.reason.empty() ? "the handshake was refused" : ack.reason);
        return kExitRefused;
    }
    if (ack.type != ConfigMessageType::HelloAck) {
        warn("the handshake was answered with something that is not a hello-ack");
        return kExitNoSocket;
    }
    if (ack.protocol != kConfigProtocolVersion) {
        warn("the window manager speaks protocol version " +
             std::to_string(ack.protocol) + ", this tool speaks " +
             std::to_string(kConfigProtocolVersion));
        return kExitRefused;
    }
    return kExitOk;
}

// Turn the window manager's reply into output and an exit code. Success is
// SILENT for `set` and `reload` -- the Unix answer to "it worked" -- and `get`
// prints the bare value with no decoration so it can be captured in a shell
// variable.
int reportReply(const ConfigMessage& reply, ConfigMessageType expected)
{
    if (reply.type == ConfigMessageType::Error) {
        warn(reply.key.empty() ? reply.reason : reply.key + ": " + reply.reason);
        return kExitRefused;
    }
    if (reply.type != expected) {
        warn("unexpected reply: " +
             std::string(configMessageTypeName(reply.type)));
        return kExitRefused;
    }

    switch (reply.type) {
    case ConfigMessageType::StatusReply:
        for (const auto& field : reply.fields) {
            std::printf("%s: %s\n", field.first.c_str(), field.second.c_str());
        }
        break;
    case ConfigMessageType::Value:
        std::printf("%s\n", reply.value.c_str());
        break;
    default:
        break;   // ack and reloaded say nothing
    }
    return kExitOk;
}

}  // namespace


int main(int argc, char** argv)
{
    // A window manager that goes away mid-write must not kill this process with
    // SIGPIPE before it can print why. MSG_NOSIGNAL covers send(); this covers
    // the printf side and anything a future path adds.
    ::signal(SIGPIPE, SIG_IGN);

    std::string socketPath;
    std::vector<std::string> words;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage(stdout);
            return kExitOk;
        }
        if (arg == "--socket") {
            if (i + 1 >= argc) {
                warn("--socket needs a path");
                return kExitUsage;
            }
            socketPath = argv[++i];
            continue;
        }
        if (arg.rfind("--socket=", 0) == 0) {
            socketPath = arg.substr(std::strlen("--socket="));
            continue;
        }
        if (arg.rfind("--", 0) == 0) {
            warn("unknown option '" + arg + "'");
            printUsage(stderr);
            return kExitUsage;
        }
        words.push_back(arg);
    }

    if (words.empty()) {
        printUsage(stderr);
        return kExitUsage;
    }

    // --- What is being asked -------------------------------------------------
    ConfigMessage request;
    ConfigMessageType expected = ConfigMessageType::Ack;

    const std::string& command = words[0];
    if (command == "status") {
        if (words.size() != 1) { warn("status takes no arguments"); return kExitUsage; }
        request.type = ConfigMessageType::Status;
        expected = ConfigMessageType::StatusReply;
    } else if (command == "reload") {
        if (words.size() != 1) { warn("reload takes no arguments"); return kExitUsage; }
        request.type = ConfigMessageType::Reload;
        expected = ConfigMessageType::Reloaded;
    } else if (command == "get") {
        if (words.size() != 2) { warn("get takes exactly one key"); return kExitUsage; }
        request.type = ConfigMessageType::Get;
        request.key = words[1];
        expected = ConfigMessageType::Value;
    } else if (command == "set") {
        if (words.size() != 3) { warn("set takes exactly one key and one value"); return kExitUsage; }
        request.type = ConfigMessageType::Set;
        request.key = words[1];
        request.value = words[2];
        expected = ConfigMessageType::Ack;
    } else {
        warn("unknown command '" + command + "'");
        printUsage(stderr);
        return kExitUsage;
    }

    // The key and value are forwarded EXACTLY as typed. This tool deliberately
    // does not pre-judge them: the window manager owns validation, through the
    // same Config::applyKeyValue() the config file goes through, and a second
    // opinion here could only ever disagree with the first.
    if (request.key.find('\n') != std::string::npos ||
        request.value.find('\n') != std::string::npos) {
        // The one exception, and it is framing rather than validation: a
        // newline would end the frame early and turn the tail into a second
        // message the window manager never agreed to receive.
        warn("a key or value may not contain a newline");
        return kExitUsage;
    }

    // --- Where to say it -----------------------------------------------------
    if (socketPath.empty()) {
        const char* displayName = std::getenv("DISPLAY");
        if (!displayName || displayName[0] == '\0') {
            warn("DISPLAY is not set; name the socket with --socket PATH");
            return kExitUsage;
        }
        socketPath = configSocketPath(displayName);
    }
    if (!configSocketPathFits(socketPath)) {
        warn("socket path is too long for a unix socket address: " + socketPath);
        return kExitNoSocket;
    }

    Connection conn;
    const int opened = conn.open(socketPath);
    if (opened != kExitOk) return opened;

    const int shaken = shakeHands(conn);
    if (shaken != kExitOk) return shaken;

    if (!conn.send(request)) {
        warn("could not send the request");
        return kExitNoSocket;
    }

    // UNSOLICITED NOTICES ARE SKIPPED, NOT MISREAD AS REFUSALS (WR-12). This
    // invocation completed the handshake, so it is in the window manager's
    // broadcast set: a reload triggered by anything else -- another
    // `wm2-ctl reload`, a settings window pressing "Re-read files" -- puts a
    // `reloaded` on this connection ahead of the reply this invocation is
    // waiting for. reportReply() reads anything that is not `expected` as a
    // refusal and exits 1, which the exit-code contract at the top of this
    // file defines as "the window manager refused the request". A shell script
    // branching on that code took the wrong branch after a request that had
    // SUCCEEDED.
    ConfigMessage reply;
    for (;;) {
        if (!conn.receive(reply)) {
            warn("no reply from the window manager");
            return kExitNoSocket;
        }
        if (!configProtocolIsUnsolicitedNotice(reply.type, expected)) break;
    }

    return reportReply(reply, expected);
}
