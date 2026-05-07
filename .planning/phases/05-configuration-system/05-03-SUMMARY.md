---
phase: 05-configuration-system
plan: 03
subsystem: config
tags: [runtime-config, integration, frame-width, spawn, x11-colors]

# Dependency graph
requires:
  - phase: 05-configuration-system
    plan: 01
    provides: Config struct with 18 settings, XDG path resolution, key=value parser, Config::load()
  - phase: 05-configuration-system
    plan: 02
    provides: CLI argument parsing via getopt_long
provides:
  - All 18 config values wired from Config struct to consumers at runtime
  - FRAME_WIDTH as runtime int (no longer constexpr)
  - Config-driven color allocation (9 color strings), timing (3 ints), frame thickness, spawn command
  - WindowManager constructor accepting const Config& ref
affects: [08-focus-policy, configuration-system, window-manager-core]

# Tech tracking
tech-stack:
  added: []
  patterns: [Config const ref injection, extern global for runtime FRAME_WIDTH, config-driven color allocation]

key-files:
  created: []
  modified:
    - src/main.cpp
    - include/Manager.h
    - src/Manager.cpp
    - include/Border.h
    - src/Border.cpp
    - src/Events.cpp

key-decisions:
  - "FRAME_WIDTH uses extern int with definition in Border.cpp, initialized from config in first Border constructor (set-once pattern)"
  - "TRANSIENT_FRAME_WIDTH kept as constexpr (fixed ratio, not user-configurable per setting inventory)"
  - "spawn() uses execl for shell mode and execlp for direct mode, both from config"

patterns-established:
  - "Config const ref injection: WindowManager stores Config as private member, exposes const accessor"
  - "Consumers access config via windowManager()->config().memberName pattern"

requirements-completed: [CONF-01, CONF-02, CONF-03, CONF-04]

# Metrics
duration: 5min
completed: 2026-05-07
---

# Phase 5 Plan 03: Config Integration Summary

**Config struct wired into all WM consumers: 9 color strings, 3 timing values, frame thickness, and spawn command now read from runtime config**

## Performance

- **Duration:** 5 min
- **Started:** 2026-05-07T22:11:00Z
- **Completed:** 2026-05-07T22:16:00Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments
- main.cpp calls Config::load(argc, argv) and passes result to WindowManager constructor
- WindowManager stores Config as const member, exposes config() accessor for all consumers
- All hardcoded color strings replaced with m_config.* references (menu colors, border colors, tab colors)
- All hardcoded timing values replaced with config (autoRaiseDelay, pointerStoppedDelay, destroyWindowDelay)
- FRAME_WIDTH changed from constexpr to runtime int initialized from config.frameThickness
- spawn() uses configurable newWindowCommand with execUsingShell support
- Build succeeds, all 88 tests pass

## Task Commits

Each task was committed atomically:

1. **Task 1: Wire Config into main.cpp and WindowManager, make FRAME_WIDTH runtime** - `fe2ecce` (feat)
2. **Task 2: Wire Config colors into Border, timing into Events, destroy-window-delay** - `83a23ea` (feat)

## Files Created/Modified
- `src/main.cpp` - Entry point now calls Config::load() and passes Config to WindowManager
- `include/Manager.h` - Added Config include, constructor takes const Config&, added m_config member and config() accessor
- `src/Manager.cpp` - Constructor accepts Config, uses m_config for menu colors, focus timing, spawn command
- `include/Border.h` - FRAME_WIDTH changed from constexpr to extern int
- `src/Border.cpp` - FRAME_WIDTH definition, config-driven color allocation and timing
- `src/Events.cpp` - MotionNotify handler uses m_config.pointerStoppedDelay

## Decisions Made
- FRAME_WIDTH uses extern int pattern: defined in Border.cpp, initialized from config in first Border constructor. This avoids passing config through the entire shape math call chain while still making it runtime-configurable.
- TRANSIENT_FRAME_WIDTH kept as constexpr since it's a fixed design ratio not exposed in the config file
- spawn() conditional: execUsingShell uses /bin/sh -c wrapper, otherwise direct execlp on the command name

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Moved FRAME_WIDTH definition from Task 2 to Task 1**
- **Found during:** Task 1 (link step failed with undefined reference to FRAME_WIDTH)
- **Issue:** Changing FRAME_WIDTH from constexpr to extern in Border.h requires a definition in exactly one .cpp file. Without it, the linker fails because inline methods in Border.h (yIndent, xIndent) reference FRAME_WIDTH.
- **Fix:** Added `int FRAME_WIDTH = 7;` definition to Border.cpp during Task 1 instead of waiting for Task 2
- **Files modified:** src/Border.cpp
- **Verification:** Build links successfully, all tests pass
- **Committed in:** fe2ecce (Task 1 commit)

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Minimal -- the FRAME_WIDTH definition was always planned for Border.cpp, just moved earlier to fix a link-time dependency between the two tasks.

## Issues Encountered
None beyond the Rule 3 deviation above.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- All 18 config values are now consumed at runtime by their respective WM components
- Focus policy booleans (clickToFocus, raiseOnFocus, autoRaise) stored in Config but not yet wired to behavior -- deferred to Phase 8 (FOCUS-02)
- Config system is complete: parsing (Plan 01), CLI (Plan 02), integration (Plan 03)
- Ready for next phase in the project roadmap

---
*Phase: 05-configuration-system*
*Completed: 2026-05-07*

## Self-Check: PASSED

- FOUND: src/main.cpp
- FOUND: include/Manager.h
- FOUND: src/Manager.cpp
- FOUND: include/Border.h
- FOUND: src/Border.cpp
- FOUND: src/Events.cpp
- FOUND: fe2ecce (Task 1 commit)
- FOUND: 83a23ea (Task 2 commit)
