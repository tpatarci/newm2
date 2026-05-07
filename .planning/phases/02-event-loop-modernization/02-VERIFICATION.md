---
phase: 02-event-loop-modernization
verified: 2026-05-07T05:10:00Z
status: passed
score: 10/10 must-haves verified
overrides_applied: 0
re_verification: false
---

# Phase 2: Event Loop Modernization Verification Report

**Phase Goal:** The window manager's event loop uses a clean poll()-based architecture with correct signal handling, replacing the original select()+goto pattern.
**Verified:** 2026-05-07T05:10:00Z
**Status:** PASSED
**Re-verification:** No -- initial verification

## Goal Achievement

### Observable Truths

| #   | Truth                                                                                           | Status     | Evidence                                                                                     |
| --- | ----------------------------------------------------------------------------------------------- | ---------- | -------------------------------------------------------------------------------------------- |
| 1   | The WM processes X11 events via poll() -- no select() or goto in the event loop                 | VERIFIED   | Events.cpp: `grep -c "select(" == 0`, `grep -c "goto " == 0`, `poll()` at line 150           |
| 2   | Windows auto-raise after 400ms hover; pointer-stopped detection at 80ms granularity             | VERIFIED   | Manager.cpp:554 uses `milliseconds(400)`, Manager.cpp:598 uses `milliseconds(80)`            |
| 3   | Sending SIGTERM/SIGINT/SIGHUP causes clean shutdown via self-pipe wakeup                         | VERIFIED   | sigHandler writes to s_pipeWriteFd (Manager.cpp:210), nextEvent drains pipe (Events.cpp:161) |
| 4   | No file descriptors leaked on exit -- FdGuard RAII wrapper                                       | VERIFIED   | FdGuard struct in Manager.h:15-28, destructor calls ::close(fd), self-pipe members use FdGuard|
| 5   | All existing event handling preserved -- only nextEvent() rewritten                              | VERIFIED   | loop() dispatch (Events.cpp:11-126) unchanged; all case handlers intact                      |
| 6   | Signal handler is async-signal-safe (only write() + volatile assignment)                         | VERIFIED   | sigHandler (Manager.cpp:203-212): m_signalled=1 + write(s_pipeWriteFd, &c, 1) only           |
| 7   | Two-phase auto-raise: 80ms pointer-stopped starts countdown, 400ms triggers raise               | VERIFIED   | checkDelaysForFocus() Branch 1 (Manager.cpp:583-599), Branch 2 (Manager.cpp:601-608)        |
| 8   | Moving during 400ms countdown resets the timer                                                   | VERIFIED   | MotionNotify handler resets m_pointerStoppedDeadline (Manager.cpp:98-101)                    |
| 9   | stopConsideringFocus() bug fixed: selectOnMotion(false) before m_focusChanging=false             | VERIFIED   | Manager.cpp:567-568 calls selectOnMotion(false) BEFORE line 571 sets m_focusChanging=false   |
| 10  | Client::focusIfAppropriate() verifies pointer via XQueryPointer before raising                   | VERIFIED   | Client.cpp:677 XQueryPointer call, then activate()/mapRaised()/stopConsideringFocus()         |

**Score:** 10/10 truths verified

### Required Artifacts

| Artifact                    | Expected                                                      | Status    | Details                                                                     |
| --------------------------- | ------------------------------------------------------------- | --------- | --------------------------------------------------------------------------- |
| `src/Events.cpp`            | poll()-based nextEvent() with self-pipe wakeup, no select/goto | VERIFIED  | 186 lines, poll() at line 150, computePollTimeout() at line 148             |
| `include/Manager.h`         | FdGuard RAII, self-pipe members, volatile sig_atomic_t, chrono | VERIFIED  | FdGuard struct (lines 15-28), chrono members (lines 147-150), sig_atomic_t  |
| `src/Manager.cpp`           | Self-pipe creation, signal handler, focus timing functions     | VERIFIED  | pipe() at line 74, O_NONBLOCK at 79, considerFocusChange/checkDelaysForFocus|
| `src/Client.cpp`            | Real focusIfAppropriate() with XQueryPointer                  | VERIFIED  | XQueryPointer at line 677, no stub (void)ifActive cast                      |
| `tests/test_eventloop.cpp`  | 5 FdGuard/self-pipe tests                                     | VERIFIED  | 5 TEST_CASE entries, all pass                                               |
| `tests/test_autoraise.cpp`  | 6 chrono timer tests                                          | VERIFIED  | 6 TEST_CASE entries, all pass                                               |
| `CMakeLists.txt`            | test_eventloop + test_autoraise targets                       | VERIFIED  | Lines 98-113, both targets registered                                       |

### Key Link Verification

| From                             | To                                | Via                                    | Status  | Details                                       |
| -------------------------------- | --------------------------------- | -------------------------------------- | ------- | --------------------------------------------- |
| sigHandler()                     | self-pipe write end               | write(s_pipeWriteFd, &c, 1)            | WIRED   | Manager.cpp:210                               |
| nextEvent()                      | self-pipe read end                | poll(fds, 2) monitoring m_pipeRead     | WIRED   | Events.cpp:134,150,161                        |
| MotionNotify handler             | m_pointerStoppedDeadline          | steady_clock::now() + milliseconds(80) | WIRED   | Events.cpp:98-101                             |
| checkDelaysForFocus()            | focusIfAppropriate()              | m_focusCandidate->focusIfAppropriate() | WIRED   | Manager.cpp:591,607                           |
| focusIfAppropriate()             | stopConsideringFocus()            | m_windowManager->stopConsideringFocus()| WIRED   | Client.cpp:682                                |
| eventEnter()                     | considerFocusChange()             | windowManager()->considerFocusChange() | WIRED   | Client.cpp:1117                               |
| considerFocusChange()            | selectOnMotion(true)              | m_focusCandidate->selectOnMotion(w, t) | WIRED   | Manager.cpp:559                               |
| stopConsideringFocus()           | selectOnMotion(false)             | m_focusCandidate->selectOnMotion(w, f) | WIRED   | Manager.cpp:568                               |

### Data-Flow Trace (Level 4)

| Artifact               | Data Variable                | Source                    | Produces Real Data | Status   |
| ---------------------- | ---------------------------- | ------------------------- | ------------------ | -------- |
| nextEvent()            | pollfd fds[2]                | ConnectionNumber + m_pipeRead | Yes            | FLOWING  |
| computePollTimeout()   | earliest deadline            | m_pointerStoppedDeadline, m_autoRaiseDeadline | Yes | FLOWING |
| checkDelaysForFocus()  | now vs deadline              | steady_clock::now()       | Yes                | FLOWING  |
| focusIfAppropriate()   | cw (child window)            | XQueryPointer return      | Yes                | FLOWING  |

### Behavioral Spot-Checks

| Behavior                        | Command                                                                     | Result | Status |
| ------------------------------- | --------------------------------------------------------------------------- | ------ | ------ |
| Build succeeds                  | `cmake -B build && cmake --build build`                                     | 0 exit | PASS   |
| All 36 tests pass               | `ctest --output-on-failure`                                                 | 36/36  | PASS   |
| No select() in Events.cpp       | `grep -c "select(" src/Events.cpp`                                          | 0      | PASS   |
| No goto in Events.cpp           | `grep -c "goto " src/Events.cpp`                                            | 0      | PASS   |
| poll() present in Events.cpp    | `grep -c "poll(" src/Events.cpp`                                            | 1      | PASS   |
| XQueryPointer in Client.cpp     | `grep -c "XQueryPointer" src/Client.cpp`                                    | 1      | PASS   |
| computePollTimeout wired        | `grep -c "computePollTimeout" src/Events.cpp`                               | 1      | PASS   |
| No hardcoded 20ms timeout       | `grep "timeout = 20" src/Events.cpp`                                        | empty  | PASS   |

### Requirements Coverage

| Requirement | Source Plan | Description                                                        | Status    | Evidence                                                       |
| ----------- | ---------- | ------------------------------------------------------------------ | --------- | -------------------------------------------------------------- |
| EVNT-01     | 02-01      | Replace select()+goto pattern with poll()-based event loop         | SATISFIED | Events.cpp: poll() at line 150, 0 select(), 0 goto            |
| EVNT-02     | 02-02      | Auto-raise timing preserved (400ms raise, 80ms pointer-stopped)    | SATISFIED | Manager.cpp: steady_clock deadlines with 400ms and 80ms values |
| EVNT-03     | 02-01      | Signal handling works correctly (SIGTERM, SIGINT, SIGHUP clean)    | SATISFIED | Self-pipe trick, FdGuard RAII, async-signal-safe handler       |

No orphaned requirements found. All three EVNT requirements are claimed by plans and verified.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
| ---- | ---- | ------- | -------- | ------ |
| (none) | -- | -- | -- | No anti-patterns detected in phase files |

**Note:** `select(0, nullptr, nullptr, nullptr, &sleepval)` calls remain in `Client.cpp` (lines 730, 833) and `Border.cpp` (line 785). These are 50ms sleep calls in interactive move/resize/drag loops, NOT in the event loop. They use `select()` with nfds=0 as a portable sub-second sleep, which is a common POSIX idiom. The roadmap criterion specifically targets "select() or goto statements in the event loop" -- these are not in the event loop.

### Human Verification Required

None. All truths are programmatically verifiable from source code, and all build/test checks pass.

### Gaps Summary

No gaps found. All 10 observable truths verified, all 7 artifacts present and substantive, all 8 key links wired, all 3 requirements satisfied, all 36 tests pass, and the build succeeds cleanly.

---

_Verified: 2026-05-07T05:10:00Z_
_Verifier: Claude (gsd-verifier)_
