---
phase: 02-event-loop-modernization
plan: 01
subsystem: infra
tags: [poll, self-pipe, signal-handling, raii, event-loop]

# Dependency graph
requires:
  - phase: 01-scaffolding
    provides: "Manager.h class structure, x11wrap.h RAII patterns, CMake build system"
provides:
  - "poll()-based nextEvent() replacing select()+goto event loop"
  - "FdGuard RAII wrapper for POSIX file descriptors"
  - "Self-pipe trick for signal-safe poll() wakeup"
  - "Async-signal-safe sigHandler writing to pipe instead of just flagging"
  - "test_eventloop unit test suite with 5 FdGuard tests"
affects: [02-event-loop-modernization, focus-tracking, config]

# Tech tracking
tech-stack:
  added: [poll.h, fcntl.h, csignal]
  patterns: [self-pipe trick, FdGuard RAII, volatile sig_atomic_t for signal safety]

key-files:
  created:
    - tests/test_eventloop.cpp
  modified:
    - include/Manager.h
    - src/Manager.cpp
    - src/Events.cpp
    - CMakeLists.txt

key-decisions:
  - "FdGuard as standalone struct (not class) following x11wrap.h RAII pattern"
  - "s_pipeWriteFd as static int for async-signal-safety (cannot dereference this in handler)"
  - "O_NONBLOCK on both pipe ends to prevent handler deadlock if pipe buffer fills"
  - "m_signalled changed to volatile sig_atomic_t for standards-compliant signal safety"

patterns-established:
  - "Self-pipe trick: signal handler writes byte to pipe, event loop drains pipe on POLLIN"
  - "FdGuard RAII: move-only wrapper for POSIX fds, automatic close() on destruction"
  - "poll() with 2 fds: X11 connection + self-pipe read end, replaces select()"

requirements-completed: [EVNT-01, EVNT-03]

# Metrics
duration: 8min
completed: 2026-05-07
---

# Phase 2 Plan 01: Event Loop Modernization Summary

**Replaced select()+goto event loop with poll()+self-pipe, added FdGuard RAII wrapper for POSIX file descriptors, and made signal handling async-signal-safe**

## Performance

- **Duration:** 8 min
- **Started:** 2026-05-07T04:41:57Z
- **Completed:** 2026-05-07T04:49:46Z
- **Tasks:** 3
- **Files modified:** 5

## Accomplishments
- Eliminated select() and goto from Events.cpp, replacing with clean poll()-based loop
- Self-pipe trick eliminates race condition between m_signalled check and entering poll()
- FdGuard RAII wrapper ensures no file descriptor leaks on WM exit
- Signal handler is fully async-signal-safe (only write() + volatile assignment)
- All 30 tests pass (5 new FdGuard/self-pipe tests + 25 existing)

## Task Commits

Each task was committed atomically:

1. **Task 1: Add FdGuard RAII wrapper and self-pipe members to Manager.h, create test stubs** - `519b356` (feat)
2. **Task 2: Create self-pipe in constructor, update signal handler to write to pipe** - `0ff0408` (feat)
3. **Task 3: Rewrite nextEvent() with poll()-based loop** - `92f3cfd` (feat)

## Files Created/Modified
- `include/Manager.h` - Added FdGuard struct, self-pipe members (m_pipeRead, m_pipeWrite, s_pipeWriteFd), changed m_signalled to volatile sig_atomic_t
- `src/Manager.cpp` - Self-pipe creation in constructor with O_NONBLOCK, updated sigHandler to write to pipe
- `src/Events.cpp` - Replaced select()+goto nextEvent() with poll()+self-pipe implementation
- `tests/test_eventloop.cpp` - New file: 5 Catch2 tests for FdGuard RAII and self-pipe
- `CMakeLists.txt` - Added test_eventloop target

## Decisions Made

- **FdGuard as struct, not class:** Follows x11wrap.h RAII pattern (public members, no getter overhead). Placed before WindowManager class in Manager.h for independence.
- **s_pipeWriteFd as plain static int:** Signal handlers cannot safely dereference object pointers (undefined behavior). The static variable is accessible without this-pointer dereference.
- **O_NONBLOCK on write end:** Prevents handler deadlock if pipe buffer fills from rapid signals. Standard self-pipe pitfall mitigation.
- **volatile sig_atomic_t for m_signalled:** Standards-compliant type for variables accessed from signal handlers. Previous `int` was technically undefined behavior.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None - all tasks compiled and tested cleanly on first attempt.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Event loop foundation complete, ready for Plan 02 (focus tracking with chrono deadlines)
- checkDelaysForFocus() stub is called but does nothing -- Plan 02 will implement auto-raise timer logic
- poll() timeout currently fixed at 20ms when m_focusChanging -- Plan 02 may refine to chrono-based deadlines

---
*Phase: 02-event-loop-modernization*
*Completed: 2026-05-07*

## Self-Check: PASSED

- tests/test_eventloop.cpp: FOUND
- .planning/phases/02-event-loop-modernization/02-01-SUMMARY.md: FOUND
- Commit 519b356 (Task 1): FOUND
- Commit 0ff0408 (Task 2): FOUND
- Commit 92f3cfd (Task 3): FOUND
