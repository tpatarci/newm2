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
// 08.5-12 Task 1 extracted these FAITHFULLY. All THREE defects are preserved
// and each is named at the site where it lives:
//
//   1. the sentinel REQUEST is a zero-length append, which requires the
//      existing property's type and format to match and therefore yields
//      BadMatch against a property a foreign client has replaced;
//   2. the WAIT ignores its deadline and blocks indefinitely;
//   3. the PREDICATE accepts any property notification at all, irrespective
//      of window, atom, send_event or state.
//
// The request is extracted WITH the wait, and not left inline, because the
// BadMatch a case must provoke originates in the request. A case driving its
// own copy of the request would prove nothing about production.
//
// If a case written against this header comes out green at this commit, the
// extraction was not faithful and the round has lost its baseline.

#include <X11/Xlib.h>
#include <chrono>


// How long the wait is permitted to block, in milliseconds.
//
// AT THIS COMMIT THIS CONSTANT IS INERT: timestampWaitFor() accepts it and
// ignores it, which is defect 2 preserved. It is declared here so that the
// call site already passes a deadline and the fix has only to start honouring
// it.
//
// The value is chosen on its own terms. A window manager blocked in a property
// wait is serving no client at all -- it is not drawing, not reparenting and
// not answering the root menu -- so the bound is set where a user cannot
// perceive it while still leaving a loaded X server room to answer a round
// trip it has already been asked to make. It is a compile-time constant and
// deliberately not readable from the environment: a timeout a user can widen
// is a timeout that can be widened until the hang returns.
inline constexpr int kTimestampWaitDeadlineMs = 250;


// What the wait did, so that the caller's instrumentation can stay exactly as
// it was. timestamp() owns the counters; this header owns no state.
struct TimestampWaitResult {
    // An event satisfying the predicate was collected into the caller's XEvent.
    bool matched = false;

    // The non-blocking check found nothing, so the wait went on to block.
    // This is what feeds m_timestampBlockedWaits.
    bool blocked = false;

    // The deadline expired without the sentinel arriving. ALWAYS FALSE AT THIS
    // COMMIT -- the wait below has no deadline to expire. The field exists now
    // so the fix changes behaviour rather than shape.
    bool timedOut = false;

    // Milliseconds spent in the blocking wait; zero when it did not block.
    long elapsedMs = 0;
};


// DEFECT 1, PRESERVED. Issue the sentinel property change.
//
// This is src/Manager.cpp:923 verbatim: a zero-length change in PropModeAppend
// whose type is the sentinel atom itself and whose format is 8.
//
// Append requires the EXISTING property's type and format to match the ones
// named here. _WM2_RUNNING lives on the root window, and root properties are
// owned by nobody -- any client on the display may replace one. A client that
// replaces it with a different type or format makes this request fail with
// BadMatch, and a failed request generates no PropertyNotify at all.
//
// The consequence is not merely a slow window manager. The constructor calls
// timestamp(true) at src/Manager.cpp:288, TWO LINES before it clears
// m_initialising at :290, and errorHandler() calls std::exit(1) for any error
// received while that flag is set (:416-419). So against a poisoned root
// property this request does not hang the window manager -- it stops the
// window manager from starting. No deadline on the wait below can reach that
// failure, because the wait is never entered.
inline void timestampSentinelRequest(Display *d, Window root, Atom atom)
{
    XChangeProperty(d, root, atom, atom, 8, PropModeAppend,
                    reinterpret_cast<unsigned char *>(const_cast<char *>("")), 0);
}


// DEFECT 3, PRESERVED. Is this event the sentinel's notification?
//
// It answers yes for ANY property notification, exactly as the mask match in
// src/Manager.cpp:944/:959 does -- a mask selects on event TYPE and on nothing
// else. The window, the atom, the send_event flag and the state are all
// ignored, and the parameters carrying the first two are accepted only so the
// call site already reads the way a correct predicate would.
//
// That breadth has three separate consequences, all of them live:
//
//   - it CONSUMES another client's notification. PropertyChangeMask is
//     selected on the root (src/Manager.cpp:603) and on every managed client,
//     so a client's own _NET_WM_NAME change can satisfy this wait, never reach
//     eventProperty() (src/Events.cpp:535), and the client's title silently
//     stops refreshing.
//   - it ACCEPTS A FORGED TIME. send_event is not tested, so any client may
//     synthesise a notification carrying a timestamp of its choosing and this
//     path will take it.
//   - it fires DETERMINISTICALLY AT STARTUP, not merely under contention:
//     initialiseScreen() selects the property bit at :603, setupEwmhProperties()
//     changes root properties at :792, and only afterwards does the constructor
//     call timestamp(true) at :288. Those earlier changes precede the sentinel
//     in request order, so the first matching event is reliably an EWMH
//     notification rather than the sentinel.
//
// A fix must test FOUR fields, not three. The sentinel is a zero-length
// change, which generates PropertyNewValue; a predicate that checks window,
// atom and send_event but not state still accepts a PropertyDelete on the very
// same window and atom, which is a different event.
inline bool timestampIsSentinel(const XEvent &e, Window root, Atom atom)
{
    (void)root;
    (void)atom;
    return e.type == PropertyNotify;
}


// DEFECT 2, PRESERVED. Collect the first event the predicate accepts.
//
// deadlineMs is accepted and IGNORED, and the blocking selector below has no
// deadline of any kind. When the sentinel request above has failed with
// BadMatch there is no notification coming, and this call does not return --
// on the window manager's only thread, with the self-pipe unable to interrupt
// it, because a mask wait is not a poll.
//
// The structure is src/Manager.cpp:944-976 unchanged: the non-blocking check
// is taken first so that an already-queued event is returned without
// blocking, and only an empty queue reaches the blocking selector.
inline TimestampWaitResult timestampWaitFor(Display *d, Window root, Atom atom,
                                            int deadlineMs, XEvent *out)
{
    (void)deadlineMs;
    (void)root;
    (void)atom;

    TimestampWaitResult r;

    if (XCheckMaskEvent(d, PropertyChangeMask, out) != False) {
        r.matched = true;
        return r;
    }

    r.blocked = true;

    const auto waitStart = std::chrono::steady_clock::now();
    XMaskEvent(d, PropertyChangeMask, out);
    r.elapsedMs = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - waitStart).count());

    r.matched = true;
    return r;
}
