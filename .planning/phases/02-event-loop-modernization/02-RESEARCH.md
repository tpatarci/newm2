# Phase 2: Event Loop Modernization - Research

**Researched:** 2026-05-07
**Domain:** POSIX poll()-based event loop with self-pipe signal wakeup, C++17 chrono timers
**Confidence:** HIGH

## Summary

This phase replaces the current `select()`+`goto` event loop in `src/Events.cpp` with a clean `poll()`-based architecture. The rewrite targets exactly two functions: `nextEvent()` (full rewrite) and `checkDelaysForFocus()` (replace stub with real timing logic). The `loop()` dispatch switch remains unchanged. A self-pipe provides signal-safe wakeup, and `<chrono>` steady_clock manages two timer deadlines (pointer-stopped at 80ms, auto-raise at 400ms) without additional file descriptors.

The upstream wm2 auto-raise behavior is fully documented in `upstream-wm2/Events.C:177-261`. The Phase 1 codebase has all necessary member variables already declared and initialized in `include/Manager.h:115-121` and wired into the MotionNotify handler at `src/Events.cpp:92-96`. The stubs at `src/Manager.cpp:511-528` need real implementation. The `Client::focusIfAppropriate()` stub at `src/Client.cpp:668-672` also needs real implementation.

**Primary recommendation:** Replace `nextEvent()` with a poll()-based loop monitoring two fds (X11 connection + self-pipe read end), compute poll timeout as the minimum of all active chrono timer deadlines, and implement the two-phase auto-raise timer logic matching upstream behavior exactly.

## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Use the self-pipe trick for signal-safe poll() wakeup. Signal handler writes a byte to a pipe monitored by poll(). Wakes the loop immediately, no race conditions between checking `m_signalled` and calling poll(). Standard POSIX pattern.
- **D-02:** Use `<chrono>` steady_clock deadlines with poll() timeout. Track timer deadlines in C++ types, compute minimum timeout for poll(). No extra file descriptors, no platform-specific timerfd. Pure C++17.
- **D-03:** Two timers needed: pointer-stopped detection (80ms) and auto-raise countdown (400ms). Both managed through the same chrono deadline mechanism.
- **D-04:** Match original wm2 behavior exactly. When pointer enters a window: start tracking pointer movement via MotionNotify. If pointer stops for 80ms (pointer-stopped), begin 400ms auto-raise countdown. If pointer moves during countdown, reset. After 400ms of stillness, raise the window.
- **D-05:** Use the existing Phase 1 member variables (`m_focusChanging`, `m_focusPointerMoved`, `m_focusPointerNowStill`, `m_focusCandidate`, `m_focusTimestamp`) -- they are already wired into the event loop. Replace the stubs with real timing logic.
- **D-06:** Match original behavior -- no active event compression. MotionNotify handler sets flags for focus tracking (already cheap). XCheckMaskEvent in `eventEnter` already drains queued EnterNotify. Sufficient for a lightweight WM on VPS.

### Claude's Discretion
(None -- all decisions were locked.)

### Deferred Ideas (OUT OF SCOPE)
(None -- discussion stayed within phase scope.)

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| EVNT-01 | Replace select()+goto pattern with poll()-based event loop | Self-pipe trick + poll() monitoring X11 fd and pipe read fd (Section: Self-Pipe Implementation). Replace `nextEvent()` entirely. |
| EVNT-02 | Auto-raise timing preserved (400ms raise delay, 80ms pointer-stopped delay) | Two-phase timer using chrono steady_clock deadlines (Section: Timer Architecture). Match upstream `checkDelaysForFocus()` logic from `upstream-wm2/Events.C:177-206`. |
| EVNT-03 | Signal handling works correctly (SIGTERM, SIGINT, SIGHUP clean shutdown) | Self-pipe trick ensures poll() wakes immediately on signal (Section: Self-Pipe Implementation). Signal handler is async-signal-safe. |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Event loop (poll, fd monitoring) | API / Backend | -- | X11 client-side event processing; single-threaded loop in WindowManager |
| Signal handling (self-pipe) | API / Backend | OS | POSIX signal handlers write to pipe; OS delivers signals |
| Timer deadline management | API / Backend | -- | C++17 chrono computations; no kernel timer fd needed |
| Auto-raise focus logic | API / Backend | -- | WM business logic in WindowManager/Client |
| RAII fd management | API / Backend | -- | Custom RAII wrapper following x11wrap.h pattern |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| `<poll.h>` | POSIX.1-2001 | poll() for multiplexed fd waiting | Replaces select(); no fd_set size limits, cleaner API [VERIFIED: man page] |
| `<chrono>` | C++17 | steady_clock timer deadline tracking | Standard, no platform dependencies, type-safe durations [CITED: cppreference.com] |
| `<unistd.h>` pipe() | POSIX.1-2001 | Self-pipe for signal wakeup | Standard POSIX; works on all Linux [VERIFIED: man page] |
| `<signal.h>` | POSIX.1-2001 | sigaction for signal handling | Already used in Phase 1; just adding pipe write [VERIFIED: src/Manager.cpp:66-73] |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `<fcntl.h>` | POSIX | Setting O_NONBLOCK on pipe fds | Prevents write() in signal handler from blocking [VERIFIED: man page] |
| `<errno.h>` | POSIX | Error handling for poll()/read() | Distinguishing EINTR from real errors [VERIFIED: POSIX spec] |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| poll() | select() | select() has fd_set size limits (FD_SETSIZE=1024), clunkier API. We only monitor 2 fds but poll is cleaner. [ASSUMED] |
| poll() | epoll() | epoll is Linux-specific. Only 2 fds -- epoll overhead not justified for this use case. [ASSUMED] |
| self-pipe | signalfd() | signalfd is Linux-specific (not portable). Self-pipe is POSIX standard. CONTEXT.md locked self-pipe (D-01). [CITED: man signalfd] |
| chrono deadlines | timerfd | timerfd is Linux-specific. CONTEXT.md locked chrono (D-02). [CITED: man timerfd] |

**Installation:**
No additional packages needed. All APIs are in standard C library and C++17 headers, already available on the build target (Ubuntu 22.04+).

**Version verification:**
```
poll(): POSIX.1-2001, available on all Linux since kernel 2.5.44
pipe(): POSIX.1-2001, available on all Unix
<chrono>: C++17 standard, CMake already sets CMAKE_CXX_STANDARD=17
```

## Architecture Patterns

### System Architecture Diagram

```
                    POSIX Signals
                    (SIGTERM/SIGINT/SIGHUP)
                         |
                         v
               +-------------------+
               | sigHandler(int)   |  [async-signal-safe: write(2) only]
               |   write(m_pipeFd[1], "x", 1)
               +-------------------+
                         |
                         | (byte written to pipe)
                         v
    +--------+    +-------------+
    | X11    |    | Self-pipe   |
    | Server |    | read end    |
    | fd     |    | fd          |
    +--------+    +-------------+
         |              |
         v              v
    +-----------------------------+
    | poll(fds, 2, timeout_ms)   |  <--- timeout computed from chrono deadlines
    +-----------------------------+
         |
         v
    +-----------------------------+
    | nextEvent()                 |
    |                             |
    | if (pipe readable):         |
    |   drain pipe bytes          |
    |   set m_looping = false     |
    |                             |
    | if (X11 fd readable):       |
    |   XNextEvent()              |
    |   return event              |
    |                             |
    | if (timeout expired):       |
    |   checkDelaysForFocus()     |
    +-----------------------------+
         |
         v
    +-----------------------------+
    | loop() dispatch switch      |  (unchanged from Phase 1)
    +-----------------------------+
         |
         v
    +-----------------------------+
    | Event handlers              |
    |   MotionNotify: set flags   |
    |   EnterNotify: considerFocus|
    +-----------------------------+
```

### Recommended Project Structure

No new files needed. Changes confined to existing files:

```
src/Events.cpp       -- rewrite nextEvent(), no structural change to loop()
src/Manager.cpp      -- real considerFocusChange(), stopConsideringFocus(), checkDelaysForFocus()
src/Client.cpp       -- real focusIfAppropriate()
include/Manager.h    -- add self-pipe members, timer deadline members
include/x11wrap.h    -- optionally add FdGuard RAII wrapper (or put in Manager.h)
```

### Pattern 1: Self-Pipe Trick

**What:** A pipe whose write end is used in a signal handler to wake poll().
**When to use:** Any poll()/select()-based event loop that needs to handle POSIX signals cleanly.

**Example:**
```cpp
// In include/Manager.h -- add members:
int m_signalPipe[2];       // [0]=read, [1]=write; -1 when not initialized

// Or better: RAII fd wrapper
struct FdGuard {
    int fd = -1;
    explicit FdGuard(int f) : fd(f) {}
    ~FdGuard() { if (fd >= 0) close(fd); }
    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;
    FdGuard(FdGuard&& o) noexcept : fd(o.fd) { o.fd = -1; }
    int get() const { return fd; }
};
// Members:
FdGuard m_pipeRead{-1};
FdGuard m_pipeWrite{-1};

// In constructor, before sigaction:
int pipefd[2];
if (pipe(pipefd) != 0) fatal("can't create signal pipe");
// Set BOTH ends non-blocking
for (int i = 0; i < 2; ++i) {
    int flags = fcntl(pipefd[i], F_GETFL);
    fcntl(pipefd[i], F_SETFL, flags | O_NONBLOCK);
}
m_pipeRead = FdGuard(pipefd[0]);
m_pipeWrite = FdGuard(pipefd[1]);

// Signal handler (must be async-signal-safe):
void WindowManager::sigHandler(int)
{
    // m_signalled is std::atomic<int> or volatile sig_atomic_t
    // Write to pipe -- use a static/global to avoid dereferencing object ptr
    // Best approach: make pipe write fd a global/static
    m_signalled = 1;
    // write() is async-signal-safe per POSIX
    (void)write(s_pipeWriteFd, "x", 1);
}

// Static member:
int WindowManager::s_pipeWriteFd = -1;

// Set it after pipe creation:
s_pipeWriteFd = m_pipeWrite.get();
```

**Critical constraint:** The signal handler can only call async-signal-safe functions. `write()` is on the POSIX async-signal-safe list. `close()`, `read()`, `poll()` are also safe but typically not called from handlers. [VERIFIED: man 7 signal-safety]

### Pattern 2: Chrono Deadline Timer Computation

**What:** Track timer state as `steady_clock::time_point` deadlines; compute poll() timeout as ms until earliest deadline.
**When to use:** When you need multiple independent timers sharing a single poll() timeout.

**Example:**
```cpp
#include <chrono>
using namespace std::chrono;

// Timer state (members of WindowManager):
steady_clock::time_point m_pointerStoppedDeadline;  // when pointer-stopped check fires
steady_clock::time_point m_autoRaiseDeadline;        // when auto-raise fires
bool m_pointerStoppedDeadlineActive = false;
bool m_autoRaiseDeadlineActive = false;

// Computing poll() timeout:
int computePollTimeout() const {
    if (!m_focusChanging) return -1;  // block indefinitely (no timers active)

    auto now = steady_clock::now();
    steady_clock::time_point earliest = steady_clock::time_point::max();

    if (m_pointerStoppedDeadlineActive)
        earliest = min(earliest, m_pointerStoppedDeadline);
    if (m_autoRaiseDeadlineActive)
        earliest = min(earliest, m_autoRaiseDeadline);

    if (earliest == steady_clock::time_point::max())
        return -1;  // no active timers, block indefinitely

    auto ms = duration_cast<milliseconds>(earliest - now).count();
    return (ms > 0) ? static_cast<int>(ms) : 0;  // 0 = poll returns immediately
}

// Starting a timer:
void startPointerStoppedTimer() {
    m_pointerStoppedDeadline = steady_clock::now() + milliseconds(80);
    m_pointerStoppedDeadlineActive = true;
}
```

### Pattern 3: The Rewritten nextEvent()

**What:** The complete replacement for the current select()+goto pattern.
**When to use:** This is the core deliverable of Phase 2.

**Example:**
```cpp
void WindowManager::nextEvent(XEvent *e)
{
    struct pollfd fds[2];
    fds[0].fd = ConnectionNumber(display());
    fds[0].events = POLLIN;
    fds[1].fd = m_pipeRead.get();
    fds[1].events = POLLIN;

    while (m_looping) {
        // Check X11 queue first (Xlib may have buffered events)
        if (QLength(display()) > 0) {
            XNextEvent(display(), e);
            return;
        }

        // Flush any pending X output before blocking
        XFlush(display());

        int timeout = computePollTimeout();

        int r = poll(fds, 2, timeout);

        if (r < 0) {
            if (errno == EINTR) continue;  // signal interrupted, re-check
            std::perror("wm2: poll failed");
            m_looping = false;
            m_returnCode = 1;
            return;
        }

        // Signal pipe readable?
        if (fds[1].revents & POLLIN) {
            // Drain pipe (signal handler may have written multiple bytes)
            char buf[32];
            while (read(m_pipeRead.get(), buf, sizeof(buf)) > 0) { /* drain */ }
            std::fprintf(stderr, "wm2: signal caught, exiting\n");
            m_looping = false;
            m_returnCode = 0;
            return;
        }

        // Timer expired (r == 0 means timeout, no fd ready)?
        if (r == 0) {
            if (m_focusChanging) {
                checkDelaysForFocus();
            }
            continue;  // re-check X11 queue, then poll again
        }

        // X11 fd readable
        if (fds[0].revents & POLLIN) {
            XNextEvent(display(), e);
            return;
        }
    }
}
```

### Pattern 4: Two-Phase Auto-Raise Timer (Matching Upstream)

**What:** The upstream wm2 auto-raise logic has two distinct phases. This is the behavioral spec from `upstream-wm2/Events.C:177-206`.

**Phase flow:**
1. Pointer enters window -> `considerFocusChange()` called
2. `m_focusPointerMoved = false`, `m_focusPointerNowStill = false`
3. First MotionNotify arrives -> `m_focusPointerMoved = true`
4. Wait for pointer to stop (80ms with no movement, checked via select() timeout)
5. Pointer stopped detected -> if `m_focusPointerNowStill`, call `focusIfAppropriate(true)`
6. `focusIfAppropriate()` checks pointer is still over the window via XQueryPointer, then activates+raises

**In the chrono deadline model, this maps to:**

```cpp
void WindowManager::considerFocusChange(Client *c, Window w, Time ts)
{
    if (m_focusChanging) {
        stopConsideringFocus();
    }

    m_focusChanging = true;
    m_focusTimestamp = ts;
    m_focusCandidate = c;
    m_focusCandidateWindow = w;
    m_focusPointerMoved = false;
    m_focusPointerNowStill = false;

    // Start the auto-raise deadline (400ms from now)
    // The pointer-stopped timer starts after first MotionNotify
    m_autoRaiseDeadline = steady_clock::now() + milliseconds(400);
    m_autoRaiseDeadlineActive = true;
    m_pointerStoppedDeadlineActive = false;  // wait for first motion event

    m_focusCandidate->selectOnMotion(m_focusCandidateWindow, true);
}
```

**The upstream `checkDelaysForFocus()` logic (Events.C:177-206) has two branches:**

```cpp
void WindowManager::checkDelaysForFocus()
{
    Time t = timestamp(true);  // get current X server time

    if (m_focusPointerMoved) {
        // Branch 1: We've seen at least one MotionNotify
        // Check if pointer has stopped for CONFIG_POINTER_STOPPED_DELAY (80ms)
        if (t < m_focusTimestamp || t - m_focusTimestamp > 80) {
            if (m_focusPointerNowStill) {
                // Pointer stopped -> try to raise
                m_focusCandidate->focusIfAppropriate(true);
            } else {
                // Pointer moved since last check; reset "still" flag
                m_focusPointerNowStill = true;
                // Next MotionNotify will set it back to false
            }
        }
    } else {
        // Branch 2: No MotionNotify at all (window doesn't generate motion)
        // Check CONFIG_AUTO_RAISE_DELAY (400ms) directly
        if (t < m_focusTimestamp || t - m_focusTimestamp > 400) {
            m_focusCandidate->focusIfAppropriate(true);
        }
    }
}
```

**Key insight:** The upstream uses X server time (from `timestamp()`) for timer comparisons, NOT wall clock time. This matters because X server time is monotonic (millisecond ticks). The `t < m_focusTimestamp` check handles X server time wrap-around (which happens every ~49.7 days).

**However**, D-02 locks chrono steady_clock. The implementation should use steady_clock internally for poll() timeout computation, but must still match upstream timing behavior. Since the upstream's 20ms select() granularity is coarser than steady_clock, using steady_clock deadlines actually produces *more precise* timing than the original. [ASSUMED -- but high confidence: steady_clock is monotonic and sub-millisecond precision]

### Anti-Patterns to Avoid

- **Do NOT use `std::atomic` in the signal handler path without `volatile sig_atomic_t`:** While `std::atomic<int>` works in practice on x86-64, the POSIX standard guarantees async-signal-safety only for `volatile sig_atomic_t`. Use `volatile std::sig_atomic_t` for `m_signalled`. [VERIFIED: POSIX spec, C++17 standard]
- **Do NOT call any X11 function from the signal handler:** Xlib is NOT async-signal-safe. The handler writes only to the pipe. [VERIFIED: Xlib documentation]
- **Do NOT use `std::chrono::system_clock` for timer deadlines:** system_clock is not monotonic (NTP adjustments, admin time changes). Use `steady_clock`. [CITED: cppreference.com]
- **Do NOT forget to drain the pipe:** Multiple signals may arrive before poll() returns. Read all available bytes, not just one. [VERIFIED: standard self-pipe pattern]
- **Do NOT leave the MotionNotify handler conditional on `CONFIG_AUTO_RAISE`:** Phase 1 already made it unconditional (`if (m_focusChanging)` at `src/Events.cpp:92`). The upstream guarded it with `CONFIG_AUTO_RAISE`. Since wm2-born-again always has auto-raise behavior, the Phase 1 approach is correct.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| FD lifetime management | Raw int fd + manual close() | FdGuard RAII wrapper (new, following x11wrap.h pattern) | Leaked fds on exception paths, double-close bugs |
| Time arithmetic | manual ms calculations with timeval | chrono duration arithmetic | Overflow bugs, unit confusion, wrap-around |
| Signal-safe wakeup | checking flag after poll() returns | Self-pipe trick (D-01) | Race between flag check and poll() sleep |

**Key insight:** The self-pipe trick eliminates the race condition that exists in the current code. The current `nextEvent()` checks `m_signalled` before entering `waiting:`, then calls `select()`. If a signal arrives after the check but before `select()`, the original code relies on `select()` returning `EINTR` to catch it. With `poll()` and the self-pipe, the signal writes to the pipe, which makes `poll()` return immediately with the pipe fd readable. No race.

## Common Pitfalls

### Pitfall 1: Pipe Write Blocking in Signal Handler

**What goes wrong:** If the pipe buffer fills up (unlikely but possible with rapid signals), `write()` blocks inside the signal handler, deadlocking the process.
**Why it happens:** Default pipe fds are blocking.
**How to avoid:** Set O_NONBLOCK on the write end of the pipe. A failed non-blocking write to a full pipe is harmless -- the signal was already delivered to `m_signalled`. [VERIFIED: POSIX, standard self-pipe pattern]
**Warning signs:** WM stops responding to signals.

### Pitfall 2: X11 Events Buffered in Xlib

**What goes wrong:** `poll()` says X11 fd is not readable, but `QLength(display()) > 0` because Xlib already read bytes from the socket into its internal queue.
**Why it happens:** Xlib buffers events internally. After `XNextEvent()` or `XCheckMaskEvent()`, events may be in Xlib's queue but the socket is empty.
**How to avoid:** Always check `QLength(display()) > 0` before calling `poll()`. The current code already does this (line 130 of `src/Events.cpp`). Preserve this pattern. [VERIFIED: Xlib documentation, upstream-wm2/Events.C:129]
**Warning signs:** Events appear delayed or lost.

### Pitfall 3: Timer Deadline in the Past

**What goes wrong:** A timer deadline has already passed by the time `computePollTimeout()` is called. `poll()` gets timeout=0, returns immediately, loop spins.
**Why it happens:** Event processing took longer than the timer duration.
**How to avoid:** When `computePollTimeout()` returns 0, handle timers immediately (call `checkDelaysForFocus()`). Then re-check `QLength()` before calling `poll()` again. This is natural in the loop structure -- the `r == 0` case handles it. [ASSUMED]
**Warning signs:** CPU spikes to 100% in the event loop.

### Pitfall 4: stopConsideringFocus Dead Code Bug

**What goes wrong:** The upstream `stopConsideringFocus()` at `upstream-wm2/Events.C:233-241` has a dead code bug: it sets `m_focusChanging = False` on line 237, then checks `if (m_focusChanging && ...)` on line 238, which is always false. The `selectOnMotion(false)` call to remove PointerMotionMask never executes.
**Why it happens:** The order of operations is wrong. The check should happen before the assignment.
**How to avoid:** In the new implementation, call `selectOnMotion(false)` BEFORE setting `m_focusChanging = false`. [VERIFIED: upstream-wm2/Events.C:233-241, CONCERNS.md line 51-57]
**Warning signs:** PointerMotionMask accumulates on windows, causing unnecessary event traffic.

### Pitfall 5: X Server Time Wrap-Around

**What goes wrong:** X server time is `unsigned long` in milliseconds. It wraps around to 0 every ~49.7 days. Comparisons like `t - m_focusTimestamp > 400` produce wrong results near the wrap boundary.
**Why it happens:** Unsigned subtraction near 0 wraps to a huge number.
**How to avoid:** The upstream handles this with `if (t < m_focusTimestamp || t - m_focusTimestamp > delay)`. The first condition catches the wrap-around case. Preserve this pattern. [VERIFIED: upstream-wm2/Events.C:185-186, 197-198]
**Warning signs:** Auto-raise stops working after 49.7 days uptime (extremely rare in VPS use but correct to handle).

### Pitfall 6: Client Destroyed During Focus Countdown

**What goes wrong:** A window is destroyed while the auto-raise timer is counting down. `m_focusCandidate` becomes a dangling pointer.
**Why it happens:** The event loop processes a DestroyNotify that deletes the client, but `m_focusChanging` is still true.
**How to avoid:** The current code already handles this at `src/Events.cpp:229`: `if (m_focusChanging && c == m_focusCandidate) m_focusChanging = false;`. Preserve this check. [VERIFIED: src/Events.cpp:229, upstream-wm2/Events.C:454-456]
**Warning signs:** Segfault in `checkDelaysForFocus()` or `focusIfAppropriate()`.

## Code Examples

### Self-Pipe Setup (in WindowManager constructor)

```cpp
// Source: POSIX standard pattern
#include <fcntl.h>
#include <unistd.h>

// Before sigaction() calls:
int pipefd[2];
if (pipe(pipefd) != 0) {
    fatal("can't create signal pipe");
}

// Set both ends non-blocking
for (int i = 0; i < 2; ++i) {
    int flags = fcntl(pipefd[i], F_GETFL, 0);
    if (flags < 0 || fcntl(pipefd[i], F_SETFL, flags | O_NONBLOCK) < 0) {
        fatal("can't set pipe non-blocking");
    }
}

m_pipeRead = FdGuard(pipefd[0]);
m_pipeWrite = FdGuard(pipefd[1]);
s_pipeWriteFd = m_pipeWrite.get();
```

### Signal Handler (async-signal-safe)

```cpp
// Source: POSIX signal-safety documentation
// In Manager.cpp:
volatile std::sig_atomic_t WindowManager::m_signalled = 0;
int WindowManager::s_pipeWriteFd = -1;

void WindowManager::sigHandler(int)
{
    m_signalled = 1;
    // write() is async-signal-safe (POSIX Table 2-4)
    // Using static fd avoids dereferencing object pointer (not guaranteed safe)
    if (s_pipeWriteFd >= 0) {
        char c = 'x';
        (void)write(s_pipeWriteFd, &c, 1);
    }
}
```

### focusIfAppropriate Implementation

```cpp
// Source: upstream-wm2/Events.C:244-261
void Client::focusIfAppropriate(bool ifActive)
{
    if (!m_managed || !isNormal()) return;
    if (!ifActive && isActive()) return;

    Window rw, cw;
    int rx, ry, cx, cy;
    unsigned int k;

    XQueryPointer(display(), root(), &rw, &cw, &rx, &ry, &cx, &cy, &k);

    if (hasWindow(cw)) {
        activate();
        mapRaised();
        m_windowManager->stopConsideringFocus();
    }
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| select() with fd_set | poll() with struct pollfd | POSIX.1-2001 | No FD_SETSIZE limit, cleaner for 2-fd case |
| select() + goto for timer loop | poll() with computed timeout | -- (design choice) | Eliminates goto, clearer control flow |
| X server time for all timing | steady_clock for internal timers | Phase 2 (this phase) | More precise, no X round-trip for time check |
| signal() function | sigaction() with self-pipe | Phase 1 (sigaction), Phase 2 (self-pipe) | Reliable signal semantics, no race conditions |

**Deprecated/outdated:**
- `select()` for new code: poll() has no FD_SETSIZE limit, simpler API. [ASSUMED]
- `signal()`: unreliable semantics across Unix variants. Already replaced with `sigaction()` in Phase 1.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | steady_clock produces more precise timing than the upstream's 20ms select() granularity | Timer Architecture | Low -- steady_clock is guaranteed monotonic and at least millisecond precision |
| A2 | poll() with only 2 fds has no performance advantage over select() -- choice is about code clarity, not performance | Standard Stack | None -- D-01/D-02 are locked decisions |
| A3 | O_NONBLOCK on pipe write end prevents signal handler deadlock (pipe buffer won't fill with single-byte writes from rapid signals) | Pitfall 1 | Low -- Linux pipe buffer is 64KB; signals are rare events |
| A4 | `volatile std::sig_atomic_t` is sufficient for `m_signalled`; no need for `std::atomic<int>` | Anti-Patterns | Low -- POSIX guarantees sig_atomic_t is safe in signal handlers |
| A5 | The upstream's X server time comparison (`t < m_focusTimestamp || t - m_focusTimestamp > delay`) should be preserved for wrap-around safety, even though we use steady_clock for poll() timeouts | Pitfall 5 | Medium -- if `checkDelaysForFocus()` is rewritten to use steady_clock instead of X time, wrap-around handling changes. But matching upstream behavior means keeping X time comparisons. |

## Open Questions

1. **Should checkDelaysForFocus() use X server time or steady_clock?**
   - What we know: Upstream uses `timestamp(true)` (X server time) for all timer comparisons. D-02 says use steady_clock for poll() timeout. The question is what time source to use INSIDE `checkDelaysForFocus()` for the actual elapsed-time comparison.
   - What's unclear: Whether using steady_clock instead of X server time changes observable behavior. They should be equivalent for short durations (80ms, 400ms), but steady_clock avoids the X round-trip in `timestamp()`.
   - Recommendation: Use steady_clock throughout. The upstream used X time because that's what was convenient. steady_clock is strictly better (no X round-trip, no wrap-around). The `t < m_focusTimestamp` wrap-around guard becomes unnecessary with steady_clock since it's monotonic. This simplifies the code.

2. **Should the FdGuard RAII wrapper go in x11wrap.h or Manager.h?**
   - What we know: x11wrap.h is for X11 resource wrappers. Pipe fds are POSIX, not X11.
   - What's unclear: Whether a generic fd RAII wrapper belongs in x11wrap.h.
   - Recommendation: Put a minimal `FdGuard` in `include/Manager.h` or create a small `include/posixwrap.h`. It's not X11-specific. A simple struct is fine -- no need for a separate header for one 10-line type.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| poll() | Event loop multiplexing | yes | POSIX.1-2001 | -- |
| pipe() | Self-pipe signal wakeup | yes | POSIX.1-2001 | -- |
| fcntl.h / O_NONBLOCK | Non-blocking pipe | yes | POSIX | -- |
| `<chrono>` | Timer deadline tracking | yes | C++17 | -- |
| Xvfb | Test execution | yes | Phase 1 setup | Skip tests if missing |
| Catch2 v3.14.0 | Test framework | yes | Phase 1 FetchContent | -- |

**Missing dependencies with no fallback:**
- None -- all dependencies are standard POSIX/C++17.

**Missing dependencies with fallback:**
- None.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 v3.14.0 |
| Config file | CMakeLists.txt (FetchContent) |
| Quick run command | `ctest -R test_smoke --output-on-failure` |
| Full suite command | `ctest --output-on-failure` |

### Phase Requirements to Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| EVNT-01 | poll()-based loop replaces select()+goto | unit (code review) | `ctest -R test_smoke` | Partial -- smoke test exists, no event loop specific test |
| EVNT-02 | Auto-raise fires after 80ms pointer stop + 400ms delay | integration (needs Xvfb + synthetic windows) | `ctest -R test_smoke` | No -- Wave 0 |
| EVNT-03 | SIGTERM/SIGINT/SIGHUP cause clean shutdown | integration (needs signal + Xvfb) | `ctest -R test_smoke` | No -- Wave 0 |

### Sampling Rate
- **Per task commit:** `ctest -R test_smoke --output-on-failure`
- **Per wave merge:** `ctest --output-on-failure`
- **Phase gate:** Full suite green before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `tests/test_eventloop.cpp` -- covers EVNT-01 (loop starts, uses poll not select), EVNT-03 (signal shutdown)
- [ ] Auto-raise timing test in `tests/test_smoke.cpp` or new file -- covers EVNT-02 (80ms + 400ms behavior)
- [ ] Note: Event loop testing is inherently integration-level (needs X connection). Consider testing individual components (timer computation, pipe drainage) as unit tests.

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | N/A -- local X11 window manager |
| V3 Session Management | no | N/A |
| V4 Access Control | no | N/A -- single-user WM |
| V5 Input Validation | yes | Validate poll() return values, handle all error paths |
| V6 Cryptography | no | N/A |

### Known Threat Patterns for C++17 + POSIX

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Signal handler calling non-async-signal-safe functions | Tampering/Denial of Service | Strictly limit signal handler to write() + volatile assignment |
| Pipe fd leak | Information Disclosure | FdGuard RAII wrapper |
| Unbounded pipe read in event loop | Denial of Service | Non-blocking pipe, drain loop with fixed buffer size |
| Timer integer overflow | Denial of Service | chrono duration arithmetic handles overflow safely |

## Sources

### Primary (HIGH confidence)
- POSIX man pages (poll(2), pipe(2), signal-safety(7), fcntl(2)) -- verified on this system
- upstream-wm2/Events.C:177-261 -- exact behavioral specification for auto-raise timing
- upstream-wm2/Config.h:28-29 -- CONFIG_AUTO_RAISE_DELAY=400, CONFIG_POINTER_STOPPED_DELAY=80
- src/Events.cpp -- current Phase 1 implementation (select+goto pattern to replace)
- src/Manager.cpp:511-528 -- current stubs to fill
- include/Manager.h:115-121 -- focus tracking member variables

### Secondary (MEDIUM confidence)
- cppreference.com -- chrono steady_clock monotonic guarantee
- POSIX Table 2-4 (signal-safety(7)) -- async-signal-safe function list

### Tertiary (LOW confidence)
- None -- all findings verified from source code or POSIX documentation

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- poll(), pipe(), chrono are all well-understood POSIX/C++17 APIs
- Architecture: HIGH -- self-pipe pattern is standard; upstream behavioral spec is in source
- Pitfalls: HIGH -- verified against upstream code and CONCERNS.md

**Research date:** 2026-05-07
**Valid until:** 2026-06-07 (stable APIs, no expected changes)
