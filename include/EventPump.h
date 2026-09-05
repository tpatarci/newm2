#pragma once

// The event loop's two decisions, lifted out of WindowManager::nextEvent()
// so that a test can reach them.
//
// This header is deliberately free of any project header. test_eventloop
// links Catch2 and X11 only, with include/ on the header path and no
// production objects, so anything reachable from there must depend on
// nothing but Xlib and the C library. That constraint is the whole reason
// these decisions live in a header rather than in a WindowManager member.
//
// 08.5-11 Task 1 extracted these FAITHFULLY: both functions reproduce the
// inline code's behaviour exactly, defects included. The defects are named
// at the sites where they live.

#include <X11/Xlib.h>
#include <poll.h>
#include <cerrno>


// How many events can be dequeued without blocking?
//
// ONE operation that both tests and flushes. XPending() is
// XEventsQueued(QueuedAfterFlush): it returns the queue length when non-zero,
// and otherwise flushes and performs the non-blocking transport read, then
// reports what that read produced.
//
// That single call is the fix. The old sequence tested QLength() first and
// flushed afterwards, and the flush polls the transport on this host -- so it
// could move an event into dpy->qlen and drain the socket AFTER the predicate
// had already answered "nothing pending". The caller then blocked in poll()
// on an event it was already holding:
//
//     before flush:  QLength=0, fd readable=1
//     after  flush:  QLength=1, fd readable=0   *** WEDGE ***
//
// (measured by evidence/gates/eventloop/wedge-probe.c and reproduced by the
// pump-reports-what-is-available case). Folding predicate and flush together
// leaves no window between them for an event to slip into.
//
// Nothing may be added between a zero result here and the caller's poll()
// except computePollTimeout(), which reads local state only. Any Xlib call
// reintroduced into that gap is the same defect in a new spelling.
inline int eventPumpPending(Display *d)
{
    return XPending(d);
}


// What the loop should do once poll() has returned.
enum class EventPumpAction {
    DeliverEvent,      // an event is available: dequeue and dispatch it
    Block,             // nothing to do but wait
    Retry,             // go round again (re-pump, then poll again)
    ServiceFocusTick,  // a focus timer expired
    StopOnSignal,      // the self-pipe fired: shut down with status 0
    StopOnError        // poll() failed: shut down with status 1
};


// Everything the post-poll decision is allowed to look at.
struct EventPumpPollResult {
    int   pollResult    = 0;      // poll()'s return value
    int   pollErrno     = 0;      // errno captured immediately after poll()
    short xRevents      = 0;      // revents on the X connection
    short pipeRevents   = 0;      // revents on the self-pipe read end
    bool  exitFlagSet   = false;  // the signal flag (m_signalled)
    bool  focusChanging = false;  // a focus change is in progress
};


// Every revent that reports a FAILED descriptor. All three are delivered
// regardless of what .events asked for, which is why testing POLLIN alone
// left an errored descriptor matching no branch at all.
inline short eventPumpFailedRevents()
{
    return static_cast<short>(POLLERR | POLLHUP | POLLNVAL);
}


inline EventPumpAction eventPumpDecide(const EventPumpPollResult &s)
{
    // The exit flag is observed FIRST, and so independently of queue depth:
    // a sustained event stream must not be able to starve shutdown. The
    // caller checks it again after the pump, because a signal can arrive
    // while the flush is in progress.
    if (s.exitFlagSet) return EventPumpAction::StopOnSignal;

    if (s.pollResult < 0) {
        // A signal interrupted the wait; the caller re-checks its loop flag.
        if (s.pollErrno == EINTR) return EventPumpAction::Retry;
        return EventPumpAction::StopOnError;
    }

    if (s.pipeRevents & POLLIN) return EventPumpAction::StopOnSignal;

    // A failed descriptor stops the loop explicitly. Previously none of these
    // matched a branch, so an errored descriptor fell through and re-polled
    // immediately, spinning at full CPU on an error that would never clear.
    //
    // The old standalone XFlush() did not rescue this. That is the WRITE
    // path: on a connection whose output buffer is empty the flush has no
    // bytes to send, so Xlib never discovered the connection was gone. This
    // does not contradict the pump reproduction above, where the same call
    // still polls the transport for READABLE bytes -- wedge-probe.c measures
    // exactly that, and the two are different directions of the same call.
    if ((s.xRevents & eventPumpFailedRevents()) ||
        (s.pipeRevents & eventPumpFailedRevents())) {
        return EventPumpAction::StopOnError;
    }

    if (s.pollResult == 0) {
        if (s.focusChanging) return EventPumpAction::ServiceFocusTick;
        return EventPumpAction::Retry;
    }

    // Readability is NOT deliverability, so a readable X descriptor never
    // yields deliver-an-event. A protocol error makes the descriptor readable
    // and satisfies poll(), but XNextEvent() will not return it as an event --
    // Xlib hands it to the error handler and keeps BLOCKING for a real one,
    // starving the self-pipe and every timer behind it.
    //
    // Re-pumping instead is what makes both outcomes correct: an ordinary
    // event becomes available through the pump, and a protocol error reaches
    // the error handler without the loop waiting for some later event first.
    return EventPumpAction::Retry;
}
