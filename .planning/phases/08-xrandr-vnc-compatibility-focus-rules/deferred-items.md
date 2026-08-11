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

## 7. The WM manages its own menu/submenu/WM-check windows as clients (PRE-EXISTING)

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

## 8. EWMH fullscreen sizes the window correctly but leaves it at its old position and Withdrawn (PRE-EXISTING)

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

**Disposition:** deferred. `tests/test_wm_geometry.cpp` pins the size (which is
the D-27 accessor claim) and deliberately does NOT pin the position, so the
correct fix will not have to fight a test that cemented the defect.

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

## 11. The sideways tab does not grow with the window title (PRE-EXISTING, visible)

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
