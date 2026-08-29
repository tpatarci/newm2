---
phase: 08-xrandr-vnc-compatibility-focus-rules
plan: 13
subsystem: testing
tags: [process-level-tests, config-to-runtime, terminating-error-paths, stress, asan, shape, event-loop, cli, denial-of-service, elevation-of-privilege]

requires:
  - phase: 08-xrandr-vnc-compatibility-focus-rules/08-01
    provides: "tests/support/WmFixture.h, tests/support/XTestDriver.h and tests/lsan.supp -- the process-level harness all 16 cases run on"
  - phase: 08-xrandr-vnc-compatibility-focus-rules/08-02
    provides: "scripts/gates/build-all.sh and scripts/analysis/run-static-analysis.sh -- where this plan's evidence comes from"
  - phase: 08-xrandr-vnc-compatibility-focus-rules/08-07
    provides: "the spaced settleWm() methodology (deferred item 9), and the focus-flag half of checklist coverage item 5"
  - phase: 08-xrandr-vnc-compatibility-focus-rules/08-10
    provides: "the class-hint reader idiom, used here to identify which program the WM spawned"
  - phase: 08-xrandr-vnc-compatibility-focus-rules/08-11
    provides: "the X-protocol-error assertion idiom, which is the ONLY thing that could have seen the SHAPE defect fixed here"
  - phase: 08-xrandr-vnc-compatibility-focus-rules/08-12
    provides: "deferred item 14, handed to this plan; and the both-trees mutation discipline, which this plan needed again"
provides:
  - "tests/test_wm_runtime.cpp and the test_wm_runtime target: 16 process-level cases in four groups ([wm_config_runtime], [wm_errors], [wm_stress], [wm_harness])"
  - "Checklist coverage items 5, 6 and 13 closed -- and with them ALL THIRTEEN missing-coverage items the phase committed to (D-09)"
  - "Four further checklist entries closed as a by-product: repeated spawn() leaves no zombies, exec-using-shell's off-by-default shell-evaluated behaviour, and frame thickness at minimum/default/maximum"
  - "A SHAPE ordering fix: the frame was left UNSHAPED at every frame thickness except the shipped default"
  - "An event-loop fix: Client::unreparent() discarded the WM's ENTIRE queued event backlog on every client teardown"
  - "A memory-safety fix: Client::getColormaps() installed uninitialised colormap XIDs"
  - "A working --help flag, and kOptionSpecs as the single declaration of the CLI surface"
  - "deferred item 14 RESOLVED, with a regression test that covers the wiring and not just the helper"
  - "deferred items 15 and 16 recorded"
affects: [08-14, any later work touching Border's shape rectangles, Client::unreparent(), Client::getColormaps() or src/Config.cpp's option table]

actuals:
  tokens: 96000
  tasks: 3
  commits: 10

tech-stack:
  added: []
  patterns:
    - "Reading colour back from the SERVER with XGetImage over ROOT rather than over the window, because the frame and tab are SHAPED and XGetImage outside a bounding shape is undefined"
    - "Turning a colour assertion from 'the two runs differ' into an absolute one by asking the server what pixel it resolves the configured colour NAME to"
    - "Choosing the setting whose runtime effect is an EXACT function of its value (yIndent() == FRAME_WIDTH + 1) so config-to-runtime can be asserted absolutely instead of by comparison"
    - "Pairing every negative security assertion with a positive control that shares the same command string, so the negative cannot pass because the mechanism was broken"
    - "Observing 'the WM has DRAWN this window' from outside as 'the rectangle is no longer one flat colour'"
    - "Installing a counting, non-fatal Xlib error handler in a test that walks the window tree -- the default one calls exit(1) and turns a race into an unexplained process death"
    - "Deriving a getopt_long() array AND the usage text from one table of records, and deriving the --no- negations rather than listing them"

key-files:
  created:
    - tests/test_wm_runtime.cpp
  modified:
    - CMakeLists.txt
    - src/Border.cpp
    - src/Client.cpp
    - src/Config.cpp
    - include/Border.h
    - tests/support/WmFixture.h
    - COMPILED_CODE_BEHAVIOR_CHECKLIST.md
    - .planning/phases/08-xrandr-vnc-compatibility-focus-rules/deferred-items.md

key-decisions:
  - "The frame-thickness case asserts ABSOLUTELY, not by comparing two runs. Border::reparent() places the client at yIndent() == FRAME_WIDTH + 1, so the vertical inset is an exact function of the setting with no font term in it. The plan's own assumption note anticipated only run-comparison; the absolute form is strictly stronger and it is what surfaced the SHAPE defect, because it forced non-default thicknesses to be exercised at all."
  - "The SHAPE fix SORTS rather than switching the ordering hint to Unsorted. Both produce the identical region, but the sort keeps the server's fast path and, being STABLE, passes every already-correct list through byte for byte -- so the fix provably cannot change rendering that was already right."
  - "Client::unreparent()'s XSync discard flag was changed rather than the call removed. The sync itself is load-bearing (it orders the reparent and border-width restore against the destruction that follows); only the 'throw away every queued event' argument was wrong."
  - "getColormaps() falls back to None rather than to the default colormap id. WindowManager::installColormap(None) ALREADY means 'install the default', so None keeps the policy in one place instead of duplicating it at the read site."
  - "X_SetInputFocus BadMatch is BOUNDED in the churn case, not excluded. Client::activate() already guards on managed/hidden/withdrawn; the residue is an irreducible race no query can close. A bound keeps the case able to notice a focus-path regression that would make the count scale with the churn."
  - "The stress memory budget was chosen and written down BEFORE the first run (8 MB debug, 32 MB ASan), with the reasoning recorded, and the baseline, budget and result are printed separately on every run so nobody can later mistake the budget for something derived from the result. Measured 536 kB and 3720 kB."
  - "The colormap-windows sub-case was folded INTO the churn case rather than added as a sixteenth. It exists only because mutation M4 survived, and it is the same claim the churn case already makes -- the WM holding a reference to a window a client can destroy at any moment."
  - "TEST-05 adjudicated as genuinely satisfied. See the section below; 08-12's note that it was still unmarked was wrong about the ledger."

patterns-established:
  - "Mutation testing carried forward: 9 production/harness branches deleted in turn, ALL 9 reddened a named case -- but only after two of them were first green and the tests were strengthened"
  - "Mutations run in BOTH trees again, and again it mattered: M4 is green in debug and red under ASan, exactly as 08-12 warned"
  - "A mutation that survives is treated as an UNCOVERED BRANCH until proven otherwise. Neither survivor here turned out to be an equivalent mutant; one was a coupling gap in production code and one was a missing test"

requirements-completed: [TEST-05]

coverage:
  - id: D1
    description: "Frame thickness reaches the runtime and changes measured frame geometry, at minimum, default and maximum"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_runtime.cpp#Frame thickness changes the measured frame geometry in both directions -- absolute (yIndent == FRAME_WIDTH + 1) at 3, 7 and 20; mutation M1 reddens"
        status: pass
    human_judgment: false
  - id: D2
    description: "Tab and menu colour settings reach rendered pixels, verified against the pixel the server resolves the configured colour name to"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_runtime.cpp -- 2 [wm_config_runtime] cases, XGetImage over ROOT; the shipped run must contain NONE of the configured colour"
        status: pass
    human_judgment: false
  - id: D3
    description: "destroy-window-delay decides whether a tab-button press hides or deletes, in both directions"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_runtime.cpp#destroy-window-delay decides whether a tab-button press hides or deletes -- real XTEST press, held past the configured threshold and released before it"
        status: pass
    human_judgment: false
  - id: D4
    description: "new-window-command decides which program the root menu's New entry starts, identified by the class hint of the window that appears"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_runtime.cpp#new-window-command decides which program the menu's New entry starts"
        status: pass
    human_judgment: false
  - id: D5
    description: "With exec-using-shell off, a command containing shell metacharacters is not evaluated by a shell and nothing at all is executed (threat T-8-SHELL)"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_runtime.cpp#exec-using-shell gates whether a command with arguments and metacharacters is evaluated by a shell -- filesystem witness, with the shell-on run as positive control; mutations M6 and M7 redden"
        status: pass
    human_judgment: false
  - id: D6
    description: "A setting on the command line overrides the same setting in the config file, observed through the running binary"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_runtime.cpp#A setting given on the command line overrides the same setting in the config file -- with a file-only control proving the file is read at all"
        status: pass
    human_judgment: false
  - id: D7
    description: "All four terminating X11 initialisation paths exit non-zero, name their own cause on stderr, and do so within a deadline"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_runtime.cpp -- 5 [wm_errors] cases (no display, invalid colour, no font, root-redirect conflict, and a dedicated tight-deadline case over all four)"
        status: pass
    human_judgment: false
  - id: D8
    description: "A second window manager fails cleanly and the incumbent is still WORKING afterwards, not merely still running"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_runtime.cpp#A second window manager fails cleanly and leaves the incumbent working -- the incumbent frames and publishes a fresh client after the conflict"
        status: pass
    human_judgment: false
  - id: D9
    description: "The binary's help flag prints usage and exits successfully with no DISPLAY, and its unrecognised-option advice names a flag that actually works"
    verification:
      - kind: e2e
        ref: "tests/test_wm_runtime.cpp -- 2 [wm_errors] cases; the second FOLLOWS the advice line's own text rather than a literal; mutations M5, M7 and M9 redden"
        status: pass
    human_judgment: false
  - id: D10
    description: "120 windows created, mapped, unmapped and destroyed in interleaved overlapping subsets leave no stale client-list entry, no dangling active window, a still-correctly-framing WM, bounded resident memory and no zombie children -- under the sanitizer"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_runtime.cpp#Repeated create, map, unmap and destroy over 120 windows -- RED before the fixes, MEASURED 17 of 20 destroyed windows stuck in _NET_CLIENT_LIST; mutations M2, M3 and M4 redden"
        status: pass
      - kind: manual_procedural
        ref: "scripts/gates/build-all.sh: 283/283 in debug, release and asan; asan reports no sanitizer findings"
        status: pass
    human_judgment: false
  - id: D11
    description: "A stale sanitizer report on a reused display prefix is cleared at fixture startup, and a neighbouring fixture's reports are not collateral (deferred item 14)"
    verification:
      - kind: e2e
        ref: "tests/test_wm_runtime.cpp#A stale sanitizer report on a reused display prefix is cleared -- three halves, the third covering the WIRING; mutation M8 reddens only that one"
        status: pass
    human_judgment: false
  - id: D12
    description: "Whether the sideways tab's rendered appearance at non-default frame thicknesses is what a user would want, now that those thicknesses produce a shaped frame at all"
    verification: []
    human_judgment: true
    rationale: "Before this plan, --frame-thickness=3 and --frame-thickness=20 left the frame UNSHAPED -- the server rejected the SHAPE request and the WM logged and continued. The tests now prove the geometry is correct and the requests are accepted, which is the safety claim. Whether the resulting frame LOOKS right at the extremes is a visual judgement no pixel comparison here makes, and it interacts with deferred item 11 (the tab does not grow with the title). Flagged for plan 08-14's visual evidence pass and for /gsd-verify-work."

duration: 200min
completed: 2026-08-29
status: complete
---

# Phase 08 Plan 13: Config-to-Runtime, Terminating Error Paths and Stress Summary

**The last three of the checklist's thirteen missing-coverage items closed — and four defects behind them, one of which meant that closing a window silently threw away every event the window manager had not yet read.**

## Performance

- **Duration:** ~200 min
- **Tasks:** 3 of 3
- **Files created:** 1; modified: 8
- **Commits:** 10

## Accomplishments

- **16 process-level cases in one new file**, in four tag groups, closing checklist coverage items **5** (config reaches runtime), **6** (the four terminating X11 error paths) and **13** (100+ window churn under the sanitizer). With them, **all thirteen** missing-coverage items the phase committed to are automated (D-09).
- **Four genuine defects found and fixed.** Every one was found by an assertion the plan asked for, and two of them are reachable by any ordinary application.
- **Four further checklist entries closed as a by-product** — zombie-free repeated `spawn()`, `exec-using-shell`'s off-by-default shell-evaluated behaviour, and frame thickness at minimum/default/maximum.
- **`--help` exists.** It did not, while the binary's unrecognised-option message advised using it.
- **Deferred item 14 resolved**, with a regression test whose third half covers the *wiring* rather than only the helper — deleting the call in `WmFixture::start()` reddens exactly that half.
- **9 mutations run; all 9 redden a named case** — but two were green first, and neither turned out to be an equivalent mutant.
- **283/283 in debug, release and asan**; zero build warnings; `cppcheck` still at its 18-finding baseline; clang-tidy `bugprone-branch-clone` still 6.

## Task Commits

1. **Deferred item 14 (prerequisite)** — `b76fd97`
2. **Task 1: config-to-runtime** — `d8d1281` (tests) → `a4c7997` (the SHAPE ordering fix it found)
3. **Task 2: terminating error paths and `--help`** — `8e2bc34`
4. **Task 3: the 120-window churn** — `e931fd5` (the two fixes it found) → `873be67` (the case)
5. **Mutation-driven** — `a55442e` (a coupling gap in production, and a missing test)
6. **Docs** — `c23dd88`
7. **Flake removal and load headroom** — `52b7b35`, `0a356a7`

## The four defects

### 1. Every frame thickness except the shipped default left the frame unshaped

A `YXSorted` SHAPE request is a **promise** to the server that the rectangles arrive sorted by y then x. The server validates the promise and rejects the **whole request** with `BadMatch` when it is broken — so the window is left unshaped, `WindowManager::errorHandler()` logs the rejection, and the WM carries on.

Every rectangle list in `Border` is assembled in an order that depends on `FRAME_WIDTH` and on the measured tab width. In `shapeParent()` the main frame rectangle lands at `y == FRAME_WIDTH` while the two tab struts sit at `y == 3`, so the promise holds only while `3 <= FRAME_WIDTH <= tabWidth - 2`.

**Measured** on the shipped binary, one client each:

```
--frame-thickness=3    5 x BadMatch per client
--frame-thickness=7    clean          (the shipped default)
--frame-thickness=20   2 x BadMatch per client
```

Nothing about this is visible to any assertion about windows, properties or geometry — which is exactly why 08-11's protocol-error assertion exists, and this is the third plan running in which it has earned its place.

`Border::combineShapeSorted()` is now the one entry point for a `YXSorted` request and sorts the list before submitting it. The sort is **stable**, so a list that already satisfies the promise — which is every list at the shipped thickness — is passed through byte for byte, and the fix cannot change rendering that was already correct. It takes its vector **by value** because `shapeParent()` submits one list twice and mutates a remembered *index* in between.

08-06 hit the same rejection from a different trigger (a tab shorter than its own width) and worked around it with a floor in `fixTabHeight()`; its comment at `src/Border.cpp:542` names this exact mechanism. This is the general fix.

### 2. Closing one window threw away the events belonging to all the others

`Client::unreparent()` ended with

```cpp
XSync(display(), true);
```

`XSync`'s second argument means *throw away every event currently queued on this connection* — not "throw away the errors from the two requests above", which is what it looks like and what it was presumably meant to do. Protocol errors reach the error handler regardless of the flag, so the discard bought nothing whatever.

`unreparent()` runs from `~Client()`, i.e. on **every** client teardown, so the queue it emptied routinely held events belonging to other windows. **Measured**, one round of the churn:

```
destroyed 20 windows -> 17 still in _NET_CLIENT_LIST, forever
```

Traced to the mechanism directly: instrumenting `eventDestroy`, `eventCreate` and `eventReparent` showed `TRACE create-created <id>` for each of those windows and **no `TRACE destroy` line at all** — the `DestroyNotify` never reached the handler because it had been thrown away.

Each of those windows keeps a live `Client` and `Border`, still holding its X resources, never freed, and published to every EWMH-aware client as a managed window. `MapRequest`s and `ConfigureRequest`s sit in the same queue, so the same discard could leave an unrelated window unframed after a user closed something else. It is a plausible contributor to deferred item 12's full-suite flake, though this plan did not try to prove that.

### 3. The WM installed colormap XIDs it had never computed

`Client::getColormaps()` used two `XGetWindowAttributes` results without checking them. On failure Xlib leaves the attribute struct **exactly as it found it**, so `attr.colormap` is stack garbage on the first call and the **previous window's** colormap inside the loop. The call fails routinely — a client may destroy a window at any moment while the WM is still working through queued events for it.

**Measured:**

```
wm2: X_InstallColormap (0x68413d80): BadColor (invalid Colormap parameter)
```

`0x68413d80` belongs to no client on that display. An arbitrary XID that happened to name a colormap owned by **another application** would not be rejected — it would be installed, changing the display's colours from a value the WM never computed. Both reads now fall back to `None`, which `WindowManager::installColormap()` already treats as "install the default".

### 4. The help flag pointed at itself and did not exist

An unrecognised option produced a getopt error followed by `Try '<binary> --help' for more information.`, and `--help` was not in the long-option table — so following the binary's own advice produced the same error again.

`kOptionSpecs` is now the single declaration of the CLI surface: the `getopt_long()` array and the usage text are both generated from it, and each boolean's `--no-` negation is **derived** rather than listed, so the two spellings of one setting cannot fall out of step. Help exits from inside `Config::load()`, before the app cache and before `WindowManager`, so it works with no `DISPLAY` — which is the case that matters, since help you need an X server to read is no use when you are working out why the window manager will not start.

## Where mutation testing changed the code and the tests

Two mutations came back green. Neither was an equivalent mutant.

| Mutation | First result | What changed |
|---|---|---|
| **M7** — delete the derived `--no-<name>` rows from the getopt table | help case **green** | `printUsage()` re-derived the negation names from the spec instead of reading the list `getopt_long()` was actually handed, so the usage text went on advertising flags the binary would then reject. A coupling gap in **production code**, found by a test that could not see it. Every printed name now comes from that same list, so a printed option cannot be unrecognised by construction — and M7 now reddens the help case too. |
| **M4** — delete the return check on the SECOND `XGetWindowAttributes` in `getColormaps()` | whole suite **green** | An **uncovered branch**, not an equivalent mutant: the branch is only reachable through `WM_COLORMAP_WINDOWS`, which nothing in the suite wrote. The churn case now writes one naming a window it then destroys, with the client active so `eventProperty()` installs the result. That sub-case is **still green in debug** with the check deleted and **red under the sanitizer** — the uninitialised stack slot reads as a harmless value in one build and as a garbage XID in the other. Precisely 08-12's warning, one plan later. |

## Mutation testing (9 deletions)

| # | Branch deleted | File | Reddened |
|---|---|---|---|
| M1 | the `stable_sort` in `combineShapeSorted` | `src/Border.cpp` | frame-thickness case (2 assertions) |
| M2 | `XSync(display(), false)` back to `true` | `src/Client.cpp` | the churn case |
| M3 | the return check on the first `XGetWindowAttributes` | `src/Client.cpp` | the churn case, **both trees** |
| M4 | the return check on the second `XGetWindowAttributes` | `src/Client.cpp` | the churn case — **green in debug, red under ASan** |
| M5 | the `OptType::Help` dispatch | `src/Config.cpp` | 11 assertions across `[wm_errors]` |
| M6 | `enable = !matched.negated` → `enable = true` | `src/Config.cpp` | the exec-using-shell case (3 assertions) |
| M7 | the derived `--no-` rows in the getopt table | `src/Config.cpp` | exec-using-shell **and** the help case |
| M8 | the `removeReportsWithPrefix` call in `spawnWm()` | `tests/support/WmFixture.h` | `[wm_harness]`, its third half only |
| M9 | the negation lines in `printUsage` | `src/Config.cpp` | the help case (3 assertions) |

**All nine redden.** No equivalent mutants recorded, because neither survivor was one.

## Three sources of flake removed, all measured

The three-group gate failed twice in six runs before this. It was not deferred item 12 — all three causes were in this plan's own test code, and all three were real.

| Cause | Measured | Fix |
|---|---|---|
| Xlib's **default** error handler calls `exit(1)`, and the tree-walk helpers `XQueryTree` and then ask each child for its geometry. Between those round trips a window can legitimately disappear — `settleWm()`'s nudge windows are doing exactly that, continuously. | 2 flakes in 4 runs, both `BadDrawable` on `X_GetGeometry`, killing the process with **no assertion output at all** | a counting, non-fatal handler installed once at static init. The helpers already handled the `false` return; they just never reached it. |
| `WindowManager::menu()` ignores a `ButtonRelease` until its `drawn` flag is set by the Expose handler, so a release that beat the Expose selected nothing and the New entry silently never fired | 1 flake in 6 runs | `openRootMenu()` returns only once the menu rectangle is no longer one flat colour — which **is** the Expose having been handled, observed from outside |
| The root menu's height depends on **how many applications the host has installed**. A menu tall enough to be clamped makes the WM warp the pointer; the warp is a `MotionNotify`; a `MotionNotify` over a category row opens a submenu — and the case then compared the submenu's pixels | 1 flake, captured as an all-black histogram | the menu is identified by the press point it is anchored on (the submenu is placed to the side and cannot contain it), and every case presses where no clamp is needed |

Afterwards: **6 consecutive green runs** of the three-group gate.

## TEST-05, adjudicated

**TEST-05 is genuinely satisfied**, and it was already marked complete in `REQUIREMENTS.md` before this plan started.

Two things are worth stating plainly rather than left implied:

- **08-12's closing note was wrong about the ledger.** It recorded "TEST-05 is not marked complete … 08-13 is the last", but `.planning/REQUIREMENTS.md` has carried `- [x] **TEST-05**` and `| TEST-05 | Phase 8 | Complete |` since `bf162fb` (plan 08-10). `requirements mark-complete TEST-05` returns `already_complete` and writes nothing. Recorded here so the next reader is not sent looking for a ledger edit that never needed making.
- **The requirement text was checked clause by clause, not assumed.** It asks for process-level tests that *launch the compiled binary under Xvfb/Xephyr, drive real X11 clients, and verify root/client ICCCM + EWMH properties after create, map, unmap, remap, hide/unhide, fullscreen, maximize, and destroy*:

| Clause | Evidence |
|---|---|
| launch the compiled binary under Xvfb | `WmFixture`, 9 process-level suites, own display per instance |
| drive real X11 clients | real connections plus XTEST device events (`XTestDriver`) |
| create / map | `[wm_process]`, `[wm_lifecycle]`, and the 120-window churn here |
| unmap / remap | `[wm_lifecycle]` (08-11), and the churn's remap phase here |
| hide / unhide | `[wm_lifecycle]` (08-11); also the short-press hide here |
| fullscreen / maximize | `[wm_fsmax]` (08-12), 7 cases |
| destroy | `[wm_lifecycle]` (08-11) and `[wm_stress]` here |
| root/client ICCCM + EWMH properties | `_NET_CLIENT_LIST`, `_NET_ACTIVE_WINDOW`, `_NET_WM_STATE`, `_NET_WORKAREA`, `WM_STATE`, `WM_PROTOCOLS` asserted throughout |

The one clause not literally exercised is **Xephyr** — everything runs under Xvfb. The requirement reads "Xvfb/Xephyr" as alternatives, and TEST-08 (still pending) is where nested-server and remote-desktop evidence lives. Noted rather than glossed.

## Decisions Made

- **Frame thickness is asserted absolutely, not by run comparison.** The plan's own assumption note anticipated only a comparison; `yIndent() == FRAME_WIDTH + 1` is exact and font-independent, and forcing non-default thicknesses to be exercised at all is what surfaced the SHAPE defect.
- **The SHAPE fix sorts rather than switching to `Unsorted`.** Both produce an identical region; the stable sort keeps the server's fast path and provably cannot change any already-correct rendering.
- **`unreparent()`'s sync was kept, only its discard flag changed.** The sync orders the reparent and border-width restore against the destruction that follows.
- **`getColormaps()` falls back to `None`,** because `installColormap(None)` already means "install the default" — keeping the policy in one place rather than duplicating it at the read site.
- **`X_SetInputFocus BadMatch` is bounded, not excluded.** `Client::activate()` already guards; the residue is an irreducible race. A bound keeps the case able to notice a focus-path regression whose count would scale with the churn.
- **The memory budget was written down before the first run** — 8 MB debug, 32 MB ASan, with the reasoning — and baseline, budget and result are printed separately on every run. Measured 536 kB and 3720 kB.
- **The colormap-windows sub-case was folded into the churn case** rather than added as a sixteenth, keeping the plan's stated 15-test gate intact and putting the claim where it belongs.
- **Deferred item 5 was deliberately not closed.** See below.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] The frame was left unshaped at every non-default frame thickness**

- **Found during:** Task 1, first run of the frame-thickness case
- **Issue:** `YXSorted` SHAPE requests whose rectangle order depends on `FRAME_WIDTH`; the server rejects the whole request with `BadMatch`
- **Fix:** `Border::combineShapeSorted()`, stable sort, all eight `YXSorted` call sites moved onto it
- **Files modified:** `src/Border.cpp`, `include/Border.h`
- **Verification:** measured 5 and 2 rejections per client at thicknesses 3 and 20, clean at 7; mutation M1 reddens
- **Commit:** `a4c7997`

**2. [Rule 1 - Bug] `Client::unreparent()` discarded the WM's entire queued event backlog**

- **Found during:** Task 3, first run of the churn case
- **Issue:** `XSync(display(), true)` on every client teardown; up to 17 of 20 destroyed windows stuck in `_NET_CLIENT_LIST` forever, with live `Client`/`Border` objects behind them
- **Fix:** `XSync(display(), false)`
- **Files modified:** `src/Client.cpp`
- **Verification:** traced on the real binary — `create-created` present, no `destroy` line at all; mutation M2 reddens
- **Commit:** `e931fd5`

**3. [Rule 2 - Missing critical functionality] Unchecked `XGetWindowAttributes` results reached `XInstallColormap`**

- **Found during:** Task 3, by the churn case's protocol-error assertion
- **Issue:** uninitialised `attr.colormap` installed as a colormap XID; measured `X_InstallColormap (0x68413d80): BadColor`
- **Fix:** both reads checked, falling back to `None`
- **Files modified:** `src/Client.cpp`
- **Verification:** mutations M3 (both trees) and M4 (ASan) redden
- **Commit:** `e931fd5`, coverage completed in `a55442e`

**4. [Rule 2 - Missing critical functionality] The usage text was not a projection of the parse table**

- **Found during:** mutation M7
- **Issue:** `printUsage()` re-derived `--no-` names, so removing them from the parse table left the help output advertising flags the binary would reject
- **Fix:** every printed name is taken from the list `getopt_long()` is handed
- **Files modified:** `src/Config.cpp`
- **Commit:** `a55442e`

### Files modified beyond the plan's `files_modified` list

The plan named `tests/test_wm_runtime.cpp`, `CMakeLists.txt` and `src/Config.cpp`. Also changed: `src/Border.cpp` and `include/Border.h` (deviation 1), `src/Client.cpp` (deviations 2 and 3), and `tests/support/WmFixture.h` (deferred item 14, assigned to this plan by the executor brief and by 08-12). Each is a one-place fix for a defect a case in this plan found, or the handed-over item; all are recorded above.

### Acceptance-criteria counts that differ from the plan (no action needed)

| Criterion | Expected | Actual | Why |
|---|---|---|---|
| `grep -c 'DISPLAY=:99' CMakeLists.txt` | 4 | **5** | Already 5 at this plan's base commit, exactly as 08-09 predicted and 08-10, 08-11 and 08-12 each recorded. `grep -c 'ENVIRONMENT "DISPLAY=:99"'` is 4, unchanged. Deliberately not "fixed". |
| `grep -c 'test_wm_runtime' CMakeLists.txt` | >=3 | 6 | Satisfied. |
| `grep -c 'XGetImage'` | >=1 | 5 | Satisfied. |
| `grep -c 'wm_errors'` | >=7 | 10 | Satisfied. |
| `grep -c 'wm_stress'` | >=1 | 5 | Satisfied. |
| `grep -c 'xterm'` | 0 | 0 | Satisfied — every case uses the preflight-checked probe program. |
| `grep -c 'sleep('` (comments stripped) | 0 | 0 | Satisfied. The one deliberate wall-clock interval (the long press) is a deadline loop. |
| `grep -c '"help"' src/Config.cpp` | >=1 | 1 | Satisfied, and the dispatch is driven by the table metadata rather than a second name list. |
| `grep -c 'longOptions' src/Config.cpp` | >=3 | 11 | Satisfied. |

## Verification Evidence

| Gate | Result |
|---|---|
| `ctest -L '^wm_config_runtime$'` (debug) | **7/7 passed** |
| `ctest -L '^wm_errors$'` (debug) | **7/7 passed** |
| `ctest -L '^wm_stress$'` (debug and asan) | **1/1 passed**, no sanitizer report files left behind |
| `ctest -L '^(wm_config_runtime\|wm_errors\|wm_stress)$'` (debug) | **15/15 passed** (16 with the preflight fixture), **6 consecutive runs** |
| Same three groups (asan) | **15/15 passed**, no sanitizer reports |
| `ctest -L '^config$'` (debug) | **48/48 passed** — the CLI parser rewrite broke no existing assertion |
| `./build/debug/wm2-born-again --help` | exit 0; contains `--frame-thickness` and `--no-auto-raise`; works with `DISPLAY` unset |
| `./build/debug/wm2-born-again --nosuchflag` | exit 2 |
| `ctest --test-dir build/debug` (full) | **283/283 passed** |
| `scripts/gates/build-all.sh` | **OK: debug release asan** — 283/283 in each tree |
| Build warnings | **0**, in all three trees |
| ASan | **no sanitizer findings** (2 matched `libfontconfig` suppressions, 6 accounting logs) |
| `scripts/analysis/run-static-analysis.sh` | **OK** — cppcheck 18 findings all baselined, clang-tidy no enforced check fired, `bugprone-branch-clone` still 6 |

### On the one intermediate gate failure, and why it is not a regression

An intermediate `build-all.sh` run reported one failure in the **release** tree: `A dock window with _NET_WM_STRUT_PARTIAL shrinks _NET_WORKAREA`, in `tests/test_wm_process.cpp` — a file this plan does not touch (`git diff --stat d890307..HEAD -- tests/test_wm_process.cpp` is empty). It is deferred item 12's documented signature, in the same file and the same dock-strut area that both 08-11 and 08-12 recorded as their own flake.

**Measured in isolation on this plan's own tree: 10 runs, 0 failures.** The final `build-all.sh` run is green end to end, **283/283 in all three trees**.

The same intermediate run also failed one of *this plan's* cases in the asan tree — that one was **not** attributed to item 12. It was investigated: `new-window-command` took 16.4 s against a 15 s deadline while passing in 0.3 s standalone in the same tree. The deadlines are now named constants sized for a loaded sanitizer run, with the reasoning recorded, because a deadline here exists to stop a broken case hanging the suite forever and not to assert that spawning is fast.

## Known Stubs

None. No placeholder values, no skipped tests, no unrun `<verify>` blocks. No equivalent mutants are recorded, because both surviving mutations turned out to be real gaps and both were closed.

## Threat Flags

None new. The plan's register is addressed:

- **T-8-SHELL** — covered as specified and then some: the case asserts a **filesystem witness** proving the metacharacter half of the command never ran with shell mode off, with the shell-on run as a positive control sharing the same command string. Mutations M6 and M7 both redden it.
- **T-8-DOS** — the churn case asserts an empty client list, a non-dangling active window, continued correct framing, a bounded resident-memory growth against a measured baseline, and no zombie children, under the sanitizer. It found a genuine unbounded-growth path (defect 2).
- **T-8-UAF** — the cycle interleaves overlapping unmap/destroy subsets rather than looping sequentially, and the sanitizer report collection is a pass criterion. No use-after-free found; the related finding was an uninitialised **read** (defect 3), which ASan does not catch directly but which the protocol-error assertion did.
- **T-8-FATAL** — all four paths covered by subprocess cases with deadlines, plus a dedicated tight-deadline case so a hang is distinguishable from a wrong exit code. The conflict case additionally proves the incumbent still frames a fresh client.
- **T-8-SC** — accepted as planned; no dependency of any ecosystem was added.

## Notes for Future Phases

- **A `YXSorted` SHAPE request is a promise, and breaking it costs the whole request.** `Border::combineShapeSorted()` is now the only way to issue one from that class. A new rectangle list that calls `combineShape(..., YXSorted)` directly is a bug waiting for a configuration that reorders it, not a style choice.
- **`XSync(dpy, True)` is almost never what you want.** It does not discard errors — it discards *events*. There is now exactly one `XSync` in `Client.cpp` and it passes `false`. Anything that reintroduces the true form on a connection with a pending event queue reintroduces defect 2.
- **Xlib out-parameters are not written on failure.** `getColormaps()` is fixed, but the codebase has 143 `cppcoreguidelines-init-variables` findings, "overwhelmingly `XGetWindowProperty` out-parameters", and that family is reported-only precisely because they are *usually* safe. They are safe only where the return value is checked. Worth a sweep when someone has a reason to open that file.
- **Deferred item 5 is still open, deliberately, and now says so.** 08-13 was the last plan scheduled to touch shared test infrastructure and did consider closing it; the two available fixes are twelve `catch_discover_tests(... PROPERTIES ENVIRONMENT ...)` edits or a `__lsan_default_suppressions()` translation unit linked into **every** test executable (the LSan runtime resolves that symbol in the main binary, so one shared object library does not reach it). Both are broad edits to a shared build file for a condition no plan in this phase caused, and the sanctioned path — `scripts/gates/build-all.sh asan` — is green. The item now records both options, their cost, and a recommended owner.
- **Deferred item 16 will cost the next person an hour.** The tab button is *subtracted from the frame's bounding shape* on an inactive client while remaining mapped with its geometry intact, so a test that finds it with `XQueryTree` and presses it gets the root menu instead and nothing announces why.
- **The root menu's shape depends on host state.** `m_appCategories` comes from the application cache, so the menu's height varies by machine. Any future menu case must either avoid a clamp or tolerate the pointer warp and the submenu it can trigger.
- **This plan closes the last of the thirteen.** `COMPILED_CODE_BEHAVIOR_CHECKLIST.md`'s "Missing Automated Coverage To Add" section is fully ticked, with the plan that closed each item named in place. What remains in that document is the manual and release-evidence work, which is plan 08-14's and TEST-08's.

## Self-Check: PASSED

`tests/test_wm_runtime.cpp` and this SUMMARY exist on disk; all 10 claimed commit hashes resolve in `git log`.
