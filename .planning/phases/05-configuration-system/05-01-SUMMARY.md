---
phase: 05-configuration-system
plan: 01
subsystem: config
tags: [xdg, key-value-parser, runtime-config, getopt]

# Dependency graph
requires:
  - phase: 01-build-infrastructure-raii-foundation
    provides: CMake build system, C++17 standard, Catch2 test framework
provides:
  - Config struct with 18 settings and built-in defaults
  - XDG-compliant config file path resolution (xdgConfigHome, xdgConfigDirs)
  - Key=value config file parser (applyFile, applyKeyValue)
  - Config::load() precedence chain (system -> user -> CLI stub)
  - Unit test suite with 19 test cases (89 assertions)
affects: [05-02, 05-03, configuration-system]

# Tech tracking
tech-stack:
  added: [getopt_long (glibc), std::ifstream line-by-line parsing]
  patterns: [precedence-chain config loading, XDG Base Directory compliance, key=value hand-rolled parser]

key-files:
  created:
    - include/Config.h
    - src/Config.cpp
    - tests/test_config.cpp
  modified:
    - CMakeLists.txt

key-decisions:
  - "Config key names use kebab-case (e.g., tab-foreground, auto-raise-delay) matching CLI flag style"
  - "xdgConfigHome() and xdgConfigDirs() exposed as free functions for testability"
  - "applyCliArgs() is a stub -- Plan 02 implements getopt_long CLI parsing"
  - "setenv/unsetenv used without std:: prefix (POSIX functions, not in C++ std namespace)"

patterns-established:
  - "Config struct as plain data with static load() factory method"
  - "Precedence chain: defaults -> system config dirs -> user config -> CLI overrides"
  - "Defensive parsing: line length limit (4096), value length limit (256), int clamping, try/catch around stoi"

requirements-completed: [CONF-01, CONF-02, CONF-03]

# Metrics
duration: 7min
completed: 2026-05-07
---

# Phase 5 Plan 01: Config Struct and Key=Value Parser Summary

**Runtime config system with XDG path resolution, key=value file parser, and 18 configurable settings**

## Performance

- **Duration:** 7 min
- **Started:** 2026-05-07T21:53:46Z
- **Completed:** 2026-05-07T22:00:46Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments
- Config struct with all 18 settings initialized to upstream defaults (replacing compile-time #define macros)
- XDG-compliant config file path resolution supporting both XDG_CONFIG_HOME and colon-separated XDG_CONFIG_DIRS
- Robust key=value parser handling comments, blank lines, malformed lines, line/value length limits
- Full test coverage: 19 test cases, 89 assertions, all passing without X11 display

## Task Commits

Each task was committed atomically:

1. **Task 1 (RED): Failing tests for Config struct** - `c22c148` (test)
2. **Task 1 (GREEN): Config struct implementation** - `fe3c522` (feat)
3. **Task 2: CMakeLists.txt build integration** - Committed as part of `c22c148` (CMake changes needed for RED phase build)

_Note: Task 1 used TDD with RED/GREEN commits. Task 2 CMakeLists.txt changes were committed with the RED phase since tests needed to compile._

## Files Created/Modified
- `include/Config.h` - Config struct definition with 18 member variables and parsing API
- `src/Config.cpp` - XDG path resolution, key=value file parser, applyKeyValue with clamping/validation
- `tests/test_config.cpp` - 19 Catch2 test cases covering defaults, parsing, XDG paths, precedence, all settings, edge cases
- `CMakeLists.txt` - Added src/Config.cpp to WM binary, added test_config target

## Decisions Made
- Config key names use kebab-case (tab-foreground, auto-raise-delay) for consistency with CLI flags
- xdgConfigHome() and xdgConfigDirs() exposed as free functions (not static members) so tests can call them directly
- Boolean parsing accepts "true"/"false", "1"/"0", and is case-insensitive
- Integer clamping prevents extreme values: delays 1-60000ms, frame thickness 1-50px
- Lines > 4096 chars skipped entirely; values > 256 chars rejected -- prevents memory exhaustion from malformed config
- Unknown config keys produce a warning but do not abort parsing

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
- `std::setenv` / `std::unsetenv` compilation error -- these are POSIX functions not in the C++ `std` namespace. Fixed by removing `std::` prefix in test file.
- `_GNU_SOURCE` redefined warning in Config.cpp -- CMake already defines it via compile flags. Fixed with `#ifndef _GNU_SOURCE` guard.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Config struct ready for consumption by WindowManager, Border, Buttons (Plan 03)
- CLI argument parsing (applyCliArgs) stub ready for implementation (Plan 02)
- All 18 settings have validated defaults matching upstream Config.h values

---
*Phase: 05-configuration-system*
*Completed: 2026-05-07*

## Self-Check: PASSED

- FOUND: include/Config.h
- FOUND: src/Config.cpp
- FOUND: tests/test_config.cpp
- FOUND: c22c148 (test commit)
- FOUND: fe3c522 (feat commit)
- FOUND: SUMMARY.md
