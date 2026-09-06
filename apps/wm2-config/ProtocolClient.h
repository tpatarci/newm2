#pragma once

// wm2-config's half of the configuration socket (D-05, D-08, D-15, plan 09-06).
//
// ---------------------------------------------------------------------------
// WHY THIS FILE MENTIONS NEITHER GTK NOR GLIB
// ---------------------------------------------------------------------------
//
// The client is the piece that carries a live change from the settings window
// to the running desktop, so it is the piece most worth proving. A client that
// included <glib.h> could only be exercised from a binary that links the
// toolkit, which on a host without gtk+-3.0 is no binary at all -- and the
// [wm2_config_smoke] case that drives a real colour change end to end would
// have had to hand-roll a second socket client instead, proving something other
// than the program.
//
// So the GLib INTEGRATION is a two-line adapter and it lives at the call site:
// this class exposes the descriptor a source should watch (fileDescriptor())
// and the handler that source should call (onReadable()), and apps/wm2-config/
// main.cpp attaches them with g_unix_fd_add(). NOTHING HERE EVER BLOCKS, on a
// read or on a write: the descriptor is non-blocking from the first syscall,
// onReadable() drains a bounded amount of what is already there and returns,
// and send() waits on poll() with the handshake deadline rather than on the
// kernel. Writes matter as much as reads and are the ones this program spends
// its time on -- a `set` per committed control, up to twenty-one in a row on a
// discard, all from GTK signal handlers (WR-09).
//
// pumpUntil() is the one exception, and it is deliberately narrow: it polls the
// descriptor with a deadline, and it exists for the handshake -- which happens
// before there is a main loop to integrate with -- and for the display-free
// tests, which have no main loop at all. It is never called while the window is
// up.
//
// ---------------------------------------------------------------------------
// D-15: NEVER SEND A SETTING TO A STRANGER
// ---------------------------------------------------------------------------
//
// connect() completes the hello and REQUIRES a hello-ack naming a protocol
// version this build speaks before it reports success. A failed connection, a
// refused handshake, or a version mismatch each leaves the client in a
// file-only state carrying a reason string the window puts in its banner. The
// window manager's own peer-uid check is the other half of the boundary; this
// is the half that protects the user's values from a process squatting on the
// socket path.

#include "ConfigProtocol.h"

#include <deque>
#include <functional>
#include <string>


// The most bytes one onReadable() call takes off the socket before returning to
// the main loop. Whatever is left is still readable, so the GLib source fires
// again at once and no message is delayed by more than a turn of the loop --
// but a peer streaming faster than this process can parse cannot hold the loop
// inside one callback (WR-10).
//
// TWO MAXIMUM-LENGTH FRAMES. Large enough that a callback always makes
// progress on the largest legal message rather than nibbling at it, and large
// enough to swallow the whole of the twenty-three-reply burst the window reads
// at startup in one turn; small enough that a flooding peer yields the loop
// promptly. Expressed in the protocol's own bound rather than as a round
// number, so the two cannot drift apart.
inline constexpr std::size_t kProtocolClientMaxDrainPerCallback =
    2 * kConfigProtocolMaxLine;


class ProtocolClient {
public:
    enum class State {
        Idle,        // never connected
        Connected,   // hello acknowledged; live apply is available
        NoSocket,    // nothing was listening, or the connection failed
        Refused      // something answered, and it was not one of us
    };

    // Called with the window manager's reply to one request. A reply is
    // delivered exactly once, in the order the requests were sent -- but the
    // STREAM is not only replies. D-08's reload notice shares its type with a
    // reload reply and is injected unsolicited, so correlation is by the head
    // request's EXPECTED type (`error` accepted for any head, since that is how
    // a refusal comes back) and never by queue depth. A line that matches no
    // outstanding request is a notice or is dropped; it never pops the queue
    // and shifts every reply behind it by one (CR-02).
    using ReplyHandler = std::function<void(const ConfigMessage&)>;

    // D-08: the window manager reloaded its files, so every effective value may
    // have moved under an open window.
    using NoticeHandler = std::function<void()>;

    // The connection state changed -- the window re-renders its banner and
    // re-evaluates which controls are sensitive.
    using StateHandler = std::function<void()>;

    ProtocolClient() = default;
    ~ProtocolClient();

    ProtocolClient(const ProtocolClient&) = delete;
    ProtocolClient& operator=(const ProtocolClient&) = delete;

    // The path this build resolves from $DISPLAY, exactly as wm2-ctl resolves
    // it -- through configSocketPath(), not through a second spelling of the
    // same convention. Empty when DISPLAY is unset, which a caller must treat
    // as "no socket" rather than as "the empty path".
    static std::string resolveSocketPath();

    // Connect and complete the handshake. True only when the window manager
    // acknowledged; on false, state() and reason() say what happened and the
    // descriptor is already closed.
    bool connect(const std::string& path);

    // Drop the connection and enter `newState` with `reason`. Safe to call on a
    // client that never connected.
    void disconnect(State newState, const std::string& reason);

    State state() const { return m_state; }
    bool connected() const { return m_state == State::Connected; }
    const std::string& reason() const { return m_reason; }

    // For a GLib socket source. -1 when there is no connection.
    int fileDescriptor() const { return m_fd; }

    // Drain everything readable and dispatch it. Called by the socket source,
    // and by pumpUntil().
    void onReadable();

    bool sendGet(const std::string& key, ReplyHandler onReply);
    bool sendSet(const std::string& key, const std::string& value,
                 ReplyHandler onReply);
    bool sendReload(ReplyHandler onReply);

    void setNoticeHandler(NoticeHandler handler) { m_onNotice = std::move(handler); }
    void setStateHandler(StateHandler handler) { m_onState = std::move(handler); }

    // Poll the descriptor until `predicate` is true or `timeoutMs` expires.
    // False on the deadline, on a closed peer, or when there is no connection.
    bool pumpUntil(const std::function<bool()>& predicate, int timeoutMs);

private:
    struct Pending {
        // READ BY dispatch(), which is the whole of the correlation rule: a
        // decodable line only pops this entry when its type is the one the
        // request asked for (or `error`, which answers anything).
        ConfigMessageType expected = ConfigMessageType::Ack;
        ReplyHandler      handler;
    };

    bool send(const ConfigMessage& message);
    bool request(const ConfigMessage& message, ConfigMessageType expected,
                 ReplyHandler onReply);
    void dispatch(const ConfigMessage& message);
    void setState(State newState, const std::string& reason);

    int                m_fd = -1;
    State              m_state = State::Idle;
    std::string        m_reason;
    std::string        m_in;
    std::deque<Pending> m_pending;
    NoticeHandler      m_onNotice;
    StateHandler       m_onState;
};
