#pragma once

// The configuration socket: one listening AF_UNIX endpoint, the connections it
// accepts, and the three verdicts that guard it.
//
// This header is deliberately free of any project header other than the wire
// contract, and free of X11, of Xft, of GTK and of GLib. test_config_socket
// links Catch2 plus src/SocketServer.cpp and nothing else, with include/ on the
// header path, and must be runnable on a host with no X server at all -- so
// anything reachable from here may depend on nothing but POSIX, the C++
// standard library and include/ConfigProtocol.h (which is itself standard
// library only). That constraint is the whole reason the decision function and
// the three verdicts live here rather than inside the window manager: a
// decision a test cannot reach is a decision nothing proves.
//
// The libc-bound half -- mkdir, bind, accept, getsockopt -- lives in
// src/SocketServer.cpp. The pure decision below stays inline so the display-free
// case can call it directly.

#include "ConfigProtocol.h"

#include <poll.h>
#include <sys/types.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>


// What to do with one descriptor, given what poll() reported about it.
//
// Four enumerators, four cases, and no else-less tail: the house rule
// include/MenuPaint.h's `Foreign` enumerator documents. Every possible revents
// value maps to exactly one of these.
enum class SocketAction {
    Accept,       // the LISTENING descriptor is readable: a connection is waiting
    ReadClient,   // a CONNECTED descriptor is readable: bytes are waiting
    CloseClient,  // the descriptor hung up or failed; it is finished either way
    Idle          // nothing this descriptor needs
};


// Which descriptor is being asked about. A readable listener means "accept" and
// a readable connection means "read", and those are different actions over the
// identical revents bits -- so the role has to be part of the question. It is
// an enumerator rather than a bool because a bare `true` at a call site says
// nothing, and it is DEFAULTED to Client so that the connection case reads as
// socketServerDecide(revents), which is the overwhelmingly common one.
enum class SocketRole { Client, Listener };


// Every revent that reports a FAILED descriptor. Spelled here rather than
// borrowed from include/EventPump.h because that header includes X11 and this
// one may not; the two agree by construction, both being the POSIX triple that
// poll() delivers whether or not .events asked for it.
inline short socketFailedRevents()
{
    return static_cast<short>(POLLERR | POLLHUP | POLLNVAL);
}


// The pure decision. No state, no side effects, no descriptor touched.
//
// ORDER MATTERS. A descriptor can report POLLIN and POLLHUP together -- a peer
// that wrote a final message and closed does exactly that -- and the failure
// bits are checked FIRST for the listener and SECOND for a connection. For a
// connection the readable case wins, so the last bytes a departing client sent
// are still processed; the following poll reports the hangup with no POLLIN and
// the connection is closed then. For the listener a failure is terminal and
// there is no payload to rescue.
inline SocketAction socketServerDecide(short revents,
                                       SocketRole role = SocketRole::Client)
{
    if (role == SocketRole::Listener) {
        // A failed listener cannot be recovered by accepting from it. Reported
        // as CloseClient, which the server reads as "shut the whole thing
        // down" for this descriptor -- the same conclusion the event loop
        // reaches for a failed X connection.
        if (revents & socketFailedRevents()) return SocketAction::CloseClient;
        if (revents & POLLIN)                return SocketAction::Accept;
        return SocketAction::Idle;
    }

    if (revents & POLLIN)                return SocketAction::ReadClient;
    if (revents & socketFailedRevents()) return SocketAction::CloseClient;
    return SocketAction::Idle;
}


// -----------------------------------------------------------------------------
// The socket path (DISC-02, frozen in include/ConfigProtocol.h's DISC-01 block)
// -----------------------------------------------------------------------------

// $XDG_RUNTIME_DIR/wm2-born-again, or /tmp/wm2-born-again-<uid> when that
// variable is unset or does not name an absolute path.
//
// Resolution follows xdgConfigHome()'s shape exactly (src/Config.cpp): read the
// variable, require it be absolute, otherwise take the ONE documented fallback,
// and chase no other convention. Deliberately does NOT include src/Config.h --
// that header is the window manager's, and this one may not depend on it.
std::string configSocketDirectory();


// The full socket path for one display.
//
// `displayName` is the DISPLAY string. Every character outside [A-Za-z0-9._-]
// is replaced by '_', so ":1" becomes "_1" and a concurrent second X display
// gets its own socket -- which is what makes the ctest suite able to run window
// manager fixtures in parallel at all. A null or empty display name yields "_",
// so the function is total.
std::string configSocketPath(const char* displayName);


// Does this path fit sockaddr_un's path field, terminator included?
//
// sun_path is 108 bytes on Linux (107 usable). bind() does not report a path
// that is too long as such: it truncates at the struct level and produces
// either a confusing EINVAL or a binding at the wrong location (RESEARCH
// Pitfall 4). Checked before bind(), never after.
bool configSocketPathFits(const std::string& path);


// -----------------------------------------------------------------------------
// The two boundary verdicts (D-16, T-9-11 / T-9-12 / T-9-17)
// -----------------------------------------------------------------------------

// Who is on the other end.
enum class PeerVerdict {
    SameUid,     // the peer's uid equals the window manager's; admit
    ForeignUid,  // some other uid, root included; close before any read
    Unknown      // the kernel would not say; treated exactly as ForeignUid
};


// SO_PEERCRED, compared against `self`.
//
// LOAD-BEARING CAVEAT, recorded here rather than left to be discovered in a
// later audit: unix(7) states the credentials are captured "at the time of the
// call to connect()", not at each subsequent read. A process that later drops
// or changes privileges does not un-authorize a connection it already holds.
// This does not weaken D-16 -- same-uid-at-connect-time is exactly what D-16
// asks for -- but it is the reason the check is performed once, at accept, and
// is not re-run per message.
//
// Unknown and ForeignUid are distinct verdicts but the same outcome. They are
// kept apart so a refusal can say WHY in the log.
PeerVerdict configSocketPeerVerdict(int fd, uid_t self);


// The peer's uid, for the refusal message. False when the kernel would not say.
bool configSocketPeerUid(int fd, uid_t& out);


// What a file already sitting at the socket path means.
enum class StaleVerdict {
    NoFile,  // nothing there; bind straight away
    Live,    // something is listening, or the file is not ours to remove
    Stale    // a socket no process is listening on; reclaim it
};


// Decided the way this project already decides "is another instance running?"
// -- by asking, not by assuming. The _WM2_RUNNING selection check in
// src/Manager.cpp asks the server who owns the selection; this asks the kernel
// whether anyone answers on the socket, by attempting a connection.
//
// NEVER unlinks. The caller does that, and only for Stale. A path holding
// something that is not a socket is Live: a file this process did not create is
// never removed by this process, which is what stops a pre-placed path from
// being quietly replaced (T-9-17).
StaleVerdict configSocketStaleVerdict(const std::string& path);


// -----------------------------------------------------------------------------
// The server
// -----------------------------------------------------------------------------

// The largest number of connections held at once. Small on purpose: the
// expected population is one wm2-config window plus the occasional wm2-ctl
// invocation, and an unbounded accept loop is a descriptor-exhaustion lever
// (T-9-16). When the limit is reached the OLDEST connection that has not
// completed a handshake is dropped first, so a flood of silent connections
// cannot push out a working client.
inline constexpr std::size_t kConfigSocketMaxClients = 16;

// How long a connection may stay silent before it is dropped, in milliseconds.
// A client that connects and never speaks holds a descriptor for nothing
// (T-9-16). Generous enough that a GUI starting under a sanitizer is never cut
// off mid-handshake, short enough that the descriptor is not held for a
// session.
inline constexpr int kConfigSocketHelloDeadlineMs = 10000;

// The most unsent reply bytes a connection may accumulate. Replies are a few
// hundred bytes and the socket buffer swallows them whole, so this is only
// reached by a peer that has stopped reading -- which is a peer whose
// connection is worth losing, not a reason to grow memory on its behalf.
inline constexpr std::size_t kConfigSocketMaxPending = 65536;


// One complete frame handed to the dispatcher, with the only piece of
// per-connection state the protocol policy needs.
struct ConfigSocketRequest {
    std::string line;         // one frame, terminating newline included
    bool        helloSeen = false;  // has this connection completed the handshake?
};


// What the dispatcher wants done with it. The transport owns descriptors and
// buffers; the policy owns what a message MEANS -- which is why D-15's
// handshake rule and D-14's field ceiling live in the window manager and not in
// this file.
struct ConfigSocketReply {
    std::string line;                  // what to send; empty sends nothing
    bool        closeAfterSend = false;
    bool        helloAccepted  = false;  // this request completed the handshake
};


class ConfigSocketServer {
public:
    using Handler = std::function<ConfigSocketReply(const ConfigSocketRequest&)>;

    ConfigSocketServer();
    ~ConfigSocketServer();

    ConfigSocketServer(const ConfigSocketServer&) = delete;
    ConfigSocketServer& operator=(const ConfigSocketServer&) = delete;

    // Create the directory, reclaim a stale socket, bind, chmod and listen.
    //
    // Returns false for every failure, having warned once on stderr with the
    // project's `wm2: warning:` prefix. A false return is NOT fatal and must not
    // be treated as one: a window manager with no configuration socket still
    // manages windows, which is the whole of its job. Only the socket is lost.
    bool listen(const char* displayName);

    bool isListening() const { return m_listenFd >= 0; }
    const std::string& path() const { return m_path; }

    // Append this server's descriptors to a poll set: the listener first, then
    // one per connection in order. Appends nothing at all when not listening,
    // so a window manager that failed to bind polls exactly what it polled
    // before this phase existed.
    void appendPollFds(std::vector<struct pollfd>& out) const;

    // Service whatever appendPollFds() put at `firstIndex`.
    //
    // `fds` must be the SAME set appendPollFds() filled, in the same iteration
    // of the same loop: the connection list is indexed positionally. A set that
    // does not match is ignored rather than misread.
    void service(const std::vector<struct pollfd>& fds, std::size_t firstIndex,
                 const Handler& handler);

    // Send one line to every connection that has completed the handshake.
    // Nothing is sent to a stranger (D-15).
    void broadcast(const std::string& line);

    std::size_t clientCount() const { return m_clients.size(); }

    // How long the caller's poll() may block before this server needs attention
    // again, in milliseconds; -1 when it does not. The silence deadline above
    // fires on the passage of time and nothing else, so a poll that blocks
    // forever would never let it run.
    int timeoutHintMs() const;

    // Close every descriptor and unlink the socket. Idempotent.
    void close();

private:
    struct Connection {
        int         fd = -1;
        bool        helloSeen = false;
        bool        closing = false;   // send what is buffered, then close
        bool        dead = false;      // reap at the end of service()
        std::string in;                // bytes read, not yet framed
        std::string out;               // bytes to send, not yet sent
        long long   deadlineMs = 0;    // steady-clock ms by which hello must arrive
    };

    void acceptPending();
    void readConnection(Connection& c, const Handler& handler);
    void deliver(Connection& c, const std::string& line, bool closeAfter);
    void flush(Connection& c);
    void expireSilent();
    void reap();
    void dropOldestSilent();
    void warnForeign(uid_t peer, bool known);
    void closeConnection(Connection& c);

    int         m_listenFd = -1;
    std::string m_path;
    std::string m_directory;
    bool        m_bound = false;

    // The strictly-NARROWING internal test lever (the WM2_FORCE_NO_SHAPE
    // precedent, D-12). When set it forces every peer verdict to ForeignUid, so
    // the refusal path and its log-once behaviour can be driven end-to-end
    // against the real binary by a test that cannot become another user.
    //
    // It can only ever REFUSE connections this build would otherwise admit. By
    // construction there is no value of it that admits a connection the uid
    // comparison would reject, so it cannot widen D-16's boundary. Read once,
    // at listen(), never re-read; no CLI flag, and kept out of user
    // documentation exactly as the shape and randr levers are.
    bool m_forceForeign = false;

    std::vector<Connection> m_clients;
    std::vector<uid_t>      m_warnedUids;
};
