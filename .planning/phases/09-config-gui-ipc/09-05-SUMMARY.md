---
phase: 09-config-gui-ipc
plan: 05
subsystem: infra
tags: [live-config, x11, xft, fontconfig, colour-allocation, unix-socket, ipc, catch2, xtest]

requires:
  - phase: 09-config-gui-ipc
    provides: "09-04's `WindowManager::applyConfig()` -- DISC-06a's single funnel, which this plan filled in for the rest of the settable surface; also `applyConfigSet()`, `reloadConfigFromDisk()`, `m_savedConfig`, and `Border::relayoutForFrameThickness()`"
  - phase: 09-config-gui-ipc
    provides: "09-04's `configKeySpecs()` / `configKeySpecFor()` / `configValueForKey()` view of the option table, and `apps/wm2-ctl` as the suite's protocol client"
  - phase: 09-config-gui-ipc
    provides: "09-03's `ConfigSocketServer::broadcast()`, unused until now, and the shared descriptor set that makes the socket answer inside a modal grab"
  - phase: 09-config-gui-ipc
    provides: "09-02's frozen version-1 codec, whose `reloaded` type carries D-08's notice so no twelfth type was needed"
  - phase: 09-config-gui-ipc
    provides: "09-01's `tab-font` / `menu-font` keys and `Border::loadTabFont()`'s four-rung ladder, whose top three rungs the live reload walks"
  - phase: 08.5-v1.0-closeout
    provides: "`tests/support/PixelVerdict.h`'s positive dominance criterion, `tests/support/WmFixture.h`, `tests/support/XTestDriver.h`, and 08.5-02's derived bevel shades"
  - phase: 08-xrandr-vnc-compatibility-focus-rules
    provides: "08-07's focus-policy gates (the very booleans this plan proves are live) and 08-13/08-14's menu rebuild, whose defect class T-9-32 names"
provides:
  - "`Border::reloadColours()` and `Border::repaintForColourChange()` -- allocate-then-swap for the five pixels, two Xft colours, two derived bevel shades and three graphics contexts every frame draws with"
  - "`Border::reloadTabFont()` and `Border::relayoutForTabFont()` -- load-then-close, rungs 1 to 3 only, then the thickness path's own re-layout"
  - "`WindowManager::tryAllocateColour()` / `tryAllocateShadeOf()` -- the non-fatal forms `allocateColour()` and `allocateShadeOf()` now wrap"
  - "`WindowManager::reloadMenuColours()` and `reloadMenuFont()`"
  - "`WindowManager::applyConfig(const Config&, std::string&)` -- now returns bool and validates BEFORE it stores"
  - "`Client::relayoutFrameForFont()` and `Client::repaintForColourChange()`"
  - "`configMenuEntriesValue()` / `parseMenuEntriesValue()` / `kMenuEntriesKey` in include/Config.h -- D-12's whole-list grammar"
  - "`AppCache::loadAutoDiscovered()`, and the manual-entry merge moved from main.cpp into `WindowManager::rebuildAppCategoriesFromConfig()`"
  - "`WindowManager::m_menuOpen` / `m_appCategoriesStale` -- T-9-32's deferral"
  - "D-08's reload broadcast, carried by the already-frozen `reloaded` type"
  - "`WM2_FORCE_TAB_FONT_RELOAD_FAILURE` -- an internal test lever in the shape of the two `loadTabFont()` levers"
  - "23 more `[wm_config_live]` cases (16 -> 39); full debug suite 453 -> 476"
  - "docs/RELEASE-NOTES.md: no restart-only setting is named, because there is none"
affects: [09-06-menu-page, 09-07-protocol-client, 09-08-gui, 09-09-release]

actuals:
  tokens: 38000     # chars/4 over the realized diff (155,351 chars, 074c561~1..e615430)
  tasks: 3
  commits: 6

tech-stack:
  added:
    - "PkgConfig::XTST on test_wm_config_live only -- the wm2-born-again target still links no libXtst"
  patterns:
    - "Allocate-then-swap / load-then-close: a live resource change obtains every new value into a local before releasing one old one, so a failure leaves the running state entirely intact"
    - "Validate before store: applyConfig() moved from 09-04's store-first shape because a colour or a font CAN fail, and a refusal after the store would leave `get` reporting a value nothing is drawn in"
    - "A repeated group travels as one value in the file's own grammar, replaced wholesale -- idempotent by construction, and mapping onto a GUI's Add/Edit/Remove rows with no per-row protocol"
    - "Each colour key is sampled over the rectangle it OWNS, derived from the frame and client geometries rather than from a magic number"
    - "Every live-policy case carries a CONTROL half proving the file's value works on the SAME running process, so a negative cannot pass on a window manager whose feature never worked at all"
    - "A timing case pins every OTHER delay at the parser's maximum and moves exactly one, so the delay under test is the only thing that can have produced the change"

key-files:
  created: []
  modified:
    - src/Border.cpp
    - include/Border.h
    - src/Manager.cpp
    - include/Manager.h
    - src/Client.cpp
    - include/Client.h
    - src/Config.cpp
    - include/Config.h
    - include/ConfigProtocol.h
    - src/Buttons.cpp
    - src/AppCache.cpp
    - include/AppCache.h
    - src/main.cpp
    - apps/wm2-ctl/main.cpp
    - tests/test_wm_config_live.cpp
    - docs/RELEASE-NOTES.md
    - CMakeLists.txt

key-decisions:
  - "applyConfig() now returns bool and validates before it stores. 09-04 stored `next` first because its one live setting could not fail; a colour string is taken verbatim by the config parser and refused by the X SERVER, so storing first would leave `get tab-background` naming a colour nothing is drawn in"
  - "A nine-colour pre-flight runs before either palette reload commits, so the frame palette cannot end up new while the menu palette is old"
  - "Named-colour pixels are still never freed, matching every other allocation site in the codebase: on the TrueColor visuals every target uses they consume no colormap cell, and freeing them would be wrong as the code stands because two keys set to the same colour share one allocation"
  - "Rung 4 of the tab-font ladder (no face at all) is a legitimate degradation at STARTUP and a refusal at RELOAD time -- there is already a working face, and replacing it with nothing is a downgrade nobody asked for"
  - "The menu font gets no re-layout counterpart, and a comment says why: menu() measures every row and sizes the popup on every opening, so the next menu is in the new face by construction"
  - "Manual menu entries travel as ONE value in the config file's own key order, records separated by ';', replaced wholesale -- and `menu-entries` is deliberately NOT added to configKeySpecs(), which is a view of the option table's single settings and is asserted equal to the file writer's managed keys"
  - "The ';' separator has no escape: an escape needs a second grammar and a second grammar is a second thing to get wrong. A command containing ';' is written into the config file, where each key is its own line"
  - "D-08's notice is the already-frozen `reloaded` type, so version 1 gains no twelfth message; it carries its type and nothing else, and a client that wants a value asks for it"
  - "The manual-entry merge moved from main.cpp into the window manager, and AppCache::loadAutoDiscovered() was split out, because rebuilding the list means re-merging onto the auto-discovered entries and D-08's name match REPLACES rather than shadows -- they cannot be recovered from the merged list"
  - "A category-list rebuild is DEFERRED while a menu is open and performed by menu() before it assembles the next one, rather than mutating a list a modal loop is iterating (T-9-32)"
  - "D-06's per-setting next-start fallback was NOT exercised: every setting the GUI will offer applies live, and docs/RELEASE-NOTES.md names no exception"

patterns-established:
  - "The tab's TOP BAND, not its column, is where the tab background is alone on screen -- the column carries the label, and a long title puts enough ink on it that no single value dominates"
  - "A shaped window's bounding-minus-clip region is painted by the server from its BORDER pixel, which is where the `borders` key is visible on windows whose border WIDTH is zero; a live colour change must set the border pixel as well as the background"
  - "A focus case that re-enters the SAME window takes the pointer-stopped branch, because the window manager keeps motion selected on a window it has tracked once; a case about the auto-raise branch must enter a window that has never been a candidate"
  - "An internal test lever for a RELOAD path is a different lever from the one for the equivalent STARTUP path, or the process starts in the degraded state and the claim under test is about nothing"

requirements-completed: [CGUI-04]

coverage:
  - id: D1
    description: "Every one of the nine colour keys changes what is on screen the moment it is set: tabs, frames, buttons, borders and the root menu repaint with no window closing and no restart"
    requirement: CGUI-04
    verification:
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#every frame colour set over the socket repaints a window already on screen"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#every menu colour set over the socket reaches the next menu opened"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#the tab bevel is re-derived from the new tab background"
        status: pass
      - kind: other
        ref: "mutation check: with applyConfig()'s repaint loop disabled, exactly the three frame-colour cases redden and nothing else does"
        status: pass
    human_judgment: false
  - id: D2
    description: "Setting `tab-font` reloads the Xft face and re-lays out every tab already on screen, so the tab width changes for windows that were open before the change"
    requirement: CGUI-04
    verification:
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#setting tab-font re-lays out every tab already on screen"
        status: pass
      - kind: other
        ref: "mutation check: with the tab-font re-layout disabled, exactly that case reddens (1 of 40)"
        status: pass
    human_judgment: false
  - id: D3
    description: "Setting `menu-font` changes the next root menu's row height"
    requirement: CGUI-04
    verification:
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#setting menu-font changes the next root menu's row height"
        status: pass
    human_judgment: false
  - id: D4
    description: "The three focus booleans, focus-stealing prevention, the three delays, the new-window command and the shell flag all take effect at once, with no restart"
    requirement: CGUI-04
    verification:
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#setting click-to-focus stops the pointer alone from focusing"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#setting raise-on-focus false focuses a window without restacking it"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#setting auto-raise false stops the pointer consulting focus at all"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#setting focus-stealing-prevention false grants focus to a window that asked not to have it"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#setting auto-raise-delay changes how long the pointer must rest"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#setting pointer-stopped-delay changes how long stillness must last"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#setting destroy-window-delay changes what a held tab button does"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#setting new-window-command and exec-using-shell changes what the menu's New entry runs"
        status: pass
    human_judgment: false
  - id: D5
    description: "Adding, editing or removing a manual menu entry over the socket changes what the next root menu shows"
    requirement: CGUI-04
    verification:
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#a manual menu entry set over the socket appears in the next root menu"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#removing every manual entry removes its category from the next menu"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#setting the same menu-entries value twice changes nothing the second time"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#a malformed menu-entries value is refused and the menu is unchanged"
        status: pass
    human_judgment: false
  - id: D6
    description: "When the window manager re-reads its configuration from disk, every connected socket client is told, so an open GUI knows its picture is stale"
    requirement: CGUI-04
    verification:
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#a reload tells every client that completed a hello, and no stranger"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#a client that never reads does not stop the window manager reloading"
        status: pass
      - kind: other
        ref: "mutation check: with the broadcast commented out, exactly the broadcast case reddens (1 of 40)"
        status: pass
    human_judgment: false
  - id: D7
    description: "A font or colour value that cannot be resolved leaves the previous one in place and reports an error rather than leaving the window manager with nothing to draw with"
    requirement: CGUI-04
    verification:
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#a colour the X server cannot parse is refused and the screen is unchanged"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#a tab-font with no usable face is refused and every tab keeps its width"
        status: pass
      - kind: other
        ref: "bash scripts/gates/build-all.sh asan -- OK, no sanitizer findings (2 matched libfontconfig suppressions, pre-existing)"
        status: pass
    human_judgment: false
  - id: D8
    description: "docs/RELEASE-NOTES.md names no setting as taking effect only at next start, because none does"
    requirement: CGUI-04
    verification:
      - kind: other
        ref: "! grep -v '^[[:space:]]*#' docs/RELEASE-NOTES.md | grep -nF -e 'takes effect at next start' -e 'takes effect at the next start' -- exit 0"
        status: pass
      - kind: other
        ref: "the 09-01 sentence 'A font change takes effect the next time the window manager starts' is deleted; 'Which settings apply live' now reads 'all of them' with a table and no exception row"
        status: pass
    human_judgment: false
  - id: D9
    description: "A menu already open when the entries change is not disturbed, and the change is picked up by the next opening (T-9-32)"
    requirement: CGUI-04
    verification:
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#a menu held open across a menu-entry change is not disturbed"
        status: pass
    human_judgment: true
    rationale: "The case asserts what a user would see -- the open menu and its flyout keep their geometry, the window manager stays responsive, the next menu shows the change -- but a mutation check found it stays GREEN with the deferral removed, in both the debug and the ASan tree. The deferral is a structural mitigation whose absence this suite does not force red; see Evidence."

duration: 1h 40m
completed: 2026-09-06
status: complete
---

# Phase 09 Plan 05: Everything Applies Live Summary

**Every setting the configuration GUI will offer now changes the running desktop the moment it is set — nine colours repaint every frame, tab, button, outline and the next menu; `tab-font` re-measures and redraws every tab already open; the focus policy, the three delays, the two command settings and the manual menu entries all take effect at once — and `docs/RELEASE-NOTES.md` names no restart-only setting because there is none.**

## Performance

- **Duration:** 1h 40m
- **Started:** 2026-09-06T09:10Z
- **Completed:** 2026-09-06T10:50Z
- **Tasks:** 3 of 3
- **Files modified:** 17 (0 created, 17 modified)
- **Tests added:** 23 (`[wm_config_live]` 16 → 39); full debug suite 453 → 476, all green

## Accomplishments

- **D-06 is literally true.** There is no list of exceptions in the release notes because there is nothing to put in it. The nine colours, both fonts, the four focus booleans, the three delays, the two command settings and the manual menu entries each have a `[wm_config_live]` case proving they change a desktop that is *already open*. Every one of those cases maps its window (or opens its menu) **before** issuing the command, so a window manager that merely stored the value could not pass.
- **The prohibition is structural, not observed.** Every live resource change **allocates before it releases**: five pixels, two Xft colours, two derived bevel shades and three graphics contexts go into locals, and only a complete success frees a single old value. The font path is the same shape in the other direction — load, then close. `allocateColour()` still calls `fatal()` at startup, where a window manager with no frame colour has nothing to draw; the socket path goes through new non-fatal predicates and refuses instead. The ASan tree is clean.
- **`applyConfig()` had to stop storing first, and that is the plan's most consequential change to 09-04's shape.** A colour string is taken *verbatim* by the config parser and refused by the *X server*; a font pattern is refused by *fontconfig*. Neither can be validated by the parser, so a refusal after the store would have left `wm2-ctl get tab-background` naming a colour nothing is drawn in. The funnel now validates, then stores, then applies, and returns `false` having changed nothing at all.
- **The bevel follows the palette.** Set `tab-background` to `#400000` on a running desktop and the highlight becomes `#d1c2c2` — derived from the new body colour, measured, asserted, and with the shipped `#f2f3f3` asserted *absent*. That is the promise 08.5-02 wrote into the release notes ("a user who sets a dark palette gets bevels that belong to it") made true for a palette set at runtime rather than at startup.
- **The nine settings that were "read at the point of use" now have evidence.** Eight of the twelve Task 2 cases were green the moment they were written, because those values genuinely are read where they are used. That is precisely the class of claim plan 08-07 found to be **false** for three of these same booleans — they parsed and nothing read them — so each now carries a CONTROL half proving the file's value works on the same running process before the negative half is asserted.
- **An open GUI can no longer be silently out of date.** A successful reload broadcasts to every connection that completed a handshake, on `reloaded` — one of the eleven types the version-1 contract already froze, so nothing was added to it. The notice carries its type and nothing else; a client that wants a value asks for it, which is what stops it becoming a second, drifting copy of the settings.

## Task Commits

1. **Task 1 (tracer): colours change the screen** — `074c561` (test, RED) then `6903aff` (feat, GREEN)
2. **Task 2: fonts reload, policy and delays flip at once** — `6945245` (test, RED) then `bfd022d` (feat, GREEN)
3. **Task 3: the root menu rebuilds, every client learns the file changed** — `b1f974c` (feat) and `e615430` (test, hardening after a mutation check)

## Files Created/Modified

- `src/Border.cpp` / `include/Border.h` — `reloadColours()` (allocate-then-swap over the whole frame palette), `repaintForColourChange()` (background *and* border pixel, clear, then the existing paint path), `reloadTabFont()` (load-then-close over rungs 1–3), `relayoutForTabFont()` (delegates to the thickness path, then redraws the label).
- `src/Manager.cpp` / `include/Manager.h` — `tryAllocateColour()` / `tryAllocateShadeOf()`, `reloadMenuColours()`, `reloadMenuFont()`, `rebuildAppCategoriesFromConfig()`, `m_autoApps`, `m_menuOpen`, `m_appCategoriesStale`, the reload broadcast, the `menu-entries` arms of the `get`/`set` dispatcher, and `applyConfig()`'s new signature and validate-then-store order.
- `src/Client.cpp` / `include/Client.h` — `relayoutFrameForFont()` and `repaintForColourChange()`, each carrying the same three skip conditions `relayoutFrame()` carries, named individually.
- `src/Config.cpp` / `include/Config.h` — `configMenuEntriesValue()`, `parseMenuEntriesValue()`, `kMenuEntriesKey`, and the grammar's comment block.
- `include/ConfigProtocol.h` — the menu-entry value grammar recorded in the contract all three ends read.
- `src/Buttons.cpp` — the stale-list pickup and the `MenuOpenGuard` at the head of `menu()`.
- `src/AppCache.cpp` / `include/AppCache.h` — `loadAutoDiscovered()` split out; `loadOrRescan()` is now one line over it.
- `src/main.cpp` — hands the window manager the auto-discovered list; the merge moved inside.
- `apps/wm2-ctl/main.cpp` — `--help` names `menu-entries` and prints its grammar, spelled from the same constant the window manager compares against.
- `tests/test_wm_config_live.cpp` — 890 → 2,902 lines; 16 → 39 cases.
- `docs/RELEASE-NOTES.md` — the restart-only sentence deleted; "Which settings apply live" rewritten as a table with no exception row; the menu-entry grammar and the reload notice documented.
- `CMakeLists.txt` — `PkgConfig::XTST` on the test target only.

## Decisions Made

Beyond the frontmatter list:

- **The tab's TOP BAND is the tab background's own rectangle, not its column.** The first form of the colour case sampled the tab window and found it dominated by the *client's* white — the tab window spans the whole width of the frame. The second form sampled the column and found the background at 36% share, because a twenty-character label puts ~45% ink on it. The band between the two border rows is the only place the tab background is alone on screen, and it is *server*-painted from the window's background pixel, so a case asserting on it is asserting that the live path re-set that pixel and cleared the window — not merely that it redrew the label.
- **`borders` is a BORDER pixel on windows whose border width is zero.** For a shaped window the server paints the region between the bounding and the clip shape from the border pixel. That region is the tab's one-pixel top row and the ring around the tab button — the black outline the key names. `repaintForColourChange()` therefore calls `XSetWindowBorder()` as well as `XSetWindowBackground()`, and the case samples the tab's top row, where the border colour holds 100% of the sample.
- **A focus case that re-enters the same window tests the wrong branch.** The window manager keeps `PointerMotionMask` selected on a window it has tracked once, so a second entry delivers a `MotionNotify` and the decision goes to the pointer-stopped branch rather than the auto-raise one. Measured: the `auto-raise-delay` case, written as park-and-re-enter, never focused at all. It now enters a window that has never been a candidate.
- **The root menu shows CATEGORY labels, never entry names.** `menu()`'s outer rows are the New entry, the hidden clients, then `m_appCategories[i].first`. An entry's own name is drawn only in that category's flyout. So "the manual entry is present in the next root menu" is asserted as the *row the entry brings into that menu* — a manual entry in a category nothing else populates adds exactly one row of exactly that label's width, and removing it takes the row away again. Recorded as a reconciliation of the plan's "by label" wording rather than passed over.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 — Blocking] `applyConfig()` could not both refuse and keep 09-04's store-first order**

- **Found during:** Task 1, implementing the colour branch.
- **Issue:** 09-04's funnel is `void` and stores `next` before it diffs, documented as the rule that "a later plan adding a branch only has to write the application, never the assignment". That holds only for settings that cannot fail. A colour is taken verbatim by `Config::applyKeyValue()` and refused by the X server; storing first and failing second would leave `m_config` carrying a colour nothing is drawn in, and `get` reporting it.
- **Fix:** `applyConfig(const Config&, std::string&) -> bool`. Everything that can fail runs first, against the previous state; the store happens only once nothing left can fail; a failure returns with a reason having changed nothing. Both callers (`applyConfigSet()`, `reloadConfigFromDisk()`) pass the reason straight through, so a client is told which key and why.
- **Verification:** The refusal cases assert the screen is byte-identical and `get` still reports the old value; `ctest -L wm_config_live` 39/39; both gates green.
- **Committed in:** `6903aff`.

**2. [Rule 3 — Blocking] The category list cannot be rebuilt from what the window manager held**

- **Found during:** Task 3.
- **Issue:** The window manager received the *merged* app list from `main.cpp`. Re-running the merge with new manual entries needs the *auto-discovered* list, and D-08's name-match rule REPLACES an auto-discovered entry rather than shadowing it — so the original is gone from the merged list and cannot be recovered.
- **Fix:** `AppCache::loadAutoDiscovered()` split out of `loadOrRescan()` (which is now one line over it, so every existing `[appcache]` case is untouched); `main.cpp` passes the auto-discovered list; `WindowManager::rebuildAppCategoriesFromConfig()` performs the merge through `AppCache::mergeEntries()` — the same merge, not a second one — at startup and again whenever the entries change.
- **Files modified:** `src/AppCache.cpp`, `include/AppCache.h`, `src/main.cpp`, `src/Manager.cpp`, `include/Manager.h`. The first three are outside the plan's `files_modified`.
- **Verification:** `ctest -L appcache` green; the menu-entry cases assert the next menu changed.
- **Committed in:** `b1f974c`.

**3. [Rule 2 — Missing critical] The tab-font refusal path was unreachable, and therefore untestable**

- **Found during:** Task 2.
- **Issue:** The plan requires that "a `tab-font` value fontconfig cannot resolve is refused with an error naming the key; the previous face stays loaded". fontconfig *substitutes* for a family it does not have rather than failing — which is what XDIS-04 wants of it, and which means no string a user can type reaches the bottom of the ladder. The guarantee would have shipped as code no case could reach.
- **Fix:** `WM2_FORCE_TAB_FONT_RELOAD_FAILURE`, an internal test lever in the same shape as `WM2_FORCE_NO_TAB_FONT` and `WM2_FORCE_NO_ROTATED_TAB_FONT` — read once, no config key, no command-line flag, undocumented for users. Deliberately a *different* lever from those two: they would leave the process with no face to begin with, and "the previous face stays loaded" would then be a claim about nothing.
- **Verification:** The refusal case asserts exit 1, the error naming `tab-font`, the tab width unchanged, `get tab-font` unchanged, and that the window manager still frames a new client afterwards.
- **Committed in:** `bfd022d`.

**4. [Rule 1 — Bug] The held-open menu case could not fail as first written**

- **Found during:** Task 3, mutation-checking.
- **Issue:** The case opened the root menu, changed the entries, and asserted the open menu's geometry was unchanged. With the deferral removed (`if (false && m_menuOpen)`) it stayed **green**: it never opened a category flyout, so nothing in the window manager was holding a pointer into the list being rebuilt.
- **Fix:** `openAnySubmenu()` sweeps the pointer down the open menu until a flyout appears (swept rather than computed: the row index depends on the menu font's entry height and on how many categories the host's application scan produced). The mid-loop change now REMOVES a category rather than adding one — `menu()` reads the category count into a local before its loop, so a list that grew is merely stale while a list that shrank is an out-of-bounds read. The pointer is then moved inside the flyout, because a dangling pointer is only a fault when it is followed.
- **Verification:** Green in both trees. It still does not redden under the mutation — see Evidence.
- **Committed in:** `e615430`.

### Planned-scope adjustments

**5. [Reconciliation] The `borders`, `tab-background` and menu-colour sample rectangles**

The plan asks for "a dominant-value change in the rectangle that key owns" per colour key. Three of the nine needed the rectangle worked out rather than assumed, and each is documented at its assertion: the tab background owns its top band (not the column, which carries the label); the frame background owns the five-pixel clip strip between tab and client (not the whole frame, which is mostly the client's own white); `borders` owns the tab window's one-pixel top row. All nine assertions are dominance assertions at `kDominanceFloor`, except `tab-foreground`, which the plan itself specifies as an ink-presence/ink-absence pair.

**6. [Reconciliation] "Present in the next root menu, by label"**

`menu()`'s outer rows are category labels; an entry name is drawn only in that category's flyout, whose row index cannot be computed from outside. The case asserts the row the entry *brings into* the root menu — a manual entry in a category nothing else populates adds exactly one row of exactly that label's width — plus wholesale replacement through `get menu-entries`. See "Decisions Made".

**7. [Scope] The release notes gained more than the deleted sentence**

The plan's prohibition is that no restart-only setting go unnamed. The pre-existing "Which settings apply live" paragraph said `frame-thickness` was the only live setting and that the rest "land in the following release" — false as of `6903aff`. It was rewritten as a table with no exception row, and the menu-entry grammar and the reload notice were documented alongside, because a user reading the notes now has two new things they can do.

**8. [Not exercised] D-06's per-setting next-start fallback**

CONTEXT.md permits one setting to fall back to next-start if its live path proves unsafe, provided it is named in the release notes and recorded here. **No setting needed it.** Every one applies live; the release notes name no exception; there is nothing to record but its absence.

---

**Total deviations:** 4 auto-fixed (1 × Rule 1, 1 × Rule 2, 2 × Rule 3) plus 4 recorded scope/reconciliation adjustments.
**Impact on plan:** No scope creep. The two blocking fixes were both required for claims the plan itself makes; the largest (`applyConfig()`'s signature) is a correction to an assumption 09-04 could not have known was load-bearing until a failable setting arrived.

## Evidence

**Mutation check 1 — the colour repaint.** With `applyConfig()`'s `repaintForColourChange()` loop disabled:

```
170 - every frame colour set over the socket repaints a window already on screen (Failed)
183 - setting the same colour twice leaves the capture byte-identical (Failed)
187 - the tab bevel is re-derived from the new tab background (Failed)
```

Exactly the three frame-side colour cases. The menu-colour case stayed green, correctly: menu colours reach the screen through `reloadMenuColours()` and the popup's own repaint on the next opening, not through the per-client loop.

**Mutation check 2 — the tab-font re-layout.** With that branch disabled:

```
190 - setting tab-font re-lays out every tab already on screen (Failed)
98% tests passed, 1 tests failed out of 40
```

**Mutation check 3 — the reload broadcast.** With `m_socketServer.broadcast(...)` commented out:

```
184 - a reload tells every client that completed a hello, and no stranger (Failed)
```

**Mutation check 4 — the menu-open deferral. NEGATIVE, and recorded as such.** With `if (m_menuOpen)` replaced by `if (false && m_menuOpen)`, the held-open case stayed **green** — in its original form, after being strengthened to open a real category flyout, after the mid-loop change was switched from adding a category to removing one, and after the pointer was moved inside the flyout to force a row repaint. It stayed green in the ASan tree too, with no sanitizer report written.

The likely reason is that `rebuildAppCategoriesFromConfig()` clears and refills the same `std::vector` buffer, so the memory the flyout's `subEntries` pointer names is reused rather than freed and ASan sees no violation; the refilled contents differ from the old ones by one entry, which is not enough to change what the flyout draws.

**What this means, stated plainly: the deferral is a structural mitigation for T-9-32 whose absence this suite does not force red.** What *is* asserted is the user-visible half — the open menu and its flyout keep their geometry across the change, the window manager stays responsive and frames a window afterwards, and the next menu opened shows the new list. `coverage` entry D9 is marked `human_judgment: true` for exactly this reason, and the gap is recorded in `.planning/WINDOWS.md`.

**Task acceptance criteria, run literally.**

| Criterion | Result |
|---|---|
| a case per colour key, each asserting a dominant-value change in its own rectangle | 9 keys across 2 cases + the bevel case |
| an unparseable colour refused, screen unchanged | pass (`after == before`, byte-identical histogram) |
| a repeated identical `set` leaves the capture byte-identical | pass |
| the bevel highlight after a tab-background change is derived from the NEW background | pass (`#400000` → `#d1c2c2`; `#f2f3f3` absent) |
| a `tab-font` case asserting BOTH tab widths changed | pass, and by the same amount |
| an unresolvable `tab-font` refused, widths unchanged, WM still framing | pass |
| a case per focus boolean, per delay, and for both command settings | 8 cases |
| release notes contain no restart-only claim | pass (grep gate exits 0) |
| a menu-entry `set` present in the next root menu | pass (one more row, wider) |
| a menu held open across a change undisturbed, WM responsive | pass |
| two clients, one hello, exactly the greeted one told | pass |
| `include/ConfigProtocol.h` states the menu-entry value grammar | pass |

**Gates.**

| Gate | Result |
|---|---|
| `ctest -L '^wm_config_live$'` | **39/39** (was 16) |
| `ctest -L '^(menupaint\|wm_menureopen\|wm_menuback\|wm_menulabel\|wm_focus\|wm_config_runtime\|autoraise\|wm_socket\|wm_bevel\|wm_geometry\|wm_tablabel)$'` | **86/86** |
| `bash scripts/gates/build-all.sh debug` | **476/476, OK** (baseline 453 + 23) |
| `bash scripts/gates/build-all.sh asan` | **OK, no sanitizer findings** (2 matched libfontconfig suppressions, pre-existing) |
| release-notes restart-only grep gate | exit 0, no match |

## Issues Encountered

- **A mutation check that comes back negative is still a result.** Mutation 4 above is the honest one: the guard it tests is correct and the case that names it cannot fail without it. Recorded rather than dressed up, and the coverage entry routes it to a human.
- **The `menu-entries` read-back check had to be a ROUND TRIP.** The settings path compares what the parser stored against the canonical form of what was requested. That does not work for a list: a value that omits a category, or separates command tokens with runs of spaces, is legitimate and stored in canonical form. The check is instead "render the stored list, parse it again, and require the same list" — which still fails closed if the renderer and the parser ever come to disagree.
- **The pointer-stopped and auto-raise branches are hard to isolate.** Both delay cases pin the *other* delay at the parser's maximum (60,000 ms) for the whole case, so the delay under test is the only thing that can have produced the focus change. Without that, a case can pass on the wrong branch and nobody would know.

## Known Stubs

None.

Two things a reader might mistake for gaps, both deliberate and both named above:

- **The menu-open deferral has no mutation-proof case.** See Evidence, mutation 4, and coverage entry D9. The guard stays: the pointer it protects is genuinely dangling without it, and "ASan happened not to notice" is not a reason to remove a correctness measure.
- **`configKeySpecs()` still does not name `menu-entries`.** That is the design, not an omission: the view is of the option table's single settings and is asserted equal to `configFileManagedKeys()`, and the file writer emits the three accumulator keys rather than a `menu-entries=` line. The key is documented in `include/ConfigProtocol.h` and printed by `wm2-ctl --help`.

## Threat Flags

None. Every trust boundary this plan opens was in the plan's `<threat_model>`.

- **T-9-26** (a colour or font failing to resolve after the old one was released) — mitigated structurally by allocate-then-swap and load-then-close, asserted by two refusal cases that check the screen is byte-identical and the window manager still frames, and by a clean ASan tree.
- **T-9-27** (repeated font reloads leaking faces) — mitigated: exactly one face is closed per swap, and the diff means an unchanged value does no work. ASan clean.
- **T-9-28** (a menu entry introducing a command the user did not type) — unchanged: the list goes through `Config::applyKeyValue()`'s existing accumulator, which tokenises on whitespace and does not shell-evaluate. The socket is still reachable only by the user's own uid.
- **T-9-29** (setting `exec-using-shell` over the socket) — same reachability argument; the setting stays documented as shell-evaluated, and the command case asserts both halves of its meaning.
- **T-9-30** (a broadcast blocking on a client that is not reading) — asserted: a hello-completed connection that never reads sits across five reloads and the window manager keeps applying them.
- **T-9-31** (the notice carrying configuration values) — asserted field by field: type set, every other member empty.
- **T-9-32** (mutating the category list while a modal loop iterates it) — mitigated by the deferral; asserted in the user-visible sense only. See Evidence.

## User Setup Required

None.

## Next Phase Readiness

Ready for **09-06 (menu page)**, **09-07 (protocol client)** and **09-08 (GUI)**:

- Every key the GUI edits applies live, so the GUI's job is to send values, not to explain to the user why nothing happened.
- `menu-entries` gives the menu page a single read and a single write: `get` returns the whole list, `set` replaces it. Add, Edit and Remove are all "send the list back, changed" — no per-row protocol, no row identity to keep in sync.
- The reload broadcast is the signal an open settings window needs (D-08). It carries nothing, so the client re-reads what it cares about.
- `savedConfig()` is still the revert source DISC-07 promised, and is still unread by any message — 09-08 owns that.

No blockers.

---
*Phase: 09-config-gui-ipc*
*Completed: 2026-09-06*

## Self-Check: PASSED

Every file named in `key-files.modified` exists on disk; every commit hash named in
`## Task Commits` resolves; every symbol named in `provides` is present in `src/` or
`include/`. All 39 `[wm_config_live]` cases and both build-all gates were re-run after
the last commit.
