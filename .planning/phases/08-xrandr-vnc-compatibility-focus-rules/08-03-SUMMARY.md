---
phase: 08-xrandr-vnc-compatibility-focus-rules
plan: 03
subsystem: rendering
tags: [x11, shape, xext, border, capability-detection, fallback, catch2, ctest, cmake]

requires:
  - phase: 08-xrandr-vnc-compatibility-focus-rules
    plan: 01
    provides: WmFixture (with its childEnv override, used unchanged), the D-33 tags-as-labels convention, tests/lsan.supp, the preflight fixture
  - phase: 08-xrandr-vnc-compatibility-focus-rules
    plan: 02
    provides: scripts/gates/build-all.sh and scripts/analysis/run-static-analysis.sh, used as this plan's verification evidence
provides:
  - Border::combineShape() -- the single guarded funnel for every rectangle-combining Shape request in the codebase
  - WM2_FORCE_NO_SHAPE -- the hidden capability lever that forces the extension unavailable at startup
  - The capability sentinel convention, documented in include/Manager.h for every extension this phase adds
  - tests/test_wm_fallbacks.cpp + the test_wm_fallbacks CMake target -- the phase's home for degraded-capability coverage
  - A source-level [shape_invariant] guard that cannot be defeated by comment prose
affects: [08-05, 08-06, 08-14]

actuals:
  tokens: 9128
  tasks: 3
  commits: 3

tech-stack:
  added: [libXext linked into the test target only (XShapeQueryExtents)]
  patterns:
    - "Capability triple: a negative member sentinel, a has*Extension() predicate over it, and exactly one call funnel that no-ops when the predicate is false"
    - "Fallback proof by positive protocol assertion: ask the server whether the frame is shaped, rather than asserting nothing crashed"
    - "Every degraded-path suite carries a control case on the non-degraded path, so it discriminates rather than always passing"
    - "Source-level invariants asserted as ctest cases that skip comment lines, so documenting an invariant cannot break it"

key-files:
  created:
    - tests/test_wm_fallbacks.cpp
  modified:
    - include/Border.h
    - src/Border.cpp
    - include/Manager.h
    - src/Manager.cpp
    - CMakeLists.txt
    - .clang-tidy
    - .planning/phases/08-xrandr-vnc-compatibility-focus-rules/deferred-items.md

key-decisions:
  - "The rectangular fallback helpers (shapeParentRectangular/shapeTabRectangular) now emit nothing at all, because routing them through the funnel makes them no-op on exactly the servers that call them -- that is the XDIS-03 fix, not a regression: a window with no shape mask is already rectangular"
  - "combineShape() takes the rectangle array as pointer-to-const and casts at the Xlib boundary, so the funnel does not force every caller to hand over mutable data"
  - "The forced-fallback stderr line is worded differently from the genuine-absence line, so a transcript can tell 'we forced it' apart from 'the server lacks it'"
  - "The [shape_invariant] guard skips // comment lines, so it survives being explained in prose -- the exact failure 08-01 and 08-02 each hit with their own greppable invariants"
  - "libXext is linked into test_wm_fallbacks only, never the shipped binary; the link audit still reports 23 entries"

patterns-established:
  - "A degraded-capability test file carries one tag group per capability and is selected by label, never by target name"
  - "Negative-test source-level guards in both directions: a real violation must go red, and a comment mentioning the token must stay green"

requirements-completed: [XDIS-03, TEST-05]

coverage:
  - id: D1
    description: "Every Shape request passes through one wrapper that returns without touching the X connection when the extension is unavailable (D-11)"
    requirement: XDIS-03
    verification:
      - kind: other
        ref: "grep -c XShapeCombineRectangles src/Border.cpp -> 1; combineShape 27 in src/Border.cpp, 1 in include/Border.h; 0 raw calls in Manager/Client/Buttons/Events.cpp"
        status: pass
      - kind: integration
        ref: "tests/test_wm_fallbacks.cpp#src/Border.cpp names the Xlib rectangle-combining call exactly once"
        status: pass
    human_judgment: false
  - id: D2
    description: "WM2_FORCE_NO_SHAPE=1 makes the WM log a distinct forced-fallback line, manage windows normally, and report frames as unshaped (D-12)"
    requirement: XDIS-03
    verification:
      - kind: integration
        ref: "tests/test_wm_fallbacks.cpp -- 5 [wm_noshape] cases (manages+frames, unshaped per server, forced line present, control shaped, clean SIGTERM)"
        status: pass
      - kind: other
        ref: "xvfb-run WM2_FORCE_NO_SHAPE=1 timeout -s TERM 3 ./build/debug/wm2-born-again -> exit 124 + forced line; same run without the variable -> exit 124, no forced line"
        status: pass
    human_judgment: false
  - id: D3
    description: "With Shape present, frame and tab rendering is unchanged -- the sideways-tab visual identity is preserved"
    requirement: XDIS-03
    verification:
      - kind: other
        ref: "the 26-site diff is a pure callee rename + continuation realignment: no argument, ordering, or surrounding conditional altered (reviewed hunk by hunk)"
        status: pass
      - kind: integration
        ref: "full debug suite 149/149; the Shape-present control case asserts the server still reports the frame as shaped"
        status: pass
    human_judgment: true
    rationale: "Proven structurally (mechanical diff) and functionally (the frame is still shaped, all existing xft/client/wm_process tests pass), but NOT pixel-compared. The before/after screenshot evidence that would close this is 08-14's job. A human should confirm the structural argument is acceptable until then."
  - id: D4
    description: "The funnel cannot silently regrow a second unguarded call site"
    requirement: TEST-05
    verification:
      - kind: other
        ref: "negative-tested both ways: a second raw call turns [shape_invariant] red naming lines 174 and 183; a comment line naming the token leaves it green while grep -c reports 2"
        status: pass
    human_judgment: false
  - id: D5
    description: "A forced no-Shape run issues zero Shape protocol requests to the server"
    requirement: XDIS-03
    verification:
      - kind: other
        ref: "proven indirectly -- single-funnel invariant (D1) plus the guard being the funnel's first statement, plus the server-side unshaped assertion. NOT a protocol trace."
        status: partial
    human_judgment: true
    rationale: "The plan flagged this as a `backstop` truth and its own planner_assumptions section flagged the proxy. xtrace/x11trace is not installed on this host, so no protocol capture was taken. The claim rests on: every call site funnels (mechanically verified + test-guarded), and the funnel returns before the Xlib call. Carried forward for /gsd-verify-work."

duration: 39min
completed: 2026-08-11
status: complete
---

# Phase 08 Plan 03: Shape Funnel and the No-Shape Fallback Summary

**All 26 raw Shape requests now pass through one guarded `Border::combineShape()`, a hidden `WM2_FORCE_NO_SHAPE` lever flips the whole WM onto the rectangular path, and the server itself confirms the resulting frame is unshaped while a control case confirms it is shaped without the lever.**

## Performance

- **Duration:** 39 min
- **Started:** 2026-08-11T12:37Z
- **Completed:** 2026-08-11T13:16Z
- **Tasks:** 3
- **Files modified:** 8 (1 created, 7 modified)

## Accomplishments

- **Closed the real XDIS-03 hole.** Before this plan, `hasShapeExtension()` guarded exactly one helper while 26 raw calls fired unconditionally — including `shapeResize()`, which `configure()` invokes on every frame creation. On a genuinely Shape-less server the WM would have issued requests the server cannot answer. There is now exactly one place that can talk to the extension, and its first statement is the guard.
- **Made the invariant checkable rather than a review promise.** `grep -c XShapeCombineRectangles src/Border.cpp` is 1, and a ctest case enforces it — negative-tested in both directions.
- **The fallback is proven by a positive protocol assertion.** `XShapeQueryExtents` on the frame, asked of the server over a connection that *does* have Shape, reports `bounding_shaped == False` under the lever and `True` without it. The suite therefore discriminates between the two paths instead of asserting something true either way.
- **The forced path announces itself**, in wording distinct from the genuine-absence warning, so release evidence can tell "we forced it" apart from "this server lacks it". Asserted in the suite *and* re-checked as a standalone foreground gate.
- **Documented the capability convention once**, in `include/Manager.h`, so 08-05's RANDR work and 08-06's RENDER work copy a stated pattern rather than reverse-engineering this one.

## Task Commits

1. **Task 1: collapse 26 Shape call sites into the guarded funnel (D-11)** — `a965939` (refactor)
2. **Task 2: `WM2_FORCE_NO_SHAPE` lever + sentinel convention (D-12)** — `d1c9c45` (feat)
3. **Task 3: end-to-end fallback proof + single-funnel guard** — `d72b644` (test)

## Files Created/Modified

- `tests/test_wm_fallbacks.cpp` — five `[wm_noshape]` cases and one `[shape_invariant]` case; the phase's home for degraded-capability coverage, designed to accumulate tag groups from 08-05/08-06.
- `src/Border.cpp` — the funnel definition plus 26 mechanically rewritten call sites.
- `include/Border.h` — the `combineShape()` declaration.
- `src/Manager.cpp` — the env lever at the existing capability query.
- `include/Manager.h` — the capability sentinel convention, worded to cover the members later plans add.
- `CMakeLists.txt` — the `test_wm_fallbacks` target (libXext + `WM2_SOURCE_DIR`; no shared-display binding).
- `.clang-tidy`, `deferred-items.md` — corrected `bugprone-branch-clone` count (see Deviations).

## Decisions Made

See `key-decisions` in the frontmatter. The one worth reading twice:

**The rectangular fallback helpers now emit nothing.** `shapeParentRectangular()` and `shapeTabRectangular()` exist to run when Shape is *absent* — and routing them through the funnel means that on exactly those servers, they now return without sending anything. That reads like it broke them, and it is in fact the entire point: they were previously sending Shape requests to a server that has no Shape extension. A window with no shape mask is already rectangular, so the correct rectangular fallback is silence. The plan's own truth statement ("issues zero Shape protocol requests") demands precisely this. The two helpers are retained rather than deleted because deleting them is a separate decision and the plan forbade behaviour changes beyond the funnel.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] The mechanical rewrite converted the funnel's own body into infinite recursion**
- **Found during:** Task 1
- **Issue:** The scripted call-site rewrite matched 27 sites, not 26 — it also rewrote the `XShapeCombineRectangles(display(), dest, ...)` inside the newly written `combineShape()` definition, turning the funnel into an unconditional self-call. This would have been a stack-overflow crash on the first framed window.
- **Fix:** Restored the definition body to the real Xlib call. Caught immediately by the site count (27 vs the expected 26) rather than at runtime.
- **Verification:** `grep -c XShapeCombineRectangles src/Border.cpp` → 1; `grep -c combineShape src/Border.cpp` → 27; full suite green.
- **Committed in:** `a965939`

**2. [Rule 2 - Correctness of the record] `bugprone-branch-clone` was recorded as 5; the true pre-existing count is 6**
- **Found during:** Task 1 verification
- **Issue:** The static-analysis gate reported 6 where 08-02's `.clang-tidy` and `deferred-items.md` both recorded 5. Left uncorrected, the next executor to run the gate would read 6 as a regression introduced here.
- **Fix:** Re-measured **at 08-02's own commit** by stashing this plan's changes: 6 there as well. 08-02's count missed `src/Client.cpp:1415` ("switch has 2 consecutive identical branches"). Corrected both records with the evidence and a note explaining the re-measurement.
- **Verification:** `git stash` + `run-clang-tidy` on the unmodified tree → 6.
- **Committed in:** `a965939`

### Plan-text corrections (no code impact)

**3. `grep -c 'DISPLAY=:99' CMakeLists.txt` equals 4 — the literal is 5, before and after this plan**
The criterion intends "the four shared-display `ENVIRONMENT` bindings are unchanged", and that holds exactly: `grep -c 'ENVIRONMENT "DISPLAY=:99"'` is 4 at HEAD and 4 now, and the new target adds none. The fifth textual occurrence is inside 08-01's `start_xvfb` readiness-poll command (`DISPLAY=:99 xdpyinfo`), which post-dates the criterion's wording. Verified against `git show HEAD:CMakeLists.txt` — 5 there too.

**4. The gate reports 7 tests, not the 6 the criterion predicts**
6 Catch2 cases (5 `[wm_noshape]` + 1 `[shape_invariant]`) plus the `preflight` fixture setup test that ctest pulls in automatically. Same arithmetic as 08-01, whose `-L '^wm_process$'` gate reports 6 for 5 cases. All 7 pass.

---

**Total deviations:** 2 auto-fixed (1 latent crash, 1 record correction), 2 plan-text corrections. No scope creep; no architectural decisions required.

## Issues Encountered

- **clang-tidy's `cppcoreguidelines-init-variables` went 140 → 141.** Investigated rather than assumed: the extra diagnostic is `include/x11wrap.h:309` (pre-existing, reported-only), now counted once more because the new test file is a 23rd translation unit including that header. No uninitialised variable was added by this plan. The family remains reported-only per 08-02's documented backlog.
- **The `RenderBadPicture` X error from 08-01 is still present** in process-level runs and still non-fatal. Untouched here; it remains in `deferred-items.md`.

## Known Stubs

None. Every artifact this plan claims is implemented and exercised by a passing, negative-tested assertion.

## Threat Flags

None. No new network endpoint, auth path, file-access pattern or trust-boundary schema change.

The plan's own register is addressed:
- **T-8-SHAPE (mitigate)** — the guarded funnel is in place and enforced by `[shape_invariant]`; the forced-off end-to-end cases prove the degraded path works.
- **T-8-ENV (accept)** — the variable is read once at construction, never re-read, reads no file, grants no capability, and appears in no user-facing documentation or config key (`grep -c 'WM2_FORCE_NO_SHAPE' include/Config.h src/Config.cpp` → 0 each; `grep -c 'no-shape' src/Config.cpp` → 0).
- **T-8-VIS (mitigate)** — see coverage item D3; structural + functional evidence here, screenshot evidence deferred to 08-14 as the register itself specifies.
- **T-8-TMP** — the forced-run transcripts are written under `build/debug/`, not a bare fixed `/tmp` name.

## Verification Results

| Plan verification step | Result |
|---|---|
| `ctest --test-dir build/debug -L '^(wm_noshape\|shape_invariant)$' --no-tests=error` | PASS (7/7 — 6 cases + preflight) |
| `ctest --test-dir build/debug --no-tests=error` (full suite) | PASS (149/149) |
| `ctest --test-dir build/asan -L '^wm_noshape$' --no-tests=error` | PASS (6/6, **0** sanitizer report files) |
| `bash scripts/analysis/run-static-analysis.sh` | PASS (exit 0; cppcheck 18/18 accounted for, 0 fatal clang-tidy) |
| Task 1 gate: `ctest -L '^(wm_process\|xft\|client)$'` | PASS (25/25) |
| Task 2 gate: forced run exits 124 **and** logs the forced line | PASS |
| Task 2 discrimination: same run without the variable exits 124, **no** forced line | PASS |
| `cmake --build build/debug` with no new warnings | PASS (0 warnings) |
| Negative check: a second raw Shape call turns `[shape_invariant]` red | PASS (names lines 174, 183) |
| Negative check: a comment naming the token leaves it green | PASS (grep says 2, guard says 1) |
| `bash scripts/gates/build-all.sh release` (the tree not otherwise exercised) | PASS (link audit 23 entries, no test-only library) |

## Self-Check: PASSED

`tests/test_wm_fallbacks.cpp` exists on disk; all three commit hashes (`a965939`, `d1c9c45`, `d72b644`) resolve in `git log`; `src/Border.cpp` is byte-identical to its committed state after the negative-check experiments (`git diff --stat` empty).

## Next Phase Readiness

- **08-05 (RANDR) and 08-06 (RENDER) can copy a stated convention**, not reverse-engineer one: the capability triple is documented at `include/Manager.h` beside `m_shapeEvent`, and the env-lever shape is in `src/Manager.cpp` next to the query it guards.
- **`test_wm_fallbacks` is ready to accumulate tag groups.** The target exists with `WM2_SOURCE_DIR`, libXext, `ADD_TAGS_AS_LABELS` and its own display; 08-05/08-06 add cases and tags, not build plumbing. Note those two plans will need `-extension RANDR` / `-extension RENDER` via `WmFixtureOptions::xvfbArgs`, which the fixture already supports.
- **Carried planner assumption, unchanged:** "zero Shape requests" is proven by the single-funnel invariant plus an unshaped-frame assertion, not by a protocol trace — `xtrace` is not installed on this host. Flagged for `/gsd-verify-work` (coverage D5).
- **Unchanged from 08-02:** bare `ctest --test-dir build/asan` remains red on the pre-existing fontconfig cache leak in three xft tests (`deferred-items.md` item 5). Run the asan tree through the gate or with an anchored `-L`, as done here.

---
*Phase: 08-xrandr-vnc-compatibility-focus-rules*
*Completed: 2026-08-11*
