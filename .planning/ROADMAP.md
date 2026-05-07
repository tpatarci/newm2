# Roadmap: wm2-born-again

## Overview

Modernize wm2 from a 1997 pre-standard C++ codebase into a buildable, maintainable C++17 window manager. Start with build infrastructure and RAII wrappers so every subsequent phase has a solid foundation. Modernize the event loop, then client lifecycle. Rebuild the visual identity with Xft while preserving the classic sideways-tab look. Add runtime configuration, EWMH compliance, and application discovery. Harden for VNC/Xrandr and add focus rules. Cap it off with a GTK3 config GUI so non-programmers can configure the WM visually.

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

Decimal phases appear between their surrounding integers in numeric order.

- [ ] **Phase 1: Build Infrastructure + RAII Foundation** - CMake build system, C++17 modernization, RAII wrappers for X11 resources, test harness on Xvfb
- [ ] **Phase 2: Event Loop Modernization** - Replace select()+goto with poll()-based loop, preserve timing, handle signals
- [ ] **Phase 3: Client Lifecycle with RAII** - ICCCM-compliant reparenting, proper lifecycle management, O(1) client lookup, client state tests
- [ ] **Phase 4: Border + Xft Font Rendering** - Preserve sideways-tab visual identity, replace xvertext with Xft, UTF-8 labels, shaped window fallback
- [ ] **Phase 5: Configuration System** - Runtime config file with key=value format, sensible defaults, CLI option overrides
- [ ] **Phase 6: EWMH Compliance** - All required EWMH atoms, single-desktop mode, panel/taskbar compatibility
- [ ] **Phase 7: Root Menu + Application Discovery** - XDG .desktop parsing, AI-powered binary scan, cached results, category-organized root menu
- [ ] **Phase 8: Xrandr + VNC Compatibility + Focus/Rules** - Display config, extension fallbacks, VNC compatibility, focus stealing prevention, window rules
- [ ] **Phase 9: Config GUI + IPC** - GTK3 config tool, Unix domain socket IPC, live configuration changes, optional dependency

## Phase Details

### Phase 1: Build Infrastructure + RAII Foundation
**Goal**: The project builds from a clean checkout on Ubuntu 22.04+ with a modern C++17 codebase and RAII-managed X11 resources, and the test harness runs on Xvfb.
**Depends on**: Nothing (first phase)
**Requirements**: BLD-01, BLD-02, BLD-03, BLD-04, BLD-05, TEST-01, TEST-02, TEST-04
**Success Criteria** (what must be TRUE):
  1. Running `cmake -B build && cmake --build build` produces a working binary from a clean checkout on Ubuntu 22.04
  2. All X11 resources (Display, Window, GC, Cursor, Font, Pixmap, Colormap) are wrapped in RAII classes that release on scope exit -- no manual XFree calls in application code
  3. Code compiles with `-std=c++17` and uses std::vector, std::string, bool throughout (no char*, no custom list macros, no C-style bools)
  4. No hardcoded X11R6 paths -- all X11 library/include paths discovered via pkg-config in CMakeLists.txt
  5. `ctest` runs the test suite successfully on Xvfb without a physical display, using Catch2 framework
**Plans**: 2 plans

Plans:
- [x] 01-01-PLAN.md -- Build infrastructure, RAII wrappers (x11wrap.h), test harness (Catch2 + Xvfb fixtures)
- [x] 01-02-PLAN.md -- Modernized WM source files (Manager, Client, Border, Events, Buttons, main) producing working binary

### Phase 2: Event Loop Modernization
**Goal**: The window manager's event loop uses a clean poll()-based architecture with correct signal handling, replacing the original select()+goto pattern.
**Depends on**: Phase 1
**Requirements**: EVNT-01, EVNT-02, EVNT-03
**Success Criteria** (what must be TRUE):
  1. The WM processes X11 events via poll() -- no select() or goto statements in the event loop
  2. Windows auto-raise after 400ms hover and pointer-stopped detection works at 80ms granularity, matching original wm2 behavior
  3. Sending SIGTERM or SIGINT to the WM process results in a clean shutdown (windows reparented, X11 connections closed, no resource leaks)
**Plans**: TBD

Plans:
- [x] 02-01: TBD
- [x] 02-02: TBD

### Phase 3: Client Lifecycle with RAII
**Goal**: Window client management uses proper RAII lifecycle control with ICCCM-compliant reparenting and O(1) lookups, replacing `delete this` and custom list macros.
**Depends on**: Phase 2
**Requirements**: CLNT-01, CLNT-02, CLNT-03, CLNT-04, CLNT-05, TEST-03
**Success Criteria** (what must be TRUE):
  1. New windows are reparented using XGrabServer/XUngrabServer -- no race conditions during reparenting
  2. Client objects are created and destroyed exclusively through the WindowManager -- no `delete this` anywhere in the codebase
  3. Client lookup by X11 window ID is O(1) via hash map, not linear scan
  4. A client transitions correctly through Withdrawn -> Normal -> Iconic -> Withdrawn states without leaks or dangling pointers
  5. Automated tests cover core operations: window map, move, resize, hide/unhide, delete -- all passing on Xvfb
**Plans**: 3 plans

Plans:
- [ ] 03-01-PLAN.md -- ServerGrab RAII, ClientState enum with validation, vector colormaps, destructor-only cleanup
- [ ] 03-02-PLAN.md -- unique_ptr ownership, O(1) hash map lookup, move-based hide/unhide, eventDestroy rewrite
- [ ] 03-03-PLAN.md -- Client lifecycle tests (TEST-03): ServerGrab, state machine, window operations on Xvfb

### Phase 4: Border + Xft Font Rendering
**Goal**: Window borders render with the classic wm2 sideways-tab look using Xft for antialiased UTF-8 text, with graceful fallback when the Shape extension is unavailable.
**Depends on**: Phase 3
**Requirements**: VISL-01, VISL-02, VISL-03, VISL-04, VISL-05
**Success Criteria** (what must be TRUE):
  1. Window frames display the classic wm2 sideways-tab visual identity with shaped borders -- visually indistinguishable from the original at first glance
  2. Tab labels render with antialiased Xft fonts using fontconfig patterns (e.g., "Sans-12") -- no core X fonts or XLFD strings anywhere
  3. Window titles containing non-ASCII characters (accented letters, CJK, etc.) display correctly in tab labels
  4. On displays without the Shape extension, windows fall back to rectangular frames without crashes or rendering artifacts
**Plans**: TBD

Plans:
- [ ] 04-01: TBD
- [ ] 04-02: TBD
- [ ] 04-03: TBD

### Phase 5: Configuration System
**Goal**: Users can configure the WM at runtime via a config file with sensible defaults, overriding settings from the command line without recompiling.
**Depends on**: Phase 4
**Requirements**: CONF-01, CONF-02, CONF-03, CONF-04
**Success Criteria** (what must be TRUE):
  1. The WM reads ~/.config/wm2-born-again/config at startup in key=value format -- fonts, colors, focus policy, frame thickness, delays, and menu command are all configurable
  2. Running the WM without any config file works perfectly with built-in defaults (no errors, no missing settings)
  3. Command-line options override config file values (e.g., `wm2-born-again --focus=click` overrides config file focus policy)
  4. Changing the config file and restarting the WM picks up the new settings
**Plans**: TBD

Plans:
- [ ] 05-01: TBD
- [ ] 05-02: TBD

### Phase 6: EWMH Compliance
**Goal**: The WM speaks EWMH so modern applications, panels, and taskbars interact with it correctly on a single-desktop setup.
**Depends on**: Phase 5
**Requirements**: EWMH-01, EWMH-02, EWMH-03, EWMH-04, EWMH-05, EWMH-06, EWMH-07, EWMH-08, EWMH-09
**Success Criteria** (what must be TRUE):
  1. Tools like `wmctrl` and `xprop` report the WM correctly via _NET_SUPPORTING_WM_CHECK and _NET_WM_NAME
  2. _NET_CLIENT_LIST stays in sync as windows are mapped, unmapped, and destroyed -- panels show the correct window list
  3. Applications requesting fullscreen via _NET_WM_STATE_FULLSCREEN get a fullscreen window; maximized hints work similarly
  4. Dock windows (_NET_WM_WINDOW_TYPE_DOCK) are not decorated with frames; notification windows and dialogs are handled appropriately
  5. The WM reports single-desktop atoms (_NET_NUMBER_OF_DESKTOPS=1, _NET_CURRENT_DESKTOP=0, _NET_WORKAREA) so panels display correctly
**Plans**: TBD

Plans:
- [ ] 06-01: TBD
- [ ] 06-02: TBD
- [ ] 06-03: TBD

### Phase 7: Root Menu + Application Discovery
**Goal**: The root menu shows all installed GUI applications organized by category, combining XDG .desktop entries with AI-powered binary discovery and user customizations.
**Depends on**: Phase 6
**Requirements**: APPS-01, APPS-02, APPS-03, APPS-04, APPS-05
**Success Criteria** (what must be TRUE):
  1. Right-clicking the root window shows a menu with applications discovered from XDG .desktop files, organized by category
  2. The AI scanner finds GUI applications in /usr/bin that lack .desktop files and adds them to the menu
  3. Scan results are cached at ~/.config/wm2-born-again/appcache.json so subsequent startups are fast
  4. User can manually add or remove menu entries via the config file, and those entries appear in the root menu
**Plans**: TBD

Plans:
- [ ] 07-01: TBD
- [ ] 07-02: TBD
- [ ] 07-03: TBD

### Phase 8: Xrandr + VNC Compatibility + Focus/Rules
**Goal**: The WM works reliably across VNC, XRDP, and X2Go with graceful extension fallbacks, and users get fine-grained control over focus behavior and per-window rules.
**Depends on**: Phase 7
**Requirements**: XDIS-01, XDIS-02, XDIS-03, XDIS-04, XDIS-05, FOCUS-01, FOCUS-02, RULES-01, RULES-02
**Success Criteria** (what must be TRUE):
  1. Display resolution changes via xrandr are handled correctly -- windows reposition within the new screen geometry
  2. The WM runs on TigerVNC, TightVNC, XRDP, and X2Go without crashes or missing functionality -- extensions that are unavailable simply degrade gracefully (rectangular frames, core fonts, no xrandr)
  3. Focus stealing prevention works -- new windows do not grab focus unless the user interacted with the launching application within a reasonable time window
  4. Users can set focus policy to click-to-focus, focus-follows-pointer, or auto-raise via the config file
  5. Window rules in the config file can match windows by WM_CLASS or WM_NAME and apply actions like no-decorate, specific position/size, or skip-taskbar
**Plans**: TBD

Plans:
- [ ] 08-01: TBD
- [ ] 08-02: TBD
- [ ] 08-03: TBD

### Phase 9: Config GUI + IPC
**Goal**: Non-programmer users can configure the WM visually through a separate GTK3 application that communicates with the running WM via IPC.
**Depends on**: Phase 8
**Requirements**: CGUI-01, CGUI-02, CGUI-03, CGUI-04, CGUI-05
**Success Criteria** (what must be TRUE):
  1. Running `wm2-config` opens a GTK3 window where users can edit fonts, colors, focus policy, frame thickness, and menu entries
  2. The config GUI communicates with the running WM via a Unix domain socket using JSON messages -- changes apply immediately without restart where possible
  3. The WM runs perfectly without GTK3 installed -- the config GUI is an optional separate package
  4. Changes made in the config GUI are persisted to the config file so they survive WM restarts
**Plans**: TBD

Plans:
- [ ] 09-01: TBD
- [ ] 09-02: TBD
- [ ] 09-03: TBD

## Progress

**Execution Order:**
Phases execute in numeric order: 1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 7 -> 8 -> 9

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Build Infrastructure + RAII Foundation | 0/2 | Planning complete | - |
| 2. Event Loop Modernization | 0/2 | Not started | - |
| 3. Client Lifecycle with RAII | 0/3 | Planning complete | - |
| 4. Border + Xft Font Rendering | 0/3 | Not started | - |
| 5. Configuration System | 0/2 | Not started | - |
| 6. EWMH Compliance | 0/3 | Not started | - |
| 7. Root Menu + Application Discovery | 0/3 | Not started | - |
| 8. Xrandr + VNC + Focus/Rules | 0/3 | Not started | - |
| 9. Config GUI + IPC | 0/3 | Not started | - |
