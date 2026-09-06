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
      committed at `08.5-v1.0-closeout/evidence/gates/preflight-versions.log`. It
      runs as the `preflight` ctest fixture, so a signoff cannot be produced
      without it.
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
      `08.5-v1.0-closeout/evidence/gates/preflight-versions.log`.

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
      **NOT RESOLVED, and deliberately left open rather than ticked.**
      `CMakeLists.txt` still fetches Catch2 v3.14.0 from GitHub at configure time,
      so a first configure needs network access. There is no CI here by project
      policy — every gate runs locally — so this is a *developer-onboarding*
      dependency, not a CI one, which is a smaller problem than the row assumes
      but not the same as no problem. Remains open.
- [x] Ensure fixed Xvfb display `:99` is free, or update the test harness to
      allocate a free display.
      DONE. The harness allocates its own display per fixture instance rather
      than depending on `:99` being free (`tests/support/WmFixture.h`).

## Build And Test Gates

- [x] Configure a clean Debug build with tests enabled:
      DONE. `scripts/gates/build-all.sh debug`; log at
      `08.5-v1.0-closeout/evidence/gates/build-all-debug.log`, exit 0 (confirmed
      in `08.5-v1.0-closeout/evidence/gates/PROVENANCE.txt` at `39de548`).

```bash
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
```

- [x] Build without compiler or linker warnings that are new for the change:
      DONE. Zero warning lines recorded in all three trees; see
      `08.5-v1.0-closeout/evidence/gates/compiler-debug.log`,
      `08.5-v1.0-closeout/evidence/gates/compiler-release.log`, and
      `08.5-v1.0-closeout/evidence/gates/compiler-asan.log`, and the
      "0 compiler/linker warning line(s)" line in each of
      `08.5-v1.0-closeout/evidence/gates/build-all-debug.log`,
      `08.5-v1.0-closeout/evidence/gates/build-all-release.log` and
      `08.5-v1.0-closeout/evidence/gates/build-all-asan.log`.

```bash
cmake --build build/debug --parallel
```

- [x] Run the full CTest suite:
      DONE. **337/337 (100% tests passed, 0 tests failed) in all three trees**
      at `39de548` (`08.5-v1.0-closeout/evidence/gates/PROVENANCE.txt`); full
      logs in `08.5-v1.0-closeout/evidence/gates/build-all-debug.log`,
      `08.5-v1.0-closeout/evidence/gates/build-all-release.log` and
      `08.5-v1.0-closeout/evidence/gates/build-all-asan.log`.

```bash
ctest --test-dir build/debug --output-on-failure
```

- [x] Confirm the expected discovered test surface is present. The tree contains
      **335 Catch2 test cases** across 23 files, of which **334 are registered
      with ctest** (`Total Tests: 337` including the `preflight`, `start_xvfb`
      and `stop_xvfb` fixture tests). RECOMPUTED at `39de548`, not transcribed.
      The arithmetic: 335 source cases − 1 hidden case = 334 registered Catch2
      cases, + 3 fixture tests = 337 `Total Tests`. The one case not registered
      is the hidden `[.][wm_resource_calibration]` case in
      `tests/test_wm_resource.cpp`, which is hidden on purpose so the judged
      resource budget cannot have been produced by a run that was itself being
      judged.

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

      Per file, at the time of writing (plan 08.5-05, commit `39de548`):

      | File | Cases | | File | Cases |
      |---|---|---|---|---|
      | `test_config.cpp` | 48 | | `test_desktopentry.cpp` | 12 |
      | `test_wm_runtime.cpp` | 32 | | `test_client.cpp` | 11 |
      | `test_rules.cpp` | 25 | | `test_appcache.cpp` | 8 |
      | `test_wm_state.cpp` | 24 | | `test_binaryscanner.cpp` | 8 |
      | `test_wm_focus.cpp` | 21 | | `test_smoke.cpp` | 7 |
      | `test_wm_lifecycle.cpp` | 17 | | `test_autoraise.cpp` | 6 |
      | `test_wm_geometry.cpp` | 17 | | `test_wm_process.cpp` | 6 |
      | `test_ewmh.cpp` | 16 | | `test_xft_poc.cpp` | 6 |
      | `test_raii.cpp` | 16 | | `test_menupaint.cpp` | 5 |
      | `test_wm_rules.cpp` | 18 | | `test_wm_resource.cpp` | 4 |
      | `test_wm_fallbacks.cpp` | 13 | | `test_wm_repro.cpp` | 2 |
      | `test_eventloop.cpp` | 13 | | | |

      Moved since the 08-14 table (293 cases, 21 files): `test_rules.cpp` 17 ->
      25 (the title criterion and match-key rename, plan 08.5-01, "8 new
      `[rules]` cases, 17 -> 25"); `test_wm_rules.cpp` 8 -> 18 (+5 from plan
      08.5-01's fold wiring, "9 -> 14" per its own summary — which does not
      square with the 08-14 table's 8, itself a sign of how easily this figure
      drifts — plus 5 further cases from the Codex review-fixes rounds — three on PR #5's findings, two from the branch reviews before PR #6 —
      `08.5-v1.0-closeout/evidence/gates/review-fixes/README.md`, a dated round rather than a
      numbered plan); `test_wm_runtime.cpp` 23 -> 32 (+9: at least 2 are named in
      the same review-fixes round — the no-decorate click case and the submenu
      overflow case — the remainder spans plans 08.5-06/08.5-11/08.5-12/08.5-13's
      menu and runtime work and is not separable file-count-by-file-count from
      the SUMMARY files; closeout round, plan not determined for that
      remainder); `test_eventloop.cpp` 5 -> 13 (+8: four cases each from plan
      08.5-11 and plan 08.5-12); and two new files, `test_wm_repro.cpp` (2 cases,
      plan 08.5-09) and `test_menupaint.cpp` (5 cases, plan 08.5-13). Every
      other file's count is unchanged from the 08-14 table.

      A count that has drifted is not automatically a defect — a plan that adds
      cases moves it legitimately. It is a prompt to find out *which* file
      changed and why.
- [x] Run a Release build and tests:
      DONE. `08.5-v1.0-closeout/evidence/gates/build-all-release.log`, exit 0,
      100% tests passed, 0 tests failed out of 337. The release gate also runs
      the link audit below (24 entries, all in the intended runtime set).

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure
```

- [x] Run an AddressSanitizer/UBSan build. Treat use-after-free, invalid enum
      use, double-free, and out-of-bounds reports as blockers:
      DONE. `08.5-v1.0-closeout/evidence/gates/build-all-asan.log`, exit 0, 100%
      tests passed, 0 tests failed out of 337, "no sanitizer findings". The six
      files in `08.5-v1.0-closeout/evidence/gates/sanitizer-reports/` are LSan
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
      `08.5-v1.0-closeout/evidence/gates/static-analysis.log`. cppcheck 2.7:
      15 finding(s), all accounted for in the baseline (keyed on
      `(id, fileName)`, since cppcheck 2.7 never computes the hash element).
      clang-tidy 14.0.0: no enforced check fired.

```bash
cppcheck --enable=all --std=c++17 -Iinclude src include
clang-tidy -p build/debug src/*.cpp
```

- [x] Verify the final executable links only to intended runtime libraries:
      DONE. `08.5-v1.0-closeout/evidence/gates/ldd-release.txt`. **libXtst does
      not appear** — XTEST is linked into test targets only, which is the
      constraint that keeps the shipped binary within the 512MB VPS budget.

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

- [x] `wm2-ctl` completes each of its four subcommands against a running window
      manager and returns its documented exit code when none is running.
      DONE (plan 09-04): `[wm_config_live]` covers all four against the real
      binary over a real socket -- "wm2-ctl status prints the seven fields the
      window manager reports", "wm2-ctl get prints the value the window manager
      is actually using", "set frame-thickness re-frames a window that was
      already mapped", and "reload re-reads the file and applies it to windows
      already open". The three non-zero codes have their own cases: "wm2-ctl
      exits 2 when there is no window manager to talk to", "wm2-ctl refuses a
      malformed invocation with the usage code" (3), and "a value the parser
      would clamp is refused and changes nothing" (1). "the settable key list
      and the config file's managed key list agree" is what keeps `--help` from
      advertising a key the window manager would refuse.
- [x] The socket path is discoverable without reconstructing it: the root
      window carries `_WM2_CONFIG_SOCKET`.
      DONE (plan 09-03): `[wm_socket]` "The socket path is published on the root
      window and is a socket", and `[wm_config_live]` "wm2-ctl finds the socket
      from DISPLAY, and honours --socket" for the client half. Reading the
      property is the documented way in; the path's spelling is written down in
      `docs/RELEASE-NOTES.md` only so a second client can recognise it, not so
      anyone reconstructs it.

```bash
DISPLAY=:2 xprop -root _WM2_CONFIG_SOCKET
DISPLAY=:2 wm2-ctl status
```

## User Interaction Checklist

- [x] Root left-click menu appears, fits on screen edges, highlights correctly,
      and unmaps after selection.
      DONE (plans 08-13, 08-14): `[wm_menulabel]`, `[wm_menureopen]`,
      `[wm_menuback]`, and the operator's two manual passes. The menu was rebuilt
      in `12f6de8`/`fa33f2e` to one grab and one loop after the operator found the
      outer menu went dead following a submenu episode. Confirmed per-item:
      `evidence/INTERACTION-CHECKLIST.md` row 1 — **PASS**, "Including the
      bottom-right edge case" (tpatarci, 2026-08-30).
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
      Confirmed per-item: `evidence/INTERACTION-CHECKLIST.md` row 2 — **PASS**,
      "xclock appeared, framed"; row 3 — **n/a**, plain exec is walked, shell
      mode needs a WM restart and is covered automatically instead (tpatarci,
      2026-08-30).

```bash
DISPLAY=:2 build/debug/wm2-born-again --new-window-command=xclock
DISPLAY=:2 build/debug/wm2-born-again --exec-using-shell --new-window-command="xmessage shell-ok"
```

- [ ] Root menu hidden-client entries restore hidden clients.
      **NOT COVERED.** `Client::hide()`/`unhide()` are tested directly
      (`[wm_lifecycle]`), but nothing drives the restore through the MENU ROW,
      which is the path a user takes. `evidence/INTERACTION-CHECKLIST.md` row
      4 records the disposition: **DEFERRED**, "no automated coverage either" —
      declined by the operator (tpatarci, 2026-08-30): *"We are not running the
      seven. It is deferred."* This is a recorded decision, not an unrecorded
      gap; the follow-up is the v1.1 "Gesture and input coverage" backlog line,
      not a re-run of this checklist. Remains open.
- [ ] Root menu exit item appears only at the lower-right screen edge and exits.
      **NOT COVERED.** No automated case. `evidence/INTERACTION-CHECKLIST.md`
      row 5 records the disposition: **DEFERRED**, "no automated coverage
      either" — declined by the operator (tpatarci, 2026-08-30), not merely a
      general verdict standing in for a per-item result as this row previously
      said; a per-item table now exists and this is its recorded answer for
      this item. Remains open.
- [ ] Right-click root/window circulation works with zero clients, one client,
      hidden clients, transient clients, and multiple normal clients.
      **PARTIAL —** the zero-client case is automated
      (`tests/test_wm_process.cpp` `[wm_circulate]`, which found and fixed a
      100%-CPU spin). `evidence/INTERACTION-CHECKLIST.md` rows 6-10 record the
      five permutations: row 6 (zero clients) — **DEFERRED**, but automated by
      `[wm_circulate]`; row 7 (one client) — **DEFERRED**; row 8 (several
      normal clients) — **DEFERRED**, exercised in the Phase 8 manual passes
      but not automated; rows 9-10 (hidden client, transient/dialog) —
      **DEFERRED**, "no automated coverage either". All five declined by the
      operator (tpatarci, 2026-08-30) in the same sitting. Remains open.
- [x] Clicking a tab/frame raises and focuses according to the selected focus
      policy.
      DONE (plan 08-07): `tests/test_wm_focus.cpp` `[wm_focus]`, six cases
      driving the real binary with real pointer events, covering click-to-focus,
      auto-raise and raise-on-focus in BOTH directions. All three were dead
      settings before that plan — they parsed and reached nothing. Confirmed
      per-item: `evidence/INTERACTION-CHECKLIST.md` row 11 — **PASS (verdict)**,
      not walked individually but covered by the operator's own statement and
      by `[wm_focus]` (tpatarci, 2026-08-30).
- [ ] Dragging a tab moves a window and sends a correct synthetic
      `ConfigureNotify`.
      **PARTIAL — the move is covered, the ConfigureNotify is not.** Plan 08-14
      added a drag case (`[wm_geometry]`) after the operator lost a window off
      the top-left; it asserts the resulting geometry but nothing asserts the
      synthetic `ConfigureNotify` that `Client::move()` sends.
      `evidence/INTERACTION-CHECKLIST.md` rows 12-13 confirm the move half by
      walking it: row 12 — **PASS**, "name tab always remains available"; row
      13 — **PASS**, the `a5a105c` off-screen-drag clamp — **the operator's own
      Phase 8 defect report, confirmed fixed by the person who filed it**
      (tpatarci, 2026-08-30). The ConfigureNotify half has no table row and no
      automated case either, which is why this row stays open on that half
      alone.
- [x] Dragging resize handles resizes normally and respects constrained
      horizontal/vertical resize paths.
      DONE (plan 08-11): `[wm_sizehints]`, 7 cases over
      `fixResizeDimensions()`, which found a client-triggerable divide-by-zero.
      Confirmed per-item: `evidence/INTERACTION-CHECKLIST.md` row 14 —
      **PASS**; row 15 — **PASS**, constrained horizontal-only and
      vertical-only both walked (tpatarci, 2026-08-30).
- [x] Tab button short press hides; long press after `destroy-window-delay` sends
      delete/kill behavior and restores the cursor.
      DONE (plan 08-13): `[wm_config_runtime]` asserts both halves — a
      hide-always build and a delete-always build each fail one of them. Plan
      08-14 added `[wm_button]`, which widened the button's target from 8x8 to
      the tab's whole top square after the operator reported it as too demanding
      to hit. Confirmed per-item: `evidence/INTERACTION-CHECKLIST.md` row 16 —
      **PASS**; row 17 — **PASS** ("Produced a finding — see below" in that
      table, the `destroy-window-delay` finding recorded there); row 18 —
      **PASS**, "feel is just right" — the `43fb24b` widening from 8x8 to the
      tab's whole top square, **the operator's own Phase 8 defect report,
      confirmed fixed by the person who filed it** (tpatarci, 2026-08-30).
- [ ] Middle-click tab toggles maximize.
      **NOT COVERED at the gesture.** The maximize STATE is covered thoroughly
      (`[wm_fsmax]`, `[ewmh][state]`), but via EWMH client messages; no case
      middle-clicks a tab. `evidence/INTERACTION-CHECKLIST.md` row 19 records
      the disposition: **DEFERRED**, "Gesture has no automated coverage; the
      *state* is covered by `[wm_fsmax]`" — declined at the gesture by the
      operator (tpatarci, 2026-08-30); the state it would produce is automated,
      the gesture that triggers it is not. Remains open.
- [ ] Right-button circular gesture toggles fullscreen and ignores short/noisy
      gestures.
      **NOT COVERED.** `detectFullscreenGesture()` has no automated case at all,
      including the "ignores short/noisy gestures" half.
      `evidence/INTERACTION-CHECKLIST.md` rows 20-21 record the disposition:
      **DEFERRED**, "`detectFullscreenGesture()` has zero coverage of any
      kind" — declined by the operator (tpatarci, 2026-08-30); the function has
      no coverage of any kind, automated or manual. Remains open.
- [ ] Pointer grabs are always released after cancel, extra button press, escape
      paths, or client destroy during interaction.
      **NOT COVERED as a property.** The `[servergrab]` cases are about
      `XGrabServer`, a different thing. Plan 08-14 hardened this area
      substantially — `attemptGrab()` took `int` for a 32-bit unsigned timestamp,
      so every grab silently failed past ~24.8 days of server uptime, and the
      menu's two-grab structure was collapsed to one — but no test asserts the
      release property across cancel and destroy paths.
      `evidence/INTERACTION-CHECKLIST.md` row 22 records the disposition:
      **DEFERRED**, "No automated coverage as a property" — declined by the
      operator (tpatarci, 2026-08-30). Remains open.

### The configuration GUI (Phase 9)

- [x] The root menu's `Configure...` entry appears when `wm2-config` is
      installed and is absent when it is not.
      DONE (plan 09-09): `[wm_config_runtime]` "The root menu carries a
      Configure entry when wm2-config is on the window manager's PATH at
      startup, and not when it is not" runs two window managers whose child
      environments differ in one variable -- one with a directory holding the
      real built `wm2-config` on PATH, one without -- and measures the second
      menu one row shorter. There is no readable label channel, so the row count
      is derived from the popup height the server reports and the row height
      read off the highlight band. The claim the runtime case cannot make --
      that every OTHER row, the exit row included, is at the index it already
      had -- is pinned by five display-free `[menupaint]` cases over
      `RootMenuLayout`, shown to fail when Configure and Exit are swapped.
- [x] Selecting `Configure...` launches the settings window and leaves no
      zombie behind.
      DONE (plan 09-09): `[wm_config_runtime]` "Selecting the root menu's
      Configure entry runs wm2-config and leaves no zombie behind". The entry
      goes through `spawnArgv()`, the same double-fork the `New` entry uses, so
      the reaping guarantee asserted at the end of `[wm_stress]` covers it with
      no new process code. The case asserts a filesystem witness written by a
      program named `wm2-config` found on the window manager's own PATH, and
      then zero zombie children.
      **What this row does not cover:** the case launches a shim rather than the
      GTK binary, deliberately -- what is under test is that the entry execs a
      program of that name through the window manager's spawn path, not that
      GTK can open a window, which is `[wm2_config_smoke]`'s subject and is
      covered separately below.
- [x] The settings window's three pages open and edit what they say they edit.
      DONE (plans 09-06, 09-07): `[wm2_config_smoke]`, which opens the real
      built binary under Xvfb and asserts its window and connection state, and
      drives every page's own model for the rest -- the Appearance and Behaviour
      pages' key partition, the Menu page's rows across a save, and the
      click-to-focus tracer, which proves a value committed through the page
      changes how the running desktop gives focus to synthesised pointer input
      rather than merely being stored. Screenshots of all three pages and both
      dialogs: `.planning/phases/09-config-gui-ipc/evidence/wm2-config/`.
- [ ] Two settings windows open at once do not overwrite each other's Save.
      **NOT COVERED, and known.** Flagged in 09-06 and carried unclosed through
      09-07: each instance writes the whole user file from its own picture, so
      the second Save silently discards the first. Nothing in the phase gates
      it. Cheap to close with an advisory lock on the user file at Save; a v1.1
      candidate rather than a v1.0 blocker, because the situation needs a user
      to open the settings window twice on one desktop. Remains open.
- [ ] The `Reset this page` button is reachable without scrolling on every page.
      **NOT COVERED, and known.** 09-07 records that the Appearance page is
      taller than the default window, so its reset button sits below the fold;
      the Behaviour and Menu pages show theirs. No automated case asserts
      control visibility at the default size, and none is proposed -- this is a
      layout judgement for the operator's screenshot review. Remains open.

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
      DONE: `08.5-v1.0-closeout/evidence/gates/build-all-asan.log`: 100% tests
      passed, 0 tests failed out of 337, "no sanitizer findings". The six files
      under `08.5-v1.0-closeout/evidence/gates/sanitizer-reports/` are LSan
      suppression-ACCOUNTING logs (2 allocations, 288 bytes, libfontconfig),
      copied in full so the distinction can be checked.
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

### The configuration socket (Phase 9)

- [x] The socket is reachable by the owning uid only; a foreign uid is refused
      and logged once.
      DONE (plan 09-03): `[wm_socket]` "The socket directory is 0700 and the
      socket is 0600" and "A refused peer is closed before a protocol byte and
      logged once per uid"; the unit half is `[config_socket]` "A peer with this
      uid is admitted and any other uid is not" and "A descriptor the kernel
      will not vouch for is not admitted". The credential check happens before a
      single byte is read, which is what makes "refused" mean refused rather
      than parsed and then rejected.
- [x] An oversized or malformed message is refused and the window manager keeps
      running.
      DONE (plan 09-03): `[wm_socket]` "A line over the protocol bound is
      refused and the connection closed", "A client that sends bytes and never a
      newline is dropped at the bound", and "A client vanishing mid-request
      costs only its own connection". The protocol half is
      `[config_protocol]`, 09-02's frozen wire contract.
- [x] A stale socket left by a crashed window manager is reclaimed.
      DONE (plan 09-03): `[wm_socket]` "A socket left by a crashed predecessor
      is reclaimed", with `[config_socket]` "An abandoned socket is stale and a
      live one is not", "Nothing at the path is nothing to reclaim" and "A file
      that is not a socket is never reclaimed" pinning the three ways the
      reclaim must NOT fire.
- [x] No per-window information crosses the socket.
      DONE (plan 09-03, D-14): `[wm_socket]` "The status counts are real and no
      window's identity is in the reply" and "The status assembly does not reach
      any per-window identity" -- the second reads the assembly's own source, so
      a future field that carried a title would fail the gate before anyone
      thought to look at a capture.
- [x] A socket path too long for the address structure is named rather than
      silently truncated.
      DONE (plan 09-03): `[wm_socket]` "A socket path too long for the address
      structure is named, not truncated" and `[config_socket]` "A path too long
      for the address structure is refused, not truncated".
- [x] The window manager and `wm2-ctl` link no toolkit in any configuration.
      DONE (plans 09-08, 09-09): `bash scripts/gates/install-components.sh`
      stages the `wm` component and runs `ldd` over every executable it
      installed, failing if GTK, GDK, GLib or GObject is named;
      `bash scripts/gates/build-all.sh nogtk` builds the whole tree with
      `-DBUILD_CONFIG_GUI=OFF` and accounts the suite test-for-test against the
      GUI-enabled tree.
- [ ] Nothing in the window manager loads a toolkit library dynamically.
      **NOT GATED, and the distinction matters.** 09-08 checked it and it holds
      --- `grep -rn 'dlopen\|dlsym\|dlvsym' src/ include/ apps/wm2-ctl/` returns
      nothing, so there is no dynamic-loading call anywhere in the window
      manager or `wm2-ctl` --- but that was a one-off command, not a gate.
      `install-components.sh`'s `ldd` audit reads the LINK line and would not
      see a `dlopen` at all, so the runtime half of the no-GTK claim rests on a
      check nothing re-runs. A future `dlopen` would need a script. Remains
      open.

## Remote Desktop And VPS Checklist

- [x] Verify on the target baseline distro, especially Ubuntu 22.04+.
      DONE: Ubuntu 22.04+ host; versions recorded in
      `08.5-v1.0-closeout/evidence/gates/preflight-versions.log`.
- [ ] Verify under Xvfb, Xephyr, one VNC server, and one XRDP session if those are
      supported deployment targets.
      **PARTIAL, and the shortfall is narrower than it was.** Xvfb (the whole
      automated suite), Xephyr (`evidence/local-xephyr/`), TigerVNC and XRDP are
      exercised with committed transcripts from Phase 8. Plan 08.5-02 added a
      headless `nxagent` capture — X2Go's own X server, run nested on an Xvfb:
      `08.5-v1.0-closeout/evidence/x2go-nxagent/`. SHAPE, RANDR and RENDER are all
      present and the WM frames clients on it.
      **CORRECTION to what this row said through Phase 8:** it stated that
      `x2goserver` "was never installed". That was false. `/var/log/dpkg.log`
      stamps `x2goserver:amd64 4.1.0.3-5` at **2026-08-29 17:43**, during Phase
      8's own manual-pass window. The package was present and the session was
      simply not run — a different and less flattering fact than the one that was
      written down.
      What remains open is X2Go over a real connection, with its NX compression
      proxy in the path. See `x2go-nxagent/SCOPE.md` and D-8-X2GO below.
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

- [x] The settings window runs over a real remote-desktop session and its
      resident memory is recorded against the 512 MB budget.
      DONE (plan 09-09): measured, not estimated, on a real TigerVNC 1.12.0
      session at 1280x1024x24, loopback only, release build, with the window
      open and each of its three pages visited and screenshotted --
      **`wm2-config` 98.5 MB**, a second instance on the same session 49.6 MB,
      `wm2-born-again` beside it 12.0 MB, `Xvnc` itself recorded in the same
      transcript. Evidence, server version and commit:
      `09-config-gui-ipc/evidence/remote-desktop/README.md`. The regression
      detector is `[wm2_config_smoke]` "wm2-config's resident memory is measured
      against a stated budget", 160 MB, which reads the same `/proc` field
      through the same reader as `[wm_resource_budget]` so the two figures are
      comparable rather than merely adjacent.
      **The research's 30-60 MB estimate was low**, and the consequence is
      stated plainly in the release notes rather than absorbed: the settings
      window costs roughly a fifth of a 512 MB machine for as long as it is
      open. See the row below for what would have to change if that stopped
      being acceptable.
- [ ] The settings window's footprint is reduced, or the desktop is usable
      without it.
      **OPEN, and it is a decision rather than a defect.** 98.5 MB is GTK's
      cost, not this project's; `wm2-config` is a few thousand lines over a
      toolkit that maps a large amount of shared machinery into every process
      linking it. Three things would change the number, in increasing order of
      cost: ship the settings window only as the separate `config-gui` package
      and tell a memory-constrained user not to install it (**already true** --
      the `wm` package has no GTK dependency and `wm2-ctl` reaches every setting
      over the same socket); find and remove whatever makes the FIRST settings
      window on a session hold twice what later ones hold, which is currently an
      undiagnosed 49 MB; or write the settings window against a lighter toolkit,
      which is a v1.1-or-later project and not a tweak. Nothing here is a
      release blocker, because opening the window is a deliberate act with an
      obvious way to undo it. Remains open so the figure is not quietly
      forgotten.
- [ ] The undiagnosed split between the first and later settings windows on one
      X server is explained.
      **NOT DIAGNOSED.** The first `wm2-config` on a freshly started X server
      holds about 99 MB and every later one about 50 MB, reproducibly, on Xvfb
      and on TigerVNC alike, three runs of each. Two candidate explanations were
      tested and both are ruled out: it is not the per-user fontconfig cache (a
      fresh `HOME` on every run still shows the low figure from the second
      launch onward) and it is not the cost of being the first client on the
      server (an `xclock` connected first changes nothing). Recorded as an open
      question rather than guessed at in the release notes. Whoever next has a
      reason to reduce the footprint should start here: it is the largest single
      unexplained term in the figure.

## Release Evidence Required

For each release or handoff, attach:

- [x] Commit hash and branch.
      DONE: `08.5-v1.0-closeout/evidence/gates/PROVENANCE.txt` — commit
      `39de54811084154ddac1d2115e5f950b0c9f1565` (`39de548`), branch
      `worktree-agent-a0ef668e51c39c8dd`.
- [x] Dependency/package list and tool versions.
      DONE: `08.5-v1.0-closeout/evidence/gates/preflight-versions.log`.
- [x] Debug, Release, and sanitizer build logs.
      DONE: `08.5-v1.0-closeout/evidence/gates/build-all-debug.log`,
      `08.5-v1.0-closeout/evidence/gates/build-all-release.log` and
      `08.5-v1.0-closeout/evidence/gates/build-all-asan.log`, plus
      `08.5-v1.0-closeout/evidence/gates/compiler-debug.log`,
      `08.5-v1.0-closeout/evidence/gates/compiler-release.log` and
      `08.5-v1.0-closeout/evidence/gates/compiler-asan.log`.
- [x] Full `ctest --output-on-failure` logs.
      DONE: in the three build-all logs above; 100% tests passed, 0 tests
      failed out of 337 in every tree.
- [x] Runtime smoke transcript with `xprop -root` and `xwininfo -root -tree`
      output.
      DONE, two things, not one: `08-xrandr-vnc-compatibility-focus-rules/evidence/local-xephyr/`
      and the three per-target directories (`tigervnc/`, `xrdp/`, `x11vnc-xvfb/`)
      are Phase 8's transcripts, inherited rather than re-captured. This phase's
      own capture is separate and taken at the gate commit:
      `08.5-v1.0-closeout/evidence/gates/runtime-smoke/` (`capabilities.txt`,
      `root-properties.txt`, `window-tree.txt`) — Xvfb `:135`, release binary
      plus one `xmessage` client titled `wm2-smoke-39de548`, all three stopped
      by PID.
- [x] Screenshots from shaped and rectangular/no-Shape runs if supported.
      DONE: `08-xrandr-vnc-compatibility-focus-rules/evidence/screenshots/`,
      seven images including the shaped and rectangular pair. Qualified at the
      Phase 8 directory deliberately — no screenshot was taken in this phase.
- [x] ASan/UBSan logs showing no actionable findings.
      DONE: `08.5-v1.0-closeout/evidence/gates/build-all-asan.log` and
      `08.5-v1.0-closeout/evidence/gates/sanitizer-reports/`.
- [x] Manual interaction checklist results with tester name and date.
      DONE: `evidence/INTERACTION-CHECKLIST.md`. **Tester** tpatarci
      (operator). **Date** 2026-08-30. **Target** TigerVNC `:11`,
      1280x1024x24, loopback only. **Commit** `0244c06`. Per-item results for
      all 31 rows: **10 PASS** (walked step by step), **6 PASS (verdict)**
      (covered by the operator's own statement, not walked row by row),
      **11 DEFERRED** (explicitly declined by the operator), **4 n/a** (not
      applicable in this session, reason given) — total **31**. This closes
      the gap the row used to describe: a per-item table now exists, has a
      tester and a date, and covers every row of the User Interaction
      Checklist above. **What it does not close:** eleven rows were declined,
      nine of them gestures (circulation permutations, middle-click maximize,
      the circular fullscreen gesture, grab release) plus the menu's exit and
      hidden-client rows — the honest gap this release ships with, and the
      v1.1 "Gesture and input coverage" backlog line exists for exactly this.
- [x] The two gates added in Phase 9 captured at the release commit.
      DONE (plans 09-08, 09-09): `bash scripts/gates/install-components.sh`
      (both install components staged separately, each manifest exact, `ldd`
      audit over the `wm` component) and `bash scripts/gates/doc-keys.sh` (every
      accepted key documented and every documented key accepted, both
      directions). Both are in the developers' command block of
      `docs/RELEASE-NOTES.md`, and both have been shown to fail against a
      deliberately introduced defect and then pass again.
- [x] List of accepted deviations, each with owner and follow-up issue.
      DONE: D-8-TIGHTVNC and D-8-X2GO below, each with reason, owner and
      follow-up.

## Accepted Deviations

A deviation belongs here only with a **reason**, an **owner** and a
**follow-up**. Without all three it is not an accepted deviation, it is an
omission wearing the word "accepted".

### D-8-TIGHTVNC — RETIRED 2026-08-30 by plan 08.5-02, and its rationale was wrong

**Retired on evidence.** TightVNC has a capability transcript, a WM startup
banner and a window tree at
`08.5-v1.0-closeout/evidence/tightvnc/`. The deviation's own follow-up said to
delete it or restate it once a TightVNC server was available; one was, and this
is the deletion — kept as a record rather than removed, because *why* it was
wrong is worth more than the paragraph it replaces.

**The rationale did not survive measurement.** It argued that a TigerVNC result
was genuine evidence about TightVNC, because both descend from the same `Xvnc`
codebase and their "extension set advertised, Shape handling, resize handling,
the fontconfig picture — substantially overlaps".

| Server | Extensions | SHAPE | RANDR | RENDER |
|---|---|---|---|---|
| TigerVNC 1.12.0 | many | yes | yes | yes |
| **TightVNC 1.3.10** | **7** | **yes** | **no** | **no** |

On the two extensions the argument specifically named, they do not overlap at
all. TigerVNC 1.12 is a current server; TightVNC's Unix side is still 1.3.10 from
2009. The deviation reasoned from **ancestry to behaviour**, and the behaviour
disagreed — which is the general lesson worth keeping, since the same move is
available for any "close relative" argument about an untested target.

**What was found instead, and it is a good result.** This is the only capture in
the project taken against a server that genuinely lacks the extensions Phase 8
built fallbacks for. The WM started, both ladders announced themselves on stderr
exactly as designed (XDIS-02, XDIS-04), the client was framed normally, and no X
protocol error was logged. The one visible difference is a 3 px narrower tab —
`xIndent()` follows the measured tab width, and the font ladder's lower rung
resolves a different font. The geometry follows the font, as designed.

Until now the RENDER-less and RANDR-less paths had only ever been exercised
through the `WM2_FORCE_NO_*` levers. They now have a real server behind them.

**What is still not covered**, and is in `INTERACTION-CHECKLIST.md` rather than
here: nothing was judged by eye, so whether the core-X11 glyph path *looks*
acceptable is unanswered — which is exactly where a fallback is most likely to be
ugly rather than broken. No interaction was exercised over a real connection, and
resolution changes were not tested, which on a RANDR-less server is the
interesting case. See `tightvnc/SCOPE.md`.

### D-8-X2GO — X2Go is not validated for this release

**RESTATED 2026-08-30 by plan 08.5-02, on evidence, and narrowed.** The previous
text is superseded rather than edited, because two of the things it said are
false and the record of that is worth more than a clean paragraph.

**Two corrections to the Phase 8 text.** It said `x2goserver` "was never
installed on the validation host": `/var/log/dpkg.log` stamps
`x2goserver:amd64 4.1.0.3-5` at **2026-08-29 17:43**, during Phase 8's own
manual-pass window. And it said "nothing in the TigerVNC or XRDP results
transfers to it" on the grounds that `nxagent`'s extension surface was unknown.
It is known now, and it is the same surface: SHAPE, RANDR and RENDER all present,
23 extensions, measured. The deviation was written more pessimistically than the
facts warranted, in a direction that made skipping the work look better justified
than it was.

**What is now done.** `08.5-v1.0-closeout/evidence/x2go-nxagent/` — a headless
capture of `nxagent 3.5.99.26` running nested on an Xvfb, with the window manager
framing a client on it. Extension matrix, root properties, window tree, and the
WM's own startup banner agreeing with `xdpyinfo`. It cost about two minutes and
no human, which is the other thing the Phase 8 reasoning got wrong: the cost of
this was assumed, not measured.

**What is still not done, and this is the real deviation.** X2Go does not merely
run `nxagent`. It starts it through `x2gostartagent` with an **NX compression
proxy** between agent and client over SSH — and that proxy is the layer most
likely to differ on the things this project depends on. Untested: menus and
drag-move under compression and latency, the long-press delete timing, resize on
reconnect at a different geometry, and what the proxy forwards for SHAPE. Nothing
here was judged by eye, either: no screenshot, no verdict on the sideways tab.

See `x2go-nxagent/SCOPE.md`, which travels with the transcript for the same
reason this paragraph exists — three green files in a directory named `x2go`
read, at a glance, like "X2Go: tested".

**Owner:** whoever next stands up a remote-desktop validation session — the same
role that runs the User Interaction Checklist for a release.

**Follow-up:** connect a real X2Go client to a session on this host, run the
interaction checklist over it, and either delete this entry or restate it again.
The install and the capability capture are already done, so what remains is one
session with a person in it.

**What a user should expect meanwhile:** X2Go is unexercised, not unsupported. If
the sideways tab renders wrongly or the frame is unshaped under `nxagent`, that
is a real bug worth reporting — and given the RENDER dependency, it is the
likeliest place for one to be hiding.
