# Compiled Code Behavior Checklist

This checklist is tailored to this repository: a C++17 X11 window manager with
runtime config, Xft rendering, EWMH support, RAII wrappers, and Xvfb-backed
Catch2 tests.

Use it as a release/signoff checklist for developers. A checkbox is not done
until there is either an automated test, a recorded manual run, or an explicit
accepted exception with a reason.

## Current Scan Findings

- [ ] Resolve build preflight before claiming test coverage. On this scan host,
      `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON` failed
      because `pkg-config` could not find `xft` or `fontconfig`. CMake requires
      `x11`, `xext`, `xft`, and `fontconfig` in `CMakeLists.txt:11-14`.
- [ ] Fix or explicitly accept the `eventDestroy` lifetime hazard in
      `src/Events.cpp:237-283`: the code erases the owning `unique_ptr<Client>`
      and then checks `c->isDock()`. Preserve `wasDock` before erase and test
      dock destruction under ASan.
- [ ] Fix or explicitly prove the no-Shape-extension path. `src/Manager.cpp:160`
      records `m_shapeEvent = -1`, but fallback helpers in `src/Border.cpp:155`
      and `src/Border.cpp:172`, plus `src/Border.cpp:590`, still call
      `XShapeCombineRectangles`. A real no-Shape X server must not trigger Shape
      requests.
- [ ] Prove focus config is actually wired to behavior. `click-to-focus`,
      `raise-on-focus`, and `auto-raise` are parsed in `src/Config.cpp:152-154`,
      but runtime paths mostly use delayed focus-follows-pointer
      (`src/Client.cpp:1329`, `src/Manager.cpp:810`).

## Environment Preflight

- [ ] Document distro, compiler, CMake, and pkg-config versions for every signoff.
- [ ] Confirm native dependencies are present:

```bash
pkg-config --exists x11 xext xft fontconfig
cmake --version
c++ --version
```

- [ ] Confirm headless and interactive X11 tools are available:

```bash
command -v Xvfb
command -v Xephyr
command -v xprop
command -v xwininfo
command -v xdotool
command -v xdpyinfo
```

- [ ] Confirm at least one simple X client is available for runtime tests
      (`xclock`, `xmessage`, or equivalent). Do not rely on `xterm` being
      installed unless it is declared as a package dependency.
- [ ] Confirm fontconfig can resolve the fonts used by the Xft fallback chains:

```bash
fc-match "Noto Sans,DejaVu Sans,Sans:size=12"
fc-match "Noto Sans,DejaVu Sans,Sans:bold:size=12"
```

- [ ] Ensure CI does not rely on live GitHub access for Catch2 unless that is an
      explicit policy. Current CMake uses `FetchContent` for Catch2.
- [ ] Ensure fixed Xvfb display `:99` is free, or update the test harness to
      allocate a free display.

## Build And Test Gates

- [ ] Configure a clean Debug build with tests enabled:

```bash
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
```

- [ ] Build without compiler or linker warnings that are new for the change:

```bash
cmake --build build/debug --parallel
```

- [ ] Run the full CTest suite:

```bash
ctest --test-dir build/debug --output-on-failure
```

- [ ] Confirm the expected discovered test surface is present. At scan time the
      tree contains 102 Catch2 test cases:
      `config` 35, `raii` 16, `ewmh` 16, `client` 11, `smoke` 7, `xft_poc` 6,
      `autoraise` 6, `eventloop` 5.
- [ ] Run a Release build and tests:

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure
```

- [ ] Run an AddressSanitizer/UBSan build. Treat use-after-free, invalid enum
      use, double-free, and out-of-bounds reports as blockers:

```bash
cmake -S . -B build/asan -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build/asan --parallel
ctest --test-dir build/asan --output-on-failure
```

- [ ] Run static analysis when tools are available:

```bash
cppcheck --enable=all --std=c++17 -Iinclude src include
clang-tidy -p build/debug src/*.cpp
```

- [ ] Verify the final executable links only to intended runtime libraries:

```bash
ldd build/release/wm2-born-again
```

## Existing Automated Coverage To Preserve

- [ ] Config parsing remains covered for defaults, XDG precedence, CLI overrides,
      malformed lines, value length limits, booleans, integers, and clamping
      (`tests/test_config.cpp`).
- [ ] RAII wrappers remain covered for null safety, move-only ownership, and
      release semantics (`tests/test_raii.cpp`, `tests/test_smoke.cpp`).
- [ ] Event-loop support remains covered for `FdGuard` and basic self-pipe I/O
      (`tests/test_eventloop.cpp`).
- [ ] Auto-raise timer arithmetic remains covered (`tests/test_autoraise.cpp`).
- [ ] Xvfb smoke tests continue to open a display and create X11 resources
      (`tests/test_smoke.cpp`).
- [ ] Client primitive tests continue to cover X window creation, move/resize,
      reparenting, `WM_STATE`, colormap property reads, and multiple windows
      (`tests/test_client.cpp`).
- [ ] Xft proof tests continue to cover font loading, rotated fonts, shaped
      window drawing, Xft colors, text metrics, and UTF-8 rendering
      (`tests/test_xft_poc.cpp`).
- [ ] EWMH property tests continue to cover atom interning, WM check window,
      `_NET_SUPPORTED`, desktop/workarea/client/active properties, state arrays,
      fullscreen/maximize atoms, and dock struts (`tests/test_ewmh.cpp`).

## Missing Automated Coverage To Add

- [ ] Add process-level tests that start `wm2-born-again` itself in Xvfb, not only
      primitive Xlib tests. `WindowManager` starts its event loop in the
      constructor (`src/Manager.cpp:59-176`), so these should run the executable
      as a child process and drive it through X11.
- [ ] Add a lifecycle test for managed window destroy:
      create a normal managed client, destroy it, assert no crash, no stale
      `_NET_CLIENT_LIST` entry, no active-window dangling value, and no ASan
      report.
- [ ] Add the same destroy test for dock windows with `_NET_WM_STRUT` and
      `_NET_WM_STRUT_PARTIAL`; assert `_NET_WORKAREA` is recomputed after the
      dock dies.
- [ ] Add tests for hidden-list transfers through `Client::hide()` and
      `Client::unhide()` (`src/Client.cpp:775`, `src/Client.cpp:793`), including
      `_NET_WM_STATE_HIDDEN` and `_NET_CLIENT_LIST` updates.
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
- [ ] Add tests for no-Shape fallback, or refactor shape calls behind a wrapper
      that can be forced unavailable in tests.
- [ ] Add tests for all `XSizeHints` resize constraints in
      `Client::fixResizeDimensions()` (`src/Client.cpp:1144`):
      min size, max size, base size, resize increments, fixed-size clients, and
      invalid/zero increments.
- [ ] Add tests for all window gravity modes used by `Client::gravitate()`
      (`src/Client.cpp:702`).
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

- [ ] Startup claims the root window and logs no fatal errors.
- [ ] A second WM on the same display fails cleanly with the "another window
      manager running" path.
- [ ] Normal top-level windows are framed with the wm2 sideways tab.
- [ ] `override_redirect` windows are ignored.
- [ ] Dock and notification windows are not decorated.
- [ ] Existing windows present before WM startup are scanned and managed
      (`src/Manager.cpp:512`).
- [ ] Root `_NET_SUPPORTED`, `_NET_SUPPORTING_WM_CHECK`,
      `_NET_NUMBER_OF_DESKTOPS`, `_NET_CURRENT_DESKTOP`, `_NET_WORKAREA`,
      `_NET_CLIENT_LIST`, and `_NET_ACTIVE_WINDOW` are correct after startup.
- [ ] Creating, mapping, unmapping, remapping, and destroying normal clients does
      not crash and leaves root EWMH properties accurate.
- [ ] SIGTERM, SIGINT, and SIGHUP wake the poll loop through the self-pipe and
      exit cleanly (`src/Events.cpp:130`, `src/Manager.cpp:269`).
- [ ] Shutdown restores/reparents managed clients and does not leave unusable
      windows behind.

## User Interaction Checklist

- [ ] Root left-click menu appears, fits on screen edges, highlights correctly,
      and unmaps after selection.
- [ ] Root menu `New` launches the configured command. Test both plain exec and
      shell mode:

```bash
DISPLAY=:2 build/debug/wm2-born-again --new-window-command=xclock
DISPLAY=:2 build/debug/wm2-born-again --exec-using-shell --new-window-command="xmessage shell-ok"
```

- [ ] Root menu hidden-client entries restore hidden clients.
- [ ] Root menu exit item appears only at the lower-right screen edge and exits.
- [ ] Right-click root/window circulation works with zero clients, one client,
      hidden clients, transient clients, and multiple normal clients.
- [ ] Clicking a tab/frame raises and focuses according to the selected focus
      policy.
- [ ] Dragging a tab moves a window and sends a correct synthetic
      `ConfigureNotify`.
- [ ] Dragging resize handles resizes normally and respects constrained
      horizontal/vertical resize paths.
- [ ] Tab button short press hides; long press after `destroy-window-delay` sends
      delete/kill behavior and restores the cursor.
- [ ] Middle-click tab toggles maximize.
- [ ] Right-button circular gesture toggles fullscreen and ignores short/noisy
      gestures.
- [ ] Pointer grabs are always released after cancel, extra button press, escape
      paths, or client destroy during interaction.

## ICCCM And EWMH Checklist

- [ ] `WM_STATE` uses exact X11 values: Withdrawn 0, Normal 1, Iconic 3.
- [ ] `WM_PROTOCOLS` honors `WM_DELETE_WINDOW` and `WM_TAKE_FOCUS`.
- [ ] `WM_TRANSIENT_FOR` handles normal transients, self-referencing transients,
      and transient raise ordering.
- [ ] `WM_COLORMAP_WINDOWS` updates and installs colormaps in the expected order.
- [ ] `_NET_CLIENT_LIST` includes normal and hidden managed clients and removes
      clients immediately on destroy.
- [ ] `_NET_ACTIVE_WINDOW` updates on activation and returns to `None` when focus
      is cleared.
- [ ] `_NET_WM_WINDOW_TYPE` handles at least normal, dock, dialog, notification,
      utility, splash, and toolbar.
- [ ] `_NET_WM_STATE` add/remove/toggle works for fullscreen, maximized vertical,
      maximized horizontal, and hidden.
- [ ] `_NET_WORKAREA` subtracts dock struts, prefers `_NET_WM_STRUT_PARTIAL` over
      `_NET_WM_STRUT`, and clamps values to the screen.
- [ ] Fullscreen covers the whole screen including dock areas; maximized windows
      use workarea and keep the frame/tab.

## Rendering And Geometry Checklist

- [ ] Xft menu font and rotated tab font load through the configured fallback
      chains.
- [ ] UTF-8 labels render without corrupting text or crashing.
- [ ] Long names fall back to icon name and then truncate with ellipsis without
      negative dimensions (`src/Border.cpp:295`).
- [ ] Tab, button, border, and menu colors apply from config and invalid colors
      fail with a clear message.
- [x] Frame thickness works at minimum, default, and maximum configured values.
      DONE (plan 08-13): asserted absolutely (`yIndent() == FRAME_WIDTH + 1`) at
      3, the shipped 7 and 20. This is where the SHAPE ordering defect surfaced:
      every thickness outside a narrow band produced a `BadMatch` and an
      unshaped frame.
- [ ] Shaped frames render correctly when Shape is available.
- [ ] Rectangular fallback renders and operates correctly when Shape is not
      available, without issuing Shape extension requests.
- [ ] Moving/resizing keeps windows visible on screen and handles small screens.
- [ ] Single-screen behavior is verified. Multi-screen is not supported; if it is
      out of scope, document that explicitly in release notes.

## Robustness And Security Checklist

- [ ] No ASan/UBSan reports in full automated and runtime smoke tests.
- [ ] No steady CPU spin while idle, while waiting for auto-raise timers, or while
      menus/grabs are active.
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
- [ ] Config files reject overlong lines and values as tested, and unknown keys
      warn without aborting.
- [ ] Client-supplied properties cannot crash the WM by using wrong atom types,
      wrong formats, deleted properties, oversized arrays, invalid UTF-8, or
      absurd dimensions.
- [ ] `ignoreBadWindowErrors` is scoped tightly around cleanup and does not mask
      unrelated protocol errors.
- [ ] Display, GC, cursor, pixmap, colormap, Xft font, Xft draw, and Xft color
      resources are freed before their owning `Display` closes.

## Remote Desktop And VPS Checklist

- [ ] Verify on the target baseline distro, especially Ubuntu 22.04+.
- [ ] Verify under Xvfb, Xephyr, one VNC server, and one XRDP session if those are
      supported deployment targets.
- [ ] Verify a low-resource run: 512 MB RAM target, multiple simple windows,
      repeated open/close cycles, and idle CPU near zero.
- [ ] Verify behavior over remote latency: menus, move/resize, auto-raise, and
      long-press delete remain usable.
- [ ] Verify common remote desktop X servers expose the required Xft/fontconfig
      behavior and either support Shape or pass the rectangular fallback test.

## Release Evidence Required

For each release or handoff, attach:

- [ ] Commit hash and branch.
- [ ] Dependency/package list and tool versions.
- [ ] Debug, Release, and sanitizer build logs.
- [ ] Full `ctest --output-on-failure` logs.
- [ ] Runtime smoke transcript with `xprop -root` and `xwininfo -root -tree`
      output.
- [ ] Screenshots from shaped and rectangular/no-Shape runs if supported.
- [ ] ASan/UBSan logs showing no actionable findings.
- [ ] Manual interaction checklist results with tester name and date.
- [ ] List of accepted deviations, each with owner and follow-up issue.
