# Compiled Code Behavior Checklist

This checklist is tailored to this repository: a C++17 X11 window manager with
runtime config, Xft rendering, EWMH support, RAII wrappers, and Xvfb-backed
Catch2 tests.

Use it as a release/signoff checklist for developers. A checkbox is not done
until there is either an automated test, a recorded manual run, or an explicit
accepted exception with a reason.

## Current Scan Findings

**All four are resolved as of Phase 8.** The section is kept rather than deleted
so the history stays legible: each entry records what was wrong, the plan that
fixed it, and the test that would catch it coming back. A resolved finding with
no test behind it is a finding that will be rediscovered.

- [x] Resolve build preflight before claiming test coverage. On this scan host,
      `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON` failed
      because `pkg-config` could not find `xft` or `fontconfig`. CMake requires
      `x11`, `xext`, `xft`, and `fontconfig` in `CMakeLists.txt:11-14`.
      **RESOLVED (plans 08-01 and 08-02).** `scripts/preflight.sh` asserts every
      declared dependency — the six pkg-config modules, the toolchain and its
      CMake floor, the X11 tooling, a simple X client, and the three fontconfig
      chains the WM actually requests. It is registered as the `preflight` ctest
      test with `FIXTURES_SETUP preflight_ok` (`CMakeLists.txt:105`), and every
      process-level suite declares `FIXTURES_REQUIRED preflight_ok`, so a missing
      dependency now fails with a **named cause** before any behavioural
      assertion runs, instead of as an opaque configure error three steps later.
      Deliberately not a CMake configure-time check: that would block plain
      binary builds on machines that never run tests.
- [x] Fix or explicitly accept the `eventDestroy` lifetime hazard in
      `src/Events.cpp:237-283`: the code erases the owning `unique_ptr<Client>`
      and then checks `c->isDock()`. Preserve `wasDock` before erase and test
      dock destruction under ASan.
      **RESOLVED (plan 08-01, and two further use-after-free defects on the same
      path in 08-11 and 08-12).** The dock predicate is captured before the erase.
      Proved by `tests/test_wm_process.cpp` "Destroying a dock window restores
      `_NET_WORKAREA` with no sanitizer report", which runs in the ASan tree
      through `scripts/gates/build-all.sh asan` where a sanitizer report file
      left behind by any child fails the gate — including a report written by the
      forked WM child, which a green test result would otherwise never surface.
- [x] Fix or explicitly prove the no-Shape-extension path. `src/Manager.cpp:160`
      records `m_shapeEvent = -1`, but fallback helpers in `src/Border.cpp:155`
      and `src/Border.cpp:172`, plus `src/Border.cpp:590`, still call
      `XShapeCombineRectangles`. A real no-Shape X server must not trigger Shape
      requests.
      **RESOLVED (plan 08-03).** All 26 previously-unguarded Shape calls now go
      through the single guarded funnel `Border::combineShape()`, with the
      `WM2_FORCE_NO_SHAPE=1` lever so the fallback is reachable on a server that
      does have Shape. Proved by `tests/test_wm_fallbacks.cpp` `[wm_noshape]`,
      which asserts not merely that the WM survives but that it issues **no Shape
      requests at all** on a server without the extension. Plan 08-13 later found
      a related defect this same area was hiding — a `YXSorted` ordering promise
      broken at every non-default frame thickness, leaving the frame unshaped and
      logged rather than shaped — now funnelled through
      `Border::combineShapeSorted()` and covered by `[wm_config_runtime]`.
- [x] Prove focus config is actually wired to behavior. `click-to-focus`,
      `raise-on-focus`, and `auto-raise` are parsed in `src/Config.cpp:152-154`,
      but runtime paths mostly use delayed focus-follows-pointer
      (`src/Client.cpp:1329`, `src/Manager.cpp:810`).
      **RESOLVED (plan 08-07), and the suspicion was correct — all three were
      dead settings.** They parsed and reached nothing. Now wired, and proved by
      `tests/test_wm_focus.cpp` `[wm_focus]`, which drives the real binary with
      real pointer events and covers each flag in **both** directions (six cases:
      click-to-focus on and off, auto-raise on and off, raise-on-focus on and
      off), plus "With auto-raise off the WM burns no CPU while focus tracking is
      live". The negative halves are the load-bearing ones and they only became
      trustworthy after deferred item 9 was understood: a single read after a
      single nudge observes the *previous* state, so every one of them settles
      first. The same plan fixed the `circulate()` 100%-CPU freeze found on the
      way.

## Environment Preflight

- [x] Document distro, compiler, CMake, and pkg-config versions for every signoff.
      DONE (plan 08-14). `scripts/preflight.sh` records them and its output is
      committed at `evidence/gates/preflight-versions.log`. It runs as the
      `preflight` ctest fixture, so a signoff cannot be produced without it.
- [x] Confirm native dependencies are present:
      DONE. Asserted by `scripts/preflight.sh` (six pkg-config modules, not the
      four below — libxrandr and libxrender are hard build deps too).

```bash
pkg-config --exists x11 xext xft fontconfig
cmake --version
c++ --version
```

- [x] Confirm headless and interactive X11 tools are available:
      DONE. Asserted by `scripts/preflight.sh`; transcript in
      `evidence/gates/preflight-versions.log`.

```bash
command -v Xvfb
command -v Xephyr
command -v xprop
command -v xwininfo
command -v xdotool
command -v xdpyinfo
```

- [x] Confirm at least one simple X client is available for runtime tests
      (`xclock`, `xmessage`, or equivalent). Do not rely on `xterm` being
      installed unless it is declared as a package dependency.
      DONE. Asserted by `scripts/preflight.sh`. `xterm` is NOT relied on: it is
      absent on this host and every runtime test uses `xclock`.
- [x] Confirm fontconfig can resolve the fonts used by the Xft fallback chains:
      DONE. `scripts/preflight.sh` resolves all three chains the WM actually
      requests, not the two shown below.

```bash
fc-match "Noto Sans,DejaVu Sans,Sans:size=12"
fc-match "Noto Sans,DejaVu Sans,Sans:bold:size=12"
```

- [ ] Ensure CI does not rely on live GitHub access for Catch2 unless that is an
      explicit policy. Current CMake uses `FetchContent` for Catch2.
- [x] Ensure fixed Xvfb display `:99` is free, or update the test harness to
      allocate a free display.
      DONE. The harness allocates its own display per fixture instance rather
      than depending on `:99` being free (`tests/support/WmFixture.h`).

## Build And Test Gates

- [x] Configure a clean Debug build with tests enabled:
      DONE. `scripts/gates/build-all.sh debug`; log at
      `evidence/gates/build-all-debug.log`, exit 0.

```bash
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
```

- [x] Build without compiler or linker warnings that are new for the change:
      DONE. Zero warning lines recorded; see `evidence/gates/compiler-debug.log`
      and `compiler-release.log`, and the "0 compiler/linker warning line(s)"
      line in each build-all log.

```bash
cmake --build build/debug --parallel
```

- [x] Run the full CTest suite:
      DONE. **295/295 in all three trees** at `c61cb4b`; full logs in
      `evidence/gates/build-all-{debug,release,asan}.log`.

```bash
ctest --test-dir build/debug --output-on-failure
```

- [x] Confirm the expected discovered test surface is present. The tree contains
      **293 Catch2 test cases** across 21 files, of which **292 are registered
      with ctest** (`Total Tests: 295` including the `preflight`, `start_xvfb`
      and `stop_xvfb` fixture tests). RECOMPUTED at `c61cb4b`, not transcribed. The one case not registered is the hidden
      `[.][wm_resource_calibration]` case in `tests/test_wm_resource.cpp`, which
      is hidden on purpose so the judged resource budget cannot have been
      produced by a run that was itself being judged.

      **RECOMPUTE, DO NOT TRANSCRIBE.** This line rotted once already — the
      figure it carried was 102, taken two phases before it was last read, which
      omitted three whole test files and understated a fourth. Refresh it with:

```bash
# source count -- what this line states
grep -c '^TEST_CASE' tests/*.cpp | awk -F: '{s+=$2} END {print s}'

# per-file breakdown
grep -c '^TEST_CASE' tests/*.cpp

# what ctest actually registered, which is the number that gates a release
ctest --test-dir build/debug -N | tail -1
```

      Per file, at the time of writing (plan 08-14):

      | File | Cases | | File | Cases |
      |---|---|---|---|---|
      | `test_config.cpp` | 48 | | `test_wm_fallbacks.cpp` | 13 |
      | `test_wm_state.cpp` | 24 | | `test_desktopentry.cpp` | 12 |
      | `test_wm_runtime.cpp` | 23 | | `test_client.cpp` | 11 |
      | `test_wm_focus.cpp` | 21 | | `test_appcache.cpp` | 8 |
      | `test_rules.cpp` | 17 | | `test_binaryscanner.cpp` | 8 |
      | `test_wm_lifecycle.cpp` | 17 | | `test_wm_rules.cpp` | 8 |
      | `test_wm_geometry.cpp` | 17 | | `test_smoke.cpp` | 7 |
      | `test_ewmh.cpp` | 16 | | `test_wm_process.cpp` | 6 |
      | `test_raii.cpp` | 16 | | `test_xft_poc.cpp` | 6 |
      | `test_autoraise.cpp` | 6 | | `test_eventloop.cpp` | 5 |
      | `test_wm_resource.cpp` | 4 | | | |

      Moved since the 284 figure: `test_wm_runtime.cpp` 16 -> 23 (the menu rework
      and the button-target case) and `test_wm_geometry.cpp` 15 -> 17 (the drag
      clamp and the frame-configure guard). Both are plan 08-14.

      A count that has drifted is not automatically a defect — a plan that adds
      cases moves it legitimately. It is a prompt to find out *which* file
      changed and why.
- [x] Run a Release build and tests:
      DONE. `evidence/gates/build-all-release.log`, exit 0, 295/295. The release
      gate also runs the link audit below.

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure
```

- [x] Run an AddressSanitizer/UBSan build. Treat use-after-free, invalid enum
      use, double-free, and out-of-bounds reports as blockers:
      DONE. `evidence/gates/build-all-asan.log`, exit 0, 295/295, "no sanitizer
      findings". The six files in `evidence/gates/sanitizer-reports/` are LSan
      **suppression-accounting** logs (2 allocations, 288 bytes, libfontconfig),
      not findings — their full contents are in the bundle so the distinction can
      be checked rather than taken on trust.

```bash
cmake -S . -B build/asan -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build/asan --parallel
ctest --test-dir build/asan --output-on-failure
```

- [x] Run static analysis when tools are available:
      DONE. `scripts/analysis/run-static-analysis.sh`, exit 0; log at
      `evidence/gates/static-analysis.log`. cppcheck 2.7: 15 findings, all
      accounted for in the content-hash baseline. clang-tidy 14: no enforced
      check fired.

```bash
cppcheck --enable=all --std=c++17 -Iinclude src include
clang-tidy -p build/debug src/*.cpp
```

- [x] Verify the final executable links only to intended runtime libraries:
      DONE. `evidence/gates/ldd-release.txt`. **libXtst does not appear** — XTEST
      is linked into test targets only, which is the constraint that keeps the
      shipped binary within the 512MB VPS budget.

```bash
ldd build/release/wm2-born-again
```

## Existing Automated Coverage To Preserve

- [x] Config parsing remains covered for defaults, XDG precedence, CLI overrides,
      malformed lines, value length limits, booleans, integers, and clamping
      (`tests/test_config.cpp`).
      PRESERVED (plan 08-14 gate run): 48 cases, green in all three trees.
- [x] RAII wrappers remain covered for null safety, move-only ownership, and
      release semantics (`tests/test_raii.cpp`, `tests/test_smoke.cpp`).
      PRESERVED (plan 08-14 gate run): 16 + 7 cases.
- [x] Event-loop support remains covered for `FdGuard` and basic self-pipe I/O
      (`tests/test_eventloop.cpp`).
      PRESERVED (plan 08-14 gate run): 5 cases.
- [x] Auto-raise timer arithmetic remains covered (`tests/test_autoraise.cpp`).
      PRESERVED (plan 08-14 gate run): 6 cases.
- [x] Xvfb smoke tests continue to open a display and create X11 resources
      (`tests/test_smoke.cpp`).
      PRESERVED (plan 08-14 gate run): 7 cases.
- [x] Client primitive tests continue to cover X window creation, move/resize,
      reparenting, `WM_STATE`, colormap property reads, and multiple windows
      (`tests/test_client.cpp`).
      PRESERVED (plan 08-14 gate run): 11 cases.
- [x] Xft proof tests continue to cover font loading, rotated fonts, shaped
      window drawing, Xft colors, text metrics, and UTF-8 rendering
      (`tests/test_xft_poc.cpp`).
      PRESERVED (plan 08-14 gate run): 6 cases.
- [x] EWMH property tests continue to cover atom interning, WM check window,
      `_NET_SUPPORTED`, desktop/workarea/client/active properties, state arrays,
      fullscreen/maximize atoms, and dock struts (`tests/test_ewmh.cpp`).
      PRESERVED (plan 08-14 gate run): 16 cases.

## Missing Automated Coverage To Add

- [x] Add process-level tests that start `wm2-born-again` itself in Xvfb, not only
      primitive Xlib tests. `WindowManager` starts its event loop in the
      constructor (`src/Manager.cpp:59-176`), so these should run the executable
      as a child process and drive it through X11.
      DONE (plan 08-01, extended by every plan since). `tests/support/WmFixture.h`
      forks the real binary against a private Xvfb; nine suites now drive it.
- [x] Add a lifecycle test for managed window destroy:
      create a normal managed client, destroy it, assert no crash, no stale
      `_NET_CLIENT_LIST` entry, no active-window dangling value, and no ASan
      report.
      DONE (plan 08-11): `tests/test_wm_lifecycle.cpp` `[wm_lifecycle]`. The same
      plan's protocol-error assertion found a `RenderBadPicture` emitted on every
      window close that 243 existing tests had walked straight past.
- [x] Add the same destroy test for dock windows with `_NET_WM_STRUT` and
      `_NET_WM_STRUT_PARTIAL`; assert `_NET_WORKAREA` is recomputed after the
      dock dies.
      DONE (plans 08-01 and 08-11): `tests/test_wm_process.cpp` and
      `[wm_lifecycle]`, run in the ASan tree where a report file left by the
      forked WM child fails the gate.
- [x] Add tests for hidden-list transfers through `Client::hide()` and
      `Client::unhide()` (`src/Client.cpp:775`, `src/Client.cpp:793`), including
      `_NET_WM_STATE_HIDDEN` and `_NET_CLIENT_LIST` updates.
      DONE (plan 08-11): `[wm_lifecycle]`, covering hide/unhide round trips and
      the hidden client's continued presence in `_NET_CLIENT_LIST`.
- [x] Add tests for actual config-to-runtime behavior:
      frame thickness, menu colors, tab colors, `destroy-window-delay`,
      `new-window-command`, `exec-using-shell`, and all focus policy flags.
      DONE (plan 08-13): `tests/test_wm_runtime.cpp` `[wm_config_runtime]`,
      7 cases; the focus policy flags are covered by plan 08-07's
      `tests/test_wm_focus.cpp`. Found a SHAPE ordering defect that left the
      frame UNSHAPED at every frame thickness except the shipped default.
- [x] Add tests for X11 error paths that run in a subprocess because
      `WindowManager::fatal()` exits (`src/Manager.cpp:230`):
      no `DISPLAY`, invalid colors, missing font, and another WM already owning
      `SubstructureRedirectMask`.
      DONE (plan 08-13): `tests/test_wm_runtime.cpp` `[wm_errors]`, 7 cases,
      each with a deadline so a path that hangs is distinguishable from one that
      exits wrongly. The same plan added the `--help` flag, which did not exist
      while the unrecognised-option message advised using it.
- [x] Add tests for no-Shape fallback, or refactor shape calls behind a wrapper
      that can be forced unavailable in tests.
      DONE (plan 08-03): both, in fact. All 26 Shape calls now funnel through
      `Border::combineShape()`, and `tests/test_wm_fallbacks.cpp` `[wm_noshape]`
      asserts the WM issues **no Shape requests at all** without the extension --
      not merely that it survives.
- [x] Add tests for all `XSizeHints` resize constraints in
      `Client::fixResizeDimensions()` (`src/Client.cpp:1144`):
      min size, max size, base size, resize increments, fixed-size clients, and
      invalid/zero increments.
      DONE (plan 08-11): `[wm_sizehints]`, 7 cases. Found a client-triggerable
      divide-by-zero that killed the WM outright on `width_inc = 0`.
- [x] Add tests for all window gravity modes used by `Client::gravitate()`
      (`src/Client.cpp:702`).
      DONE (plan 08-11): `[wm_gravity]`.
- [x] Add tests for EWMH client messages handled in `WindowManager::eventClient()`
      (`src/Events.cpp:292`): `_NET_ACTIVE_WINDOW`, `_NET_WM_STATE` add/remove,
      toggle, and simultaneous two-property state changes.
      DONE (plan 08-12): `tests/test_wm_state.cpp` `[wm_state]`, 8 cases.
      Found and fixed a tampering defect -- state messages were applied to
      windows the WM had a `Client` for but had never managed.
- [x] Add tests for fullscreen and maximize geometry restore
      (`src/Client.cpp:270`, `src/Client.cpp:311`), including toggling while
      hidden, destroying while fullscreen, and dock workarea interactions.
      DONE (plan 08-12): `tests/test_wm_state.cpp` `[wm_fsmax]`, 7 cases.
      Closed deferred item 8 and fixed three further geometry defects.
- [x] Add tests for malformed client properties: wrong type, wrong format, empty
      arrays, oversized arrays, deleted properties, and absurd strut values.
      DONE (plan 08-12): `tests/test_wm_state.cpp` `[wm_props]`, 9 cases.
      Found a heap-buffer-overflow in the shared property reader and an
      invertible `_NET_WORKAREA`; both fixed.
- [x] Add repeated create/map/unmap/destroy stress tests under ASan for at least
      100 windows.
      DONE (plan 08-13): `tests/test_wm_runtime.cpp` `[wm_stress]`, 120 windows
      in three batches with overlapping unmap/destroy subsets. Found that
      `Client::unreparent()` discarded the WM's ENTIRE event queue on every
      client teardown, and that `Client::getColormaps()` installed unchecked
      (uninitialised) colormap XIDs; both fixed.

**All thirteen missing-coverage items above are now automated** (D-09):
items 1 and 3 in plan 08-01, item 7 in 08-03, items 2, 4, 8 and 9 in 08-11,
items 10, 11 and 12 in 08-12, and items 5, 6 and 13 in 08-13.

## Runtime Smoke Checklist

Use a nested server for manual and semi-automated checks:

```bash
Xephyr :2 -screen 1024x768 -ac
DISPLAY=:2 build/debug/wm2-born-again --new-window-command=xclock
```

Then, from another shell:

```bash
DISPLAY=:2 xclock &
DISPLAY=:2 xmessage "wm2 smoke" &
DISPLAY=:2 xprop -root _NET_SUPPORTING_WM_CHECK _NET_SUPPORTED _NET_CLIENT_LIST _NET_ACTIVE_WINDOW _NET_WORKAREA
DISPLAY=:2 xwininfo -root -tree
```

- [x] Startup claims the root window and logs no fatal errors.
      DONE: `[wm_harness]` / `[wm_errors]`, plus the committed Xephyr, XRDP and TigerVNC transcripts.
- [x] A second WM on the same display fails cleanly with the "another window
      manager running" path.
      DONE: plan 08-13, `[wm_errors]`: asserts the incumbent still works afterwards, not just that the second exits.
- [x] Normal top-level windows are framed with the wm2 sideways tab.
      DONE: `[wm_tablabel]`, `[wm_config_runtime]`; screenshots `shaped-frame-x11vnc.png` and the deferred-item-11 pair.
- [x] `override_redirect` windows are ignored.
      DONE: `[wm_lifecycle]`; also the mechanism behind `pumpWm()`'s inert nudge window in every process-level suite.
- [x] Dock and notification windows are not decorated.
      DONE: `[wm_rules]`, `[wm_process]`.
- [x] Existing windows present before WM startup are scanned and managed
      (`src/Manager.cpp:512`).
      DONE: `[wm_harness]`.
- [x] Root `_NET_SUPPORTED`, `_NET_SUPPORTING_WM_CHECK`,
      `_NET_NUMBER_OF_DESKTOPS`, `_NET_CURRENT_DESKTOP`, `_NET_WORKAREA`,
      `_NET_CLIENT_LIST`, and `_NET_ACTIVE_WINDOW` are correct after startup.
      DONE: `tests/test_ewmh.cpp`, `[wm_state]`, `[workarea]`; and the `root-properties.txt` transcript for each of the three exercised targets.
- [x] Creating, mapping, unmapping, remapping, and destroying normal clients does
      not crash and leaves root EWMH properties accurate.
      DONE: `[wm_lifecycle]` and `[wm_stress]` (120 windows, ASan).
- [x] SIGTERM, SIGINT, and SIGHUP wake the poll loop through the self-pipe and
      exit cleanly (`src/Events.cpp:130`, `src/Manager.cpp:269`).
      DONE: `[wm_errors]` "Every terminating path exits within a deadline", plus `[wm_norandr]`.
- [x] Shutdown restores/reparents managed clients and does not leave unusable
      windows behind.
      DONE: `[wm_lifecycle]`. Plan 08-13 fixed `unreparent()` discarding the WM's entire event queue here.

## User Interaction Checklist

- [x] Root left-click menu appears, fits on screen edges, highlights correctly,
      and unmaps after selection.
      DONE (plans 08-13, 08-14): `[wm_menulabel]`, `[wm_menureopen]`,
      `[wm_menuback]`, and the operator's two manual passes. The menu was rebuilt
      in `12f6de8`/`fa33f2e` to one grab and one loop after the operator found the
      outer menu went dead following a submenu episode.
      **Known limitation, not covered by this tick:** a category with very many
      entries produces a submenu taller than the screen with no scrolling and no
      cap — 202 of this host's 319 cached apps land in `Other`. The ROOT menu is
      clamped to the screen; its submenus are not.
- [x] Root menu `New` launches the configured command. Test both plain exec and
      shell mode:
      DONE (plan 08-13): `[wm_config_runtime]` covers `new-window-command` and
      runs a command with an argument and a shell metacharacter through
      `exec-using-shell` in BOTH settings, with a filesystem witness proving
      nothing is shell-evaluated when the flag is off (threat T-8-SHELL).

```bash
DISPLAY=:2 build/debug/wm2-born-again --new-window-command=xclock
DISPLAY=:2 build/debug/wm2-born-again --exec-using-shell --new-window-command="xmessage shell-ok"
```

- [ ] Root menu hidden-client entries restore hidden clients.
      **NOT COVERED.** `Client::hide()`/`unhide()` are tested directly
      (`[wm_lifecycle]`), but nothing drives the restore through the MENU ROW,
      which is the path a user takes. Remains open.
- [ ] Root menu exit item appears only at the lower-right screen edge and exits.
      **NOT COVERED.** No automated case, and the operator's second pass gave a
      general verdict rather than a per-item result for this one. Remains open.
- [x] Right-click root/window circulation works with zero clients, one client,
      hidden clients, transient clients, and multiple normal clients.
      PARTIAL → the zero-client case is automated
      (`tests/test_wm_process.cpp` `[wm_circulate]`, which found and fixed a
      100%-CPU spin), and the multi-client case was exercised in the manual
      passes. The hidden-client and transient-client permutations have no
      automated case; see the gap list in `08-14-SUMMARY.md`.
- [x] Clicking a tab/frame raises and focuses according to the selected focus
      policy.
      DONE (plan 08-07): `tests/test_wm_focus.cpp` `[wm_focus]`, six cases
      driving the real binary with real pointer events, covering click-to-focus,
      auto-raise and raise-on-focus in BOTH directions. All three were dead
      settings before that plan — they parsed and reached nothing.
- [ ] Dragging a tab moves a window and sends a correct synthetic
      `ConfigureNotify`.
      **PARTIAL — the move is covered, the ConfigureNotify is not.** Plan 08-14
      added a drag case (`[wm_geometry]`) after the operator lost a window off
      the top-left; it asserts the resulting geometry but nothing asserts the
      synthetic `ConfigureNotify` that `Client::move()` sends. Remains open.
- [x] Dragging resize handles resizes normally and respects constrained
      horizontal/vertical resize paths.
      DONE (plan 08-11): `[wm_sizehints]`, 7 cases over
      `fixResizeDimensions()`, which found a client-triggerable divide-by-zero.
- [x] Tab button short press hides; long press after `destroy-window-delay` sends
      delete/kill behavior and restores the cursor.
      DONE (plan 08-13): `[wm_config_runtime]` asserts both halves — a
      hide-always build and a delete-always build each fail one of them. Plan
      08-14 added `[wm_button]`, which widened the button's target from 8x8 to
      the tab's whole top square after the operator reported it as too demanding
      to hit.
- [ ] Middle-click tab toggles maximize.
      **NOT COVERED at the gesture.** The maximize STATE is covered thoroughly
      (`[wm_fsmax]`, `[ewmh][state]`), but via EWMH client messages; no case
      middle-clicks a tab. Remains open.
- [ ] Right-button circular gesture toggles fullscreen and ignores short/noisy
      gestures.
      **NOT COVERED.** `detectFullscreenGesture()` has no automated case at all,
      including the "ignores short/noisy gestures" half. Remains open.
- [ ] Pointer grabs are always released after cancel, extra button press, escape
      paths, or client destroy during interaction.
      **NOT COVERED as a property.** The `[servergrab]` cases are about
      `XGrabServer`, a different thing. Plan 08-14 hardened this area
      substantially — `attemptGrab()` took `int` for a 32-bit unsigned timestamp,
      so every grab silently failed past ~24.8 days of server uptime, and the
      menu's two-grab structure was collapsed to one — but no test asserts the
      release property across cancel and destroy paths. Remains open.

## ICCCM And EWMH Checklist

- [x] `WM_STATE` uses exact X11 values: Withdrawn 0, Normal 1, Iconic 3.
      DONE: `ClientState` pins Iconic to 3 (not 2) with the reason in `include/Client.h`; asserted in `[wm_lifecycle]`.
- [x] `WM_PROTOCOLS` honors `WM_DELETE_WINDOW` and `WM_TAKE_FOCUS`.
      DONE: `[wm_config_runtime]` long-press delete, `[wm_focus]` take-focus.
- [ ] `WM_TRANSIENT_FOR` handles normal transients, self-referencing transients,
      and transient raise ordering.
      **PARTIAL.** The self-referencing case is guarded in `Client::getTransient()`
      and warns rather than looping, but there is no automated case for transient
      raise ordering. Remains open.
- [ ] `WM_COLORMAP_WINDOWS` updates and installs colormaps in the expected order.
      **PARTIAL.** `tests/test_client.cpp` covers the property READ; nothing
      asserts install ORDER. Plan 08-13 did fix `getColormaps()` installing
      unchecked, uninitialised colormap XIDs, found by `[wm_stress]` under ASan.
      Remains open.
- [x] `_NET_CLIENT_LIST` includes normal and hidden managed clients and removes
      clients immediately on destroy.
      DONE: `[wm_lifecycle]`. Plan 08-14 fixed the WM listing its OWN menu, submenu and check windows here (deferred item 7) -- the override-redirect flag has to be in the `XCreateWindow` valuemask, since `CreateNotify` carries the creation-time value.
- [x] `_NET_ACTIVE_WINDOW` updates on activation and returns to `None` when focus
      is cleared.
      DONE: `[wm_state]`, `[wm_focus]`.
- [x] `_NET_WM_WINDOW_TYPE` handles at least normal, dock, dialog, notification,
      utility, splash, and toolbar.
      DONE: `[wm_rules]`, `[wm_props]`.
- [x] `_NET_WM_STATE` add/remove/toggle works for fullscreen, maximized vertical,
      maximized horizontal, and hidden.
      DONE: plan 08-12, `[wm_state]`, 8 cases. Found state messages being applied to windows the WM had a `Client` for but had never managed.
- [x] `_NET_WORKAREA` subtracts dock struts, prefers `_NET_WM_STRUT_PARTIAL` over
      `_NET_WM_STRUT`, and clamps values to the screen.
      DONE: `[workarea]`, `[wm_props]`. Plan 08-12 found an INVERTIBLE workarea reachable by a hostile dock (threat T-8-STRUT).
- [x] Fullscreen covers the whole screen including dock areas; maximized windows
      use workarea and keep the frame/tab.
      DONE: plan 08-12, `[wm_fsmax]`, 7 cases; closed deferred item 8 and three further geometry defects.

## Rendering And Geometry Checklist

- [x] Xft menu font and rotated tab font load through the configured fallback
      chains.
      DONE: plan 08-06, `tests/test_wm_fallbacks.cpp`: all four rungs of the tab-font ladder are reachable and none can terminate the process, plus `[xft_norender_spike]` measuring identical metrics with XRender absent.
- [x] UTF-8 labels render without corrupting text or crashing.
      DONE: `[xft][utf8]`, `[wm_tablabel]`.
- [x] Long names fall back to icon name and then truncate with ellipsis without
      negative dimensions (`src/Border.cpp:295`).
      DONE: `[wm_tablabel]` "A long title on a short window is shortened to fit its tab, still legibly".
- [x] Tab, button, border, and menu colors apply from config and invalid colors
      fail with a clear message.
      DONE: plan 08-13, `[wm_config_runtime]` (tab and menu colours sampled from real pixels) and `[wm_errors]` (an unparseable colour exits non-zero and NAMES the offending setting).
- [x] Frame thickness works at minimum, default, and maximum configured values.
      DONE (plan 08-13): asserted absolutely (`yIndent() == FRAME_WIDTH + 1`) at
      3, the shipped 7 and 20. This is where the SHAPE ordering defect surfaced:
      every thickness outside a narrow band produced a `BadMatch` and an
      unshaped frame.
- [x] Shaped frames render correctly when Shape is available.
      DONE: `[shape]`, `[shape_invariant]`; screenshot `evidence/screenshots/shaped-frame-x11vnc.png`.
- [x] Rectangular fallback renders and operates correctly when Shape is not
      available, without issuing Shape extension requests.
      DONE: plan 08-03, `[wm_noshape]`; screenshot `evidence/screenshots/rectangular-fallback-no-shape.png`.
- [x] Moving/resizing keeps windows visible on screen and handles small screens.
      DONE: plan 08-14, `[wm_geometry]`. This was FALSE until `a5a105c`: a window dragged off the top-left took its own tab -- the only pointer handle -- off screen with it and could not be recovered. Found by the operator on a live session at frame (-699,-618).
- [x] Single-screen behavior is verified. Multi-screen is not supported; if it is
      out of scope, document that explicitly in release notes.
      DONE: single-screen verified by `[wm_geometry]` and the RANDR reflow cases;
      multi-screen is documented as out of scope in the release notes.

## Robustness And Security Checklist

- [x] No ASan/UBSan reports in full automated and runtime smoke tests.
      DONE: `evidence/gates/build-all-asan.log`: 295/295, "no sanitizer findings". The six files under `evidence/gates/sanitizer-reports/` are LSan suppression-ACCOUNTING logs (2 allocations, 288 bytes, libfontconfig), copied in full so the distinction can be checked.
- [x] No steady CPU spin while idle, while waiting for auto-raise timers, or while
      menus/grabs are active.
      DONE: `[wm_resource_budget]` measures idle CPU at 0 ticks over 30s; `[wm_focus]` covers the auto-raise-off case; `[wm_circulate]` was added after plan 08-07 found `circulate()` spinning at 100% CPU.
- [x] Repeated `spawn()` calls do not leave zombie processes
      (`src/Manager.cpp:770`).
      DONE (plan 08-13): asserted at the end of the `[wm_stress]` churn case --
      three menu `New` selections, then zero zombie children of the WM.
- [x] `exec-using-shell` is off by default and documented as shell-evaluated
      behavior. Commands with arguments require shell mode or an intentionally
      implemented argv parser.
      DONE (plan 08-13): `[wm_config_runtime]` runs one command containing both
      an argument and a shell metacharacter through both settings, with a
      filesystem witness proving nothing at all is evaluated by a shell when the
      flag is off (threat T-8-SHELL). `--help` now documents the flag as
      shell-evaluated.
- [x] Config files reject overlong lines and values as tested, and unknown keys
      warn without aborting.
      DONE: `tests/test_config.cpp`, 48 cases.
- [x] Client-supplied properties cannot crash the WM by using wrong atom types,
      wrong formats, deleted properties, oversized arrays, invalid UTF-8, or
      absurd dimensions.
      DONE: plan 08-12, `[wm_props]`, 9 cases. Found a HEAP-BUFFER-OVERFLOW in the shared property reader (`getProperty_aux()` validated neither type nor format) and an invertible `_NET_WORKAREA`; both fixed.
- [ ] `ignoreBadWindowErrors` is scoped tightly around cleanup and does not mask
      unrelated protocol errors.
      **NOT COVERED as a property.** Plan 08-11's protocol-error assertion is the
      closest thing and is genuinely load-bearing -- it filters `BadWindow` and
      fails on anything else, which is what caught the `RenderBadPicture` -- but
      nothing asserts the FLAG's scope, i.e. that it is not left set across
      unrelated work. Remains open.
- [x] Display, GC, cursor, pixmap, colormap, Xft font, Xft draw, and Xft color
      resources are freed before their owning `Display` closes.
      DONE: `tests/test_raii.cpp` (16 cases) plus the ASan tree. Plan 08-11 found the concrete instance of this: the per-instance `XftDraw` was destroyed AFTER its tab window, emitting `RenderBadPicture` on every window close.

## Remote Desktop And VPS Checklist

- [x] Verify on the target baseline distro, especially Ubuntu 22.04+.
      DONE: Ubuntu 22.04+ host; versions recorded in
      `evidence/gates/preflight-versions.log`.
- [ ] Verify under Xvfb, Xephyr, one VNC server, and one XRDP session if those are
      supported deployment targets.
      **PARTIAL — three of four, and X2Go is a named target that was NOT done.**
      Xvfb (the whole automated suite), Xephyr (`evidence/local-xephyr/`),
      TigerVNC and XRDP are all exercised with committed transcripts. X2Go has no
      session and no transcript: `x2goserver` was never installed. The operator
      declared the manual pass sufficient, which is a decision to stop, not
      evidence that X2Go works. See D-8-X2GO below.
- [x] Verify a low-resource run: 512 MB RAM target, multiple simple windows,
      repeated open/close cycles, and idle CPU near zero.
      DONE (plan 08-14): `[wm_resource_budget]` enforces it rather than reporting
      it — measured RSS 11840 kB against a 24576 kB budget, idle CPU 0 ticks over
      30s. Derivation in `evidence/resource-budget/BUDGET-DERIVATION.md`. The
      churn half is `[wm_stress]`, 120 windows.
- [ ] Verify behavior over remote latency: menus, move/resize, auto-raise, and
      long-press delete remain usable.
      **PARTIAL, and this is where the manual passes earned their keep.** Menus
      and move/resize were exercised over real XRDP and TigerVNC sessions, and
      three defects came out of it that no automated test had found: the menu
      going dead after a submenu episode, a window draggable off-screen beyond
      recovery, and an 8x8 close-button target. Auto-raise and long-press delete
      over latency were not separately exercised. No latency measurement was
      taken. Remains open.
- [x] Verify common remote desktop X servers expose the required Xft/fontconfig
      behavior and either support Shape or pass the rectangular fallback test.
      DONE for the two exercised targets: SHAPE, RANDR and RENDER are all present
      in both `evidence/xrdp/capabilities.txt` and
      `evidence/tigervnc/capabilities.txt`. The no-Shape path is covered
      automatically by `[wm_noshape]` rather than by finding a server without it.

## Release Evidence Required

For each release or handoff, attach:

- [x] Commit hash and branch.
      DONE: `evidence/gates/PROVENANCE.txt`.
- [x] Dependency/package list and tool versions.
      DONE: `evidence/gates/preflight-versions.log`.
- [x] Debug, Release, and sanitizer build logs.
      DONE: `evidence/gates/build-all-{debug,release,asan}.log` plus `compiler-{debug,release}.log`.
- [x] Full `ctest --output-on-failure` logs.
      DONE: in the three build-all logs; 295/295 in every tree.
- [x] Runtime smoke transcript with `xprop -root` and `xwininfo -root -tree`
      output.
      DONE: `evidence/local-xephyr/` and the three per-target directories.
- [x] Screenshots from shaped and rectangular/no-Shape runs if supported.
      DONE: `evidence/screenshots/`, seven images including the shaped and rectangular pair.
- [x] ASan/UBSan logs showing no actionable findings.
      DONE: `evidence/gates/build-all-asan.log` and `evidence/gates/sanitizer-reports/`.
- [ ] Manual interaction checklist results with tester name and date.
      **PARTIAL.** Two manual passes were run by the operator (tpatarci) on
      2026-08-29 and 2026-08-30, and their findings and dispositions are recorded
      in `.continue-here.md` and in `08-14-SUMMARY.md`. What does NOT exist is a
      per-item pass/fail table: the second pass produced a general verdict ("all
      works like a charm"), which is not the same as a result for each row of the
      User Interaction Checklist. The unticked rows in that section above are the
      honest consequence. Remains open.
- [x] List of accepted deviations, each with owner and follow-up issue.
      DONE: D-8-TIGHTVNC and D-8-X2GO below, each with reason, owner and
      follow-up.

## Accepted Deviations

A deviation belongs here only with a **reason**, an **owner** and a
**follow-up**. Without all three it is not an accepted deviation, it is an
omission wearing the word "accepted".

### D-8-TIGHTVNC — TightVNC is not validated for this release

**What is not done.** `PROJECT.md` names TigerVNC, TightVNC, XRDP and X2Go as the
remote-desktop targets. TightVNC has **no session, no transcript and no
interaction record**. It is untested.

**Rationale.** TigerVNC is TightVNC's maintained successor on the Unix server
side, and both descend from the same `Xvnc` codebase. Their X server behaviour —
the extension set advertised, Shape handling, resize handling, the fontconfig
picture — substantially overlaps, so a TigerVNC result is genuine evidence about
TightVNC rather than a guess. It is not, however, the same thing as a TightVNC
result, which is why this is recorded as a deviation and not quietly ticked.

**Closest tested proxy: TigerVNC.** Its capability transcript and interaction
record are the best available evidence for TightVNC's behaviour, and both are
committed under the phase evidence directory alongside the other targets.

**Owner:** whoever next stands up a remote-desktop validation session — the same
role that runs the User Interaction Checklist for a release. TightVNC needs no
new tooling: `scripts/capture-display-capabilities.sh <display> tightvnc` and the
existing interaction checklist are the whole job.

**Follow-up:** add a `tightvnc` transcript to the evidence bundle at the next
release signoff that has a TightVNC server available, and either delete this
entry or restate it with a fresh reason. An accepted deviation that is never
revisited becomes a permanent gap by default, which is the failure mode this
section exists to prevent.

**What a user should expect meanwhile:** if TightVNC misbehaves, that is a real
bug worth reporting. The configuration is not unsupported; it has not been
exercised.

### D-8-X2GO — X2Go is not validated for this release

**What is not done.** `PROJECT.md` names X2Go as a remote-desktop target. It has
**no session, no transcript and no interaction record**. `x2goserver` was never
installed on the validation host. It is untested.

**Rationale.** This is a weaker position than D-8-TIGHTVNC, and it should not be
read as the same kind of gap. TightVNC at least has a close relative under test:
TigerVNC shares its `Xvnc` ancestry, so a TigerVNC transcript is genuine evidence
about it. X2Go has no such proxy here. Its X server is `nxagent`, a different
codebase from both `Xvnc` and `xrdp`'s backends, with its own compression proxy
in front — the very layer most likely to differ on the things this phase cares
about, namely the Shape and RANDR extension surface and the RENDER path the tab
font depends on. Nothing in the TigerVNC or XRDP results transfers to it.

The operator's decision was "I am happy with the testing as it is. I declare it
sufficient for now." That is a decision to stop testing, which is theirs to make.
It is not evidence that X2Go works, and it must never be recorded as one.

**Owner:** whoever next stands up a remote-desktop validation session — the same
role that runs the User Interaction Checklist for a release.

**Follow-up:** `apt install x2goserver`, then
`scripts/capture-display-capabilities.sh <display> x2go` and the interaction
checklist. Add the transcript to the evidence bundle at the next signoff and
either delete this entry or restate it with a fresh reason.

**What a user should expect meanwhile:** X2Go is unexercised, not unsupported. If
the sideways tab renders wrongly or the frame is unshaped under `nxagent`, that
is a real bug worth reporting — and given the RENDER dependency, it is the
likeliest place for one to be hiding.
