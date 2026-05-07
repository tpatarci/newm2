---
phase: 03-client-lifecycle-with-raii
plan: 01
subsystem: client-lifecycle
tags: [raii, x11, statemachine, xgrabserver, unique_ptr]

# Dependency graph
requires:
  - phase: 01-build-infrastructure-raii-foundation
    provides: RAII wrapper patterns in x11wrap.h, CMake build system
  - phase: 02-event-loop-modernization
    provides: poll()-based event loop, signal handling
provides:
  - ServerGrab RAII class for ICCCM-compliant atomic reparenting
  - ClientState enum class with validated transitions
  - Vector-based colormap arrays replacing malloc/free
  - Destructor-only cleanup path (release() method removed)
affects: [03-client-lifecycle-with-raii, 04-border-xft-font-rendering]

# Tech tracking
tech-stack:
  added: []
  patterns: [RAII scope guard for XGrabServer, enum class state machine with transition validation, std::vector for X11 resource arrays]

key-files:
  created: []
  modified:
    - include/x11wrap.h
    - include/Client.h
    - src/Client.cpp
    - src/Border.cpp
    - src/Events.cpp
    - src/Manager.cpp

key-decisions:
  - "ClientState enum values match X11 constants (Withdrawn=0, Normal=1, Iconic=3) eliminating conversion at X11 boundary"
  - "State transitions validated but applied anyway with warning -- matches upstream permissive behavior per D-11"
  - "ServerGrab is a non-movable scope guard -- no move semantics needed for short-lived server grab"
  - "Destructor calls unreparent() for non-withdrawn clients, replacing separate release() method per D-03"
  - "Colormaps copy from X11-allocated memory to vector then XFree -- prevents use-after-free per Pitfall 4"

patterns-established:
  - "RAII scope guard pattern: ServerGrab follows UniqueCursor/FdGuard pattern in x11wrap.h"
  - "Enum class state machine: type-safe states with validation function, int overload for X11 boundary"
  - "X11 property copy-then-free: always copy from X11-allocated memory into owned containers before XFree"

requirements-completed: [CLNT-01, CLNT-05, CLNT-03]

# Metrics
duration: 15min
completed: 2026-05-07
---

# Phase 03 Plan 01: ServerGrab, ClientState Enum, Vector Colormaps Summary

**ServerGrab RAII wraps XReparentWindow, ClientState enum with transition validation replaces raw int state, vector colormaps replace malloc/free arrays, destructor becomes sole cleanup path**

## Performance

- **Duration:** 15 min
- **Started:** 2026-05-07T14:41:08Z
- **Completed:** 2026-05-07T14:56:32Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments
- ServerGrab RAII class in x11wrap.h provides exception-safe XGrabServer/XUngrabServer around Border::reparent() (CLNT-01)
- ClientState enum class (Withdrawn=0, Normal=1, Iconic=3) with isValidTransition() validation catches logic bugs while remaining permissive (CLNT-05)
- Colormap arrays use std::vector with XFree-after-copy pattern, eliminating malloc/free and use-after-free risk (CLNT-03)
- release() method removed; destructor is sole cleanup path (unreparent, border reset, deactivate) per D-03
- All 36 existing tests pass with the changes

## Task Commits

Each task was committed atomically:

1. **Task 1: Add ServerGrab RAII class to x11wrap.h, add ClientState enum to Client.h, update state queries and colormap members** - `86b3127` (feat)
2. **Task 2: Implement ClientState validation, vector colormaps, ServerGrab reparent, destructor-only cleanup** - `d30a53d` (feat)

## Files Created/Modified
- `include/x11wrap.h` - ServerGrab RAII class added after UniqueColormap
- `include/Client.h` - ClientState enum, vector colormap members, setState overloads, isValidTransition declaration; release() removed
- `src/Client.cpp` - isValidTransition, setState validation, destructor-only cleanup, vector getColormaps, all ClientState enum usage
- `src/Border.cpp` - ServerGrab wrapping XReparentWindow in reparent()
- `src/Events.cpp` - `delete c` replaces `c->release()` in eventDestroy
- `src/Manager.cpp` - `delete c` replaces `c->release()` in release()

## Decisions Made
- Kept local `int state` variable in manage() for X11 boundary reads (NormalState/IconicState comparisons against hints->initial_state) -- these are correct X11 boundary handling, not internal state references
- isValidTransition returns true for same-state transitions (Normal->Normal etc.) to avoid noise; only truly invalid paths generate warnings

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Plan 02 (unique_ptr ownership, O(1) hash map lookup) can proceed directly -- ClientState enum and ServerGrab are in place
- The `delete c` calls in Events.cpp and Manager.cpp are temporary raw-pointer cleanup; Plan 02 replaces these with unique_ptr erase
- The manage() method still uses raw int for X11 state reads -- this is correct and intentional (X11 boundary)

---
*Phase: 03-client-lifecycle-with-raii*
*Completed: 2026-05-07*
