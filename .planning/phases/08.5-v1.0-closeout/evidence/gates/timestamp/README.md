# 08.5-12 — the timestamp path: three defects, four cases

Established by **reading the source and reproducing deterministically**. No rate,
no sampling run, no p-value appears anywhere in this record or in the cases it
describes.

## Pins

| Pin | SHA | What it is |
| --- | --- | --- |
| `ROUND_BASE` | `7de1bdc4aa6328571f3be999796b75eb7ae5d8a4` | the commit before `08.5-11` began |
| `PLAN_11_TIP` | `97a00427f759594acc41197cc0f10ed750924858` | `08.5-11`'s code tip |
| `PLAN_12_T1` | `0d5dc07c7f79fac3ab426554a97292726c879fa5` | this plan's Task 1 tip, recorded in `PLAN-12-T1-TIP.txt` |

`PLAN_12_T1` exists because Task 1 changed `src/Manager.cpp` and added
`include/TimestampWait.h` deliberately. A no-production-diff gate for the cases
therefore cannot use `ROUND_BASE` or `PLAN_11_TIP`: against either of those it
would print Task 1's own legitimate work and fail on every run.

## Host

- `tomislav-HP-Z440-Workstation`, Linux 6.8.0-124-generic
- Xvfb `:99`, `1024x768x24`, `+render`, the arguments `CMakeLists.txt:185` uses
- Debug tree, `ctest -L '^eventloop$' --no-tests=error`

## The three defects

### 1. Fatal at startup

`src/Manager.cpp:923` sends the sentinel as a **zero-length `PropModeAppend`**
whose type is the `_WM2_RUNNING` atom itself and whose format is 8. Append
requires the *existing* property's type and format to match. `_WM2_RUNNING`
lives on the root window and root properties are owned by nobody, so any client
on the display may replace it with a different type — after which the request
fails with `BadMatch` and generates no `PropertyNotify` at all.

During initialisation that error does not slow the window manager down; it stops
it starting:

```
:923 request  ->  BadMatch
              ->  errorHandler()            src/Manager.cpp:397
              ->  m_initialising still true  src/Manager.cpp:416
              ->  std::exit(1)               src/Manager.cpp:418
```

The constructor calls `timestamp(true)` at `:288` and does not clear
`m_initialising` until `:290`, so the whole sentinel round trip happens inside
that window. **Bounding the wait does not touch this defect** — the wait is
never entered.

### 2. Unbounded at runtime

The same `BadMatch` after startup leaves nothing to wait for. The non-blocking
check at `:944` finds nothing and the blocking selector at `:959` waits **with
no deadline**, on the window manager's only thread. The self-pipe cannot
interrupt it, because a mask wait is not a poll.

### 3. Over-broad, always

Both selectors match **any** `PropertyNotify` on the connection — a mask selects
on event type and on nothing else — irrespective of window, atom, `send_event`
or `state`. Three consequences, all live:

- another client's notification is **consumed** here and never reaches
  `eventProperty()` (`src/Events.cpp:535`), so its title silently stops
  refreshing;
- a **synthetic** notification is accepted, so any client may supply a
  timestamp of its choosing;
- it fires **deterministically at startup**, not merely under contention:
  `initialiseScreen()` selects the property bit at `:603`,
  `setupEwmhProperties()` changes root properties at `:792`, and only afterwards
  does the constructor call `timestamp(true)` at `:288`. Those earlier changes
  precede the sentinel in request order, so the first matching event is reliably
  an EWMH notification rather than the sentinel.

A fix needs **four** field checks, not three. The sentinel is a zero-length
change and so generates `PropertyNewValue`; a predicate testing window, atom and
`send_event` but not `state` still accepts a `PropertyDelete` on the very same
window and atom.

## The four cases

All four drive `include/TimestampWait.h` — the real extracted production code
that `WindowManager::timestamp()` itself consults. None reimplements what it
tests.

| Case | What it asserts |
| --- | --- |
| `timestamp-survives-poisoned-property` | the sentinel request raises no protocol error against a root property a foreign client has replaced |
| `timestamp-leaves-foreign-property-alone` | a `PropertyDelete` on the sentinel's own window and atom, and a notification for a different atom, both survive the wait and remain retrievable |
| `timestamp-rejects-synthetic` | a `send_event` notification naming the root and the sentinel atom does not supply the timestamp |
| `timestamp-is-bounded` | the wait returns within its deadline when no sentinel can arrive |

`timestamp-is-bounded` runs the production wait in a **forked child**, bounded
and `SIGKILL`ed by the parent using the pid `fork()` returned, then reaped. A
thread cannot be cancelled inside Xlib without leaving the connection lock held,
and a detached one leaks a blocked thread into every later case. The test bounds
what production does not, and **that asymmetry is the defect**. A bounded
wrapper of the test's own construction was rejected deliberately: it would avoid
calling the defective production helper at all.

Every case removes `_WM2_RUNNING` from the root through a destructor, so cleanup
also runs on the path where a `REQUIRE` has thrown and after a killed child.
Root properties outlive the connection that set them; a case that left it
poisoned would break every later case and every later run on `:99`.

## RED at `PLAN_12_T1`

`ctest -L '^eventloop$' --no-tests=error` exits **8**, four of fifteen tests
failing. Full output in `red-before-fix.log`. The reported reasons:

| Case | Observed |
| --- | --- |
| `timestamp-survives-poisoned-property` | `protocol errors from the sentinel request: 1; last error code: 8` — `BadMatch` |
| `timestamp-leaves-foreign-property-alone` | `queued before the wait: 2; wait matched: 1; PropertyDelete survived: 0` |
| `timestamp-rejects-synthetic` | `wait matched: 1; send_event set on the collected event: 1; forged time accepted` |
| `timestamp-is-bounded` | `child observed for 3000 ms; killed: 1; exited cleanly: 0` |

Ten consecutive runs produced ten identical exit codes — `8 8 8 8 8 8 8 8 8 8` —
in 31 s total, no run approaching the 300 s outer bound. `_WM2_RUNNING` was
absent from the `:99` root after all ten.

Nothing under `src/`, `include/` or `CMakeLists.txt` differs from `PLAN_12_T1`:
the cases establish this RED result against untouched production.

## Relationship to round 3

Round 3 ruled on whether this path explained an **observed** reparent hang, on
the strength of five backtraces that named the event loop's own poll and not
this path, and ruled `REFUTED`. **That ruling stands, untouched, and this record
does not re-open, amend or qualify it.**

This round establishes a *different* claim by a *different* method: that the code
is wrong **on its own terms**, by source reading and deterministic reproduction.
Not-observed is not absent. Both statements are true and neither contradicts the
other.

## What this does not establish

This round measures **no rate**. It does not establish that the release gate is
reliable, and nothing here should be read as saying so.

The standing arithmetic bounds what a later green run may be taken to mean:
2-red-of-5 against 0-red-of-10 gives p ≈ 0.095; clearing 0.05 needs 0 red of 16;
detecting 0.40 → 0.20 at 80% power needs roughly 36 runs.
