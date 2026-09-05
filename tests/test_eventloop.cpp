#include <catch2/catch_test_macros.hpp>
#include "Manager.h"
#include "EventPump.h"
#include <X11/Xatom.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

TEST_CASE("FdGuard default constructed has fd -1", "[eventloop][raii]")
{
    FdGuard g;
    REQUIRE(g.get() == -1);
}

TEST_CASE("FdGuard closes fd on destruction", "[eventloop][raii]")
{
    int pipefd[2];
    REQUIRE(pipe(pipefd) == 0);
    int read_end = pipefd[0];
    int write_end = pipefd[1];

    {
        FdGuard g(write_end);
        REQUIRE(g.get() == write_end);
        // g goes out of scope, should close write_end
    }

    // Writing to read_end's peer (write_end) should fail now
    // because write_end was closed by FdGuard destructor.
    // The read_end should get EPIPE when we try to write to it.
    // Alternatively, verify by checking that write_end fd is invalid.
    // Best: use fcntl to check if write_end is still valid.
    // After close, fcntl should return -1 with EBADF.
    REQUIRE(fcntl(write_end, F_GETFL) == -1);
    REQUIRE(errno == EBADF);

    // Clean up read_end
    close(read_end);
}

TEST_CASE("FdGuard move transfers ownership", "[eventloop][raii]")
{
    int pipefd[2];
    REQUIRE(pipe(pipefd) == 0);

    FdGuard a(pipefd[1]);
    REQUIRE(a.get() == pipefd[1]);

    FdGuard b(std::move(a));
    REQUIRE(b.get() == pipefd[1]);
    REQUIRE(a.get() == -1);  // moved-from

    close(pipefd[0]);
    // b will close pipefd[1] on destruction
}

TEST_CASE("FdGuard move assignment transfers ownership", "[eventloop][raii]")
{
    int pipefd[2];
    REQUIRE(pipe(pipefd) == 0);

    FdGuard a(pipefd[1]);
    REQUIRE(a.get() == pipefd[1]);

    FdGuard b;
    b = std::move(a);
    REQUIRE(b.get() == pipefd[1]);
    REQUIRE(a.get() == -1);  // moved-from

    close(pipefd[0]);
    // b will close pipefd[1] on destruction
}

TEST_CASE("Self-pipe can be created and used", "[eventloop][pipe]")
{
    int pipefd[2];
    REQUIRE(pipe(pipefd) == 0);

    // Write a byte to the write end
    char c = 'x';
    REQUIRE(write(pipefd[1], &c, 1) == 1);

    // Read it back from the read end
    char buf;
    REQUIRE(read(pipefd[0], &buf, 1) == 1);
    REQUIRE(buf == 'x');

    close(pipefd[0]);
    close(pipefd[1]);
}


// ---------------------------------------------------------------------------
// 08.5-11: deterministic cases against the extracted event-loop surface.
//
// Every case here drives a symbol from include/EventPump.h, which is what
// WindowManager::nextEvent() itself consults -- so changing production changes
// these results. No case spawns a window manager, samples a rate, or waits on
// a wall-clock threshold; each creates and owns the X connections it uses and
// closes them on the way out.
// ---------------------------------------------------------------------------

// Bounded wait for bytes to reach a descriptor.
//
// Production polls with timeout = -1 (src/Manager.cpp computePollTimeout()
// returns -1 whenever no focus timer is armed). That is exactly what turns the
// bounded reproduction below into an unbounded hang in the real loop. A test
// must never do it: a failure has to be REPORTED, not hung on.
static bool waitReadable(int fd, int timeoutMs)
{
    struct pollfd p;
    p.fd = fd;
    p.events = POLLIN;
    p.revents = 0;
    int r = poll(&p, 1, timeoutMs);
    return r > 0 && (p.revents & POLLIN) != 0;
}

static bool fdReadable(Display *d)
{
    return waitReadable(ConnectionNumber(d), 0);
}


TEST_CASE("pump-reports-what-is-available: the pump must not report zero "
          "while an event is already in hand", "[eventloop][pump]")
{
    // Follows evidence/gates/eventloop/wedge-probe.c step for step. Nothing
    // further is queued on A before the flush: on this host the flush polls
    // the transport anyway, and the probe is the evidence for that.
    Display *A = XOpenDisplay(nullptr);
    Display *B = XOpenDisplay(nullptr);
    REQUIRE(A != nullptr);
    REQUIRE(B != nullptr);

    XSelectInput(A, DefaultRootWindow(A), PropertyChangeMask);
    XSync(A, False);

    // Start from a genuinely quiet connection, so the event observed below is
    // the one this case caused and not leftover root traffic.
    while (XPending(A) > 0) {
        XEvent discard;
        XNextEvent(A, &discard);
    }

    Atom a = XInternAtom(B, "_WM2_PUMP_CASE", False);
    XChangeProperty(B, DefaultRootWindow(B), a, XA_STRING, 8,
                    PropModeReplace, (unsigned char *)"y", 1);
    XFlush(B);

    // The bytes are on their way; wait for them rather than for a duration.
    REQUIRE(waitReadable(ConnectionNumber(A), 2000));

    // This is src/Events.cpp:195 as it was: the queue is empty, so the
    // early-return branch is NOT taken.
    REQUIRE(QLength(A) == 0);

    int reported = eventPumpPending(A);

    // Availability is established INDEPENDENTLY of the pump's own answer:
    // after the call the event is either reported or sitting in Xlib's queue
    // where the flush deposited it. This assertion is what makes the next one
    // mean "reported zero while holding an event" rather than merely
    // restating whatever the pump said.
    int queuedAfter = QLength(A);
    REQUIRE((reported > 0 || queuedAfter > 0));

    // THE DEFECT. At the round base the flush has moved the event into the
    // queue and drained the socket, and the pump reports nothing pending --
    // so the caller goes on to block in poll() on an event it already holds.
    REQUIRE(reported > 0);

    XCloseDisplay(A);
    XCloseDisplay(B);
}


TEST_CASE("pump-reports-zero-when-idle: a quiescent connection reports "
          "nothing pending", "[eventloop][pump]")
{
    // Guards the case above against a stub. Without this, a pump rewritten to
    // `return 1` would satisfy pump-reports-what-is-available and the round
    // would certify a constant.
    Display *D = XOpenDisplay(nullptr);
    REQUIRE(D != nullptr);

    // Nothing selected, so nothing can be generated for this connection.
    XSync(D, False);
    while (XPending(D) > 0) {
        XEvent discard;
        XNextEvent(D, &discard);
    }

    REQUIRE(eventPumpPending(D) == 0);

    XCloseDisplay(D);
}


// Scoped error handler for the case below. Xlib's default handler prints the
// error and TERMINATES the process, which would take the whole binary down
// instead of failing one case.
static int g_protocolErrors = 0;

static int countingErrorHandler(Display *, XErrorEvent *)
{
    ++g_protocolErrors;
    return 0;
}


TEST_CASE("readable-is-not-deliverable: a readable descriptor carrying a "
          "protocol error must not mean deliver-an-event", "[eventloop][pump]")
{
    Display *D = XOpenDisplay(nullptr);
    REQUIRE(D != nullptr);

    g_protocolErrors = 0;
    XErrorHandler previous = XSetErrorHandler(countingErrorHandler);

    Window w = XCreateSimpleWindow(D, DefaultRootWindow(D), 0, 0, 10, 10, 0,
                                   BlackPixel(D, DefaultScreen(D)),
                                   WhitePixel(D, DefaultScreen(D)));
    XSync(D, False);
    XDestroyWindow(D, w);
    XSync(D, False);

    while (XPending(D) > 0) {
        XEvent discard;
        XNextEvent(D, &discard);
    }

    // A request against the window just destroyed. The server answers with a
    // BadWindow error, which makes the descriptor readable without making an
    // ordinary event available.
    Atom a = XInternAtom(D, "_WM2_ERROR_CASE", False);
    XChangeProperty(D, w, a, XA_STRING, 8, PropModeReplace,
                    (unsigned char *)"y", 1);
    XFlush(D);

    REQUIRE(waitReadable(ConnectionNumber(D), 2000));

    // The state src/Events.cpp:235 would see: descriptor readable, no
    // ordinary event available.
    REQUIRE(fdReadable(D));
    REQUIRE(QLength(D) == 0);

    EventPumpPollResult state;
    state.pollResult    = 1;
    state.pollErrno     = 0;
    state.xRevents      = POLLIN;
    state.pipeRevents   = 0;
    state.exitFlagSet   = false;
    state.focusChanging = false;

    // THE DEFECT. Readability is not deliverability: XNextEvent() will hand
    // this to the error handler and keep BLOCKING for a real event, starving
    // the self-pipe and every timer behind it.
    REQUIRE(eventPumpDecide(state) != EventPumpAction::DeliverEvent);

    // Dispatch the error while OUR handler is still installed. The error is
    // still sitting unread on the connection at this point, and both
    // XCloseDisplay() and a restored default handler would process it under
    // Xlib's default handler -- which prints and terminates the process,
    // taking the whole binary down instead of failing this case. Draining it
    // here also proves the request really did generate a protocol error
    // rather than the descriptor being readable for some other reason.
    XSync(D, False);
    REQUIRE(g_protocolErrors == 1);

    XCloseDisplay(D);
    XSetErrorHandler(previous);
}


TEST_CASE("exit-flag-stops-the-loop: the decision must be to stop when the "
          "exit flag is set", "[eventloop][pump]")
{
    // Drives the decision itself, not the presence of a symbol: a comment
    // mentioning m_signalled would satisfy a token grep and not this.
    EventPumpPollResult state;
    state.pollResult    = 0;
    state.pollErrno     = 0;
    state.xRevents      = 0;
    state.pipeRevents   = 0;
    state.exitFlagSet   = true;
    state.focusChanging = false;

    // THE DEFECT. The flag is accepted and never read, so the root menu's
    // Exit action (src/Buttons.cpp:591) sets a flag with no consumer and the
    // loop goes round again.
    REQUIRE(eventPumpDecide(state) == EventPumpAction::StopOnSignal);
}
