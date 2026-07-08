---
phase: 07-root-menu-application-discovery
plan: 05
subsystem: root-menu-ui
tags: [x11, xft, wm2, root-menu, submenu, popup, event-loop]

# Dependency graph
requires:
  - phase: 07-04
    provides: "m_appCategories (alphabetical category buckets), m_submenuWindow/m_submenuDraw popup resources, launchApp() safe dispatch -- all consumed unchanged here"
provides:
  - "Category rows rendered in WindowManager::menu() (one per m_appCategories entry, left-aligned, positioned between hidden clients and Exit)"
  - "WindowManager::openCategorySubmenu(): hover-triggered second popup listing a category's apps, click-to-select loop, launchApp() dispatch"
affects: [07-06-main-startup-wiring]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Nested popup event loop: openCategorySubmenu() is a structural mirror of menu()'s own popup lifecycle (measure -> position -> map -> XftDraw bind -> grab -> event loop -> dispatch), reusing m_menuFont/m_menuFgColor/m_menuBgColor/m_menuHlColor -- no new font/color resources"
    - "Grab transfer: release the outer menu's XGrabPointer before attempting a new grab on the submenu window; on failure, re-grab the outer menu so the pointer is never left ungrabbed mid-interaction (T-7-09)"
    - "Two-level interaction resolves as a single unit: once a submenu opens, either an app launches or the user backs out, and both popups close together (both m_submenuWindow and m_menuWindow unmapped) rather than a full bidirectional grab-transfer state machine"

key-files:
  created: []
  modified:
    - include/Manager.h
    - src/Buttons.cpp

key-decisions:
  - "Extended openCategorySubmenu()'s declared signature from (category, e) to (category, e, outerX, outerY, outerMaxWidth, rowIndex): the outer menu's x/y/maxWidth and the currently-hovered row index are menu()'s own local stack variables, not recoverable from e (which holds the original click position, not the live hover position) or from any member-variable state. Passing them explicitly is the correct, robust way to anchor the submenu's position -- recomputing the outer menu's entire geometry a second time inside openCategorySubmenu() (reusing only e) was considered and rejected as fragile and duplicative."
  - "Corrected the Expose-loop right-alignment condition from `i >= nh` to `allowExit && i == n - 1`: under the old condition, every category row (also at index >= nh) would have been incorrectly right-aligned; only the true final Exit row right-aligns now"
  - "menuLabelFn() reorders its checks (idx==0 -> idx<nh hidden clients -> idx<nh+numCategories category names -> allowExit&&idx==n-1 exit label -> fallback) so the exit check uses the corrected n-1, which now already accounts for numCategories"
  - "Category-row selection has two entry paths: the primary hover-triggered one in MotionNotify (D-03's actual interaction requirement), and a defensive click-only fallback in the post-loop dispatch block for the rare case a ButtonRelease lands on a category row without a preceding MotionNotify"

requirements-completed: [APPS-05]

coverage:
  - id: D1
    description: "Root menu shows New, hidden clients, one row per category (alphabetical, Custom last), and Exit -- in that order, with only the Exit row right-aligned"
    requirement: "APPS-05"
    verification:
      - kind: other
        ref: "grep -c 'i >= nh' src/Buttons.cpp -> 0 (old incorrect condition removed); grep -c numCategories src/Buttons.cpp -> 5; real g++ -Wall -fsyntax-only compile exit 0; real CMake-driven object compile of Buttons.cpp.o exit 0"
        status: pass
      human_judgment: true
      human_judgment_note: "Deferred to Plan 07-06's checkpoint per this plan's own acceptance_criteria -- visual confirmation on Xvfb that category rows render correctly requires a running WM instance, out of scope until main.cpp is wired up"
  - id: D2
    description: "Hovering a category row opens m_submenuWindow listing that category's apps, without requiring a click first"
    requirement: "APPS-05"
    verification:
      - kind: other
        ref: "src/Buttons.cpp MotionNotify case: `if (selecting >= nh && selecting < nh + numCategories) { openCategorySubmenu(...); return; }`; grep -c m_submenuWindow src/Buttons.cpp -> 8"
        status: pass
      human_judgment: true
      human_judgment_note: "Deferred to Plan 07-06's checkpoint (manual UAT: right-click root on Xvfb, hover a category, confirm submenu appears)"
  - id: D3
    description: "Clicking an app row in the submenu launches it via launchApp(); dismissing either popup launches nothing and leaves the WM fully ungrabbed"
    requirement: "APPS-05"
    verification:
      - kind: other
        ref: "src/Buttons.cpp openCategorySubmenu(): sel2 bounds-checked against n2 before `launchApp(category.second[sel2])`; ButtonRelease case unconditionally calls releaseGrab()+XUnmapWindow() regardless of sel2; grep -c launchApp( src/Buttons.cpp -> 1"
        status: pass
      human_judgment: true
      human_judgment_note: "Deferred to Plan 07-06's checkpoint (manual UAT: click an app, confirm it launches and both popups close)"
  - id: D4
    description: "T-7-09: a failed submenu grab re-grabs the outer menu, never leaving the pointer ungrabbed"
    requirement: "APPS-05"
    verification:
      - kind: other
        ref: "src/Buttons.cpp openCategorySubmenu(): `if (attemptGrab(m_submenuWindow, ...) != GrabSuccess) { XUnmapWindow(...); attemptGrab(m_menuWindow, ...); return; }`"
        status: pass
      human_judgment: false
  - id: D5
    description: "T-7-10: submenu selection index is bounds-checked against the category's own app count before launchApp() dispatch"
    requirement: "APPS-05"
    verification:
      - kind: other
        ref: "src/Buttons.cpp: `if (sel2 >= 0 && sel2 < n2) { launchApp(category.second[sel2]); }`, mirroring the outer menu's existing `sel < 0 || sel >= n` pattern"
        status: pass
      human_judgment: false

duration: ~20min
completed: 2026-07-08
status: complete
---

# Phase 7 Plan 5: Category Rows + Submenu Popup in menu() Summary

**`WindowManager::menu()` now renders one left-aligned row per app category between hidden clients and Exit, with corrected right-alignment logic (only the true Exit row right-aligns); hovering a category row opens `openCategorySubmenu()` -- a second popup mirroring the outer menu's own lifecycle -- that lists the category's apps and dispatches a click to `launchApp()`.**

## Performance

- **Duration:** ~20 min
- **Tasks:** 2
- **Files modified:** 2 (`include/Manager.h`, `src/Buttons.cpp`)

## Accomplishments
- `menu()`'s row count (`n`) now includes `numCategories = m_appCategories.size()` before the `allowExit` adjustment, so category rows sit between the hidden-client rows and the (optional) Exit row
- `menuLabelFn()` resolves labels through explicit index-range checks (hidden clients -> categories -> exit), fixing the old `allowExit && idx > clients.size()` condition that would have matched every category row as "Exit"
- The Expose-loop right-alignment condition is corrected from `i >= nh` (which would incorrectly right-align every category row) to `allowExit && i == n - 1` (only the true final Exit row)
- `openCategorySubmenu()` is a new private method: computes its own width/height from the category's app names, positions itself to the right of the hovered row (flipping left if it would cross the screen's right edge), transfers the pointer grab from `m_menuWindow` to `m_submenuWindow`, runs a nested click-to-select event loop, and calls `launchApp()` on a valid selection
- Category-row selection has a primary hover-triggered path (`MotionNotify`, per D-03/D-04) and a defensive click-only fallback (post-loop dispatch), both calling `openCategorySubmenu()`
- Once a submenu resolves (launch or dismiss), `openCategorySubmenu()` unconditionally unmaps `m_menuWindow` too -- the two-level interaction closes as a single unit, and `menu()` returns immediately after `openCategorySubmenu()` returns

## Task Commits

Each task was committed atomically:

1. **Task 1: Category rows in menu() -- indices, corrected right-alignment, label rendering** - `80ba992` (feat)
2. **Task 2: openCategorySubmenu() -- hover-triggered second popup, click-to-select loop, launchApp dispatch** - `45c75a5` (feat)

**Plan metadata:** (this commit)

## Files Created/Modified
- `include/Manager.h` -- adds the `openCategorySubmenu()` private method declaration, near the existing `menu(XButtonEvent *e)` declaration
- `src/Buttons.cpp` -- `menu()` extended with category-row index math, corrected `menuLabelFn()` and Expose right-alignment logic, hover-triggered submenu open in `MotionNotify`, defensive click fallback in the post-loop dispatch block, and the full `openCategorySubmenu()` implementation

## Decisions Made
- **Signature extension (deviation from the plan's literal text, documented below):** the plan's `<interfaces>` section and Task 1's action text describe `openCategorySubmenu(category, e)` with two parameters. Implementing it that way is not achievable without either (a) storing the outer menu's `x`/`y`/`maxWidth`/hovered-row-index in member variables purely to thread them across a function boundary (worse: introduces unnecessary shared mutable state), or (b) recomputing the entire outer-menu geometry a second time inside `openCategorySubmenu()` from `e` alone -- fragile, since `e` holds the position from the *original click* that opened the outer menu (post-warp-adjustment), not the *live hover position* that determined which row is currently selected. Neither alternative is as robust as simply passing the four `int`s the function actually needs (`outerX`, `outerY`, `outerMaxWidth`, `rowIndex`). The acceptance criteria only grep for the substring `openCategorySubmenu` (count >= 1 in each file), which this signature still satisfies.
- Reused the outer menu's `nobuttons()` static helper and `MenuMask`/`MenuGrabMask` macros unchanged in the submenu's nested event loop, exactly as the plan specifies -- no new resources
- `entryHeight` inside `openCategorySubmenu()` is recomputed via the same formula the outer menu uses (`m_menuFont->ascent + m_menuFont->descent + 4`) rather than passed as a parameter, since it is deterministic given the shared `m_menuFont` and always produces an identical value to the outer menu's own `entryHeight`

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking issue] Extended `openCategorySubmenu()`'s signature beyond the plan's literal 2-parameter declaration**
- **Found during:** Task 1 (declaring the method) / Task 2 (implementing it)
- **Issue:** The plan specifies `void openCategorySubmenu(const std::pair<std::string, std::vector<AppEntry>>& category, XButtonEvent* e);` and describes positioning the submenu using "the outer menu's already-computed x/y/entryHeight" and "vertical offset `selecting * entryHeight`" -- but `x`, `y`, `maxWidth`, and `selecting` are all local stack variables of `menu()`, not accessible to a separate member function through `e` (which holds the stale original-click position, not the live hover position that determines `selecting`)
- **Fix:** Added four `int` parameters (`outerX`, `outerY`, `outerMaxWidth`, `rowIndex`) carrying exactly the geometry `openCategorySubmenu()` needs, passed from both call sites (`MotionNotify` hover path and the post-loop click fallback) using `menu()`'s own in-scope locals `x`, `y`, `maxWidth`, `selecting`
- **Files modified:** `include/Manager.h`, `src/Buttons.cpp`
- **Commits:** `80ba992` (declaration), `45c75a5` (call sites + implementation)

No other deviations -- all other `<action>` steps (index math, label lambda reordering, right-alignment fix, grab-transfer/re-grab-on-failure, bounds-checked `launchApp()` dispatch, unconditional close-both-popups-at-end) were implemented exactly as specified.

## Issues Encountered

**Sandbox environment gap (documented in 07-01 through 07-04 SUMMARYs, reused unchanged this plan): `libxft-dev` is not installed and `sudo apt-get install` is unavailable this session.**

Resolved using the same workaround established in Plan 07-04 (no code changes, no `sudo`, no root):
1. Real Xft/fontconfig/freetype/libpng/uuid/x11/xcb headers, already extracted via `apt-get download` + `dpkg-deb -x` at `/tmp/xft-extract/extracted`, were reused via `PKG_CONFIG_LIBDIR`/`PKG_CONFIG_SYSROOT_DIR`/`CPATH`/`LIBRARY_PATH` environment variables.
2. `g++ -std=c++17 -Wall -fsyntax-only` compiled `src/Buttons.cpp` against these real headers after each task: **exit 0, zero warnings**, both times.
3. Went further than syntax-checking: ran a real `cmake -S . -B <tmpdir>` configure (found real `x11`/`xext`/`xft`/`fontconfig` via pkg-config, exit 0), then built actual object files (`gmake -f .../build.make`) for `Buttons.cpp.o`, `Manager.cpp.o`, `Client.cpp.o`, `Border.cpp.o`, `Events.cpp.o` -- all compiled with **zero errors**.
4. `src/main.cpp` was not touched and was not built -- per the environment note, it is expected to fail (calls the old 1-argument `WindowManager` constructor) until Plan 07-06 wires up the 2-argument constructor call. This is not a regression.
5. Temp build/CMake directories were cleaned up after verification; nothing was installed system-wide, no `sudo` was used.

## User Setup Required
None -- no external service configuration required. (Same note as 07-04: installing `libxft-dev` on this dev machine, e.g. `sudo apt-get install libxft-dev`, would let future plans in this phase run the project's normal `cmake --build build` without the `/tmp` extraction workaround.)

## Next Phase Readiness
- `WindowManager::menu()` and `openCategorySubmenu()` are feature-complete for APPS-05's presentation layer; the only remaining piece of this phase is Plan 07-06's `main.cpp` startup wiring (merged `AppEntry` list into the 2-argument `WindowManager` constructor), which will finally make the whole binary link and runnable for manual UAT on Xvfb.
- Manual UAT (right-click root on Xvfb, hover a category, confirm submenu appears, click an app, confirm it launches and both popups close) is explicitly deferred to Plan 07-06's checkpoint, as this plan's own `<acceptance_criteria>` specifies -- `main.cpp` cannot currently link, so no running WM instance exists yet to test against.
- No blockers. The `libxft-dev` sandbox gap has a documented, reusable workaround that Plan 07-06 (and any later X11/Xft-touching plan) can reapply if the gap persists.

---
*Phase: 07-root-menu-application-discovery*
*Completed: 2026-07-08*
