---
phase: 08-xrandr-vnc-compatibility-focus-rules
plan: 08
subsystem: window-management
tags: [x11, ewmh, focus, focus-stealing-prevention, net-wm-user-time, security, asan]

requires:
  - phase: 06-ewmh-compliance
    provides: "the _NET_WM_STATE publisher, the atom table, and decision D-10 (always-grant activation), which this plan supersedes"
  - phase: 08-xrandr-vnc-compatibility-focus-rules/08-07
    provides: "the three focus-policy gates, the corrected D-17 defaults, tests/test_wm_focus.cpp and its spaced settleWm() methodology"
provides:
  - "Map-time focus arbitration: a window whose _NET_WM_USER_TIME predates the last real user interaction is mapped unfocused and flagged"
  - "Activation-message arbitration by EWMH source indication, closing the bypass that made the map-time half decorative"
  - "A visible refusal: _NET_WM_STATE_DEMANDS_ATTENTION plus the ICCCM urgency hint, set together and cleared on activation"
  - "The focus-stealing-prevention off switch (config key + CLI pair), defaulting on"
  - "Five newly interned and advertised EWMH atoms, two of them staged for plan 08-10"
  - "A use-after-free fix in the client destruction path (T-8-UAF)"
affects: [08-10 window rules and skip-taskbar, 08-11..08-13 test infrastructure, any later work touching focus or _NET_WM_STATE]

actuals:
  tokens: 20500
  tasks: 4
  commits: 6

tech-stack:
  added: []
  patterns:
    - "One shared timestamp arbiter (WindowManager::isUserTimeRecent) consumed by both focus entry points, never reimplemented"
    - "Untrusted client properties read through a bounded, type/format/count-checked, always-freed reader"
    - "Unfocused test preconditions constructed explicitly via a zero _NET_WM_USER_TIME rather than inherited from WM defaults"

key-files:
  created: []
  modified:
    - src/Client.cpp
    - include/Client.h
    - src/Events.cpp
    - src/Config.cpp
    - tests/test_wm_focus.cpp
    - tests/test_wm_process.cpp
    - .planning/phases/06-ewmh-compliance/06-CONTEXT.md

key-decisions:
  - "Activation requests are arbitrated by source indication (option-a): pager granted, application arbitrated, source-less legacy granted, refusal flagged. Supersedes Phase 6 D-10, amended append-only in its own record."
  - "Map-time focus granting is NOT gated on the pointer-entry policy. The plan's behaviour list and 08-07's behaviour 1 were in genuine conflict; resolved in favour of FOCUS-01 by reconstructing the two affected test preconditions, not by weakening their assertions."
  - "A user-time of exactly zero is honoured as the spec's explicit do-not-focus-me at map time, but treated as absent evidence (and therefore arbitrated as stale) on a request to BE focused."
  - "_NET_WM_STATE_DEMANDS_ATTENTION is published only through the existing updateNetWmState() writer, accepting a lower grep count than the plan predicted rather than adding a second writer."

patterns-established:
  - "Mutation testing as the acceptance gate for security-relevant branches: every new guard was deleted and the suite confirmed to redden"
  - "Test preconditions that depend on absent WM behaviour are made explicit the moment that behaviour stops being absent"

requirements-completed: [FOCUS-01]

coverage:
  - id: D1
    description: "A newly mapped window whose user-time is older than the last real user interaction is mapped unfocused and marked as demanding attention"
    requirement: FOCUS-01
    verification:
      - kind: e2e
        ref: "tests/test_wm_focus.cpp#A window mapped with a stale user-time is refused focus and demands attention"
        status: pass
    human_judgment: false
  - id: D2
    description: "A window that publishes no user-time is focused normally, so legacy X clients are not punished (D-19)"
    requirement: FOCUS-01
    verification:
      - kind: e2e
        ref: "tests/test_wm_focus.cpp#A window mapped with no user-time property is focused"
        status: pass
    human_judgment: false
  - id: D3
    description: "A user-time of exactly zero is honoured as the spec's explicit do-not-focus-me, including before any user interaction has occurred"
    requirement: FOCUS-01
    verification:
      - kind: e2e
        ref: "tests/test_wm_focus.cpp#A window mapped with a user-time of exactly zero is not focused"
        status: pass
      - kind: e2e
        ref: "tests/test_wm_focus.cpp#A user-time of zero is refused even before any user interaction"
        status: pass
    human_judgment: false
  - id: D4
    description: "The user-time-window proxy is consulted, so a client using that mechanism is arbitrated rather than exempt"
    requirement: FOCUS-01
    verification:
      - kind: e2e
        ref: "tests/test_wm_focus.cpp#A stale user-time on the user-time-window proxy is arbitrated"
        status: pass
    human_judgment: false
  - id: D5
    description: "Activation client messages are arbitrated by source indication: pager granted, stale application refused, fresh application granted, source-less legacy granted"
    requirement: FOCUS-01
    verification:
      - kind: e2e
        ref: "tests/test_wm_focus.cpp#An activation request from a pager is granted despite a stale timestamp"
        status: pass
      - kind: e2e
        ref: "tests/test_wm_focus.cpp#A stale application activation request is refused and demands attention"
        status: pass
      - kind: e2e
        ref: "tests/test_wm_focus.cpp#A fresh application activation request is granted"
        status: pass
      - kind: e2e
        ref: "tests/test_wm_focus.cpp#An activation request with no source indication is granted"
        status: pass
    human_judgment: false
  - id: D6
    description: "Malformed activation requests (wrong format, unknown source, unmanaged target) are ignored without crashing the WM"
    requirement: FOCUS-01
    verification:
      - kind: e2e
        ref: "tests/test_wm_focus.cpp#Malformed activation requests are ignored without crashing the WM"
        status: pass
    human_judgment: false
  - id: D7
    description: "Focus-stealing prevention can be switched off from the config file and the command line, and defaults to on"
    requirement: FOCUS-01
    verification:
      - kind: unit
        ref: "tests/test_config.cpp — 4 cases covering the default and all four precedence steps"
        status: pass
      - kind: e2e
        ref: "tests/test_wm_focus.cpp#With focus-stealing prevention off a stale window is focused anyway"
        status: pass
      - kind: e2e
        ref: "tests/test_wm_focus.cpp#With focus-stealing prevention off a stale activation request is granted"
        status: pass
    human_judgment: false
  - id: D8
    description: "The five atoms the behaviour depends on are interned and advertised in _NET_SUPPORTED"
    requirement: FOCUS-01
    verification:
      - kind: manual_procedural
        ref: "xprop -root _NET_SUPPORTED against a live WM on Xvfb :97 — all five present"
        status: pass
    human_judgment: false
  - id: D9
    description: "The refusal is discoverable by the whole desktop, not only EWMH-aware clients"
    requirement: FOCUS-01
    verification:
      - kind: e2e
        ref: "tests/test_wm_focus.cpp#Activating a refused window clears its demands-attention state (asserts both _NET_WM_STATE and XUrgencyHint, set and cleared)"
        status: pass
    human_judgment: false
  - id: D10
    description: "Whether the shipped focus behaviour feels right in daily VNC use — a newly launched app coming to the front, a background app not stealing keystrokes"
    verification: []
    human_judgment: true
    rationale: "The mitigation's value is a judgment about interruption, and the planner flagged two assumptions for /gsd-verify-work: that prevention is only as strong as client honesty, and that the first window mapped before any user input is granted focus. Both are now asserted by tests, but whether the resulting feel is correct is not a property any test can settle."

duration: 115min
completed: 2026-08-29
status: complete
---

# Phase 08 Plan 08: Focus-Stealing Prevention Summary

**A window that pops up while you are working elsewhere now gets a highlighted tab instead of your keystrokes — enforced at both the map-time and the activation-message entry points, so it cannot be bypassed by simply asking for focus.**

## Performance

- **Duration:** ~115 min (continuation session; earlier RED/atom work landed in a prior session)
- **Tasks:** 4 of 4
- **Files modified:** 11
- **Commits:** 6 in this session, 8 for the plan overall

## Accomplishments

- **FOCUS-01 delivered at both entry points.** `Client::shouldFocusOnMap()` arbitrates newly mapped windows, and `WindowManager::eventClient()` arbitrates `_NET_ACTIVE_WINDOW` requests. Both call the *same* `isUserTimeRecent()` helper — a security rule implemented twice is a security rule that drifts.
- **The Phase 6 D-10 conflict resolved on the record.** Always-grant is superseded by source-indication arbitration, and the amendment is appended beneath the original D-10 text in `06-CONTEXT.md`, which stays intact and legible.
- **Refusal is visible.** A refused window advertises `_NET_WM_STATE_DEMANDS_ATTENTION` *and* the ICCCM urgency hint, set and cleared together, so both halves of the desktop can see it.
- **A heap-use-after-free found and fixed.** The ASan gate caught `clearFocus()` reading a freed `Client` through a dangling revert pointer — latent in the destruction path for some time, made routinely reachable by this plan.
- **Every new guard mutation-tested.** Twelve separate deletions of production logic, each confirmed to redden the specific tests that claim to cover it. One of those mutations exposed a test that was not testing what it was named for, which is now fixed.

## Task Commits

1. **Task 1: activation-policy decision** — resolved before this session (option-a); no code commit
2. **Task 2: five atoms + interaction clock** — `0fd7b5f` (feat, prior session)
3. **Task 3: map-time arbitration** — `f2ce907` (test/RED, prior session) → `8d37e61` (feat, config off switch) → `1f84763` (feat, GREEN)
4. **Task 4: activation arbitration + D-10 amendment** — `2a12fe0` (test/RED) → `b0d237b` (feat, GREEN)

Supporting commits: `9952064` (test, process-suite preconditions), `642c4a3` (fix, T-8-UAF).

## The behaviour-1 conflict, and how it was resolved

The previous executor stopped here, having spotted that the plan's behaviour list appeared to contradict 08-07's behaviour 1. **The conflict was real**, and it is worth recording precisely because the resolution changes two existing tests.

**The contradiction.** The plan requires (behaviour 10 / D-19) that *a window publishing no user-time is focused at map time*. 08-07's behaviour 1 requires that *a window mapped with no user-time is NOT the active window right after mapping* (`REQUIRE(pumpedActiveWindow != client)`), because before this plan the WM focused nothing at map time at all — `Client::manage()` called `deactivate()` unconditionally, with the comment "Focus follows pointer — don't auto-activate on manage". Both cases map an identical window with the pointer parked away and assert opposite outcomes. They cannot both hold.

**It was confirmed empirically, not argued.** Running the suite with the map-time grant implemented produced exactly two failures — behaviours 1 and 2 — and behaviours 3 through 7 passed untouched. So the conflict is precisely scoped, not systemic.

**The two candidate readings.**

1. *Gate the map-time grant on `clickToFocus`.* This would leave behaviour 1 passing untouched. It was rejected for two independent reasons. First, the plan explicitly prohibits it ("Do not make this depend on pointer-entry policy"). Second — and decisively — **it does not actually work**: behaviour 2 runs with click-to-focus *off*, so the gate would not save it, and behaviour 2 would silently become vacuous (the window would already be focused before the pointer moved, so its `CHECK(focused)` would pass without the pointer route ever running). The gating buys nothing and costs the prohibition.
2. *Grant unconditionally, and reconstruct the two affected preconditions.* Behaviours 1 and 2 need a window that is **not focused** so they can then ask whether the pointer focuses it. Until now they got that for free. They now construct it explicitly, with a `_NET_WM_USER_TIME` of exactly zero — the EWMH's own "do not focus me on map". This is the mechanism the spec provides for the purpose, not a workaround.

**Reading 2 was taken.** No assertion in either case was altered or removed; only the way each reaches its starting state moved from implicit to explicit. For behaviour 2 the change is a strict *strengthening* — without it the case would have gone vacuous, which is precisely the failure mode this phase's methodology warning exists to prevent.

**This was a judgment call, not a user decision**, because the alternative was independently ruled out by the plan's own prohibition *and* by failing to achieve its purpose. FOCUS-01 ("focus stealing prevention using `_NET_WM_USER_TIME` timestamps") is satisfied either way in the letter; the plan's behaviours 8 and 10 are what require map-time granting to exist at all, and those are unambiguous.

The same conflict class reached two `test_wm_process.cpp` cases (`9952064`), fixed the same way via a shared `setDoNotFocusOnMap()` helper.

## Decisions Made

- **Activation arbitration by source indication** (Task 1 checkpoint, option-a, resolved before this session). Source 2 granted unconditionally; source 1 arbitrated; source 0 granted. The honest ceiling is recorded in the code: a client that lies about its source is granted, but so is one that forges a timestamp, so arbitrating all sources would not raise the ceiling — it would only break pagers.
- **Zero means different things at the two entry points.** At map time, `_NET_WM_USER_TIME == 0` is the spec's explicit "do not focus me" and is honoured as a request. On an activation request the same value is absent evidence, so it arbitrates as stale rather than being granted on the strength of nothing. Both readings are commented at their sites.
- **`_NET_WM_STATE_DEMANDS_ATTENTION` is published through the single existing writer.** The plan's acceptance criteria predicted "at least 2" occurrences in `Client.cpp` implying explicit set/clear writes, while the plan's prose forbade a second write site. The prose won; the count is satisfied instead by `applyWmState()` honouring add/remove/toggle of the state, which is correct EWMH behaviour and lets a pager clear the flag on the window's behalf.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Heap-use-after-free in the client destruction path (T-8-UAF)**
- **Found during:** Task 4 verification, by `scripts/gates/build-all.sh asan`
- **Issue:** `Client::m_revert` is a raw `Client*` naming the focus fallback target, and `WindowManager::clearFocus()` walks that chain calling `isNormal()` on each link. `skipInRevert()` — which repoints anything aimed at a client that is ceasing to be a valid target — had exactly one caller, `Client::activate()`. Nothing called it when a client was **destroyed**, so a dead client remained referenced as a revert target and the next `clearFocus()` read freed memory. Pre-existing in `eventDestroy`, but its *reachability* is what this plan changed: `m_revert` was previously populated only when the user actually focused something, so chains were rare and short; with map-time activation every mapped window activates, and "map two windows, close the first, close the second" now reaches it. The entire `[wm_focus]` group went red under ASan at once.
- **Fix:** `skipInRevert(c, c->revertTo())` in `eventDestroy`, before the erase that frees. Passing the dying client's own target preserves the chain instead of truncating it; that target is live by induction.
- **Files modified:** `src/Events.cpp`
- **Verification:** ASan gate red before with a `heap-use-after-free` report naming `clearFocus()`; "no sanitizer findings" after, same tree, same command.
- **Committed in:** `642c4a3`

**2. [Rule 2 - Missing critical coverage] The zero-user-time rule was not actually tested**
- **Found during:** Task 3 mutation testing
- **Issue:** Deleting the `if (userTime == 0) return false;` branch left the whole suite **green**. The existing case advances the interaction clock with a real button press first, so its zero timestamp is *also* stale and the ordinary comparison refuses the window anyway. The branch is load-bearing in exactly one window of time — before the first user interaction, when the clock is still the `CurrentTime` sentinel of 0 and a zero timestamp compares as *not* stale and would be **granted**.
- **Fix:** Added `A user-time of zero is refused even before any user interaction`, which maps into a freshly started WM with no interaction at all. It also pins the planner's flagged assumption that the first window mapped before any user input is granted focus.
- **Files modified:** `tests/test_wm_focus.cpp`
- **Verification:** Green normally; reddens when the zero branch is deleted.
- **Committed in:** `1f84763`

**3. [Rule 1 - Bug] Two `test_wm_process.cpp` cases regressed on the same precondition class**
- **Found during:** Task 4 full-suite verification
- **Issue:** `A root right-click with no eligible client...` requires `_NET_ACTIVE_WINDOW == None` so `circulate(true)` cannot short-circuit and must run the scan under test; `Synthesised XTEST input on a client's tab...` requires the target unfocused so the post-click assertion is not vacuous. Both assumed the WM focuses nothing at map time. Both reproduced deterministically in isolation, so **not** deferred item 12's flake.
- **Fix:** New `setDoNotFocusOnMap()` helper publishing a zero user-time before mapping. No assertion changed.
- **Files modified:** `tests/test_wm_process.cpp`
- **Verification:** 201/201 full debug suite.
- **Committed in:** `9952064`

### Acceptance-criteria counts that differ from the plan (no action needed)

Three of the plan's literal grep counts do not match, all benign arithmetic in the plan rather than gaps in the implementation:

| Criterion | Expected | Actual | Why |
|---|---|---|---|
| `shouldFocusOnMap` / `demandAttention` in `Client.h` | 1 | 2 | The explanatory comment block names each method in prose; `grep -c` counts those lines too. |
| `focus-stealing-prevention` in `Config.cpp` | 3 | 5 | `no-focus-stealing-prevention` **contains** the shorter string, so the two negation lines are counted twice over. All four parse routes exist and all four config precedence tests pass. |
| `net_wmStateDemandsAttention` in `Client.cpp` | ≥2 | 2 | Satisfied, but by the publisher plus the `applyWmState()` handler rather than by two write sites — see Decisions. |

---

**Total deviations:** 3 auto-fixed (2 × Rule 1 bug, 1 × Rule 2 missing critical coverage)
**Impact on plan:** No scope creep. The use-after-free fix was mandatory — shipping a focus mitigation while making a memory-safety defect routinely reachable would have been a net loss. The added test closed a hole the plan's own behaviour list claimed was covered.

## Issues Encountered

- **The plan's behaviour list was internally inconsistent with 08-07's shipped tests.** Documented in full above. Resolved by reconstructing two test preconditions without altering any assertion.
- **The ASan failure initially looked catastrophic** (every `[wm_focus]` case failing, including 08-07 cases untouched by this plan) and was tempting to write off as environmental. It was a single real defect with a wide blast radius. Reading the actual report rather than the ctest summary was what distinguished the two.
- **`grep -c` counts lines, not occurrences**, and substring containment inflated one acceptance criterion. Worth knowing for future plans that specify exact counts on flag names with `no-` variants.

## Verification Evidence

| Gate | Result |
|---|---|
| `ctest --test-dir build/debug` (full) | **201/201 passed** |
| `ctest -L '^wm_focus$'` | **22/22 passed** (7 from 08-07, 8 map-time, 6 activation, 1 preflight) |
| `ctest -L '^config$'` | **48/48 passed** |
| `scripts/gates/build-all.sh` | **OK: debug release asan** |
| ASan | **no sanitizer findings** (2 matched `libfontconfig` suppressions) |
| `scripts/analysis/run-static-analysis.sh` | **OK** — cppcheck 18 findings all baselined; clang-tidy no enforced check fired; `bugprone-branch-clone` still 6, unchanged |
| `_NET_SUPPORTED` on a live WM | all five new atoms advertised |

### Mutation testing (12 deletions, each confirmed to redden)

| # | Guard deleted | Reddened |
|---|---|---|
| M1 | map-time arbitration entirely | 4 cases |
| M2 | the off switch (map-time) | off-switch case |
| M3 | the user-time-window proxy read | proxy case |
| M4 | clear-attention-on-activate | attention-clearing case |
| M5 | the urgency hint on refusal | 2 cases |
| M6 | the zero special case | **nothing** → exposed the gap, now closed |
| N1 | activation arbitration entirely | stale-activation case |
| N2 | pager/legacy exemptions | 3 cases |
| N3 | the off switch (activation) | activation off-switch case |
| N4 | source-enum validation | malformed case |
| N5 | the `format == 32` check | malformed case |
| N6 | demand-attention on activation refusal | stale-activation case |

## Known Stubs

None. No placeholder values, no unwired data paths, no skipped tests introduced.

## Threat Flags

None. The plan's threat register is fully addressed: T-8-FOCUS (both entry points arbitrated), T-8-PROP (bounded, type/format/count-checked, always-freed reads), T-8-MSG (format, target and source-enum validation before any field is trusted), T-8-WRAP (shared signed-delta comparison), T-8-OFF and T-8-SC accepted as planned. T-8-UAF, from the phase's existing register, was additionally *fixed* here.

## Notes for Future Phases

- **Plan 08-10** extends `updateNetWmState()` with skip-taskbar/skip-pager. It must preserve the `m_demandsAttention` entry in that list — the state property has exactly one writer and dropping a state there is silent. The two atoms it needs are already interned and advertised.
- **The revert-chain invariant** now has two maintainers (`activate()` and `eventDestroy`). Any future code path that removes a `Client` must call `skipInRevert()` before freeing, or the use-after-free returns.
- **Deferred item 9 remains the dominant test hazard.** Every non-event assertion added here uses the spaced `settleWm()` helper. The mutation battery is what proves they are not reading stale values.

## Self-Check: PASSED

All 8 claimed files exist on disk; all 8 claimed commit hashes resolve in `git log`.
</content>
</invoke>
