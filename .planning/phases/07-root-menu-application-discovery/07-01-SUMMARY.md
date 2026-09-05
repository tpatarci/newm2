---
phase: 07-root-menu-application-discovery
plan: 01
subsystem: app-discovery
tags: [xdg-desktop-entry, cpp17, catch2, exec-tokenizer, hand-rolled-parser]

# Dependency graph
requires: []
provides:
  - "AppEntry struct: shared plain-data model (name, execArgv, icon, category, Source enum) consumed by every later plan in this phase"
  - "DesktopEntry::parseExec/parseFile/scanAll/xdgDataDirs/xdgApplicationsDirs -- XDG .desktop file parser, X11-free"
affects: [07-02-binary-scanner, 07-03-app-cache, 07-04-config-manual-entries, 07-05-root-menu-submenus]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Hand-rolled .desktop key=value parser mirroring Config::applyFile's trim/skip-comment/warn-and-continue loop (no GLib/GKeyFile dependency)"
    - "Exec= tokenized into a fixed argv respecting spec quoting; unrecognized field codes reject the whole entry rather than being silently dropped -- never routed through a shell"

key-files:
  created:
    - include/AppEntry.h
    - include/DesktopEntry.h
    - src/DesktopEntry.cpp
    - tests/test_desktopentry.cpp
  modified:
    - CMakeLists.txt

key-decisions:
  - "xdgDataHome() kept as an internal (anonymous-namespace) helper, not exposed in DesktopEntry.h, since the header only declares the five functions the plan specified"
  - "Empty tokens produced by field-code stripping (e.g. a lone %u argument) are dropped from the final argv rather than kept as empty strings"

patterns-established:
  - "X11-free data-class separation: DesktopEntry has zero Display*/Window/Xlib includes, matching Config's precedent -- only Buttons.cpp/Manager.cpp may touch X11 in this phase"

requirements-completed: [APPS-01]

coverage:
  - id: D1
    description: ".desktop files under XDG_DATA_DIRS(+XDG_DATA_HOME)/applications are parsed into AppEntry records with Name/Exec/Icon/Categories resolved"
    requirement: "APPS-01"
    verification:
      - kind: unit
        ref: "tests/test_desktopentry.cpp#parseFile parses a well-formed entry"
        status: pass
    human_judgment: false
  - id: D2
    description: "D-05 filtering: NoDisplay/Hidden/NotShowIn/unresolvable-TryExec entries excluded from scanAll()'s results"
    requirement: "APPS-01"
    verification:
      - kind: unit
        ref: "tests/test_desktopentry.cpp#parseFile excludes NoDisplay=true entries"
        status: pass
      - kind: unit
        ref: "tests/test_desktopentry.cpp#parseFile excludes Hidden=true entries"
        status: pass
      - kind: unit
        ref: "tests/test_desktopentry.cpp#parseFile excludes NotShowIn=wm2-born-again entries"
        status: pass
      - kind: unit
        ref: "tests/test_desktopentry.cpp#parseFile excludes entries with unresolvable TryExec"
        status: pass
    human_judgment: false
  - id: D3
    description: "T-7-01: Exec= is tokenized into a fixed argv; unrecognized field codes reject the whole entry rather than reaching execvp() or a shell"
    requirement: "APPS-01"
    verification:
      - kind: unit
        ref: "tests/test_desktopentry.cpp#parseFile rejects unrecognized Exec field codes"
        status: pass
      - kind: unit
        ref: "tests/test_desktopentry.cpp#parseExec treats a quoted argument as one literal token"
        status: pass
    human_judgment: false

duration: 15min
completed: 2026-07-08
status: complete
---

# Phase 7 Plan 1: DesktopEntry XDG Parser Summary

**Hand-rolled XDG .desktop parser (AppEntry model + DesktopEntry namespace) with a spec-compliant Exec= tokenizer that rejects unrecognized field codes outright and never touches a shell.**

## Performance

- **Duration:** ~15 min
- **Started:** 2026-07-08T08:19:00Z
- **Completed:** 2026-07-08T08:30:39Z
- **Tasks:** 3
- **Files modified:** 5 (4 created, 1 modified)

## Accomplishments
- `AppEntry` struct (`include/AppEntry.h`) -- plain-data model with `Source` enum, mirrors `Config`'s no-X11 convention exactly
- `DesktopEntry` namespace (`include/DesktopEntry.h` / `src/DesktopEntry.cpp`) -- `xdgDataDirs()`/`xdgApplicationsDirs()` (colon-split XDG enumeration mirroring `xdgConfigDirs()`), `parseExec()` (spec-quoting tokenizer + field-code stripper, unrecognized codes reject the whole value per T-7-01), `parseFile()` (`[Desktop Entry]` section parser with full D-05 filtering), `scanAll()` (directory walk, missing dirs skipped silently)
- `tests/test_desktopentry.cpp` -- 11 Catch2 `[desktopentry]`-tagged test cases (27 assertions), all passing

## Task Commits

Each task was committed atomically:

1. **Task 1: Define AppEntry.h and DesktopEntry.h** - `8a0f086` (feat)
2. **Task 2: Implement src/DesktopEntry.cpp** - `dfadd60` (feat)
3. **Task 3: tests/test_desktopentry.cpp + CMakeLists.txt registration** - `de5bb55` (test)

**Plan metadata:** (this commit)

_Note: Task 2 carried `tdd="true"` in the plan but the plan structured actual Catch2 test authoring as a separate Task 3 (test file + CMake registration created after the implementation, per the plan's own `<action>`/`<verify>` split); Task 2's `<behavior>` block was used as the implementation spec and verified via a standalone `g++` compile plus an ad-hoc sanity driver before commit, matching the plan's documented fallback verify path._

## Files Created/Modified
- `include/AppEntry.h` - Shared plain-data model consumed by every later plan in this phase
- `include/DesktopEntry.h` - Public API: parseExec/parseFile/scanAll/xdgDataDirs/xdgApplicationsDirs
- `src/DesktopEntry.cpp` - XDG dir enumeration, Exec tokenizer, .desktop key=value parser, directory scan
- `tests/test_desktopentry.cpp` - 11 TEST_CASE entries tagged [desktopentry]
- `CMakeLists.txt` - `test_desktopentry` executable target registered after `test_config`, X11-free (purely additive change)

## Decisions Made
- `xdgDataHome()` implemented as an internal (anonymous-namespace) helper rather than a public `DesktopEntry::` function, since the plan's header contract only specifies five public functions
- Empty tokens produced when a field code is the entire token (e.g. a lone `%u`) are dropped from the final `execArgv` rather than kept as empty-string elements, matching the plan's worked example (`"firefox %u"` -> `{"firefox"}`, not `{"firefox", ""}`)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking, environment-only] Full-project `cmake --build build` unavailable in this sandbox**
- **Found during:** Task 2 verify / Task 3 verify
- **Issue:** `cmake -S . -B build` fails at configure time on `pkg_check_modules(XFT REQUIRED ...)` -- `libxft-dev` (providing `xft.pc`) is not installed on this dev machine, and `sudo apt-get install` requires a password this session does not have. This is a pre-existing top-level project dependency unrelated to any file this plan touches (`AppEntry.h`/`DesktopEntry.h`/`DesktopEntry.cpp`/`test_desktopentry.cpp` have zero X11/Xft dependency by design).
- **Fix:** Verified correctness through two fallback paths sanctioned by the plan's own verify text ("if Task 3 has not yet run, a standalone `g++ ... -c` exits 0") plus an equivalent for the Catch2 suite: (1) standalone `g++ -std=c++17 -Wall -c -Iinclude src/DesktopEntry.cpp` compiled cleanly; (2) an ad-hoc `g++` sanity driver exercised `parseExec`/`parseFile` against the plan's worked examples; (3) a minimal isolated CMake project (outside the repo, in `/tmp`) that `FetchContent`s the identical Catch2 v3.14.0 tag and builds `tests/test_desktopentry.cpp` + `src/DesktopEntry.cpp` directly from this repo's paths -- all 11 `[desktopentry]` test cases passed (27 assertions, 0 failures).
- **Files modified:** none (verification-only workaround)
- **Verification:** `./test_desktopentry` (standalone harness) -> "All tests passed (27 assertions in 11 test cases)"
- **Committed in:** N/A (no code change required; documented here per Rule 3)

---

**Total deviations:** 1 auto-fixed (1 environment-only blocking issue, verification workaround)
**Impact on plan:** No code changes were needed to work around the missing system package; all plan-specified behavior was verified with equivalent rigor via an isolated build harness. The real project `CMakeLists.txt` change is a pure additive block identical in shape to the existing `test_config` registration, so it will build normally once `libxft-dev` is present (or on any machine where the top-level configure already succeeds).

## Issues Encountered
- `catch_discover_tests` post-build test discovery reported "No tests were found" in the isolated `/tmp` verification harness (likely a CTest working-directory/path quirk specific to that ad-hoc setup, since the harness referenced source files by absolute path outside its own project tree). Worked around by invoking the compiled `test_desktopentry` binary directly (`--list-tests`, then a full run), which confirmed all 11 cases pass. This does not affect the real project's `CMakeLists.txt`, whose `test_desktopentry` block is structurally identical to the already-working `test_config` block.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- `AppEntry` and `DesktopEntry` are ready for `07-02` (BinaryScanner) and `07-03` (AppCache) to build on -- both are documented as consuming the same `AppEntry` struct unchanged.
- **Environment note for future plans in this phase:** this sandbox cannot currently run `cmake --build build` for the full project (missing `libxft-dev`). Subsequent plans (07-02 through 07-06) that also register X11-free test targets can use the same standalone-Catch2-harness fallback if this persists; plans that touch `src/Buttons.cpp`/X11 rendering will need `libxft-dev` installed (or sudo access) before their `<verify>` steps can run against the real project build.
- No blockers for 07-02/07-03 (both are X11-free per the phase's Architectural Responsibility Map).

---
*Phase: 07-root-menu-application-discovery*
*Completed: 2026-07-08*

## Self-Check: PASSED

All created files verified present on disk; all three task commit hashes (`8a0f086`, `dfadd60`, `de5bb55`) verified present in git history.
