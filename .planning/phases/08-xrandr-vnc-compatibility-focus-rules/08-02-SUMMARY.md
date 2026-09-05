---
phase: 08-xrandr-vnc-compatibility-focus-rules
plan: 02
subsystem: testing
tags: [cppcheck, clang-tidy, static-analysis, asan, ubsan, lsan, cmake, ctest, ldd, fork, gates]

requires:
  - phase: 08-xrandr-vnc-compatibility-focus-rules
    plan: 01
    provides: the three build trees, scripts/preflight.sh and its shell conventions, tests/lsan.supp, and the D-33 --no-tests=error convention
provides:
  - scripts/gates/build-all.sh -- one command reproducing D-04 blockers 1 and 2 (Debug + Release + ASan/UBSan, full suite each), plus the checklist link audit
  - scripts/analysis/run-static-analysis.sh -- D-04 blocker 3, a real pass/fail static-analysis gate
  - A cppcheck baseline that survives line drift, anchored on content hashes computed by the gate
  - .clang-tidy -- explicit checks list plus a strictly smaller fatal list
  - _exit() in every forked spawn child, removing the sanitizer noise source in the WM itself
affects: [08-03, 08-04, 08-05, 08-06, 08-07, 08-08, 08-09, 08-10, 08-11, 08-12, 08-13, 08-14]

actuals:
  tokens: 11129
  tasks: 3
  commits: 3

tech-stack:
  added: [cppcheck 2.7 (dev tool), clang-tidy 14.0.0 (dev tool)]
  patterns:
    - "Static-analysis baselines anchor on a content hash over (id, file, message, source line) with the line number deliberately excluded"
    - "A gate that cannot distinguish informational sanitizer output from a finding classifies fail-closed: informational only if EVERY line matches the known benign shape"
    - "Every gate mechanism is negative-tested -- proven to go red -- before being trusted"
    - "Forked children terminate with _exit(), never std::exit()"

key-files:
  created:
    - scripts/gates/build-all.sh
    - scripts/analysis/run-static-analysis.sh
    - scripts/analysis/cppcheck-suppressions.xml
    - .clang-tidy
  modified:
    - src/Manager.cpp
    - .planning/phases/08-xrandr-vnc-compatibility-focus-rules/deferred-items.md

key-decisions:
  - "cppcheck 2.7's CLI never computes a per-finding hash, so the baseline is anchored on a hash the gate computes itself and cppcheck consumes a derived, hash-free suppression file generated at run time"
  - "Two cppcheck layers, both load-bearing: the native run catches new finding classes, the hash comparison catches a new finding of an already-baselined id in an already-baselined file"
  - "clang-tidy's fatal list contains only families already clean on this tree; the three the plan named that fire today are reported-only, because the same plan forbids editing production code into compliance"
  - "Sanitizer logs are classified rather than counted: LSan's suppression-accounting block is informational, anything else fails the gate"
  - "A stale baseline entry is a loud NOTE, not a failure -- fixing a pre-existing defect must not block the developer who fixed it"

requirements-completed: [TEST-06]

coverage:
  - id: D1
    description: "One command configures, builds and runs the full test suite in Debug, Release and ASan/UBSan, exiting non-zero naming the failing tree"
    requirement: TEST-06
    verification:
      - kind: other
        ref: "bash scripts/gates/build-all.sh -> exit 0, names debug release asan; per-tree runs exit 0; `nosuchtree` exits 2"
        status: pass
    human_judgment: false
  - id: D2
    description: "The suite gate cannot certify green against zero tests"
    requirement: TEST-06
    verification:
      - kind: other
        ref: "a scratch tree configured -DBUILD_TESTS=OFF exits 8 with --no-tests=error and 0 without it; the script has exactly one test-runner line and it carries the flag"
        status: pass
    human_judgment: false
  - id: D3
    description: "A sanitizer finding anywhere in the process tree fails the gate; benign suppression accounting does not"
    requirement: TEST-06
    verification:
      - kind: other
        ref: "the real classifier, evaluated against a planted heap-use-after-free report plus a genuine suppression block: finding -> exit 1, accounting -> exit 0"
        status: pass
    human_judgment: false
  - id: D4
    description: "The shipped binary links only intended runtime libraries; a test-only library is a failure"
    requirement: TEST-06
    verification:
      - kind: other
        ref: "link audit OK on build/release/wm2-born-again (23 entries); the same function run against build/release/test_wm_process names libXtst and exits 1"
        status: pass
    human_judgment: false
  - id: D5
    description: "Static analysis is a real pass/fail gate: exits 0 on the clean tree, non-zero on a new finding"
    requirement: TEST-06
    verification:
      - kind: other
        ref: "clean tree exit 0; an uninitialised local read in src/ trips both cppcheck layers and exits 1"
        status: pass
    human_judgment: false
  - id: D6
    description: "The baseline survives unrelated edits that shift line numbers"
    requirement: TEST-06
    verification:
      - kind: other
        ref: "8 blank lines prepended to src/Border.cpp (bool found; 956 -> 964): gate stays green, 18/18 accounted for. Repeated in situ by Task 3's own edit, which shifted every line below 844 in src/Manager.cpp"
        status: pass
    human_judgment: false
  - id: D7
    description: "A new finding of an already-baselined id in an already-baselined file still fails"
    requirement: TEST-06
    verification:
      - kind: other
        ref: "a second variableScope added to src/Border.cpp: native cppcheck run reports clean, hash layer reports 2 new findings, gate exits 1"
        status: pass
    human_judgment: false
  - id: D8
    description: "A missing tool or missing input fails with a named cause instead of silently skipping half the gate"
    requirement: TEST-06
    verification:
      - kind: other
        ref: "absent cppcheck / clang-tidy / compile_commands.json / baseline each produce a distinct named preflight error and exit 1"
        status: pass
    human_judgment: false
  - id: D9
    description: "Forked spawn children terminate without running atexit handlers or static destructors, and zombie reaping is unchanged"
    requirement: TEST-06
    verification:
      - kind: other
        ref: "root menu driven 6x against the real binary on a private Xvfb: 5 xclock processes launched, all reparented to init, 0 children left on the WM, 0 zombies on the host"
        status: pass
      - kind: other
        ref: "ctest --test-dir build/asan -L '^wm_process$' --no-tests=error -> 6/6, 0 sanitizer log files"
        status: pass
    human_judgment: true
    rationale: "The zombie check drove the real menu through XTEST/xdotool rather than a dedicated regression test, so it is evidence rather than a guard. A human should confirm that is acceptable until the spawn/zombie coverage item lands."

duration: 68min
completed: 2026-08-11
status: complete
---

# Phase 08 Plan 02: Build and Static-Analysis Gates Summary

**D-04's three hard blockers are now each one runnable local command, with a cppcheck baseline that is immune to line drift because cppcheck 2.7 cannot supply the hashes the plan assumed and the gate computes its own.**

## Performance

- **Duration:** 68 min
- **Started:** 2026-08-11T11:51:39Z
- **Completed:** 2026-08-11T12:59:00Z
- **Tasks:** 3
- **Files modified:** 6 (4 created, 2 modified)

## Accomplishments

- **`scripts/gates/build-all.sh`** — preflight, then configure/build/full-suite for Debug, Release and ASan/UBSan, plus the checklist's `ldd` link audit and per-tree `build.log` warning counts. Optional single-tree argument for iteration.
- **`scripts/analysis/run-static-analysis.sh`** — cppcheck and clang-tidy as one pass/fail gate, green today and red the moment a new finding appears.
- **A baseline that will not rot.** Anchored on a content hash over `(id, file, message, source line)` with the line number deliberately excluded. Proven twice: synthetically, and in situ by this plan's own `Manager.cpp` edit.
- **Removed the sanitizer noise source in the WM.** Seven forked-child terminations now use `_exit()`, so the ASan gate measures real findings only.
- **Every gate mechanism was negative-tested.** Nothing here is trusted because it printed OK once; each was proven to go red.

## Task Commits

1. **Task 1: build-all gate (Debug/Release/ASan)** — `e55128e` (feat)
2. **Task 2: static-analysis gate + baseline + .clang-tidy** — `825fe1c` (feat)
3. **Task 3: `_exit()` in forked spawn children** — `ce2be36` (fix)

## Files Created/Modified

- `scripts/gates/build-all.sh` — three-tree gate, sanitizer report collection and classification, link audit, warning ledger.
- `scripts/analysis/run-static-analysis.sh` — two-tool gate, two-layer cppcheck anchoring, `--regenerate-baseline` as an explicit operator action that never runs on the normal path.
- `scripts/analysis/cppcheck-suppressions.xml` — 18 hash-anchored entries, each with a human-readable anchor comment so the regeneration diff is reviewable, and a header recording the cppcheck version and generation date.
- `.clang-tidy` — explicit checks list from `-*`, strictly smaller fatal list, header scoping, and the promotion path for the three deferred families with their measured counts.
- `src/Manager.cpp` — `_exit()` at seven forked-child sites; `fatal()` and `errorHandler()` deliberately untouched.
- `deferred-items.md` — four new entries (see Deferred Issues).

## Decisions Made

See `key-decisions` in the frontmatter. The two that changed the plan's shape:

1. **cppcheck 2.7 cannot do what the plan asked, so the gate does it instead.** The plan mandated a hash-anchored cppcheck baseline and forbade a line-anchored one. cppcheck accepts a `<hash>` suppression element but its CLI never *computes* one — verified on this host: `<hash>0</hash>` matches every finding (because every finding's hash is 0) and any non-zero hash matches nothing at all. A hash written into a file handed straight to cppcheck would silently suppress nothing, and the gate would look green while enforcing nothing. So the baseline stores hashes the gate computes, and cppcheck is handed a *derived*, hash-free, line-number-free suppression file generated at run time. The plan's stated property — survives unrelated edits — is delivered; the named mechanism was not available.

2. **The hash layer is not redundant with the native run.** The derived suppressions match on `(id, fileName)`, which is coarse: a second `variableScope` in a file that already has one is absorbed. Proven — adding exactly that leaves the native cppcheck run reporting "clean" while the hash comparison reports 2 new findings and fails the gate. Both layers earn their place.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] cppcheck 2.7 supplies no per-finding hash**
- **Found during:** Task 2, before writing the baseline
- **Issue:** The plan's prescribed mechanism (`<hash>` entries consumed by cppcheck via `--suppress-xml`) does not work on the pinned toolchain. Writing it anyway would have produced a baseline that suppresses nothing.
- **Fix:** Two-layer design — the gate computes content hashes and derives a hash-free suppression file for cppcheck at run time. The plan's `--suppress-xml` and `--error-exitcode=1` requirements are still satisfied, and the anti-rot property is delivered and proven.
- **Committed in:** `825fe1c`

**2. [Rule 3 - Blocking] Three of the five named clang-tidy families already fire**
- **Found during:** Task 2, first clang-tidy run
- **Issue:** The plan named five families for the fatal list, required the gate to exit 0 on the unmodified tree, and forbade editing production code into compliance. Those three constraints are jointly unsatisfiable: `cppcoreguidelines-init-variables` (140), `bugprone-branch-clone` (5), `bugprone-use-after-move` (3) all fire today.
- **Fix:** Fatal list = the two families already clean (`bugprone-dangling-handle`, `clang-analyzer-core.*`). The other three are reported-only, with counts, cause and promotion path recorded in `.clang-tidy` and `deferred-items.md`. The uninitialised-read acceptance case is still caught, by cppcheck's `uninitvar`.
- **Committed in:** `825fe1c`

**3. [Rule 1 - Bug] The sanitizer report check failed the gate on benign output**
- **Found during:** Task 1, first asan run
- **Issue:** "any report file exists → fail" fired on LSan's *suppression-accounting* block, which is written on every clean run. The gate reported failure with all tests passing.
- **Fix:** Classify fail-closed — a file is informational only if every line matches the accounting block's shape; anything unrecognised is a finding. Verified against a planted heap-use-after-free. The accounting is now printed as useful signal (it is how a stale `lsan.supp` entry would surface).
- **Committed in:** `e55128e`

**4. [Rule 1 - Bug] Comment prose would have defeated two greppable invariants**
- **Found during:** Task 2, writing `.clang-tidy`
- **Issue:** The acceptance greps require `WarningsAsErrors` and `HeaderFilterRegex` to appear exactly once each; the explanatory comments named both, giving counts of 3 and 2.
- **Fix:** Reworded the comments to describe the keys by role, with an explicit note warning future editors. Same failure mode 08-01 hit and recorded.
- **Committed in:** `825fe1c`

**5. [Improvement] `link_audit()` parameterised so it can be negative-tested**
- **Found during:** Task 1
- **Issue:** A hardcoded binary path made the audit unprovable without breaking the build.
- **Fix:** Optional argument. Verified against `build/release/test_wm_process`, which legitimately links libXtst — the audit names it as test-only and exits 1.
- **Committed in:** `e55128e`

---

**Total deviations:** 5 (2 blocking, 2 bugs, 1 improvement). Both blocking deviations are the same shape — the plan named a mechanism the pinned toolchain does not provide — and both were resolved by delivering the stated *property* rather than the named mechanism, with the divergence documented in the artifact itself.

## Issues Encountered

- **Bare `ctest --test-dir build/asan` is red; the gate is green.** Three xft tests fail under bare ctest on a pre-existing 288-byte fontconfig cache leak — exactly what `tests/lsan.supp` exists to suppress, but 08-01 wired those suppressions only into the forked WM child, never into the Catch2 test binaries. `build-all.sh asan` supplies them and is green. **This is not a regression from this plan** and is unrelated to the `_exit()` change; it was simply never visible because 08-01 only ran the asan tree with `-L '^wm_process$'`. Full analysis in `deferred-items.md` item 5. Run the asan tree through the gate, not through bare ctest.
- **The plan's Task 3 verification bullet is satisfied:** that run leaves **zero** sanitizer report files behind.

## Known Stubs

None. Every artifact this plan claims is implemented and negative-tested.

## Deferred Issues

Recorded in `deferred-items.md`:

2. Three clang-tidy families reported but not fatal — including **5 `bugprone-branch-clone` findings in production code** (`src/Buttons.cpp` x4, `src/Client.cpp` x1), the only entries that may be real defects. Not investigated: this plan builds gates, it does not fix findings.
3. cppcheck 2.7 has no native finding hash — revisit on any cppcheck upgrade.
4. Pre-existing `-Wunused-result` on `write()` in `sigHandler` (Release only); the `(void)` cast does not suppress it in GCC.
5. Bare asan ctest redness (above).

## Threat Flags

None. No new network endpoint, auth path, file-access pattern or trust-boundary schema change. The plan's own register is addressed: T-8-GATE by the two-layer anchoring plus stale-entry reporting and an explicit, loudly-announced regeneration flag; T-8-FORK by `_exit()` with the double-fork structure re-verified; T-8-TMP by both sanitizer report sinks living under the CMake binary directory.

## Verification Results

| Plan verification step | Result |
|---|---|
| `bash scripts/gates/build-all.sh` exits 0 for all three trees | PASS (debug, release, asan; re-run after the Task 3 edit) |
| `bash scripts/analysis/run-static-analysis.sh` exits 0 on the clean tree | PASS (18 findings, all accounted for; 0 fatal clang-tidy) |
| ... and non-zero with a deliberately introduced defect | PASS (uninitialised local read: both cppcheck layers fire, exit 1) |
| `ldd build/release/wm2-born-again` names no test-only library | PASS (23 entries, 0 test-only; audit proven to fire on libXtst) |
| `ctest --test-dir build/asan --no-tests=error` leaves no sanitizer report files | PASS (0 files) |
| Line drift produces no false new finding | PASS (synthetic 8-line shift, and Task 3's real 15-line shift) |
| Every preflight input absent → distinct named error | PASS (4/4) |
| Zombie reaping unchanged after `_exit()` | PASS (6 spawns, 5 children reparented to init, 0 zombies) |

## Self-Check: PASSED

All 4 created files exist on disk; `src/Manager.cpp` and `deferred-items.md` modified as claimed; all 3 commit hashes (`e55128e`, `825fe1c`, `ce2be36`) resolve in `git log`.

## Next Phase Readiness

- **D-04 is now enforceable.** Plans 08-03 … 08-14 can be gated with two commands, and a new static-analysis finding introduced by any of them will block rather than accumulate.
- **The baseline will hold** through the heavy refactors this phase plans (`combineShape()` funnel, the 14-site `DisplayWidth`/`DisplayHeight` → `screenWidth()`/`screenHeight()` change): moving code does not move hashes. Changing a line *under* a baselined finding does, by design — regenerate and review the diff.
- **Carried planner assumption, unchanged:** "no new findings" is evaluated against the baseline as committed here, on cppcheck 2.7. An upgrade can change every hash at once; the recorded version plus `--regenerate-baseline` is the mitigation, not a guarantee. Flagged for `/gsd-verify-work`.
- **Before the asan tree is trusted under bare ctest**, `deferred-items.md` item 5 must be resolved (08-11 … 08-13).

---
*Phase: 08-xrandr-vnc-compatibility-focus-rules*
*Completed: 2026-08-11*
