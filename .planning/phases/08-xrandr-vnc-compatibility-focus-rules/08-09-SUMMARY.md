---
phase: 08-xrandr-vnc-compatibility-focus-rules
plan: 09
subsystem: configuration
tags: [config, rules, window-matching, parser, plain-data, display-free-tests]

requires:
  - phase: 05-configuration-system
    provides: "the key=value config parser, its line/value length limits, the boolean and clamp helpers, and the XDG system-then-user precedence chain"
  - phase: 07-root-menu-application-discovery
    provides: "the menu-entry-* repeated-key accumulator (the precedent D-20 names) and include/AppEntry.h, the plain-data-header convention"
  - phase: 08-xrandr-vnc-compatibility-focus-rules/08-08
    provides: "the Config parser shape most recently extended (focus-stealing-prevention), and the two skip-* atoms plan 08-10 will publish"
provides:
  - "include/Rules.h: the plain-data window-rule model, with no X11 dependency of any kind"
  - "ruleMatches(): AND semantics over class, name and type criteria, exact or substring comparison only"
  - "applyRules(): the later-wins-per-action fold plan 08-10 applies at map time"
  - "Eight rule-* config keys parsed as repeated ordered key groups, with per-file group state"
  - "tests/test_rules.cpp and the display-free test_rules target: 17 cases in 0.08s with no X server"
affects: [08-10 rule application at map time, any later work touching Config::applyKeyValue]

actuals:
  tokens: 13000
  tasks: 3
  commits: 5

tech-stack:
  added: []
  patterns:
    - "Tri-state action fields (Unset/Off/On) wherever a later-wins fold must distinguish 'not mentioned' from 'explicitly off'"
    - "Parser group state passed in by the caller, so a file boundary provably closes an open group"
    - "Enumerator names checked against Xlib's object-like macros before a plain-data header is pulled into X11-including translation units"

key-files:
  created:
    - include/Rules.h
    - src/Rules.cpp
    - tests/test_rules.cpp
  modified:
    - include/Config.h
    - src/Config.cpp
    - CMakeLists.txt
    - scripts/analysis/cppcheck-suppressions.xml

key-decisions:
  - "RuleTriState's enumerators are Off/On rather than False/True. Xlib #defines True and False as object-like macros, and Config.h now includes Rules.h, so the natural names failed to compile in every X11-including translation unit. Caught by the full build, not by the display-free test target."
  - "rule-match-class is tested against BOTH WM_CLASS fields; rule-match-name is tested against the instance name only. The asymmetry is deliberate -- the class criterion is the forgiving one -- and is negatively asserted so it cannot drift into 'both are dual'."
  - "Rule group state is two explicit fields (ruleOpen, ruleLastWasAction), not one. A single action-boundary flag cannot express 'no rule is open in THIS file', which is exactly the state a file boundary must restore."
  - "The type criterion is asserted inside the existing AND-semantics case rather than in a case of its own, keeping the planned 17-case count while closing a mutation gap."
  - "The transient cppcheck unusedFunction on applyRules is baselined rather than suppressed inline; it self-expires as a stale baseline entry when plan 08-10 wires the caller."

patterns-established:
  - "Guard-token hygiene: source-level acceptance greps are kept honest by keeping the tokens they search for out of comment prose, per the convention CMakeLists.txt already documents"
  - "Mutation testing extended to a parser state machine: every branch of the grouping rule deleted in turn and confirmed to redden a named case"

requirements-completed: []

coverage:
  - id: D1
    description: "A rule matches only when every criterion it sets matches (AND), and a rule with no criteria matches nothing"
    requirement: RULES-01
    verification:
      - kind: unit
        ref: "tests/test_rules.cpp#A class criterion and a name criterion are AND-ed"
        status: pass
      - kind: unit
        ref: "tests/test_rules.cpp#A rule with no criteria matches nothing"
        status: pass
    human_judgment: false
  - id: D2
    description: "Comparison is exact or substring only, with substring the default (D-21)"
    requirement: RULES-01
    verification:
      - kind: unit
        ref: "tests/test_rules.cpp#Exact mode requires whole-string equality, substring mode accepts a partial match"
        status: pass
      - kind: unit
        ref: "tests/test_rules.cpp#Substring is the default match mode"
        status: pass
    human_judgment: false
  - id: D3
    description: "The class criterion matches either WM_CLASS field, so a user writing either name gets a match"
    requirement: RULES-01
    verification:
      - kind: unit
        ref: "tests/test_rules.cpp#The class criterion is tested against both the instance and the class name"
        status: pass
    human_judgment: false
  - id: D4
    description: "Several matching rules fold later-wins per action, including a later rule setting an action back to its negative value (D-22)"
    requirement: RULES-01
    verification:
      - kind: unit
        ref: "tests/test_rules.cpp#Folding matching rules yields the last value set for each action"
        status: pass
      - kind: unit
        ref: "tests/test_rules.cpp#A later rule setting an action false overrides an earlier true"
        status: pass
    human_judgment: false
  - id: D5
    description: "Rules are repeated ordered key groups in the existing config file: match/mode lines attach to the open rule, and a match line after an action line opens the next one (D-20)"
    requirement: RULES-01
    verification:
      - kind: unit
        ref: "tests/test_rules.cpp#A class line, a name line and two action lines produce one rule"
        status: pass
      - kind: unit
        ref: "tests/test_rules.cpp#A match line following an action line opens a second rule"
        status: pass
      - kind: unit
        ref: "tests/test_rules.cpp#Consecutive match and mode lines attach to the same rule"
        status: pass
    human_judgment: false
  - id: D6
    description: "Group state does not cross the system-to-user file boundary, in either direction"
    requirement: RULES-01
    verification:
      - kind: unit
        ref: "tests/test_rules.cpp#Rules append across the system-then-user chain without merging groups"
        status: pass
    human_judgment: false
  - id: D7
    description: "Bad input warns by name instead of failing silently: orphan actions, unrecognised window types and match modes, malformed position and size values"
    requirement: RULES-01
    verification:
      - kind: unit
        ref: "tests/test_rules.cpp#An action with no preceding match line warns and produces no rule"
        status: pass
      - kind: unit
        ref: "tests/test_rules.cpp#An unrecognised window type warns and leaves the type criterion unset"
        status: pass
      - kind: unit
        ref: "tests/test_rules.cpp#A malformed position or size warns and leaves that action unset"
        status: pass
    human_judgment: false
  - id: D8
    description: "Every rule key is handled explicitly, so a rules-only config file emits no warnings at all"
    requirement: RULES-01
    verification:
      - kind: unit
        ref: "tests/test_rules.cpp#A config file of only rule lines produces no unknown-key warnings"
        status: pass
    human_judgment: false
  - id: D9
    description: "Rule values pass through the same value-length limit as every other config value (T-8-CFG)"
    requirement: RULES-01
    verification:
      - kind: unit
        ref: "tests/test_rules.cpp#A rule value longer than the value-length limit is rejected"
        status: pass
    human_judgment: false
  - id: D10
    description: "The rules model and matcher have no X11 dependency, so the suite runs without a display"
    requirement: RULES-01
    verification:
      - kind: e2e
        ref: "env -u DISPLAY ctest -L '^rules$' --no-tests=error -- 17/17 in 0.08s"
        status: pass
    human_judgment: false
  - id: D11
    description: "Whether the rule syntax is actually writable by a non-programmer, and whether the grouping rule is discoverable from the config file alone"
    verification: []
    human_judgment: true
    rationale: "The parser is proven correct against its own grammar, but whether a user who has never read the source can look at four rule lines and predict which rule an action attaches to is a judgment about the design, not a property any test settles. It becomes checkable once plan 08-10 makes rules observable on real windows."

duration: 60min
completed: 2026-08-29
status: complete
---

# Phase 08 Plan 09: Window Rules Model and Parser Summary

**Window matching rules are now expressible in the config file a user already has — same syntax, same limits, same warnings — with a matcher and a later-wins fold that are pure data and prove themselves in 0.08 seconds without an X server.**

## Performance

- **Duration:** ~60 min
- **Tasks:** 3 of 3
- **Files created:** 3; modified: 4
- **Commits:** 5

## Accomplishments

- **The model is genuinely X11-free**, and that is enforced rather than hoped for: `test_rules` links no X11 library and declares no display fixture, so the 17 cases run with `DISPLAY` unset. This is what makes the parser's edge cases — orphan actions, cross-file group boundaries, malformed geometry — cheap enough to cover at all.
- **Tri-state actions, not booleans.** The later-wins fold cannot distinguish "this rule did not mention no-decorate" from "this rule turned it off" if the field is a plain `bool`; every later rule would carry a default-false value and clobber whatever an earlier rule set. Test 8 is the case that fails under the boolean design.
- **The grouping state machine is two explicit fields, and the file boundary closes the group.** A rule left open at the end of the system config cannot absorb the first match key of the user config — the failure mode where a user's own rule silently narrows a rule they never wrote and cannot see.
- **Fifteen mutations, every one confirmed to redden a named case.** Five on the matcher, six on the parser, four more on details. One of them found a real hole (below).
- **Every source-level acceptance guard was made honest.** Three of them initially "failed" against my own comment prose rather than the code, which is the trap `CMakeLists.txt` already documents in the D-33 block. The comments were reworded so the greps check the implementation.

## Task Commits

1. **Task 1: model and matcher** — `791c783` (test/RED) → `e73e2ca` (feat/GREEN)
2. **Task 2: `rule-*` parsing** — `4320df9` (test/RED) → `0f21319` (feat/GREEN)
3. **Task 3: display-free test target** — `40314dc`

## The one mutation that found a hole

Deleting the type-criterion check from `ruleMatches()` left the **entire suite green**. The plan's eight matcher behaviours cover class, name, both modes, the dual test, the empty rule and the fold — and not one of them constructs a rule with a type criterion. The type vocabulary is exercised only by the *parser* section (which asserts `hasMatchType`, not that it is honoured), and by plan 08-10's process-level tests, which do not exist yet.

Closed by extending the AND-semantics case rather than adding a case of its own, so the plan's literal "17 tests passed" criterion still holds. The extension asserts three things: a type criterion AND-s like any other, a window of the wrong type is rejected, and a type criterion **alone** is a complete rule. The mutation now reddens.

This is the second consecutive plan in this phase where a mutation exposed a test that did not test its own name. The methodology is earning its cost.

## Decisions Made

- **`RuleTriState::Off/On`, not `False/True`.** Xlib `#define`s both `True` and `False` as object-like macros. `Config.h` now includes `Rules.h`, so the moment the header reached an X11-including translation unit the enumerators were textually replaced by `0` and `1` and the build broke — with an error pointing at `Rules.h`, a file containing no X11 include. The display-free test target compiled it fine, which is exactly why the failure only surfaced on the full build. Recorded in a comment at the enumeration so nobody "fixes" the names back.
- **`rule-match-class` is dual, `rule-match-name` is not.** The class criterion is tested against both `WM_CLASS` fields because users say "Firefox" meaning either; the name criterion tests the instance name only. The asymmetry is asserted *negatively* in Test 5, so a later change that makes both dual reddens rather than passing quietly.
- **An unrecognised `rule-match-type` warns and leaves the criterion unset**, which leaves the rule with no criteria — and a rule with no criteria matches nothing. That is the safe direction: a typo'd rule applies to no windows rather than to all of them. The alternative (refusing to open the group) would have desynchronised the state machine for every following line.
- **A malformed or orphan action is still a syntactic action boundary.** It contributes no value, but the next match key opens a fresh rule rather than inheriting whatever came before.
- **No CLI flags for rules.** Repeated ordered groups have no getopt expression — there is no way to say which `--rule-position` belongs to which `--rule-match-class` — and the existing enable/negate pairs exist for scalar booleans only. Recorded as a comment at the dispatch so the omission reads as a decision.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Test sequencing forced the CMake target earlier than Task 3**

- **Found during:** Task 1, at the first RED step
- **Issue:** Tasks 1 and 2 are `tdd="true"`, but the test file and its CMake target are both listed under Task 3. There is no way to run a failing test for Task 1 before the target that builds it exists.
- **Fix:** `tests/test_rules.cpp` and the `test_rules` target were created in Task 1's RED commit and extended in Task 2's RED commit; Task 3 became the finalisation and full-gate verification it was always going to be. No content was dropped — all 17 planned cases exist, and the WM source-list entry landed with Task 1's implementation so the shipped binary compiled `src/Rules.cpp` from the moment it existed.
- **Files modified:** `CMakeLists.txt`, `tests/test_rules.cpp`
- **Committed in:** `791c783`, `4320df9`, `40314dc`

**2. [Rule 3 - Blocking] `RuleTriState::True`/`False` collided with Xlib macros**

- **Found during:** Task 2, on the first full build after `Config.h` gained the rules include
- **Issue:** `include/Rules.h:66: error: expected identifier before numeric constant`. Xlib `#define`s `True` and `False`; every translation unit including X11 before `Config.h` had the enumerators replaced by literals. The display-free `test_rules` target built and passed 17/17 throughout — the collision is invisible from there.
- **Fix:** Renamed the enumerators to `Off`/`On` at every site, with the reason recorded at the enumeration.
- **Files modified:** `include/Rules.h`, `src/Rules.cpp`, `src/Config.cpp`, `tests/test_rules.cpp`
- **Verification:** Full debug/release/asan build clean, 0 warnings.
- **Committed in:** `0f21319`

**3. [Rule 2 - Missing critical coverage] The type criterion was not actually tested**

- **Found during:** Task 1 mutation testing (M5)
- **Issue:** Deleting `if (rule.hasMatchType) { if (facts.type != rule.matchType) return false; }` left the whole suite green.
- **Fix:** Extended the AND-semantics case with three type assertions, keeping the 17-case count.
- **Files modified:** `tests/test_rules.cpp`
- **Verification:** M5 re-run reddens one named case.
- **Committed in:** `e73e2ca`

**4. [Rule 3 - Blocking] Three acceptance guards matched their own documentation**

- **Found during:** Task 1 and Task 3 acceptance checks
- **Issue:** `grep -c 'regex\|fnmatch\|glob'`, `grep -c 'system(\|exec\|popen'` and `grep -c 'src/Rules.cpp' CMakeLists.txt` all matched comment prose I had written *explaining* the corresponding prohibition. A comment that trips the guard it describes defeats the guard — the convention `CMakeLists.txt` already states in its D-33 block.
- **Fix:** Reworded the three comments to carry the meaning without the literal tokens, and said so at each site. All guards now read the implementation.
- **Files modified:** `include/Rules.h`, `src/Rules.cpp`, `CMakeLists.txt`
- **Committed in:** `e73e2ca`, `40314dc`

### Acceptance-criteria counts that differ from the plan (no action needed)

| Criterion | Expected | Actual | Why |
|---|---|---|---|
| the eight rule keys in `src/Config.cpp` | 8 | 27 | `grep -c` counts *lines*, and the eight key names appear in the two classification predicates, in the per-key branches, in the warning strings and in the worked example in the block comment. The substantive claim — all eight handled explicitly — is asserted directly by the no-unknown-key-warnings case. |
| `rules` in `include/Config.h` | ≥1 | 2 | Satisfied. |
| `enum class` in `include/Rules.h` | ≥2 | 3 | Satisfied (match mode, window type, tri-state). |

## Verification Evidence

| Gate | Result |
|---|---|
| `ctest -L '^rules$'` | **17/17 passed** in 0.08s |
| `env -u DISPLAY ctest -L '^rules$'` | **17/17 passed** — the target genuinely needs no display |
| `ctest -L '^config$'` | **48/48 passed** |
| `ctest --test-dir build/debug` (full) | **218/218 passed** |
| `scripts/gates/build-all.sh` | **OK: debug release asan** |
| ASan | **no sanitizer findings** (2 matched `libfontconfig` suppressions) |
| `scripts/analysis/run-static-analysis.sh` | **OK** — cppcheck baselined, clang-tidy no enforced check fired, `bugprone-branch-clone` still 6 |
| Build warnings | **0** across all three trees |

### Mutation testing (11 deletions, each confirmed to redden)

| # | Guard deleted | Reddened |
|---|---|---|
| M1 | the no-criteria-matches-nothing guard | 1 case |
| M2 | the instance-name half of the dual class test | 1 case |
| M3 | the tri-state guard in the fold (plain-bool semantics) | 1 case |
| M4 | substring comparison (behaves as exact) | 2 cases |
| M5 | the type criterion | **nothing** → exposed the gap, now closed (reddens after the fix) |
| P1 | the per-file group-state reset | 1 case |
| P2 | the action boundary that opens a new rule | 2 cases |
| P3 | the orphan-action guard | 1 case |
| P4 | the unknown-window-type warning (accepted silently as Normal) | 1 case |
| P5 | the malformed-position guard (sets `hasPosition` anyway) | 1 case |
| P6 | explicit handling of a rule key (falls through to unknown-key) | 3 cases |

## Known Stubs

None. `applyRules()` has no production caller yet — plan 08-10 is its consumer — but it is fully implemented and fully tested, not a placeholder. The absence of a caller is recorded as a cppcheck `unusedFunction` baseline entry that will surface as a **stale** baseline entry once 08-10 wires it, which is a self-correcting reminder rather than a hidden suppression.

## Threat Flags

None. The plan's register is addressed: **T-8-CFG** — rule values pass through the unchanged 4096-char line and 256-char value limits before reaching any handler (asserted), and every integer parse uses the established two-catch warn-and-continue shape plus a clamp. **T-8-SHELL** — no rule action runs a command, a shell string or any external program; enforced by a source-level guard that now genuinely reads the implementation. **T-8-DOS** and **T-8-SC** accepted as planned; no dependency was added.

## Notes for Future Phases

- **Plan 08-10 is the consumer.** The interfaces it needs are `ruleMatches()`, `applyRules()`, `RuleWindowFacts` (instance name, class name, type) and `Config::rules`. Note the criterion asymmetry: `rule-match-class` is tested against **both** `WM_CLASS` fields, `rule-match-name` against the instance name only. 08-10 needs to supply both fields for either to work.
- **08-10 will make the cppcheck baseline stale.** Wiring the fold removes the `unusedFunction` finding, so the gate will report a stale baseline entry. Resolve with `--regenerate-baseline` and review the one-line diff; that is the designed behaviour, not a failure.
- **08-10's `grep -c 'DISPLAY=:99' CMakeLists.txt` equals 4` criterion will read 5.** It already read 5 at this plan's base commit: the fifth match is the readiness poll inside the `start_xvfb` command, not a test binding. The four `ENVIRONMENT "DISPLAY=:99"` bindings are intact and unchanged. Do not "fix" this.
- **Watch Xlib's macros when adding plain-data headers to `Config.h`.** `True`, `False`, `None`, `Status`, `Bool` and `Success` are all object-like macros or typedefs in the X11 headers. A plain-data header with no X11 include is not insulated from them once `Config.h` pulls it into an X11-including translation unit.
- **Deferred item 12 reproduced once.** The asan tree failed `A window that still fits after a shrink is not moved` on one run with **no sanitizer findings**, then passed three times in isolation and on a full gate re-run. This plan adds no runtime caller for any rules code, so the WM's behaviour is unchanged by it. Consistent with the known ~1-test-per-run process-level flake.

## Self-Check: PASSED

All 3 created files and 4 modified files exist on disk; all 5 claimed commit hashes resolve in `git log`.
