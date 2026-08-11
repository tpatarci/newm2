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

## 6. `WindowManager::circulate()` spins forever when no client is in Normal state (PRE-EXISTING, severe)

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

**Disposition:** deferred, recommended for the focus/rules work (08-07 … 08-10),
which is the phase's next scheduled visit to `src/Buttons.cpp`. Until then,
`tests/test_wm_geometry.cpp` deliberately never drives the clamp on a client that
is not Normal, and says so at the point where it declines to.

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
