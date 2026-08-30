# Flake measurement — NOT the gate bundle

**This directory is not the success-criterion-7 gate bundle. That bundle was not
produced. Its capture was held.**

There is no `build-all-debug.log`, no `build-all-release.log`, no
`build-all-asan.log`, no `static-analysis.log`, no `preflight-versions.log`, no
`ldd-release.txt`, no `runtime-smoke/` and no `PROVENANCE.txt` at the top level
of `evidence/gates/`, and their absence is deliberate. Plan 08.5-04 halted
before capturing them. Nothing here certifies a tree, and nothing here should be
cited as a gate result.

## What this is

Five full-suite observations of the **debug** tree, run through the mandated
gate path (`bash scripts/gates/build-all.sh debug`, unfiltered 313-test suite,
`--no-tests=error` intact), plus a twelve-run isolation measurement of one case.

They exist because plan 08.5-04's first gate run went red, and the plan forbids
resolving a red gate by re-running until it passes. The measurement was taken to
decide whether the failure was a regression or a pre-existing flake. It measured
a **pre-existing load-dependent flake**, and on that measurement the operator
held the gate capture rather than recording a signoff against a gate that is not
reliably green in one shot.

Captured at commit `69da106` on branch `main`, 2026-08-30, against a source tree
with no uncommitted modifications under `src/`, `include/`, `tests/`, `scripts/`
or `CMakeLists.txt`.

## The five full-suite runs

| Run | Exit | Suite | Case 198 | Other failure | Total test time |
|---|---|---|---|---|---|
| 1 | 1 | 312/313 | **FAILED** 120.61 s | — | 322.41 s |
| 2 | 1 | 312/313 | Passed 30.72 s | **#93** — "A window dragged off the top-left keeps its tab on screen" (`tests/test_wm_geometry.cpp:1492`) | 241.37 s |
| 3 | 0 | 313/313 | Passed 30.72 s | none | 309.22 s |
| 4 | 0 | 313/313 | Passed 30.69 s | none | 227.80 s |
| 5 | 0 | 313/313 | Passed 30.72 s | none | 228.06 s |

Logs: `build-all-debug-run1-FAILED.log` through `build-all-debug-run5.log`.
Run 1 keeps its original name from the moment it was produced.

Case 198 is `exec-using-shell gates whether a command with arguments and
metacharacters is evaluated by a shell`, `tests/test_wm_runtime.cpp:1461`,
tag `[wm_config_runtime]`.

## The rates

- **Case 198 under full-suite load: 1 of 5 runs failed.**
- **Case 198 in isolation: 0 of 12 runs failed** (`ctest --test-dir build/debug
  -I 198,198`, 31–32 s each — see `isolation-198-12runs.txt`). The failure did
  not reproduce in isolation even once.
- **Suite level: 2 of 5 runs red**, each with exactly **one** failing test, and
  **a different test each time** (#198 in run 1, #93 in run 2).

Case 198's passing duration is metronomic across four runs — 30.69 to 30.72 s,
which is the negative half's two 15 s deadlines. The 120.61 s failure is exactly
the four deadlines (45 + 45 + 15 + 15) with nothing arriving: a spawn that never
happened, not a crash.

## The known intermittent does not cover this

`[wm_menureopen]`, the intermittent recorded in `08.5-CONTEXT.md` at a measured
11/1 rate, **fired in zero of these five runs.**

Neither observed failure is that case. Deferred item 12's "known intermittent"
framing therefore does not cover either of them, and nothing in this directory
attributes either failure to it. The finding is recorded as measured, not
explained.

## What bounds the severity: the security half never failed

Case 198 has two halves that share one command string. Across all five runs the
only assertions that ever failed were `tests/test_wm_runtime.cpp:1534`, `:1535`
and `:1536` — the **positive control**, which asks whether the spawn happened at
all.

**The security half — `:1541`, `:1542`, `:1543`, which is threat T-8-SHELL's
claim that with shell mode OFF nothing is evaluated by a shell — failed 0 of 5
times.** It does not appear in a FAILED line in any run.

So the observed failure is confined to "the program we asked to be spawned
sometimes does not come up under load". The property the case exists to protect
is green in every observation.

## Neither failing case is new work

Both case bodies are **untouched since Phase 8's gate commit `c61cb4b`**:

- Case 198 was introduced in `d8d1281` (08-13). `git log -L 1461,1544:tests/test_wm_runtime.cpp c61cb4b..HEAD` returns nothing.
- Case 93 lives in `tests/test_wm_geometry.cpp`, which this phase did not modify.

`tests/test_wm_runtime.cpp` did change after `c61cb4b`, in `e5e5c11` (08.5-01
title read) and `0244c06` (08.5-02 palette), but neither touched case 198.

Both failing cases are the timing-sensitive interactive class `08.5-CONTEXT.md`
already flags — a spawn-await deadline and an interactive drag. A defect in the
exec path cannot express itself as a geometry drag failure, which is why the
different-case-each-time pattern reads as a shared load-dependent timing
substrate rather than as a defect in either case.

## What this directory does not establish

- **Not a cause.** Nothing here explains *why* the suite drops roughly one test
  per loaded run. It quantifies the rate; it does not diagnose it.
- **Not a release/asan result.** Only the debug tree was run. The release and
  asan trees were not run at all under this plan.
- **Not a verdict on any shipped behaviour.** Every failure was a test-side
  timeout waiting for something to appear. No assertion about window management
  state failed.

## Screening applied before commit

- Credential screen over this directory (`API_KEY|ACCESS_KEY|SECRET|TOKEN|PRIVATE KEY|Bearer `) — no matches.
- No reference to `~/.xsession-errors` anywhere in these files (T-8-XSESSION).
- These are ctest transcripts, not display transcripts: they contain no window
  tree, no window titles and no host name. The absolute repository path appears,
  as it does in the Phase 8 bundle's own committed logs.
