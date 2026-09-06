// The libc half of the configuration socket: the path, the directory, the
// boundary verdicts and the accept/read/write loop.
//
// No X11, no Xft, no GTK, no project header but include/SocketServer.h and the
// wire contract it pulls in. test_config_socket compiles this file straight
// into itself (the test_config pattern) and runs with no X server.
//
// TWO RULES HOLD THROUGHOUT AND NEITHER HAS AN EXCEPTION:
//
//   1. Nothing here blocks. Every descriptor is created non-blocking, every
//      read is a SINGLE recv() per readable notification -- never a loop that
//      could spin on a fast writer -- and every write is a single send() whose
//      remainder is buffered rather than waited on. The window manager has one
//      thread and it is also the thread that draws frames (T-9-15).
//
//   2. Nothing grows without a bound. The input buffer is capped at the
//      protocol's line bound and the output buffer at kConfigSocketMaxPending;
//      reaching either is a refusal and a close, not an allocation (T-9-14).

#include "SocketServer.h"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>


namespace {

// steady_clock milliseconds. Steady rather than wall-clock so the silence
// deadline survives a clock adjustment, exactly as the timestamp wait's
// deadline does (include/TimestampWait.h).
long long nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Fill a sockaddr_un for `path`. The caller has already established the path
// fits; this asserts it again rather than trusting, because a silent truncation
// here binds somewhere else entirely.
bool fillAddress(const std::string& path, struct sockaddr_un& addr)
{
    if (!configSocketPathFits(path)) return false;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.c_str(), path.size());
    addr.sun_path[path.size()] = '\0';
    return true;
}

bool setNonBlockingCloexec(int fd)
{
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return false;
    int fdFlags = ::fcntl(fd, F_GETFD, 0);
    if (fdFlags < 0 || ::fcntl(fd, F_SETFD, fdFlags | FD_CLOEXEC) < 0) return false;
    return true;
}

// The one place a refusal reply is spelled. The key is empty because a
// transport-level refusal is not about any key.
std::string errorLine(const char* reason)
{
    ConfigMessage m;
    m.type = ConfigMessageType::Error;
    m.reason = reason;
    return configProtocolEncode(m);
}

}  // namespace


// -----------------------------------------------------------------------------
// Path resolution (DISC-02)
// -----------------------------------------------------------------------------

std::string configSocketDirectory()
{
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime && runtime[0] == '/') {
        return std::string(runtime) + "/wm2-born-again";
    }
    // The ONE documented fallback (D-16). The uid is in the NAME, not merely in
    // the mode: two users falling back to /tmp must not contend for one path.
    return "/tmp/wm2-born-again-" + std::to_string(static_cast<unsigned long>(::geteuid()));
}


std::string configSocketPath(const char* displayName)
{
    std::string suffix;
    if (displayName != nullptr) {
        for (const char* p = displayName; *p != '\0'; ++p) {
            const unsigned char c = static_cast<unsigned char>(*p);
            const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                              (c >= '0' && c <= '9') ||
                              c == '.' || c == '_' || c == '-';
            suffix += safe ? static_cast<char>(c) : '_';
        }
    }
    // Total by construction: an absent or empty DISPLAY still names a socket,
    // rather than producing a directory path with a trailing "socket".
    if (suffix.empty()) suffix = "_";

    return configSocketDirectory() + "/socket" + suffix;
}


bool configSocketPathFits(const std::string& path)
{
    struct sockaddr_un addr;
    return path.size() + 1 <= sizeof(addr.sun_path);
}


// -----------------------------------------------------------------------------
// Boundary verdicts (D-16)
// -----------------------------------------------------------------------------

bool configSocketPeerUid(int fd, uid_t& out)
{
    struct ucred cred;
    socklen_t len = sizeof(cred);
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0) return false;
    if (len != sizeof(cred)) return false;
    out = cred.uid;
    return true;
}


PeerVerdict configSocketPeerVerdict(int fd, uid_t self)
{
    uid_t peer = 0;
    if (!configSocketPeerUid(fd, peer)) return PeerVerdict::Unknown;
    // EQUALITY, not a range and not a capability test. root is uid 0 and is
    // foreign to a window manager running as anyone else, which is what D-16
    // names explicitly (T-9-12).
    return (peer == self) ? PeerVerdict::SameUid : PeerVerdict::ForeignUid;
}


StaleVerdict configSocketStaleVerdict(const std::string& path)
{
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) return StaleVerdict::NoFile;

    // Not a socket: not ours to remove. A regular file, a symlink or a
    // directory at this path was not put there by a window manager, and
    // unlinking whatever happens to be in the way is precisely the behaviour
    // that lets a pre-placed path be replaced (T-9-17).
    if (!S_ISSOCK(st.st_mode)) return StaleVerdict::Live;

    struct sockaddr_un addr;
    if (!fillAddress(path, addr)) return StaleVerdict::Live;

    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return StaleVerdict::Live;   // cannot prove stale; do not unlink

    const int rc = ::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    const int err = errno;
    ::close(fd);

    if (rc == 0) return StaleVerdict::Live;                 // somebody answered
    if (err == ECONNREFUSED || err == ENOENT) return StaleVerdict::Stale;
    return StaleVerdict::Live;                              // unknown: conservative
}


// -----------------------------------------------------------------------------
// ConfigSocketServer
// -----------------------------------------------------------------------------

ConfigSocketServer::ConfigSocketServer() = default;

ConfigSocketServer::~ConfigSocketServer()
{
    close();
}


bool ConfigSocketServer::listen(const char* displayName)
{
    close();

    m_directory = configSocketDirectory();
    m_path      = configSocketPath(displayName);

    if (!configSocketPathFits(m_path)) {
        // Named, not truncated (RESEARCH Pitfall 4). The window manager carries
        // on with no socket.
        struct sockaddr_un probe;
        std::fprintf(stderr,
                     "wm2: warning: configuration socket path is too long "
                     "(%zu bytes, limit %zu), no socket will be created\n",
                     m_path.size() + 1, sizeof(probe.sun_path));
        m_path.clear();
        return false;
    }

    // The directory, mode 0700 and verified rather than trusted. mkdir applies
    // the umask, so the mode is set explicitly afterwards; an existing
    // directory is checked, because a directory this process did not create is
    // a directory whose mode it does not know.
    //
    // NOTHING BELOW OPERATES ON THE DIRECTORY BY NAME AFTER THE mkdir (CR-03).
    // mkdir() on an existing symlink-to-directory returns EEXIST, and chmod()
    // and stat() both FOLLOW symlinks -- so a by-name sequence inspects and
    // modifies the LINK'S TARGET rather than the path the socket will live
    // under. D-16's documented fallback is /tmp/wm2-born-again-<uid>: a
    // predictable name in a world-writable directory, and the ordinary state
    // under `su`, a bare startx and the minimal VNC session scripts this
    // project targets. An attacker who plants that name as a symlink to a
    // directory the victim owns would otherwise get an attacker-directed
    // `chmod 0700` on it and a socket node inside it.
    //
    // So: open the final component with O_NOFOLLOW and work through the
    // descriptor. The ELOOP that O_NOFOLLOW produces IS the symlink refusal;
    // the socket NODE's handling has always been this careful
    // (configSocketStaleVerdict's lstat + S_ISSOCK, T-9-17), and the asymmetry
    // between the two is what made this a defect rather than an accepted risk.
    if (::mkdir(m_directory.c_str(), 0700) != 0 && errno != EEXIST) {
        std::fprintf(stderr, "wm2: warning: cannot create %s (%s), "
                             "no configuration socket\n",
                     m_directory.c_str(), std::strerror(errno));
        return false;
    }

    const int dirFd = ::open(m_directory.c_str(),
                             O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (dirFd < 0) {
        std::fprintf(stderr, "wm2: warning: %s is not a directory this process "
                             "may use (%s), no configuration socket\n",
                     m_directory.c_str(), std::strerror(errno));
        return false;
    }

    struct stat dst;
    if (::fstat(dirFd, &dst) != 0 || !S_ISDIR(dst.st_mode) ||
        dst.st_uid != ::geteuid()) {
        std::fprintf(stderr, "wm2: warning: %s is not a directory owned by this "
                             "user, no configuration socket\n",
                     m_directory.c_str());
        ::close(dirFd);
        return false;
    }

    // Corrected only when it is wrong, and through the descriptor that has
    // already been proved to name a directory this user owns.
    if ((dst.st_mode & 07777) != 0700 && ::fchmod(dirFd, 0700) != 0) {
        std::fprintf(stderr, "wm2: warning: cannot set mode 0700 on %s (%s), "
                             "no configuration socket\n",
                     m_directory.c_str(), std::strerror(errno));
        ::close(dirFd);
        return false;
    }
    ::close(dirFd);

    // A predecessor's socket. Reclaimed only when nothing answers on it.
    switch (configSocketStaleVerdict(m_path)) {
    case StaleVerdict::NoFile:
        break;
    case StaleVerdict::Stale:
        if (::unlink(m_path.c_str()) != 0) {
            std::fprintf(stderr, "wm2: warning: cannot remove stale socket %s (%s), "
                                 "no configuration socket\n",
                         m_path.c_str(), std::strerror(errno));
            return false;
        }
        break;
    case StaleVerdict::Live:
        std::fprintf(stderr, "wm2: warning: %s is already in use, "
                             "no configuration socket\n", m_path.c_str());
        return false;
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        std::fprintf(stderr, "wm2: warning: cannot create configuration socket (%s)\n",
                     std::strerror(errno));
        return false;
    }
    if (!setNonBlockingCloexec(fd)) {
        std::fprintf(stderr, "wm2: warning: cannot configure the listening socket (%s)\n",
                     std::strerror(errno));
        ::close(fd);
        return false;
    }

    struct sockaddr_un addr;
    if (!fillAddress(m_path, addr)) {
        ::close(fd);
        return false;
    }

    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::fprintf(stderr, "wm2: warning: cannot bind %s (%s), "
                             "no configuration socket\n",
                     m_path.c_str(), std::strerror(errno));
        ::close(fd);
        return false;
    }
    m_bound = true;

    // 0600 as D-16 requires, belt to the directory's braces. Set after bind
    // because bind() is what creates the node.
    if (::chmod(m_path.c_str(), 0600) != 0) {
        std::fprintf(stderr, "wm2: warning: cannot set mode 0600 on %s (%s), "
                             "no configuration socket\n",
                     m_path.c_str(), std::strerror(errno));
        ::close(fd);
        ::unlink(m_path.c_str());
        m_bound = false;
        return false;
    }

    if (::listen(fd, 8) != 0) {
        std::fprintf(stderr, "wm2: warning: cannot listen on %s (%s), "
                             "no configuration socket\n",
                     m_path.c_str(), std::strerror(errno));
        ::close(fd);
        ::unlink(m_path.c_str());
        m_bound = false;
        return false;
    }

    m_listenFd = fd;

    const char* force = std::getenv("WM2_SOCKET_FORCE_FOREIGN");
    m_forceForeign = (force != nullptr && std::strcmp(force, "1") == 0);
    if (m_forceForeign) {
        // Worded so a captured transcript proves the lever was taken, the same
        // discipline the shape and randr levers follow.
        std::fprintf(stderr, "wm2: warning: configuration socket peer check "
                             "forced to refuse, no client will be admitted\n");
    }

    std::fprintf(stderr, "  Configuration socket: %s\n", m_path.c_str());
    return true;
}


void ConfigSocketServer::appendPollFds(std::vector<struct pollfd>& out) const
{
    if (m_listenFd < 0) return;

    struct pollfd p;
    p.fd      = m_listenFd;
    p.events  = POLLIN;
    p.revents = 0;
    out.push_back(p);

    for (const Connection& c : m_clients) {
        struct pollfd q;
        q.fd      = c.fd;
        // A connection with nothing left to say is polled for readability only.
        // POLLOUT is asked for ONLY while a reply is still unsent, because a
        // permanently writable descriptor with POLLOUT set turns every poll()
        // into a busy loop.
        q.events  = static_cast<short>((c.closing ? 0 : POLLIN) |
                                       (c.out.empty() ? 0 : POLLOUT));
        q.revents = 0;
        out.push_back(q);
    }
}


int ConfigSocketServer::timeoutHintMs() const
{
    if (m_listenFd < 0) return -1;

    long long earliest = -1;
    for (const Connection& c : m_clients) {
        if (c.helloSeen || c.closing) continue;
        if (earliest < 0 || c.deadlineMs < earliest) earliest = c.deadlineMs;
    }
    if (earliest < 0) return -1;

    const long long left = earliest - nowMs();
    if (left <= 0) return 0;
    if (left > 60000) return 60000;
    return static_cast<int>(left);
}


void ConfigSocketServer::service(const std::vector<struct pollfd>& fds,
                                 std::size_t firstIndex, const Handler& handler)
{
    if (m_listenFd < 0) return;

    // The silence deadline fires on the passage of time, so it is evaluated on
    // EVERY call and not only when some descriptor spoke.
    expireSilent();

    // The set has to be the one appendPollFds() filled. A mismatch is ignored
    // rather than misread: reading connection i's verdict off some other
    // descriptor is worse than doing nothing this iteration.
    const std::size_t needed = firstIndex + 1 + m_clients.size();
    if (fds.size() >= needed && fds[firstIndex].fd == m_listenFd) {

        // Connections FIRST, so nothing an accept does can shift the indices
        // being read here.
        //
        // THE WHOLE LOOP IS ONE SERVICING PASS (CR-01). A handler is allowed to
        // call back into this server -- the window manager's broadcasts D-08's
        // reload notice from inside the `reload` it is answering -- and a reap
        // running under this loop would erase, shift and destroy the very
        // elements it is indexing. So reap() defers while the depth is
        // non-zero and is paid once, below, when every handler has returned.
        ++m_serviceDepth;
        for (std::size_t i = 0; i < m_clients.size(); ++i) {
            const struct pollfd& p = fds[firstIndex + 1 + i];
            if (p.fd != m_clients[i].fd) continue;

            if (p.revents & POLLOUT) flush(m_clients[i]);
            if (m_clients[i].dead) continue;

            switch (socketServerDecide(p.revents)) {
            case SocketAction::ReadClient:
                readConnection(i, handler);
                break;
            case SocketAction::CloseClient:
                closeConnection(m_clients[i]);
                break;
            case SocketAction::Accept:
                // Unreachable for a connection role; named rather than left to
                // a default arm so a fifth action is a compile error here.
                break;
            case SocketAction::Idle:
                break;
            }
        }
        --m_serviceDepth;

        reap();

        if (socketServerDecide(fds[firstIndex].revents, SocketRole::Listener) ==
            SocketAction::Accept) {
            acceptPending();
        }
    } else {
        reap();
    }
}


void ConfigSocketServer::acceptPending()
{
    for (;;) {
        const int fd = ::accept(m_listenFd, nullptr, nullptr);
        if (fd < 0) break;   // EAGAIN on a non-blocking listener: nothing left

        if (!setNonBlockingCloexec(fd)) {
            ::close(fd);
            continue;
        }

        // THE BOUNDARY, BEFORE THE FIRST READ. A foreign process never reaches
        // configProtocolDecode() (T-9-11, T-9-12).
        uid_t peer = 0;
        const bool known = configSocketPeerUid(fd, peer);
        PeerVerdict verdict = configSocketPeerVerdict(fd, ::geteuid());
        if (m_forceForeign) verdict = PeerVerdict::ForeignUid;

        if (verdict != PeerVerdict::SameUid) {
            warnForeign(peer, known);
            ::close(fd);
            continue;
        }

        if (m_clients.size() >= kConfigSocketMaxClients) dropOldestSilent();
        if (m_clients.size() >= kConfigSocketMaxClients) {
            ::close(fd);
            continue;
        }

        Connection c;
        c.fd         = fd;
        c.deadlineMs = nowMs() + kConfigSocketHelloDeadlineMs;
        m_clients.push_back(std::move(c));
    }
}


void ConfigSocketServer::readConnection(std::size_t index, const Handler& handler)
{
    // BY INDEX, NEVER BY A REFERENCE HELD ACROSS THE HANDLER (CR-01). The
    // handler is the window manager's, and a `reload` reaches
    // reloadConfigFromDisk(), which broadcasts on this server -- so control
    // re-enters this object between the frame being extracted and the reply
    // being written. A `Connection&` taken before the call and used after it is
    // a reference into a vector the call may have shifted, moved from or
    // shortened; the ASan report that led to this shape named exactly that
    // write. The descriptor the index named on entry is remembered too, so an
    // index that survives while its OCCUPANT changed is caught as well.
    if (index >= m_clients.size()) return;
    const int fdAtEntry = m_clients[index].fd;

    // ONE receive per readable notification. Not a loop: a peer writing as fast
    // as this process can read would otherwise keep the window manager inside
    // this function indefinitely, which is the same stall a blocking read would
    // cause by a different route (T-9-15).
    {
        Connection& c = m_clients[index];

        char buf[4096];
        const ssize_t n = ::recv(c.fd, buf, sizeof(buf), 0);

        if (n == 0) {                       // orderly shutdown by the peer
            closeConnection(c);
            return;
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;
            closeConnection(c);
            return;
        }

        // The bound is enforced HERE, at the transport, and not only in the
        // decoder: the decoder never sees a frame this refuses (T-9-14).
        if (c.in.size() + static_cast<std::size_t>(n) > kConfigProtocolMaxLine) {
            // Everything up to the bound is already more than any legal frame.
            deliver(c, errorLine("message too long"), true);
            c.in.clear();
            return;
        }
        c.in.append(buf, static_cast<std::size_t>(n));
    }

    for (;;) {
        // RE-TAKEN every iteration, and validated first. Nothing below this
        // line may be carried across the handler call at the bottom of the
        // loop.
        if (index >= m_clients.size()) return;
        if (m_clients[index].fd != fdAtEntry) return;
        if (m_clients[index].closing || m_clients[index].dead) return;

        std::string line;
        bool helloSeen = false;
        {
            Connection& c = m_clients[index];

            const std::size_t nl = c.in.find('\n');
            if (nl == std::string::npos) {
                // No frame yet. A buffer that has reached the bound without a
                // newline never will have one, so it is refused now rather than
                // held (the no-newline-forever case).
                if (c.in.size() >= kConfigProtocolMaxLine) {
                    deliver(c, errorLine("message too long"), true);
                    c.in.clear();
                }
                return;
            }

            line      = c.in.substr(0, nl + 1);
            helloSeen = c.helloSeen;
            c.in.erase(0, nl + 1);
        }

        ConfigSocketRequest req;
        req.line      = std::move(line);
        req.helloSeen = helloSeen;

        const ConfigSocketReply reply = handler(req);

        // The re-look-up. A handler that closed this connection, or a reap
        // that ran despite the deferral (a caller outside service(), which is
        // allowed), leaves nothing here to write to.
        if (index >= m_clients.size()) return;
        if (m_clients[index].fd != fdAtEntry) return;

        Connection& after = m_clients[index];
        if (reply.helloAccepted) after.helloSeen = true;
        if (!reply.line.empty() || reply.closeAfterSend) {
            deliver(after, reply.line, reply.closeAfterSend);
        }
    }
}


void ConfigSocketServer::deliver(Connection& c, const std::string& line,
                                 bool closeAfter)
{
    if (c.out.size() + line.size() > kConfigSocketMaxPending) {
        // A peer that has stopped reading. Its connection is worth losing.
        closeConnection(c);
        return;
    }
    c.out += line;
    if (closeAfter) c.closing = true;

    flush(c);
}


void ConfigSocketServer::flush(Connection& c)
{
    while (!c.out.empty()) {
        // MSG_NOSIGNAL: a peer that vanished mid-reply must not kill the window
        // manager with SIGPIPE. This is the "one client disconnecting
        // mid-message does not disturb the other" guarantee at its root.
        const ssize_t n = ::send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL);
        if (n > 0) {
            c.out.erase(0, static_cast<std::size_t>(n));
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;  // poll again
        if (n < 0 && errno == EINTR) continue;
        closeConnection(c);
        return;
    }
    if (c.closing) closeConnection(c);
}


void ConfigSocketServer::broadcast(const std::string& line)
{
    if (line.empty()) return;
    for (Connection& c : m_clients) {
        if (!c.helloSeen || c.closing || c.dead) continue;   // never to a stranger
        deliver(c, line, false);
    }
    reap();
}


void ConfigSocketServer::expireSilent()
{
    const long long now = nowMs();
    bool any = false;
    for (Connection& c : m_clients) {
        if (c.helloSeen || c.dead) continue;
        if (c.deadlineMs > now) continue;
        closeConnection(c);
        any = true;
    }
    if (any) reap();
}


void ConfigSocketServer::dropOldestSilent()
{
    // Oldest first, and only among connections that never spoke: a flood of
    // silent connections must not be able to push out a working client
    // (T-9-16).
    for (Connection& c : m_clients) {
        if (c.helloSeen) continue;
        closeConnection(c);
        reap();
        return;
    }
}


void ConfigSocketServer::closeConnection(Connection& c)
{
    if (c.fd >= 0) ::close(c.fd);
    c.fd   = -1;
    c.dead = true;
}


void ConfigSocketServer::reap()
{
    // DEFERRED WHILE A SERVICING PASS IS IN FLIGHT (CR-01). Every path that
    // marks a connection dead ends here -- closeConnection() through
    // expireSilent(), dropOldestSilent(), flush() and deliver(), and
    // broadcast(), which a handler may call while this object is walking its
    // own vector. Erasing there shifts and destroys elements the servicing loop
    // still names. The debt is recorded and paid by service() the moment the
    // last handler has returned, so a dead connection lives at most to the end
    // of the pass that killed it and never past a poll().
    if (m_serviceDepth != 0) {
        m_reapPending = true;
        return;
    }

    m_clients.erase(std::remove_if(m_clients.begin(), m_clients.end(),
                                   [](const Connection& c) { return c.dead; }),
                    m_clients.end());
    m_reapPending = false;
}


void ConfigSocketServer::warnForeign(uid_t peer, bool known)
{
    if (known) {
        // ONCE PER UID, deliberately not once per attempt: the log records the
        // event without being floodable by a caller that simply reconnects
        // (T-9-19).
        if (std::find(m_warnedUids.begin(), m_warnedUids.end(), peer) !=
            m_warnedUids.end()) {
            return;
        }
        // Bounded, so the set of remembered uids is not itself a growth lever.
        if (m_warnedUids.size() < 64) m_warnedUids.push_back(peer);
        std::fprintf(stderr, "wm2: warning: refused configuration socket "
                             "connection from uid %lu\n",
                     static_cast<unsigned long>(peer));
        std::fflush(stderr);
        return;
    }

    // The kernel would not name the peer. Reported once in total, using the
    // uid-less spelling, and remembered under a sentinel so it cannot flood.
    const uid_t sentinel = static_cast<uid_t>(-1);
    if (std::find(m_warnedUids.begin(), m_warnedUids.end(), sentinel) !=
        m_warnedUids.end()) {
        return;
    }
    m_warnedUids.push_back(sentinel);
    std::fprintf(stderr, "wm2: warning: refused configuration socket "
                         "connection from an unidentifiable peer\n");
    std::fflush(stderr);
}


void ConfigSocketServer::close()
{
    for (Connection& c : m_clients) {
        if (c.fd >= 0) ::close(c.fd);
    }
    m_clients.clear();
    m_warnedUids.clear();

    if (m_listenFd >= 0) {
        ::close(m_listenFd);
        m_listenFd = -1;
    }
    if (m_bound && !m_path.empty()) {
        ::unlink(m_path.c_str());
        m_bound = false;
    }
}
