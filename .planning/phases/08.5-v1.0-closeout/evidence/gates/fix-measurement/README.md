# Fix measurement — NOT the gate bundle

**This directory is not the success-criterion-7 gate bundle**, and it is not a
proof that the flake is gone. It is the operational record that the debug gate
is green in one shot, ten times in a row with no run omitted and none retried.
The first two series below are the **pre-capture precondition** for plan
08.5-08 (ten green at `5ffee20`, then ten green at `70fbd8f`); the capture
itself then found and fixed two things in the source, so those series are not
final-tree evidence. The final-tree series, at the capture commit `d08b6e5`, is
the third section, added after it ran; until that section exists, nothing here
speaks for the tree that shipped.
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
found two things in it (`../capture-8e29d6d/README.md`): five cppcheck
findings in the modal loops (dead stores, one shadowed local) and the window
manager's own name published one byte short. Commit `2a94cbb` fixed both, so
the tree under test here differs from the capture tree by exactly that commit;
`git diff --stat 70fbd8f..2a94cbb -- src include tests` names the three source
files and one test file. The store deletions and the rename cannot change
behaviour; the name fix changes one property's length. A third series at the
capture tree itself is recorded in its own section below, added after it ran.
Logs: `run-01.log` .. `run-10.log`; ledger: `LEDGER.txt`.

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

## Runs stopped and discarded

Two later attempts were started and stopped before completing, each because a
Codex re-review finding changed the source tree mid-measurement: one run at
`2128a4d` (green) and two at `4830909` (both green). Their logs were removed
rather than kept, because a partial series at a superseded tree is neither a
measurement nor a gate result; they are named here so the ledger's gap in
commit hashes is not read as a hidden red run.

## What this does not establish

- **It is not a rate proof.** The before-rate (`flake-measurement/README.md`)
  was 2 red of 5 full debug runs at `69da106`. Against that, 0 red of 10 gives a
  one-sided Fisher exact `p ≈ 0.095`; 0 of 16 would have been needed for
  `p < 0.05`. Pooling both tens as 0 of 20 gives `p ≈ 0.033`, but the two
  tens are at different trees and pooling them assumes the review fixes did not
  touch the failing paths, which is an assumption rather than a reading. The
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
