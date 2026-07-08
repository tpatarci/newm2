---
phase: 07-root-menu-application-discovery
verified: 2026-07-08T10:05:50Z
status: passed
score: 9/9 must-haves verified
behavior_unverified: 0
overrides_applied: 0
---

# Phase 7: Root Menu + Application Discovery Verification Report

**Phase Goal:** The root menu shows all installed GUI applications organized by category, combining XDG .desktop entries with heuristic-based binary discovery and user customizations.
**Verified:** 2026-07-08T10:05:50Z
**Status:** passed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths (ROADMAP Success Criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Clicking the root window shows a menu with applications discovered from XDG .desktop files, organized by category | ✓ VERIFIED | Live Xvfb screenshot (`/tmp/wm2-menu-open.png`) shows the root menu with "New" plus 19 alphabetical category rows ("Application", "AudioVideo", ... "XFCE") and "Custom" last, matching `WindowManager::buildAppCategories()` (src/Manager.cpp:194-214) and `menu()`'s category-row rendering (src/Buttons.cpp:104-334). **Caveat:** ROADMAP.md's literal wording says "Right-clicking" — the actual (and correct, upstream-inherited) binding is `Button1` (left-click); `Button3` triggers `circulate()` instead (src/Buttons.cpp:16-22). This wording mismatch pre-dates Phase 7 (inherited from upstream wm2's `Buttons.C:25`) and was not introduced or fixed by any Phase 7 plan — see Anti-Patterns/Gaps Summary below. |
| 2 | The scanner finds GUI applications in /usr/bin that lack .desktop files and adds them to the menu | ✓ VERIFIED | Live appcache.json from a real scan on this machine: 192 `binaryscan`-sourced entries out of 307 total; `BinaryScanner::scanUsrBin()` (src/BinaryScanner.cpp:202-270) skips names already in `existingNames`, uses in-process ELF `.dynamic`/`DT_NEEDED` parsing (no subprocess), classifies via `isGuiBinary()` toolkit-substring match, defaults `category = "Other"` (visible in the live screenshot). |
| 3 | Scan results are cached at ~/.config/wm2-born-again/appcache.json so subsequent startups are fast | ✓ VERIFIED | `AppCache::defaultCachePath()` = `$XDG_CONFIG_HOME/wm2-born-again/appcache.json` (src/AppCache.cpp:246-248); live run created `/tmp/wm2-verify-xdgconfig/wm2-born-again/appcache.json` (38KB, 307 entries) on first start; `needsRescan()` (src/AppCache.cpp:250-266) mtime-gates rescans against `/usr/bin` + XDG applications dirs; `loadOrRescan()` fast-paths from the cache when fresh (D-06). Round-trip and invalidation covered by `tests/test_appcache.cpp` (8 cases, all passing). |
| 4 | User can manually add or remove menu entries via the config file, and those entries appear in the root menu | ✓ VERIFIED | Live test: wrote `menu-entry-name=VerifyTestEntry` / `menu-entry-command=/bin/true` / `menu-entry-category=Custom` to the config file, restarted the WM, and confirmed "VerifyTestEntry" appeared in the live-rendered "Custom" submenu alongside pre-existing Custom-category apps (screenshot `/tmp/wm2-submenu3.png`). `Config::applyKeyValue`'s accumulator pattern (src/Config.cpp:191-225) and `AppCache::mergeEntries()`'s D-08 name-based override (src/AppCache.cpp:380-402) both confirmed by unit tests and live behavior. |

**Score:** 4/4 ROADMAP success criteria verified.

### Additional Plan-Level Must-Haves (from PLAN.md frontmatter, APPS-01 through APPS-05)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 5 | APPS-01: `.desktop` files parsed into `AppEntry` with D-05 filtering (NoDisplay/Hidden/NotShowIn/unresolvable TryExec excluded) | ✓ VERIFIED | `src/DesktopEntry.cpp:223-319` implements exact D-05 filter order; `tests/test_desktopentry.cpp` (11 cases) all pass; live scan produced 115 `desktop`-sourced entries. |
| 6 | T-7-01: Exec= tokenized into fixed argv; unrecognized field code rejects the whole entry; never routed through a shell | ✓ VERIFIED | `DesktopEntry::parseExec()` (src/DesktopEntry.cpp:137-221) returns `std::nullopt` on any unrecognized `%`-code; `grep -c "sh -c\|system(\|popen("` on `src/DesktopEntry.cpp`/`src/BinaryScanner.cpp` returns 0. |
| 7 | T-7-04: TOCTOU-safe open()+fstat() pattern (never a separate stat() before open()) in the ELF read path | ✓ VERIFIED | `src/BinaryScanner.cpp:42-58` (readNeededLibraries) and `:222-235` (scanUsrBin) both `open()` then `fstat()` the fd before any decision. |
| 8 | T-7-06: Desktop/BinaryScan-sourced entries always launch via `execvp()`, never a shell, regardless of config; only Manual entries may opt into `execUsingShell` | ✓ VERIFIED | `WindowManager::launchApp()` (src/Manager.cpp:895-933): the `execl("/bin/sh", ...)` branch is gated exclusively on `entry.source == AppEntry::Source::Manual && m_config.execUsingShell`; all other cases fall through to `spawnArgv()` → `execvp()` (src/Manager.cpp:854-892). |
| 9 | D-03/D-04: hovering a category row opens a submenu without requiring a click first; clicking an app launches it; dismissing closes both popups cleanly | ✓ VERIFIED | `MotionNotify` case in `menu()` (src/Buttons.cpp:254-262) triggers `openCategorySubmenu()` on hover; live screenshots confirm submenu opens on hover (no click); `sel2` bounds-checked before `launchApp()` (src/Buttons.cpp:516-518, T-7-10); grab-transfer failure re-grabs the outer menu (T-7-09, src/Buttons.cpp:395-399). |

**Score:** 9/9 must-haves verified (0 present-but-behavior-unverified, 0 overrides).

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `include/AppEntry.h` | Shared `AppEntry` model, `Source` enum | ✓ VERIFIED | Present, X11-free (`grep -c "#include <X11"` = 0), matches spec exactly |
| `include/DesktopEntry.h` / `src/DesktopEntry.cpp` | XDG `.desktop` parser | ✓ VERIFIED | All 5 declared functions implemented; 11 `[desktopentry]` tests pass |
| `include/BinaryScanner.h` / `src/BinaryScanner.cpp` | ELF DT_NEEDED reader, GUI classifier, `/usr/bin` scan | ✓ VERIFIED | In-process mmap parsing, bounds-checked; 8 `[binaryscanner]` tests pass; live scan found 192 GUI binaries |
| `include/AppCache.h` / `src/AppCache.cpp` | Hand-rolled JSON cache, mtime invalidation, merge | ✓ VERIFIED | Round-trips confirmed by unit test + live appcache.json; 8 `[appcache]` tests pass |
| `include/Config.h` / `src/Config.cpp` (extended) | `manualMenuEntries`, `menu-entry-*` keys | ✓ VERIFIED | 5 new `[config]` tests pass (Tests 36-40); live manual entry confirmed rendering |
| `include/Manager.h` / `src/Manager.cpp` (extended) | `WindowManager(config, apps)`, `buildAppCategories()`, `spawnArgv()`/`launchApp()`, submenu window resources | ✓ VERIFIED | Constructor signature, category grouping, safe dispatch all present and correct |
| `src/Buttons.cpp` (extended) | Category rows, `openCategorySubmenu()` | ✓ VERIFIED | Category rows render correctly (left-aligned); submenu hover-open confirmed live |
| `src/main.cpp` | Full pipeline wiring | ✓ VERIFIED | `Config::load()` → `AppCache::loadOrRescan()` → `WindowManager(config, apps)`, confirmed by source read and live run |
| `CMakeLists.txt` | Test targets + real binary source list | ✓ VERIFIED | `wm2-born-again` target includes `AppCache.cpp`/`DesktopEntry.cpp`/`BinaryScanner.cpp` (src list, lines 25-27) — this was a real cross-plan integration bug caught and fixed in 07-06 (documented in its SUMMARY); confirmed fixed by inspecting the current file |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| `DesktopEntry::scanAll` | `DesktopEntry::parseFile` | directory walk | ✓ WIRED | src/DesktopEntry.cpp:321-345 |
| `DesktopEntry::parseFile` | `DesktopEntry::parseExec` | Exec= tokenization | ✓ WIRED | src/DesktopEntry.cpp:301-304 |
| `BinaryScanner::scanUsrBin` | `readNeededLibraries`/`isGuiBinary` | per-file classification | ✓ WIRED | src/BinaryScanner.cpp:247-253 |
| `AppCache::loadOrRescan` | `DesktopEntry::scanAll` + `BinaryScanner::scanUsrBin` | rescan path | ✓ WIRED | src/AppCache.cpp:415-424 |
| `Config::applyKeyValue` | `Config::manualMenuEntries` | `menu-entry-*` accumulator | ✓ WIRED | src/Config.cpp:191-225 |
| `main.cpp` | `AppCache::loadOrRescan` | startup wiring | ✓ WIRED | src/main.cpp:8 |
| `main.cpp` | `WindowManager(config, apps)` | constructor call | ✓ WIRED | src/main.cpp:9 |
| `Buttons.cpp (menu(), MotionNotify)` | `openCategorySubmenu` | hover trigger | ✓ WIRED | src/Buttons.cpp:254-261 |
| `openCategorySubmenu` | `launchApp` | app-row click dispatch | ✓ WIRED | src/Buttons.cpp:516-518 |
| `launchApp` | `spawnArgv`/`execvp` | safe process launch | ✓ WIRED | src/Manager.cpp:895-933 |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|---------------------|--------|
| `WindowManager::menu()` category rows | `m_appCategories` | `buildAppCategories()` groups `m_apps` (constructor param, from `AppCache::loadOrRescan()`'s real filesystem scan) | Yes — live run produced 307 real entries across 19 categories from a real `/usr/bin` + XDG scan on this machine | ✓ FLOWING |
| `openCategorySubmenu()` app rows | `category.second` | passed by reference from `m_appCategories` element | Yes — live screenshots show real app names (YouTube, Klasična GeoGebra, VerifyTestEntry) | ✓ FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Full project build | `cmake -S . -B /tmp/wm2-verify-build && cmake --build /tmp/wm2-verify-build -j4` | exit 0, `wm2-born-again` binary produced | ✓ PASS |
| Full test suite | `ctest` (in fresh build dir) | 136/136 passed | ✓ PASS |
| Root menu renders categorized rows on live Xvfb | `xdotool mousedown 1` + `xwd`/`convert` screenshot | Menu shows New + 19 categories (alphabetical, Custom last) | ✓ PASS |
| Hover-to-expand submenu (no click required) | `xdotool mousemove` over "Custom" row while button held | Submenu popup opened listing that category's apps | ✓ PASS |
| Manual config entry appears in menu | wrote `menu-entry-*` keys, restarted WM, re-screenshotted | "VerifyTestEntry" appeared in the live Custom submenu | ✓ PASS |
| appcache.json created with real scan data | `ls`/`python3 -m json` on live cache file | 307 entries (192 binaryscan + 115 desktop), 19 categories | ✓ PASS |
| No shell-exec path in parser modules | `grep -c "sh -c\|system(\|popen(" src/DesktopEntry.cpp src/BinaryScanner.cpp` | 0 matches | ✓ PASS |
| No subprocess spawn in BinaryScanner | `grep -c "fork()\|popen(\|system(" src/BinaryScanner.cpp` | 0 | ✓ PASS |
| No anti-pattern debt markers in phase files | `grep -n -E "TBD\|FIXME\|XXX\|TODO\|HACK\|PLACEHOLDER"` across all 13 phase-touched files | 0 matches | ✓ PASS |

### Requirements Coverage

| Requirement | Source Plan(s) | Description | Status | Evidence |
|-------------|-----------------|--------------|--------|----------|
| APPS-01 | 07-01, 07-06 | Parse XDG .desktop files for menu entries | ✓ SATISFIED | `DesktopEntry::scanAll()` implemented, tested, wired into real binary |
| APPS-02 | 07-02, 07-06 | Heuristic binary scan of /usr/bin for apps lacking .desktop files | ✓ SATISFIED | `BinaryScanner::scanUsrBin()` implemented, tested, live-confirmed (192 entries) |
| APPS-03 | 07-03, 07-06 | Cached results at ~/.config/wm2-born-again/appcache.json | ✓ SATISFIED | `AppCache` round-trip + mtime invalidation, live cache file confirmed |
| APPS-04 | 07-03, 07-06 | User can manually add/remove entries via config file | ✓ SATISFIED | `menu-entry-*` keys parsed, D-07/D-08 merge, live-confirmed rendering |
| APPS-05 | 07-04, 07-05, 07-06 | Root menu shows discovered apps organized by category | ✓ SATISFIED | Category grouping + hover submenu, live-confirmed screenshots |

No orphaned requirements — REQUIREMENTS.md's Phase 7 row set (APPS-01 through APPS-05) matches exactly what the plans' frontmatter `requirements:` fields declare.

### Anti-Patterns Found

None. Scanned all 13 phase-touched source/header files for `TBD`/`FIXME`/`XXX`/`TODO`/`HACK`/`PLACEHOLDER` and placeholder-language patterns — zero matches.

## Human Verification Required

None. All must-haves were independently verified via source inspection, a real (re-run, not reused) build + full test suite (136/136), and a live interactive Xvfb session driven directly by this verifier (screenshots captured, manual config entry added and confirmed rendering). This supersedes and independently corroborates the orchestrator's own Xvfb session referenced in the environment context.

### Gaps Summary

No blocking gaps. One documentation-wording issue was found and is recorded here for awareness, not as a functional gap:

**ROADMAP.md Success Criterion 1 says "Right-clicking the root window shows a menu..." but the actual (and correct) binding is left-click (`Button1`); right-click (`Button3`) triggers window circulation instead.** This is inherited, unmodified upstream wm2 behavior (`upstream-wm2/Buttons.C:25`) — no Phase 7 plan touched mouse-button bindings, and the feature itself (a categorized menu appearing on a root-window click) is fully implemented and verified working. The same "right-click" wording error also appears in `07-VALIDATION.md`'s manual-verification row, but the Phase 7 Plan 6 executor caught the discrepancy independently and correctly used left-click in its own `<what-built>`/`<how-to-verify>` text and live verification. Recommend a documentation-only follow-up to correct "Right-clicking" → "Left-clicking" (or "clicking") in ROADMAP.md Phase 7 Success Criterion 1 and in 07-VALIDATION.md; this does not block phase completion since it does not reflect a functional defect.

Separately, `.planning/ROADMAP.md` line 177 ("The AI scanner finds GUI applications...") and `.planning/REQUIREMENTS.md` line 76 ("AI-powered binary scan...") still use AI-framed language for the heuristic binary scanner, even though Plan 07-06's Task 3 (D-02) reworded the *other* "AI-powered" occurrences in PROJECT.md/ROADMAP.md to "heuristic-based." These two lines used different phrasing ("AI scanner" / not containing the literal substring "AI-powered") and were not caught by the plan's `grep -c "AI-powered"` acceptance check. Cosmetic only — the actual scanner is confirmed heuristic-only (substring match against known GUI toolkit libraries, no runtime LLM/network call, verified in `src/BinaryScanner.cpp`).

---

_Verified: 2026-07-08T10:05:50Z_
_Verifier: Claude (gsd-verifier)_
