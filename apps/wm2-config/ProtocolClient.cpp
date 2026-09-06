#include "ProtocolClient.h"

#include "SocketServer.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
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

    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        setState(State::NoSocket,
                 std::string("cannot create a socket: ") + std::strerror(errno));
        return false;
    }

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        const int err = errno;
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
    m_state = State::Connected;   // provisional: the handshake decides

    // --- D-15: the hello, and nothing else, until it is acknowledged --------
    ConfigMessage hello;
    hello.type = ConfigMessageType::Hello;
    hello.program = "wm2-config";
    hello.protocol = kConfigProtocolVersion;
    if (!send(hello)) {
        disconnect(State::NoSocket, "could not send the handshake");
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
        disconnect(State::NoSocket, "no reply to the handshake");
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
            if (Clock::now() >= until) return false;
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
    // apart by whether anything is waiting for a reply: a `reloaded` that
    // arrives with no outstanding request is somebody else's reload, and is the
    // notice this window must react to.
    if (m_pending.empty()) {
        if (message.type == ConfigMessageType::Reloaded && m_onNotice) m_onNotice();
        return;
    }

    Pending p = std::move(m_pending.front());
    m_pending.pop_front();
    if (p.handler) p.handler(message);
}


void ProtocolClient::onReadable()
{
    if (m_fd < 0) return;

    for (;;) {
        char buf[4096];
        const ssize_t n = ::recv(m_fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n == 0) {
            disconnect(State::NoSocket, "the window manager closed the connection");
            return;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            disconnect(State::NoSocket,
                       std::string("the connection failed: ") + std::strerror(errno));
            return;
        }
        m_in.append(buf, static_cast<std::size_t>(n));

        // A frame longer than the contract allows is refused HERE rather than
        // buffered until this process runs out of memory. The window manager
        // applies the same bound to what it reads from us.
        if (m_in.size() > kConfigProtocolMaxLine * 2 &&
            m_in.find('\n') == std::string::npos) {
            disconnect(State::Refused,
                       "the window manager sent a line longer than the protocol allows");
            return;
        }
    }

    for (;;) {
        const std::size_t nl = m_in.find('\n');
        if (nl == std::string::npos) break;
        const std::string line = m_in.substr(0, nl + 1);
        m_in.erase(0, nl + 1);

        ConfigMessage message;
        if (configProtocolDecode(line, message) != ConfigDecodeResult::Ok) {
            // Undecodable is not the same as absent: a peer producing lines
            // this build cannot read is not one we may keep sending values to.
            disconnect(State::Refused,
                       "the window manager sent a message this program cannot read");
            return;
        }
        dispatch(message);
        if (m_fd < 0) return;    // a handler, or dispatch itself, dropped us
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
