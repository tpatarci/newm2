#include <catch2/catch_test_macros.hpp>
#include "Manager.h"
#include "EventPump.h"
#include "TimestampWait.h"
#include <X11/Xatom.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <csignal>
#include <cstring>
#include <ctime>
#include <sys/wait.h>

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


// ---------------------------------------------------------------------------
// 08.5-12: deterministic cases against the extracted timestamp surface.
//
// Every case below drives a symbol from include/TimestampWait.h -- the real
// extracted production code, which WindowManager::timestamp() itself consults,
// so changing production changes these results. None reimplements the code it
// tests, none spawns a window manager, and none uses a rate or a sampling run
// as evidence. Each is a deterministic reproduction of a defect established by
// reading the source.
//
// CLEANUP RUNS FROM DESTRUCTORS, NOT FROM STRAIGHT-LINE CODE. _WM2_RUNNING
// lives on the root window of the display this binary shares with every other
// case, and root properties OUTLIVE the connection that set them. A case that
// poisons it and does not restore it breaks every later case and every later
// run on :99 -- including 08.5-11's, which hold the same x_display_99 resource
// lock but run in a different process. Catch2 unwinds a failed REQUIRE by
// throwing, so cleanup written after an assertion is cleanup that does not
// happen on precisely the path where it matters.
// ---------------------------------------------------------------------------

namespace {

// Non-fatal protocol-error trap. Xlib's default handler PRINTS AND TERMINATES
// the process, which would take the whole binary down instead of failing one
// case. This is the scoped idiom 08.5-11's readable-is-not-deliverable case
// established, reused rather than reinvented.
int g_timestampErrors = 0;
int g_timestampLastErrorCode = 0;

int timestampErrorHandler(Display *, XErrorEvent *e)
{
    ++g_timestampErrors;
    g_timestampLastErrorCode = e->error_code;
    return 0;
}

struct ErrorHandlerGuard {
    XErrorHandler previous;
    explicit ErrorHandlerGuard(XErrorHandler h) : previous(XSetErrorHandler(h)) {}
    ~ErrorHandlerGuard() { XSetErrorHandler(previous); }
};

struct DisplayGuard {
    Display *d;
    explicit DisplayGuard(Display *dd) : d(dd) {}
    ~DisplayGuard() { if (d != nullptr) XCloseDisplay(d); }
};

// Removes a root property on EVERY exit path, assertion failures included.
// Always declared AFTER the display and handler guards so that it destructs
// BEFORE them: the delete is an ordinary request and has to be issued while
// the connection is still open and the non-fatal handler is still installed.
struct RootPropertyGuard {
    Display *d;
    Window   root;
    Atom     atom;
    ~RootPropertyGuard()
    {
        XDeleteProperty(d, root, atom);
        XSync(d, False);
    }
};

void drainQueue(Display *d)
{
    XSync(d, False);
    while (XPending(d) > 0) {
        XEvent discard;
        XNextEvent(d, &discard);
    }
}

}  // namespace


TEST_CASE("timestamp-survives-poisoned-property: the sentinel request must not "
          "raise a protocol error against a root property a foreign client has "
          "replaced", "[eventloop][timestamp]")
{
    Display *D = XOpenDisplay(nullptr);
    Display *F = XOpenDisplay(nullptr);

    ErrorHandlerGuard handlerGuard(timestampErrorHandler);
    DisplayGuard dGuard(D);
    DisplayGuard fGuard(F);

    REQUIRE(D != nullptr);
    REQUIRE(F != nullptr);

    const Window root     = DefaultRootWindow(D);
    const Atom   sentinel = XInternAtom(D, "_WM2_RUNNING", False);

    RootPropertyGuard propertyGuard{D, root, sentinel};

    g_timestampErrors = 0;
    g_timestampLastErrorCode = 0;

    // A foreign client replaces the root property with a DIFFERENT type.
    // Nothing privileged is involved and no window manager need be running:
    // root properties are owned by nobody and any client on the display may do
    // exactly this.
    XChangeProperty(F, DefaultRootWindow(F), sentinel, XA_STRING, 8,
                    PropModeReplace, (unsigned char *)"poison", 6);
    XSync(F, False);

    // The production request itself, not a copy of it.
    timestampSentinelRequest(D, root, sentinel);
    XSync(D, False);

    const int errors = g_timestampErrors;
    const int code   = g_timestampLastErrorCode;

    INFO("protocol errors from the sentinel request: " << errors
         << "; last error code: " << code << " (BadMatch is " << BadMatch << ")");

    // THE DEFECT, AND IT IS FATAL RATHER THAN SLOW. A zero-length append
    // requires the EXISTING property's type and format to match the ones named
    // in the request, so this fails with BadMatch. In the real constructor that
    // error arrives while m_initialising is still true -- timestamp(true) is
    // called at src/Manager.cpp:288 and the flag is not cleared until :290 --
    // and errorHandler() calls std::exit(1) for ANY error in that window
    // (:416-419). So the consequence in production is not a window manager that
    // hangs; it is a window manager that never starts. No deadline on the wait
    // can reach this, because the wait is never entered.
    REQUIRE(errors == 0);
}


TEST_CASE("timestamp-leaves-foreign-property-alone: a notification that is not "
          "the sentinel must be left in the queue for the ordinary dispatcher",
          "[eventloop][timestamp]")
{
    Display *D = XOpenDisplay(nullptr);

    ErrorHandlerGuard handlerGuard(timestampErrorHandler);
    DisplayGuard dGuard(D);

    REQUIRE(D != nullptr);

    const Window root     = DefaultRootWindow(D);
    const Atom   sentinel = XInternAtom(D, "_WM2_RUNNING", False);
    const Atom   foreign  = XInternAtom(D, "_WM2_FOREIGN_CASE", False);

    RootPropertyGuard sentinelGuard{D, root, sentinel};
    RootPropertyGuard foreignGuard{D, root, foreign};

    XSelectInput(D, root, PropertyChangeMask);
    drainQueue(D);

    // Create the sentinel property so that it can be DELETED below. The
    // creation itself generates PropertyNewValue on the sentinel's own window
    // and atom -- which is exactly the sentinel's shape -- so it is drained
    // before the events under test are armed. Leaving it queued would let this
    // case pass for the wrong reason.
    XChangeProperty(D, root, sentinel, sentinel, 8, PropModeReplace,
                    (unsigned char *)"", 0);
    drainQueue(D);

    // Arm the two events that must NOT be accepted.
    //
    // (a) a PropertyDelete on the sentinel's OWN window and atom. The sentinel
    //     is a zero-length change and so generates PropertyNewValue; a
    //     predicate that checks window, atom and send_event but NOT state
    //     still accepts this one, and it is a different event. This is why the
    //     predicate needs four fields rather than three.
    XDeleteProperty(D, root, sentinel);

    // (b) an ordinary notification for a different atom on the same window --
    //     what a managed client's _NET_WM_NAME change looks like from here.
    XChangeProperty(D, root, foreign, XA_STRING, 8, PropModeReplace,
                    (unsigned char *)"y", 1);
    XSync(D, False);

    const int queuedBefore = QLength(D);

    XEvent collected;
    const TimestampWaitResult r =
        timestampWaitFor(D, root, sentinel, kTimestampWaitDeadlineMs, &collected);

    // Availability is established by RETRIEVING the armed events afterwards,
    // not by asking the wait what it thinks it did. Asserting only that the
    // call returned a time would pass at the round base and prove nothing.
    bool foundDelete  = false;
    bool foundForeign = false;
    XSync(D, False);
    while (XPending(D) > 0) {
        XEvent e;
        XNextEvent(D, &e);
        if (e.type != PropertyNotify) continue;
        if (e.xproperty.window == root && e.xproperty.atom == sentinel &&
            e.xproperty.state == PropertyDelete) {
            foundDelete = true;
        }
        if (e.xproperty.window == root && e.xproperty.atom == foreign &&
            e.xproperty.state == PropertyNewValue) {
            foundForeign = true;
        }
    }

    INFO("queued before the wait: " << queuedBefore
         << "; wait matched: " << r.matched
         << "; PropertyDelete survived: " << foundDelete
         << "; foreign notification survived: " << foundForeign);

    REQUIRE(queuedBefore == 2);

    // THE DEFECT. The mask match selects on event TYPE alone, so it consumes
    // whichever of these two arrives first. In production that event never
    // reaches eventProperty() (src/Events.cpp:535) and the client's title
    // silently stops refreshing.
    REQUIRE(r.matched == false);
    REQUIRE(foundDelete);
    REQUIRE(foundForeign);
}


TEST_CASE("timestamp-rejects-synthetic: a forged notification must not supply "
          "the timestamp", "[eventloop][timestamp]")
{
    Display *D = XOpenDisplay(nullptr);

    ErrorHandlerGuard handlerGuard(timestampErrorHandler);
    DisplayGuard dGuard(D);

    REQUIRE(D != nullptr);

    const Window root     = DefaultRootWindow(D);
    const Atom   sentinel = XInternAtom(D, "_WM2_RUNNING", False);

    XSelectInput(D, root, PropertyChangeMask);
    drainQueue(D);

    // A time no server on this host will produce for a real event, so the
    // assertion below cannot pass or fail by coincidence.
    const Time forged = 0x5E4713A9UL;

    XEvent forgedEvent;
    std::memset(&forgedEvent, 0, sizeof(forgedEvent));
    forgedEvent.type             = PropertyNotify;
    forgedEvent.xproperty.window = root;
    forgedEvent.xproperty.atom   = sentinel;
    forgedEvent.xproperty.state  = PropertyNewValue;
    forgedEvent.xproperty.time   = forged;
    forgedEvent.xproperty.display = D;

    // Any client may do this. No privileged access is involved, and the event
    // names the root window and the sentinel atom, so window and atom checks
    // alone do not reject it.
    REQUIRE(XSendEvent(D, root, False, PropertyChangeMask, &forgedEvent) != 0);
    XSync(D, False);

    XEvent collected;
    const TimestampWaitResult r =
        timestampWaitFor(D, root, sentinel, kTimestampWaitDeadlineMs, &collected);

    const bool tookForgedTime = r.matched && collected.xproperty.time == forged;
    const bool sawSendEvent   = r.matched && collected.xproperty.send_event != 0;

    drainQueue(D);

    INFO("wait matched: " << r.matched
         << "; send_event set on the collected event: " << sawSendEvent
         << "; forged time accepted: " << tookForgedTime);

    // THE DEFECT. send_event is never tested, so any client can synthesise a
    // notification carrying a timestamp of its own choosing and this path takes
    // it. Rejecting send_event is correctness, not hardening.
    REQUIRE_FALSE(tookForgedTime);
}


TEST_CASE("timestamp-is-bounded: the wait must return within its deadline when "
          "the sentinel can never arrive", "[eventloop][timestamp]")
{
    // THE ASYMMETRY IS THE DEFECT. This case bounds what production does not.
    // The wait under test has no deadline of its own at the round base, so its
    // failure cannot be observed from inside the process running it: a thread
    // cannot be cancelled inside Xlib without leaving the connection lock held,
    // and a detached one leaks a blocked thread into every later case. The wait
    // therefore runs in a forked CHILD; the parent bounds it, kills it by the
    // pid fork() returned, and REAPS it. An unreaped child holds a display
    // connection open and poisons what follows.
    //
    // Driving the selector through a bounded wrapper of this file's own
    // construction was rejected deliberately: it would avoid calling the
    // defective production helper at all, which is the failure this round
    // exists to end.
    Display *F = XOpenDisplay(nullptr);

    ErrorHandlerGuard handlerGuard(timestampErrorHandler);
    DisplayGuard fGuard(F);

    REQUIRE(F != nullptr);

    const Window root     = DefaultRootWindow(F);
    const Atom   sentinel = XInternAtom(F, "_WM2_RUNNING", False);

    // Removed only when this scope ends, which is AFTER the child has been
    // reaped. Deleting it earlier would generate a PropertyNotify that could
    // wake the very wait this case is proving does not return.
    RootPropertyGuard propertyGuard{F, root, sentinel};

    // Poison the property, so the child's sentinel request fails with BadMatch
    // and no notification is generated for it.
    XChangeProperty(F, root, sentinel, XA_STRING, 8, PropModeReplace,
                    (unsigned char *)"poison", 6);
    XSync(F, False);

    const int childBoundMs = 3000;

    const pid_t pid = fork();
    REQUIRE(pid >= 0);

    if (pid == 0) {
        // CHILD. _exit() on every path: exit() would flush Catch2's buffered
        // stdout a second time and run the sanitizer's atexit hooks inside a
        // fork of a test runner.
        Display *C = XOpenDisplay(nullptr);
        if (C == nullptr) _exit(2);

        // Without this the BadMatch below reaches Xlib's default handler,
        // which exits -- the child would die promptly and this case would pass
        // at the round base while reproducing nothing.
        XSetErrorHandler(timestampErrorHandler);

        const Window childRoot     = DefaultRootWindow(C);
        const Atom   childSentinel = XInternAtom(C, "_WM2_RUNNING", False);

        // PropertyChangeMask is deliberately NOT selected on this connection.
        // "No sentinel can arrive" then holds identically before and after the
        // fix -- at the round base because the poisoned property makes the
        // request fail, and after the fix because nothing is deliverable here
        // at all -- so this case measures BOUNDEDNESS alone and does not
        // silently become a different test once the request is made
        // survivable. It also removes any dependence on the display being
        // quiet: no event of any kind can reach this connection.
        timestampSentinelRequest(C, childRoot, childSentinel);
        XSync(C, False);

        XEvent ev;
        const TimestampWaitResult r = timestampWaitFor(
            C, childRoot, childSentinel, kTimestampWaitDeadlineMs, &ev);

        // Returned at all. 0 is the correct outcome -- there is no sentinel to
        // accept -- and 3 records the wait having accepted something anyway.
        _exit(r.matched ? 3 : 0);
    }

    bool killed   = false;
    int  status   = 0;
    long waitedMs = 0;
    for (;;) {
        const pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) break;
        if (w < 0 && errno != EINTR) { killed = true; break; }
        if (waitedMs >= childBoundMs) {
            kill(pid, SIGKILL);        // the pid fork() returned, never a scan
            waitpid(pid, &status, 0);  // reap, or it holds its connection open
            killed = true;
            break;
        }
        struct timespec ts;
        ts.tv_sec  = 0;
        ts.tv_nsec = 10L * 1000L * 1000L;
        nanosleep(&ts, nullptr);
        waitedMs += 10;
    }

    const bool exitedCleanly =
        !killed && WIFEXITED(status) && WEXITSTATUS(status) == 0;

    INFO("child observed for " << waitedMs << " ms; killed: " << killed
         << "; exited cleanly: " << exitedCleanly
         << "; exit status: " << (WIFEXITED(status) ? WEXITSTATUS(status) : -1));

    // THE DEFECT. With no deadline the wait never returns -- on the window
    // manager's only thread, and the self-pipe cannot interrupt it, because a
    // mask wait is not a poll.
    REQUIRE_FALSE(killed);
    REQUIRE(exitedCleanly);
}
