---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
current_phase: 08
current_phase_name: xrandr-vnc-compatibility-focus-rules
status: executing
stopped_at: Completed 08-03-PLAN.md
last_updated: "2026-08-11T13:17:55.182Z"
last_activity: 2026-08-11
last_activity_desc: gathered Phase 8 context across 8 gray areas (32 decisions captured)
progress:
  total_phases: 8
  completed_phases: 7
  total_plans: 36
  completed_plans: 25
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-06)

**Core value:** A lightweight, visually distinctive window manager that works well on resource-constrained VPS instances via remote desktop -- simple enough for non-programmers to configure, reliable enough for daily use.
**Current focus:** Phase 08 — xrandr-vnc-compatibility-focus-rules

## Current Position

Phase: 08 (xrandr-vnc-compatibility-focus-rules) — EXECUTING
Plan: 4 of 14
Status: Ready to execute
Last activity: 2026-08-11 — Phase 08 execution started

Progress: [███████░░░] 69% (7/9 phases complete)

## Performance Metrics

**Velocity:**

- Total plans completed: 22
- Average duration: 18min
- Total execution time: 0.6 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 01 | 2 | - | - |
| 02 | 2 | - | - |
| 03 | 3 | - | - |
| 04 | 3 | - | - |
| 05 | 3 | - | - |
| 06 | 3 | - | - |
| 07 | 6 | - | - |

**Recent Trend:**

- Last 5 plans: P01(14min), P02(22min)
- Trend: Steady
- Trend: -

*Updated after each plan completion*
| Phase 01 P01 | 14min | 3 tasks | 12 files |
| Phase 01 P02 | 22min | 3 tasks | 9 files |
| Phase 03 P01 | 15min | 2 tasks | 6 files |
| Phase 06 P01 | 15min | 1 tasks | 6 files |
| Phase 06 P02 | 7min | 1 tasks | 6 files |
| Phase 06 P03 | 5min | 1 tasks | 5 files |
| Phase 07 P01 | 15min | 3 tasks | 5 files |
| Phase 07 P02 | 20min | 2 tasks | 4 files |
| Phase 07 P04 | 25min | 2 tasks | 2 files |
| Phase 07 P03 | 20min | 3 tasks | 7 files |
| Phase 07 P05 | 20min | 2 tasks | 2 files |
| Phase 07 P06 | 25min | 3 tasks | 4 files |
**Per-Plan Metrics:**

| Plan | Duration | Tasks | Files |
|------|----------|-------|-------|
| Phase 08 P01 | 103min | 3 tasks | 7 files |
| Phase 08 P02 | 68min | 3 tasks | 6 files |
| Phase 08 P03 | 39min | 3 tasks | 8 files |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- [Phase ?]: test_raii linked against X11 because inline RAII destructors call XFree* functions
- [Phase ?]: catch_discover_tests PROPERTIES used for Xvfb fixtures instead of post-discovery set_tests_properties
- [Phase 01 P02]: Client::display() moved to public so Border can access it
- [Phase 01 P02]: installCursorOnWindow() added to Manager for Border to set cursor on resize handle
- [Phase 01 P02]: std::vector<XRectangle> replaces custom RectangleList macro in Border.cpp
- [Phase 01 P02]: Shape extension missing produces warning but not fatal exit
- [Phase ?]: ClientState enum values match X11 constants (Withdrawn=0, Normal=1, Iconic=3)
- [Phase ?]: State transitions validated but applied anyway with warning -- matches upstream permissive behavior
- [Phase ?]: Destructor-only cleanup replaces release() method; unreparent called in destructor for non-withdrawn clients
- [Phase 03 P03]: Tests verify infrastructure types (ServerGrab, ClientState) directly on Xvfb since full WindowManager cannot be unit-tested
- [Phase 03 P03]: Added X11/Xatom.h include for XA_WINDOW atom in colormap property test
- [Phase ?]: 07-01: xdgDataHome() kept internal (anonymous namespace), not exposed in DesktopEntry.h public API
- [Phase ?]: 07-01: field-code-only tokens (e.g. lone %u) are dropped from execArgv rather than kept as empty strings
- [Phase ?]: Local MmapGuard RAII struct in BinaryScanner.cpp's anonymous namespace instead of reusing Manager.h's FdGuard, to keep BinaryScanner X11-free
- [Phase ?]: Binary-scan-discovered apps default to category=Other (distinct from D-07's Custom for manual entries)
- [Phase ?]: 07-04: m_apps/m_submenuWindow initializer-list position follows header declaration order (not the plan text's literal wording) to avoid -Wreorder
- [Phase ?]: 07-04: Task 1/Task 2 commits split via temporary revert-reapply since both touch the same two files
- [Phase ?]: 07-03: write() creates parent config directory if missing (nothing else in the project creates ~/.config/wm2-born-again/)
- [Phase ?]: 07-03: mergeEntries() does not re-apply D-07 Custom default -- applied once at Config::applyKeyValue's menu-entry-name= handler
- [Phase 07]: openCategorySubmenu() signature extended with outerX/outerY/outerMaxWidth/rowIndex params beyond the plan's literal 2-param declaration, since the outer menu's position/hover-row-index are menu()'s local variables not otherwise recoverable
- [Phase ?]: 07-06: wm2-born-again CMake target was missing AppCache.cpp/DesktopEntry.cpp/BinaryScanner.cpp source entries -- fixed in Task 1
- [Phase ?]: 07-06: D-02 wording fix scoped strictly to PROJECT.md/ROADMAP.md's 5 flagged AI-powered occurrences -- AI showcase framing preserved
- [Phase 8]: COMPILED_CODE_BEHAVIOR_CHECKLIST.md is routed into Phase 8 as the first verification/hardening gate rather than a standalone milestone, because its findings overlap XDIS, FOCUS, remote-display compatibility, and runtime proof requirements
- [Phase 08]: 08-01: eventDestroy fix cited as SCAN-02 not D-07 -- D-07 already denotes the child-process harness (08-CONTEXT) and Phase 7's Custom-category default
- [Phase 08]: 08-01: dock-ness read via a 'const Client& dying' alias so the c->isDock() grep guard is a true regression detector
- [Phase 08]: 08-01: fixture Xvfb needs -noreset plus a retained keepalive connection; the readiness probe's disconnect was resetting the server under the WM (~9% flake)
- [Phase 08]: 08-01: XTEST tab click uses press -> 200ms -> release, not click(), to avoid racing the WM's move-loop pointer grab
- [Phase 08]: 08-01: D-33 tag-selection convention established -- Catch2 tags as ctest labels, anchored -L, --no-tests=error on every gate; CMake floor raised to 3.20
- [Phase ?]: cppcheck 2.7's CLI never computes a per-finding hash, so the static-analysis baseline is anchored on hashes the gate computes itself and cppcheck consumes a derived hash-free suppression file generated at run time
- [Phase ?]: clang-tidy's fatal list contains only families already clean on this tree (bugprone-dangling-handle, clang-analyzer-core.*); the three families that fire today are reported-only with a documented promotion path
- [Phase ?]: 08-03: routing the rectangular fallback helpers through combineShape() makes them emit nothing on Shape-less servers -- that silence IS the correct fallback, since an unshaped window is already rectangular
- [Phase ?]: 08-03: capability triple documented in include/Manager.h (negative member sentinel + has*Extension() predicate + one no-op-when-absent funnel); 08-05/08-06 copy it
- [Phase ?]: 08-03: bugprone-branch-clone true pre-existing count is 6, not the 5 recorded by 08-02 (missed src/Client.cpp:1415); re-measured at 08-02's own commit

### Pending Todos

- Phase 8 08-01: Turn COMPILED_CODE_BEHAVIOR_CHECKLIST.md into executable process-level Xvfb/Xephyr tests, sanitizer/static-analysis gates, and release evidence capture.
- Phase 8 08-01: Resolve or explicitly accept current scan findings: missing xft/fontconfig build preflight on this host, eventDestroy client lifetime hazard, no-Shape fallback proof, and focus policy config wiring proof.

### Blockers/Concerns

- Phase 4 (Border/Xft): Xft rendering inside shaped windows is poorly documented; build a PoC first
- Phase 7 (App Scanner): Binary scan heuristics for identifying GUI apps are novel
- Phase 8 (Behavior Verification): Current host cannot configure until pkg-config sees xft/fontconfig; do not claim compiled behavior coverage until dependency preflight passes
- Phase 8 (Behavior Verification): Static scan found eventDestroy use-after-free risk, incomplete no-Shape fallback proof, and parsed focus config that needs runtime behavior tests
- Phase 9 (Config GUI): GTK3 performance over SSH X forwarding is unvalidated
- Bare 'ctest --test-dir build/asan' fails 3 xft tests on a pre-existing fontconfig cache leak: tests/lsan.supp is wired only into the forked WM child, not the Catch2 test binaries. Run the asan tree via scripts/gates/build-all.sh until fixed (deferred-items.md item 5).

## Deferred Items

Items acknowledged and carried forward from previous milestone close:

| Category | Item | Status | Deferred At |
|----------|------|--------|-------------|
| *(none)* | | | |

## Session Continuity

Last session: 2026-08-11T13:17:55.161Z
Stopped at: Completed 08-03-PLAN.md
Resume file: None
