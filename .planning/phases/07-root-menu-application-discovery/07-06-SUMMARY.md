---
phase: 07-root-menu-application-discovery
plan: 06
subsystem: root-menu-ui
tags: [x11, xft, wm2, root-menu, app-discovery, startup-wiring, xdg, appcache]

# Dependency graph
requires:
  - phase: 07-01
    provides: "DesktopEntry XDG .desktop parsing"
  - phase: 07-02
    provides: "BinaryScanner heuristic GUI-binary classification"
  - phase: 07-03
    provides: "AppCache::loadOrRescan/defaultCachePath, Config::manualMenuEntries"
  - phase: 07-04
    provides: "WindowManager(config, apps) constructor, m_appCategories, launchApp()"
  - phase: 07-05
    provides: "Category rows + hover-triggered submenu popup in WindowManager::menu()"
provides:
  - "Full Phase 7 startup wiring: Config::load() -> AppCache::loadOrRescan() -> WindowManager(config, apps) in src/main.cpp"
  - "Manually verified end-to-end root menu: categorized rows, hover-to-expand submenus, manual Custom entries, launch dispatch (Xvfb, human-approved)"
  - "PROJECT.md/ROADMAP.md wording corrected (D-02): scanner described as heuristic-based, not runtime AI-powered"
affects: []

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "main() remains a thin sequencing script: Config::load -> AppCache::loadOrRescan -> WindowManager(config, apps), no added error handling beyond what each subsystem already provides internally (warn-and-continue convention established in prior plans)"

key-files:
  created: []
  modified:
    - src/main.cpp
    - CMakeLists.txt
    - .planning/PROJECT.md
    - .planning/ROADMAP.md

key-decisions:
  - "wm2-born-again CMake target was missing src/AppCache.cpp/DesktopEntry.cpp/BinaryScanner.cpp in its source list -- each prior plan (07-01/02/03) only wired its own file into its own isolated test target, never into the real binary target. Fixed as part of Task 1 (cross-plan integration bug, Rule 1)."
  - "D-02 wording fix scoped exactly as specified: three AI-powered occurrences in PROJECT.md, two in ROADMAP.md reworded to heuristic-based; REQUIREMENTS.md and research/FEATURES.md left untouched (out of scope per D-02); AI-assisted-development showcase framing itself preserved"

requirements-completed: [APPS-01, APPS-02, APPS-03, APPS-04, APPS-05]

coverage:
  - id: D1
    description: "src/main.cpp wires the complete Phase 7 pipeline: Config::load() -> AppCache::loadOrRescan(defaultCachePath(), config.manualMenuEntries) -> WindowManager(config, apps); CMakeLists.txt fixed to compile AppCache.cpp/DesktopEntry.cpp/BinaryScanner.cpp into the real wm2-born-again binary (not just isolated test targets)"
    requirement: "APPS-01"
    verification:
      - kind: unit
        ref: "ctest (136/136 tests, including test_desktopentry, test_binaryscanner, test_appcache, test_config) -- exit 0"
        status: pass
      - kind: other
        ref: "cmake --build build -> exit 0, produces build/wm2-born-again; grep -c 'AppCache::loadOrRescan' src/main.cpp -> 1; grep -c 'WindowManager manager(config, apps)' src/main.cpp -> 1"
        status: pass
    human_judgment: false
  - id: D2
    description: "Categorized root menu with hover-to-expand submenus, manual Custom config entries, and execvp() launch dispatch verified end-to-end on a running Xvfb instance"
    requirement: "APPS-05"
    verification:
      - kind: manual_procedural
        ref: "Xvfb :98 interactive session: root-click menu shows New + 19 category rows alphabetically sorted (Custom last, D-07); hover on Custom opens submenu (D-03/D-04); manually-configured Test Entry (menu-entry-category=Custom) appears in Custom submenu (APPS-04); clicking Test Entry launches via execvp() and closes both popups cleanly; appcache.json created with 191 binary-scan + 115 desktop-file entries"
        status: pass
    human_judgment: true
    rationale: "Root menu visual/interactive behavior (hover timing, popup rendering, submenu positioning) has no automated screenshot-diff coverage in this project (documented in 07-VALIDATION.md as a manual-only verification). Human operator explicitly typed 'approved' after reviewing the driven Xvfb session and screenshots."
  - id: D3
    description: "PROJECT.md and ROADMAP.md no longer describe the binary scanner as a runtime 'AI-powered' mechanism; reworded to 'heuristic-based' per D-02, while preserving the project's AI-assisted-development showcase framing"
    requirement: "APPS-01"
    verification:
      - kind: other
        ref: "grep -c 'AI-powered' .planning/PROJECT.md .planning/ROADMAP.md -> 0 for both files; grep -n 'AI showcase' .planning/PROJECT.md confirms showcase paragraph intact (heading + one pre-existing unrelated reference in Key Decisions table, out of D-02's scope)"
        status: pass
    human_judgment: false

duration: ~25min (Task 1 + Task 2 checkpoint + Task 3, across the original executor run and this continuation)
completed: 2026-07-08
status: complete
---

# Phase 7 Plan 6: Startup Wiring + Manual Root-Menu Verification Summary

**`src/main.cpp` now runs the full Phase 7 pipeline (`Config::load` -> `AppCache::loadOrRescan` -> `WindowManager(config, apps)`), the missing `AppCache`/`DesktopEntry`/`BinaryScanner` sources were added to the real binary's CMake target, and the categorized root menu with hover-to-expand submenus was manually verified end-to-end on Xvfb and approved by the human operator -- completing Phase 7.**

## Performance

- **Duration:** ~25 min (Task 1 build/wiring + Task 2 manual-verification checkpoint + Task 3 doc wording, spanning the original executor run and this continuation agent)
- **Tasks:** 3 (1 auto, 1 checkpoint, 1 auto)
- **Files modified:** 4 (`src/main.cpp`, `CMakeLists.txt`, `.planning/PROJECT.md`, `.planning/ROADMAP.md`)

## Accomplishments
- `src/main.cpp` sequences the entire Phase 7 pipeline: `Config::load(argc, argv)` -> `AppCache::loadOrRescan(AppCache::defaultCachePath(), config.manualMenuEntries)` -> `WindowManager manager(config, apps)`
- Fixed a cross-plan integration bug: the `wm2-born-again` CMake target's source list was missing `src/AppCache.cpp`, `src/DesktopEntry.cpp`, and `src/BinaryScanner.cpp` -- these were only ever compiled into each prior plan's isolated unit-test target, never into the real binary, so the full pipeline could not have linked without this fix
- Manually verified on Xvfb (:98): root menu shows "New" + 19 alphabetically-sorted category rows (Custom last, per D-07) + Exit; hovering a category opens a second popup listing that category's apps (D-03/D-04); a manually-configured `menu-entry-category=Custom` config entry correctly appears in the Custom submenu (APPS-04 end-to-end); clicking an entry launches it via `execvp()` and closes both popups cleanly with no crash/hang
- `~/.config/wm2-born-again/appcache.json` confirmed created with 191 binary-scan entries + 115 desktop-file entries, proving both discovery pipelines (07-01 `DesktopEntry`, 07-02 `BinaryScanner`) feed the merged, cached app list
- Reworded all five "AI-powered" occurrences across `.planning/PROJECT.md` (3) and `.planning/ROADMAP.md` (2) to "heuristic-based," per D-02, while leaving the project's AI-assisted-development showcase framing intact

## Task Commits

Each task was committed atomically:

1. **Task 1: Wire Config -> AppCache::loadOrRescan -> WindowManager in main.cpp** - `92d7dad` (feat) -- includes the CMakeLists.txt source-list fix as part of the same commit (both required for the binary to link)
2. **Task 2: Manual verification -- categorized root menu with hover-to-expand submenus on Xvfb** - checkpoint, no source commit (verification-only task); driven by the orchestrator on Xvfb `:98`, approved by the human operator with "Approved"
3. **Task 3: Clarify "AI-powered" wording in PROJECT.md/ROADMAP.md (D-02)** - `9c3a5f0` (docs)

**Plan metadata:** (this commit)

## Files Created/Modified
- `src/main.cpp` -- full Phase 7 startup wiring (`AppCache::loadOrRescan` call, `WindowManager(config, apps)` construction)
- `CMakeLists.txt` -- `wm2-born-again` target source list extended with `AppCache.cpp`, `DesktopEntry.cpp`, `BinaryScanner.cpp` (cross-plan integration fix)
- `.planning/PROJECT.md` -- "AI-powered" reworded to "heuristic-based" in the project summary line, the scan checklist item, and the AI showcase paragraph
- `.planning/ROADMAP.md` -- "AI-powered" reworded to "heuristic-based" in the Phase 7 one-line summary and the Phase 7 Goal line

## Decisions Made
- **CMakeLists.txt fix bundled into Task 1's commit** rather than split out: the binary cannot link without both the `main.cpp` wiring and the source-list fix, so they form one atomic change to "make the full pipeline build."
- **D-02 scope held strictly to PROJECT.md/ROADMAP.md**, per the plan's explicit exclusion of `REQUIREMENTS.md` and `research/FEATURES.md`. One pre-existing, unrelated "AI showcase" reference in PROJECT.md's Key Decisions table (`"fits AI showcase"`, line 83) was left untouched -- it predates this plan, is not one of the three flagged "AI-powered" occurrences, and does not claim runtime AI classification.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `wm2-born-again` CMake target missing AppCache/DesktopEntry/BinaryScanner sources**
- **Found during:** Task 1 (wiring `main.cpp`, then attempting to build the real binary)
- **Issue:** Plans 07-01/07-02/07-03 each added their new `.cpp` file to their own isolated test-only CMake target (`test_desktopentry`, `test_binaryscanner`, `test_appcache`) but never to the `wm2-born-again` executable target itself. Once `main.cpp` called `AppCache::loadOrRescan()`, the real binary failed to link with undefined-reference errors.
- **Fix:** Added `src/AppCache.cpp`, `src/DesktopEntry.cpp`, `src/BinaryScanner.cpp` to the `wm2-born-again` target's source list in `CMakeLists.txt`.
- **Files modified:** `CMakeLists.txt`
- **Verification:** `cmake --build build` exits 0, produces `build/wm2-born-again`; full `ctest` suite (136/136) still green.
- **Committed in:** `92d7dad` (Task 1 commit)

---

**Total deviations:** 1 auto-fixed (1 bug)
**Impact on plan:** Necessary for the plan's core deliverable (a linkable, runnable binary) to exist at all. No scope creep -- fix was strictly the missing source-list entries, nothing else changed in the build config.

## Issues Encountered

**Sandbox environment gap (documented in every prior Phase 7 plan's SUMMARY, reused unchanged this plan): `libxft-dev` is not installed and `sudo apt-get install` is unavailable in this session.**

Resolved using the same workaround established starting in Plan 07-04: real Xft/fontconfig/freetype/libpng/uuid/x11/xcb headers were extracted (no root needed: `apt-get download` + `dpkg-deb -x`) to `/tmp/xft-extract/extracted`, and `PKG_CONFIG_LIBDIR`/`PKG_CONFIG_SYSROOT_DIR`/`CPATH`/`LIBRARY_PATH` environment variables pointed the real `cmake --build` at them. The full `wm2-born-again` binary builds and links cleanly, and `ctest` was re-run at the very end of this plan (after the Task 3 doc-only commit) as final insurance: **136/136 tests passed.**

5 pre-existing legacy test binaries (`test_client`, `test_ewmh`, `test_raii`, `test_smoke`, `test_xft_poc`) fail to *link* in this specific `/tmp`-extraction sandbox with `undefined reference to xcb_disconnect/xcb_get_setup` -- a known artifact of mixing the extracted-package `libxcb` with the system's own, unrelated to any Phase 7 code, and already documented in STATE.md decisions from earlier waves in this phase. Not chased further here; `ctest` still reports 136/136 passing because these binaries' tests are not part of the discovered test set in this build configuration.

## User Setup Required

None -- no external service configuration required. (Same note as every prior plan this phase: installing `libxft-dev` on this dev machine, e.g. `sudo apt-get install libxft-dev`, would let future work run the project's normal `cmake --build build` without the `/tmp` extraction workaround.)

## Next Phase Readiness

**Phase 7 (Root Menu + Application Discovery) is complete.** All five requirements (APPS-01 through APPS-05) are implemented, wired into the real binary, and manually verified end-to-end:
- APPS-01/02/03: XDG `.desktop` parsing (07-01) + heuristic binary scanning (07-02) + `AppCache` merge/persist (07-03), all now actually compiled into and invoked by the running `wm2-born-again` binary
- APPS-04: manual config-file menu entries (`menu-entry-name`/`menu-entry-command`/`menu-entry-category`) merge correctly, verified live in the Custom submenu
- APPS-05: categorized root menu with hover-to-expand submenus (07-04/07-05), verified live on Xvfb

No blockers carried forward. The `libxft-dev` sandbox gap has a documented, reusable workaround (`/tmp/xft-extract/extracted` + `PKG_CONFIG_*`/`CPATH`/`LIBRARY_PATH`) that any future plan touching X11/Xft code in this environment can reapply. Phase 8 (Xrandr + VNC Compatibility + Focus/Rules) can proceed once discussed/planned.

---
*Phase: 07-root-menu-application-discovery*
*Completed: 2026-07-08*

## Self-Check: PASSED

All modified files verified present on disk (`src/main.cpp`, `CMakeLists.txt`, `.planning/PROJECT.md`, `.planning/ROADMAP.md`, `.planning/phases/07-root-menu-application-discovery/07-06-SUMMARY.md`); all three commit hashes (`92d7dad`, `9c3a5f0`, `e998cd8`) verified present in git history.
