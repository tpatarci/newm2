#include "ProtocolClient.h"

#include "SocketServer.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

using Clock = std::chrono::steady_clock;

// The handshake's own deadline. Generous enough for a window manager under a
// sanitizer on a loaded machine, short enough that a wedged peer produces a
// file-only window rather than a settings program that never appears.
constexpr int kHandshakeMs = 5000;

}  // namespace


ProtocolClient::~ProtocolClient()
{
    if (m_fd >= 0) ::close(m_fd);
}


std::string ProtocolClient::resolveSocketPath()
{
    const char* display = std::getenv("DISPLAY");
    if (!display || display[0] == '\0') return std::string();
    return configSocketPath(display);
}


void ProtocolClient::setState(State newState, const std::string& reason)
{
    const bool changed = (newState != m_state) || (reason != m_reason);
    m_state = newState;
    m_reason = reason;
    if (changed && m_onState) m_onState();
}


void ProtocolClient::disconnect(State newState, const std::string& reason)
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_in.clear();

    // Every request still waiting is answered with an error rather than left
    // hanging, so a caller's handler runs exactly once whatever happens. A
    // handler that never runs is how a settings window ends up with a control
    // stuck in a pending state nothing will ever clear.
    std::deque<Pending> pending;
    pending.swap(m_pending);
    for (Pending& p : pending) {
        if (!p.handler) continue;
        ConfigMessage error;
        error.type = ConfigMessageType::Error;
        error.reason = reason.empty() ? "the connection was lost" : reason;
        p.handler(error);
    }

    setState(newState, reason);
}


bool ProtocolClient::connect(const std::string& path)
{
    disconnect(State::Idle, std::string());

    if (path.empty()) {
        setState(State::NoSocket, "no DISPLAY, so there is no socket to look for");
        return false;
    }
    if (!configSocketPathFits(path)) {
        setState(State::NoSocket,
                 "the socket path is too long for a unix socket address");
        return false;
    }

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.c_str(), path.size());

    // NON-BLOCKING, FROM THE FIRST SYSCALL (WR-09). This descriptor is owned by
    // a GTK main loop: every blocking operation on it is a frozen settings
    // window with no repaint and no way out, and writes are where this program
    // spends its time -- request() -> send() runs from button and entry
    // handlers, up to twenty-one in a row on a discard. connect() has the same
    // exposure at startup, bounded only by the peer's listen backlog, so it is
    // covered by the same treatment.
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        setState(State::NoSocket,
                 std::string("cannot create a socket: ") + std::strerror(errno));
        return false;
    }

    int rc  = ::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    int err = errno;
    if (rc < 0 && (err == EINPROGRESS || err == EAGAIN || err == EWOULDBLOCK)) {
        struct pollfd p;
        p.fd      = fd;
        p.events  = POLLOUT;
        p.revents = 0;
        const int r = ::poll(&p, 1, kHandshakeMs);
        if (r <= 0) {
            ::close(fd);
            setState(State::NoSocket,
                     "the window manager's socket did not accept a connection "
                     "in time");
            return false;
        }
        socklen_t len = sizeof(err);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0) {
            const int failed = errno;
            ::close(fd);
            setState(State::NoSocket,
                     std::string("cannot connect: ") + std::strerror(failed));
            return false;
        }
        rc = (err == 0) ? 0 : -1;
    }

    if (rc != 0) {
        ::close(fd);
        // ENOENT and ECONNREFUSED are the two ordinary "nothing is listening"
        // answers -- no socket node, or one left behind by a window manager
        // that is gone -- and a user does not care which of the two it is.
        setState(State::NoSocket,
                 (err == ENOENT || err == ECONNREFUSED)
                     ? "no window manager is listening"
                     : std::string("cannot connect: ") + std::strerror(err));
        return false;
    }

    m_fd = fd;
    // The state stays whatever it was until the handshake FINISHES. An earlier
    // draft set it to Connected here "provisionally", and the consequence was
    // visible: a peer that accepts the connection and then says nothing left
    // the window announcing itself connected for the whole five seconds of the
    // handshake deadline. Nothing may observe a connection the hello has not
    // yet paid for.

    // --- D-15: the hello, and nothing else, until it is acknowledged --------
    ConfigMessage hello;
    hello.type = ConfigMessageType::Hello;
    hello.program = "wm2-config";
    hello.protocol = kConfigProtocolVersion;
    // From here on a failure is REFUSED, never NoSocket. The transport
    // connected, so something is on the other end of the window manager's
    // socket path; a peer that will not identify itself is a stranger, and
    // D-15's whole point is that the two situations are different for the
    // person reading the banner. "Nothing is listening" is an ordinary
    // Tuesday on a droplet with no desktop open; "something is listening and
    // will not say what it is" is worth investigating.
    if (!send(hello)) {
        disconnect(State::Refused, "could not send the handshake");
        return false;
    }

    bool answered = false;
    ConfigMessage ack;
    m_pending.push_back(Pending{ConfigMessageType::HelloAck,
                                [&](const ConfigMessage& reply) {
                                    ack = reply;
                                    answered = true;
                                }});

    if (!pumpUntil([&]() { return answered; }, kHandshakeMs)) {
        disconnect(State::Refused,
                   "something is listening on the socket but did not answer the "
                   "handshake");
        return false;
    }

    if (ack.type == ConfigMessageType::Error) {
        disconnect(State::Refused,
                   ack.reason.empty() ? "the handshake was refused" : ack.reason);
        return false;
    }
    if (ack.type != ConfigMessageType::HelloAck) {
        disconnect(State::Refused,
                   "the handshake was answered with something that is not a hello-ack");
        return false;
    }
    if (ack.protocol != kConfigProtocolVersion) {
        disconnect(State::Refused,
                   "it speaks protocol version " + std::to_string(ack.protocol) +
                       ", this program speaks " +
                       std::to_string(kConfigProtocolVersion));
        return false;
    }
    // T-9-34: the version alone does not say WHO answered. A same-uid process
    // on the socket path, or whatever `--socket` named, gets past the server
    // side's peer-uid check by construction; the name it gives itself is the
    // only thing left to refuse it on, and it is refused before any setting.
    if (ack.program != kConfigProtocolWindowManagerProgram) {
        disconnect(State::Refused,
                   "it calls itself \"" + ack.program + "\", not " +
                       kConfigProtocolWindowManagerProgram);
        return false;
    }

    setState(State::Connected, std::string());
    return true;
}


bool ProtocolClient::send(const ConfigMessage& message)
{
    if (m_fd < 0) return false;

    const std::string bytes = configProtocolEncode(message);
    if (bytes.empty()) return false;

    std::size_t sent = 0;
    const auto until = Clock::now() + std::chrono::milliseconds(kHandshakeMs);
    while (sent < bytes.size()) {
        const ssize_t n = ::send(m_fd, bytes.data() + sent, bytes.size() - sent,
                                 MSG_NOSIGNAL);
        if (n > 0) { sent += static_cast<std::size_t>(n); continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // WAITED ON, NOT SPUN ON (WR-09). The descriptor is non-blocking,
            // so without the poll below this arm is a busy loop burning a core
            // for the whole handshake deadline while the window is frozen
            // anyway.
            const auto left = until - Clock::now();
            if (left <= Clock::duration::zero()) return false;
            struct pollfd p;
            p.fd      = m_fd;
            p.events  = POLLOUT;
            p.revents = 0;
            const int ms = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(left).count());
            if (::poll(&p, 1, ms > 0 ? ms : 1) <= 0) return false;
            continue;
        }
        return false;
    }
    return true;
}


bool ProtocolClient::request(const ConfigMessage& message,
                             ConfigMessageType expected, ReplyHandler onReply)
{
    if (m_state != State::Connected || m_fd < 0) return false;
    if (!send(message)) {
        disconnect(State::NoSocket, "the connection to the window manager was lost");
        return false;
    }
    m_pending.push_back(Pending{expected, std::move(onReply)});
    return true;
}


bool ProtocolClient::sendGet(const std::string& key, ReplyHandler onReply)
{
    ConfigMessage m;
    m.type = ConfigMessageType::Get;
    m.key = key;
    return request(m, ConfigMessageType::Value, std::move(onReply));
}


bool ProtocolClient::sendSet(const std::string& key, const std::string& value,
                             ReplyHandler onReply)
{
    // A newline would end the frame early and turn the tail into a second
    // message nobody agreed to receive. Framing, not validation: the window
    // manager owns validation, through the same Config::applyKeyValue() the
    // config file goes through.
    if (key.find('\n') != std::string::npos ||
        value.find('\n') != std::string::npos) {
        return false;
    }
    ConfigMessage m;
    m.type = ConfigMessageType::Set;
    m.key = key;
    m.value = value;
    return request(m, ConfigMessageType::Ack, std::move(onReply));
}


bool ProtocolClient::sendReload(ReplyHandler onReply)
{
    ConfigMessage m;
    m.type = ConfigMessageType::Reload;
    return request(m, ConfigMessageType::Reloaded, std::move(onReply));
}


void ProtocolClient::dispatch(const ConfigMessage& message)
{
    // D-08's broadcast and the reply to our own `reload` are the SAME message
    // type, which is what let the phase avoid a twelfth one. They are told
    // apart BY TYPE AGAINST THE HEAD OF THE QUEUE, never by queue depth: the
    // window manager injects its broadcast into a stream that may already have
    // requests in flight, and wm2-config has 23 of them outstanding in its
    // first main-loop turn. Popping the head for any decodable line shifts
    // every reply behind the notice by one, and the per-key handlers then
    // adopt a colour under a font key -- into the form, and from there into
    // the user's configuration file on the next Save (CR-02).
    //
    // `error` is accepted for ANY head, spelled explicitly: the window manager
    // answers a refused `get` or `set` with `error` rather than with the type
    // the request asked for, and dropping it would leave that request's
    // handler unrun forever.
    const bool matchesHead =
        !m_pending.empty() &&
        (message.type == m_pending.front().expected ||
         message.type == ConfigMessageType::Error);

    if (!matchesHead) {
        // Not an answer to anything this client asked for. A `reloaded` here is
        // D-08's notice; anything else is a line the window manager had no
        // business sending, and is ignored rather than mistaken for a reply.
        // The classification is the one wm2-ctl uses, from the same place, so
        // the two clients cannot come to disagree about it (WR-12).
        const ConfigMessageType expected =
            m_pending.empty() ? ConfigMessageType::Unknown
                              : m_pending.front().expected;
        if (configProtocolIsUnsolicitedNotice(message.type, expected)) {
            if (m_onNotice) m_onNotice();
            return;
        }
        // AN ERROR IS NEVER DROPPED (W-04). `matchesHead` accepts an `error`
        // for any head, so reaching here means the queue was EMPTY -- and an
        // error on an empty queue is still a refusal of something this client
        // asked for. The route that produced it was a reload whose reply had
        // already been correlated away; dropping it silently is how a refused
        // "Re-read files" leaves the window saying nothing at all.
        if (message.type == ConfigMessageType::Error) {
            std::fprintf(stderr,
                         "wm2-config: warning: the window manager refused "
                         "something with no request outstanding: %s\n",
                         message.reason.c_str());
            std::fflush(stderr);
            if (m_onProtocolError) m_onProtocolError(message.reason);
        }
        return;
    }

    Pending p = std::move(m_pending.front());
    m_pending.pop_front();
    if (p.handler) p.handler(message);
}


void ProtocolClient::onReadable()
{
    if (m_fd < 0) return;

    // BOUNDED PER CALLBACK, AND FRAMES ARE PARSED AS THEY COMPLETE (WR-10).
    //
    // The old shape drained until EAGAIN and only then parsed. Its growth
    // guard required the ABSENCE of a newline, so a peer streaming
    // well-formed short lines faster than the loop could exit kept the GTK
    // main loop inside this function with m_in growing without limit -- the
    // window frozen and the process growing until the OOM killer took it.
    //
    // This is the same "a single readable notification does not license an
    // unbounded read" rule the window manager's side states as
    // non-negotiable (src/SocketServer.cpp's rule 1). The client is a GLib
    // source, so it takes the bound in bytes and returns; whatever is left is
    // still readable, and the source fires again immediately.
    std::size_t drained = 0;

    for (;;) {
        // Frames FIRST, so m_in never holds more than one incomplete line and
        // the bound below is about the socket rather than about the buffer.
        for (;;) {
            const std::size_t nl = m_in.find('\n');
            if (nl == std::string::npos) break;
            const std::string line = m_in.substr(0, nl + 1);
            m_in.erase(0, nl + 1);

            ConfigMessage message;
            if (configProtocolDecode(line, message) != ConfigDecodeResult::Ok) {
                // Undecodable is not the same as absent: a peer producing
                // lines this build cannot read is not one we may keep sending
                // values to.
                disconnect(State::Refused,
                           "the window manager sent a message this program cannot read");
                return;
            }
            dispatch(message);
            if (m_fd < 0) return;    // a handler, or dispatch itself, dropped us
        }

        // A frame longer than the contract allows is refused HERE rather than
        // buffered until this process runs out of memory. Asserted with no
        // regard to newlines, because the framing loop above has already taken
        // every complete line out: whatever is left IS an incomplete frame.
        if (m_in.size() > kConfigProtocolMaxLine) {
            disconnect(State::Refused,
                       "the window manager sent a line longer than the protocol allows");
            return;
        }

        if (drained >= kProtocolClientMaxDrainPerCallback) return;  // finish next callback

        char buf[4096];
        const ssize_t n = ::recv(m_fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n == 0) {
            disconnect(State::NoSocket, "the window manager closed the connection");
            return;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            disconnect(State::NoSocket,
                       std::string("the connection failed: ") + std::strerror(errno));
            return;
        }
        m_in.append(buf, static_cast<std::size_t>(n));
        drained += static_cast<std::size_t>(n);
    }
}


bool ProtocolClient::pumpUntil(const std::function<bool()>& predicate, int timeoutMs)
{
    if (predicate()) return true;
    if (m_fd < 0) return false;

    const auto until = Clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        const auto left = until - Clock::now();
        if (left <= Clock::duration::zero()) return false;

        struct pollfd p;
        p.fd = m_fd;
        p.events = POLLIN;
        p.revents = 0;
        const int ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(left).count());
        const int r = ::poll(&p, 1, ms > 0 ? ms : 1);
        if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (r == 0) return false;

        onReadable();
        if (predicate()) return true;
        if (m_fd < 0) return false;
    }
}
