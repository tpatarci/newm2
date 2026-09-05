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

Both failing cases are the timing-sensitive class `08.5-CONTEXT.md` already
flags — a spawn-await deadline and a frame-await deadline. Case #93 did **not**
fail in its drag; it failed in `mapClientAndAwaitFrame()` before its first
click, and the description of it as an interactive-drag failure is corrected in
the appended section "Correction (2026-08-31, plan 08.5-06)" below. A defect in
the exec path cannot express itself as a geometry failure, which is why the
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

## Correction (2026-08-31, plan 08.5-06)

This section corrects one interpretive description in the body above. **No
measurement in this directory changes.** The per-run table, its column names,
the rates, the isolation figures, the `[wm_menureopen]` finding and the
T-8-SHELL finding all stand exactly as recorded, and no log file has been
renamed, edited or removed.

### What case #93 actually did

Read from `build-all-debug-run2.log` — the committed log itself, not from any
summary:

- **Case:** `#93 — A window dragged off the top-left keeps its tab on screen`,
  `tests/test_wm_geometry.cpp:1474`, tag `[wm_geometry]`.
- **Failing line:** `tests/test_wm_geometry.cpp:1492`.
- **Assertion:** `REQUIRE( frame != 0L )`, with expansion `0 != 0`.
- **Assertion tally:** `test cases: 1 | 1 failed` / `assertions: 5 | 4 passed |
  1 failed`.
- **Elapsed:** `***Failed 8.35 sec`.

`frame` is the return value of `mapClientAndAwaitFrame()`, called one line
earlier at `tests/test_wm_geometry.cpp:1489`. That helper returns `None` on
exactly one path: its reparent poll — `XQueryTree` until the client's parent is
neither `None` nor the root — expiring on its **8000 ms** deadline at
`tests/test_wm_geometry.cpp:195`. The 8.35 s elapsed time is that deadline plus
fixture startup, which is what a `0 != 0` expansion at that line means.

The case's `XTestDriver` is not even constructed until
`tests/test_wm_geometry.cpp:1499`, seven lines *after* the assertion that
failed. **No click, no press, no drag and no pointer motion had been issued when
this failure occurred.** The window manager had simply not reparented a mapped
client within eight seconds.

### The two consequences

1. **Both observed failures are the same shape.** Case #198 is "the window
   manager did not spawn the program we asked for, within the deadline"; case
   #93 is "the window manager did not frame the client we mapped, within the
   deadline". Neither is an assertion about window-management *state* that came
   out wrong. Both are the window manager failing to act on a request inside a
   test-side deadline. That makes the two failures considerably more alike than
   the halt record treated them — it read them as one spawn problem and one
   input problem, and they are two instances of the same "did not act" shape.

2. **`dragSettle()`'s 90 ms is not implicated in either failure.** Case #93
   never reached a drag, so no inter-event spacing constant participated in it,
   and case #198 spawns through the root menu and never calls `dragSettle()` at
   all. A fix that widened drag spacing — or any other `constexpr int k*Ms` —
   would therefore be chasing a symptom that was never present in either
   observation. The substrate to look for is on the window-manager side of the
   boundary, not in the harness's input timing.

### On `08.5-04-SUMMARY.md`

`08.5-04-SUMMARY.md` carries the same earlier "interactive drag" description.
It is **deliberately left unmodified**, and `git diff --stat` over it is empty
for plan 08.5-06. A summary is a historical record of what was believed at the
moment it was written; rewriting it would destroy the record of the belief and
leave no trace that the correction ever happened. The correction lives here,
beside the evidence it was read from, and this section is where a later reader
is pointed.
