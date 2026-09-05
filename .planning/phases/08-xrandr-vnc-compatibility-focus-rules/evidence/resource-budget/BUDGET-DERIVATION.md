# Resource budget derivation (D-32)

The two-stage record behind the constants in `tests/test_wm_resource.cpp`.
Stage 1 measured; stage 2 judges against numbers a human then wrote down. The
judged run never computes its own bar — see below for why that is structural
rather than a matter of discipline.

## Calibration run

| | |
|---|---|
| Date | 2026-08-29 |
| Host | `tomislav-HP-Z440-Workstation` |
| Distro | Ubuntu 22.04.5 LTS, Linux 6.8.0-124-generic |
| Server | Xvfb 1024x768x24, one `WmFixture` instance per measurement |
| Commit | `0e56fc8` (branch `main`) |
| Command (debug) | `./build/debug/test_wm_resource '[wm_resource_calibration]' -s` |
| Command (asan) | `LSAN_OPTIONS=suppressions=tests/lsan.supp:fast_unwind_on_malloc=0 ./build/asan/test_wm_resource '[wm_resource_calibration]' -s` |

Full transcripts: `calibration-debug.txt`, `calibration-asan.txt`.

## Measured — resident set size, `/proc/<pid>/statm`

| Clients mapped | debug | asan |
|---|---|---|
| 0 | 11724 kB | 34736 kB |
| 1 | 11724 kB | 34744 kB |
| 5 | 11748 kB | 34916 kB |
| **20** | **11840 kB** | **35300 kB** |

**Read the shape, not just the last row.** Almost all of it is startup — the Xft
font, the colours, the application cache. A managed window costs the window
manager roughly **six kilobytes**, and twenty of them add under 1% to the
figure. So this budget is overwhelmingly a bound on what the WM holds *at rest*
and only marginally a bound on per-window cost. That is stated in the test file
too, so nobody reads it as a tight per-client bound it is not.

## Measured — idle CPU, 20 clients mapped, no X traffic at all

| Tree | Window | CPU ticks accrued | % of wall clock |
|---|---|---|---|
| debug | 30.00 s | **0** (at 100 Hz) | 0.0000 % |
| asan | 30.00 s | **0** (at 100 Hz) | 0.0000 % |

Zero ticks in both trees. The event loop is genuinely blocked in `poll()` rather
than spinning.

## The budgets chosen

```
kRssBudgetKbDebug = 24576    // 24 MB -- 4.7% of the 512 MB target
kRssBudgetKbAsan  = 98304    // 96 MB -- diagnostic tree only, never ships
kIdleCpuPercentBudget = 1.0  // same in both trees, deliberately
```

The RSS budget is stated as an **absolute** resident figure, not as growth from
a baseline, precisely because the ladder above shows growth is the small term. A
growth budget here would happily pass a window manager that held 300 MB at
startup.

The idle-CPU bar is the **same number in both trees on purpose**: a blocked
`poll()` costs the same nothing whether or not the binary is instrumented, so a
sanitizer-specific allowance would be an allowance for a busy-wait.

## Why the judged run cannot have produced its own bar

The calibration case is tagged `[.][wm_resource_calibration]`. The leading `.`
makes it **hidden**, so Catch2 does not list it and `catch_discover_tests`
therefore never registers it with ctest. It is not "excluded by convention" —
it is not in the judged suite at all, and can only be run by asking for it by
name. That is the mechanism, not a convenience.

## Mutation testing

Both assertions were mutated to prove they are not vacuous. Production code was
mutated, not the test.

| # | Mutation | Result |
|---|---|---|
| **M1** | `src/Events.cpp:191` — `poll(fds, 2, timeout)` → `poll(fds, 2, 0)`, i.e. busy-poll instead of blocking | **REDDENS both idle cases.** Measured **99.87 %** and **99.80 %** of wall clock against a 0.0000 % baseline. Not a marginal signal. |
| **M2a** | `Client::Client()` — leak 1 MB per managed client, **touching one byte** | **GREEN.** See the finding below. |
| **M2b** | `Client::Client()` — leak 1 MB per managed client, **`memset` over the whole block** | **REDDENS the memory case.** 11840 kB → **36592 kB** against a 24576 kB budget. |

### The finding M2a produced, recorded rather than discarded

**An untouched allocation is invisible to a resident-set budget.** `new char[1 MB]`
followed by a single byte write makes exactly one page resident, so twenty of
them cost 80 kB, not 20 MB, and the assertion passes. RSS measures pages the
process has actually faulted in — which is the right thing to measure for a
512 MB VPS, because untouched pages cost that VPS nothing either.

The consequence for a future reader: this gate catches leaks of memory the WM
**uses**. It does not catch an over-allocation the WM never touches. That is a
deliberate property of choosing RSS over `VmSize` (the alternative would charge
the WM for ASan's enormous virtual reservation and measure nothing useful), but
it is a limit, and a limit that is written down is not a trap.

### Idle-CPU sampler: the positive control

The idle case asserts `afterWork > 0` — the WM must have accrued *some* CPU while
framing twenty windows — before asserting the idle window accrued almost none. A
sampler that reported zero for a busy process would otherwise make the idle
assertion pass for entirely the wrong reason. Measured: 5 ticks (debug) and 58
ticks (asan) accrued during framing, then 0 over the following 30 s.

## Refreshing this

If the host or the build mode changes materially: re-run stage 1, commit the new
transcripts here, update the table above, and change the constant **deliberately**.
Widening a budget because a run exceeded it is the one thing this file exists to
prevent. A run over budget is a finding.
