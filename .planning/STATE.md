---
gsd_state_version: 1.0
milestone: v1.0
current_phase: 08.5
current_phase_name: v1.0-closeout
status: executing
stopped_at: Completed 08.5-09-PLAN.md -- operator ruled REFUTED at the Task 3 blocking-human checkpoint; 08.5-07 disposition RE-PLAN. 08.5-07/-08/-05 NOT to be started; next action is re-planning the attribution
last_updated: "2026-08-31T13:09:09.766Z"
last_activity: 2026-08-31
last_activity_desc: 08.5-09 closed at Task 3 -- operator ruled REFUTED, disposition RE-PLAN
state_head: eb9eba5b2396b3e1d1f2f21bce475e471ef27356
progress:
  total_phases: 10
  completed_phases: 4
  total_plans: 46
  completed_plans: 42
milestone_name: milestone
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-06)

**Core value:** A lightweight, visually distinctive window manager that works well on resource-constrained VPS instances via remote desktop -- simple enough for non-programmers to configure, reliable enough for daily use.
**Current focus:** Phase 08.5 — v1.0 Closeout *(INSERTED)*

## Current Position

Phase: 08.5 (v1.0-closeout) — **08.5-10 PLANNED AND READY. EXECUTE 08.5-10 ONLY.**
  "Ready to execute" here means **08.5-10 and nothing else**. 08.5-07, 08.5-08 and
  08.5-05 remain HELD under the `RE-PLAN` disposition — see the DO NOT START block
  below. Prior headline, still the verdict of record: 08.5-09 COMPLETE, VERDICT
  REFUTED, CHAIN GOES RE-PLAN.
Plan: 9 plans; 6 have summaries (08.5-01, -02, -03, -04, -06, -09). 08.5-04's gate
  capture is HELD, not passed.
**DO NOT START 08.5-07, 08.5-08 OR 08.5-05.** The operator's disposition is
  `RE-PLAN`, not `START AS WRITTEN`. Those three plans stay written and stay held;
  executing any of them as written would record 08.5-07 as the flake fix, which
  this round's measurement refuted and which the precommitted terminal rule
  forbids. **The next action is RE-PLANNING the attribution around the actual
  failing path** — an add-only round, as 08.5-09 was.
Status: **08.5-09 COMPLETE (2026-08-31).** All three tasks executed; the operator
  ruled at the Task 3 `checkpoint:decision` (`gate="blocking-human"`).
  **Verdict of record: `REFUTED`. `08.5-07` disposition: `RE-PLAN`. No
  sub-decision** (a sub-decision is carried only by an `inconclusive` ruling).
  `evidence/gates/attribution/README.md` now carries exactly one `**Verdict:**`
  line reading `REFUTED` and exactly one `**08.5-07 disposition:** RE-PLAN` line,
  with one two-ended delimited region beneath saying which measurement the verdict
  reports and what it does not cover. Every measured figure, per-run table, log
  filename, counter reading and the prior round's ruling section in that file are
  untouched; the eight-run sampling measurement's own `INCONCLUSIVE` outcome
  stands unchanged.
  Commits: `8623618` (capture tool, fixture opt-in, calibration case, calibration
  bundle), `3b0fd37` (budgeted two-mode sampler), `9f016a4` (the measurement),
  `397310a` (the operator ruling).
  **REFUTED is a SUCCESSFUL outcome under the Negative-Result Contract in
  `08.5-VALIDATION.md`, not a failed plan** — a refutation is committed with the
  same weight as a confirmation, and an attribution is withheld when the
  measurement does not support one.
  **Lead for the next round, recorded and explicitly NOT attributed:** `trip-r-3`,
  `trip-r-4` and `trip-r-5` each carry a `BadWindow` cascade — `X_CreateWindow`,
  `X_ReparentWindow`, `X_MapWindow`, `X_ChangeSaveSet` all failing on window
  `0x80003b`, i.e. the WM framing a window that no longer exists. `trip-r-1` and
  `trip-r-2` have clean 12-line stderr and still failed to frame, so the cascade
  is in 3 of 5 and is NOT the universal signature. A refutation of one mechanism
  is not an attribution to another.
  **Corroborating reading ruled on at the checkpoint:** the stderr pair
  `timestamp: entering blocking property wait` / `property wait matched a foreign
  event` appears exactly ONCE in EVERY bundle **including the healthy control**,
  so the swallowing mechanism fires identically in the healthy case and does not
  discriminate a stalled WM from a healthy one.
  **The reproducer REPRODUCED.** Budget stated before the run and not extended:
  mode S 200 fixtures/1 map each, mode R 5 fixtures x 400 maps, 10 min and 20
  bundles per mode, 500 ms threshold inside the hard 8000 ms reparent deadline.
  Observed, per mode and never pooled: `trips-s=0` over 200 maps; `trips-r=5`
  over 2000 maps, all five written as bundles, all five clients also failing to
  frame inside 8000 ms. Neither mode exhausted its budget.
  **All five backtraces name `WindowManager::nextEvent` at `src/Events.cpp:205`**
  — the event loop's own `poll()`, `timeout=-1` — across three distinct WM
  processes. None names `WindowManager::timestamp`; none names `XMaskEvent`. By
  the criteria written BEFORE the run that is five REFUTED-contributing readings
  and no CONFIRMED-shaped one, exactly at the five-backtrace floor — met exactly,
  not with margin, across three independent WM processes (68887; 69033 twice;
  69518 twice), and the operator was shown that explicitly and ruled the floor
  satisfied. The proposed verdict REFUTED is now the **verdict of record**. The
  healthy-WM calibration bundle shows the SAME frame, which is what makes this a
  comparison rather than an argument from absence.
  Mutation table green in debug AND through
  `build-all.sh asan` (rows 2 and 3 redden the calibration case at `0 == 5` and
  `3 == 5` respectively, which is what shows the assertion counts channels).
  Final `build-all.sh asan` at the restored source: 316/316, no findings.
  Expected structural warning, recorded so it is not "corrected": `wave: 3` with
  `depends_on: []` is deliberate; `verify.plan-structure` flags it and a wave
  normalizer computing `max(deps)+1` would rewrite it to `wave: 1`. The ordering
  relative to 08.5-07 (`wave: 4`) holds under either value.
Previously: **08.5-09 PLANNED (2026-08-31).** The targeted-reproducer round, planned
  add-only per operator decision: existing 08.5-01..08 untouched. Committed
  `0c985a1` (plan + ROADMAP), revised `8f1e690` (per-mode readout gate and two
  gates strengthened to match their prose). gsd-plan-checker: **VERIFICATION
  PASSED** at revision iteration 2 (prior pass: 1 blocker, 2 warnings, all fixed).
  08.5-09 is wave 3, `gap_closure: true`, `autonomous: false`, TEST-08, 3 tasks:
  a calibration capture against a healthy WM, a budgeted two-mode sampler over the
  post-readiness reparent, and a `blocking-human` operator ruling that writes the
  verdict. Budget: 200 Mode-S fixtures + 2000 Mode-R maps, 10 min and 20 bundles
  per mode, hard stop. Zero trips inside budget is INCONCLUSIVE, recorded as such.
Prior round (08.5-06) stands unchanged: **Verdict INCONCLUSIVE.** 8 debug runs at
  `015e8c6`, 7 green / 1 red; the red run (case #80, `test_wm_fallbacks.cpp:272`)
  carried no WM stderr because its `INFO` guard sits below the failing assertion,
  so no counter reading exists for it. One ASan run, green, as one observation.
Operator ruling (2026-08-31, 08.5-06 Task 4 `checkpoint:decision`, `gate="blocking-human"`):
  **`inconclusive`, sub-decision `hold`** — the run budget was NOT extended, because
  the guard-ordering defect is widespread (14 of 59 frame assertions carry a stderr
  guard above them; a second-opinion audit put it at ~74 sites), so another red run
  would very likely also be unreadable. Recorded in
  `evidence/gates/attribution/README.md` § "Operator ruling — 2026-08-31".
**08.5-07's Task 1 precondition is permanently unsatisfiable as written** (found
  while planning 08.5-09, verified by the plan-checker against the tree). It is a
  conjunction: the attribution README's single `**Verdict:**` line must read
  `CONFIRMED` **and** `08.5-06-SUMMARY.md` must record the operator ruling
  `confirmed`. That summary is frozen at `inconclusive` and add-only forbids
  editing it, so no outcome of 08.5-09 can satisfy the second clause. 08.5-09
  Task 3 therefore also writes an explicit `**08.5-07 disposition:**` line
  (`RE-PLAN` | `START AS WRITTEN` | `STAYS HELD`) beside the verdict. **The
  disposition, not the verdict, governs whether the held chain moves.**
Remaining chain: the old chain 08.5-09 → 08.5-07 → 08.5-08 → 08.5-05 is **BROKEN
  BY THE RULING.** The disposition returned `RE-PLAN`, so 08.5-07/-08/-05 are NOT
  next and are NOT to be started. The chain is now: 08.5-09 (done) → **re-plan the
  attribution around the actual failing path** → then, and only then, a fix plan.
Out of scope for this round, named in 08.5-09 rather than dropped: the ~74-site
  `INFO("wm stderr")` guard-ordering fix (with `test_wm_geometry.cpp:1490` flagged
  as the already-correct counter-example a mechanical rewrite must not "fix"), the
  `tests/support/WmFixture.h:190` socket-recheck latent bug, and 08.5-04's held
  gate capture.
**PRECOMMITTED TERMINAL RULE (operator decision, 2026-08-31)** — set BEFORE 08.5-09
  runs, after an adversarial second opinion (Codex CLI `gpt-5.6-sol`, read-only
  against the tree) on whether this phase had lost its exit. Accepted finding:
  *"You have not demonstrated a repetitive technical loop. You have demonstrated a
  decision process without a terminal rule."* Recorded in 08.5-09-PLAN.md at the
  Task 3 checkpoint. It binds that checkpoint:
  - `confirmed`   → disposition `START AS WRITTEN`; execute 08.5-07, then its
                    regression and the criterion-7 gate capture.
  - `inconclusive`→ disposition `START AS WRITTEN`; execute 08.5-07 **anyway** —
                    both halves of its fix are independently justified defects.
                    **No fourth attribution round.** The extend/hold/re-plan
                    sub-decisions are WITHDRAWN; they are what let the last two
                    rounds terminate without ending.
  - `refuted`     → disposition `RE-PLAN`; 08.5-07 is NOT the flake fix and must
                    not be recorded as one. v1.0 stays blocked until the bundle
                    table names the actual failing path, or ruling B is reversed
                    as a new, recorded release-policy decision.
Corrected on the same consult, and load-bearing: **a post-fix rate drop is NOT
  attribution.** Baseline 2 red of 5; exact one-sided Fisher vs 0 red of 10 gives
  **p ≈ 0.095**, not the `0.006` that treating 0.40 as a known rate implies.
  Clearing p<0.05 needs 0 red of 16 (p ≈ 0.0476, ~64 min of runs); a 0.40→0.20
  improvement needs ~36 runs for 80% power. 08.5-07's plan text advertising
  `0.006` is arithmetically right and statistically overconfident. Its
  re-measurement is **operational validation**, with the causal claim qualified.
Known risks in 08.5-07, recorded and deliberately NOT folded in: (1) the
  `CurrentTime` fallback reaches `WM_TAKE_FOCUS` — `Client::sendMessage()`
  (`src/Client.cpp:779`) puts `timestamp(false)` in `data.l[1]`; (2) the deadline
  bounds waiting for readability, not the surrounding `XFlush()`/Xlib round trips;
  (3) a late `_WM2_RUNNING` `PropertyNotify` can be consumed by a later call as a
  stale cached timestamp, and repeated timeouts can leave several outstanding.
Also noted, unactioned: criterion 7's wording ("re-run at the final commit") does
  NOT itself require a one-shot-green suite — the 08.5-04 hold added that release
  policy. But TEST-06 requires full Debug/Release/ASan `ctest` for signoff, so a
  selected green run after known reds satisfies criterion 7 literally without
  establishing a reliable gate. Reversing ruling B is available, but only as a NEW
  recorded operator decision — never as though ruling B had been satisfied.
Resume: **the re-plan is DONE. 08.5-10 is planned, checked and committed
  (2026-08-31).** Add-only was preserved: since `1745cfb` only `ROADMAP.md` (+8/-1)
  and the new `08.5-10-PLAN.md` changed; `git diff --name-only` over `evidence/`,
  `08.5-0*-PLAN.md`, `*-SUMMARY.md`, `src/`, `include/`, `tests/` and
  `CMakeLists.txt` prints nothing. Commits: `8a16ace` (plan + scoped ROADMAP edit),
  `e638465` (checker warnings 1-4), `eb9eba5` (instrument-failure partition closed).
  gsd-plan-checker: **VERIFICATION PASSED** at iteration 3 (pass 1: 0 blockers /
  4 warnings; pass 2: 0 blockers / 1 warning; pass 3: clean).
  **The next action is `/gsd-execute-phase 8.5` — and it must run 08.5-10 ONLY.**
  **Do NOT let it start 08.5-07, 08.5-08 or 08.5-05.** Those three are NOT to be
  started under the `RE-PLAN` disposition; starting 08.5-07 would record it as the
  flake fix, which the measurement refuted and the precommitted terminal rule
  forbids. Reversing that is available only as a NEW, separately recorded operator
  decision.
**08.5-10 in one line:** a two-arm controlled experiment over the post-readiness
  reparent that either names the failing path or ends the attribution effort.
  Hypothesis under test (NOT yet a finding): `nextEvent()` gates on
  `QLength()` (`src/Events.cpp:195`) then blocks in `poll()` (`:205`); no
  `XPending`/`XEventsQueued` exists anywhere in `src/` or `include/`, and
  `libX11.so.6` links `libxcb.so.1`, so the transport holds a queue neither check
  counts. That account also predicts the `BadWindow` cascade on `0x80003b` and
  explains why the healthy control shows the same backtrace frame.
  Operator decision taken during planning (2026-08-31): **arm floor = four per
  arm**, lowered from six. Six was justified by an arithmetically false claim
  ("smallest table that can reach the threshold"); the true minimum is four
  (`1/C(8,4) = 0.0143` clears, `1/C(6,3) = 0.050` fails). Under a terminal rule a
  false negative is unrecoverable, and a floor of six would have routed a clearing
  5-vs-5 table (`p = 1/252 ~ 0.004`) to `NOT-ATTRIBUTED`. Cost recorded: at exactly
  four, only perfect separation clears, and family-wise across two eligible modes
  the bound is ~`0.029`.
  **Terminal rule (precommitted, in the plan before the round runs):** `ATTRIBUTED`
  -> `FIX-PLAN`; **everything else** -> a fixed three-item menu
  (`SHIP-WITH-RECORDED-DEFECT` | `REVERSE-RULING-B` | `DESCOPE-FROM-V1.0`).
  Extend/hold/re-plan are WITHDRAWN. **There is no outcome whose disposition is
  "run another attribution round."** More attribution spend is a new
  milestone-level decision, not a disposition of this checkpoint.
Known gate false-negative, recorded so it is not "fixed": `check.decision-coverage-plan`
  returns `passed:false, total:0` on this phase. It is a PARSER MISMATCH, not a
  coverage gap — this project writes decisions as `### D-8.5-01 - title` headings
  with a three-part ID, and the gate expects `- **D-NN:** text` bullets with a
  two-digit ID. Coverage verified manually at planning time: all 8 decisions
  (`D-8.5-01`..`-06`, `D-8-TIGHTVNC`, `D-8-X2GO`) are cited by at least one plan;
  `08.5-10` cites six of them.
Last activity: 2026-08-31 — 08.5-10 planned, verified PASSED, committed; add-only
  preserved; next action is executing 08.5-10 alone

Phase 8 closed at `66591ec`, verified with 2 declared gaps (5/7 success criteria).
Phase 8.5 exists to close them: RULES-01, XDIS-05, TEST-08.

Progress: [████████░░] 81% (7/9 phases complete)

## Performance Metrics

**Velocity:**

- Total plans completed: 22
- Average duration: 18min
- Total execution time: 0.6 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 01 | 2 | - | - |
| 02 | 2 | - | - |
| 03 | 3 | - | - |
| 04 | 3 | - | - |
| 05 | 3 | - | - |
| 06 | 3 | - | - |
| 07 | 6 | - | - |

**Recent Trend:**

- Last 5 plans: P01(14min), P02(22min)
- Trend: Steady
- Trend: -

*Updated after each plan completion*
| Phase 01 P01 | 14min | 3 tasks | 12 files |
| Phase 01 P02 | 22min | 3 tasks | 9 files |
| Phase 03 P01 | 15min | 2 tasks | 6 files |
| Phase 06 P01 | 15min | 1 tasks | 6 files |
| Phase 06 P02 | 7min | 1 tasks | 6 files |
| Phase 06 P03 | 5min | 1 tasks | 5 files |
| Phase 07 P01 | 15min | 3 tasks | 5 files |
| Phase 07 P02 | 20min | 2 tasks | 4 files |
| Phase 07 P04 | 25min | 2 tasks | 2 files |
| Phase 07 P03 | 20min | 3 tasks | 7 files |
| Phase 07 P05 | 20min | 2 tasks | 2 files |
| Phase 07 P06 | 25min | 3 tasks | 4 files |
**Per-Plan Metrics:**

| Plan | Duration | Tasks | Files |
|------|----------|-------|-------|
| Phase 08 P01 | 103min | 3 tasks | 7 files |
| Phase 08 P02 | 68min | 3 tasks | 6 files |
| Phase 08 P03 | 39min | 3 tasks | 8 files |
| Phase 08 P04 | 52min | 2 tasks | 7 files |
| Phase 08 P05 | 78min | 3 tasks | 7 files |
| Phase 08 P06 | 71min | 3 tasks | 9 files |
| Phase 08 P07 | 55min | 4 tasks | 11 files |
| Phase 08 P08 | 115min | 4 tasks | 11 files |
| Phase 08 P09 | 60min | 3 tasks | 7 files |
| Phase 08 P10 | 21min | 3 tasks | 6 files |
| Phase 08 P11 | 75min | 3 tasks | 6 files |
| Phase 08 P12 | 95min | 3 tasks | 9 files |
| Phase 08 P13 | 200min | 3 tasks | 9 files |
| Phase 08.5 P03 | 5min | 4 tasks | 1 files |
| Phase 08.5 P06 | 1h 0m | 3 tasks | 18 files |
| Phase 08.5 P09 | 1h 17m | 3 tasks | 51 files |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- [Phase ?]: test_raii linked against X11 because inline RAII destructors call XFree* functions
- [Phase ?]: catch_discover_tests PROPERTIES used for Xvfb fixtures instead of post-discovery set_tests_properties
- [Phase 01 P02]: Client::display() moved to public so Border can access it
- [Phase 01 P02]: installCursorOnWindow() added to Manager for Border to set cursor on resize handle
- [Phase 01 P02]: std::vector<XRectangle> replaces custom RectangleList macro in Border.cpp
- [Phase 01 P02]: Shape extension missing produces warning but not fatal exit
- [Phase ?]: ClientState enum values match X11 constants (Withdrawn=0, Normal=1, Iconic=3)
- [Phase ?]: State transitions validated but applied anyway with warning -- matches upstream permissive behavior
- [Phase ?]: Destructor-only cleanup replaces release() method; unreparent called in destructor for non-withdrawn clients
- [Phase 03 P03]: Tests verify infrastructure types (ServerGrab, ClientState) directly on Xvfb since full WindowManager cannot be unit-tested
- [Phase 03 P03]: Added X11/Xatom.h include for XA_WINDOW atom in colormap property test
- [Phase ?]: 07-01: xdgDataHome() kept internal (anonymous namespace), not exposed in DesktopEntry.h public API
- [Phase ?]: 07-01: field-code-only tokens (e.g. lone %u) are dropped from execArgv rather than kept as empty strings
- [Phase ?]: Local MmapGuard RAII struct in BinaryScanner.cpp's anonymous namespace instead of reusing Manager.h's FdGuard, to keep BinaryScanner X11-free
- [Phase ?]: Binary-scan-discovered apps default to category=Other (distinct from D-07's Custom for manual entries)
- [Phase ?]: 07-04: m_apps/m_submenuWindow initializer-list position follows header declaration order (not the plan text's literal wording) to avoid -Wreorder
- [Phase ?]: 07-04: Task 1/Task 2 commits split via temporary revert-reapply since both touch the same two files
- [Phase ?]: 07-03: write() creates parent config directory if missing (nothing else in the project creates ~/.config/wm2-born-again/)
- [Phase ?]: 07-03: mergeEntries() does not re-apply D-07 Custom default -- applied once at Config::applyKeyValue's menu-entry-name= handler
- [Phase 07]: openCategorySubmenu() signature extended with outerX/outerY/outerMaxWidth/rowIndex params beyond the plan's literal 2-param declaration, since the outer menu's position/hover-row-index are menu()'s local variables not otherwise recoverable
- [Phase ?]: 07-06: wm2-born-again CMake target was missing AppCache.cpp/DesktopEntry.cpp/BinaryScanner.cpp source entries -- fixed in Task 1
- [Phase ?]: 07-06: D-02 wording fix scoped strictly to PROJECT.md/ROADMAP.md's 5 flagged AI-powered occurrences -- AI showcase framing preserved
- [Phase 8]: COMPILED_CODE_BEHAVIOR_CHECKLIST.md is routed into Phase 8 as the first verification/hardening gate rather than a standalone milestone, because its findings overlap XDIS, FOCUS, remote-display compatibility, and runtime proof requirements
- [Phase 08]: 08-01: eventDestroy fix cited as SCAN-02 not D-07 -- D-07 already denotes the child-process harness (08-CONTEXT) and Phase 7's Custom-category default
- [Phase 08]: 08-01: dock-ness read via a 'const Client& dying' alias so the c->isDock() grep guard is a true regression detector
- [Phase 08]: 08-01: fixture Xvfb needs -noreset plus a retained keepalive connection; the readiness probe's disconnect was resetting the server under the WM (~9% flake)
- [Phase 08]: 08-01: XTEST tab click uses press -> 200ms -> release, not click(), to avoid racing the WM's move-loop pointer grab
- [Phase 08]: 08-01: D-33 tag-selection convention established -- Catch2 tags as ctest labels, anchored -L, --no-tests=error on every gate; CMake floor raised to 3.20
- [Phase ?]: cppcheck 2.7's CLI never computes a per-finding hash, so the static-analysis baseline is anchored on hashes the gate computes itself and cppcheck consumes a derived hash-free suppression file generated at run time
- [Phase ?]: clang-tidy's fatal list contains only families already clean on this tree (bugprone-dangling-handle, clang-analyzer-core.*); the three families that fire today are reported-only with a documented promotion path
- [Phase ?]: 08-03: routing the rectangular fallback helpers through combineShape() makes them emit nothing on Shape-less servers -- that silence IS the correct fallback, since an unshaped window is already rectangular
- [Phase ?]: 08-03: capability triple documented in include/Manager.h (negative member sentinel + has*Extension() predicate + one no-op-when-absent funnel); 08-05/08-06 copy it
- [Phase ?]: 08-03: bugprone-branch-clone true pre-existing count is 6, not the 5 recorded by 08-02 (missed src/Client.cpp:1415); re-measured at 08-02's own commit
- [Phase ?]: D-27 geometry funnel: screenWidth()/screenHeight() over a manager-owned cache seeded in initialiseScreen(); all 16 direct DisplayWidth/DisplayHeight reads routed through it
- [Phase ?]: Geometry tests pin exact values and are each negative-tested; fullscreen POSITION is deliberately not pinned because the value the WM produces today is a defect (deferred item 8)
- [Phase ?]: 08-05: the resolution-change handler re-reads root geometry with XGetWindowAttributes rather than re-reading screenWidth()/screenHeight() -- since 08-04 those return the manager's own cache, so re-reading them after a resize returns the value being replaced
- [Phase ?]: 08-05: XDIS-02's reflow claim is proven with WM2_FORCE_NO_RANDR on a RANDR-capable server, because on a genuinely RANDR-less server no resolution change can be produced at all -- the only X resize mechanism is the missing extension
- [Phase ?]: Open Question 1 answered: a rotated FcMatrix Xft font loads, measures identically and draws without protocol error on a RENDER-less server -- libXft's core X11 glyph path keeps the sideways tab, so XDIS-05 targets without XRender lose nothing
- [Phase ?]: Border's tab-font load is a four-rung ladder (rotated preferred, rotated generic, unrotated, none) that cannot terminate the process; rung 1 is silent and every other rung prints one warning
- [Phase ?]: The XRender capability probe carries no sentinel and drives no behaviour -- it exists for the XDIS-05 evidence transcript and as the discriminator for the RENDER-less tests
- [Phase ?]: Deferred item 10's XMaskEvent hypothesis is refuted; the rare startup wedge is deferred item 9 (unflushed WM output), cured by retrying the readiness probe with a fresh window
- [Phase ?]: XDIS-04's requirement text amended (D-14) to state fontconfig-level degradation and record why core X font revival is excluded
- [Phase ?]: 08-07: FOCUS-02 was unimplemented, not merely unproven -- the three focus booleans had zero runtime consumers; each now reaches a real branch (D-15)
- [Phase ?]: 08-07: the auto-raise gate sits where the deadline is ARMED, not in the expiry branches, so computePollTimeout() reports no deadline and the loop blocks indefinitely
- [Phase ?]: 08-07: D-17 resolved in favour of behaviour -- raiseOnFocus and autoRaise now default TRUE, which is what users already got; the all-false defaults never described the binary
- [Phase ?]: 08-07: the WM startup banner now reports the running focus policy instead of claiming 'Focus follows pointer' unconditionally -- that transcript is the XDIS-05 evidence artefact
- [Phase ?]: 08-07: deferred item 6 (circulate() 100%-CPU spin) fixed with a bounded scan; the regression test creates its own transient client so it cannot go vacuous when item 7 is fixed
- [Phase ?]: 08-07: a settle before a non-event assertion must be REPEATED AND SPACED -- 15 pumps back to back left a deleted production gate green; the same 15 at 20ms intervals redden it
- [Phase ?]: 08-07: gating auto-raise at the expiry site was implemented and measured -- it does NOT spin (0 CPU ticks), so the idle-CPU case is not evidence for the gate's placement
- [Phase 08]: 08-08: _NET_ACTIVE_WINDOW arbitrated by EWMH source indication (pager granted, application arbitrated, legacy granted) — supersedes Phase 6 D-10, amended append-only in 06-CONTEXT.md
- [Phase 08]: 08-08: map-time focus granting is NOT gated on the pointer-entry policy; the resulting conflict with 08-07 behaviours 1-2 was resolved by constructing their unfocused precondition with a zero _NET_WM_USER_TIME, leaving every assertion intact
- [Phase 08]: 08-08: _NET_WM_USER_TIME of zero means do-not-focus-me at map time, but absent evidence (arbitrated as stale) on an activation request
- [Phase 08]: RuleTriState uses Off/On, not False/True: Xlib #defines both macros and Config.h now reaches every X11-including translation unit
- [Phase 08]: rule-match-class is tested against both WM_CLASS fields; rule-match-name against the instance name only (asymmetry asserted negatively)
- [Phase 08]: Rule group state is two explicit fields (ruleOpen + ruleLastWasAction), reset per file so a system-config rule cannot absorb a user-config match key
- [Phase 08]: No CLI flags for rules: repeated ordered key groups have no getopt expression
- [Phase 08]: RULES-02: m_ruleNoDecorate kept distinct from isDock() -- both reach the unframed path but only a dock may recompute the workarea
- [Phase 08]: RULES-02: rule geometry is overlaid on the client's request between the size-hint floors and the clamps, never applied after mapping as a second geometry authority
- [Phase 08]: D-23: RULES-02 amended in the requirement text -- three shipping actions, workspace action excluded because the WM is single-desktop by design (Phase 6, D-11)
- [Phase 08]: Plan 08-11: the focus-candidate destroy case stretches the auto-raise delay rather than shortening it, so the 'tracking still pending' precondition is observable from outside the WM instead of assumed
- [Phase 08]: Plan 08-11: the WM's X protocol error log is now a test observable; errorHandler() logs and returns 0, so teardown defects are invisible to any assertion about windows or properties
- [Phase 08]: Plan 08-11: hidden-list transfers are proven through the published _NET_CLIENT_LIST ORDER, explicitly as a proxy for the vector move and not as an EWMH ordering claim
- [Phase 08]: Plan 08-11: fixResizeDimensions() validates each resize increment per axis before its own division; a non-positive increment means 'do not quantise this axis', never a divide by zero
- [Phase 08]: Deferred item 8's recorded cause was corrected: Border::unmap()'s frame/tab/button unmaps each reach Client::eventUnmap() via hasWindow(), and the tab's is what withdrew the fullscreen client -- the reparent guard alone is not sufficient
- [Phase 08]: eventClient's _NET_WM_STATE branch guards on !isWithdrawn(), not !isNormal(): an Iconic client is genuinely managed, Withdrawn is the ICCCM's own word for unmanaged
- [Phase 08]: Maximize insets the workarea by the frame decoration so the FRAME fills it; the client sits at (xIndent, yIndent), the one convention every geometry path in the WM now follows
- [Phase 08]: setMaximized falls back to the screen when the workarea is not a usable rectangle inside it -- a range check, deliberately not a size threshold, which would be a policy about how much screen a panel may claim
- [Phase 08]: clampStrut recorded as an equivalent mutant: Xlib sign-extends format-32 property data into long, so the narrowing conversion is exact and no external observation can distinguish it
- [Phase 08]: Memory-safety mutations must be run in the ASan tree -- two of fifteen are green in debug and kill the WM under the sanitizer
- [Phase 08]: All thirteen of the checklist's missing-coverage items are now automated; 08-13 closed items 5, 6 and 13
- [Phase 08]: A YXSorted SHAPE request is a promise the server validates: Border::combineShapeSorted() is now the only way to issue one, because every rectangle list depends on FRAME_WIDTH and the tab width
- [Phase 08]: XSync(dpy, True) discards EVENTS, not errors: Client::unreparent() was throwing away the WM's whole queued backlog on every client teardown
- [Phase 08]: Xlib out-parameters are not written on failure -- getColormaps() was installing uninitialised colormap XIDs
- [Phase 08]: kOptionSpecs is the single declaration of the CLI surface: getopt_long()'s array and the --help usage text are both generated from it, and --no- negations are derived rather than listed
- [Phase 08]: Deferred item 5 (LSan suppressions not wired into Catch2 binaries) is deliberately still open, with both candidate fixes costed and an owner recommended
- [Phase 08.5]: D-8.5-04 reverses the handoff's X2Go descope on measurement -- `x2goserver`/`nxagent` were already installed (dpkg 2026-08-29 17:43), nxagent runs headless nested on an Xvfb advertising SHAPE/RANDR/RENDER, and the WM frames clients on it. Validation costs ~2 minutes, not a human session, so XDIS-05 is met as written rather than amended
- [Phase 08.5]: D-8.5-02 the WM reads only legacy WM_NAME and never `_NET_WM_NAME` for a client title (`Atoms::net_wmName` is used solely to name the WM's own check window). A title rule on that alone would be dead for modern clients, so the title read becomes EWMH-first with ICCCM fallback
- [Phase 08.5]: D-8.5-01 `rule-match-name` renamed to `rule-match-instance` with NO deprecated alias -- free before v1.0, breaking after; the cost is one `unknown config key` warning for an existing local config
- [Phase 08.5]: D-8.5-03 title rules fold once at map time and are deliberately NOT re-folded on title change -- re-applying geometry on rename would make windows jump when a document is renamed; re-fold goes to the v1.1 backlog
- [Phase 08.5]: 08.5-03: the shipped release notes and the signoff checklist are worded differently on purpose -- COMPILED_CODE_BEHAVIOR_CHECKLIST.md keeps the retired D-8-TIGHTVNC rationale verbatim as a record, the user-facing notes name it only as a retired argument and state the general lesson instead
- [Phase 08.5]: 08.5-03: the literals x2gostartagent and SCOPE.md were deliberately withheld from Task 2's table cells and introduced in Task 3, so Task 3's positive guards could still redden -- a guard pre-greened by an earlier task in the same plan measures nothing, which is the exact defect this plan closes
- [Phase 08.5]: 08.5-04: gate capture held rather than recorded on a suite that is not reliably green in one shot -- a release signoff whose gate needs retries is weaker evidence than no signoff, and the measurement (2/5 red, different case each time) is a rate rather than an attribution
- [Phase 08.5]: 08.5-06 Task 4 operator ruling (2026-08-31): `inconclusive`, sub-decision HOLD -- the run budget was NOT extended, because the guard-ordering defect is widespread (14 of 59 frame assertions carry a stderr guard above them; ~74 sites by a second-opinion audit) so another red run would very likely also be unreadable. Separate NEW operator decision, recorded as context and not acted on: re-plan the attribution around a targeted reproducer (isolated post-readiness reparent loop, ~500ms diagnostic threshold, /proc/<pid>/wchan plus gdb backtrace -- XMaskEvent CONFIRMS, poll/Xlib/teardown/dead-WM REFUTES) instead of more full-suite sampling. The ruling does NOT unblock 08.5-07/08/05.
- [Phase 08.5]: 08.5-06 Task 4: the causal chain is real and verified, which is what keeps the hypothesis alive rather than refuted -- the fixture readiness probe returns as soon as its own window is reparented (tests/support/WmFixture.h:666), but the WM then proceeds m_border->reparent() (src/Client.cpp:251) -> activate() (src/Client.cpp:275) -> timestamp() (src/Client.cpp:315) where it can wedge, so construction can return "ready" while the WM is about to block and the NEXT window's MapRequest is never processed
- [Phase 08.5]: 08.5-06 Task 4: foreign=5 of cold=5 claims LESS than 08.5-06 first recorded -- "foreign" also covers the WM's own root _NET_CLIENT_LIST/_NET_ACTIVE_WINDOW traffic, which eventProperty() (src/Events.cpp:535) would ignore anyway. The swallowing defect is real as a mechanism; that counter does not measure its impact
- [Phase 08.5]: 08.5-06: attribution verdict INCONCLUSIVE -- 8 debug runs at 015e8c6 produced 1 red (case #80, test_wm_fallbacks.cpp:272, 8000ms reparent poll), but that case INFO guard sits BELOW the failing assertion so no WM stderr reached the log; neither CONFIRMED nor REFUTED condition observed. 08.5-07 Task 1 precondition UNMET. — The measurement did its job and withheld an attribution the evidence does not support (Negative-Result Contract). Probe counters read cold=5 blocked=2 foreign=5 longestms=0: the branch is live and blocks but costs 0ms under that workload, and 5 of 5 waits matched a FOREIGN property event -- a separately recordable defect (eventProperty never sees those). Case #80 is the same 8000ms reparent-poll shape as the before-half case #93, in a second independent helper.
- [Phase 08.5]: 08.5-09 Task 2 measurement: the targeted reproducer REPRODUCED. Mode R (2000 post-readiness maps across 5 fixtures) crossed the 500 ms diagnostic threshold 5 times, all 5 clients also failed to frame inside the hard 8000 ms deadline, and all 5 bundles are readable in five channels. Mode S (200 maps) tripped 0 times. Neither mode exhausted its 10-minute budget and the budget was NOT extended. — Evidence: evidence/gates/reproducer/ at 3b0fd37 (measurement), committed 9f016a4. Per-mode counters, never pooled: trips-s=0 bundles-s=0 capped-s=0 maps-s=200; trips-r=5 bundles-r=5 capped-r=0 maps-r=2000 unframed-r=5.
- [Phase 08.5]: 08.5-09 Task 2: all five trip backtraces name WindowManager::nextEvent at src/Events.cpp:205 -- the event loop's own poll(), timeout=-1 -- across three distinct WM processes. None names WindowManager::timestamp and none names XMaskEvent. By the criteria written BEFORE the run that is five REFUTED-contributing readings and no CONFIRMED-shaped one, exactly at the five-backtrace floor. PROPOSED verdict REFUTED -- proposed only; the verdict of record is the operator's at Task 3 and has NOT been taken. — The calibration capture against a healthy WM (evidence/gates/reproducer/control/) shows the SAME frame, which is what makes the trip reading a comparison rather than an argument from absence: an idle WM and a WM that has not reparented for 500 ms are, at this level, both blocked in the event loop's poll with no deadline armed. A refutation of this mechanism is not an attribution to another one.
- [Phase 08.5]: 08.5-09 Task 3 operator ruling (2026-08-31): attribution verdict REFUTED, and the 08.5-07 disposition is RE-PLAN. Five hang-time backtraces from three distinct WM processes all name WindowManager::nextEvent at src/Events.cpp:205 inside poll(timeout=-1); none names WindowManager::timestamp or XMaskEvent, and the healthy-WM calibration capture has the SAME shape. REFUTED is a successful outcome under the Negative-Result Contract, not a failed plan. 08.5-07/-08/-05 are NOT to be started; v1.0 stays blocked until the bundle table names the actual failing path or ruling B is reversed as a NEW recorded release-policy decision.
- [Phase 08.5]: 08.5-09 Task 3: the DISPOSITION, not the verdict, governs the held chain. 08.5-07's Task 1 precondition is a conjunction whose second clause requires 08.5-06-SUMMARY.md to record confirmed; that summary is frozen at inconclusive and add-only forbids editing it, so the precondition is unsatisfiable by ANY measurement including a confirming one. Amending the single Verdict line therefore neither starts nor unblocks 08.5-07.
- [Phase 08.5]: 08.5-09: the BadWindow reparent cascade (X_CreateWindow/X_ReparentWindow/X_MapWindow/X_ChangeSaveSet on window 0x80003b) in trip-r-3/-4/-5 is recorded as a LEAD and explicitly NOT as an attribution -- it is present in 3 of 5 bundles, and trip-r-1/-2 have clean stderr and still failed to frame, so it is not the universal signature. A refutation of one mechanism is not an attribution to another.

### Pending Todos

- Phase 8 08-01: Turn COMPILED_CODE_BEHAVIOR_CHECKLIST.md into executable process-level Xvfb/Xephyr tests, sanitizer/static-analysis gates, and release evidence capture.
- Phase 8 08-01: Resolve or explicitly accept current scan findings: missing xft/fontconfig build preflight on this host, eventDestroy client lifetime hazard, no-Shape fallback proof, and focus policy config wiring proof.

### Blockers/Concerns

- Phase 4 (Border/Xft): Xft rendering inside shaped windows is poorly documented; build a PoC first
- Phase 7 (App Scanner): Binary scan heuristics for identifying GUI apps are novel
- Phase 8 (Behavior Verification): Current host cannot configure until pkg-config sees xft/fontconfig; do not claim compiled behavior coverage until dependency preflight passes
- Phase 8 (Behavior Verification): Static scan found eventDestroy use-after-free risk, incomplete no-Shape fallback proof, and parsed focus config that needs runtime behavior tests
- Phase 9 (Config GUI): GTK3 performance over SSH X forwarding is unvalidated
- Bare 'ctest --test-dir build/asan' fails 3 xft tests on a pre-existing fontconfig cache leak: tests/lsan.supp is wired only into the forked WM child, not the Catch2 test binaries. Run the asan tree via scripts/gates/build-all.sh until fixed (deferred-items.md item 5).
- ~~PRE-EXISTING: WindowManager::circulate() spins at 100% CPU forever when no client is Normal~~ RESOLVED in 08-07 (`62e9c0d`): bounded scan plus a [wm_circulate] regression test asserting both responsiveness and idleness. deferred-items.md item 6.
- 08-05 deferred item 10: rare WM startup hang between the EWMH publication and the event loop, suspected timestamp()'s unbounded XMaskEvent. **The suspicion is REFUTED for the post-readiness reparent shape as of 08.5-09 (2026-08-31)**: five hang-time backtraces from three WM processes all name WindowManager::nextEvent at src/Events.cpp:205 inside poll(timeout=-1); none names WindowManager::timestamp or XMaskEvent, and the healthy-WM calibration has the same shape. timestamp()'s unbounded wait remains a real defect on its own merits; it is no longer the attributed cause.
- Deferred item 11: the sideways tab does not grow with the window title (axis-swapped rotated extents) -- measured in 08-06, out of scope there, recommended for 08-14
- Deferred item 12: the full process-level suite flakes at ~1 test per run on both this tree and the pre-08-06 baseline -- build-all.sh is not reliably green in one shot
- Deferred item 13 (08-11): every client destroy logs one X_UnmapWindow BadWindow because the resize handle is a child of the client window; blocks a strict no-protocol-errors assertion
- 08.5-04 GATE CAPTURE HELD (operator ruling B, 2026-08-30): the full debug suite is red in 2 of 5 runs at 313 tests, a DIFFERENT single case each time -- #198 exec-using-shell spawn-await (1/5 loaded, 0/12 isolated) and #93 interactive drag (test_wm_geometry.cpp:1492). [wm_menureopen] fired 0/5, so the known-intermittent framing does not cover either. Sharpens deferred item 12 from ~1 test per run to a measured 40% red-run rate. Blocks the v1.0 success-criterion-7 gate capture until the flake substrate is diagnosed. Evidence: evidence/gates/flake-measurement/ (af30f47). T-8-SHELL security half failed 0/5.
- ~~08.5-06 BLOCKED ON OPERATOR RULING~~ RULED 2026-08-31: `inconclusive`, sub-decision **hold**. The run budget was NOT extended. 08.5-06 is complete; the CHAIN REMAINS BLOCKED -- 08.5-07 Task 1 requires CONFIRMED and is UNMET, so 08.5-07, 08.5-08 (criterion-7 capture) and 08.5-05 all stay held. New operator decision: re-plan around a targeted reproducer, not more full-suite sampling. Do not start 08.5-07.
- 08.5-06 Task 4 finding: the WM stderr guard-ordering defect is WIDESPREAD, not a one-off -- only 14 of 59 frame assertions across tests/test_wm_*.cpp carry an INFO("wm stderr guard within two lines above them (a second-opinion audit put it at ~74 sites, naming test_wm_fallbacks.cpp:272, test_wm_lifecycle.cpp:569, test_wm_rules.cpp:355, test_wm_state.cpp:780). CORRECTION to the earlier record: test_wm_geometry.cpp:1490 already carries its guard ABOVE the assertion at :1492 -- that site is correct and the defect must not be described as universal. And hoisting a guard to the TOP of a case does NOT fix it: wmStderr() is evaluated when INFO executes, so a top-of-case guard captures stale pre-stall output. Correct shape: bind the poll result first, then INFO, then assert.
- 08.5-06 Task 4 finding: the 8.23s/8.35s failure times are CENSORED OBSERVATIONS -- the tests' own 8000ms reparent-poll deadlines expiring, not measured stalls. They never supported the cold-cache hypothesis and do not contradict longestms=0. Also: 08.5-07's fix must NOT be split -- shipping the predicate narrowing without the deadline would make the hang MORE deterministic, since the foreign events currently consumed are what accidentally wake the unbounded wait.
- 08.5-06 Task 4 latent bug, not implicated in either red run: after taking the fixture flock, display reservation rechecks the X lock file but NOT the socket (tests/support/WmFixture.h:190). Worth a test.
- **08.5-09 RULED 2026-08-31: verdict REFUTED, 08.5-07 disposition RE-PLAN. 08.5-07, 08.5-08 and 08.5-05 are NOT to be started.** The precommitted terminal rule binds: 08.5-07 is NOT the flake fix and must not be recorded as one. timestamp() may still be hardened on its own merits, but v1.0 stays blocked until the bundle table names the actual failing path, or ruling B is reversed as a NEW, separately recorded release-policy decision. Evidence: evidence/gates/reproducer/ and the single **Verdict:** line in evidence/gates/attribution/README.md (397310a).
- 08.5-09 lead, recorded and NOT attributed: trip-r-3/-4/-5 each carry a BadWindow cascade (X_CreateWindow, X_ReparentWindow, X_MapWindow, X_ChangeSaveSet, all on window 0x80003b) -- the WM framing a window that no longer exists. trip-r-1 and trip-r-2 have clean 12-line stderr and still failed to frame, so it is 3 of 5 and NOT the universal signature. Starting point for the re-plan; a refutation of one mechanism is not an attribution to another.

## Deferred Items

Items acknowledged and carried forward from previous milestone close:

| Category | Item | Status | Deferred At |
|----------|------|--------|-------------|
| *(none)* | | | |

## v1.1 backlog (created by Phase 8.5 drafting)

| Item | Why it is a debt with a name |
|------|------------------------------|
| Gesture and input coverage | Every defect the manual passes found lived in a path the suite already exercised: the suite asserts the *states* controls produce and never the *reachability* of the controls that drive them. Middle-click maximize, the circular fullscreen gesture, the menu's exit and hidden-client rows, and grab-release across cancel paths all have state coverage and no gesture coverage. |
| Rule re-fold on title change | D-8.5-03 declines it deliberately (a renamed document must not move its window). Revisit if users ask for it. |
| X2Go over a real network link | Phase 8.5 measures nxagent nested and locally; the compression proxy under latency is a different question and may remain open after 08.5-02. |

## Session Continuity

Last session: 2026-08-31T10:52:18.892Z
Stopped at: Completed 08.5-09-PLAN.md -- operator ruled REFUTED at the Task 3 blocking-human checkpoint; 08.5-07 disposition RE-PLAN. 08.5-07/-08/-05 NOT to be started; next action is re-planning the attribution
Resume file: None
