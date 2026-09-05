---
phase: 08-xrandr-vnc-compatibility-focus-rules
plan: 10
subsystem: window-management
tags: [rules, wm-class, window-placement, ewmh-state, undecorated-windows, process-level-tests]

requires:
  - phase: 08-xrandr-vnc-compatibility-focus-rules/08-09
    provides: "include/Rules.h, ruleMatches(), applyRules(), RuleWindowFacts and Config::rules -- the model and parser this plan consumes"
  - phase: 08-xrandr-vnc-compatibility-focus-rules/08-08
    provides: "the single _NET_WM_STATE publisher with m_demandsAttention, and the two interned/advertised skip-* atoms"
  - phase: 08-xrandr-vnc-compatibility-focus-rules/08-04
    provides: "screenWidth()/screenHeight(), which both clamps read"
  - phase: 06-ewmh-compliance
    provides: "the single-desktop model (D-11) that makes the fourth RULES-02 action untargetable, and the dock/notification unframed path the no-decorate action reuses"
provides:
  - "Client::getClassHint(), Client::resName(), Client::resClass(): the WM now knows which application a window belongs to"
  - "Client::applyWindowRules(): the map-time fold, with the three shipping actions wired to existing primitives"
  - "Client::clampGeometryToScreen(): the unframed counterpart of ensureVisible()"
  - "skip-taskbar/skip-pager published through the single _NET_WM_STATE writer"
  - "tests/test_wm_rules.cpp and the test_wm_rules target: 8 process-level cases against the real binary"
  - ".planning/REQUIREMENTS.md RULES-02 amended (D-23) to the three shipping actions with the exclusion reason"
affects: [any later work touching Client::manage(), the unframed map path, or _NET_WM_STATE publication]

actuals:
  tokens: 10300
  tasks: 3
  commits: 4

tech-stack:
  added: []
  patterns:
    - "A rule-derived flag kept distinct from the type it shares a code path with (m_ruleNoDecorate vs isDock()), so the side effects of the type do not leak to the rule"
    - "Rule geometry overlaid on the client's REQUEST between the size-hint floors and the clamps, rather than applied as a second authority after mapping"
    - "Isolated XDG config tree per process-level case, with XDG_CONFIG_DIRS overridden as well as XDG_CONFIG_HOME so the host's /etc/xdg cannot layer under the fixture"

key-files:
  created:
    - tests/test_wm_rules.cpp
  modified:
    - include/Client.h
    - src/Client.cpp
    - CMakeLists.txt
    - .planning/REQUIREMENTS.md
    - scripts/analysis/cppcheck-suppressions.xml

key-decisions:
  - "The undecorated early-return moved BELOW the size-hint/minimum-size normalisation rather than the rule geometry being applied above it. A rule-undecorated window needs the minimum-size floors as much as a framed one, and the alternative -- a second geometry block inside the early return -- is exactly the duplicate lifecycle path the plan prohibits."
  - "m_ruleNoDecorate is a separate flag from isDock(), and the workarea recomputation stays guarded on isDock(). Collapsing them would let any no-decorate rule shrink every other window's usable screen."
  - "The rule position is the FRAME's outer corner for a framed window and the CLIENT's corner for an unframed one, because the rule value is overlaid on the client's request and then passes through gravitate() exactly as a client-requested position does. Consistent with what the user means; asserted both ways."
  - "ensureVisible() is called for rule-driven geometry only, not unconditionally. Running it for every window would change placement for windows the WM has managed since Phase 1, which the no-rules control case would have caught."
  - "skip-taskbar triggers a publish from applyWindowRules(). Nothing else in the ordinary map path writes _NET_WM_STATE, so without that call the rule would set a flag no panel ever sees -- but the WRITE still happens only inside updateNetWmState()."

patterns-established:
  - "Mutation testing extended to the wiring layer: nine production branches deleted in turn, each confirmed to redden a named case, with the one equivalent mutant identified as equivalent rather than papered over with a contrived test"

requirements-completed: []

coverage:
  - id: D1
    description: "The WM reads a window's instance and class names when it takes the window under management, so class-based rules can match at all"
    requirement: RULES-01
    verification:
      - kind: e2e
        ref: "tests/test_wm_rules.cpp -- five cases redden when the getClassHint() call is deleted (mutation M1)"
        status: pass
    human_judgment: false
  - id: D2
    description: "A window matching a no-decorate rule is mapped and managed without a frame, reusing the existing undecorated path"
    requirement: RULES-02
    verification:
      - kind: e2e
        ref: "tests/test_wm_rules.cpp#A window matching a no-decorate rule is managed without a frame"
        status: pass
    human_judgment: false
  - id: D3
    description: "A window matching a position or size rule is placed at that geometry"
    requirement: RULES-02
    verification:
      - kind: e2e
        ref: "tests/test_wm_rules.cpp#A position rule places a window and a size rule sizes it"
        status: pass
    human_judgment: false
  - id: D4
    description: "Rule geometry is clamped so the window stays fully on screen, by moving rather than resizing, on BOTH the framed and the unframed path"
    requirement: RULES-02
    verification:
      - kind: e2e
        ref: "tests/test_wm_rules.cpp#A position rule that would strand a window offscreen is clamped"
        status: pass
    human_judgment: false
  - id: D5
    description: "A window matching a skip-taskbar rule has the skip-taskbar AND skip-pager states published through the single existing state-publication function"
    requirement: RULES-02
    verification:
      - kind: e2e
        ref: "tests/test_wm_rules.cpp#A window matching a skip-taskbar rule carries skip-taskbar and skip-pager"
        status: pass
      - kind: source
        ref: "grep -c 'XChangeProperty(display(), m_window, Atoms::net_wmState' src/Client.cpp == 2 (unchanged: the single publisher's two branches)"
        status: pass
    human_judgment: false
  - id: D6
    description: "When several rules match one window the applied outcome is the later-wins fold -- not first-match, not last-rule-wholesale"
    requirement: RULES-02
    verification:
      - kind: e2e
        ref: "tests/test_wm_rules.cpp#Two matching rules fold later-wins per action, not first-match"
        status: pass
    human_judgment: false
  - id: D7
    description: "A rule keyed on window type alone applies to every window of that type and to nothing else"
    requirement: RULES-02
    verification:
      - kind: e2e
        ref: "tests/test_wm_rules.cpp#A rule matching on window type alone applies to every window of that type"
        status: pass
    human_judgment: false
  - id: D8
    description: "Rules change behaviour rather than merely coexisting with it: with no rules, and with rules that do not select the window, framing/placement/state are exactly as before"
    requirement: RULES-02
    verification:
      - kind: e2e
        ref: "tests/test_wm_rules.cpp#With no rules configured a window is framed, placed and stated as before"
        status: pass
      - kind: e2e
        ref: "tests/test_wm_rules.cpp#A window that matches no configured rule is untouched by the rules"
        status: pass
    human_judgment: false
  - id: D9
    description: "The requirement text records that the workspace action is excluded and why, rather than the exclusion living only in a planning document"
    requirement: RULES-02
    verification:
      - kind: source
        ref: ".planning/REQUIREMENTS.md line 86 -- three actions listed, exclusion and Phase 6 reason appended, checkbox and Traceability row untouched"
        status: pass
    human_judgment: false
  - id: D10
    description: "Whether a rule file a non-programmer writes produces the placement they pictured -- particularly whether 'position 100,120' meaning the frame corner rather than the client corner matches the user's mental model"
    verification: []
    human_judgment: true
    rationale: "The two coordinate conventions (frame corner when framed, client corner when undecorated) are each internally consistent and each defensible, and both are asserted. Which one a user expects when they write a number into a config file is a judgment about the design, not a property any test settles. Flagged for /gsd-verify-work."
  - id: D11
    description: "Rules are evaluated exactly once, at manage time; a window that rewrites WM_CLASS, its name or its type afterwards is not re-evaluated"
    verification: []
    human_judgment: true
    rationale: "The planner flagged this assumption as unresolved. It is implemented as assumed and documented at getClassHint(), but whether a user hits it in practice -- an application that sets its class late, so the rule they wrote never fires and nothing says why -- is a usability question. Flagged for /gsd-verify-work."

duration: 21min
completed: 2026-08-29
status: complete
---

# Phase 08 Plan 10: Window Rules Applied at Map Time Summary

**A user can now put four lines in their config file and have a named application open undecorated, at a fixed place, hidden from the taskbar — and almost none of that is new code, because the window manager already had a path for every one of those outcomes and had simply never been told which windows to send down it.**

## Performance

- **Duration:** ~21 min
- **Tasks:** 3 of 3
- **Files created:** 1; modified: 5
- **Commits:** 4

## Accomplishments

- **The genuinely new code is one Xlib call.** Until this plan nothing in the tree had ever asked a window which application it belonged to — `XGetClassHint` appeared nowhere. Everything else here is routing: no-decorate takes the early return dock and notification windows have taken since Phase 6, geometry goes through the existing size-hint floors and the existing clamp, and skip-taskbar goes through the `_NET_WM_STATE` writer plan 08-08 built.
- **The undecorated early-return moved down, not the geometry up.** The plan called for the shared size-hint normalisation to precede the unframed branch, and that ordering is what makes the no-decorate path reuse rather than duplicate: a rule-undecorated window gets its minimum-size floors, its rule geometry and its clamp from the same lines a framed window does, then returns before the frame is built. `grep -c 'XMapWindow' src/Client.cpp` is unchanged at 5 — no second lifecycle path was written.
- **Nine mutations, eight confirmed to redden a named case, one identified as equivalent.** Deleting the class read reddens five cases; deleting skip-pager reddens two; deleting the position overlay reddens three. One deletion reddened nothing, and chasing it is what produced this plan's real finding (below).
- **08-09's three handoff predictions all held exactly.** The cppcheck baseline went stale the moment the fold got a caller; `grep -c 'DISPLAY=:99' CMakeLists.txt` read 5, not 4; and the `Off`/`On` enumerators were left alone.
- **Zero build warnings across debug, release and asan; no sanitizer findings.** The two server-allocated `WM_CLASS` strings are freed individually on every path, and ASan agrees.

## Task Commits

1. **Task 1: the class-hint read** — `c60dfc4`
2. **Task 2: rule application at map time** — `d2204c0` (test/RED) → `86dd90b` (feat/GREEN)
3. **Task 3: unframed clamp coverage and the D-23 amendment** — `2e61da9`

## The mutation that found a hole, and the one that did not

**Found a hole (M5).** Deleting the clamp call from the *unframed* geometry path left the whole suite green. The clamp case only ever mapped a framed window, so the no-decorate path was free to strand a rule-undecorated window past the right edge of the screen with nothing to notice — and a rule-undecorated window is precisely the one a user cannot drag back, because it has no frame to grab. Closed by making the clamp case map **both** a framed and a rule-undecorated window against the same WM and asserting the same four bounds plus the unshrunk size on each. M5 now reddens.

**Equivalent mutant (M10), recorded as equivalent rather than papered over.** Removing the `isDock()` guard on the workarea recomputation — so a rule-undecorated window would also trigger it — reddens nothing, and no test can make it redden: `WindowManager::updateWorkarea()`'s `checkStruts` lambda already filters on `client->isDock()` internally, so a recomputation triggered by a non-dock window recomputes the identical value from the identical dock set. The call-site guard is defence-in-depth against a future change to that lambda, not a behaviour. Writing a test that appeared to cover it would have been a test that did not test its own name — the exact failure this phase has now caught four times.

## Decisions Made

- **`m_ruleNoDecorate` is a distinct flag from `isDock()`.** Both reach the same unframed return, but only a dock may recompute the workarea. Collapsing them would let a purely cosmetic rule — "open this application without a border" — shrink every other window's usable screen as a side effect.
- **A rule position means the frame's outer corner for a framed window and the client's corner for an unframed one.** This falls out of applying the rule to the client's *request* and letting `gravitate()` consume it, which is exactly how a client-requested position is treated. Both conventions are asserted; which one a user expects is flagged for human judgment.
- **`ensureVisible()` runs for rule-driven geometry only.** The pre-existing map-time clamp keeps the frame's *indent* on screen, which is not the same as keeping the window on screen: 980,700 for a 300x220 window satisfies it and still leaves most of the window past the right edge. But calling the whole-window clamp unconditionally would change placement for every window the WM has managed since Phase 1 — the no-rules control case exists partly to catch that.
- **A rule asking for a size below the application's minimum is raised to the minimum**, exactly as an undersized client request is a few lines later. A rule is a user preference, not a licence to violate what the application stated about itself.
- **`skip-taskbar` triggers a publish from `applyWindowRules()`, but the write still happens only inside `updateNetWmState()`.** Nothing in the ordinary map path writes `_NET_WM_STATE`, so without the trigger the rule would set a flag no panel ever sees; without the single-writer discipline the array would lose whichever states the second site did not know about.
- **No workspace action, recorded in three places.** At the fold in `src/Client.cpp`, at the action set in `include/Rules.h` (08-09), and now in the requirement text itself.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Task 1's three behaviours have no observable at process level**

- **Found during:** Task 1, at the RED step
- **Issue:** Task 1 is marked `tdd="true"`, but `resName()`/`resClass()` are not published anywhere the X server can see. Until Task 2 wires the fold, there is no observation that distinguishes "the class was read" from "the class was not read", so no failing test could be written for it in isolation. Client is not unit-testable in-process either (D-07: the event loop runs inside `WindowManager`'s constructor).
- **Fix:** Task 1 was implemented as plumbing and verified by its own acceptance greps plus the build, ASan and static-analysis gates. Its three behaviours are covered *transitively and provably* by Task 2's tests: deleting the `getClassHint()` call (mutation M1) reddens five named cases, which is a stronger demonstration than a RED-then-GREEN pair on an accessor would have been. The no-class-hint behaviour is covered by the type-only case, whose windows carry classes no rule matches.
- **Files modified:** none beyond the plan
- **Commit:** `c60dfc4`

**2. [Rule 2 - Missing critical coverage] The unframed clamp was untested**

- **Found during:** Task 3 mutation testing (M5)
- **Issue:** Deleting `clampGeometryToScreen()` from the no-decorate path left the suite green.
- **Fix:** The clamp case now exercises both paths against one WM.
- **Files modified:** `tests/test_wm_rules.cpp`
- **Verification:** M5 re-run reddens the named case.
- **Commit:** `2e61da9`

**3. [Rule 3 - Blocking] `std::strnlen` does not exist**

- **Found during:** Task 1, first build
- **Issue:** `error: 'strnlen' is not a member of 'std'`. It is POSIX, not C++.
- **Fix:** A four-line file-static `boundedCopy()` instead, so the bound is visible at the point it matters rather than delegated to a library whose availability varies.
- **Files modified:** `src/Client.cpp`
- **Commit:** `c60dfc4`

**4. [Rule 3 - Blocking] The D-23 amendment tripped its own acceptance guard**

- **Found during:** Task 3
- **Issue:** `grep -c 'specific workspace' .planning/REQUIREMENTS.md` must be 0, but the amendment quoted the original wording verbatim in order to explain what was removed — so the guard matched my own explanation of the removal. Precisely the trap plan 08-09 hit three times and `CMakeLists.txt` documents in its D-33 block.
- **Fix:** The amendment now names the excluded action descriptively ("sending a window to a nominated workspace") without the literal token. The guard reads the requirement, not the footnote.
- **Files modified:** `.planning/REQUIREMENTS.md`
- **Commit:** `2e61da9`

### Acceptance-criteria counts that differ from the plan (no action needed)

| Criterion | Expected | Actual | Why |
|---|---|---|---|
| `grep -c 'DISPLAY=:99' CMakeLists.txt` | 4 | **5** | Predicted by 08-09 and already 5 at this plan's base commit. The fifth match is the readiness poll inside the `start_xvfb` command; `grep -c 'ENVIRONMENT "DISPLAY=:99"'` is 4, unchanged. Not a defect and deliberately not "fixed". |
| `grep -c 'ensureVisible' src/Client.cpp` | ≥2 | 5 | Satisfied. `grep -c` counts lines, and the name appears in the comment explaining why the call is conditional. |
| `grep -c 'test_wm_rules' CMakeLists.txt` | ≥3 | 6 | Satisfied. |
| `grep -c 'single-desktop' .planning/REQUIREMENTS.md` | ≥1 | 2 | Satisfied. |

## Verification Evidence

| Gate | Result |
|---|---|
| `ctest -L '^wm_rules$'` (debug) | **8/8 passed** |
| `ctest -L '^wm_rules$'` (asan) | **8/8 passed**, no sanitizer report files left behind |
| `ctest -L '^(wm_rules\|rules)$'` (debug) | **26/26 passed** (8 + 17 + the preflight fixture) |
| `ctest --test-dir build/debug` (full) | **226/226 passed** |
| `scripts/gates/build-all.sh` | **OK: debug release asan** — 226/226 in each tree |
| Build warnings | **0** across all three trees |
| ASan | **no sanitizer findings** (2 matched `libfontconfig` suppressions) |
| `scripts/analysis/run-static-analysis.sh` | **OK** — cppcheck 18 findings all baselined, clang-tidy no enforced check fired, `bugprone-branch-clone` still 6 |
| cppcheck baseline diff | **six lines removed, none added** — the `applyRules` `unusedFunction` entry, exactly as 08-09 predicted |

### Mutation testing (9 deletions)

| # | Production branch deleted | Reddened |
|---|---|---|
| M1 | the `getClassHint()` call in `manage()` | **5 cases** |
| M2 | `m_ruleNoDecorate` from the unframed condition | 2 cases |
| M3 | the skip-pager push in `updateNetWmState()` | 2 cases |
| M4 | the framed whole-window clamp (`ensureVisible()`) | 1 case |
| M5 | the unframed clamp | **nothing** → exposed the gap, now closed (reddens after the fix) |
| M6 | the window type mapping into `RuleWindowFacts` | 1 case |
| M7 | the publish trigger for skip-taskbar | 3 cases |
| M8 | the rule position overlay | 3 cases |
| M9 | the rule size overlay | 1 case |
| M10 | the `isDock()` guard on the workarea recomputation | **nothing** — equivalent mutant, see above |

## Known Stubs

None. Every action in the shipping set is implemented and proven against the real binary. The fourth action from the original requirement wording is not a stub: it is an exclusion recorded at the fold, at the action set, and in the requirement text.

## Threat Flags

None. The plan's register is addressed:

- **T-8-PROP** (the untrusted, server-allocated class-hint strings) — the return status is checked before either field is touched, both fields are freed individually on every path, and each copy is length-bounded at 1024 bytes by a hand-written scan so the bound is visible where it matters. ASan reports no leak across the whole suite.
- **T-8-STRUT** (rule-supplied geometry as a denial-of-service) — rule size is raised to the size-hint minimums, and rule position passes through a whole-window clamp that **moves** rather than resizes, on both the framed and the unframed path. Pinned by the clamp case in both directions, and by mutations M4 and M5.
- **T-8-SHELL** (a rule action that executes something) — no action added here runs a command, a shell string or any external program; the action set is the three in `include/Rules.h` and nothing was added to it.
- **T-8-SPOOF** and **T-8-SC** accepted as planned; no dependency of any ecosystem was added.

## Notes for Future Phases

- **`Client::manage()` now has a different shape.** The unframed early return sits *below* the size-hint block rather than above it, so anything added between the two runs for dock, notification and rule-undecorated windows as well as framed ones. That is deliberate — it is what makes the no-decorate action reuse the path — but it is a change of assumption for anyone editing that function.
- **`RuleOutcome` is a member (`m_ruleOutcome`), read twice in `manage()`.** If a later plan re-evaluates rules on property change, it must decide what happens to geometry already applied; the current answer is "nothing, rules are map-time only", documented at `getClassHint()`.
- **The two coordinate conventions are the one thing here worth a human's eye.** `rule-position=100,120` puts the *frame* at 100,120 for a framed window and the *client* at 140,160 for an undecorated one. Both are internally consistent and both are asserted; whether a config-file author predicts either is flagged for `/gsd-verify-work` (coverage D10).
- **Deferred item 12 did not reproduce.** Six full-suite runs across three build trees during this plan, all 226/226. Recorded because the flake is quantified at roughly one test per run and its absence over six runs is worth knowing.

## Self-Check: PASSED

All 1 created file and 5 modified files exist on disk; all 4 claimed commit hashes resolve in `git log`.
