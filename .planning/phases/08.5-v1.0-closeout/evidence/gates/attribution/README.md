# Attribution measurement — NOT the gate bundle

**This directory is not the success-criterion-7 gate bundle. That bundle was not
produced. Its capture is still held.**

There is no `build-all-debug.log`, no `build-all-release.log`, no
`build-all-asan.log`, no `static-analysis.log`, no `preflight-versions.log`, no
`ldd-release.txt`, no `runtime-smoke/` and no `PROVENANCE.txt` at the top level
of `evidence/gates/`, and their absence is still deliberate. Nothing here
certifies a tree, and nothing here should be cited as a gate result. The release
tree was not run at all under this plan.

## What this is

The *after* half of the flake measurement whose *before* half is
`evidence/gates/flake-measurement/`. Eight full-suite observations of the
**debug** tree plus one of the **asan** tree, all through the mandated gate path
(`bash scripts/gates/build-all.sh <tree>`, unfiltered suite, `--no-tests=error`
intact), taken with the window manager reporting its own cold-cache property
wait for the first time.

It exists to settle one question that `08.5-RESEARCH.md` tagged `[ASSUMED]`:
whether `WindowManager::timestamp()`'s unbounded blocking `XMaskEvent`
(`src/Manager.cpp:917-924`), invalidated on every loop iteration by
`src/Events.cpp:21`, is what reddens the gate.

Captured at commit `015e8c6` on branch `main`, 2026-08-31, against a source tree
with no uncommitted modifications under `src/`, `include/`, `tests/`, `scripts/`
or `CMakeLists.txt`. Every debug run was produced by
`bash scripts/gates/flake-run.sh <path>`, which writes the host conditions into
the run's own log before invoking the gate unmodified.

**The suite is 314 tests here, not the before-half's 313.** Plan 08.5-06 Task 1
added the `[wm_timestamp]` probe case. The counts in the table below are not
comparable to the before-table's by absolute number, only by pass/fail.

## The host sweep

Both halves of the sweep were run before the first measurement run, under the
anchored survivor rule: a candidate window manager is a process whose
`/proc/<pid>/exe` equals exactly `$R/build/{debug,asan}/wm2-born-again` **and**
whose `DISPLAY` lies inside the fixture's reserved `:120`–`:199` pool. Both
conditions, never either alone. All kills are by PID from an explicit list; no
process was ever selected by a process-name pattern.

| Reading | Before | After |
|---|---|---|
| Orphaned display servers inside the fixture pool `:120`–`:199` | 0 | 0 |
| Orphaned window managers under the anchored survivor rule | 0 | 0 |
| Sockets in the fixture pool | 0 | 0 |
| Lock files in the fixture pool | 0 | 0 |
| PIDs killed — display-server half | 0 | 1 (mid-sequence, see below) |
| PIDs killed — window-manager half | 0 | 0 |
| Stale pool socket/lock pairs cleared | 0 | 1 |

**Both before-figures are zero because the pool had already been swept**, by the
executing session, immediately before this plan resumed: eight orphaned
`wm2-born-again` children on `:127`–`:134` and seventeen orphaned display
servers, all roughly 34 hours old and all left by plan 08.5-04's halted runs,
were killed by explicit PID under this same rule, and twenty-one stale display
locks were cleared. The eight-survivor figure this plan's text predicted was
therefore real; it had simply been collected an hour earlier. **The measurement
below started on an empty pool**, which is the condition that matters, and that
is what makes the comparison with the 0.40 baseline confounded rather than the
sweep's timing.

**Two processes matched the exe anchor and were deliberately left alone**,
because their `DISPLAY` falls outside the fixture's pool:

| PID | DISPLAY | Binary |
|---|---|---|
| 1541961 | `:10.0` | `build/debug/wm2-born-again` |
| 2346022 | `:11` | `build/release/wm2-born-again` |

Both are live desktop sessions launched from this same build tree. **This count
is not zero, and that is the point**: the previously planned substring match
over `ps -eo args` would have killed both. The display-pool condition is the
only thing that excludes them, and it did.

**Two display servers remain on the host and were not touched**: `:20` (the
operator's own, 16 days old) and `:81` (1d 06h). Neither is inside `:120`–`:199`,
so neither can contend for a fixture display, and killing an X server outside
the reserved pool is not required by this measurement and carries the risk the
killing rule exists to avoid. They are recorded, not swept.

**One mid-sequence kill.** The first attempt at run 4 was terminated by the
executing session's own ten-minute command cap, not by the gate. It left a
truncated log with no ledger line and one orphaned display server on `:99`
(pid 3336360, started 01:56:46, exactly when that invocation began) — the
`::setsid()` orphan mechanism the research names. That server was killed by PID
and the truncated log was deliberately removed before index 4 was run cleanly.
The ledger is untouched by this: the wrapper writes its line only after the gate
exits, so an invocation that never reached the gate left no line. See
"Deviations from the run protocol" below.

## The eight debug runs

The first six columns are named identically to the before-table in
`flake-measurement/README.md`. The host-condition columns are **added** on the
right; none of the original six was renamed or dropped. Every host-condition
figure was read from the header the wrapper wrote into that run's own log, not
from a separate reading taken later.

| Run | Exit | Suite | Case 198 | Other failure | Total test time | Load (1 min) | Free MB | Swap used MB | Orphaned Xvfb |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 0 | 314/314 | Passed 30.63 s | — | 337.51 s | 2 | 1090 | 3771 | 2 |
| 2 | 0 | 314/314 | Passed 30.70 s | — | 230.04 s | 3 | 334 | 3978 | 2 |
| 3 | 0 | 314/314 | Passed 30.68 s | — | 251.10 s | 3 | 1167 | 4537 | 2 |
| 4 | 0 | 314/314 | Passed 30.78 s | — | 277.28 s | 7 | 479 | 6566 | 2 |
| 5 | 0 | 314/314 | Passed 30.75 s | — | 230.06 s | 3 | 1155 | 6580 | 2 |
| 6 | 1 | 313/314 | Passed 30.80 s | **#80** — "With Shape forced off the server reports the frame as unshaped" (`tests/test_wm_fallbacks.cpp:272`) | 243.38 s | 5 | 616 | 7040 | 2 |
| 7 | 0 | 314/314 | Passed 30.68 s | — | 232.34 s | 3 | 1599 | 8191 | 2 |
| 8 | 0 | 314/314 | Passed 30.77 s | — | 252.02 s | 1 | 355 | 8191 | 2 |

Logs: `attribution-debug-run1.log` through `attribution-debug-run8.log`, with
run 6 carrying the `-FAILED` suffix its exit code earned it. The executed range
is contiguous from 1, no run was omitted and no run was retried — and those are
checks rather than claims: `RUN-LEDGER.txt` carries exactly one append-only line
per invocation, `uniq -d` over its log names is empty, and the ledger's name set
and the directory's name set are identical.

**The "Orphaned Xvfb" column reads 2 in every row and that is not pool
contention.** It is the wrapper's whole-host bracketed count, and both entries
are the `:20` and `:81` servers named above, neither of which is inside the
fixture's pool. The pool itself held zero servers for the whole sequence.

**Swap used climbs monotonically across the sequence**, from 3771 MB at run 1 to
the full 8191 MB by run 7, on a 32012 MB host whose available memory fell from
13126 MB to 7238 MB over the same span. This is the single clearest trend in the
table and it is recorded because named alternate (c) is exactly host memory
pressure. It is *not* offered as an explanation: the two runs at fully exhausted
swap were both green, and the one red run sat mid-range at 7040 MB.

**Case 198 passed all eight times**, metronomically, 30.63–30.80 s — the same
two 15 s deadlines that produced 30.69–30.72 s across the before-half's four
passing runs. Its Task 2 stderr hoist did not change its duration.

## The red run, in full

From `attribution-debug-run6-FAILED.log`:

- **Line 237:** `82/314 Test #80: With Shape forced off the server reports the
  frame as unshaped ... ***Failed 8.23 sec`
- **Line 248:** the case is `tests/test_wm_fallbacks.cpp:257`, tag `[wm_noshape]`
- **Line 251:** `/home/tomislav/development/wm2-born-again/tests/test_wm_fallbacks.cpp:272: FAILED:`
- **Lines 252–254:** `REQUIRE( frame != 0L )` `with expansion:` `0 != 0`
- **Lines 257–258:** `test cases: 1 | 1 failed` / `assertions: 3 | 2 passed | 1 failed`

`frame` is the return value of that file's own `mapClientAndAwaitFrame()`
(`tests/test_wm_fallbacks.cpp:129`), which returns `None` on exactly one path:
its `XQueryTree`-until-reparented poll expiring on its **8000 ms** deadline
(`tests/test_wm_fallbacks.cpp:148`). The 8.23 s elapsed time is that deadline
plus fixture startup.

**This is the same failure shape as the before-half's case #93**, which failed
at `tests/test_wm_geometry.cpp:1492` on the same `REQUIRE( frame != 0L )` at
8.35 s, inside that file's own independent copy of the same 8000 ms reparent
poll. Two different test files, two independent helpers, one shape: *the window
manager did not reparent a mapped client within eight seconds*. Together with
case 198 — *the window manager did not spawn the program it was asked to spawn
within the deadline* — three distinct cases have now failed across two
measurements and all three are "the window manager did not act on a request",
never an assertion about window-management state coming out wrong.

**And the red run carries no window-manager stderr.** Lines 251–255 run straight
from the expansion to the separator with no `with message:` block. The reason is
concrete and is the finding that matters most here: that case *does* have an
`INFO("wm stderr: " << fixture.wmStderr())` guard, at
`tests/test_wm_fallbacks.cpp:278` — **six lines below the assertion that
failed**. A Catch2 `INFO` annotates only assertions that come after it. Case
198's defect, closed in this plan's Task 2, was the same fact expressed through
lambda scope; this one is the same fact expressed through statement order.

So the one red run in this sequence produced no attribution-bearing observation,
and it did so for a reason that is fixable and now named.

## The counter readings

The mandated gate command runs `ctest --output-on-failure`, which prints a
case's own stdout **only when that case fails**. The `[wm_timestamp]` probe case
passed in all eight runs (3.01 s in run 6, line 494), so its printed summary
appears in none of the eight logs. This is a property of the gate command, which
must not be modified, and it is why the counter reading below was taken by a
separate verbose run of the probe case against the same commit rather than
lifted from a gate log. It is a per-tree reading, not a per-run one, and it is
not offered as a per-run figure.

```
wm2: timestamp: cold=5 blocked=2 foreign=5 longestms=0
```

Read that as: in one probe fixture's lifetime the cold-cache branch was entered
**5** times; **2** of those entries had to block because nothing matching was
already queued; **5 of 5** matches were satisfied by an event that was *not* the
window manager's own root append on `_WM2_RUNNING`; and the longest single
blocked wait was **0 ms**.

Two things follow, and only two:

1. **The branch is live.** It is entered, and it does block. The same two
   diagnostic lines — `wm2: timestamp: entering blocking property wait` and
   `wm2: warning: timestamp: property wait matched a foreign event` — were also
   observed in case 198's own stderr during this plan's Task 2 positive-control
   reading. Half of Open Question 1 is answered: the branch fires in ordinary
   operation.

2. **Under this workload it costs nothing.** `longestms=0` says the blocked
   waits returned essentially immediately. That is evidence against the
   "unbounded wait stalls the window manager's only thread" mechanism *for this
   workload*, and it is not evidence about the workload that reddens the gate,
   because the red run produced no counter reading at all.

**A separate finding, worth recording on its own merits and independent of the
flake question:** `foreign=5` out of `cold=5` means every single wait was
satisfied by a property event belonging to something other than the window
manager's own check window. Those events are removed from the queue and
discarded, so `eventProperty()` never sees them. A `WM_NAME`,
`WM_TRANSIENT_FOR` or `WM_NORMAL_HINTS` change arriving while the timestamp path
is waiting is silently swallowed. That is the predicate-narrowing half of what
plan 08.5-07 was scoped to do, and this measurement says it is not hypothetical.

## The ASan observation

`bash scripts/gates/build-all.sh asan`, run **once**, teed to
`attribution-asan-run1.log`. Never a bare `ctest --test-dir build/asan`
(deferred item 5).

- Exit **0**. `100% tests passed, 0 tests failed out of 314`.
- `Total Test time (real) = 411.20 sec`.
- `asan: no sanitizer findings`. Six suppression-accounting logs, which the gate
  classifies as informational by design.
- `0 compiler/linker warning line(s)`.

**This is one observation, not a rate.** It is the phase's first look at the
asan tree under this suite, taken so that the three-tree capture in plan 08.5-08
is not the first. One green run says nothing about how often this tree is green.

## Verdict

**Verdict:** INCONCLUSIVE

The eight-run budget expired without an attribution-bearing observation. Seven
of eight debug runs were green; the one red run,
`attribution-debug-run6-FAILED.log` **line 251**, failed at
`tests/test_wm_fallbacks.cpp:272` on `REQUIRE( frame != 0L )` and carried **no
window-manager stderr block**, because that case's `INFO` guard is declared at
`tests/test_wm_fallbacks.cpp:278`, below the assertion that failed. No counter
reading exists for that run, so neither the CONFIRMED condition (a red run's
stderr showing the branch entered with a non-zero blocked-wait or foreign-match
count) nor the REFUTED condition (counters showing the branch was never entered,
or entered without blocking or matching foreign while the case failed anyway)
was observed.

**This is not a confirmation and must not be read as one.** The hypothesis in
`08.5-RESEARCH.md` is neither supported nor ruled out by this measurement. What
was established is narrower and is stated above: the branch is entered, it does
block, and every one of its waits in the probe fixture matched a foreign event —
none of which is a link to a failing run.

**The comparison with the 0.40 baseline is confounded and no rate claim is made
from it.** The red-run rate here is 1 of 8; the before-half's was 2 of 5. But
the pool was swept between the two measurements — eight orphaned window managers
and seventeen orphaned display servers removed from inside the fixture's own
`:120`–`:199` pool — so the two sequences did not run on the same host, and a
lower red rate afterwards is a fact about the swept host rather than evidence
about the defect. The 08.5-04 measurement's 0.40 remains the only rate taken on
the unswept host, and nothing here supersedes it.

**Green count: 7 of 8** (runs 1–5, 7, 8), plus the single green asan run.

### Where this goes next

Under the Negative-Result Contract in `08.5-VALIDATION.md` an attribution is
withheld when the measurement does not support one, and that is what has
happened. Plan 08.5-07's Task 1 precondition requires a CONFIRMED verdict and is
**unmet**.

The named alternates from `08.5-RESEARCH.md` § Open Questions 2, with what each
would predict:

| Alternate | What it would predict |
|---|---|
| (a) Deferred item 17's `findOpenMenu()` first-viewable-child fallback, reaching case 198 through the unverified `openRootMenu()`/`selectFirstMenuEntry()` pair | Failures concentrated in the five cases using the unverified menu helper, and none in cases that never open a menu. **Weakened by this measurement:** case #80 opens no menu at all, and neither did case #93. |
| (b) Fixture teardown racing the next fixture's display reservation | Failures clustered at fixture startup, before any interaction — exactly where #80 and #93 both failed, in the reparent poll. **Strengthened by this measurement**, and the cheapest of the three to instrument next. |
| (c) Host memory pressure with swap exhausted making `fork`/`exec` latency unbounded | Red runs correlating with swap exhaustion. **Not supported as stated:** the two runs at fully exhausted swap (7 and 8) were both green and the red run sat mid-range. |

The concrete blocker to settle any of them is the same one this measurement hit:
**the window manager's stderr does not reach the observer for most cases.** The
Task 2 hoist covered case 198 only. Case #80's guard is below its assertion and
case #93's is a single `INFO` covering only its own file's helper. Making the
stderr guard precede the first fallible assertion in every fixture-using case is
a small, mechanical change, and without it another eight runs would very likely
produce another unreadable red.

Nothing in this record attributes any failure to `[wm_menureopen]`, to deferred
item 12 or to any known intermittent. Every claim above cites a file, a line or
a committed log.

## Deviations from the run protocol

Recorded rather than smoothed over, because the integrity gates over this
directory are only worth something if the exceptions are visible.

1. **Run 4's first invocation was killed by the executing session's ten-minute
   command cap**, mid-suite at test #188, with the gate still running. It
   produced no gate result and no ledger line. Its truncated log was removed
   deliberately — which the wrapper's no-clobber rule requires, and which is
   exactly the deliberate `rm` that rule is designed to force — and index 4 was
   then run cleanly. This is not a retry of a run: no observation was discarded,
   because none was ever produced.

2. **Run 6's ledger line was corrected by hand, once, and here is exactly what
   changed.** The wrapper as first written appended its ledger line under the
   name it was invoked with and left the `-FAILED` rename to the caller, which
   made two of this plan's own acceptance criteria unsatisfiable at the same
   time: a red run must keep a `-FAILED` name, and the ledger's name set must
   equal the directory's. The line read
   `2026-08-31T00:12:14Z attribution-debug-run6.log 015e8c6a... 1` and now reads
   `2026-08-31T00:12:14Z attribution-debug-run6-FAILED.log 015e8c6a... 1`.
   Nothing else about it changed — not the timestamp, not the commit, not the
   exit code. The wrapper has since been fixed to perform the rename itself,
   before writing the ledger, so no later run needs this correction.

## Screening applied before commit

- Credential screen over this directory — **no matches**. The pattern is the
  same one plan 08.5-04 used and is written out verbatim in `08.5-06-PLAN.md`
  Task 3's `<verify>` block; it is deliberately **not** quoted here, because a
  record that spells its own screening alternation makes that screen match
  itself on the next run and the failure looks like a finding. That is exactly
  what happened once while this file was being written, and this sentence is
  the fix.
- **No content from `~/.xsession-errors` appears anywhere in these files**
  (T-8-XSESSION). That file was not read, copied or quoted at any point in this
  measurement. The claim is worded about its *contents* rather than about the
  path, because this bullet names the path and a claim that no reference exists
  would be falsified by the bullet making it.
- These are ctest transcripts, not display transcripts: **no window tree, no
  window titles and no host name.** The strings `WM_NAME` and `_NET_WM_NAME`
  appear only inside ctest test *names* (for example
  `_NET_WM_NAME UTF8_STRING encoding`), never as a title read from a display.
  The absolute repository path appears, as it does in the Phase 8 bundle's own
  committed logs.
- Every diagnostic line the window manager contributed is a fixed ASCII state
  word plus an integer (`wm2: timestamp: cold=5 blocked=2 foreign=5
  longestms=0`), so byte-equality greps over these logs are the check
  (T-8.5-TRACE-01).

## Operator ruling — 2026-08-31

Plan 08.5-06's Task 4 is a `checkpoint:decision` with `gate="blocking-human"`.
It was presented to the operator on **2026-08-31** and the operator has ruled.

**Ruling: `inconclusive`. Budget sub-decision: `hold`.**

The measured outcome recorded in the section above stands unchanged and is
accepted as written. The operator did **not** extend the eight-run budget.

### Why the budget was not extended

Because another eight runs would very probably buy another unreadable red. The
"Where this goes next" section above already named the blocker — the window
manager's stderr does not reach the observer for most cases — and the
second-opinion review below establishes that this is a *widespread* property of
the suite rather than a defect at one or two sites. Spending roughly another
half hour of wall clock per run against a ~1-in-8 red rate, with the red run
still likely to arrive without an attribution-bearing stderr block, is compute
spent on a sampling problem whose readout is known to be broken. Holding is the
honest stop; the Negative-Result Contract in `08.5-VALIDATION.md` is what makes
it a clean one rather than a failure.

**This ruling does not unblock anything.** Plan 08.5-07's Task 1 precondition
requires a CONFIRMED result and remains **unmet**, so 08.5-07, 08.5-08 and
08.5-05 all stay blocked.

### New operator decision, recorded here as context and not acted on

> **New operator decision (2026-08-31): the phase re-plans the attribution
> around a targeted reproducer instead of more full-suite sampling.**

Task 4's own `<resolution>` text anticipated exactly this shape of outcome —
"If the operator wants \[a different path\] ... that is a new operator decision
and a re-plan, not a resolution of this checkpoint." That is what this is. It is
**not** a resolution of the checkpoint, it does **not** substitute for a
CONFIRMED result, and it does **not** license starting 08.5-07. It is direction
for the next planning round, and nothing in plan 08.5-06 executes it.

### Second-opinion findings, recorded because the next planner must reach them

The ruling was informed by an adversarial second opinion from Codex CLI
(`gpt-5.6-sol`, read-only against the live working tree). Every finding below
was independently re-verified against the tree by the orchestrator before the
ruling was made, and the file:line references were re-checked once more while
this section was written. They are recorded here rather than in a scratch note
because the next plan in this phase is going to be built on them.

**1. Extending the run budget is wasted compute — the guard-ordering defect is
widespread, not a one-off.** The orchestrator's own check: of **59** frame
assertions across `tests/test_wm_*.cpp`, only **14** carry an
`INFO("wm stderr` guard within two lines above them. Codex's broader audit put
the affected-site count at roughly **74**, naming
`tests/test_wm_fallbacks.cpp:272`, `tests/test_wm_lifecycle.cpp:569`,
`tests/test_wm_rules.cpp:355` and `tests/test_wm_state.cpp:780`. Another red run
landing anywhere in that population would also be unreadable.

**2. CORRECTION to an earlier reading in this record: the geometry site is
already ordered CORRECTLY.** The "Where this goes next" section above says case
\#93's guard is "a single `INFO` covering only its own file's helper", which
reads as though that site shares case \#80's ordering defect. It does not.
`tests/test_wm_geometry.cpp:1490` carries
`INFO("wm stderr: " << fixture.wmStderr())` **directly above** the assertion at
`tests/test_wm_geometry.cpp:1492`, and re-reading those lines confirms it. Only
the fallbacks site is mis-ordered — assertion at
`tests/test_wm_fallbacks.cpp:272`, guard at
`tests/test_wm_fallbacks.cpp:278`. **The ordering defect must not be described as
universal.** It is common (finding 1) but it is not everywhere, and the geometry
site is a counter-example that a mechanical rewrite must not "fix".

**3. Hoisting the guard to the top of a case does NOT fix it.**
`fixture.wmStderr()` is evaluated at the moment the `INFO` statement executes,
not at the moment the annotated assertion fails. A guard placed at the top of a
case therefore captures the window manager's output as it stood *before* the
stall, which is exactly the output that carries no information about the stall.
The correct shape binds the poll result first, then reads stderr, then asserts:

```cpp
bool framed = pollUntil(...);
INFO("wm stderr: " << fixture.wmStderr());
REQUIRE(framed);
```

This matters for whatever plan generalises Task 2's hoist: the naive
generalisation — move every guard to the top of its case — would produce 74
sites that all look fixed and all still report stale output.

**4. The 8-second figure is a CENSORED OBSERVATION, not a measured stall.** The
`***Failed 8.23 sec` on run 6 and the `8.35 s` on the before-half's case \#93
are the tests' own **8000 ms** reparent-poll deadlines expiring
(`tests/test_wm_fallbacks.cpp:148`), plus fixture startup. Neither is a measured
eight-second wait inside `timestamp()`. The true stall length is unobserved and
bounded only from below: it is *at least* long enough to outlast the poll, and
the poll expiring is what ended the observation. Two consequences. First, this
is why `longestms=0` does not contradict the 8.23 s — they are not measurements
of the same thing, and the record above should be read with that in mind.
Second, and more sharply: **the eight seconds never supported the cold-cache
hypothesis in the first place.** It was the harness's own timeout being reported
back, and reading it as evidence about the window manager's wait was a category
error.

**5. Do NOT split plan 08.5-07's fix.** 08.5-07 scopes two changes to
`timestamp()`: (a) bounding the wait with a deadline, and (b) narrowing the
event predicate so the wait stops consuming property events that belong to
something else. Shipping (b) without (a) would make the hang **more** reliable,
not less. The foreign property events that the wait currently swallows are what
accidentally wake it; remove them and leave the wait unbounded and it wedges
deterministically instead of intermittently. Verified as sound against
`src/Manager.cpp`. Relatedly, the `foreign=5` of `cold=5` reading above is a
weaker finding than the "separate finding" paragraph makes it sound:
**`foreign` does not mean "a client event `eventProperty()` needed."** It means
"not the window manager's own root append on `_WM2_RUNNING`", which also covers
the window manager's own root `_NET_CLIENT_LIST` and `_NET_ACTIVE_WINDOW`
traffic — property changes `eventProperty()` (`src/Events.cpp:535`) would ignore
anyway. Five of five being foreign is consistent with nothing of consequence
having been swallowed. The swallowing defect is real as a *mechanism*; this
counter reading does not measure its *impact*.

**6. The causal chain is real and verified — which is what keeps the hypothesis
alive rather than refuted.** The fixture's readiness probe returns as soon as
its own probe window is reparented (`tests/support/WmFixture.h:666`). But the
window manager, having reparented that window, then proceeds:
`m_border->reparent()` (`src/Client.cpp:251`) → `activate()`
(`src/Client.cpp:275`) → `timestamp()` (`src/Client.cpp:315`) — where it can
wedge on the unbounded wait. So fixture construction can legitimately return
"ready" at the exact moment the window manager is about to block, and the
**next** window's `MapRequest` is then never processed. That is a complete,
line-by-line path from "fixture says ready" to "the next client is never
framed", and it is the shape both \#80 and \#93 exhibit. All four line
references re-checked against the tree.

**7. The recommended next round: a cheaper decisive experiment.** An isolated
post-readiness reparent reproducer with hang-time stack capture. Loop only the
startup-and-reparent sequence rather than the full suite; on a short diagnostic
threshold — around **500 ms**, well inside the existing 8000 ms deadline so the
capture happens while the window manager is still wedged rather than after the
poll has given up — capture `fixture.wmAlive()`, the window manager's stderr,
the Xvfb log, `/proc/<wm-pid>/wchan`, and ideally
`gdb -batch -p <pid> -ex 'thread apply all bt'`. The readout is decisive in both
directions: a backtrace **inside `XMaskEvent` CONFIRMS** the hypothesis; a
backtrace in `poll`, in an Xlib round trip, in teardown, or against a window
manager that is already dead **REFUTES** it. Iteration cost is seconds rather
than the ~4 minutes a full-suite debug run costs, which is the whole argument
for preferring it over more sampling.

**8. A separate latent bug worth a test, not implicated in either red run.**
After taking the fixture's flock, the display reservation re-checks the X lock
file but not the socket (`tests/support/WmFixture.h:190`). An external X server
that has created its socket but not yet its lock file — or whose lock file has
been cleared while the socket lives — would pass this re-check. Recorded here so
it is not lost; there is no evidence it contributed to run 6 or to the
before-half's case \#93, and it should not be folded into the attribution
question.

### What this ruling changes in the record above

Nothing, except by correction. The per-run table, the host-condition columns,
the rates, the log filenames, the counter readings, the ASan observation and the
stated outcome are all unchanged and are still the record. Finding 2 corrects
one interpretive reading about the geometry site; finding 4 corrects how the
8.23 s / 8.35 s figures should be read; finding 5 narrows what the
`foreign=5` reading is entitled to claim. The measured facts those readings were
drawn from are not disturbed.
