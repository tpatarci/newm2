#pragma once

// The timestamp path's three operations, lifted out of
// WindowManager::timestamp() so that a test can reach them.
//
// This header is deliberately free of any project header. test_eventloop
// links Catch2 and X11 only, with include/ on the header path and no
// production objects, so anything reachable from there must depend on
// nothing but Xlib and the C++ library. That constraint is the whole reason
// these operations live in a header rather than in a WindowManager member:
// timestamp() is a member of a class whose constructor opens a display and
// enters the event loop, so a case cannot call it.
//
// 08.5-12 Task 1 extracted these faithfully, defects included, so that Task 2
// could reproduce all three deterministically. 08.5-12 Task 3 -- this commit --
// fixes them. All three fixes were required and none subsumes another:
//
//   1. the sentinel REQUEST is now a zero-length PropModeReplace, whose type
//      and format the code itself chooses and which therefore cannot mismatch;
//   2. the WAIT is now bounded against a monotonic deadline, built from the
//      NON-BLOCKING predicate selector plus poll();
//   3. the PREDICATE now tests four fields -- window, atom, send_event and
//      state -- and a non-matching event is LEFT IN THE QUEUE.
//
// Narrowing alone would still permit an unbounded wait when the sentinel never
// arrives; bounding alone would still steal other clients' events and still let
// a poisoned property kill startup; fixing the request alone would leave both
// selector defects.
//
// The request lives here WITH the wait, and not inline in timestamp(), because
// the BadMatch a case must provoke originates in the request. A case driving
// its own copy of the request would prove nothing about production.

#include <X11/Xlib.h>
#include <chrono>
#include <poll.h>
#include <cerrno>


// How long the wait may block, in milliseconds.
//
// Chosen ON ITS OWN TERMS, and deliberately not by reference to any constant
// under tests/. A window manager blocked in a property wait is serving no
// client at all -- not drawing, not reparenting, not answering the root menu --
// so the bound is set where a user cannot perceive it while still leaving a
// loaded X server room to answer a round trip it has already been asked to
// make. Expiry is a defined outcome, not an error: the caller returns
// CurrentTime and leaves its cache cold so the next call retries.
//
// A compile-time constant, and deliberately NOT readable from the environment:
// a timeout a user can widen is a timeout that can be widened until the hang
// comes back.
inline constexpr int kTimestampWaitDeadlineMs = 250;


// What the wait did, so that the caller's instrumentation can stay exactly as
// it was. timestamp() owns the counters; this header owns no state.
struct TimestampWaitResult {
    // An event satisfying the predicate was collected into the caller's XEvent.
    bool matched = false;

    // The non-blocking check found nothing, so the wait went on to block.
    // This is what feeds m_timestampBlockedWaits.
    bool blocked = false;

    // The deadline expired without the sentinel arriving. A DEFINED OUTCOME:
    // the caller returns CurrentTime and leaves its cache cold, so a later
    // call retries rather than caching a bad value.
    bool timedOut = false;

    // Milliseconds spent in the blocking wait; zero when it did not block.
    long elapsedMs = 0;
};


// DEFECT 1, FIXED. Issue the sentinel property change.
//
// A zero-length change whose only purpose is to make the server generate a
// PropertyNotify carrying a server timestamp. The value is empty and nothing
// anywhere reads it; only the notification matters.
//
// PropModeReplace, NOT PropModeAppend. This is the whole fix, and the failure
// it removes is fatal rather than slow:
//
//   Append requires the EXISTING property's type and format to match the ones
//   named in the request. _WM2_RUNNING lives on the root window and root
//   properties are owned by nobody, so any client on the display may replace
//   it with a different type -- after which the append fails with BadMatch and
//   generates no PropertyNotify at all. The constructor calls timestamp(true)
//   at src/Manager.cpp:288, TWO LINES before it clears m_initialising at :290,
//   and errorHandler() calls std::exit(1) for ANY error received while that
//   flag is set (:416-419). So against a poisoned root property the old
//   request did not hang the window manager -- IT STOPPED THE WINDOW MANAGER
//   FROM STARTING, and no deadline on the wait below could reach that failure
//   because the wait was never entered.
//
// Replace defines the property's type and format rather than requiring them,
// so it cannot mismatch and there is nothing here for a foreign client to
// poison. That is why no error trap is installed: a trap would have had to sit
// beside an error the handler at :416-419 is otherwise RIGHT to treat as fatal,
// and the surest way not to suppress a genuine initialisation error is not to
// generate a spurious one.
inline void timestampSentinelRequest(Display *d, Window root, Atom atom)
{
    XChangeProperty(d, root, atom, atom, 8, PropModeReplace,
                    reinterpret_cast<unsigned char *>(const_cast<char *>("")), 0);
}


// DEFECT 3, FIXED. Is this event the sentinel's own notification?
//
// FOUR fields, not three, and every one of them is load-bearing. The mask match
// this replaces (src/Manager.cpp:944/:959) tested none of them: a mask selects
// on event TYPE and on nothing else, so it accepted every property
// notification on the connection. That breadth had three separate consequences:
//
//   - window and atom. PropertyChangeMask is selected on the root
//     (src/Manager.cpp:603) and on every managed client, so a client's own
//     _NET_WM_NAME change could satisfy the wait, be consumed here, never reach
//     eventProperty() (src/Events.cpp:535), and the client's title would
//     silently stop refreshing. It was not merely a contention hazard either:
//     initialiseScreen() selects the property bit at :603,
//     setupEwmhProperties() changes root properties at :792, and only
//     afterwards does the constructor call timestamp(true) at :288 -- so the
//     first matching event was DETERMINISTICALLY an EWMH notification rather
//     than the sentinel.
//
//   - send_event. Any client may synthesise a notification naming the root and
//     this atom and carrying a timestamp of its own choosing, and the old code
//     took it. Rejecting a synthetic event here is correctness, not hardening.
//
//   - state. The sentinel is a zero-length change and therefore generates
//     PropertyNewValue. A predicate testing window, atom and send_event but NOT
//     state would still accept a PropertyDelete on the very same window and
//     atom, which is a different event -- a narrower version of the same
//     defect rather than a fix for it.
inline bool timestampIsSentinel(const XEvent &e, Window root, Atom atom)
{
    return e.type == PropertyNotify &&
           e.xproperty.window == root &&
           e.xproperty.atom == atom &&
           e.xproperty.send_event == False &&
           e.xproperty.state == PropertyNewValue;
}


// Adapter for Xlib's predicate-based selectors, which take a C-style callback
// and one opaque argument.
struct TimestampSentinelMatch {
    Window root;
    Atom   atom;
};

inline Bool timestampSentinelPredicate(Display *, XEvent *e, XPointer arg)
{
    const TimestampSentinelMatch *m =
        reinterpret_cast<const TimestampSentinelMatch *>(arg);
    return timestampIsSentinel(*e, m->root, m->atom) ? True : False;
}


// DEFECT 2, FIXED. Collect the sentinel's notification, or give up on time.
//
// Built from the NON-BLOCKING predicate selector plus poll() against a
// MONOTONIC remaining deadline. The blocking selectors are all unusable here
// and none of them may come back: XMaskEvent, XIfEvent, XWindowEvent and
// XPeekIfEvent every one of them waits with no deadline, so a sentinel that
// never arrives -- which is exactly what a failed request produces -- never
// returns, on the window manager's only thread, with the self-pipe unable to
// interrupt it because a mask wait is not a poll.
//
// The predicate form matters for a second reason beyond the deadline: a
// non-matching event is LEFT IN THE QUEUE for the ordinary dispatcher instead
// of being swallowed here.
//
// The clock is steady_clock, so the bound survives a wall-clock adjustment.
// The selector is called once BEFORE the loop, once after every poll() return,
// and once more before a timeout is declared -- never poll-then-conclude.
// XCheckIfEvent flushes and reads the transport itself, so concluding from
// poll()'s answer alone would reproduce the wedge 08.5-11 removed from the
// event loop: an event already moved into Xlib's queue while the descriptor is
// no longer readable.
inline TimestampWaitResult timestampWaitFor(Display *d, Window root, Atom atom,
                                            int deadlineMs, XEvent *out)
{
    TimestampWaitResult r;
    TimestampSentinelMatch match{root, atom};
    const XPointer arg = reinterpret_cast<XPointer>(&match);

    if (XCheckIfEvent(d, out, timestampSentinelPredicate, arg) == True) {
        r.matched = true;
        return r;
    }

    r.blocked = true;

    const auto waitStart = std::chrono::steady_clock::now();
    const auto elapsedMs = [&waitStart]() {
        return static_cast<long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - waitStart).count());
    };

    const int fd = ConnectionNumber(d);

    for (;;) {
        const long spent = elapsedMs();
        if (spent >= static_cast<long>(deadlineMs)) {
            r.timedOut = true;
            break;
        }

        struct pollfd p;
        p.fd      = fd;
        p.events  = POLLIN;
        p.revents = 0;

        const int pollResult = poll(&p, 1, static_cast<int>(deadlineMs - spent));

        if (pollResult < 0) {
            if (errno == EINTR) continue;
            // The connection is unusable. Give up on time rather than spin on
            // an error that will never clear -- the same conclusion the event
            // loop reaches for a failed descriptor.
            r.timedOut = true;
            break;
        }

        if (XCheckIfEvent(d, out, timestampSentinelPredicate, arg) == True) {
            r.matched = true;
            break;
        }

        if (pollResult == 0) {
            r.timedOut = true;
            break;
        }
    }

    r.elapsedMs = elapsedMs();
    return r;
}
