---
phase: 09-config-gui-ipc
plan: 01
subsystem: config
tags: [xft, fontconfig, config-file, getopt_long, catch2, xvfb]

requires:
  - phase: 08.5-v1.0-closeout
    provides: "D-8.5-01 (a key name chosen before v1.0 is permanent, no aliases); the 08.5-06 guard-ordering rule for WM stderr in fixtures; the silver palette whose literals the runtime colour cases pin"
  - phase: 08-xrandr-vnc-compatibility-focus-rules
    provides: "the [wm_config_runtime] tag group and its rule that config is observed through the running binary, never through the parser; tests/support/WmFixture.h"
provides:
  - "`tab-font` config key and `--tab-font` flag, reaching Border::loadTabFont()'s preferred rung"
  - "`menu-font` config key and `--menu-font` flag, reaching the menu font load in WindowManager::initialiseScreen()"
  - "Config::tabFont and Config::menuFont, the two string members the Phase 9 GUI's Appearance page edits"
  - "Two [wm_config_runtime] differential cases proving each key reaches real drawn geometry, each proven non-vacuous by hunk revert"
  - "Two T-9-01/T-9-02 robustness cases: an unresolvable pattern never stops the window manager"
  - "docs/RELEASE-NOTES.md Appearance section naming both keys, with the next-start caveat written down"
affects: [09-02-appearance-page, 09-05-live-apply, 09-06-config-file-writer, 09-07-protocol, 09-08-gui]

actuals:
  tokens: 6100      # chars/4 over the realized diff (24,487 chars, 810e31be..HEAD)
  tasks: 3
  commits: 5

tech-stack:
  added: []
  patterns:
    - "A user-supplied fontconfig pattern reaches ONLY the preferred rung of a fallback ladder; every lower rung keeps its own literal"
    - "A config-to-runtime differential is proven non-vacuous by reverting its single production hunk and recording the reddened assertion"

key-files:
  created: []
  modified:
    - include/Config.h
    - src/Config.cpp
    - src/Border.cpp
    - src/Manager.cpp
    - tests/test_config.cpp
    - tests/test_wm_runtime.cpp
    - docs/RELEASE-NOTES.md

key-decisions:
  - "DISC-05: the key names are `tab-font` and `menu-font`, permanent under D-8.5-01, chosen because every existing key in this file is <subject>-<attribute>"
  - "DISC-05a: the value is a fontconfig pattern handed to XftFontOpenName unchanged; no separate size key, because the pattern already carries the size and two spellings could disagree"
  - "DISC-05b: each default is character-for-character the literal the binary hardcoded, so a user with no config file sees no change"
  - "The fallback ladder below the preferred rung stays hardcoded: a fallback the user can also break is not a fallback"
  - "The menu font keeps its fatal() on total failure; the menu measures every row against it, so there is no 'carry on without it' the way there is for the tab"

patterns-established:
  - "Fonts block in Config.h: the config home for values that used to be literals inside the code that draws with them"
  - "A new string setting is three rows in src/Config.cpp -- parser chain, kOptionSpecs, applyCliArgs -- not two; kOptionSpecs alone makes getopt accept the flag but never assigns it"

requirements-completed: [CGUI-03]

coverage:
  - id: D1
    description: "`tab-font` in a config file or on the command line changes the font the sideways tab label is drawn in, in a running window manager, with no source edit"
    requirement: CGUI-03
    verification:
      - kind: unit
        ref: "tests/test_config.cpp#tab-font defaults to the pattern Border::loadTabFont hardcoded"
        status: pass
      - kind: unit
        ref: "tests/test_config.cpp#applyKeyValue sets tab-font verbatim"
        status: pass
      - kind: unit
        ref: "tests/test_config.cpp#A tab-font value with spaces, commas and colons survives applyFile intact"
        status: pass
      - kind: unit
        ref: "tests/test_config.cpp#CLI --tab-font sets string value"
        status: pass
      - kind: e2e
        ref: "tests/test_wm_runtime.cpp#tab-font changes the thickness of the tab a real frame is built with"
        status: pass
    human_judgment: false
  - id: D2
    description: "`menu-font` changes the font the root menu is drawn in, in a running window manager"
    requirement: CGUI-03
    verification:
      - kind: unit
        ref: "tests/test_config.cpp#menu-font defaults to the pattern the menu font load hardcoded"
        status: pass
      - kind: unit
        ref: "tests/test_config.cpp#applyKeyValue sets menu-font verbatim"
        status: pass
      - kind: unit
        ref: "tests/test_config.cpp#tab-font and menu-font are independent settings"
        status: pass
      - kind: unit
        ref: "tests/test_config.cpp#CLI --menu-font sets string value"
        status: pass
      - kind: e2e
        ref: "tests/test_wm_runtime.cpp#menu-font changes the height of the root menu a real click opens"
        status: pass
    human_judgment: false
  - id: D3
    description: "`--help` lists `--tab-font` and `--menu-font` alongside the nine colour keys, because usage text and the getopt table come from one declaration"
    requirement: CGUI-03
    verification:
      - kind: integration
        ref: "build/debug/wm2-born-again --help 2>&1 | grep -c -e '--tab-font' -e '--menu-font'  # => 2"
        status: pass
      - kind: integration
        ref: "build/debug/wm2-born-again --menu-font=Monospace:size=24 --help  # => exit 0"
        status: pass
    human_judgment: false
  - id: D4
    description: "With no tab-font and no menu-font in any config layer, the window manager draws exactly the fonts it drew before this plan"
    requirement: CGUI-03
    verification:
      - kind: unit
        ref: "tests/test_config.cpp#tab-font defaults to the pattern Border::loadTabFont hardcoded (literal assertion, not a named constant)"
        status: pass
      - kind: unit
        ref: "tests/test_config.cpp#menu-font defaults to the pattern the menu font load hardcoded"
        status: pass
      - kind: e2e
        ref: "bash scripts/gates/build-all.sh debug  # 240 s, whole suite green including every pre-existing appearance case"
        status: pass
    human_judgment: false
  - id: D5
    description: "An unresolvable font pattern degrades rather than terminating the process (T-9-01, T-9-02)"
    verification:
      - kind: e2e
        ref: "tests/test_wm_runtime.cpp#An unresolvable tab-font still leaves the window manager framing windows"
        status: pass
      - kind: e2e
        ref: "tests/test_wm_runtime.cpp#An unresolvable menu-font still lets the window manager start"
        status: pass
    human_judgment: false
  - id: D6
    description: "docs/RELEASE-NOTES.md states the font chain as the default rather than the only option, names both keys, and records the next-start caveat"
    verification:
      - kind: integration
        ref: "comm -13 between the kOptionSpecs font rows and the release notes, run in both directions  # both empty"
        status: pass
    human_judgment: true
    rationale: "Parity greps prove the two key NAMES appear; they cannot judge whether the rewritten Appearance paragraph reads as intended, whether the caveat sentence is honest about what a user will experience, or whether the prose still contradicts itself somewhere. A human has to read it."

duration: 24 min
completed: 2026-09-06
status: complete
---

# Phase 9 Plan 01: Configurable tab and menu fonts Summary

**`tab-font` and `menu-font` config keys and CLI flags, defaulted to the exact literals the binary hardcoded, wired through to `Border::loadTabFont()`'s preferred rung and the menu font load, with two runtime differentials each proven non-vacuous by hunk revert.**

## Performance

- **Duration:** 24 min
- **Started:** 2026-09-06T00:11:00Z (approx; first commit 00:23:42Z)
- **Completed:** 2026-09-06T00:35:00Z
- **Tasks:** 3
- **Files modified:** 7

## Accomplishments

- **CONF-02's outstanding "fonts" box is closed.** Before this plan `grep -ci font src/Config.cpp` returned 0: the tab font was a string literal inside `Border::loadTabFont()` and the menu font a string literal inside `WindowManager::initialiseScreen()`, neither changeable without editing the source and recompiling. Both are configuration now, from the config file and from the command line.
- **Nothing changes for a user with no config file.** Both defaults are character-for-character the literals the binary carried, and both are asserted as literals in `tests/test_config.cpp` rather than against a named constant — so a silent drift of the shipped look fails a test instead of shipping.
- **Each key is proven to reach real drawn geometry, not just a struct member.** `tab-font` moves the tab button square from 8 px to 27 px and the client's horizontal inset from 24 px to 43 px at size 32 — both readings, taken independently from the X server, moving by the same 19 px, which is what says the tab's thickness changed and not the frame around it. `menu-font` moves the mapped root menu from 493 px to 1113 px tall.
- **Both differentials are proven non-vacuous.** Reverting only the production hunk each depends on reddens it: without the `src/Border.cpp` hunk the tab case fails at `8 > 8` and `24 > 24`; without the `src/Manager.cpp` hunk the menu case fails at `493 > 493`.
- **The fallback ladders are untouched.** `tab-font` reaches rung 1 (rotated, preferred) and rung 3 (unrotated, same preferred pattern) only. Rungs 2 and 4 keep their own literals, and the menu font keeps its generic-sans second rung and its `fatal()`. Two cases assert an unresolvable pattern still leaves the window manager alive and framing (T-9-01, T-9-02).
- **`--help` lists both flags** because the usage text is generated from the same `kOptionSpecs` table `getopt_long()` is handed, so the parser and the documentation cannot drift.

## Task Commits

1. **Task 1: `tab-font`, end to end — file to glyph** (tracer, tdd)
   - `534d1fb` (test) — failing unit + runtime coverage
   - `2ad3aad` (feat) — `Config::tabFont`, three rows in `src/Config.cpp`, both preferred-rung call sites in `src/Border.cpp`
2. **Task 2: `menu-font`, and the usage text that must list both** (tdd)
   - `7488aa6` (test) — failing unit + runtime coverage
   - `8dacac3` (feat) — `Config::menuFont`, three rows in `src/Config.cpp`, the first menu-font load in `src/Manager.cpp`
3. **Task 3: say so in the release notes, and prove the parity guard sees it**
   - `56c2353` (docs) — Appearance section rewritten

_TDD tasks carry two commits each (test → feat). No refactor commit was needed: the GREEN implementation is three one-line rows per key plus a named local, which has nothing to clean up._

## Files Created/Modified

- `include/Config.h` — new "Fonts" block after the colours, carrying `tabFont` and `menuFont`, with DISC-05/05a/05b and the D-8.5-01 permanence note recorded at the declaration
- `src/Config.cpp` — one parser row, one `kOptionSpecs` row and one `applyCliArgs` row per key
- `src/Border.cpp` — rungs 1 and 3 of `loadTabFont()` read the configured pattern through a named `preferred` local; rungs 2 and 4 unchanged
- `src/Manager.cpp` — the first menu-font load reads `m_config.menuFont`; the second rung and the `fatal()` unchanged
- `tests/test_config.cpp` — 9 new `[config]` cases (48 → 57 selected by the label)
- `tests/test_wm_runtime.cpp` — 4 new `[wm_config_runtime]` cases (7 → 11 selected by the label)
- `docs/RELEASE-NOTES.md` — Appearance section: the chain is now stated as the default, both keys named with their grammar and CLI spelling, the next-start caveat written down, and the configurable-colours sentence extended to the fonts

## Verification Run

| Check | Result |
|---|---|
| `ctest -L '^config$' --no-tests=error` | 52 → 57 tests, all pass (48 before the plan) |
| `ctest -L '^wm_config_runtime$' --no-tests=error` | 11 tests, all pass (7 before the plan) |
| `grep -c 'key == "tab-font"' src/Config.cpp` | 1 |
| `grep -c '"tab-font"' src/Config.cpp` | 3 (parser, kOptionSpecs, applyCliArgs) |
| `grep -c 'key == "menu-font"' src/Config.cpp` | 1 |
| `wm2-born-again --help \| grep -c -e '--tab-font' -e '--menu-font'` | 2 |
| `wm2-born-again --menu-font=Monospace:size=24 --help` | exit 0 |
| `grep -c -e 'tab-font' -e 'menu-font' docs/RELEASE-NOTES.md` | 4 |
| option-table ↔ release-notes parity, **both directions** | empty, empty |
| `bash scripts/gates/build-all.sh debug` | `build-all OK: debug` (240 s, whole suite) |

### Non-vacuity checks (plan-level verification)

Both differential cases were run with their single production hunk reverted and the rest of the change in place, so the flag still parses and simply never reaches the drawing code:

| Case | Hunk reverted | Reddened assertion | With the hunk |
|---|---|---|---|
| tab-font thickness | `src/Border.cpp` | `CHECK(large.buttonSize > shipped.buttonSize)` → `8 > 8`; `CHECK(large.horizontal > shipped.horizontal)` → `24 > 24` | button 8 → 27 px, inset 24 → 43 px |
| menu-font height | `src/Manager.cpp` | `CHECK(large > shipped)` → `493 > 493` | menu 493 → 1113 px |

Each file was restored from a copy kept under `build/debug/` and removed afterwards; no `git stash` was used at any point (the stash is shared across worktrees).

## Decisions Made

- **`tab-font` and `menu-font`** (DISC-05), permanent under D-8.5-01. Chosen over `font-tab`/`font-menu` because every existing key in this file is `<subject>-<attribute>` — `tab-foreground`, `menu-highlight`, `frame-background` — so these sort and read with their siblings.
- **Value grammar is a fontconfig pattern, verbatim** (DISC-05a). No XLFD, no separate size key: a size key would be a second way to say something the pattern already says, and the two could disagree.
- **Defaults are the previous literals** (DISC-05b), and they differ by exactly one token — the tab is `:bold`, the menu is not. That difference is deliberate and documented at the declaration: bold survives a RENDER-less remote server where lighter weights go ragged (measured in 08.5-02), and the tab label is the text that has to stay legible sideways at 12 px.
- **Only the preferred rung is configurable.** If `tab-font` fed rungs 2 and 3 as well, one bad value would take out the preferred face and every net under it at once — which is the outcome the XDIS-04 ladder exists to prevent. Rung 3 does read the configured pattern, because rung 3 *is* the preferred pattern unrotated; rungs 2 and 4 keep their literals.
- **The menu font keeps its `fatal()`.** The menu measures every row against it, so there is no "carry on without it" the way there is for an unlabelled tab. Only a host with no sans font at all reaches that exit — the same condition that already ended startup before this key existed.
- **The runtime tab measurement reads the button square and the client inset**, not the tab child window's own height. Both are exact functions of `Border::m_tabWidth` (`buttonDrawSize() == m_tabWidth - 8`, `xIndent() == m_tabWidth + FRAME_WIDTH + 1`), whereas the tab child's height folds in `m_tabHeight`, which `fixTabHeight()` derives from `m_tabWidth` in a branchy way. Two independent readings that must move by the same amount is also a stronger assertion than one reading that merely moves.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 — Missing Critical] A string setting needs a third row in `src/Config.cpp`, not two**

- **Found during:** Task 1 (and repeated in Task 2)
- **Issue:** The plan's action clause names two sites for each key — the `applyKeyValue()` parser chain and `kOptionSpecs`. `kOptionSpecs` alone only makes `getopt_long()` *accept* the flag; the value is assigned by a third site, the `std::strcmp` chain in `Config::applyCliArgs()`. Without it `--tab-font=...` would parse silently and be discarded, which is exactly the dead-config defect plan 08-07 found in the focus booleans — and the plan's own runtime differentials, which drive the WM with `--tab-font`/`--menu-font`, could never pass.
- **Fix:** Added the matching `applyCliArgs()` row for each key, immediately below the colour rows.
- **Files modified:** `src/Config.cpp`
- **Verification:** `CLI --tab-font sets string value` and `CLI --menu-font sets string value` unit cases; both `[wm_config_runtime]` differentials, which have no other way to reach the WM.
- **Committed in:** `2ad3aad`, `8dacac3` (part of the task commits)

**2. [Rule 3 — Blocking] Task 3's "one contiguous hunk" acceptance criterion is not reachable from its own action clause**

- **Found during:** Task 3
- **Issue:** The action clause requires editing two separated paragraphs of the Appearance section — the font paragraph near the section's top (line 71) and the configurable-colours sentence at its bottom (line 100), ~25 lines apart. `git diff` cannot render those as one hunk. The criterion's stated *intent* — "No line outside the Appearance section changed" — is what was checked instead.
- **Fix:** Verified the actual invariant. `git diff docs/RELEASE-NOTES.md` shows 2 hunks (3 at `-U0`) at lines 71, 76 and 117; the Appearance section runs from line 65 to line 122 (`## Focus behaviour` starts at 123). Every changed line is inside it. The Remote-desktop, Limitations and Resource-use sections are byte-identical.
- **Files modified:** `docs/RELEASE-NOTES.md`
- **Verification:** `git diff --stat` (22 insertions, 3 deletions, one file) plus the hunk-header line numbers against the section boundaries above.
- **Committed in:** `56c2353`

**3. [Rule 2 — Missing Critical] Threat-register mitigations T-9-01 and T-9-02 had no case**

- **Found during:** Tasks 1 and 2
- **Issue:** The plan's `<threat_model>` commits both mitigations to "a `[wm_config_runtime]` case that starts the window manager with a deliberately unresolvable pattern", but neither task's `<acceptance_criteria>` names one, so it would have been easy to ship the disposition without the evidence.
- **Fix:** Added `An unresolvable tab-font still leaves the window manager framing windows` and `An unresolvable menu-font still lets the window manager start`, each asserting `fixture.wmAlive()` and a real reparented frame.
- **Files modified:** `tests/test_wm_runtime.cpp`
- **Verification:** Both cases green in the `[wm_config_runtime]` label run; both are cheap (0.2–0.4 s).
- **Committed in:** `534d1fb`/`2ad3aad`, `7488aa6`/`8dacac3`

**4. [Rule 2 — Missing Critical] Added an independence case for the two keys**

- **Found during:** Task 2
- **Issue:** Every unit case the plan asks for would pass just as happily against a single shared member behind two key names.
- **Fix:** Added `tab-font and menu-font are independent settings`, which asserts the defaults differ and that setting each leaves the other alone.
- **Files modified:** `tests/test_config.cpp`
- **Verification:** Green in the `[config]` label run.
- **Committed in:** `7488aa6`/`8dacac3`

---

**Total deviations:** 4 auto-fixed (3 missing critical, 1 blocking). No Rule 4 architectural decisions arose.
**Impact on plan:** Deviation 1 was required for the plan's own verification commands to be able to pass at all. Deviations 3 and 4 add coverage the plan's prose commits to but its criteria omit. Deviation 2 is a bookkeeping correction to an unreachable criterion, with the stated intent verified instead. No scope creep: no file outside `files_modified` was touched.

## Issues Encountered

- **The RED commits do not compile, by construction.** `tests/test_config.cpp` names `cfg.tabFont` / `cfg.menuFont` before the GREEN commit introduces them, so `534d1fb` and `7488aa6` fail to build. That is a genuine RED for a compiled language and it is what the TDD protocol asks for, but it means `git bisect` across this plan will hit two non-building commits. Recorded rather than hidden. The runtime halves of both RED commits *do* compile and fail at runtime with `unrecognized option '--tab-font=…'` / `'--menu-font=…'`, which is the more informative RED of the two.
- **Nothing else.** No X protocol errors appeared in any fixture's stderr; the full three-tree gate was not needed beyond `debug`, which the plan's verification names.

## Flagged Assumption (carried from the plan's edge-probe accounting)

CGUI-03's edge remains `unresolved`. This plan assumes **"edit fonts" means editing the two fontconfig pattern strings the renderer already uses, and nothing more** — not font size as a separate control, not per-element fonts beyond tab and menu. If that reading is wrong, this plan is where the error entered, and the two key names are permanent under D-8.5-01. A reviewer who disagrees should say so before 09-02 builds the Appearance page against these two members.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- **Ready for 09-02** (Appearance page): `Config::tabFont` and `Config::menuFont` exist as plain `std::string` members with stable key names, which is what a `GtkFontButton` plus a raw-value field (D-10) binds to.
- **Ready for 09-06** (surgical config-file writer): both keys are ordinary string settings with no grouping state, so the writer's replace-in-place classification needs no special case for them.
- **Owed to 09-05** (live apply): the release-notes sentence *"A font change takes effect the next time the window manager starts"* is the exception this plan's prohibition required to be named. 09-05 must delete that paragraph when the live path lands, or the notes will be lying. It is the only setting in the release that does not apply immediately.
- **No blockers.**

## Self-Check: PASSED

- All 7 files in `key-files.modified` exist on disk and carry the changes described.
- All 5 commit hashes (`534d1fb`, `2ad3aad`, `7488aa6`, `8dacac3`, `56c2353`) are present in `git log`.
- Every `<acceptance_criteria>` item from all three tasks was re-run and passes (table above), including the two the plan asked to be recorded here: the hunk-revert reddening for both differentials, and the swapped-direction parity command.
- The plan-level `<verification>` is green: `bash scripts/gates/build-all.sh debug` → `build-all OK: debug`.
- No stubs, no skipped tests, no unrun `<verify>` commands.

---
*Phase: 09-config-gui-ipc*
*Completed: 2026-09-06*
