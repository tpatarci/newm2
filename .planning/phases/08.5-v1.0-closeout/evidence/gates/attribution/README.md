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
