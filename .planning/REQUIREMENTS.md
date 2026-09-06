# Requirements: wm2-born-again

**Defined:** 2026-05-06
**Core Value:** A lightweight, visually distinctive window manager that works well on resource-constrained VPS instances via remote desktop — simple enough for non-programmers to configure, reliable enough for daily use.

## v1 Requirements

### Build Infrastructure

- [x] **BLD-01**: WM builds with CMake 3.16+ and pkg-config for X11 dependency discovery
- [x] **BLD-02**: C++17 standard (std::vector, std::string, bool, structured bindings)
- [x] **BLD-03**: RAII wrappers for all X11 resources (Display, Window, GC, Cursor, Font, Pixmap, Colormap)
- [x] **BLD-04**: Builds and runs on Ubuntu 22.04+ with standard apt packages
- [x] **BLD-05**: No hardcoded X11R6 paths — all library paths discovered via pkg-config

### Event Loop

- [ ] **EVNT-01**: Replace select()+goto pattern with poll()-based event loop
- [ ] **EVNT-02**: Auto-raise timing preserved (400ms raise delay, 80ms pointer-stopped delay)
- [ ] **EVNT-03**: Signal handling works correctly (SIGTERM, SIGINT, SIGHUP clean shutdown)

### Client Lifecycle

- [x] **CLNT-01**: ICCCM-compliant reparenting using XGrabServer/XUngrabServer
- [x] **CLNT-02**: Replace `delete this` with proper lifecycle management via WindowManager
- [x] **CLNT-03**: Replace custom listmacro2.h with std::vector<Client*>
- [x] **CLNT-04**: O(1) client lookup by window ID (hash map instead of linear scan)
- [x] **CLNT-05**: Client state transitions are correct (Withdrawn → Normal → Iconic → Withdrawn)

### Visual Identity

- [ ] **VISL-01**: Preserved classic wm2 sideways-tab look with shaped borders
- [ ] **VISL-02**: Xft + fontconfig replace core X fonts + xvertext for all text rendering
- [ ] **VISL-03**: Antialiased, UTF-8 tab labels (supports non-ASCII window names)
- [ ] **VISL-04**: Font specified by fontconfig pattern (e.g., "Sans-12") not XLFD
- [ ] **VISL-05**: Shape extension used when available, rectangular fallback when not

### EWMH Compliance

- [x] **EWMH-01**: Set _NET_SUPPORTED listing all supported EWMH atoms
- [x] **EWMH-02**: Set _NET_SUPPORTING_WM_CHECK for WM identification
- [x] **EWMH-03**: Maintain _NET_CLIENT_LIST (updated on map/unmap/destroy)
- [x] **EWMH-04**: Handle _NET_ACTIVE_WINDOW (respond to client activation requests)
- [x] **EWMH-05**: Handle _NET_WM_WINDOW_TYPE (DOCK, DIALOG, NOTIFICATION, NORMAL)
- [x] **EWMH-06**: Handle _NET_WM_STATE (_NET_WM_STATE_FULLSCREEN, _NET_WM_STATE_MAXIMIZED_*)
- [x] **EWMH-07**: Set _NET_WM_NAME with UTF-8 encoding for WM name
- [x] **EWMH-08**: Set single-desktop atoms (_NET_NUMBER_OF_DESKTOPS=1, _NET_CURRENT_DESKTOP=0)
- [x] **EWMH-09**: Set _NET_WORKAREA for panel/taskbar compatibility

### Xrandr & VNC

- [x] **XDIS-01**: Xrandr support for display configuration and resolution changes
- [x] **XDIS-02**: Graceful fallback when Xrandr unavailable (VNC)
- [x] **XDIS-03**: Graceful fallback when Shape extension unavailable (rectangular frames)
- [x] **XDIS-04**: Graceful fallback when XRender unavailable — the WM keeps running and keeps managing windows, with tab labels degraded or absent rather than the process exiting. Satisfied at the fontconfig-fallback level: the preferred family chain, a generic sans chain, an unrotated face, and finally no label, none of which may terminate the WM. (Amended in Phase 8, plan 08-06, D-14. The original wording promised a fallback to the X server's own bitmap fonts, which contradicts what is built: Phase 4 removed core X fonts and the bundled rotation library by decision, so reviving them here would undo that. Phase 8 also measured that libXft renders the rotated tab face through its core X11 glyph path when XRender is absent, so the sideways tab survives a RENDER-less server anyway.)
- [x] **XDIS-05**: Compatible with TigerVNC, TightVNC, XRDP, X2Go out of the box. **MET, all four targets measured.** TigerVNC and XRDP were validated in Phase 8; plan 08.5-02 added the other two, both headlessly and both cheaply — TightVNC via `Xtightvnc` started directly, X2Go via `nxagent` nested on an Xvfb. Transcripts under `08.5-v1.0-closeout/evidence/{tightvnc,x2go-nxagent}/`, each with the WM's own startup banner agreeing with `xdpyinfo`. **The TightVNC result is the valuable one and it disproved its own deviation:** TightVNC 1.3.10 advertises 7 extensions — SHAPE but no RANDR and no RENDER — against TigerVNC's full set, so D-8-TIGHTVNC's "substantially overlapping extension set" rationale was wrong on exactly the two extensions it named. It reasoned from shared `Xvnc` ancestry to behaviour, and the behaviour disagreed. The WM ran there with both fallback ladders announcing themselves and no protocol errors — the first time the RENDER-less and RANDR-less paths have been exercised against a real server rather than the `WM2_FORCE_NO_*` levers. D-8-TIGHTVNC is retired; **D-8-X2GO is restated, not deleted**, because a nested nxagent settles the extension surface and says nothing about X2Go's NX compression proxy over a network link, which is what that deviation was actually about.

### Configuration

- [ ] **CONF-01**: Runtime config file at ~/.config/wm2-born-again/config (key=value format)
- [ ] **CONF-02**: Config file supports: fonts, colors, focus policy, frame thickness, delays, menu command
- [ ] **CONF-03**: Defaults work without config file (sensible built-in defaults)
- [ ] **CONF-04**: Command-line options override config file settings

### Configuration GUI

- [ ] **CGUI-01**: Separate GTK3 binary (wm2-config) for visual configuration editing
- [ ] **CGUI-02**: Communicates with WM via Unix domain socket (JSON protocol)
- [ ] **CGUI-03**: Edit fonts, colors, focus policy, frame thickness, menu entries
- [ ] **CGUI-04**: Changes apply immediately (no restart required) where possible
- [ ] **CGUI-05**: WM works without GTK installed (GUI is optional dependency)

### Application Discovery

- [x] **APPS-01**: Parse XDG .desktop files from $XDG_DATA_DIRS/applications/ for menu entries
- [x] **APPS-02**: AI-powered binary scan of /usr/bin/ for apps lacking .desktop files
- [x] **APPS-03**: Cached results at ~/.config/wm2-born-again/appcache.json
- [x] **APPS-04**: User can manually add/remove entries via config file
- [x] **APPS-05**: Root menu shows discovered apps organized by category

### Focus & Window Rules

- [x] **FOCUS-01**: Focus stealing prevention using _NET_WM_USER_TIME timestamps
- [x] **FOCUS-02**: Configurable focus policy (click-to-focus, focus-follows-pointer, auto-raise)
- [x] **RULES-01**: Window matching rules in config file (match by WM_CLASS, WM_NAME, window type). **MET AS WRITTEN, not amended** — delivered by plan 08.5-01, and the contrast with its neighbours is the useful part of this entry. XDIS-04 and RULES-02 were both *reworded* because a prior architectural decision blocked the original promise. Nothing blocked this one: `Client::m_name` already held a title, the facts struct was plain data, and the matcher was mode-agnostic. Phase 8 therefore held it Pending rather than rewording it, and Phase 8.5 implemented it. What shipped: (1) `rule-match-title`, matching the window title; (2) the title read corrected to EWMH-first — the WM read only the legacy `WM_NAME` and never `_NET_WM_NAME` off a client, so a rule built on the old read would have been dead for any modern toolkit, which is the failure class this project spent Phase 8 removing (D-8.5-02); (3) the config keys renamed so each says what it compares — `rule-match-instance` for the WM_CLASS instance name, `rule-match-title` for the title, `rule-match-class` unchanged, with **no alias** for the old `rule-match-name`, taken before v1.0 made the mistake permanent (D-8.5-01). **The one semantic to know:** rules fold once, at manage time, so a title rule matches the map-time title and the WM does not re-fold when a title later changes — re-applying position and size on rename would make windows jump when a document is renamed (D-8.5-03). That is stated in `docs/RELEASE-NOTES.md`, at the fold in `src/Client.cpp`, and here, and it is pinned by a test that reddens if a re-fold is added. Re-fold-on-title-change is on the v1.1 backlog.
- [x] **RULES-02**: Per-rule actions: no-decorate, specific position/size, skip-taskbar. (Amended in Phase 8, plan 08-10, D-23. The original wording also named a fourth action, sending a window to a nominated workspace; that action is deliberately excluded because the WM is single-desktop by design — Phase 6 fixed `_NET_NUMBER_OF_DESKTOPS` at 1 and `_NET_CURRENT_DESKTOP` at 0 — so there is no second workspace for a rule to send a window to. Implementing it would require a desktop model this window manager does not have and does not intend to acquire, and promising it here would leave a requirement that can never be checked off.)

### Testing

- [x] **TEST-01**: Test suite runs on Xvfb without physical display
- [x] **TEST-02**: Catch2 test framework for unit and integration tests
- [x] **TEST-03**: Core WM operations tested: window map, move, resize, hide/unhide, delete
- [x] **TEST-04**: CI-ready (tests pass with cmake + ctest)
- [x] **TEST-05**: Process-level integration tests launch the compiled `wm2-born-again` binary under Xvfb/Xephyr, drive real X11 clients, and verify root/client ICCCM + EWMH properties after create, map, unmap, remap, hide/unhide, fullscreen, maximize, and destroy
- [x] **TEST-06**: Release signoff requires Debug, Release, and ASan/UBSan builds plus full `ctest --output-on-failure`; sanitizer findings block completion
- [x] **TEST-07**: Environment preflight verifies pkg-config dependencies (`x11`, `xext`, `xft`, `fontconfig`), X11 tools, Xvfb/Xephyr availability, and fontconfig fallback resolution before behavioral claims are accepted
- [x] **TEST-08**: Runtime smoke evidence is captured for nested/headless X11 and supported remote desktop targets, including `xprop -root`, `xwininfo -root -tree`, interaction checklist results, and accepted deviations. **MET, with a declared gap in the interaction table.** Both components Phase 8 left short are closed: four of four remote targets now have transcripts (XDIS-05), and the interaction checklist exists as a per-item table with tester name and date at `08.5-v1.0-closeout/evidence/INTERACTION-CHECKLIST.md`. It distinguishes three outcomes rather than collapsing them — 9 rows walked step by step, 7 covered by the operator's general verdict, 11 **deferred by explicit operator decision**, 4 n/a with reasons. The eleven deferred rows are the honest gap and they cluster: nine are gestures with no automated coverage either, which is the same blind spot that produced all three defects the Phase 8 manual pass found. Carried as the v1.1 backlog line "Gesture and input coverage", for which this table is the evidence. The bundle also gained `STRESS-RESULTS.md` (400 windows churned, RSS flat after warm-up, LSan clean) and per-target `SCOPE.md` files stating what each transcript does not establish.

## v2 Requirements

### Keyboard

- **KEYS-01**: Configurable keyboard shortcuts for common operations (move, resize, close, switch)
- **KEYS-02**: Keyboard-driven window switching (Alt+Tab style)

### Session

- **SESS-01**: XSMP session management (WM_SAVE_YOURSELF)
- **SESS-02**: Remember window positions across restarts

### Shaped Windows

- **SHAP-01**: Support for client-set window shapes (non-rectangular windows)

### Polish

- **PLSH-01**: Startup notification support (_NET_STARTUP_INFO)
- **PLSH-02**: _NET_WM_PING for hung window detection
- **PLSH-03**: Animated window operations (optional, with config toggle)

### Native X11 Configuration Tool (added 2026-09-06, Phase 10, last in line)

- [ ] **XCFG-01**: A configuration front end built against plain Xlib (Xft when available) with no widget toolkit dependency, offering the same three pages and the same editing operations as the GTK tool
- [ ] **XCFG-02**: Uses only the X11 core protocol: no required extensions; with Render or Xft unavailable it falls back to core X fonts and still runs
- [ ] **XCFG-03**: Speaks the Phase 9 socket protocol unchanged and uses the same surgical config-file writer, including file-only mode when no window manager is running
- [ ] **XCFG-04**: Verified under Xvfb in the test suite and on the four remote-desktop targets (TigerVNC, TightVNC, XRDP, X2Go), with RSS recorded
- [ ] **XCFG-05**: The widget set it needs lives in the tree under the project's MIT licence; no bundled third-party toolkit

## Out of Scope

| Feature | Reason |
|---------|--------|
| Wayland support | X11 only; VPS remote desktop protocols all use X11 |
| Virtual desktops/workspaces | Keep it simple for VPS use; wm2's philosophy |
| Compositing/transparency | Adds complexity, GPU dependency, not needed for VPS |
| Theming engine | Config file for colors/fonts is sufficient |
| Multi-screen support | VPS typically has single virtual display |
| Icons/icon tray | wm2's philosophy: "no icons" |
| Taskbar/panel | wm2 is a window manager, not a desktop environment |
| IPC for third-party tools | Only config GUI needs IPC; not building a general API |
| XCB migration | Xlib works fine; migration adds complexity for no benefit |
| C++20/C++23 | C++17 is sufficient; no need for modules/concepts |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| BLD-01 | Phase 1 | Complete |
| BLD-02 | Phase 1 | Complete |
| BLD-03 | Phase 1 | Complete |
| BLD-04 | Phase 1 | Complete |
| BLD-05 | Phase 1 | Complete |
| EVNT-01 | Phase 2 | Pending |
| EVNT-02 | Phase 2 | Pending |
| EVNT-03 | Phase 2 | Pending |
| CLNT-01 | Phase 3 | Complete |
| CLNT-02 | Phase 3 | Complete |
| CLNT-03 | Phase 3 | Complete |
| CLNT-04 | Phase 3 | Complete |
| CLNT-05 | Phase 3 | Complete |
| VISL-01 | Phase 4 | Pending |
| VISL-02 | Phase 4 | Pending |
| VISL-03 | Phase 4 | Pending |
| VISL-04 | Phase 4 | Pending |
| VISL-05 | Phase 4 | Pending |
| CONF-01 | Phase 5 | Pending |
| CONF-02 | Phase 5 | Pending |
| CONF-03 | Phase 5 | Pending |
| CONF-04 | Phase 5 | Pending |
| EWMH-01 | Phase 6 | Complete |
| EWMH-02 | Phase 6 | Complete |
| EWMH-03 | Phase 6 | Complete |
| EWMH-04 | Phase 6 | Complete |
| EWMH-05 | Phase 6 | Complete |
| EWMH-06 | Phase 6 | Complete |
| EWMH-07 | Phase 6 | Complete |
| EWMH-08 | Phase 6 | Complete |
| EWMH-09 | Phase 6 | Complete |
| APPS-01 | Phase 7 | Complete |
| APPS-02 | Phase 7 | Complete |
| APPS-03 | Phase 7 | Complete |
| APPS-04 | Phase 7 | Complete |
| APPS-05 | Phase 7 | Complete |
| XDIS-01 | Phase 8 | Complete |
| XDIS-02 | Phase 8 | Complete |
| XDIS-03 | Phase 8 | Complete |
| XDIS-04 | Phase 8 | Complete |
| XDIS-05 | Phase 8.5 | Complete |
| FOCUS-01 | Phase 8 | Complete |
| FOCUS-02 | Phase 8 | Complete |
| RULES-01 | Phase 8.5 | Complete |
| RULES-02 | Phase 8 | Complete |
| CGUI-01 | Phase 9 | Pending |
| CGUI-02 | Phase 9 | Pending |
| CGUI-03 | Phase 9 | Pending |
| CGUI-04 | Phase 9 | Pending |
| CGUI-05 | Phase 9 | Pending |
| XCFG-01 | Phase 10 | Pending |
| XCFG-02 | Phase 10 | Pending |
| XCFG-03 | Phase 10 | Pending |
| XCFG-04 | Phase 10 | Pending |
| XCFG-05 | Phase 10 | Pending |
| TEST-01 | Phase 1 | Complete |
| TEST-02 | Phase 1 | Complete |
| TEST-03 | Phase 3 | Complete |
| TEST-04 | Phase 1 | Complete |
| TEST-05 | Phase 8 | Complete |
| TEST-06 | Phase 8 | Complete |
| TEST-07 | Phase 8 | Complete |
| TEST-08 | Phase 8.5 | Complete |

**Coverage:**

- v1 requirements: 53 total
- Mapped to phases: 53
- Unmapped: 0 ✓

---
*Requirements defined: 2026-05-06*
*Last updated: 2026-07-08 after compiled behavior checklist insertion*
