# 08.5-11 — the event loop's readiness-ordering defect

Round base (`ROUND_BASE`): `7de1bdc4aa6328571f3be999796b75eb7ae5d8a4`
Task 1 tip (`PLAN_11_T1`): `e21be616c1b54650b53b18eee9015118912a71ed`
Code tip (`PLAN_11_TIP`): `97a00427f759594acc41197cc0f10ed750924858`

Host: Ubuntu 22.04.5 LTS, Linux 6.8.0-124-generic x86_64, libX11 2:1.7.5-1ubuntu0.3.

Every figure below is traceable to a committed log in this directory. Nothing
here is a rate, a sample or a statistical test.

## The mechanism

An ordering error, not a visibility error. Replaying `src/Events.cpp` as it
stood at `ROUND_BASE`:

1. `:195` tests `QLength(display())`. Empty, so the early-return branch is not
   taken.
2. `:201` calls `XFlush()`. On this libX11 the flush path polls the transport
   — `_XFlush` reaches `xcb_poll_for_event` / `xcb_poll_for_queued_event` — and
   **moves the pending event into `dpy->qlen` while draining the socket.**
3. The queue length is never re-tested.
4. `:205` calls `poll()` with `timeout = -1` whenever no focus timer is armed
   (`computePollTimeout()` returns `-1` in the ordinary idle case). The socket
   is empty. **Nothing will ever wake it.**

The four measured readings, from `wedge-probe.c` (committed in this directory,
output in `wedge-probe.log`):

```
line 195  -> QLength=0   (branch NOT taken)
          -> fd readable=1
line 201  -> XFlush() called
line 205  -> QLength=1, fd readable=0     *** WEDGE ***
```

The same reading is reproduced as a test case rather than a standalone probe:
in `red-before-fix.log`, `pump-reports-what-is-available` fails at
`tests/test_eventloop.cpp:171` with `reported > 0` expanding to `0 > 0`, while
the five assertions before it pass — including the independent availability
check `(reported > 0 || queuedAfter > 0)`. That combination is the whole
claim: the pump reported nothing pending **while the event was sitting in
Xlib's queue**.

## A correction of the earlier account

The earlier account held that events were stranded in libxcb's transport
queue, invisible to both `QLength()` and `poll()`. **That account was tested
and is false on this host**: a round trip transfers such events into Xlib's own
queue, where `QLength()` sees them. The defect needs no transport subtlety —
it is an ordering error between a predicate and a flush. The superseded
account is recorded here explicitly so that it is not cited again.

## Consistency with the three prior measurement rounds

This is a **consistency check, not independent confirmation**. The mechanism
above accounts for every reading the three earlier rounds produced:

- all five round-3 backtraces named `poll(nfds=2, timeout=-1)` at `:205` —
  that is where it wedges;
- the round-4 control clients never framed even at the hard 8000 ms deadline —
  nothing can wake that poll;
- the round-4 intervention clients framed in 20–45 ms — any delivered event
  makes the socket readable, and the queued backlog then drains oldest-first;
- `recvq` read 0 in all 40 bundles — `XFlush()` had drained the socket;
- the healthy calibration bundle showed the same frame as the trips — an idle
  window manager is legitimately in that same poll, which is exactly why no
  backtrace could separate the two.

## The fix

- The pump is `XPending()`, which is `XEventsQueued(QueuedAfterFlush)`:
  predicate and flush in **one** operation, so no window exists between them.
  The standalone `XFlush()` is gone, not reordered — kept after the pump it
  would move a later-arriving event into the queue between the predicate and
  the `poll()`, which is the identical race relocated.
- Nothing runs between a zero pump result and `poll()` except
  `computePollTimeout()`, which reads local state only.
- A readable X descriptor never yields deliver-an-event; the decision is to
  re-pump. Delivery now happens only through the pump.
- `POLLERR`, `POLLHUP` and `POLLNVAL` are handled for both descriptors and
  stop the loop with a `wm2: `-prefixed diagnostic, instead of matching no
  branch and re-polling at full CPU. `revents` is zeroed before each `poll()`.
- `m_signalled` is observed by the loop before the pump and again after it,
  independently of queue depth, and the menu Exit path writes the self-pipe so
  a blocked `poll()` wakes at once.

## Before and after

| Case | At `PLAN_11_T1` (`red-before-fix.log`) | At `PLAN_11_TIP` (`green-after-fix.log`) |
| --- | --- | --- |
| `pump-reports-what-is-available` | FAILED, `reported > 0` → `0 > 0` (`test_eventloop.cpp:171`) | passed |
| `pump-reports-zero-when-idle` | passed | passed |
| `readable-is-not-deliverable` | FAILED, `eventPumpDecide(state) != DeliverEvent` → `0 != 0` (`test_eventloop.cpp:259`) | passed |
| `exit-flag-stops-the-loop` | FAILED, `eventPumpDecide(state) == StopOnSignal` → `2 == 4` (`test_eventloop.cpp:282`) | passed |

`red-before-fix.log` records exit code 8 for the `eventloop` label with three
named failures; ten consecutive runs at that commit produced ten identical
exit codes of 8. `green-after-fix.log` records the full suite at 320/320
passing; ten consecutive runs of the `eventloop` label after the fix produced
ten identical exit codes of 0.

Menu Exit was additionally verified against the built binary rather than by a
token check: driving the root menu over XTEST and selecting the Exit row
terminated the window manager with status 0. At `ROUND_BASE` this could not
have worked — `m_signalled` had one definition, one declaration, two writes
and **no read anywhere in the tree**.

## What this does not establish

- **Not a gate rate.** The release gate's behaviour is a rate, and this round
  measures none. Nothing here licenses a claim about how often the suite is
  green.
- The standing arithmetic bounds what a later green run may mean: a
  2-red-of-5 baseline against 0-red-of-10 gives p ~ 0.095; clearing 0.05 needs
  0 red of 16; detecting 0.40 → 0.20 at 80% power needs roughly 36 runs.
- **The full suite is still not uniformly green run to run.** Recorded as
  counts, not as a rate, and not extrapolated: at `ROUND_BASE` two full-suite
  runs gave one clean run and one with four failures (a reparent-deadline
  timeout and three others), each of which passed when re-run alone. After the
  fix, three full-suite runs gave two clean runs and one with a single
  failure — `Menu background colour reaches the menu opened by a real root
  click`, whose sampled menu was unpainted — which then passed ten times out of
  ten when run alone. Whether this round changed that behaviour is exactly the
  question this round does not answer.
- This round neither re-opens nor amends the round-3 `REFUTED` finding, and it
  does not rule the open `08.5-10` Task 3 checkpoint.

## Deferred to `08.5-12`

The timestamp path: `WindowManager::timestamp()` in `src/Manager.cpp`
(around `:915-1000`), together with `m_currentTime` declared `int` rather than
`Time` in `include/Manager.h`. `src/Manager.cpp` is byte-identical against
`ROUND_BASE` across this whole round.

Everything else this round audited but did not fix is recorded in
`.planning/WINDOWS.md`, each entry carrying its concrete failing interleaving
and a destination.
