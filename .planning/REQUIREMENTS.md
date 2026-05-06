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

- [ ] **CLNT-01**: ICCCM-compliant reparenting using XGrabServer/XUngrabServer
- [ ] **CLNT-02**: Replace `delete this` with proper lifecycle management via WindowManager
- [ ] **CLNT-03**: Replace custom listmacro2.h with std::vector<Client*>
- [ ] **CLNT-04**: O(1) client lookup by window ID (hash map instead of linear scan)
- [ ] **CLNT-05**: Client state transitions are correct (Withdrawn → Normal → Iconic → Withdrawn)

### Visual Identity

- [ ] **VISL-01**: Preserved classic wm2 sideways-tab look with shaped borders
- [ ] **VISL-02**: Xft + fontconfig replace core X fonts + xvertext for all text rendering
- [ ] **VISL-03**: Antialiased, UTF-8 tab labels (supports non-ASCII window names)
- [ ] **VISL-04**: Font specified by fontconfig pattern (e.g., "Sans-12") not XLFD
- [ ] **VISL-05**: Shape extension used when available, rectangular fallback when not

### EWMH Compliance

- [ ] **EWMH-01**: Set _NET_SUPPORTED listing all supported EWMH atoms
- [ ] **EWMH-02**: Set _NET_SUPPORTING_WM_CHECK for WM identification
- [ ] **EWMH-03**: Maintain _NET_CLIENT_LIST (updated on map/unmap/destroy)
- [ ] **EWMH-04**: Handle _NET_ACTIVE_WINDOW (respond to client activation requests)
- [ ] **EWMH-05**: Handle _NET_WM_WINDOW_TYPE (DOCK, DIALOG, NOTIFICATION, NORMAL)
- [ ] **EWMH-06**: Handle _NET_WM_STATE (_NET_WM_STATE_FULLSCREEN, _NET_WM_STATE_MAXIMIZED_*)
- [ ] **EWMH-07**: Set _NET_WM_NAME with UTF-8 encoding for WM name
- [ ] **EWMH-08**: Set single-desktop atoms (_NET_NUMBER_OF_DESKTOPS=1, _NET_CURRENT_DESKTOP=0)
- [ ] **EWMH-09**: Set _NET_WORKAREA for panel/taskbar compatibility

### Xrandr & VNC

- [ ] **XDIS-01**: Xrandr support for display configuration and resolution changes
- [ ] **XDIS-02**: Graceful fallback when Xrandr unavailable (VNC)
- [ ] **XDIS-03**: Graceful fallback when Shape extension unavailable (rectangular frames)
- [ ] **XDIS-04**: Graceful fallback when XRender unavailable (core X font fallback)
- [ ] **XDIS-05**: Compatible with TigerVNC, TightVNC, XRDP, X2Go out of the box

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

- [ ] **APPS-01**: Parse XDG .desktop files from $XDG_DATA_DIRS/applications/ for menu entries
- [ ] **APPS-02**: AI-powered binary scan of /usr/bin/ for apps lacking .desktop files
- [ ] **APPS-03**: Cached results at ~/.config/wm2-born-again/appcache.json
- [ ] **APPS-04**: User can manually add/remove entries via config file
- [ ] **APPS-05**: Root menu shows discovered apps organized by category

### Focus & Window Rules

- [ ] **FOCUS-01**: Focus stealing prevention using _NET_WM_USER_TIME timestamps
- [ ] **FOCUS-02**: Configurable focus policy (click-to-focus, focus-follows-pointer, auto-raise)
- [ ] **RULES-01**: Window matching rules in config file (match by WM_CLASS, WM_NAME, window type)
- [ ] **RULES-02**: Per-rule actions: no-decorate, specific position/size, specific workspace, skip-taskbar

### Testing

- [x] **TEST-01**: Test suite runs on Xvfb without physical display
- [x] **TEST-02**: Catch2 test framework for unit and integration tests
- [ ] **TEST-03**: Core WM operations tested: window map, move, resize, hide/unhide, delete
- [x] **TEST-04**: CI-ready (tests pass with cmake + ctest)

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
| CLNT-01 | Phase 3 | Pending |
| CLNT-02 | Phase 3 | Pending |
| CLNT-03 | Phase 3 | Pending |
| CLNT-04 | Phase 3 | Pending |
| CLNT-05 | Phase 3 | Pending |
| VISL-01 | Phase 4 | Pending |
| VISL-02 | Phase 4 | Pending |
| VISL-03 | Phase 4 | Pending |
| VISL-04 | Phase 4 | Pending |
| VISL-05 | Phase 4 | Pending |
| CONF-01 | Phase 5 | Pending |
| CONF-02 | Phase 5 | Pending |
| CONF-03 | Phase 5 | Pending |
| CONF-04 | Phase 5 | Pending |
| EWMH-01 | Phase 6 | Pending |
| EWMH-02 | Phase 6 | Pending |
| EWMH-03 | Phase 6 | Pending |
| EWMH-04 | Phase 6 | Pending |
| EWMH-05 | Phase 6 | Pending |
| EWMH-06 | Phase 6 | Pending |
| EWMH-07 | Phase 6 | Pending |
| EWMH-08 | Phase 6 | Pending |
| EWMH-09 | Phase 6 | Pending |
| APPS-01 | Phase 7 | Pending |
| APPS-02 | Phase 7 | Pending |
| APPS-03 | Phase 7 | Pending |
| APPS-04 | Phase 7 | Pending |
| APPS-05 | Phase 7 | Pending |
| XDIS-01 | Phase 8 | Pending |
| XDIS-02 | Phase 8 | Pending |
| XDIS-03 | Phase 8 | Pending |
| XDIS-04 | Phase 8 | Pending |
| XDIS-05 | Phase 8 | Pending |
| FOCUS-01 | Phase 8 | Pending |
| FOCUS-02 | Phase 8 | Pending |
| RULES-01 | Phase 8 | Pending |
| RULES-02 | Phase 8 | Pending |
| CGUI-01 | Phase 9 | Pending |
| CGUI-02 | Phase 9 | Pending |
| CGUI-03 | Phase 9 | Pending |
| CGUI-04 | Phase 9 | Pending |
| CGUI-05 | Phase 9 | Pending |
| TEST-01 | Phase 1 | Complete |
| TEST-02 | Phase 1 | Complete |
| TEST-03 | Phase 3 | Pending |
| TEST-04 | Phase 1 | Complete |

**Coverage:**
- v1 requirements: 49 total
- Mapped to phases: 49
- Unmapped: 0 ✓

---
*Requirements defined: 2026-05-06*
*Last updated: 2026-05-06 after initial definition*
