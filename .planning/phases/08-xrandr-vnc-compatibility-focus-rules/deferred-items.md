# Deferred / Out-of-Scope Discoveries — Phase 08

## 1. Legacy `start_xvfb` ctest fixture hangs (PRE-EXISTING)

**Found during:** 08-01 Task 1, running the full `ctest --test-dir build/debug` suite.

**Symptom:** `ctest -R '^start_xvfb$'` never returns (reproduced in isolation,
exit 124 under a 45s timeout). All behavioral tests actually pass; ctest itself
blocks after "End testing".

**Cause:** `CMakeLists.txt` `start_xvfb` runs
`bash -c "Xvfb :99 ... & echo $! > pidfile && sleep 1"`. The backgrounded Xvfb
inherits ctest's stdout/stderr pipe and never closes it, so ctest waits forever
on the pipe even though the test command itself exited.

**Pre-existing:** yes — `git diff CMakeLists.txt` shows no changes to the
`start_xvfb` / `stop_xvfb` / `XVFB_PID_FILE` lines. 08-01 only appended the
tags-as-labels option to `catch_discover_tests()` calls.

**Impact:** any ctest invocation selecting a test that requires the
`xvfb_display` fixture (`test_smoke`, `test_client`, `test_xft_poc`,
`test_ewmh`) hangs. `test_wm_process` is unaffected — it owns its own display.

**Disposition:** blocks the 08-01 Task 3 acceptance criterion
`ctest -L '^(wm_process|ewmh|client)$' -j4`, so it is fixed under deviation
Rule 3 in Task 3 rather than deferred further. Recorded here because it was
discovered during Task 1 and is not caused by this phase.

## 2. clang-tidy families reported but not enforced (08-02 Task 2)

**Found during:** 08-02 Task 2, standing up the static-analysis gate.

The plan named five clang-tidy families for the fatal allowlist. Three of them
already fire on the current tree, and the same plan forbids editing production
code into compliance while requiring the gate to be green today. They are
therefore configured as reported-only in `.clang-tidy`, with the promotion path
documented in that file.

| Family | Count | Where | Note |
|---|---|---|---|
| `cppcoreguidelines-init-variables` | 140 | src/ + tests/ | Overwhelmingly `XGetWindowProperty` out-parameters (`actualType`, `actualFormat`, `nItems`, `bytesAfter`), uninitialised by design because Xlib fills them. Enforcing this would require a project-wide convention change, not a bug fix. |
| `bugprone-branch-clone` | 6 | `src/Buttons.cpp` x4 (lines 220, 242, 453, 475), `src/Client.cpp` x2 (line 1362 "repeated branch in conditional chain", line 1415 "switch has 2 consecutive identical branches" -- the latter was missed by 08-02's count and re-measured at that plan's own commit in 08-03, so 6 is pre-existing, not a regression) | **The only entries here that may be real defects.** "Repeated branch in conditional chain" in the menu/geometry code. Not investigated in 08-02 (out of scope: this plan builds the gate, it does not fix findings). Worth a look during the focus/rules work that touches `Buttons.cpp`. |
| `bugprone-use-after-move` | 3 | `tests/test_eventloop.cpp` x2, `tests/test_raii.cpp` x1 | Deliberate: the tests read a moved-from object to prove the move-only RAII wrappers null themselves out. Enforcing would require `NOLINT` on intentional test code. |

**Disposition:** deferred. The gate is real today on `bugprone-dangling-handle`
and `clang-analyzer-core.*` (both clean), and tightens by moving a family from
the checks list into the errors list once its findings are resolved.

## 3. cppcheck 2.7 cannot produce a native hash-anchored baseline (08-02 Task 2)

`cppcheck` accepts a `<hash>` suppression element but its CLI never computes a
per-finding hash. Verified on this host: `<hash>0</hash>` matches every finding
and any non-zero hash matches nothing. The gate therefore computes its own
content hash and derives a hash-free suppression file for cppcheck at run time
(see the header of `scripts/analysis/run-static-analysis.sh`).

**Disposition:** revisit if cppcheck is ever upgraded past the Ubuntu 22.04
archive version — a newer release that emits finding hashes would let layer 2
be replaced by cppcheck's own mechanism. Not a blocker; the current two-layer
design is strictly more precise than cppcheck's `(id, fileName)` matching.

## 4. Pre-existing `-Wunused-result` warning in `sigHandler` (Release only)

**Found during:** 08-02 Task 3, once `scripts/gates/build-all.sh` started recording
per-tree build logs and the Manager.cpp edit forced a Release recompile.

```
src/Manager.cpp:311:20: warning: ignoring return value of 'ssize_t write(int, const void*, size_t)'
                       declared with attribute 'warn_unused_result' [-Wunused-result]
  311 |         (void)write(s_pipeWriteFd, &c, 1);
```

**Pre-existing:** yes. `git diff -U0 src/Manager.cpp` for 08-02 Task 3 touches
only `spawn()`, `spawnArgv()` and `launchApp()` (lines 844+); `sigHandler` is
untouched. It appears only in the Release tree because `_FORTIFY_SOURCE` is
active at -O2, and it had never been surfaced before because the Release object
for `Manager.cpp` was already up to date.

**Note:** the `(void)` cast does not suppress `warn_unused_result` in GCC. The
usual fix is to consume the result (`if (write(...) < 0) { }`). Not fixed here:
out of scope for this plan, which builds gates rather than fixing findings.

**Disposition:** deferred. `build-all.sh` records warnings without failing on
them, so this is visible in `build/release/build.log` rather than silent.

## 5. Bare `ctest --test-dir build/asan` is red (PRE-EXISTING); the gate is green

**Found during:** 08-02 Task 3, running the plan's verification commands.

`ctest --test-dir build/asan --output-on-failure --no-tests=error` fails 3 xft
tests (#49 shaped-window text, #50 UTF-8 rendering, #51 rotated font):

```
==...==ERROR: LeakSanitizer: detected memory leaks
SUMMARY: AddressSanitizer: 288 byte(s) leaked in 2 allocation(s).
```

**Cause:** the fontconfig process-lifetime cache leak that `tests/lsan.supp`
exists to suppress. 08-01 wired those suppressions into the *forked WM child*
only (`WmFixture::sanitizerEnv`), never into the ASan-instrumented Catch2 test
binaries themselves. Nothing had surfaced it because 08-01 only ever ran the
asan tree with `-L '^wm_process$'`, and those tests do not touch Xft directly.

Confirmed to be the whole story: re-running the same test with
`LSAN_OPTIONS=suppressions=tests/lsan.supp:fast_unwind_on_malloc=0` passes.
The 288 bytes / 2 allocations match the `libfontconfig` suppression that
`build-all.sh` reports as matched on every asan run.

**Pre-existing:** yes. Independent of the 08-02 Task 3 `_exit()` change, which
touches only `spawn()`, `spawnArgv()` and `launchApp()`.

**Why not fixed in 08-02:** the fix is to attach `LSAN_OPTIONS` to the test
binaries, which means either 12 `catch_discover_tests(... PROPERTIES ...)`
edits in `CMakeLists.txt` (5 of which already carry other properties) or a
`__lsan_default_suppressions()` translation unit linked into every test target.
Both are non-trivial edits to a shared build file that the remaining twelve
Phase 8 plans will also be editing, for a condition this plan did not cause.

**Workaround in place:** `scripts/gates/build-all.sh asan` exports the declared
suppressions itself, so the sanitizer gate is green and reports the matched
suppression counts. Run the asan tree through the gate, not through bare ctest.

**Disposition:** deferred to the test-infrastructure work (08-11 … 08-13).

### STILL OPEN after 08-13, deliberately

08-13 was the last plan scheduled to touch shared test infrastructure and did
consider closing this. It did not, and the reason is a judgement rather than an
oversight: the two available fixes are twelve `catch_discover_tests(...
PROPERTIES ENVIRONMENT ...)` edits in `CMakeLists.txt`, or a
`__lsan_default_suppressions()` translation unit that has to be linked into
every test executable individually (the LSan runtime resolves that symbol in the
MAIN binary, so one shared object library does not reach it). Both are broad
edits to a shared build file for a condition no plan in this phase caused, and
`scripts/gates/build-all.sh asan` -- the sanctioned way to run the asan tree --
exports the suppressions itself and is green.

**Recommended owner:** whoever next has an independent reason to restructure the
test target declarations in `CMakeLists.txt`. Until then the rule stands: run
the asan tree through the gate, not through bare `ctest`.

## 6. `WindowManager::circulate()` spins forever when no client is in Normal state — RESOLVED in 08-07 (`62e9c0d`)

**Found during:** 08-04 Task 2, while building the geometry suite's clamp trigger.

**Symptom:** a Button3 press on the root window freezes the WM at 100% CPU,
permanently. **Measured:** 299 CPU ticks of user time over 3 seconds of wall
clock on a WM with no Normal client, immediately after a synthetic root Button3.

**Cause:** `src/Buttons.cpp:55-60`.

```
for (j = i + 1; ; ++j) {
    if (static_cast<size_t>(j) >= m_clients.size()) j = 0;
    if (j == i) return;
    if (m_clients[j]->isNormal() && !m_clients[j]->isTransient()) break;
}
```

The loop's only exit besides the `break` is `j == i`. When `m_activeClient` is
null, `i` is `-1`, and `j` — being an index that is wrapped into
`[0, m_clients.size())` — can never equal `-1`. So if no entry satisfies the
`break` condition, the loop is unbounded. The `m_clients.empty()` guard above it
does not help: the list only has to be non-empty, not to contain a *Normal*
client.

**Reachability is not exotic.** `m_clients` is never empty in practice, because
the WM's own menu, submenu and WM-check windows are picked up by
`scanInitialWindows()` and managed as clients (see item 7), and those are always
Withdrawn. So a right-click on the root of a freshly started WM with no windows
open freezes it. The same happens after any transition that leaves the only real
client non-Normal — which is how this was found: the EWMH fullscreen path
(item 8) leaves the client Withdrawn, and the geometry suite's clamp trigger then
wedged the WM.

**Pre-existing:** yes. `src/Buttons.cpp` is touched by plan 08-04 only at the six
`DisplayWidth`/`DisplayHeight` call sites (lines 119-120, 361-362, 556-557); the
`circulate()` loop is untouched, and the `CR-01/CR-03` comment above it shows the
bounds work predates this phase.

**Why not fixed here:** 08-04 is a declared behaviour-preserving refactor
("Do not add ... any resolution-change behaviour in this plan"), and the fix is a
behaviour change to an unrelated subsystem (deviation Rule 4). It needs its own
decision about what a circulate with no eligible client should do — return, or
fall through to the first client regardless of state.

**Disposition:** ~~deferred, recommended for the focus/rules work (08-07 … 08-10),
which is the phase's next scheduled visit to `src/Buttons.cpp`.~~ **RESOLVED** —
see below.

### RESOLVED (08-07): bounded scan, `62e9c0d`

Fixed in plan 08-07 at the user's explicit direction, as additional in-scope work
alongside the focus-policy tasks — 08-07 opens `src/Buttons.cpp` anyway and the
defect is focus-adjacent.

**Regression test first.** `tests/test_wm_process.cpp` gained a `[wm_circulate]`
case that reproduces the freeze and fails against the unfixed code. Measured RED:
**595 CPU ticks burned during a 6-second responsiveness probe**, and a window
mapped after the root Button3 was never framed. Both assertions go green after the
fix, and the case now completes in 2.2 s.

The case asserts **two** things, because "alive" and "working" are different
claims and the defect satisfies the first:

- **responsiveness** — a window mapped after the click is still framed, on a 6 s
  deadline, so a regression *fails* rather than hanging the suite. A `TIMEOUT` was
  also added to the `test_wm_process` ctest registration as a structural backstop.
- **idleness** — the WM's CPU time over a quiet 2 s interval, read from
  `/proc/<pid>/stat` through the new `wm2test::processCpuTicks()` helper, must stay
  under 20 ticks. A wedged WM accrues roughly 200 over the same interval. This is
  the assertion that separates "blocked in `poll()`" from "burning a core", and the
  one that would catch a "fix" that merely relocated the busy-wait.

**The test does not depend on item 7.** The obvious precondition — start the WM
with no clients and rely on the adopted menu/WM-check windows to keep `m_clients`
non-empty — would go silently vacuous the day item 7 is fixed. The case instead
maps its **own transient** window: `Normal` but `isTransient()`, which is exactly
what the break condition rejects, so the list is non-empty and eligible-free under
the test's own control whichever way item 7 goes.

**The fix** replaces the unbounded loop with a scan bounded by `m_clients.size()`
that visits each entry at most once and returns when nothing is eligible. It is
behaviour-preserving where eligible clients exist: with `i >= 0` the step range
`1..n` visits `i+1 … i+n-1` and then hits `j == i`, which is precisely the set the
old loop examined before its `j == i` return. Confirmed by the `[wm_geometry]`
clamp cases, which reach `ensureVisible()` through this same call and still pass.

**Verified:** `[wm_circulate]` green in debug and through
`scripts/gates/build-all.sh asan` (0 sanitizer findings);
`scripts/analysis/run-static-analysis.sh` exit 0 with cppcheck back at its
18-finding baseline.

**Note for whoever revisits `tests/test_wm_geometry.cpp`:** its case 5 comment
still explains that it declines to drive the clamp on a non-Normal client because
circulate would wedge the WM. The wedge is gone; the comment is now history rather
than a live constraint. Left in place because rewriting that file is not 08-07's
job, but it should not be read as a standing restriction.

## 7. The WM manages its own menu/submenu/WM-check windows as clients (PRE-EXISTING) -- RESOLVED in 08-14 (`12f6de8`)

**Resolution.** The popups and the EWMH WM-check window were created with plain
`XCreateSimpleWindow`, so they were ordinary top-level children of the root and
`eventCreate()` adopted each as a client. They are now created override-redirect
via `XCreateWindow`, with the flag IN THE CREATION VALUEMASK -- setting it
afterwards with `XChangeWindowAttributes` does not work, because `CreateNotify`
carries the creation-time value and `eventCreate()` reads it off that event.
Measured on a live WM: `_NET_CLIENT_LIST` went from four WM-owned windows plus
the real client, to the real client alone. `scanInitialWindows()`'s
`wins[i] == m_menuWindow` special case is gone with it -- it named one of the
four affected windows and was never a general fix.

**Found during:** 08-04 Task 2, reading `_NET_CLIENT_LIST` from the geometry suite.

**Symptom:** on a WM with no real clients at all, `_NET_CLIENT_LIST` on root
contains three windows, each 1x1, all owned by the WM's own connection — the
menu window, the submenu window and the EWMH WM-check window.

**Cause:** `initialiseScreen()` creates those three with plain
`XCreateSimpleWindow`, which leaves `override_redirect` false, and
`scanInitialWindows()` then adopts every non-override-redirect child of root.
They stay Withdrawn (never mapped through a MapRequest), so nothing visible
breaks — but they are published to every EWMH-aware client as managed windows,
and they are what makes `m_clients` non-empty in item 6.

**Pre-existing:** yes; untouched by plan 08-04.

**Disposition:** deferred. Two candidate fixes, both out of scope here: set
`override_redirect` on the three WM-internal windows at creation, or filter them
out in `updateClientList()`. The EWMH work (Phase 6) is the natural owner.

## 8. EWMH fullscreen sizes the window correctly but leaves it at its old position and Withdrawn (PRE-EXISTING) -- RESOLVED in 08-12 (`3ff6d97`)

**Found during:** 08-04 Task 2, writing the fullscreen geometry case.

**Symptom:** after `_NET_WM_STATE_ADD _NET_WM_STATE_FULLSCREEN`, the client
window is resized to exactly the screen (1280x1024 on the fixture) — correct —
but sits at the coordinates its frame had before, not at the screen origin.
**Measured:** a client mapped at 1100,900 became `1280x1024+1100+900`, parented
to root. It is also no longer in Normal state.

**Cause (traced):** `Border::stripForFullscreen()` reparents the child to root.
`XReparentWindow` implicitly unmaps and remaps a mapped window, so the WM
receives an `UnmapNotify` for its own reparent. `Client::eventUnmap()` sees a
Normal client with `m_reparenting` false and calls `withdraw()`, which does
`gravitate(true)` and reparents the window to root **at the pre-fullscreen
coordinates**, undoing the placement `setFullscreen()` had just made and leaving
the state Withdrawn. The `XMoveResizeWindow(m_window, 0, 0, sw, sh)` size
survives because only the position is rewritten.

**Pre-existing:** yes. Plan 08-04 changed only the two screen-dimension reads
inside `setFullscreen()`, and the size half — the part those reads feed — is
demonstrably correct.

**Why not fixed here:** behaviour change, unrelated subsystem, deviation Rule 4.
The likely fix is to set `m_reparenting` around the strip/restore reparents the
same way `Client::manage()` does, but that is a decision for the owner of the
fullscreen path.

**Disposition:** ~~deferred.~~ **RESOLVED** -- see below. `tests/test_wm_geometry.cpp`
pins the size (which is the D-27 accessor claim) and deliberately does NOT pin
the position, which is exactly why the fix landed without a test to fight.

### RESOLVED (08-12): the diagnosis above was half right, and the smaller half

Plan 08-12's `[wm_fsmax]` group could not be written at all without fixing this,
so it was fixed with a reproducing test first.

**The reparent is not the trigger.** Setting `m_reparenting` around the
strip/restore reparents -- the fix item 8 itself proposed -- was implemented and
was *not sufficient*. Tracing `Client::eventUnmap` on the real binary showed why:

```
TRACE eventUnmap win=<frame> state=Normal reparenting=1  -> guard consumed
TRACE eventUnmap win=<tab>   state=Normal reparenting=0  -> withdraw()
```

`WindowManager::eventUnmap()` resolves the event window through
`windowToClient()`, whose fallback scan calls `Client::hasWindow()` -- and that
is true for the **frame, the tab, the button and the resize handle** as well as
for `m_window`. `Border::stripForFullscreen()` calls `XUnmapWindow` on three of
them, so all three arrived in `Client::eventUnmap()` as though the application
had unmapped its own window. The first consumed the reparent guard; the **tab's**
unmap then took the withdraw path. `gravitate(true)` put the window back at its
pre-fullscreen coordinates -- the reported symptom, exactly -- and
`setState(Withdrawn)` took it out of management.

`Client::hide()` escaped the same fate only by timing: it calls
`Border::unmap()` and then `setState(Iconic)` synchronously, so by the time those
three events are processed the switch lands on the Iconic arm. Luck, not design.

**Fixed in three places** (`3ff6d97`, `cddb979`):

- `Client::eventUnmap()` acts only on `m_window`'s own unmap. The ICCCM synthetic
  withdraw a client sends by hand carries the client window in `e->window`, so
  this does not interfere with it.
- `Client::markReparenting()` arms the existing guard around both fullscreen
  reparents, the way `Client::manage()` already does for its own. It queries map
  state first -- a hidden client's reparent generates no `UnmapNotify`, and an
  unconditional flag would survive to swallow a real withdraw.
- `setFullscreen()` records the fullscreen rect in `m_x/m_y/m_w/m_h`, so the WM's
  coordinates agree with what it told the server.

**Mutation-verified:** deleting the `eventUnmap` guard reddens 2 cases; deleting
both `markReparenting()` calls reddens the same 2. Neither alone is sufficient,
which is why the original one-line diagnosis would not have worked.

## 9. The WM does not flush its X output until its event loop wakes again (PRE-EXISTING)

**Found during:** 08-04 Task 2; it made three of the five new geometry cases fail
against a correct implementation.

**Symptom:** a client that moves itself with `XMoveResizeWindow` and then goes
quiet does not visibly move. **Measured:** the WM handled the `ConfigureRequest`
and issued `XConfigureWindow(frame, CWX|CWY|CWWidth|CWHeight, ...)` with the
right coordinates (confirmed by instrumenting `Border::configure()`), yet the
server still reported the frame at its old position after 8 seconds of polling
with no other X traffic. The move appeared the instant any unrelated event
reached the WM — reproduced deterministically by creating one throwaway window.

**Not yet explained.** `WindowManager::nextEvent()` does call
`XFlush(display())` before `poll()` (`src/Events.cpp:147`), so on a reading of
the code the buffer should already be on the wire. Something between that flush
and the server is holding the request; this was not chased further because the
plan in progress was a behaviour-preserving refactor. It is worth chasing: for a
user it means a self-repositioning application appears frozen in place until
something else happens on the desktop.

**Pre-existing:** yes; reproduced with `src/Border.cpp`, `src/Events.cpp` and
`src/Client.cpp` at their pre-08-04 state.

**Workaround in place:** `tests/test_wm_geometry.cpp` wakes the WM with an inert
override-redirect 1x1 window before every geometry read (`pumpWm()`), and
documents that it is a workaround rather than a convention to copy.

**Disposition:** deferred to the test-infrastructure / diagnostics work
(08-11 … 08-13), or to whoever next touches the event loop.

### UPDATE (08-07): this now makes NEGATIVE TESTS LIE — priority up

08-06 recorded item 9 as a harness inconvenience with a known workaround. 08-07
found it is worse than that: **two of that plan's three gate negative-tests
failed to fail because of it.** Deleting the production click-to-focus gate left
`tests/test_wm_focus.cpp` fully green — the WM had processed the crossing event,
fired its timer and published `_NET_ACTIVE_WINDOW`, and the test still read the
previous value. A test suite that stays green with a gate deleted is worth
nothing, and nothing about the symptom announces itself.

Two facts measured while chasing it, both now encoded in `settleWm()`:

- **One nudge is not enough** before a non-event read; the WM's output is still
  in its buffer.
- **N nudges back to back are not N wake-ups.** A ten-pump tight loop still read
  the stale value. The WM is not necessarily scheduled between two nudges issued
  microseconds apart. Fifteen pumps at 20 ms intervals read the true value every
  time, and reddened the deleted gate every time.

So every future "the WM did not do X" assertion in this phase has to carry a
repeated, *spaced* settle, or it is not an assertion at all. That is a workaround
being copied into more places rather than a defect getting smaller.

Worth checking when this is fixed, or before: `tests/test_wm_geometry.cpp` case 9
uses a ten-pump TIGHT loop before its own "the WM did nothing" assertions. Its
negative tests were recorded red at the time it was written, so it is probably
fine — but it is the same shape that failed here.

## 10. Rare WM startup hang before the event loop begins (PRE-EXISTING, unconfirmed cause)

**Found during:** 08-05 Task 3, running the combined
`-L '^(wm_geometry|wm_norandr)$'` gate repeatedly for the plan's stability
criterion.

**Symptom:** roughly once in ten runs of a 15-case gate, one or two fixtures
fail at startup with the staged diagnostic

```
WmFixture: WM startup failed at stage: event loop not pumping
  (probe window never reparented) on :120
xvfb: pid=... alive
wm:   pid=... alive
```

The WM child is **alive**, its stderr contains the complete startup banner
through `  Xrandr extension available.`, and `_NET_SUPPORTING_WM_CHECK` has
been published on root — so `setupEwmhProperties()` ran. What never happens is
the reparent of the readiness probe window, i.e. `loop()` never processes a
MapRequest. The WM is wedged **between the EWMH publication and the event
loop**, and stays that way until the fixture kills it.

**Most likely cause (NOT yet confirmed — no hung process was caught under a
debugger):** `WindowManager::timestamp()` at `src/Manager.cpp:723-737` is the
only unbounded blocking wait in that window. It appends zero bytes to
`_WM2_RUNNING` on root and then calls

```cpp
XMaskEvent(display(), PropertyChangeMask, &event);
```

which blocks forever if the expected `PropertyNotify` never arrives. It is
called as `timestamp(true)` from the constructor
(`XSetSelectionOwner(..., timestamp(true))`), immediately after
`initialiseScreen()` and before `scanInitialWindows()`/`loop()` — exactly the
interval the symptom brackets. A missing or already-consumed `PropertyNotify`
would produce precisely this picture. `Client` construction during
`scanInitialWindows()` is the only other candidate and would have to be reached
first.

**Pre-existing:** treated as such on the evidence available, which is
suggestive rather than conclusive and is recorded honestly as such:

- The blocking call is untouched by plan 08-05. This plan added
  `StructureNotifyMask` to the root mask and an `XRRSelectInput`, neither of
  which removes `PropertyChangeMask` or consumes queued events —
  `XMaskEvent` leaves non-matching events on the queue by construction.
- Measured rate on the post-08-05 tree: **0 failures in 6 consecutive runs**
  of the 15-case gate (90 test executions) after the single observed
  occurrence.
- Measured rate at the pre-08-05 baseline commit `f4941dd`, on an
  equivalent-load gate (`-L '^(wm_geometry|wm_noshape|wm_process)$'`, also 15
  cases, same number of Xvfb+WM fixture startups): **0 failures in 6
  consecutive runs**.

So neither tree reproduced it in 12 further runs, and the rate is too low for
6-run samples to separate them. What can be said with confidence is that the
code path that hangs is not code this plan wrote or modified.

**Impact:** a rare red run of any process-level suite, with a clear diagnostic
naming the stage. It is a test-harness-visible symptom of a WM defect, not a
harness defect: a real user whose WM wedged here would see a session that never
starts managing windows.

**Disposition:** deferred. Worth catching under a debugger the next time it
reproduces — `gdb -p <wm pid>` on the hung child would confirm or refute the
`XMaskEvent` hypothesis in one backtrace. Natural owner is the
test-infrastructure / diagnostics work (08-11 … 08-13), alongside item 9, which
is also an event-loop timing defect.

### UPDATE (08-06): the `XMaskEvent` hypothesis is refuted; this is item 9

08-06 reproduced this at a much higher rate than 08-05 could — **5 failures in
144 fixture startups (~3.5%)** on its new `[wm_norender]` group — which made it
measurable rather than anecdotal. What that measurement establishes:

- **Not `XMaskEvent`.** `timestamp(true)` waits for a `PropertyNotify` matching
  `PropertyChangeMask`, and root carries that bit (`src/Manager.cpp:568`).
  `setupEwmhProperties()` runs *before* it (last statement of
  `initialiseScreen()`) and writes a dozen root properties, so several matching
  events are already queued when the wait begins. `XMaskEvent` returns the first
  one immediately. The originally suspected call cannot block here.
- **Not `scanInitialWindows()` adopting the probe either.** Tested directly by
  inserting a 700 ms delay ahead of the scan so the readiness probe was
  guaranteed to be adopted rather than map-requested: the probe was still
  reparented and the case still passed, with the retry disabled.
- **It is item 9.** The remaining signature matches item 9 exactly: the WM has
  *issued* the reparent but its output is not on the wire, and the fixture is
  polling with no other X traffic to wake it. Item 9 records the same behaviour
  and the same cure — "The move appeared the instant any unrelated event reached
  the WM — reproduced deterministically by creating one throwaway window."
- **The cure confirms it.** `WmFixture::proveEventLoopLive()` now retries with a
  **fresh** probe window (up to three attempts) instead of waiting longer on one.
  Creating the second window is itself the unrelated event that wakes the WM.
  Measured after the change: **0 failures in 150 fixture startups** of the same
  gate.

**Limits of this evidence, stated plainly:** the flush stall is inferred from
item 9's documented signature and from the fact that the cure is item 9's known
workaround — it was not observed directly on a wedged process. `ptrace_scope=1`
on this host prevents a sibling `gdb` from attaching, and the failure did not
reproduce under a standalone harness where the WM could be made a direct child
(0 wedges in 20 runs there). The before/after rates are the strong part of this;
the mechanism is the plausible part.

**Disposition:** item 10 is now a *symptom* of item 9 and should be closed with
it. The harness no longer flakes on it, so it is no longer a test-reliability
problem — but the underlying defect is unchanged and still user-visible: a
self-repositioning application appears frozen until something else happens on
the desktop.

## 11. The sideways tab does not grow with the window title (PRE-EXISTING, visible) — RESOLVED in 08-14 (`cab658b`)

**Found during:** 08-06 Task 2, while reading `Border::fixTabHeight()` closely
enough to make it null-font safe.

**Symptom:** the tab is very nearly the same length whatever the title is.
**Measured** on a live WM (two clients, one titled `A`, one titled
`A Very Long Window Title Indeed Yes`): the tab windows came back **265x54** and
**265x58** respectively — four pixels apart for a 34-fold difference in title
length. The long label therefore runs far past the end of its tab.

**Cause:** the two rotated-extent reads have their axes swapped. For an
`FcMatrix`-rotated font the string runs along `XGlyphInfo::height` and its
thickness is `XGlyphInfo::width` — **measured** in this plan's
`[xft_norender_spike]`: `"M"` is `w=12 h=13`, `"Hello"` is `w=12 h=39`. But:

- the tab WIDTH (thickness) is computed from `extents.height` of a sample glyph
  — the along-string advance;
- the tab LENGTH is computed from `extents.width` of the label — the thickness,
  which barely moves with the label and changes only when the glyph set gains a
  descender or a capital.

Swapping the two reads would make the tab track the title, which is what the
sideways-tab design intends and what the ellipsis-shortening loop below it
already assumes (that loop is currently near-dead: `m_tabHeight` almost always
comes out under `maxHeight` on the first try, so titles are never shortened).

**Pre-existing:** yes. Both reads predate Phase 8; 08-06 moved the first one
into `loadTabFont()` unchanged and left the second exactly where it was.

**Why not fixed here:** 08-06 explicitly forbids changing the rendered
appearance when XRender is available, and this is a pure appearance change on
the healthy path — the opposite of what that plan is for. It also needs a
before/after visual comparison to land safely, which is 08-14's evidence work.

**Disposition:** deferred, recommended for 08-14 or a follow-up rendering plan.
It is a two-line change plus screenshots; the risk is entirely in the review,
not in the edit.

### UPDATE (08-14): SCREENSHOTTED, re-measured, and the consequence is worse than recorded

08-06 deferred this saying it "wants 08-14's screenshots". It now has them:
`evidence/screenshots/deferred-item-11-tab-does-not-track-title.png`.

**Reproduced independently on a fresh run** (Xvfb 1000x700, shipped defaults,
two `xmessage` windows differing only in title):

| Title | Length | Tab window geometry |
|---|---|---|
| `A` | 1 char | `0x200027` **325x54** |
| `A Very Long Window Title Indeed Yes` | 34 chars | `0x200021` **325x58** |

**Four pixels apart for a 34-fold difference in title length** — the same
54 / 58 figures 08-06 measured, on a different display size, which makes the
axis-swap diagnosis in the entry above about as confirmed as it gets without
touching the code.

**What the screenshot adds, and it is not cosmetic.** 08-06 predicted the label
would "run far past the end of its tab". It does not. It is CLIPPED TO THE TAB,
and the tab is ~54 px of the ~130 px the rotated string needs, so:

- the 1-character title renders a legible sideways `A`;
- **the 34-character title renders NO VISIBLE LABEL AT ALL.**

So the user-visible effect is not an overhanging label, it is a window with an
**empty tab**. Every window whose title is more than a few characters — which is
most windows, most of the time — is unlabelled. That is a materially worse
defect than the entry above describes, and it is the single most visible
rendering issue in the product.

**Still not fixed, deliberately, and here is the reasoning rather than a
shrug.** Plan 08-14 is the phase's evidence and signoff plan: its
`files_modified` does not include `src/Border.cpp`, and it neither asks for nor
budgets a rendering change. Editing the tab-metrics reads at the final signoff
step would put an unreviewed, highly visible appearance change into the same
commit range that is supposed to be *certifying* the appearance — and the human
visual pass (08-14 Task 3) is precisely the review this change has always needed
and has never had. Changing what that pass is looking at, on the way into it,
would be the wrong order.

**Recommended owner:** the first plan of the next phase, as a small dedicated
rendering change — swap the two `XGlyphInfo` axis reads in `Border::loadTabFont()`
and `Border::fixTabHeight()`, then re-take the screenshot above as the
before/after pair. Note that the ellipsis-shortening loop below those reads is
currently near-dead (`m_tabHeight` almost always lands under `maxHeight` on the
first try) and **will start firing** once the tab tracks the title, so that loop
gets exercised for the first time by this fix and needs its own look.

### RESOLVED (08-14, `cab658b`) — at the user's direction, after the screenshots

The deferral above was overtaken: the screenshots it asked for were damning
enough that fixing it before the appearance-certification pass became the right
order rather than the wrong one. Recorded as an **authorised deviation** —
`src/Border.cpp` is outside plan 08-14's declared `files_modified`.

**THREE sites, not the two this entry predicted.** The third is the one that
mattered:

| # | Site | Was | Now |
|---|---|---|---|
| 1 | `loadTabFont()` — tab THICKNESS | `extents.height + 4` | `extents.width + 4` |
| 2 | `drawLabel()` — the x DRAW ORIGIN | `2 + extents.height` | `2 + extents.width` |
| 3 | `fixTabHeight()` x3 — tab LENGTH | `extents.width + 6 + …` | `extents.height + 6 + …` |

**Site 2 is why the label vanished rather than overhanging.** This entry
predicted the label would "run far past the end of its tab". It does not. The
draw origin was computed on the same swapped axis, so for a 35-character title
it landed at `2 + 286 = 288 px` — far outside a ~20 px tab — and the server
clipped the entire label away. That is the difference between an ugly tab and a
blank one, and it is why the defect was invisible to every geometry assertion.

**Site 1 was wrong by ONE PIXEL and that is why it survived 25 years.** On the
one-character sample `"M"` the two axes nearly coincide (width 12, height 13),
so the thickness read looked correct. Its siblings, measuring the whole *label*
on the same wrong axis, were wrong by an order of magnitude.

**Measured, by the new test:** tab length `58 -> 325 px`, label ink
`0 -> 611 px`. Screenshots: `evidence/screenshots/deferred-item-11-BEFORE-…png`
and `…-AFTER-…png`, same recipe, same display size.

**The shortening loop this entry warned about is now covered.** It was dead code
before the fix and fires whenever a long title meets a short window. Covering it
needed a long **icon name** as well as a long title — with the icon name unset
the `"incognito"` fallback fits and the loop is still never entered. Measured:
mutation M4 stayed **green** until that was corrected, so the entry's warning was
right and would otherwise have gone unmet.

**Mutation testing, both trees, no divergence:** M2, M3 and M4 each redden a
named case; **M1 SURVIVES and is recorded as a survivor, not as an equivalent
mutant.** It is a genuine 1-pixel over-allocation of tab thickness that no
assertion pins. Killing it would require asserting the font's own metric from a
process-level test, i.e. linking Xft into `test_wm_runtime` — a shared-build-file
change outside 08-14's scope, deliberately not made silently.

**Note on the ASan flake seen while verifying this.** The three-tree gate failed
once on `new-window-command decides which program the menu's New entry starts`
(08-13's case, untouched here). Investigated rather than assumed: measured in
ASan **isolation**, 30 runs per tree state — **pre-fix 4 failures, post-fix 2**.
The unfixed tree flakes *more*. An initial pre-fix batch of 10/10 was a lucky
sample, which is precisely why 10-run samples cannot separate rates in this
range. Unrelated to this fix; it belongs with item 12.

## 12. The full process-level suite flakes at roughly one test per run (PRE-EXISTING, quantified)

**Found during:** 08-06 Task 3, running `scripts/gates/build-all.sh` end to end
for the first time since the suite reached 171 tests.

**Symptom:** a full-suite run occasionally reports one or two failures, and a
different test each time. Observed across `wm_process`, `wm_geometry`,
`wm_noshape` and `xtest` groups; every one of them passes on its own, often
dozens of times in a row.

**Measured, both trees, same command (`ctest --test-dir build/<tree>` over 171
tests):**

| Tree | Runs | Failing test executions |
|---|---|---|
| pre-08-06 baseline `22a1ba8`, release | 4 | **8** |
| post-08-06 `3f7c7fb`, release | 4 | **3** |
| post-08-06 `3f7c7fb`, debug | 3 | **1** |

So it is **pre-existing and not caused by this plan** — if anything 08-06's
`proveEventLoopLive()` retry (see item 10's update) removed a share of it.
Individually re-running the tests that failed passes every time: measured
6/6 green for the two release failures and 3/3 for the ASan one.

**Suspected cause:** the same unflushed-output stall as item 9. Every one of the
observed failures is an assertion that polls the server for a geometry or
property the WM has already issued a request for. `tests/test_wm_geometry.cpp`
carries a `pumpWm()` workaround for exactly this; `tests/test_wm_process.cpp`
and the others do not. Under full-suite load the WM sits idle longer between
events, which is precisely when the stall is visible.

**Impact:** a full-suite gate is not reliably green in one shot, which erodes
the value of `build-all.sh` as a release signoff (TEST-06). It is a harness /
event-loop timing problem, not a correctness problem in the assertions
themselves.

**Disposition:** deferred to the test-infrastructure work (08-11 … 08-13),
together with items 9 and 10 — they are one defect wearing three hats. The
likely fix is at the source (make the WM flush when it has issued requests and
is about to block) rather than spreading `pumpWm()` to every suite.

## 13. Destroying a client always logs one `X_UnmapWindow BadWindow` (PRE-EXISTING)

**Found during:** 08-11 Task 1, by the new "no X protocol error" assertion in
`tests/test_wm_lifecycle.cpp`. The WM's `errorHandler()` logs and returns 0, so
an entire class of teardown defect is invisible to any assertion about windows
and properties -- which is why that assertion exists at all, and why it found
two things at once.

**Symptom:** every destroy of a managed client produces exactly one

```
wm2: X_UnmapWindow (0x...): BadWindow (invalid Window parameter)
```

**Cause (confirmed by backtrace, with `XSynchronize` on and
`backtrace_symbols_fd` in `errorHandler`):**

```
Client::deactivate()          src/Client.cpp:345
  Client::decorate(bool)      src/Client.cpp:1206
  Border::decorate(...)       src/Border.cpp:1117
  Border::setFrameVisibility  src/Border.cpp:960   -> XUnmapWindow(m_resize)
```

`m_resize` is created as a child of `m_child` -- the **client** window --
(`Border::configure`), so the server destroys it together with the client. The
`UnmapNotify` that precedes the `DestroyNotify` sends the WM down
`eventUnmap -> clearFocus -> deactivate -> decorate(false)`, which unmaps a
resize handle the server has already reclaimed.

**Why not fixed in 08-11:** the candidate fixes are all bigger than the symptom.
Reparenting `m_resize` under `m_parent` instead of `m_child` changes the frame's
structure (deviation Rule 4); widening `ignoreBadWindowErrors` to cover
`eventUnmap` is exactly what the checklist's "scoped tightly around cleanup and
does not mask unrelated protocol errors" line warns against. It is benign today:
the error is logged, the WM continues, and nothing user-visible follows.

**Consequence for tests:** `tests/test_wm_lifecycle.cpp` asserts on protocol
errors *excluding BadWindow*, in every case rather than only the destroy cases --
`WmFixture` destroys its own readiness probe during startup, so this error can
surface in a case that destroys nothing of its own. Every other error class
(`RenderBadPicture`, `BadValue`, `BadMatch`, `BadDrawable`, ...) still fails.
That narrower assertion is what caught the `XftDraw` teardown defect fixed in
this plan, so the exclusion costs one error code, not the assertion.

**Disposition:** deferred. Natural owner is whoever next revisits frame
construction in `src/Border.cpp`.

## 14. Stale ASan report files fail an innocent test on a reused display (HARNESS)

**Found during:** 08-12 Task 3, while mutation-testing the property-reader
hardening.

**Symptom:** after a mutation run in which the WM child genuinely tripped the
sanitizer, every subsequent `[wm_props]` run was red at
`CHECK(fixture.asanReports().empty())` — with a clean WM stderr, a passing WM,
and no fault of its own. Measured: 9 of 9 cases red on a tree whose source was
back at its correct state, green again the moment five files were deleted.

**Cause:** `WmFixture::globAsanReports()` globs `workDir()/asan<display>*`, and
the display number is drawn from a small reused pool. A report written by an
*earlier* run on display :122 is therefore attributed to the *next* fixture that
happens to be handed :122. Nothing cleans the prefix at fixture startup.

```
build/asan/wm-process-tests/asan-122.1378304   <- written by a mutation run
build/asan/wm-process-tests/asan-122.1378363
...
```

**Not a defect this plan caused, and not one it can hit in normal use:** a
green tree never writes a report, so the files only accumulate when someone is
deliberately breaking the WM — which is precisely what mutation testing is. It
cost roughly twenty minutes of chasing a phantom regression in
`updateWorkarea()`, and it will cost the same again to whoever next mutates this
suite.

**Fix (one line, not applied here):** unlink the fixture's own
`m_asanLogPrefix*` glob in `WmFixture::start()`, before the WM child is
launched. Deliberately not done in 08-12: `WmFixture.h` is shared by six
process-level suites and this plan had no other reason to touch it.

**Workaround meanwhile:** `rm -f build/*/wm-process-tests/asan-*` between
mutation runs.

**Disposition:** ~~deferred to 08-13, which owns the remaining test-infrastructure
work.~~ **RESOLVED (08-13, `b76fd97`).**

### RESOLVED (08-13)

`WmFixture::removeReportsWithPrefix()` is called from `spawnWm()` before the WM
child is launched, and `globAsanReports()` now delegates to the same static
`globWithPrefix()` the cleanup uses -- a cleanup that globbed differently from
the attribution would be a cleanup that does not clean.

The helper is static and public rather than buried in `start()` for a testable
reason: the fixture cannot be handed a chosen display number, but the helper can
be handed a chosen prefix. `tests/test_wm_runtime.cpp` `[wm_harness]` covers
three things, and the third is the one that matters:

1. the attribution really is prefix-based, so the hazard is real rather than
   asserted;
2. the helper clears exactly its own prefix and leaves a neighbouring fixture's
   reports alone -- a cleanup that wiped the directory would pass (1) and would
   silently delete a concurrent worker's genuine finding;
3. the **wiring**: a stale file is planted at a released display's prefix and a
   fresh fixture is started onto that same display, which the reservation hands
   back deterministically (it scans upward from a fixed base to the lowest FREE
   display, and `scripts/gates/build-all.sh` runs ctest serially). Deleting the
   call in `start()` reddens only this half -- mutation M8.

## 15. `X_SetInputFocus ... BadMatch` under heavy window churn (PRE-EXISTING, irreducible)

**Found during:** 08-13 Task 3, by the new 120-window `[wm_stress]` churn case
and 08-11's protocol-error assertion.

**Symptom:** a run that creates, maps, unmaps and destroys 120 windows produces
a small number (measured: 2) of

```
wm2: X_SetInputFocus (0x...): BadMatch (invalid parameter attributes)
```

`BadMatch` from `XSetInputFocus` means the target window was not viewable when
the request reached the server.

**Cause: an irreducible race, not a missing guard.** `Client::activate()`
already returns early unless the client is managed, not hidden and not
withdrawn (`src/Client.cpp`), so the WM only ever focuses a window it believes
is Normal. But its belief is only as fresh as the last event it has processed,
and under churn a window is routinely destroyed between the decision and the
request arriving. No query can close that window: a viewability check would move
the race one round trip earlier and no further. The server discards the request
and nothing downstream is affected.

**Reachability changed in 08-13, the defect did not.** Before the
`Client::unreparent()` queue-discard fix (`e931fd5`) the WM was throwing away
queued events on every teardown, so far fewer focus transitions ran at all.

**Disposition:** accepted rather than deferred. The `[wm_stress]` case BOUNDS it
rather than excluding it -- at most ten occurrences across 120 windows -- so the
race stays invisible while a count that scaled with the churn would fail the
case. Every other protocol-error class still fails it outright.

## 16. The tab-button hit area is subtracted from the frame shape on an inactive client (BY DESIGN, but surprising)

**Found during:** 08-13 Task 1, writing the `destroy-window-delay` case.

**Symptom:** a synthetic press aimed at the tab button of an INACTIVE client does
not reach the button. It falls through to the root window and opens the root
menu instead.

**Cause:** `Border::setFrameVisibility(false, ...)` includes the button's square
in the rectangle list it SUBTRACTS from the frame's bounding shape. The button
window itself is still mapped and still has its geometry, so
`XQueryTree` + `XGetGeometry` find it exactly where it should be -- nothing about
the situation announces itself to a test.

**Not a defect:** the wm2 design is that an inactive window shows only its tab,
and the button belongs to the active decoration. Recorded because it cost time
once and will again: any future case that drives the tab button must activate
the client first (`tests/test_wm_runtime.cpp` does, and says why at the call
site).

## 17. The WM's own menu window can be invalid, so the root menu silently never opens (PRE-EXISTING) -- CAUSE PARTLY REMOVED in 08-14, STILL OPEN

**Status after 08-14.** Item 7 -- the WM adopting its own menu window as a
client, which put every client lifecycle path in a position to invalidate the
very window `menu()` was about to map -- is fixed (`12f6de8`), and that removes
the most plausible mechanism for the `BadWindow` form of this failure.

**It does NOT close this item, and the retry mitigation stays.** The
`[wm_menureopen]` case still fails intermittently with the menu simply never
appearing. MEASURED, 12 isolated runs each: 11/1 with the one-grab rewrite,
11/1 with the settle added, and 11/1 against the PRE-EXISTING two-loop
`menu()`. The identical rate on the old implementation is what establishes that
this is not caused by the rewrite. One captured failure carried
`X_UnmapWindow (0x600024): BadWindow`, but on a `0x6...` XID -- another
connection's client window, not the WM's own menu -- so it is not this item's
signature either. Cause still unknown; do not remove `openRootMenuVerified()`'s
retry until it is understood.

**Found during:** 08-14, by the new `[wm_menulabel]` cases. Not caused by the
menu-label fix those cases exist for: the failing requests are the ones
`menu()` has always issued.

**Symptom:** a Button1 press on root produces **no menu at all**. The WM keeps
running and logs three protocol errors against **its own** menu window id:

```
wm2: X_ConfigureWindow (0x600023): BadWindow (invalid Window parameter)
wm2: X_MapWindow       (0x600023): BadWindow (invalid Window parameter)
wm2: X_UnmapWindow     (0x600023): BadWindow (invalid Window parameter)
```

Those are exactly `menu()`'s opening `XMoveResizeWindow`, its `XMapRaised`, and
the `XUnmapWindow` on the way back out — so `m_menuWindow` is already a dead XID
by the time the user clicks.

**Measured:** 1 run in 10 on the ASan tree; not observed in the debug tree. The
sanitizer is slower, which is consistent with a lifetime race rather than a
deterministic bug.

**Why it matters to a user:** the root menu is the *entire* interface for
launching a window, restoring a hidden one, and exiting the window manager. When
this fires, clicking the root does nothing at all and the only feedback is a line
on stderr nobody is reading.

**Likely mechanism, NOT confirmed:** deferred item 7 — the WM adopts its own
menu, submenu and EWMH check windows as ordinary clients via
`scanInitialWindows()`, because they are created with plain
`XCreateSimpleWindow` and so are not `override_redirect`. Anything that takes a
`Client` through a destroy path is then holding one of the WM's own windows. That
is a hypothesis from the shape of the evidence, not a traced cause; nobody has
caught it under a debugger.

**Harness workaround in place, and it is a workaround:**
`openRootMenuVerified()` in `tests/test_wm_runtime.cpp` insists the window it
returns is viewable AND contains the press point, and retries the whole
press/release cycle up to three times. This is 08-06's remedy for item 10 applied
to the same class of problem — retry rather than wait longer, because no amount
of waiting fixes an interaction that never started. Measured after the change:
**0 failures in 25 ASan runs**, from 1 in 10 before.

The wrapper also closes a real hole in `findOpenMenu()`, which falls back to "the
first viewable child" when nothing contains the press point. When the menu is
missing, that silently hands the caller an unrelated window and every later
assertion measures the wrong pixels — which is how this defect spent its first
hour disguised as a flaky test.

**Disposition:** deferred. It is a genuine user-facing defect and it should be
fixed at the source, most plausibly by giving the three WM-internal windows
`override_redirect` at creation (which also closes item 7) rather than by
hardening `menu()` against its own window having been destroyed. Natural owner is
whoever takes item 7.
