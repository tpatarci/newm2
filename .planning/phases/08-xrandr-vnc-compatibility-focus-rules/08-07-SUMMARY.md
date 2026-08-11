---
phase: 08-xrandr-vnc-compatibility-focus-rules
plan: 07
subsystem: input
tags: [x11, focus, click-to-focus, auto-raise, raise-on-focus, config, xtest, catch2, ctest, cmake, event-loop]

requires:
  - phase: 08-xrandr-vnc-compatibility-focus-rules
    plan: 01
    provides: WmFixture, XTestDriver (the dedicated grab-control input connection every case here drives), tests/lsan.supp, the preflight fixture, D-33 tags-as-labels
  - phase: 08-xrandr-vnc-compatibility-focus-rules
    plan: 02
    provides: scripts/gates/build-all.sh and scripts/analysis/run-static-analysis.sh, used as this plan's verification evidence
  - phase: 08-xrandr-vnc-compatibility-focus-rules
    plan: 04
    provides: tests/test_wm_geometry.cpp -- the multi-group fixture layout this file mirrors, the pumpWm() workaround, and the measurement of the circulate() spin fixed here
  - phase: 08-xrandr-vnc-compatibility-focus-rules
    plan: 06
    provides: the retrying WmFixture readiness probe that makes 7 more fixture startups per run affordable
provides:
  - The three focus-policy booleans wired to real runtime branches (click-to-focus, auto-raise, raise-on-focus)
  - Corrected shipped defaults that describe what the binary actually does (D-17)
  - A startup banner that reports the focus policy this process is running, not a hardcoded claim
  - tests/test_wm_focus.cpp [wm_focus] -- 7 process-level cases, every gate negative-tested
  - tests/test_wm_process.cpp [wm_circulate] -- the circulate() spin regression test
  - wm2test::processCpuTicks() -- CPU-time sampling, the way "idle" is distinguished from "alive"
  - WindowManager::circulate() terminates when no client is eligible (deferred item 6 closed)
affects: [08-08, 08-09, 08-10, 08-11, 08-12, 08-13, 08-14]

actuals:
  tokens: 16509
  tasks: 4
  commits: 6

tech-stack:
  added: []
  patterns:
    - "A settle before a non-event assertion must be REPEATED and SPACED -- N wake-ups back to back are not N wake-ups, because the process under test is not necessarily scheduled between them"
    - "A regression test establishes its own precondition rather than inheriting one from another open defect, or it goes silently vacuous the day that defect is fixed"
    - "Assert BOTH responsiveness and idleness when testing a loop: 'alive' and 'working' are different claims and a spin satisfies the first"
    - "When a default value and the runtime disagree, and nothing reads the value, correct the VALUE -- resolving it the other way silently changes behaviour for every existing user"
    - "Record what a negative test disproved, not just what it proved: the feared failure mode that did not materialise is worth a comment so nobody re-derives it"

key-files:
  created:
    - tests/test_wm_focus.cpp
  modified:
    - src/Client.cpp
    - src/Manager.cpp
    - src/Buttons.cpp
    - include/Config.h
    - tests/test_config.cpp
    - tests/test_wm_process.cpp
    - tests/support/WmFixture.h
    - CMakeLists.txt
    - .planning/phases/08-xrandr-vnc-compatibility-focus-rules/deferred-items.md
    - .planning/STATE.md

key-decisions:
  - "FOCUS-02 was unimplemented, not merely unproven: the three booleans had zero runtime consumers, so the WM did delayed focus-follows-pointer with auto-raise whatever the user configured. Each now reaches a real branch"
  - "The auto-raise gate sits where the deadline is ARMED, not in the expiry branches, so computePollTimeout() reports no active deadline and the loop blocks indefinitely"
  - "D-17 resolved in favour of behaviour: raiseOnFocus and autoRaise now default TRUE, which is what users already got. The all-false defaults had never described the binary"
  - "The startup banner now reports the running focus policy. It claimed 'Focus follows pointer' unconditionally, which becomes a lie under click-to-focus -- and that transcript is the XDIS-05 evidence artefact"
  - "Deferred item 6 (circulate() spin) fixed with a bounded scan; the regression test creates its OWN transient client rather than relying on deferred item 7's adopted WM windows, so it cannot go vacuous when item 7 is fixed"
  - "The idle-CPU case records that it does NOT discriminate the auto-raise gate's placement: the feared gate-at-expiry spin was implemented and measured at 0 ticks"
  - "settleWm() spaces its pumps 20ms apart. Fifteen back-to-back nudges left a deleted gate green; the same fifteen at 20ms intervals turn it red every time"

patterns-established:
  - "Every gate added to production code is negative-tested by deleting it and showing the specific case that reddens -- and the deletion result is recorded even when it is a surprise"
  - "A non-event assertion is not trustworthy until its own negative test has been shown to fail; two harness defects here were only visible that way"

requirements-completed: [FOCUS-02]

coverage:
  - id: D1
    description: "Each of the three focus booleans changes observable runtime behaviour of the compiled binary; none is read only by tests any more (D-15, FOCUS-02)"
    requirement: FOCUS-02
    verification:
      - kind: integration
        ref: "tests/test_wm_focus.cpp [wm_focus] -- 7 cases, each launching the real binary with the boolean flipped on its command line and observing the outcome through the X server"
        status: pass
      - kind: other
        ref: "negative-tested, one gate at a time: deleting the click-to-focus gate reddens behaviour 1; deleting the auto-raise gate reddens behaviour 3; deleting both raise-on-focus gates reddens behaviour 5"
        status: pass
      - kind: other
        ref: "grep: clickToFocus in src/Client.cpp = 1, raiseOnFocus in src/Client.cpp = 2 (the two split sites), mapRaised in src/Client.cpp unchanged at 10, focus-policy key strings = 0 in both Config files"
        status: pass
    human_judgment: false
  - id: D2
    description: "With click-to-focus on, pointer entry does not focus and a click does; with it off, pointer entry starts focus tracking as it does today"
    requirement: FOCUS-02
    verification:
      - kind: integration
        ref: "behaviour 1 asserts both halves in one case (a WM that focused nothing at all would pass the negative half alone); behaviour 2 is the control on the same geometry with the flag inverted"
        status: pass
      - kind: other
        ref: "negative-tested: with the gate deleted, behaviour 1's negative half is red"
        status: pass
    human_judgment: false
  - id: D3
    description: "With auto-raise off no deadline is armed and the loop blocks with no timeout rather than waking periodically -- which also satisfies the checklist's no-idle-CPU-spin item"
    requirement: FOCUS-02
    verification:
      - kind: integration
        ref: "behaviour 3 -- pointer entry raises nothing and focuses nothing; behaviour 4 is the control with auto-raise on, where the same entry does raise"
        status: pass
      - kind: integration
        ref: "behaviour 7 -- 3s of complete quiet with focus tracking live, CPU delta read from /proc: 0 ticks against a bound of 20"
        status: pass
      - kind: other
        ref: "negative-tested: arming the deadline unconditionally reddens behaviour 3"
        status: pass
    human_judgment: true
    rationale: "The no-idle-spin half is measured directly and is a proven spin detector (the same measurement read 595 ticks on the circulate() defect and 0 after the fix). The stronger claim -- that the poll loop blocks with NO timeout rather than a short one -- is proven structurally (computePollTimeout returns -1 when neither deadline is active, and behaviour 3 shows the auto-raise deadline is never armed) rather than by observing the poll syscall. The gate-in-the-wrong-place variant was implemented and also measured 0, so this case is explicitly NOT evidence for the gate's placement; that is recorded at the case."
  - id: D4
    description: "With raise-on-focus off, focusing a window activates it without raising it; with it on, the pair fires together as it does today"
    requirement: FOCUS-02
    verification:
      - kind: integration
        ref: "behaviour 5 -- the window becomes active (REQUIRE) and its stacking index is unchanged (CHECK), an assertion pair that is only expressible once the fused activate+raise is split; behaviour 6 is the control"
        status: pass
      - kind: other
        ref: "negative-tested: restoring the unconditional mapRaised() at both sites reddens behaviour 5"
        status: pass
    human_judgment: false
  - id: D5
    description: "The shipped defaults reproduce today's actual behaviour -- pointer focus with auto-raise -- rather than the literal current values (D-17)"
    requirement: FOCUS-02
    verification:
      - kind: unit
        ref: "tests/test_config.cpp -- default assertions state the new values explicitly; 44 config tests (was 40), including new CLI coverage for --raise-on-focus/--no-raise-on-focus and two whole-chain cases asserting a CLI flag beats a config-file value in both directions for all three booleans"
        status: pass
      - kind: other
        ref: "the two cases that would have gone vacuous against a true default were strengthened rather than left: the XDG-precedence case now writes `auto-raise = false`, and the --auto-raise case forces the value off first"
        status: pass
      - kind: other
        ref: "all six focus flags parse on the real binary (exit 1 at display-open, not exit 2 at getopt); the WM's own banner reports the resulting policy and every [wm_focus] case asserts that banner line before relying on the configuration"
        status: pass
    human_judgment: false
  - id: D6
    description: "The three booleans remain three booleans with their existing config keys and CLI flags; no focus-policy enum key is introduced (D-16)"
    requirement: FOCUS-02
    verification:
      - kind: other
        ref: "grep: 0 occurrences of a focus-policy key in src/Config.cpp and include/Config.h; the eight existing long options (three enable, three negate, plus the two exec-using-shell) are untouched and all six focus flags are asserted in test_config"
        status: pass
    human_judgment: false
  - id: D7
    description: "deferred item 6 -- circulate() no longer spins when no client is eligible (additional in-scope work, authorised by the user for this plan)"
    requirement: FOCUS-02
    verification:
      - kind: integration
        ref: "tests/test_wm_process.cpp [wm_circulate] -- RED before the fix at 595 CPU ticks with the post-click window never framed; green in 2.2s after, asserting both responsiveness and idleness"
        status: pass
      - kind: other
        ref: "behaviour-preservation checked by the [wm_geometry] clamp cases, which reach ensureVisible() through this exact call and still pass"
        status: pass
    human_judgment: false

duration: 55min
completed: 2026-08-11
status: complete
---

# Phase 08 Plan 07: Focus-Policy Wiring Summary

**The three focus booleans users have been able to set since Phase 5 now do something: each reaches a real runtime branch, the shipped defaults finally describe what the binary actually does, seven process-level cases prove each mode produces a different observable behaviour in the running window manager, and the 100%-CPU root-right-click freeze that three prior plans flagged is fixed with a regression test that measures the freeze rather than describing it.**

## Performance

- **Duration:** 55 min
- **Tasks:** 4 (3 planned + the authorised circulate fix)
- **Commits:** 6
- **Files:** 11 (1 created, 10 modified)

## What was actually wrong

FOCUS-02 was not an unproven claim. It was an **unimplemented feature with a passing test**. `Config` parsed `click-to-focus`, `raise-on-focus` and `auto-raise` from the config file and from six CLI flags, `tests/test_config.cpp` asserted the parsed values, and **nothing else in the codebase ever read them**. The WM performed delayed focus-follows-pointer with auto-raise, and fused raising into focusing, no matter what a user configured — while the shipped defaults said all three were off.

So there were two defects on top of each other: three dead switches, and a set of defaults that contradicted the binary. Fixing only the first would have silently changed behaviour for every existing user the moment the gates landed — which is why D-17's correction is a *prerequisite* of the wiring, not a tidy-up after it.

## Accomplishments

- **Three gates, each negative-tested individually.** Click-to-focus suppresses the pointer-entry route into focus tracking; auto-raise gates the deadline at the point it is **armed**, so the poll loop blocks with no timeout instead of waking on a timer that would do nothing; raise-on-focus splits the previously inseparable activate-and-raise pair at both of its sites. Deleting any one of them reddens exactly one case.
- **Defaults corrected in the direction of the binary, and the tests strengthened rather than loosened.** Two config cases would have become vacuous against a true default (a precondition asserting `false`, and a config file writing `true`); both were changed to keep discriminating. `--raise-on-focus`/`--no-raise-on-focus` had **no CLI coverage at all** in either direction and now do, along with two whole-chain cases asserting a CLI flag beats a config-file value both ways for all three booleans. 40 → 44 config tests.
- **The startup banner stopped lying.** It printed `Focus follows pointer.` unconditionally. That is a false statement the instant a user sets click-to-focus, and the startup transcript is the per-target evidence artefact XDIS-05 collects. It now reports the policy the process is actually running, and every `[wm_focus]` case asserts the relevant banner line before relying on its configuration — so a case cannot pass on a WM that quietly ignored its flags.
- **Deferred item 6 closed with measurement, not description.** `circulate()` froze the WM at 100% CPU on a root right-click with no eligible client. The regression test measured it at **595 CPU ticks during a 6-second probe with the post-click window never framed**; after the bounded-scan fix the same case is green in 2.2 s at 0 ticks.
- **Two harness defects found by negative-testing, not by the tests passing.** Both are in Deviations. Neither would have been visible any other way: the suite was 8-for-8 green with a production gate deleted.

## Task Commits

1. **Circulate regression test (RED)** — `39cc5dc` (test)
2. **Circulate fix (GREEN)** — `62e9c0d` (fix)
3. **Deferred item 6 / STATE blocker closed** — `fb8b07a` (docs)
4. **Task 1: the three focus gates** — `22f26b3` (feat)
5. **Task 2: corrected defaults + banner** — `19638ce` (feat)
6. **Task 3: process-level focus tests** — `532cc6e` (test)

## Files Created/Modified

- `tests/test_wm_focus.cpp` — **new.** 7 `[wm_focus]` cases plus the stacking/exposed-point/settle helpers.
- `src/Client.cpp` — the click-to-focus gate in `eventEnter()`; the raise-on-focus split at `focusIfAppropriate()` and `eventFocusIn()`.
- `src/Manager.cpp` — the auto-raise gate at the deadline-arming site; the policy-reporting startup banner.
- `src/Buttons.cpp` — `circulate()`'s bounded scan.
- `include/Config.h` — the D-17 default correction and the reason for it.
- `tests/test_config.cpp` — updated default assertions, two strengthened cases, four new cases.
- `tests/test_wm_process.cpp` — the `[wm_circulate]` case and its helpers.
- `tests/support/WmFixture.h` — `processCpuTicks()`.
- `CMakeLists.txt` — the `test_wm_focus` target; ctest `TIMEOUT` on both process-level targets.
- `deferred-items.md`, `.planning/STATE.md` — item 6 closed.

## Decisions Made

See `key-decisions` in the frontmatter. The two worth reading twice:

**The auto-raise gate is at the arming site, and the alternative was actually built and measured.** The plan forbids gating the expiry branch, on the reasoning that an armed-but-ignored deadline would leave `computePollTimeout()` returning 0 forever and spin the loop. That variant was implemented and the CPU delta measured: **0 ticks**, not a spin — something else on that path clears the deadline. The arming site is still the right place (it is the only form in which the loop provably blocks with no timeout, and it is what the plan specifies), but the feared failure mode does not occur, and the idle-CPU case now says so at the point where a reader would otherwise assume it was the evidence.

**The circulate regression test creates its own precondition.** The obvious version — start the WM with no clients and let the adopted menu/WM-check windows (deferred item 7) keep `m_clients` non-empty — would pass today and go **silently vacuous** the day item 7 is fixed, still green while testing nothing. The case maps its own transient instead: `Normal` but `isTransient()`, exactly what the scan's break condition rejects. The list is non-empty and eligible-free under the test's own control whichever way item 7 goes.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] A single pump before a non-event read observes the previous state, so the click-to-focus case was green with its gate deleted**
- **Found during:** Task 3, negative-testing the click-to-focus gate
- **Issue:** deleting the gate should have reddened behaviour 1's negative half. It stayed green. Traced through the WM: the `EnterNotify` was delivered, `considerFocusChange` ran, the auto-raise deadline fired and `activate()` published `_NET_ACTIVE_WINDOW` — but the test still read the old value. Deferred item 9: the WM's output sits in its buffer until its loop wakes again.
- **Fix:** a `settleWm()` helper before every non-event read.
- **Verification:** see deviation 2 — the first version of this fix was not enough either.
- **Committed in:** `532cc6e`

**2. [Rule 1 - Bug] Fifteen wake-ups back to back are not fifteen wake-ups**
- **Found during:** Task 3, immediately after deviation 1's fix failed to redden the case
- **Issue:** `settleWm()` initially pumped ten times in a tight loop. The negative test was **still** green. The WM is not necessarily scheduled between two pumps issued microseconds apart, so ten nudges amount to roughly one wake-up. Confirmed by instrumenting the case with a 6-second sampling timeline: with pumps spaced 200 ms apart the property flipped to the correct value and stayed there; with the tight loop it never did.
- **Fix:** `settleWm()` now pumps 15 times at 20 ms intervals. The deleted gate reddens the case every time.
- **Verification:** gate deleted → behaviour 1 red; gate restored → 3 consecutive green runs of the whole group.
- **Committed in:** `532cc6e`
- **Why this matters beyond this file:** every "the WM did nothing" assertion in this phase depends on a settle of this shape. `tests/test_wm_geometry.cpp` uses a ten-pump tight loop for its own non-event assertions (case 9). It passes, and its negative tests were recorded as red at the time — but the spacing point is worth checking there if that case ever starts looking suspiciously robust.

**3. [Rule 1 - Bug] The circulate fix introduced a new cppcheck finding**
- **Found during:** the circulate fix, running the static-analysis gate
- **Issue:** `variableScope` — `j` was still declared in the outer scope after the rewrite stopped using it there. The gate is a strict baseline comparison, so one new finding fails it.
- **Fix:** `j` declared `const` inside the loop. `cppcoreguidelines-init-variables` also dropped 142 → 141, because the old `int i = -1, j;` left `j` uninitialised.
- **Verification:** `run-static-analysis.sh` exit 0, cppcheck back to its 18-finding baseline.
- **Committed in:** `62e9c0d`

**4. [Rule 2 - Missing critical functionality] The startup banner claimed a focus policy it no longer necessarily had**
- **Found during:** Task 2
- **Issue:** `Focus follows pointer.` was printed unconditionally. Once the booleans are live this is false under click-to-focus, and the startup transcript is exactly what XDIS-05's per-target evidence capture reads. It also left the `[wm_focus]` cases with no way to prove their flags had reached the running process rather than being silently dropped.
- **Fix:** the banner reports all three policies. Every focus case asserts its own banner line as a precondition.
- **Verification:** no test or script anywhere consumed the old string (checked); all seven cases assert the new lines.
- **Committed in:** `19638ce`

**5. [Rule 2 - Missing critical functionality] A ctest TIMEOUT on the process-level targets**
- **Found during:** the circulate work, satisfying the user's "hard timeout so a regression fails rather than hanging the suite" requirement
- **Issue:** every case is deadline-bounded internally, but nothing structural stopped a future edit that lost a deadline from stalling the suite — and the `[wm_circulate]` case exists precisely to exercise a WM that stops responding.
- **Fix:** `TIMEOUT 240` on `test_wm_process` and `test_wm_focus`. Generous against the observed per-case cost (~11 s debug, more under ASan), so it can never redden a healthy run.
- **Committed in:** `39cc5dc`, `532cc6e`

### Plan-text corrections (no code impact)

**6. `--help` does not exist in this binary.** Task 2's criterion runs `./build/debug/wm2-born-again --no-auto-raise --help`. There is no `--help` long option (Phase 5 never added one — `applyCliArgs` only *mentions* it in the error message for an unrecognised option), so that command exits 2 on `--help` itself and proves nothing about the focus flag. The criterion's intent was verified directly instead: each of the six focus flags was run against an unreachable display, and all six reached `wm2: can't open display` with exit 1 and **zero** `unrecognized option` messages — i.e. getopt accepted every one. Adding `--help` was declined as out-of-scope surface for a plan whose prohibitions include not touching the CLI flag set.

**7. `grep -c 'DISPLAY=:99' CMakeLists.txt` is 5, not 4.** Measured at this plan's parent commit and at 08-06's: 5 in both. Four `ENVIRONMENT` properties plus the one inside the `start_xvfb` readiness command. The plan's "unchanged from 08-01" is right; its number is not.

**8. `grep -c 'autoRaise' src/Manager.cpp` was already ≥1 before this plan.** `m_autoRaiseDeadline` and `m_config.autoRaiseDelay` both match the substring, so that criterion was satisfiable without adding any gate at all. The real gate was added regardless; noted so nobody reads that criterion as evidence.

---

**Total deviations:** 5 auto-fixed (2 harness defects that were making a negative test lie, 1 static-analysis regression in this plan's own new code, 2 gaps that would have left this plan's own claims unprovable), 3 plan-text corrections. No architectural decisions required.

## Issues Encountered

- **`bugprone-branch-clone` in `src/Buttons.cpp` — investigated, as the phase notes requested. Not defects.** All four are `if (A) x = -1; else if (B) x = -1;` in the menu hover/commit paths: two different range checks with the same assignment, mergeable with `||` but harmless. There **is** a real asymmetry inside them worth recording: the commit paths test `sel >= n` / `sel2 >= n2` while the hover paths test `selecting > n` / `selecting2 > n2`, so the hover paths admit the one-past-the-end index. It is not reachable as a defect — every downstream use re-guards (`selecting >= 0 && selecting < n` for the highlight, `selecting >= nh && selecting < nh + numCategories` before indexing `m_appCategories`) — so nothing was changed. Count remains 6, unchanged.
- **The idle-CPU case is weaker than it first reads.** Stated at the case and in the coverage rationale rather than left to be discovered.
- **`_NET_ACTIVE_WINDOW` can be transiently correct and then cleared.** While tracing deviation 1 the WM was observed publishing the client, then publishing `None` again after a `mapRaised()`-induced crossing event. The polling cases (behaviours 2, 4, 6) return on first success and are therefore satisfied by a transient state; the non-event cases read the settled state. Both are legitimate for what they assert — "focus did reach this window" and "focus never reached this window" — but a future plan asserting *steady-state* focus should not assume `pollUntil` gives it.
- **Full process-level suite: 183/183 green in one shot this run.** Deferred item 12's ~1-flake-per-run was not observed here, on a suite 12 tests larger than 08-06's. One data point, not a claim that it is gone.
- **The Release-only `-Wunused-result` in `sigHandler`** remains (deferred item 4). Debug `build.log` has 0 warnings.

## Known Stubs

None. Every branch added by this plan is reachable at runtime and is exercised by an assertion that has been shown to go red when the branch is removed.

Two places where a claim is narrower than it looks, stated rather than hidden:

- **The idle-CPU case does not discriminate the auto-raise gate's placement.** Measured, recorded at the case, and carried into the D3 coverage rationale.
- **The "poll loop blocks with no timeout" half of D3 is structural, not observed.** `computePollTimeout()` returns -1 when neither deadline is active, and behaviour 3 shows the auto-raise deadline is never armed — but nothing here watches the `poll()` syscall itself.

## Deferred Issues

- **Item 6 is CLOSED** (`62e9c0d`), with its STATE.md blocker cleared.
- **Item 9 is now blocking test design, not just test convenience.** Two of this plan's three negative tests failed to fail because of it, and the cure (a spaced multi-pump settle) is a workaround every future non-event assertion in this phase will have to copy. Its priority for 08-11 … 08-13 should go up.
- **`tests/test_wm_geometry.cpp` case 5's comment is now history.** It explains that it declines to drive the clamp on a non-Normal client because circulate would wedge the WM. That restriction is gone. Left in place — rewriting that file is not this plan's job — but noted in `deferred-items.md` so it is not read as a live constraint.
- Items 1–5, 7–12 otherwise unchanged.

## Threat Flags

None. No new network endpoint, auth path, file-access pattern or trust-boundary schema change.

The plan's own register is addressed:

- **T-8-FOCUS (mitigate)** — no new focus-grant route was added. The gates only *suppress* existing routes; every activation still originates in a crossing or button event the server delivered, and `Client::eventButton()`'s `send_event` refusal is untouched (which is precisely why the tests must drive XTEST). Focus-stealing arbitration remains 08-08's.
- **T-8-CFG (mitigate)** — no new parsing surface. The same Phase 5 boolean parser, the same keys, the same six flags; only three literal default values changed. Line- and value-length limits untouched.
- **T-8-SPIN (mitigate)** — asserted directly by behaviour 7 at 0 ticks over 3 s. And, unplanned but in the same family, a genuine 100%-CPU denial of service reachable by any user with a mouse was removed (`circulate()`), with its own measured before/after.
- **T-8-SC (accept)** — unchanged. No dependency of any ecosystem was added; `PkgConfig::XTST` was already a declared test-only dependency from 08-01.

## Verification Results

| Plan verification step | Result |
|---|---|
| `ctest -L '^(wm_focus\|config\|autoraise)$'` | PASS |
| `ctest --test-dir build/debug` (full suite) | PASS (183/183 in one shot) |
| Every focus boolean has a runtime consumer outside `tests/` | PASS (`src/Client.cpp`, `src/Manager.cpp`, plus `src/Config.cpp` and `include/Config.h`) |
| `bash scripts/analysis/run-static-analysis.sh` | PASS (exit 0; cppcheck 18/18 baseline, 0 fatal clang-tidy) |
| `bash scripts/gates/build-all.sh debug` | PASS (0 warnings in build.log) |
| `bash scripts/gates/build-all.sh asan` | PASS (0 sanitizer findings; 2 matched libfontconfig suppressions) |
| `ctest --test-dir build/asan -L '^wm_focus$'` | PASS (8/8, **0** ASan report files) |
| `ctest -L '^wm_focus$'` three consecutive times | PASS, PASS, PASS (no flakes) |
| Task 1 grep criteria (clickToFocus 1, raiseOnFocus 2, mapRaised unchanged at 10, consumers 5, focus-policy 0/0) | PASS |
| Task 2 grep criteria (three exact default literals = 1 each; test_config counts 15/17/34 vs ≥4) | PASS |
| Task 2 CLI criterion | PASS by substitute — see plan-text correction 6 (`--help` does not exist) |
| Task 3 grep criteria (file exists, XTestDriver 8, `sleep(` 0, `PkgConfig::XTST` 2) | PASS |
| Task 3 `DISPLAY=:99` = 4 | Criterion wrong: 5, and 5 at the parent commit too — correction 7 |
| `ctest -L '^wm_focus$'` reports 7 cases + preflight | PASS (8 total, the same +1 arithmetic every prior plan recorded) |
| `[wm_circulate]` before the fix | RED — 595 CPU ticks, post-click window never framed |
| `[wm_circulate]` after the fix | PASS (2.2 s; 0 idle ticks over 2 s) |
| Negative: delete the click-to-focus gate | PASS (behaviour 1 red — after deviations 1 and 2 were fixed) |
| Negative: arm the auto-raise deadline unconditionally | PASS (behaviour 3 red) |
| Negative: restore the unconditional `mapRaised()` at both sites | PASS (behaviour 5 red) |
| Negative: move the auto-raise gate to the expiry branch | **No spin** — 0 CPU ticks; the feared failure mode does not occur (recorded, not hidden) |
| Probe: all six focus CLI flags against an unreachable display | CONFIRMED (exit 1 at display-open, 0 getopt errors) |
| Probe: `_NET_ACTIVE_WINDOW` timeline under a deleted gate | CONFIRMED (stale for a tight-loop settle, correct for a spaced one) |
| Probe: `bugprone-branch-clone` sites in `src/Buttons.cpp` | CONFIRMED not defects; the `> n` / `>= n` asymmetry is re-guarded downstream |

## Self-Check: PASSED

All six commit hashes (`39cc5dc`, `62e9c0d`, `fb8b07a`, `22f26b3`, `19638ce`, `532cc6e`) resolve in `git log`. `tests/test_wm_focus.cpp` exists on disk; all ten modified files exist with the claimed content. `src/` and `include/` are byte-identical to their committed state after the four negative-test experiments — every temporary edit was reverted with a per-file `git checkout` and `grep -c 'TRACE'` reports 0 in both instrumented files. No worktrees were created.

## Next Phase Readiness

- **FOCUS-02 is closed and 08-08 inherits a live focus path.** Focus-stealing arbitration now has three real switches to arbitrate against rather than three dead values, and `[wm_focus]` is the group it should extend — the file's fixture helper takes an arbitrary flag list with no build plumbing.
- **The banner is now a focus-policy oracle.** Any later plan that needs to prove a WM child received its configuration can assert one line of stderr instead of inferring it from behaviour. 08-14's evidence capture gets all three policies for free alongside the Shape/RANDR/XRender lines.
- **The 100%-CPU root-right-click freeze is gone**, which removes the standing restriction 08-04 and 08-05 both had to design around. Any plan may now drive `circulate()` on a client in any state.
- **`processCpuTicks()` is available to every process-level suite**, and the pattern it enables — asserting *idle*, not merely *alive* — is the one that caught item 6 and would catch its next relative.
- **Carry-forward for 08-11 … 08-13:** deferred item 9 has moved from "annoying flake" to "makes negative tests lie". The spaced-settle workaround is documented at `settleWm()`, but the real fix is at the source.

---
*Phase: 08-xrandr-vnc-compatibility-focus-rules*
*Completed: 2026-08-11*
