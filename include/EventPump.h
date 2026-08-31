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
// This mirrors src/Events.cpp's old top-of-loop sequence exactly: test the
// queue, and on an empty queue flush and report nothing pending.
inline int eventPumpPending(Display *d)
{
    int queued = QLength(d);
    if (queued > 0) return queued;

    XFlush(d);

    // *** THE DEFECT UNDER TEST ***
    //
    // Reporting zero here is what strands the event. On this host XFlush()
    // polls the transport -- _XFlush reaches xcb_poll_for_event /
    // xcb_poll_for_queued_event -- so by the time control returns the flush
    // may have MOVED a pending event into dpy->qlen AND drained the socket.
    // The queue length is never re-tested, so the caller reports "nothing
    // pending" while holding an event, and then blocks in poll() on a
    // descriptor that has nothing left to deliver.
    //
    // Measured, replaying src/Events.cpp:195-205 (see
    // evidence/gates/eventloop/wedge-probe.c):
    //     before flush:  QLength=0, fd readable=1
    //     after  flush:  QLength=1, fd readable=0   *** WEDGE ***
    return 0;
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


// The current mapping, reproduced exactly.
inline EventPumpAction eventPumpDecide(const EventPumpPollResult &s)
{
    if (s.pollResult < 0) {
        // A signal interrupted the wait; the caller re-checks its loop flag.
        if (s.pollErrno == EINTR) return EventPumpAction::Retry;
        return EventPumpAction::StopOnError;
    }

    if (s.pipeRevents & POLLIN) return EventPumpAction::StopOnSignal;

    if (s.pollResult == 0) {
        if (s.focusChanging) return EventPumpAction::ServiceFocusTick;
        return EventPumpAction::Retry;
    }

    // *** THE SECOND DEFECT UNDER TEST ***
    //
    // A readable X descriptor is treated as proof that an ordinary event is
    // available. It is not. A protocol ERROR makes the descriptor readable
    // and satisfies poll(), but XNextEvent() will not return it as an event
    // -- Xlib hands it to the error handler and keeps waiting for a real one.
    // So the caller blocks inside XNextEvent(), and the self-pipe and every
    // timer are starved behind it.
    if (s.xRevents & POLLIN) return EventPumpAction::DeliverEvent;

    // *** THE THIRD DEFECT UNDER TEST ***
    //
    // POLLERR, POLLHUP and POLLNVAL are delivered regardless of what .events
    // asked for, and none of them is matched above. An errored descriptor
    // therefore falls through to a retry, which re-polls immediately and
    // returns the same error: a spin at full CPU.
    //
    // *** THE FOURTH DEFECT UNDER TEST ***
    //
    // s.exitFlagSet is accepted and never read. The root menu's Exit action
    // sets that flag (src/Buttons.cpp:591) and nothing consumes it, so Exit
    // does not exit.
    return EventPumpAction::Retry;
}
