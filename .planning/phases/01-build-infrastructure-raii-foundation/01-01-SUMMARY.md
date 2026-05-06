---
phase: 01-build-infrastructure-raii-foundation
plan: 01
subsystem: infra, testing
tags: [cmake, pkg-config, x11, catch2, xvfb, raii, c++17]

# Dependency graph
requires: []
provides:
  - CMakeLists.txt with pkg-config X11 discovery and Catch2 FetchContent
  - RAII wrappers for all 7 X11 resource types (Display, GC, FontStruct, Cursor, Font, Pixmap, Colormap)
  - Catch2 test harness with Xvfb fixtures for headless testing
  - Cursor bitmap data from upstream
  - Source stubs for WM binary (filled in Plan 02)
affects: [02-event-loop-modernization, 03-client-lifecycle, 04-border-xft, all subsequent phases]

# Tech tracking
tech-stack:
  added: [cmake-3.28, pkg-config, libx11-dev, libxext-dev, xvfb, catch2-v3.14.0]
  patterns: [unique_ptr + custom deleters for pointer-type X11 resources, thin RAII classes for XID-type resources, CTest fixtures for Xvfb lifecycle, FetchContent for test framework]

key-files:
  created:
    - CMakeLists.txt
    - include/x11wrap.h
    - include/Cursors.h
    - tests/test_raii.cpp
    - tests/test_smoke.cpp
    - src/main.cpp
    - src/Manager.cpp
    - src/Client.cpp
    - src/Border.cpp
    - src/Buttons.cpp
    - src/Events.cpp
  modified: []

key-decisions:
  - "test_raii linked against X11 because inline RAII destructors call XFree* functions even in unit tests"
  - "catch_discover_tests PROPERTIES used instead of post-discovery set_tests_properties for Xvfb fixtures (latter evaluated before discovery)"
  - "Added #include X11/cursorfont.h to test_smoke.cpp for XC_left_ptr cursor constant"

patterns-established:
  - "RAII pattern: unique_ptr + custom deleter for pointer types (Display*, GC, XFontStruct*)"
  - "RAII pattern: thin move-only class for XID types (Cursor, Font, Pixmap, Colormap)"
  - "Test pattern: CTest fixtures start/stop Xvfb around smoke tests automatically"
  - "Build pattern: pkg_check_modules IMPORTED_TARGET for X11, FetchContent for Catch2"
  - "Third-party code: Rotated.C compiled with -w flag for warning suppression"

requirements-completed: [BLD-01, BLD-03, BLD-05, TEST-01, TEST-02, TEST-04]

# Metrics
duration: 14min
completed: 2026-05-06
---

# Phase 1 Plan 01: Build Infrastructure + RAII Foundation Summary

**CMake build system with pkg-config X11 discovery, 7 RAII wrapper types for X11 resources, Catch2 test harness on Xvfb (25 tests passing)**

## Performance

- **Duration:** 14 min
- **Started:** 2026-05-06T20:17:10Z
- **Completed:** 2026-05-06T20:31:20Z
- **Tasks:** 3
- **Files modified:** 12

## Accomplishments
- CMake build system discovers X11 via pkg-config with no hardcoded paths -- builds from clean checkout
- All 7 X11 resource types wrapped in RAII: DisplayPtr, GCPtr, FontStructPtr (unique_ptr), UniqueCursor, UniqueFont, UniquePixmap, UniqueColormap (thin classes)
- 25 tests pass on Xvfb: 16 RAII unit tests (null safety, move-only traits, ownership transfer) + 7 smoke tests (real X11 resource lifecycle) + 2 fixture tests

## Task Commits

Each task was committed atomically:

1. **Task 1: Create CMakeLists.txt** - `dbec005` (feat)
2. **Task 2: Create RAII wrappers + Cursors.h** - `3b10c5f` (feat)
3. **Task 3: Create tests + verify suite** - `faae8ff` (feat)

## Files Created/Modified
- `CMakeLists.txt` - Build system with pkg-config X11, Catch2 FetchContent, Xvfb fixtures
- `include/x11wrap.h` - RAII wrappers for all 7 X11 resource types in namespace x11
- `include/Cursors.h` - Cursor bitmap data copied verbatim from upstream
- `tests/test_raii.cpp` - 16 unit tests for RAII null safety and type traits
- `tests/test_smoke.cpp` - 7 integration tests using real Xvfb display
- `src/main.cpp` - Stub with empty main() (Plan 02 implements)
- `src/Manager.cpp` - Stub (Plan 02 implements)
- `src/Client.cpp` - Stub (Plan 02 implements)
- `src/Border.cpp` - Stub (Plan 02 implements)
- `src/Buttons.cpp` - Stub (Plan 02 implements)
- `src/Events.cpp` - Stub (Plan 02 implements)
- `.gitignore` - Excludes build/ directory

## Decisions Made
- test_raii linked against X11 because inline RAII destructors in x11wrap.h call XFree* functions, requiring X11 symbols at link time even though no display is needed
- Used `catch_discover_tests(... PROPERTIES ...)` instead of post-discovery `set_tests_properties(${test_smoke_DISCOVERED_TESTS} ...)` because the variable is empty at configure time
- Added `#include <X11/cursorfont.h>` to test_smoke.cpp for XC_left_ptr constant used in UniqueCursor test

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical] X11 linkage for test_raii**
- **Found during:** Task 3 (build phase)
- **Issue:** CMakeLists.txt originally did not link test_raii against X11, but x11wrap.h's inline destructors call XFree* functions requiring X11 symbols
- **Fix:** Added `PkgConfig::X11` to test_raii target_link_libraries
- **Files modified:** CMakeLists.txt
- **Verification:** Build and all tests pass
- **Committed in:** faae8ff (Task 3 commit)

**2. [Rule 3 - Blocking] Missing cursorfont.h include**
- **Found during:** Task 3 (build phase)
- **Issue:** `XC_left_ptr` constant undeclared -- requires `#include <X11/cursorfont.h>`
- **Fix:** Added include to test_smoke.cpp
- **Files modified:** tests/test_smoke.cpp
- **Verification:** Build succeeds, all tests pass
- **Committed in:** faae8ff (Task 3 commit)

**3. [Rule 3 - Blocking] Xvfb fixture not applied to smoke tests**
- **Found during:** Task 3 (test run)
- **Issue:** `set_tests_properties(${test_smoke_DISCOVERED_TESTS} ...)` silently failed because the variable is empty at configure time (tests discovered at build time)
- **Fix:** Moved properties into `catch_discover_tests(test_smoke PROPERTIES ENVIRONMENT "DISPLAY=:99" FIXTURES_REQUIRED xvfb_display)`
- **Files modified:** CMakeLists.txt
- **Verification:** Smoke tests now run after Xvfb start fixture, all 25 tests pass
- **Committed in:** faae8ff (Task 3 commit)

---

**Total deviations:** 3 auto-fixed (1 missing critical, 2 blocking)
**Impact on plan:** All auto-fixes were necessary for correctness and buildability. No scope creep.

## Issues Encountered
- Rotated.C compiled cleanly with `-w` flag alone under C++17 -- no `-fpermissive` needed (contrary to research concern A4)
- main.cpp stub needed `int main() { return 0; }` instead of empty file for the WM binary target to link

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Build infrastructure is complete and reproducible from clean checkout
- RAII wrappers ready for consumption by Plan 02 (WM source files)
- Test harness validated with both unit and integration tests
- Source stubs in place for Plan 02 to fill in

---
*Phase: 01-build-infrastructure-raii-foundation*
*Completed: 2026-05-06*
