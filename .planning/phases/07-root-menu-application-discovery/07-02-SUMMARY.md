---
phase: 07-root-menu-application-discovery
plan: 02
subsystem: app-discovery
tags: [elf-parsing, mmap, gui-classifier, cpp17, catch2, toctou]

# Dependency graph
requires:
  - phase: 07-root-menu-application-discovery (plan 01)
    provides: "AppEntry struct (Source enum with BinaryScan value already defined)"
provides:
  - "BinaryScanner::readNeededLibraries -- in-process ELF64 .dynamic/DT_NEEDED reader (mmap + raw struct casts, no libelf, no subprocess)"
  - "BinaryScanner::isGuiBinary -- GUI-toolkit substring classifier"
  - "BinaryScanner::scanUsrBin -- /usr/bin directory walk synthesizing AppEntry::Source::BinaryScan records"
affects: [07-03-app-cache, 07-05-root-menu-submenus]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "In-process ELF .dynamic-section parsing via mmap + Elf64_Ehdr/Elf64_Phdr/Elf64_Dyn struct casts (no libelf dependency), replacing subprocess-spawn approaches (readelf/ldd) that were measured at 11.4s wall-clock for ~2400 files"
    - "TOCTOU-safe file classification: open() then fstat() on the resulting fd, never a separate stat() on the path before opening (T-7-04), reused identically in both readNeededLibraries and scanUsrBin's executable-bit check"
    - "Cheap 2-byte shebang probe via the already-open fd before any ELF/mmap work, avoiding wasted parsing on script files"

key-files:
  created:
    - include/BinaryScanner.h
    - src/BinaryScanner.cpp
    - tests/test_binaryscanner.cpp
  modified:
    - CMakeLists.txt

key-decisions:
  - "Local MmapGuard RAII struct defined in BinaryScanner.cpp's anonymous namespace rather than reusing Manager.h's FdGuard, to keep this module X11-free per the plan's explicit instruction"
  - "Binary-scan-discovered apps default to category=\"Other\" (distinct from D-07's \"Custom\", which is reserved for manual config entries) since they have no XDG category metadata"

patterns-established:
  - "X11-free data-class separation continues: BinaryScanner has zero Display*/Window/Xlib includes, matching Config/DesktopEntry precedent"

requirements-completed: [APPS-02]

coverage:
  - id: D1
    description: "readNeededLibraries() extracts DT_NEEDED library names from real ELF64 binaries via in-process mmap parsing with zero subprocess spawns, correctly handling malformed/truncated/statically-linked inputs"
    requirement: "APPS-02"
    verification:
      - kind: unit
        ref: "tests/test_binaryscanner.cpp#readNeededLibraries succeeds on a real CLI ELF binary"
        status: pass
      - kind: unit
        ref: "tests/test_binaryscanner.cpp#readNeededLibraries returns nullopt for bad ELF magic"
        status: pass
      - kind: unit
        ref: "tests/test_binaryscanner.cpp#readNeededLibraries returns nullopt for an empty file"
        status: pass
      - kind: unit
        ref: "tests/test_binaryscanner.cpp#readNeededLibraries returns nullopt for a shebang script"
        status: pass
      - kind: unit
        ref: "tests/test_binaryscanner.cpp#readNeededLibraries returns nullopt for a nonexistent path"
        status: pass
    human_judgment: false
  - id: D2
    description: "isGuiBinary() classifies GUI-toolkit-linked binaries (libX11/libgtk/libQt/libwayland-client/libSDL2 substrings) correctly against a known needed-libs vector"
    requirement: "APPS-02"
    verification:
      - kind: unit
        ref: "tests/test_binaryscanner.cpp#isGuiBinary classifies known GUI toolkit substrings"
        status: pass
    human_judgment: false
  - id: D3
    description: "T-7-04: open()+fstat()-on-the-fd TOCTOU-safe pattern used in both readNeededLibraries and scanUsrBin (never a separate stat() on the path before open())"
    requirement: "APPS-02"
    verification:
      - kind: other
        ref: "grep -c \"fstat(\" src/BinaryScanner.cpp (returns 4: 2 call sites + their preceding comment lines)"
        status: pass
    human_judgment: false
  - id: D4
    description: "scanUsrBin() walks /usr/bin, skips shebang scripts via a cheap 2-byte probe before any ELF parsing, and never duplicates a name already present in existingNames"
    requirement: "APPS-02"
    verification:
      - kind: unit
        ref: "tests/test_binaryscanner.cpp#scanUsrBin produces only well-formed BinaryScan entries"
        status: pass
      - kind: unit
        ref: "tests/test_binaryscanner.cpp#scanUsrBin excludes names already present in existingNames"
        status: pass
    human_judgment: false
  - id: D5
    description: "No subprocess is spawned anywhere in this module to classify a binary (no fork/popen/system, no ldd/readelf invocation)"
    requirement: "APPS-02"
    verification:
      - kind: other
        ref: "grep -c \"fork()\\|popen(\\|system(\" src/BinaryScanner.cpp (returns 0)"
        status: pass
    human_judgment: false

# Metrics
duration: 20min
completed: 2026-07-08
status: complete
---

# Phase 7 Plan 2: BinaryScanner ELF Classifier Summary

**In-process ELF64 `.dynamic`/`DT_NEEDED` reader (mmap + raw struct casts, no libelf, no subprocess) with a GUI-toolkit substring classifier and a `/usr/bin` directory walk that synthesizes `AppEntry::Source::BinaryScan` records for GUI binaries lacking a `.desktop` file.**

## Performance

- **Duration:** ~20 min
- **Started:** 2026-07-08T08:35:00Z
- **Completed:** 2026-07-08T08:43:04Z
- **Tasks:** 2
- **Files modified:** 4 (3 created, 1 modified)

## Accomplishments
- `BinaryScanner::readNeededLibraries()` (`src/BinaryScanner.cpp`) -- mmap-based ELF64 `.dynamic`-section reader that walks `PT_DYNAMIC`/`PT_LOAD` program headers, resolves `DT_STRTAB`'s vaddr to a file offset, and bounds-checks every byte access against the mapped file size before dereferencing; returns `std::nullopt` on bad magic, truncated files, 32-bit ELF, or statically-linked binaries with no `PT_DYNAMIC` segment
- `BinaryScanner::isGuiBinary()` -- substring classifier against `libX11`, `libgtk-3`, `libgtk-4`, `libQt5`, `libQt6`, `libwayland-client`, `libSDL2`
- `BinaryScanner::scanUsrBin()` -- `/usr/bin` directory walk, skips non-regular/non-executable files and shebang scripts (cheap 2-byte probe) before any ELF work, synthesizes `AppEntry` records (`category = "Other"`, `source = BinaryScan`) for GUI binaries not already present in the caller-supplied `existingNames`
- `tests/test_binaryscanner.cpp` -- 8 Catch2 `[binaryscanner]`-tagged test cases (779 assertions), all passing, verified against real system binaries (`/bin/ls`, `/usr/bin/xeyes`)

## Task Commits

Each task was committed atomically:

1. **Task 1: ELF DT_NEEDED reader + GUI/CLI classifier** - `df97268` (feat)
2. **Task 2: scanUsrBin directory walk + tests/test_binaryscanner.cpp + CMakeLists.txt registration** - `c005deb` (test)

**Plan metadata:** (this commit)

## Files Created/Modified
- `include/BinaryScanner.h` -- Public API: `readNeededLibraries`, `isGuiBinary`, `scanUsrBin`
- `src/BinaryScanner.cpp` -- ELF `.dynamic`/`DT_NEEDED` reader, GUI classifier, `/usr/bin` scan
- `tests/test_binaryscanner.cpp` -- 8 TEST_CASE entries tagged `[binaryscanner]`
- `CMakeLists.txt` -- `test_binaryscanner` executable target registered after `test_desktopentry`, X11-free (purely additive change)

## Decisions Made
- Local `MmapGuard` RAII struct defined in `BinaryScanner.cpp`'s anonymous namespace instead of reusing `Manager.h`'s `FdGuard`, keeping this module free of any X11 include chain
- Binary-scan-discovered apps default to `category = "Other"`, distinct from D-07's `"Custom"` (reserved for manual config entries), since they carry no XDG category metadata to draw from -- this was flagged in the plan as Claude's-Discretion

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking, environment-only] Full-project `cmake --build build` unavailable in this sandbox**
- **Found during:** Task 2 verify
- **Issue:** `cmake -S . -B build` still fails at configure time on `pkg_check_modules(XFT REQUIRED ...)` -- `libxft-dev` remains not installed in this sandbox (same pre-existing gap documented in 07-01-SUMMARY.md, confirmed unchanged: `pkg-config --exists xft` fails, and a fresh `cmake -S . -B build` reproduces the identical `No package 'xft' found` error). Unrelated to any file this plan touches (`BinaryScanner.h`/`BinaryScanner.cpp`/`test_binaryscanner.cpp` have zero X11/Xft dependency by design).
- **Fix:** Built and ran the new test target through an isolated `/tmp` CMake project (same fallback pattern as 07-01) that `FetchContent`s the identical Catch2 v3.14.0 tag and compiles `tests/test_binaryscanner.cpp` + `src/BinaryScanner.cpp` directly from this repo's absolute paths, with no other project dependencies. All 8 `[binaryscanner]` test cases passed (779 assertions, 0 failures). A standalone `g++ -std=c++17 -Wall -Iinclude` compile of the check driver + `src/BinaryScanner.cpp` also confirmed the acceptance-criteria greps (`fork()|popen(|system(` = 0, `fstat(` = 4, `AppEntry::Source::BinaryScan` >= 1) pass against the real repo file.
- **Files modified:** none (verification-only workaround; the real `CMakeLists.txt` block is additive and structurally identical to the already-working `test_desktopentry`/`test_config` blocks)
- **Verification:** `/tmp/wm2-binscan-harness/build/test_binaryscanner` -> "All tests passed (779 assertions in 8 test cases)"
- **Committed in:** N/A (no code change required; documented here per Rule 3)

---

**Total deviations:** 1 auto-fixed (1 environment-only blocking issue, verification workaround)
**Impact on plan:** No code changes were needed to work around the missing system package. The real project `CMakeLists.txt` change is a pure additive block, so it will build normally once `libxft-dev` is present or on any machine where the top-level configure already succeeds.

## Issues Encountered
None beyond the environment gap documented above.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- `BinaryScanner::scanUsrBin()` is ready for `07-03` (AppCache) to invoke during a rescan, passing in the set of names already covered by `DesktopEntry::scanAll()` as `existingNames`
- **Environment note carried forward:** this sandbox still cannot run `cmake --build build` for the full project (missing `libxft-dev`). `07-03` (AppCache, X11-free per the phase's Architectural Responsibility Map) can use the same standalone-Catch2-harness fallback if this persists.
- No blockers for `07-03`; `BinaryScanner.h`'s `scanUsrBin()` signature (`existingNames` -> `vector<AppEntry>`) matches the merge-step shape described in RESEARCH.md's architecture diagram.

---
*Phase: 07-root-menu-application-discovery*
*Completed: 2026-07-08*

## Self-Check: PASSED

All created files verified present on disk; both task commit hashes (`df97268`, `c005deb`) verified present in git history.
