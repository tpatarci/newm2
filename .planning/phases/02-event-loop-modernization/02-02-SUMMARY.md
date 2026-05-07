---
phase: 02-event-loop-modernization
plan: 02
subsystem: infra
tags: [chrono, auto-raise, focus-tracking, timer, steady_clock, poll]

# Dependency graph
requires:
  - phase: 02-event-loop-modernization
    provides: "Plan 01: poll()-based event loop, self-pipe, focus tracking member variables, MotionNotify handler"
provides:
  - "Two-phase auto-raise timer: 80ms pointer-stopped + 400ms auto-raise with chrono deadlines"
  - "computePollTimeout() computing minimum of active timer deadlines for poll() timeout"
  - "Fixed stopConsideringFocus() bug (selectOnMotion before m_focusChanging=false)"
  - "Client::focusIfAppropriate() with XQueryPointer pointer verification"
  - "eventEnter() wired to auto-raise timing instead of immediate activation"
  - "test_autoraise unit test suite with 6 chrono arithmetic tests"
affects: [focus-tracking, config, border]

# Tech tracking
tech-stack:
  added: [chrono steady_clock]
  patterns: [chrono deadline timer, two-phase auto-raise, computePollTimeout minimum deadline]

key-files:
  created:
    - tests/test_autoraise.cpp
  modified:
    - include/Manager.h
    - src/Manager.cpp
    - src/Client.cpp
    - src/Events.cpp
    - CMakeLists.txt

key-decisions:
  - "stopConsideringFocus() bug fixed: selectOnMotion(false) called before m_focusChanging=false (upstream dead code)"
  - "checkDelaysForFocus() uses steady_clock internally instead of X server time (per D-02)"
  - "MotionNotify handler starts 80ms pointer-stopped deadline on first motion event"
  - "eventEnter() changed from immediate activate() to considerFocusChange() for auto-raise timing"

patterns-established:
  - "Chrono deadline timer: steady_clock::time_point deadlines with active flags, computePollTimeout() for minimum"
  - "Two-phase auto-raise: Branch 1 (motion seen) uses 80ms pointer-stopped; Branch 2 (no motion) uses 400ms direct"

requirements-completed: [EVNT-02]

# Metrics
duration: 6min
completed: 2026-05-07
---

# Phase 2 Plan 02: Two-Phase Auto-Raise Timer Summary

**Two-phase auto-raise focus system with chrono-based timing (80ms pointer-stopped + 400ms auto-raise), upstream dead code bug fix, and XQueryPointer verification before window raise**

## Performance

- **Duration:** 6 min
- **Started:** 2026-05-07T04:52:42Z
- **Completed:** 2026-05-07T04:58:42Z
- **Tasks:** 3
- **Files modified:** 6

## Accomplishments
- Two-phase auto-raise timer fully implemented matching upstream wm2 behavior with steady_clock deadlines
- Fixed upstream stopConsideringFocus() dead code bug where selectOnMotion(false) never executed
- eventEnter() now uses considerFocusChange() for proper auto-raise timing instead of immediate activation
- Client::focusIfAppropriate() verifies pointer position via XQueryPointer before raising window
- Poll timeout computed dynamically from active timer deadlines instead of fixed 20ms

## Task Commits

Each task was committed atomically:

1. **Task 1: Add chrono timer deadline members to Manager.h, create timer tests** - `d77ba41` (test)
2. **Task 2: Implement considerFocusChange(), checkDelaysForFocus(), computePollTimeout()** - `1fa366a` (feat)
3. **Task 3: Implement stopConsideringFocus() with bug fix, implement Client::focusIfAppropriate()** - `423d41a` (feat)

## Files Created/Modified
- `include/Manager.h` - Added chrono include, timer deadline members (m_pointerStoppedDeadline, m_autoRaiseDeadline), active flags, computePollTimeout() declaration
- `src/Manager.cpp` - Replaced three stubs with real implementations: considerFocusChange(), stopConsideringFocus() (bug fixed), checkDelaysForFocus(), computePollTimeout(). Added timer member initializers.
- `src/Events.cpp` - Replaced hardcoded 20ms poll timeout with computePollTimeout(). Updated MotionNotify handler to start 80ms pointer-stopped deadline on first motion.
- `src/Client.cpp` - Replaced focusIfAppropriate() stub with XQueryPointer-based implementation. Changed eventEnter() from immediate activate() to considerFocusChange().
- `tests/test_autoraise.cpp` - New file: 6 Catch2 tests for chrono timer arithmetic
- `CMakeLists.txt` - Added test_autoraise target

## Decisions Made

- **stopConsideringFocus() bug fix:** Upstream sets m_focusChanging=false before checking it, making the selectOnMotion(false) call dead code. Fixed by calling selectOnMotion before clearing the flag. This prevents PointerMotionMask from accumulating on windows.
- **steady_clock instead of X server time:** checkDelaysForFocus() uses std::chrono::steady_clock internally for timer comparisons (per locked decision D-02). This eliminates X server time wrap-around issues and avoids an X round-trip for time queries.
- **MotionNotify starts pointer-stopped timer:** The 80ms pointer-stopped deadline is started on the first MotionNotify event (in Events.cpp), not in considerFocusChange(). This matches upstream behavior where motion tracking begins only after the first motion event arrives.
- **eventEnter() triggers auto-raise timing:** Changed from immediate activate() to considerFocusChange(). This is the correct wm2 behavior -- the original always uses auto-raise timing, never instant activation on pointer enter.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None - all tasks compiled and tested cleanly on first attempt.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Auto-raise focus system complete, Phase 02 fully implemented
- All 36 tests pass (6 new autoraise timer tests + 30 existing)
- Event loop uses dynamic poll() timeout based on active timer deadlines
- Ready for Phase 03 (likely border/Xft rendering or EWMH compliance)

---
*Phase: 02-event-loop-modernization*
*Completed: 2026-05-07*

## Self-Check: PASSED

- tests/test_autoraise.cpp: FOUND
- include/Manager.h: FOUND
- src/Manager.cpp: FOUND
- src/Events.cpp: FOUND
- src/Client.cpp: FOUND
- CMakeLists.txt: FOUND
- .planning/phases/02-event-loop-modernization/02-02-SUMMARY.md: FOUND
- Commit d77ba41 (Task 1): FOUND
- Commit 1fa366a (Task 2): FOUND
- Commit 423d41a (Task 3): FOUND
