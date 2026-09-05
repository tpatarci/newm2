# The wake-up intervention — NOT the gate bundle

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

This record is also **not** the address of the previous round's verdict. That
verdict lives in `evidence/gates/attribution/README.md`, its single
`**Verdict:**` line still reads `REFUTED`, its single `**08.5-07 disposition:**`
line still reads `RE-PLAN`, and this round neither re-opens nor amends either.

## What this is

A **controlled two-arm intervention** on the post-readiness reparent sequence,
run once against a budget stated before it started.

Round 3 (`evidence/gates/reproducer/`) refuted the leading hypothesis on five
readable hang-time backtraces and **could not attribute**, for a reason it
recorded honestly: the healthy-window-manager calibration bundle shows the same
frame as the five trips. A backtrace cannot separate "idle with nothing to do"
from "idle while holding work it cannot see". A sixth passive channel would not
have changed that.

This round therefore changed the **kind** of evidence. At a trip the instrument
captures the bundle exactly as before, and then, on alternating trips, delivers
to the window manager **one event its own handler treats as a no-op** — a
property change on the root window. The other half of the trips get the
identical capture and no event. The arms are compared to each other, inside one
run, and never to a figure from another round.

Captured at commit `7cb1b39bdd781312c6712bb233a1f096220436c6` on branch `main`,
2026-08-31, against a source tree with **no uncommitted modifications** under
`src/`, `include/`, `tests/`, `scripts/` or `CMakeLists.txt`. That statement is
a reading rather than a claim: the emptiness of
`git status --porcelain -- src include tests scripts CMakeLists.txt` was written
into `measurement.log` by the run itself, at line 5.

Every figure below traces to a line in `measurement.log`, which is the run's
whole output and is committed beside this file.

## The capability reading

Verbatim from `bash scripts/diag/wm-stack-capture.sh --probe`, recorded into
`measurement.log` immediately before the run — four lines this round, the three
carried forward plus the socket-statistics tool:

```
gdb: PRESENT /usr/bin/gdb GNU gdb (Ubuntu 12.1-0ubuntu1~22.04.2) 12.1
ptrace-scope: PRESENT 1
wchan: PRESENT self=0
ss: PRESENT /usr/bin/ss ss utility, iproute2-5.15.0
```

All four channels the capture depends on were available. `ptrace_scope` reading
`1` is Yama's restricted mode, under which the attach is possible only because
the fixture's forked window-manager child declares the test process as its
permitted tracer. Nothing was installed by this round: both tools are probed for
and recorded PRESENT or ABSENT, never installed, and either absence would have
degraded to a recorded absence rather than a red suite.

## The host sweep

Under the anchored survivor rule: a candidate window manager is a process whose
`/proc/<pid>/exe`, with a trailing ` (deleted)` stripped, equals exactly
`$R/build/{debug,asan,release}/wm2-born-again` **and** whose `DISPLAY` lies
inside the fixture's reserved `:120`–`:199` pool. Both conditions, never either
alone. No process was selected by a process-name pattern at any point, and the
capture tool takes its PID as an explicit argument.

| Reading | Before the run |
|---|---|
| Window managers inside the fixture pool under the anchored survivor rule | 0 |
| PIDs killed | 0 |
| **Exe-anchored processes OUTSIDE the pool, deliberately NOT touched** | **0** |

That last count was **2** in round 3 — two live desktop sessions launched from
this same build tree, which the display-pool condition and nothing else
excluded. Both had exited before this run, so the count is honestly zero here
rather than demonstrating the exclusion again. The exclusion itself is unchanged
and is still the only thing standing between the capture tool and a live desktop
window manager built from this tree.

Host conditions at the start of the run, from `measurement.log` lines 7–9: load
average 3.43 (1 min), 777 MB free and 7276 MB available of 32012 MB.

## The hypothesis under test, stated as a mechanism with its citations

Offered so a reader can **check** it rather than take it. Each clause was
re-verified against the tree at the measurement commit.

1. `WindowManager::nextEvent()` decides whether work is waiting by consulting
   Xlib's own queue length at **`src/Events.cpp:195`** (`if (QLength(display()) > 0)`).
2. If that reads zero it flushes output — **`src/Events.cpp:201`**, which only
   *writes* — and blocks in **`src/Events.cpp:205`** (`poll(fds, 2, timeout)`).
3. The call that transfers events the transport has **already read off the
   socket** into Xlib's queue appears **nowhere in this tree**: a search for
   `XPending` and `XEventsQueued` across `src/` and `include/` returns nothing.
4. On this host `libX11.so.6` is linked against `libxcb.so.1` — checked, not
   assumed — so the transport genuinely maintains a queue of its own.

Every framing operation issues replies, and a read that satisfies one reply can
carry further events into the transport's queue in the same burst. Those events
are off the socket, so `poll()` never reports the descriptor readable for them;
and they are not in Xlib's queue, so the length check reads zero. The window
manager blocks with no deadline armed while holding undelivered work.

**The intervention.** A property change on the root window. Delivery is certain:
the root event mask carries `PropertyChangeMask` — the first term on
**`src/Manager.cpp:605`**, inside the mask assignment spanning
`src/Manager.cpp:603-605`. The handler does nothing with it:
`WindowManager::eventProperty()` (**`src/Events.cpp:535`**) resolves the event's
window to a client and returns when there is none, and the root window is never
a managed client. The event therefore adds **bytes** to the connection and adds
no **work**.

## The budget, stated before the run

Written into `measurement.log` line 6 before the first fixture was constructed:

- **Mode S:** 200 fixtures, one post-readiness map each. Hard stop at 200 or
  600000 ms of wall clock, whichever came first.
- **Mode R:** 8 fixtures × 1000 maps = 8000 maps. Hard stop at 8000 or 1500000
  ms, whichever came first.
- **At most 40 bundles per mode.** A trip beyond the cap still takes its arm and
  is still counted; only the bundle is declined.
- Diagnostic threshold **500 ms**, wake window **750 ms**, both strictly inside
  the hard 8000 ms reparent deadline and summing to well under it. The deadline
  is a `constexpr` literal declared exactly once in `tests/test_wm_repro.cpp`
  and has no environment override of any spelling.

The budget was **not** extended, no mode was added, and no sequence was
restarted. The bounded instrument repair was **not** used: no `unreadable-*`
directory exists and no `sequence=2` marker appears in the transcript.

**Mode R was sized at four times round 3's map count because that round observed
five trips in 2000 maps; at the same rate this budget was expected to produce
roughly twenty trips, about ten per arm.** That was an expectation used to
**size a budget** and is **not** a prediction of what the run would find — the
run in fact observed 129 trips, six times the expectation. The arm floor below,
not the expectation, is what decides whether the sample was adequate. The
figures reported are the actual counts.

**Mode S was kept rather than dropped**, even though it observed nothing in
round 3: 200 fixtures cost 37 s there and 38 s here, so retaining it is nearly
free, it keeps this record comparable to the previous one, and it leaves a
startup-adjacent trip observable if one occurs.

## The observed counts

Verbatim, the four per-mode summary lines from `measurement.log`:

```
wm2-repro: mode-s fixtures-s=200 startfail-s=0 maps-s=200 trips-s=0 bundles-s=0 capped-s=0 unframed-s=0 wallms-s=38050
wm2-repro: arm-s poke-s=0 pokewoke-s=0 pokeunframed-s=0 ctl-s=0 ctlwoke-s=0 ctlunframed-s=0
wm2-repro: mode-r fixtures-r=8 startfail-r=0 maps-r=8000 trips-r=129 bundles-r=40 capped-r=89 unframed-r=64 wallms-r=741405
wm2-repro: arm-r poke-r=65 pokewoke-r=65 pokeunframed-r=0 ctl-r=64 ctlwoke-r=0 ctlunframed-r=64
```

| Reading | Mode S | Mode R |
|---|---|---|
| Fixtures constructed | 200 | 8 |
| Fixtures that never reached ready | 0 | 0 |
| Post-readiness maps | 200 | 8000 |
| Polls that crossed the 500 ms threshold | **0** | **129** |
| Bundles written | 0 | 40 |
| Trips beyond the 40-bundle cap | 0 | 89 |
| Clients that never framed inside 8000 ms | 0 | **64** |
| Wall clock consumed | 38050 ms | 741405 ms |

Neither mode exhausted its wall-clock allowance: Mode S ran all 200 fixtures in
38 s of its 600 s, and Mode R ran all 8000 maps in 741 s of its 1500 s. Mode R
consumed its full map allocation; the hard stop that bound was the map count,
not the clock.

`trips-<m> == bundles-<m> + capped-<m>` and `trips-<m> == poke-<m> + ctl-<m>`
both hold in both modes, so a dropped write or a mis-assigned arm would be
visible in these figures rather than absorbed by them.

**Both modes' lines are printed unconditionally**, including Mode S's, which
observed nothing — a missing token would be a defect rather than a value of
zero. There is no mode-agnostic spelling of any countable key anywhere in the
transcript.

**`unframed-r=64` is NOT comparable to round 3's `unframed-r=5`, and the reason
must be read here rather than in a footnote: on half the trips this instrument
now intervenes.** The comparable figure is the do-nothing arm's own never-framed
count, `ctlunframed-r=64` — and it happens to be the same number, because every
client that never framed was on the control arm. A reader who compared 64
against 5 without this sentence would be reading a fourfold increase in sampling
as a degradation of the window manager.

**Reading the S-versus-R split, carefully.** Mode R reproduced and Mode S did
not, at 8000 maps against 200. That is *consistent with* a per-activation
mechanism rather than a startup-adjacent one, but the two modes did not sample
equally. It is recorded as an observation, not as an inference.

## The arm table

The 2×2 of arm against framed-inside-the-wake-window, **Mode R**. Mode S
recorded no trips and therefore contributes no table.

**Table A — all 129 trips (the full arm counts, including the 89 trips past the
bundle cap):**

| Arm | Framed inside the 750 ms wake window | Did not |
|---|---|---|
| **poke** (intervention delivered) | **65** | 0 |
| **control** (nothing delivered) | 0 | **64** |

Perfect separation. The exact one-sided test on balanced-by-construction arms
under perfect separation returns `1 / C(n_poke + n_control, n_poke)`; here that
is `1 / C(129, 65) ≈ 2.1 × 10⁻³⁸`, far below the pre-registered `p < 0.05`.

**Table B — the 40 bundled trips only, where the intervention's confirmation is
individually readable:**

| Arm | Framed inside the 750 ms wake window | Did not |
|---|---|---|
| **poke** (intervention confirmed issued, 20 of 20) | **20** | 0 |
| **control** (nothing delivered) | 0 | **20** |

Also perfectly separated; `1 / C(40, 20) ≈ 7.3 × 10⁻¹²`.

**Why both tables are given, and which one criterion 2 turns on.** The
pre-registered criterion requires that *every intervening-arm trip counted
records the intervention as confirmed issued*. That confirmation is written into
a bundle, and only 40 of the 129 trips wrote one — the other 89 were past the
per-mode bundle cap, took their arm and were counted, but declined a bundle.
**Table B is therefore the criterion-conformant table**: all 20 of its
intervening-arm trips record `poke-issued=yes`, and none is excluded. Table A is
the wider reading and is reported beside it rather than in place of it. Both
clear the floor of four per arm and both clear the test by many orders of
magnitude, so the outcome does not turn on the choice — but the record states
which is which rather than letting a reader assume.

**This is a test of an intervention against a control within one run, not a
comparison of two runs.** That is what distinguishes it from the rate comparison
this phase already had to correct once, and it is also its limit: it names a
failing **path** and it cannot produce a rate.

The separation is not merely categorical. Every intervening-arm bundle records
the client framing in **20–45 ms** after the event was delivered; every
control-arm bundle records the full **750–769 ms** wake window elapsing with no
frame, and every one of those clients then went on to miss the 8000 ms deadline
entirely.

## The criteria, decided in advance

Written into `08.5-10-PLAN.md` before the measurement ran, and reproduced here
word for word so the criterion cannot be fitted to the result.

`ATTRIBUTED` requires **all** of the following, in a **single mode**, with the
mode named:

1. At least **4** trips on the intervening arm **and** at least **4** on the
   do-nothing arm in that mode. **The floor is the derived minimum and the
   arithmetic it comes from is stated so a reader can check it.** Under perfect
   separation on balanced arms of size `n`, the exact one-sided test returns
   `1 / C(2n, n)`: at three per arm that is `1/20 = 0.050`, which does **not**
   clear `p < 0.05`; at four per arm it is `1/70 ≈ 0.014`, which does. Four is
   the smallest balanced table that can reach the stated threshold at all.
2. Every intervening-arm trip counted records the intervention as confirmed
   issued. A trip whose intervention did not leave the test process is excluded
   from the table and counted separately, never silently included.
3. An exact one-sided test on that mode's 2×2 table — arm against
   framed-inside-the-wake-window — gives **p < 0.05**, with the table and the
   p-value written into the record.

**The floor stood at six in an earlier draft of the plan and was lowered
deliberately**, recorded as a correction rather than silently changed. Six was a
planner's choice two steps above the reachable minimum, justified by the claim
that six *was* that minimum; that claim was arithmetically false. Five per arm
with perfect separation gives `1 / C(10,5) ≈ 0.004`, clearing the pre-registered
test by roughly a factor of twelve, and a floor of six would have routed that
table to `NOT-ATTRIBUTED` under a terminal rule. **What lowering it costs:** at
exactly four per arm only perfect separation clears — 3 of 4 against 0 of 4
gives `5/70 ≈ 0.071` and fails — so at the floor it is the test doing the work
and not the floor, and an `ATTRIBUTED` ruling could rest on as few as eight
paired observations. *This measurement does not rest on the floor:* it rests on
**40 paired observations** in the criterion-conformant table and **129** in the
wider one.

**Multiplicity, stated rather than left to be noticed.** The criteria are met
*in a single mode* and there are **two** eligible modes, so the rule permits two
shots at the threshold. At the floor each shot is about `0.014`, so the
family-wise figure is bounded by their sum, about **`0.029`** — still under the
stated `p < 0.05`. **No threshold and no criterion changes because of this.**

`NOT-ATTRIBUTED` is **everything else**, and explicitly covers: an inert
intervention; arms that do not separate; separation in the opposite direction;
fewer than four trips on either arm; zero trips inside the budget; and an
instrument that could not read its own channels.

### The instrument-failure partition

Every reachable instrument state has exactly one destination, and **no state
routes to "measure again" without a stated bound**:

| Instrument state | Destination | Terminal at the ruling? |
|---|---|---|
| Bundles written; one or more channels read `ABSENT` with a stated reason | A **reading**. Committed as written; the sampling case does not redden on it. If the absences leave the arm comparison unreadable, the proposed outcome is `NOT-ATTRIBUTED` with the reading named as *channels absent*. | Yes |
| Trips recorded and **no bundle written at all** in a mode, or a bundle whose capability file is missing, unparseable, or not carrying exactly eight verdict lines | A **defect in this round's own writer**. The sampling case reddens. One bounded repair of the bundle-writing path only — no change to budget, arm rule, knobs, threshold or wake window — capped at a single repeat. | No — one bounded repair |
| The bounded repair is exhausted: the second sequence lands in **either disjunct** of the row above | A **reading** again. Both sequences' bundles are quarantined out of the `trip-` name space as `unreadable-<m>-<N>`, the record carries `**Instrument failure:** EXHAUSTED-AFTER-SECOND-SEQUENCE`, and the proposed outcome is `NOT-ATTRIBUTED` whatever the other mode read. | Yes |

**Neither the repair nor the escape was reached in this run.** All 40 bundles
carry exactly eight verdict lines, no channel is absent in any of them, no
`unreadable-*` directory exists and the transcript carries only `sequence=1`.

## The bundle table

Every one of the 40 bundles is readable in all eight channels, and every channel
is PRESENT with content — no absences occurred in this run.

| Bundle | Mode | Arm | Elapsed at trip | Intervention confirmed issued | Framed inside wake window | Eventually framed by the deadline | `poll-timeout` | `socket-recvq` | `wchan` | Frames | Topmost project frame |
|---|---|---|---|---|---|---|---|---|---|---|---|
| `trip-r-1` | r | poke | 505 ms | yes | yes, 20 ms | yes | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-2` | r | control | 504 ms | no | no, 766 ms | no | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-3` | r | poke | 505 ms | yes | yes, 20 ms | yes | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-4` | r | control | 504 ms | no | no, 766 ms | no | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-5` | r | poke | 504 ms | yes | yes, 20 ms | yes | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-6` | r | control | 504 ms | no | no, 767 ms | no | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-7` | r | poke | 505 ms | yes | yes, 20 ms | yes | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-8` | r | control | 506 ms | no | no, 758 ms | no | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-9` | r | poke | 508 ms | yes | yes, 20 ms | yes | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-10` | r | control | 508 ms | no | no, 751 ms | no | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-11` | r | poke | 508 ms | yes | yes, 45 ms | yes | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-12` | r | control | 511 ms | no | no, 768 ms | no | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-13` | r | poke | 504 ms | yes | yes, 40 ms | yes | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-14` | r | control | 500 ms | no | no, 768 ms | no | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-15` | r | poke | 510 ms | yes | yes, 20 ms | yes | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-16` | r | control | 504 ms | no | no, 750 ms | no | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-17` | r | poke | 517 ms | yes | yes, 20 ms | yes | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-18` | r | control | 506 ms | no | no, 751 ms | no | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-19` | r | poke | 505 ms | yes | yes, 20 ms | yes | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-20` | r | control | 504 ms | no | no, 750 ms | no | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-21` | r | poke | 508 ms | yes | yes, 20 ms | yes | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-22` | r | control | 510 ms | no | no, 768 ms | no | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-23` | r | poke | 504 ms | yes | yes, 20 ms | yes | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-24` | r | control | 507 ms | no | no, 767 ms | no | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-25` | r | poke | 506 ms | yes | yes, 20 ms | yes | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-26` | r | control | 515 ms | no | no, 752 ms | no | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-27` | r | poke | 504 ms | yes | yes, 20 ms | yes | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-28` | r | control | 504 ms | no | no, 750 ms | no | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-29` | r | poke | 504 ms | yes | yes, 20 ms | yes | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-30` | r | control | 504 ms | no | no, 766 ms | no | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-31` | r | poke | 504 ms | yes | yes, 23 ms | yes | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-32` | r | control | 505 ms | no | no, 766 ms | no | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-33` | r | poke | 504 ms | yes | yes, 20 ms | yes | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-34` | r | control | 503 ms | no | no, 767 ms | no | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-35` | r | poke | 504 ms | yes | yes, 20 ms | yes | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-36` | r | control | 504 ms | no | no, 768 ms | no | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-37` | r | poke | 506 ms | yes | yes, 20 ms | yes | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-38` | r | control | 504 ms | no | no, 766 ms | no | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-39` | r | poke | 504 ms | yes | yes, 20 ms | yes | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |
| `trip-r-40` | r | control | 504 ms | no | no, 769 ms | no | -1 | sockets=1 recvq=0 max=0 | `do_poll.constprop.0` | 5 | `WindowManager::nextEvent` (`src/Events.cpp:205`) |

The 40 bundles came from **three distinct window-manager processes** (507142,
507692, 508513), so they are not forty readings of one stuck process. Every
bundle records `proc-state: S` after the debugger detached, so the instrument
did not leave any window manager suspended and therefore did not manufacture the
condition it was measuring.

### The calibration bundle, beside them

`evidence/gates/wakeup/control/` holds a forced capture and a forced
intervention taken against a **healthy** window manager — one that framed its
client in 20 ms.

| Reading | Calibration (`control/`) | Round 3's calibration | The 40 trip bundles |
|---|---|---|---|
| Topmost project frame | `WindowManager::nextEvent` (`src/Events.cpp:205`) | `WindowManager::nextEvent` (`src/Events.cpp:205`) | `WindowManager::nextEvent` (`src/Events.cpp:205`) — all 40 |
| `wchan` | `do_poll.constprop.0` | `do_poll.constprop.0` | `do_poll.constprop.0` — all 40 |
| `poll-timeout` | `-1` | not extracted that round; its `poll` frame carried `timeout=-1` | `-1` — all 40 |
| `socket-recvq` | `sockets=1 recvq=0 max=0` | channel did not exist | `sockets=1 recvq=0 max=0` — all 40 |
| `wake-arm` | `arm=poke poke-issued=yes framed-after=yes ms=0` | channel did not exist | see the table above |

**Every passive channel reads the same in the healthy case and in the stalled
case.** That is exactly the wall round 3 hit, reproduced here on three channels
instead of one, and it is why this round intervened. The one channel that
separates them is the arm.

## The four pre-registered corroborating predictions

Registered in `08.5-10-PLAN.md` **before** the run. **These are corroboration
and explicitly NOT criteria** — the outcome turns on the arm comparison and on
nothing else. Each outcome is recorded, and a failing prediction would be
recorded just as plainly.

**P1 — intervening-arm trips frame inside the wake window and do-nothing-arm
trips do not.** *Outcome: held, without exception.* 65 of 65 and 0 of 64
(Table A); 20 of 20 and 0 of 20 among the bundled trips. This prediction is also
the criterion, so it carries no independent corroborative weight — it is listed
for completeness rather than as support.

**P2 — a do-nothing-arm trip's teardown is followed by a framing cascade of
protocol errors on that window in the next bundle's stderr, while an
intervening-arm trip that frames adds none.** *Outcome: held, and it is the most
informative reading in this record.* Within a single window-manager process the
`X_ReparentWindow` `BadWindow` count rises by exactly **one after each
control-arm trip and by exactly zero after each intervening-arm trip**. Within
pid 508513 the count runs `0, 0, 1, 1, 2, 2, 3, 3, …, 14, 14` across
`trip-r-11` … `trip-r-40` — fifteen consecutive arm pairs, fifteen increments,
each following a control trip. The cascade shape is the one round 3 recorded as
an unattributed lead:

```
wm2: X_CreateWindow (0x8000e4): BadWindow (invalid Window parameter)
wm2: X_ReparentWindow (0x8000e4): BadWindow (invalid Window parameter)
wm2: X_ChangeSaveSet (0x8000e4): BadWindow (invalid Window parameter)
wm2: X_MapWindow (0x8000e4): BadWindow (invalid Window parameter)
```

Round 3 recorded that cascade and refused to attribute it, correctly: it was
present in three of five bundles and was not the universal signature.
**Predicting it in advance, and then observing it appear once per control trip
and never after an intervention, is what turns it from a story that fits into
evidence.** Under the account, the sampler destroys the unframed client when the
deadline expires, the destruction puts new bytes on the connection, the poll
returns, the backlog drains oldest-first, and the window manager frames a window
that no longer exists.

**P3 — the socket receive-queue reading is zero at every trip.** *Outcome:
held.* `recvq=0 max=0` in all 40 bundles and in the calibration. What this
excludes is the rival explanation in which bytes sit unread on the connection
while the poll fails to report the descriptor readable — a defect in the
descriptor set or the returned-events handling. What it does **not** do is
distinguish "bytes already consumed by the transport" from "never sent"; both
read zero, and the channel's own comment in the capture tool says so.

**P4 — the poll-timeout reading is negative at every trip, meaning no deadline
was armed.** *Outcome: held.* `-1` in all 40 bundles and in the calibration.

**One further reading, available free from round 3's committed bundles and
checked here.** Two of round 3's five trips carried a clean stderr with no
bad-map-request line, so the map request had not been dequeued and rejected.
**No bundle in this round carries such a line either** — a search for
`bad map request` across all 40 `wm-stderr.txt` files returns zero. Had any trip
carried one, the account under test would be wrong for that trip and this
section would say so.

## Proposed outcome

**Proposed outcome:** ATTRIBUTED

The reading that produced it, in Mode R:

1. **129 trips occurred inside a budget stated before the run** — the budget was
   not extended, no mode was added and no sequence was restarted — of which 65
   took the intervening arm and 64 the do-nothing arm, far above the
   pre-registered floor of four per arm.
2. **Every intervening-arm trip that wrote a bundle records the intervention as
   confirmed issued** (20 of 20). The 89 trips past the bundle cap took their
   arm and were counted but wrote no bundle, so the criterion-conformant table
   is the 40-bundle Table B; it is perfectly separated at `p ≈ 7.3 × 10⁻¹²`.
3. **Perfect separation in both tables.** Every intervening-arm client framed
   within 20–45 ms of the no-op event; every do-nothing-arm client sat through
   the full 750 ms window unframed and then missed the 8000 ms deadline
   entirely. The exact one-sided test gives `p ≈ 2.1 × 10⁻³⁸` over all 129 trips
   and `p ≈ 7.3 × 10⁻¹²` over the 40 bundled ones, against a threshold of 0.05.
4. **The failing path this names:** the window manager was holding an undelivered
   map request that neither of its readiness checks could see — the Xlib
   queue-length read at `src/Events.cpp:195` and the connection `poll()` at
   `src/Events.cpp:205` — and one no-op event on the connection was enough to
   release it. All three corroborating predictions that carry independent weight
   (P2, P3, P4) held.

Proposed, not final. The outcome of record is written only by the operator at
this plan's Task 3.

## What this does not establish

1. **This is not a gate rate.** It is a measurement of the reproducer's isolated
   post-readiness reparent sequence, not of the full suite. No figure here
   supersedes the 0.40 red-run rate plan 08.5-04 measured, and nothing here may
   be cited as a gate result.

2. **A trip is not by itself a wedge.** Crossing a 500 ms threshold on a loaded
   host can happen benignly. What raises these above that objection is the arm
   comparison, not the threshold crossing.

3. **Everything after a capture is perturbed by the capture**, which is exactly
   why both arms take it. A debugger attach suspends the observed process and
   its return interrupts a blocked poll (`src/Events.cpp:210` resumes the loop
   and re-checks the queue), so the attach is a genuine rival cause of any
   subsequent wake — made common to both arms rather than confounded with the
   intervention. The control arm's 64 clients received that same attach and did
   not frame, which is what rules the attach out on its own.

4. **An intervention names a failing PATH and never a rate.** Nothing here says
   how often that path reddens the release gate.

5. **It shows an event was undelivered; it does not identify which layer held
   it.** The account under test puts the event in the transport's own queue, and
   the socket reading is consistent with that — but consistent is not the same
   as demonstrated, and a fix must be justified on the readiness check itself
   rather than on a layer diagnosis this evidence does not carry.

6. **The 8.23 s and 8.35 s figures in the sibling records remain censored
   observations** of the harness's own 8000 ms deadline expiring, not measured
   stalls. They bound the true stall only from below.

7. **A refutation of one mechanism is not an attribution to another.** Round 3's
   `REFUTED` verdict on the timestamp path stands on its own evidence and is
   untouched by this round. Nothing here records plan `08.5-07` as the flake
   fix; hardening the timestamp path on its own merits remains available and is
   a different claim.

8. **The 89 capped trips took their arm but no capture.** Only the 40 bundled
   trips received a debugger attach. Because the arm alternates strictly by trip
   index parity, capping affects both arms almost equally (45 poke, 44 control
   uncaptured), so this does not bias the comparison — but Table A's arms are
   not identical in instrumentation to each other's captured subsets, which is
   the reason Table B is reported separately.

## Screening applied before commit

- Credential screen over this directory — **no matches**. The pattern is the one
  this phase has used since plan 08.5-04 and is written out verbatim in
  `08.5-10-PLAN.md` Task 2's `<verify>` block; it is deliberately **not** quoted
  here, because a record that spells its own screening alternation makes that
  screen match itself on the next run and the failure looks like a finding.
- Every backtrace file and every socket line in this directory was **read before
  it was committed.** These are the only artifacts in the round produced by
  observing another process. Only the arguments-only backtrace form was ever
  requested — no stack locals, no register dump, no memory read, no expression
  evaluated inside the observed process.
- The socket channel writes a socket count and integer queue depths and nothing
  else. The host's unix socket table was read into the capture tool and never
  written out; no address, path, peer or row belonging to any other process
  appears in any bundle.
- **No content from `~/.xsession-errors` appears anywhere in these files**
  (T-8-XSESSION). That file was not read, copied or quoted at any point.
- These are ctest and debugger transcripts: **no window tree, no window titles
  and no host name.** The window manager's own diagnostic lines are fixed ASCII
  state words plus integers, and its protocol-error lines carry window ids in
  hex, never titles. The absolute repository path appears exactly as it does in
  this project's other committed logs.

## Ruling — 2026-09-05

**Attribution outcome:** ATTRIBUTED
**Terminal disposition:** FIX-PLAN

Taken on 2026-09-05 by the delegated project lead under the operator's standing
instruction of the same date to finish every planned item without stopping for
per-item confirmation. The operator did not type the word; that fact is recorded
here so it can be reversed as a recorded decision rather than discovered.

**Reason, recorded verbatim.** The measurement meets the three pre-registered
criteria in Mode R on the criterion-conformant table: 20 confirmed-issued
intervening-arm trips against 20 do-nothing-arm trips, perfectly separated,
exact one-sided p = 1/C(40,20) ≈ 7.3e-12 against the 0.05 threshold and the
floor of four per arm. The path it names — the readiness check before the
blocking poll at `src/Events.cpp:195` and `:205` — was subsequently reproduced
deterministically by `08.5-11` in a forty-line probe (`evidence/gates/eventloop/
wedge-probe.c`), which also corrected the layer diagnosis this record declined to
make: the undelivered event sat in Xlib's own queue after `XFlush()`, not in the
transport's. `08.5-11` is the FIX-PLAN this disposition binds the chain to; it
has been executed, its four cases went RED at its Task 1 tip and GREEN after its
Task 3, and its record is at `evidence/gates/eventloop/README.md`.

**Limits carried forward, not softened.** The adversarial review taken before
this ruling stands as read: the alternating-parity arm assignment is not a
randomisation, so the exact test's p-value is a description of the separation
rather than a randomisation inference; the family-wise bound stated above is not
supported by an independence argument; 89 of 129 trips have no per-trip
confirmation of delivery and are excluded from the ruled table; and one sentence
in the channel-absence discussion above was found factually wrong and is
corrected by this note rather than edited in place. None of these changes the
reading that produced the ruling — twenty confirmed interventions each released
a client the paired control did not — and the deterministic reproduction, not
the p-value, is what the fix rests on. This ruling names a path and never a
rate.
