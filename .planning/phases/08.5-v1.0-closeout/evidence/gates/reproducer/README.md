# Targeted reproducer measurement — NOT the gate bundle

> *Historical note, added 2026-09-05 by the phase verification:* the paragraphs below were
> written while plan 08.5-04's capture was held and say the criterion-7 bundle was not
> produced. That was true when written. The bundle now exists at the top level of
> `evidence/gates/` (plan 08.5-08, `PROVENANCE.txt`); this directory's own content is
> unchanged and is still not a gate result.


**This directory is not the success-criterion-7 gate bundle. That bundle was not
produced. Its capture is still held.**

There is no `build-all-debug.log`, no `build-all-release.log`, no
`build-all-asan.log`, no `static-analysis.log`, no `preflight-versions.log`, no
`ldd-release.txt`, no `runtime-smoke/` and no `PROVENANCE.txt` at the top level
of `evidence/gates/`, and their absence is still deliberate. **Nothing here
certifies a tree, nothing here is a rate, and nothing here may be cited as a
gate result.** The release tree was not run at all under this plan.

## What this is

A **targeted reproducer** of the post-readiness reparent sequence, run once
against a budget stated before it started. It replaces the *method* of the
eight-run full-suite sampling recorded in `evidence/gates/attribution/`, not
that measurement's outcome — which stands unchanged at `INCONCLUSIVE` on its own
terms.

It exists to settle, in either direction, the one question `08.5-RESEARCH.md`
tagged `[ASSUMED]`: whether `WindowManager::timestamp()`'s unbounded blocking
`XMaskEvent` (`src/Manager.cpp:944,958-959`), with the cached time invalidated on
every loop iteration by `src/Events.cpp:21`, is what stops the window manager
reparenting a mapped client.

Captured at commit `3b0fd37778b46b043ccdeb13f287835f5b7a5979` on branch `main`,
2026-08-31, against a source tree with **no uncommitted modifications** under
`src/`, `include/`, `tests/`, `scripts/` or `CMakeLists.txt`. That statement is a
reading rather than a claim: the emptiness of
`git status --porcelain -- src include tests scripts CMakeLists.txt` was written
into `measurement.log` by the run itself, at line 5.

Every figure below traces to a line in `measurement.log`, which is the run's
whole output and is committed beside this file.

## The capability reading

Verbatim from `bash scripts/diag/wm-stack-capture.sh --probe`, recorded into
`measurement.log` immediately before the run:

```
gdb: PRESENT /usr/bin/gdb GNU gdb (Ubuntu 12.1-0ubuntu1~22.04.2) 12.1
ptrace-scope: PRESENT 1
wchan: PRESENT self=0
```

All three channels the capture depends on were available. `ptrace_scope` reading
`1` is Yama's restricted mode, under which the attach is possible only because
the fixture's forked window-manager child declares the test process as its
permitted tracer — the narrow, opt-in declaration added by this plan's Task 1.

## The host sweep

Under the anchored survivor rule: a candidate window manager is a process whose
`/proc/<pid>/exe`, with a trailing ` (deleted)` stripped, equals exactly
`$R/build/{debug,asan}/wm2-born-again` **and** whose `DISPLAY` lies inside the
fixture's reserved `:120`–`:199` pool. Both conditions, never either alone. No
process was selected by a process-name pattern at any point.

| Reading | Before the run |
|---|---|
| Window managers inside the fixture pool under the anchored survivor rule | 0 |
| Sockets in the fixture pool | 0 |
| Lock files in the fixture pool | 0 |
| PIDs killed | 0 |
| **Exe-anchored processes OUTSIDE the pool, deliberately NOT touched** | **2** |

**That last count is not zero, and that is the point.** Both are live desktop
sessions launched from this same build tree:

| PID | DISPLAY | Binary |
|---|---|---|
| 1541961 | `:10.0` | `build/debug/wm2-born-again` |
| 2346022 | `:11` | `build/release/wm2-born-again` |

Pid 1541961 passes the exe anchor. The display-pool condition is the only thing
that excludes it, and it did — verified directly against the capture tool, which
answered `refused pid 1541961: DISPLAY is outside the fixture display pool
:120-:199` and exited 3 without attaching.

Host conditions at the start of the run, from `measurement.log` lines 7–9:
load average 2.47 (1 min), 2084 MB free and 15972 MB available of 32012 MB.

## The budget, stated before the run

Written into `measurement.log` line 6 before the first fixture was constructed:

- **Mode S:** 200 fixtures, one post-readiness map each. Hard stop at 200 or
  600000 ms of wall clock, whichever came first.
- **Mode R:** 5 fixtures × 400 maps = 2000 maps. Hard stop at 2000 or 600000 ms,
  whichever came first.
- **At most 20 bundles per mode.** A trip beyond the cap increments a counter and
  writes no bundle.
- Diagnostic threshold 500 ms, strictly inside the hard 8000 ms reparent
  deadline. The deadline is a `constexpr` literal declared exactly once in
  `tests/test_wm_repro.cpp` and has no environment override of any spelling.

The budget was **not** extended, no third mode was added, and no sequence was
restarted.

## What the two modes are, and why both

**Mode S** is the startup-adjacent shape: a fresh fixture, exactly one client
mapped after it reports ready, then teardown. This is the shape both the
after-half's case #80 and the before-half's case #93 exhibited.

**Mode R** is the repeated-map shape: one fixture, many clients mapped, awaited
and destroyed in sequence inside it. Every mapped client traverses
`m_border->reparent()` → `activate()` → `timestamp()`, and `src/Events.cpp:21`
invalidates the cached time on every loop iteration, so every one of those
activations takes the cold-cache path.

Their counts are recorded **separately and are never pooled into one rate**.

## The observed counts

Verbatim, the two per-mode summary lines from `measurement.log`:

```
wm2-repro: mode-s fixtures-s=200 startfail-s=0 maps-s=200 trips-s=0 bundles-s=0 capped-s=0 unframed-s=0 wallms-s=36618
wm2-repro: mode-r fixtures-r=5 startfail-r=0 maps-r=2000 trips-r=5 bundles-r=5 capped-r=0 unframed-r=5 wallms-r=83206
```

| Reading | Mode S | Mode R |
|---|---|---|
| Fixtures constructed | 200 | 5 |
| Fixtures that never reached ready | 0 | 0 |
| Post-readiness maps | 200 | 2000 |
| Polls that crossed the 500 ms threshold | **0** | **5** |
| Bundles written | 0 | 5 |
| Trips beyond the 20-bundle cap | 0 | 0 |
| Clients that never framed inside 8000 ms | 0 | **5** |
| Wall clock consumed | 36618 ms | 83206 ms |

Neither mode exhausted its budget: Mode S ran all 200 fixtures in 37 s of its
600 s allowance, and Mode R ran all 2000 maps in 83 s of its own. `trips = bundles
+ capped` holds in both modes by construction, so a dropped write would be visible
in these figures rather than absorbed by them.

**Both modes' lines are printed unconditionally**, including Mode S's, which
observed nothing — a missing token would be a defect rather than a value of zero.

**Reading the S-versus-R split, carefully.** Mode R reproduced and Mode S did
not, at 2000 maps against 200. That is *consistent with* a per-activation
mechanism rather than a startup-adjacent one, but the two modes did not sample
equally (2000 maps versus 200) and one measurement of five events does not
establish a rate. It is recorded as an observation, not as an inference.

## The bundle table

Every trip in this measurement produced a bundle, and every bundle is readable in
all five channels.

| Bundle | Mode | Elapsed at trip | Eventually framed | WM alive | `wchan` | Frames | Topmost project frame |
|---|---|---|---|---|---|---|---|
| `trip-r-1` | r | 505 ms | no | yes (pid 68887) | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-2` | r | 504 ms | no | yes (pid 69033) | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-3` | r | 504 ms | no | yes (pid 69033) | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-4` | r | 504 ms | no | yes (pid 69518) | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-5` | r | 508 ms | no | yes (pid 69518) | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |

Every bundle records `proc-state: S` after the debugger detached, so the
instrument did not leave the window manager suspended and therefore did not
manufacture the condition it was measuring. `xvfb-log` reads
`ABSENT log file empty or not present` in every bundle: the Xvfb server wrote
nothing at all on these runs, which is an absence **with a stated reason**, not a
channel silently missing.

The five bundles came from **three distinct window-manager processes** (68887,
69033, 69518), so they are not five readings of one stuck process.

### The calibration reading, beside them

`evidence/gates/reproducer/control/` holds a forced capture taken against a
**healthy** window manager — one that framed its client in 20 ms — so that a
backtrace in the event loop's poll is legible as a comparison rather than as an
argument from absence.

Its topmost project frame is `WindowManager::nextEvent` (`src/Events.cpp:205`),
`wchan` reads `do_poll.constprop.0`, and its `poll` frame carries `timeout=-1`.

**That is the same shape the five trip bundles show.** A healthy idle window
manager and a window manager that has failed to reparent a client for 500 ms are,
at the level this capture reads, in the same place: blocked in the event loop's
own `poll` with no deadline armed. The calibration is what makes that statement a
reading rather than a guess.

## What the backtraces actually name

All five trip backtraces are identical in shape:

```
#0  __GI___poll (fds=..., nfds=2, timeout=-1) at ../sysdeps/unix/sysv/linux/poll.c:29
#1  WindowManager::nextEvent (...) at src/Events.cpp:205
#2  WindowManager::loop (...) at src/Events.cpp:20
#3  WindowManager::WindowManager (...) at src/Manager.cpp:295
#4  main (...) at src/main.cpp:9
```

`src/Events.cpp:205` is `poll(fds, 2, timeout)` inside `WindowManager::nextEvent()`
— the event loop's own idle wait — and `timeout=-1` means `computePollTimeout()`
reported no deadline armed. **No frame in any of the five names
`WindowManager::timestamp`, and none names `XMaskEvent`.**

Each bundle's `wm-stderr.txt` does carry
`wm2: timestamp: entering blocking property wait`, which is printed once per
process on the first blocked wait. So the branch *was* entered in each of these
window managers at some point in its life — but the backtrace says the process
was **not** in that wait at the moment of capture, and the once-per-process line
cannot distinguish a current wedge from an earlier harmless one. That is exactly
why the plan made the backtrace the discriminator and not the stderr line.

**One further observation, recorded and deliberately NOT attributed.** The
mode-R bundles' stderr carries a large volume of the window manager's own
`BadWindow` protocol-error lines (`X_UnmapWindow`, `X_GetProperty`,
`X_GetWindowAttributes`, `X_SetInputFocus` and others) from the create/await/
destroy churn this mode drives. Deferred item 13 already records that every
client destroy logs one `X_UnmapWindow` `BadWindow` because the resize handle is
a child of the client window. Whether any of this traffic bears on the unframed
clients is **not** established by this measurement, and nothing here should be
read as saying it does.

## The verdict criteria, decided in advance

These were written into this plan before the measurement ran, so the criterion
cannot be fitted to the result.

**A CONFIRMED-shaped reading** is a backtrace naming `WindowManager::timestamp`
with an Xlib event-wait frame at or above it.

**A REFUTED-contributing reading** is a backtrace in `WindowManager::nextEvent`,
or in an Xlib round trip reached from anywhere other than the timestamp path, or
in teardown, or a window manager already reaped.

**A trip with no readable backtrace on any channel** contributes to neither and is
counted separately. There were none.

**The aggregation rule:**

- **One CONFIRMED-shaped backtrace is sufficient for CONFIRMED**, because it is an
  existence claim.
- **REFUTED requires** that trips occurred, that at least **five** readable
  backtraces were captured, and that none was CONFIRMED-shaped.
- **Anything else — including zero trips — is INCONCLUSIVE.**

**The five-backtrace floor is a planner's choice, not a derived number.** Its
reason: refuting an intermittent mechanism on a single non-observation is the same
error as confirming one on a single observation. The operator may rule otherwise at
the checkpoint. Task 1's calibration capture is **not** a trip and does **not**
count toward the floor.

## The verdict

**Proposed verdict:** REFUTED

The reading that produced it: **five** trips occurred, **five** readable
backtraces were captured (frames=5 in every bundle, at least one project frame in
every bundle), and **none** was CONFIRMED-shaped. Every one of the five named
`WindowManager::nextEvent` at `src/Events.cpp:205`, which is a
REFUTED-contributing reading by the criteria above. The measurement lands exactly
on the five-backtrace floor — not above it — and that is stated plainly because
the floor was chosen in advance and the result meets it without margin.

Proposed, not final. The verdict of record is written only by the operator at this
plan's Task 3.

## What this does not establish

1. **This is not a gate rate.** It is a measurement of the reproducer's isolated
   post-readiness reparent sequence, not of the full suite. No figure here
   supersedes the 0.40 red-run rate plan 08.5-04 measured on the unswept host, and
   nothing here may be cited as a gate result.

2. **A trip is not by itself a wedge.** Crossing a 500 ms threshold on a loaded
   host can happen benignly. What raises these five above that objection is that
   all five clients *also* failed to frame inside the full 8000 ms deadline
   (`unframed-r=5`) — but the threshold crossing alone would not have.

3. **Every elapsed figure after a capture is perturbed by the capture.** A
   debugger attach suspends the observed process for the duration of the
   backtrace, so the "eventually framed" column is a liveness record, not a stall
   measurement.

4. **The 8.23 s and 8.35 s figures in the sibling records are censored
   observations**, not measured stalls. They are the tests' own 8000 ms
   reparent-poll deadlines expiring plus fixture startup, and they bound the true
   stall only from below. They never supported the cold-cache hypothesis.

5. **The `foreign=5 of cold=5` counter measures the swallowing mechanism's
   existence, not its impact.** `foreign` means only "not the window manager's own
   root append on `_WM2_RUNNING`", which also covers its own `_NET_CLIENT_LIST`
   and `_NET_ACTIVE_WINDOW` traffic that `eventProperty()` (`src/Events.cpp:535`)
   would ignore anyway.

6. **A refutation of this mechanism is not an attribution to another one.** These
   backtraces say where the window manager was; they do not say why a `MapRequest`
   it should have received did not reach it. Naming the actual failing path is the
   next round's work, not this one's.

## Screening applied before commit

- Credential screen over this directory —
  **no matches**. The pattern is the one this phase has used since plan 08.5-04
  and is written out verbatim in `08.5-09-PLAN.md` Task 2's `<verify>` block; it is
  deliberately **not** quoted here, because a record that spells its own screening
  alternation makes that screen match itself on the next run and the failure looks
  like a finding.
- Every backtrace file in this directory was **read by a human-facing review before
  it was committed.** These are the only artifacts in this phase produced by
  reading another process's memory, and only the arguments-only backtrace form was
  ever requested — no stack locals, no register dump, no memory read.
- **No content from `~/.xsession-errors` appears anywhere in these files**
  (T-8-XSESSION). That file was not read, copied or quoted at any point.
- These are ctest and debugger transcripts: **no window tree, no window titles and
  no host name.** The window manager's own diagnostic lines are fixed ASCII state
  words plus integers, and its protocol-error lines carry window ids in hex, never
  titles. The absolute repository path appears exactly as it does in Phase 8's own
  committed logs.

## Operator ruling — 2026-08-31

Taken at plan 08.5-09's Task 3 `checkpoint:decision`, `gate="blocking-human"`, on
the evidence presented above. **Every measured figure above this section is
untouched by this ruling.**

**Ruling: `refuted`.** The proposed verdict is accepted as the verdict of record
and written to the single `**Verdict:**` line in
`evidence/gates/attribution/README.md`, which now reads `REFUTED`.

**Sub-decision: none.** A sub-decision is carried only by an `inconclusive`
ruling, and under the precommitted terminal rule the extend/hold/re-plan
sub-decisions are withdrawn in any case.

**`08.5-07` disposition: `RE-PLAN`.** The operator chose explicitly to honour the
precommitted terminal rule of 2026-08-31 rather than override it.

**The operator's stated reason, recorded as ruled:**

1. Five trips occurred inside the stated budget — neither mode exhausted it, and
   there was no extension, no third mode and no restart. Five readable backtraces
   were captured and **none** was CONFIRMED-shaped. Every one names
   `WindowManager::nextEvent` at `src/Events.cpp:205` inside
   `poll(nfds=2, timeout=-1)` — the event loop's own idle wait with no deadline
   armed — under `wchan` `do_poll.constprop.0`. **No frame in any bundle names
   `WindowManager::timestamp` or `XMaskEvent`.** The window manager was idle, not
   wedged.

2. The calibration capture against a healthy window manager that framed in 20 ms
   has the **same shape**, which is what makes this a comparison rather than an
   argument from absence.

3. An additional reading, surfaced at the checkpoint and ruled on with the rest:
   the pair `timestamp: entering blocking property wait` /
   `property wait matched a foreign event` appears exactly **once in every
   bundle, including the healthy control**. The swallowing mechanism therefore
   fires identically in the healthy case and does not discriminate a stalled
   window manager from a healthy one. That corroborates the refutation rather
   than merely failing to confirm.

4. The five-backtrace floor is met **exactly, not with margin**, and across three
   independent window-manager processes (68887; 69033 twice; 69518 twice). The
   operator was shown this explicitly and ruled the floor satisfied.

5. `RE-PLAN` follows the precommitted terminal rule: **08.5-07 is NOT the flake
   fix and must not be recorded as one.** v1.0 stays blocked until the bundle
   table names the actual failing path, or ruling B is reversed as a NEW,
   separately recorded release-policy decision.

6. Lead for the next round, recorded but **not** attributed: `trip-r-3`,
   `trip-r-4` and `trip-r-5` each carry a `BadWindow` cascade —
   `X_CreateWindow`, `X_ReparentWindow`, `X_MapWindow` and `X_ChangeSaveSet` all
   failing on window `0x80003b`, i.e. the window manager framing a window that no
   longer exists. `trip-r-1` and `trip-r-2` have clean 12-line stderr and still
   failed to frame, so the cascade is present in **three of five** and is not the
   universal signature. A refutation of one mechanism is not an attribution to
   another.

Under the Negative-Result Contract in `08.5-VALIDATION.md` this refutation is
committed with the same weight as a confirmation. Withholding the attribution the
evidence does not support is the correct outcome of the method, not a failure of
the plan.
