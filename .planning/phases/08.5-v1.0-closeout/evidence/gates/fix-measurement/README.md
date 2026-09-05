# Fix measurement — NOT the gate bundle

**This directory is not the success-criterion-7 gate bundle**, and it is not a
proof that the flake is gone. It is the operational record that the debug gate
is green in one shot, ten times in a row with no run omitted and none retried.
The first two series below are the **pre-capture precondition** for plan
08.5-08 (ten green at `5ffee20`, then ten green at `70fbd8f`); the capture
itself then found and fixed two things in the source, so those series are not
final-tree evidence. The final-tree series, at the capture commit `39de548`, is
the third section: ten green at 337 tests, the tree that shipped.
The gate bundle itself sits at the top level of `evidence/gates/`; the
diagnosis is under `attribution/` and `wakeup/`; the fixes and their
RED-before / GREEN-after records are under `eventloop/` (08.5-11),
`timestamp/` (08.5-12), `menupaint/` (08.5-13) and `modal/` (ledger 8).

## What this is

Each run is one invocation of `bash scripts/gates/build-all.sh debug` —
configure, build, the unfiltered suite with `--no-tests=error` intact — made
by a wrapper that refuses to overwrite an existing run log and appends one line
per run to an append-only `LEDGER.txt` with the commit, start and end times,
exit code and the host's 1-minute load average and available memory at the
moment the run started. The wrapper is ten runs in a `for` loop; there is no
branch that retries or skips.

Host: the development workstation, 8 logical CPUs, 31 GiB RAM,
Linux 6.8.0-124-generic. Not a VPS; the 512 MiB VPS budget is measured elsewhere
(`evidence/STRESS-RESULTS.md`, the `[wm_resource]` cases).

## Ten runs at the pre-capture tree — commit `70fbd8f`

This was written as "the final source tree" and then the first 08.5-08 capture
found two things in it (`../capture-attempts/README.md`): five cppcheck
findings in the modal loops (dead stores, one shadowed local) and the window
manager's own name published one byte short. Commit `2a94cbb` fixed both, so
the tree under test here differs from the final capture tree by that commit
and the five review-fix commits after it (`git log --oneline 70fbd8f..39de548
-- src include tests scripts CMakeLists.txt` lists six). The third series, at
the capture tree `39de548` itself, is the section below.
Logs: `at-70fbd8f/run-01.log` .. `run-10.log`; ledger: `at-70fbd8f/LEDGER.txt`.

| Run | Start (UTC) | End (UTC) | Exit | load1 at start | MemAvailable (MiB) | ctest summary |
|---|---|---|---|---|---|---|
| 1 | 2026-09-05T12:51:49Z | 2026-09-05T12:55:48Z | 0 | 3.45 | 14876 | 100% tests passed, 0 tests failed out of 335 |
| 2 | 2026-09-05T12:55:48Z | 2026-09-05T12:59:46Z | 0 | 3.20 | 15408 | 100% tests passed, 0 tests failed out of 335 |
| 3 | 2026-09-05T12:59:46Z | 2026-09-05T13:03:44Z | 0 | 4.13 | 15263 | 100% tests passed, 0 tests failed out of 335 |
| 4 | 2026-09-05T13:03:44Z | 2026-09-05T13:07:41Z | 0 | 4.16 | 15037 | 100% tests passed, 0 tests failed out of 335 |
| 5 | 2026-09-05T13:07:41Z | 2026-09-05T13:11:39Z | 0 | 4.28 | 14910 | 100% tests passed, 0 tests failed out of 335 |
| 6 | 2026-09-05T13:11:39Z | 2026-09-05T13:15:42Z | 0 | 3.01 | 15139 | 100% tests passed, 0 tests failed out of 335 |
| 7 | 2026-09-05T13:15:42Z | 2026-09-05T13:19:40Z | 0 | 4.18 | 15098 | 100% tests passed, 0 tests failed out of 335 |
| 8 | 2026-09-05T13:19:40Z | 2026-09-05T13:23:38Z | 0 | 5.18 | 15026 | 100% tests passed, 0 tests failed out of 335 |
| 9 | 2026-09-05T13:23:38Z | 2026-09-05T13:27:36Z | 0 | 3.78 | 14997 | 100% tests passed, 0 tests failed out of 335 |
| 10 | 2026-09-05T13:27:36Z | 2026-09-05T13:31:33Z | 0 | 4.50 | 15591 | 100% tests passed, 0 tests failed out of 335 |

**10 of 10 green, 335 of 335 tests each.** Runs took 3 min 57 s to 4 min 03 s.
Concurrent with the runs: one Codex review process reading a sibling worktree,
which during runs 1-3 built one test target and executed one test case there.
The load-average column records what that amounted to.

## Ten runs at the pre-review tree — commit `5ffee20`

Taken earlier the same day, before the Codex review of the closeout's source
changes produced five rounds of fixes (`review-fixes/README.md`). Kept under
`at-5ffee20/` because they are a measurement in their own right and because
the review fixes changed the source under test. 330 tests at that tree; the
five added since are the review-fix regression cases.

| Run | Start (UTC) | End (UTC) | Exit | load1 at start | MemAvailable (MiB) | ctest summary |
|---|---|---|---|---|---|---|
| 1 | 2026-09-05T11:24:41Z | 2026-09-05T11:28:35Z | 0 | 3.36 | 14061 | 100% tests passed, 0 tests failed out of 330 |
| 2 | 2026-09-05T11:28:35Z | 2026-09-05T11:32:30Z | 0 | 3.83 | 13036 | 100% tests passed, 0 tests failed out of 330 |
| 3 | 2026-09-05T11:32:30Z | 2026-09-05T11:36:23Z | 0 | 2.13 | 13794 | 100% tests passed, 0 tests failed out of 330 |
| 4 | 2026-09-05T11:36:23Z | 2026-09-05T11:40:17Z | 0 | 3.40 | 12767 | 100% tests passed, 0 tests failed out of 330 |
| 5 | 2026-09-05T11:40:17Z | 2026-09-05T11:44:12Z | 0 | 2.20 | 13661 | 100% tests passed, 0 tests failed out of 330 |
| 6 | 2026-09-05T11:44:12Z | 2026-09-05T11:48:11Z | 0 | 2.88 | 13407 | 100% tests passed, 0 tests failed out of 330 |
| 7 | 2026-09-05T11:48:11Z | 2026-09-05T11:52:05Z | 0 | 2.82 | 12548 | 100% tests passed, 0 tests failed out of 330 |
| 8 | 2026-09-05T11:52:05Z | 2026-09-05T11:55:59Z | 0 | 2.18 | 13874 | 100% tests passed, 0 tests failed out of 330 |
| 9 | 2026-09-05T11:55:59Z | 2026-09-05T11:59:53Z | 0 | 2.54 | 15125 | 100% tests passed, 0 tests failed out of 330 |
| 10 | 2026-09-05T11:59:53Z | 2026-09-05T12:03:48Z | 0 | 3.52 | 14789 | 100% tests passed, 0 tests failed out of 330 |

**10 of 10 green, 330 of 330 tests each.** Concurrent with these runs: a full
debug-suite run at `-j2` in a sibling worktree (whose two failures were the
load-sensitive cases noted in `review-fixes/README.md`, each passing 2/2 alone),
and Codex review processes.

## Ten runs at the final tree — commit `39de548` (the criterion-7 capture commit)

The series this directory exists for: the tree the gate bundle at the top level
of `evidence/gates/` describes, after every review fix. Nothing under `src/`,
`include/`, `tests/`, `scripts/` or `CMakeLists.txt` changed between this
commit and the phase's final commit (`git diff --name-only 39de548..HEAD --
src include tests scripts CMakeLists.txt` prints nothing). Logs: `run-01.log`
.. `run-10.log`; ledger: `LEDGER.txt`. Started 2026-09-05T17:09:32Z, finished 2026-09-05T17:49:24Z.

| Run | Start (UTC) | End (UTC) | Exit | load1 at start | MemAvailable (MiB) | ctest summary |
|---|---|---|---|---|---|---|
| 1 | 2026-09-05T17:09:32Z | 2026-09-05T17:13:36Z | 0 | 1.92 | 15139 | 100% tests passed, 0 tests failed out of 337 |
| 2 | 2026-09-05T17:13:36Z | 2026-09-05T17:17:35Z | 0 | 0.49 | 15407 | 100% tests passed, 0 tests failed out of 337 |
| 3 | 2026-09-05T17:17:35Z | 2026-09-05T17:21:34Z | 0 | 0.43 | 15576 | 100% tests passed, 0 tests failed out of 337 |
| 4 | 2026-09-05T17:21:34Z | 2026-09-05T17:25:32Z | 0 | 1.50 | 15411 | 100% tests passed, 0 tests failed out of 337 |
| 5 | 2026-09-05T17:25:32Z | 2026-09-05T17:29:31Z | 0 | 2.64 | 16892 | 100% tests passed, 0 tests failed out of 337 |
| 6 | 2026-09-05T17:29:31Z | 2026-09-05T17:33:29Z | 0 | 3.70 | 16506 | 100% tests passed, 0 tests failed out of 337 |
| 7 | 2026-09-05T17:33:29Z | 2026-09-05T17:37:27Z | 0 | 2.67 | 16892 | 100% tests passed, 0 tests failed out of 337 |
| 8 | 2026-09-05T17:37:27Z | 2026-09-05T17:41:25Z | 0 | 2.66 | 16852 | 100% tests passed, 0 tests failed out of 337 |
| 9 | 2026-09-05T17:41:25Z | 2026-09-05T17:45:24Z | 0 | 0.97 | 17130 | 100% tests passed, 0 tests failed out of 337 |
| 10 | 2026-09-05T17:45:24Z | 2026-09-05T17:49:24Z | 0 | 2.54 | 16735 | 100% tests passed, 0 tests failed out of 337 |

**10 of 10 green, 337 tests each.** Concurrent with these runs: nothing that
builds or tests; documentation edits only (the Codex review of `39de548` was
deliberately held until the series finished, because its sandbox builds in
this worktree).

## Runs stopped and discarded

Three later attempts were started and stopped before completing, each because a
review finding changed the source tree mid-measurement: one run at `2128a4d`
(green), two at `4830909` (both green) and six at `f54de6e` (all green, stopped
when Codex's second pass over the review fixes changed `src/Client.cpp` once
more). Their logs were removed
rather than kept, because a partial series at a superseded tree is neither a
measurement nor a gate result; they are named here so the ledger's gap in
commit hashes is not read as a hidden red run.

## What this does not establish

- **It is not a rate proof.** The before-rate (`flake-measurement/README.md`)
  was 2 red of 5 full debug runs at `69da106`. Against that, 0 red of 10 gives a
  one-sided Fisher exact `p ≈ 0.095`; 0 of 16 would have been needed for
  `p < 0.05`. Pooling all three tens as 0 of 30 gives `p ≈ 0.017`
  (`C(5,2)/C(35,2)`), but the three tens are at different trees and pooling
  them assumes the review fixes did not touch the failing paths, which is an
  assumption rather than a reading. The
  evidence that the flake is fixed is the attribution (`wakeup/README.md`,
  ruled ATTRIBUTED) and the three RED-to-GREEN records above, not this table.
- **It is the debug tree only.** Release and ASan are gated once each in the
  criterion-7 capture, not sampled here.
- **It is one host.** An Ubuntu 24.04 host with fonts-dejavu-core 2.37-8 fails
  exactly three font-metric geometry cases regardless of the flake (issue #1);
  the suite is not portable across font packages yet, so this table says nothing
  about other hosts.
- **Nothing re-runs it.** A later commit that touches the source moves the tree
  away from the one measured, as `2a94cbb` did to the `70fbd8f` series, and no
  hook or gate re-runs the ten.
