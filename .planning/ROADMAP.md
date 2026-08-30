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
- [x] **Phase 6: EWMH Compliance** - All required EWMH atoms, single-desktop mode, panel/taskbar compatibility
- [x] **Phase 7: Root Menu + Application Discovery** - XDG .desktop parsing, heuristic-based binary scan, cached results, category-organized root menu (completed 2026-07-08)
- [~] **Phase 8: Xrandr + VNC Compatibility + Focus/Rules** - Display config, extension fallbacks, VNC compatibility, focus stealing prevention, window rules. **All 14 plans complete; 5 of 7 success criteria verified.** Two remain unmet and are declared, not stubbed: four-target remote-desktop coverage (2 of 4 exercised — XDIS-05) and WM_NAME matching (RULES-01). See VERIFICATION.md.
- [ ] **Phase 8.5: v1.0 Closeout** *(INSERTED 2026-08-30)* - Finish the three requirements Phase 8 left Pending: title matching for window rules plus the config key rename (RULES-01), the two remaining remote-desktop targets (XDIS-05), and the interaction checklist as a per-item table (TEST-08). Inserted **before** Phase 9 because RULES-01 changes the config surface the GUI will expose.
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

**Plans**: 3 plans

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

- [x] 03-01-PLAN.md -- ServerGrab RAII, ClientState enum with validation, vector colormaps, destructor-only cleanup
- [x] 03-02-PLAN.md -- unique_ptr ownership, O(1) hash map lookup, move-based hide/unhide, eventDestroy rewrite
- [x] 03-03-PLAN.md -- Client lifecycle tests (TEST-03): ServerGrab, state machine, window operations on Xvfb

### Phase 4: Border + Xft Font Rendering

**Goal**: Window borders render with the classic wm2 sideways-tab look using Xft for antialiased UTF-8 text, with graceful fallback when the Shape extension is unavailable.
**Depends on**: Phase 3
**Requirements**: VISL-01, VISL-02, VISL-03, VISL-04, VISL-05
**Success Criteria** (what must be TRUE):

  1. Window frames display the classic wm2 sideways-tab visual identity with shaped borders -- visually indistinguishable from the original at first glance
  2. Tab labels render with antialiased Xft fonts using fontconfig patterns (e.g., "Sans-12") -- no core X fonts or XLFD strings anywhere
  3. Window titles containing non-ASCII characters (accented letters, CJK, etc.) display correctly in tab labels
  4. On displays without the Shape extension, windows fall back to rectangular frames without crashes or rendering artifacts

**Plans**: 3 plans

Plans:
**Wave 1**

- [x] 04-01-PLAN.md -- Install Xft/fontconfig, RAII wrappers (XftFontPtr, XftDrawPtr, XftColorWrap), PoC test validating Xft in shaped windows

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 04-02-PLAN.md -- Border tab Xft rendering: replace xvertext with FcMatrix rotation, rectangular Shape fallback (VISL-01, VISL-03, VISL-05)
- [x] 04-03-PLAN.md -- Menu Xft rendering: replace core X fonts, remove Rotated.C, explicit highlight (VISL-02, VISL-04)

### Phase 5: Configuration System

**Goal**: Users can configure the WM at runtime via a config file with sensible defaults, overriding settings from the command line without recompiling.
**Depends on**: Phase 4
**Requirements**: CONF-01, CONF-02, CONF-03, CONF-04
**Success Criteria** (what must be TRUE):

  1. The WM reads ~/.config/wm2-born-again/config at startup in key=value format -- fonts, colors, focus policy, frame thickness, delays, and menu command are all configurable
  2. Running the WM without any config file works perfectly with built-in defaults (no errors, no missing settings)
  3. Command-line options override config file values (e.g., `wm2-born-again --focus=click` overrides config file focus policy)
  4. Changing the config file and restarting the WM picks up the new settings

**Plans**: 3 plans

**Wave 1**

- [x] 05-01-PLAN.md -- Config struct, XDG paths, key=value file parser, unit tests (CONF-01, CONF-02, CONF-03)

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 05-02-PLAN.md -- getopt_long CLI parsing, boolean --no-xxx flags, CLI override tests (CONF-04)

**Wave 3** *(blocked on Waves 1+2 completion)*

- [x] 05-03-PLAN.md -- Wire config into WindowManager, Border, Events; FRAME_WIDTH runtime (all CONF)

**Cross-cutting constraints:**

- Config passed as `const Config&` to WindowManager (no singleton/global)
- Color validation deferred to `initialiseScreen()` after X11 display opens

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

**Plans**: 3 plans

**Wave 1**

- [x] 06-01-PLAN.md -- EWMH atoms, WM check window, root properties, _NET_CLIENT_LIST, _NET_ACTIVE_WINDOW, single-desktop atoms, test scaffold (EWMH-01, EWMH-02, EWMH-03, EWMH-07, EWMH-08)

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 06-02-PLAN.md -- Window type handling, fullscreen/maximize state, _NET_WM_STATE dispatch, dock struts, border strip/restore (EWMH-04, EWMH-05, EWMH-06, EWMH-09)

**Wave 3** *(blocked on Wave 2 completion)*

- [x] 06-03-PLAN.md -- Circular right-button gesture for fullscreen, Button2 maximize on tab, EWMH state machine tests (EWMH-06)

### Phase 7: Root Menu + Application Discovery

**Goal**: The root menu shows all installed GUI applications organized by category, combining XDG .desktop entries with heuristic-based binary discovery and user customizations.
**Depends on**: Phase 6
**Requirements**: APPS-01, APPS-02, APPS-03, APPS-04, APPS-05
**Success Criteria** (what must be TRUE):

  1. Right-clicking the root window shows a menu with applications discovered from XDG .desktop files, organized by category
  2. The AI scanner finds GUI applications in /usr/bin that lack .desktop files and adds them to the menu
  3. Scan results are cached at ~/.config/wm2-born-again/appcache.json so subsequent startups are fast
  4. User can manually add or remove menu entries via the config file, and those entries appear in the root menu

**Plans**: 6/6 plans complete

**Wave 1**

- [x] 07-01-PLAN.md -- XDG .desktop parser: AppEntry model, Exec tokenizer/field-code validator, D-05 filtering (APPS-01)

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 07-02-PLAN.md -- Heuristic /usr/bin binary scanner: ELF DT_NEEDED reader, GUI/CLI classifier (APPS-02)
- [x] 07-04-PLAN.md -- WindowManager apps/category wiring, spawnArgv()/launchApp() safe process dispatch (APPS-05 backend)

**Wave 3** *(blocked on Wave 2 completion)*

- [x] 07-03-PLAN.md -- AppCache (hand-rolled JSON cache, mtime invalidation, D-07/D-08 merge) + Config manual menu entries (APPS-03, APPS-04)
- [x] 07-05-PLAN.md -- Categorized root menu + hover-to-expand submenu rendering in Buttons.cpp (APPS-05 presentation)

**Wave 4** *(blocked on Wave 3 completion)*

- [x] 07-06-PLAN.md -- main.cpp startup wiring + manual verification checkpoint (APPS-01 through APPS-05 end-to-end)

### Phase 8: Xrandr + VNC Compatibility + Focus/Rules

**Goal**: The WM works reliably across VNC, XRDP, and X2Go with graceful extension fallbacks, users get fine-grained control over focus behavior and per-window rules, and the compiled binary is proven through repeatable build, sanitizer, Xvfb/Xephyr, and runtime smoke gates.
**Depends on**: Phase 7
**Requirements**: XDIS-01, XDIS-02, XDIS-03, XDIS-04, XDIS-05, FOCUS-01, FOCUS-02, RULES-01, RULES-02, TEST-05, TEST-06, TEST-07, TEST-08
**Success Criteria** (what must be TRUE):

  1. Display resolution changes via xrandr are handled correctly -- windows reposition within the new screen geometry
  2. The WM runs on TigerVNC, TightVNC, XRDP, and X2Go without crashes or missing functionality -- extensions that are unavailable simply degrade gracefully (rectangular frames, core fonts, no xrandr)
  3. Focus stealing prevention works -- new windows do not grab focus unless the user interacted with the launching application within a reasonable time window
  4. Users can set focus policy to click-to-focus, focus-follows-pointer, or auto-raise via the config file
  5. Window rules in the config file can match windows by WM_CLASS or WM_NAME and apply actions like no-decorate, specific position/size, or skip-taskbar
  6. COMPILED_CODE_BEHAVIOR_CHECKLIST.md gates pass or have explicit accepted exceptions: dependency preflight, Debug/Release builds, full CTest, ASan/UBSan, runtime Xvfb/Xephyr smoke, and release evidence
  7. Current behavior-scan findings are resolved or explicitly accepted: missing xft/fontconfig build preflight, eventDestroy client lifetime hazard, no-Shape fallback proof, and focus config wiring proof

**Plans**: 13/14 plans executed

> **Plan-count revision (2026-08-11).** The original 3-plan skeleton is superseded. D-09 requires all 13 "Missing Automated Coverage To Add" checklist items to land in this phase, and D-04 makes four build/sanitizer/static-analysis/evidence gates unwaivable. The original 08-01 alone expands into six plans (harness, gates, and three coverage plans plus the evidence bundle). Scope is unchanged -- nothing was dropped or deferred; the same work is sliced into executable 2-4 task units. The three original plan themes map to: 08-01/02/11/12/13 (verification gate), 08-03/04/05/06/14 (Xrandr + remote desktop), 08-07/08/09/10 (focus + rules).

Plans execute as a linear chain: `parallelization` is disabled in `.planning/config.json`, and nearly every plan touches `CMakeLists.txt`, `src/Manager.cpp` or `src/Client.cpp`, so no two plans have disjoint `files_modified`.

Plans:

**Wave 1**

- [x] 08-01-PLAN.md -- Build unblock (libxft-dev/libfontconfig1-dev, clean build trees), process-level tracer proving the real binary under Xvfb with the eventDestroy use-after-free fixed, preflight script + blocking ctest fixture, XTestDriver (TEST-05, TEST-07)

**Wave 2** *(blocked on Wave 1)*

- [x] 08-02-PLAN.md -- Build-all gate (Debug/Release/ASan), two-mechanism static-analysis gate (hash-anchored cppcheck baseline + clang-tidy allowlist), forked-child sanitizer noise fix (TEST-06)

**Wave 3** *(blocked on Wave 2)*

- [x] 08-03-PLAN.md -- Shape funnel: 26 raw call sites collapsed into one guarded `combineShape()`, `WM2_FORCE_NO_SHAPE` lever, rectangular-fallback proof + greppable invariant (XDIS-03)

**Wave 4** *(blocked on Wave 3)*

- [x] 08-04-PLAN.md -- Screen-geometry accessor refactor: `screenWidth()`/`screenHeight()` as single source of truth, 16 direct reads rerouted, pre-refactor values pinned by tests (XDIS-01 foundation)

**Wave 5** *(blocked on Wave 4)*

- [x] 08-05-PLAN.md -- Xrandr wiring: required pkg-config dep, capability query, root StructureNotifyMask, idempotent geometry-change handler with the mandatory Xlib cache refresh, D-25 reflow, proof on a RANDR-less server (XDIS-01, XDIS-02)

**Wave 6** *(blocked on Wave 5)*

- [x] 08-06-PLAN.md -- XRender-less degradation: rotated-font spike, fatal-to-ladder rewrite in Border, RENDER-less end-to-end proof, XDIS-04 requirement-wording amendment (XDIS-04)

**Wave 7** *(blocked on Wave 6)*

- [x] 08-07-PLAN.md -- Focus policy wiring: the three dead booleans gated to real runtime behavior, defaults corrected to match shipped behavior, process-level focus tests (FOCUS-02)

**Wave 8** *(blocked on Wave 7)*

- [x] 08-08-PLAN.md -- Focus stealing prevention: five new atoms advertised, last-user-interaction clock, map-time arbitration with DEMANDS_ATTENTION, `_NET_ACTIVE_WINDOW` arbitration (blocking decision checkpoint -- overturns Phase 6 D-10), configurable off switch (FOCUS-01)

**Wave 9** *(blocked on Wave 8)*

- [x] 08-09-PLAN.md -- Window rules model + parser: X11-free `Rules.h`/`Rules.cpp`, repeated-key-group `rule-*` config keys, exact/substring AND matching, later-wins fold, display-free unit tests (RULES-01)

**Wave 10** *(blocked on Wave 9)*

- [x] 08-10-PLAN.md -- Window rule application: `XGetClassHint` read, no-decorate / position+size / skip-taskbar at map time via existing primitives, RULES-02 requirement-wording amendment recording the workspace exclusion (RULES-02)

**Wave 11** *(blocked on Wave 10)*

- [x] 08-11-PLAN.md -- Coverage items 2, 4, 8, 9: normal-client destroy, hidden-list transfers, all size-hint resize constraints, all gravity modes (TEST-05)

**Wave 12** *(blocked on Wave 11)*

- [x] 08-12-PLAN.md -- Coverage items 10, 11, 12: EWMH client-message matrix, fullscreen/maximize restore in awkward orderings, malformed client properties (TEST-05)

**Wave 13** *(blocked on Wave 12)*

- [x] 08-13-PLAN.md -- Coverage items 5, 6, 13: config-to-runtime behavior, the four terminating X11 error paths, 100-window stress under ASan, plus a working `--help` (TEST-05)

**Wave 14** *(blocked on Wave 13)*

- [ ] 08-14-PLAN.md -- Capability-capture script, enforced RSS/idle-CPU budget, real TigerVNC/XRDP/X2Go sessions (blocking human-verify checkpoint), release notes with the single-screen limitation, corrected checklist baseline, committed evidence bundle, final run of all four hard blockers (XDIS-05, TEST-06, TEST-08)

### Phase 8.5: v1.0 Closeout *(INSERTED)*

**Goal**: v1.0 ships with zero requirements silently unmet — window rules match by title under keys named for what they compare, all four remote-desktop targets have committed transcripts, and the interaction record is a per-item table rather than a verdict.
**Depends on**: Phase 8
**Requirements**: RULES-01, XDIS-05, TEST-08
**Inserted because**: RULES-01 changes the config surface Phase 9's GTK tool will expose and edit. Landing it after the GUI is built means rework that buys nothing; landing it first lets Phase 9's record be about the GUI.

**Success Criteria** (what must be TRUE):

  1. A rule can match a window by its title, and the title matched is the one the window actually advertises — `_NET_WM_NAME` preferred, `WM_NAME` as fallback
  2. The three match keys are named for what they compare: `rule-match-class`, `rule-match-instance`, `rule-match-title` — with `rule-match-name` gone, not aliased
  3. The map-time fold semantic is stated in the requirement, the release notes and the code — a renamed document does not move its window, and that is a documented choice rather than a discovered surprise
  4. All four remote-desktop targets named in PROJECT.md have a committed capability transcript captured at a known commit
  5. The User Interaction Checklist exists as a per-item pass/fail table with a tester name and a date
  6. Both Phase 8 deviations are retired on evidence or restated with a reason that is true — neither survives as written
  7. The four hard blockers are re-run at the final commit, not inherited from Phase 8's snapshot

**Plans**: 2/5 plans executed (2 executed, 3 gap-closure)

Plans:

**Wave 1**

- [x] 08.5-01-PLAN.md -- Title matching: EWMH-first title read, `rule-match-title`, the `rule-match-instance` rename, process-level proof, release-notes and ledger correction (RULES-01)

**Wave 2** *(blocked on Wave 1)*

- [x] 08.5-02-PLAN.md -- TightVNC and nxagent transcripts captured headlessly, interaction checklist as a table (blocking human checkpoint), deviations retired or restated, gates re-run (XDIS-05, TEST-08)

**Gap closure** *(added after `08.5-VERIFICATION.md` scored the phase 5/7; run with `/gsd-execute-phase 08.5 --gaps-only`)*

Gap-closure wave 1:

- [ ] 08.5-03-PLAN.md -- Rewrite the shipped remote-desktop section of `docs/RELEASE-NOTES.md` to the four measured statuses, retiring the TightVNC rationale the phase's own transcript disproved and narrowing the X2Go deviation to the NX proxy; prove the rules-key record survived the edit (criterion 6; XDIS-05, RULES-01)

Gap-closure wave 2 *(blocked on gap-closure wave 1)*:

- [ ] 08.5-04-PLAN.md -- Capture all four hard blockers at the final commit into `08.5-v1.0-closeout/evidence/gates/`, with a `PROVENANCE.txt` that proves no source moved between the gate commit and HEAD (criterion 7, capture half; TEST-08)

Gap-closure wave 3 *(blocked on gap-closure wave 2)*:

- [ ] 08.5-05-PLAN.md -- Repoint every gate row in `COMPILED_CODE_BEHAVIOR_CHECKLIST.md` at this phase's bundle, recompute the test-surface row with its own commands, update the interaction rows from the filled table, and write `evidence/README.md` (criterion 7, record half; TEST-08)

**Key finding that shaped this phase (2026-08-30):** the handoff's decided shape moved X2Go to a v1.1 requirement on the reasoning that validating it cost a human session. Measurement falsified that — `x2goserver`/`nxagent` were already installed (dpkg stamp 2026-08-29 17:43), nxagent runs headless nested on an Xvfb, and it advertises SHAPE, RANDR and RENDER. The WM was run against it successfully. So both remaining targets are validated rather than amended away, and XDIS-05 is expected to be met **as written**. See `08.5-CONTEXT.md`, "What changed since the handoff".

### Phase 9: Config GUI + IPC

**Goal**: Non-programmer users can configure the WM visually through a separate GTK3 application that communicates with the running WM via IPC.
**Depends on**: Phase 8
**Requirements**: CGUI-01, CGUI-02, CGUI-03, CGUI-04, CGUI-05
**Success Criteria** (what must be TRUE):

  1. Running `wm2-config` opens a GTK3 window where users can edit fonts, colors, focus policy, frame thickness, and menu entries
  2. The config GUI communicates with the running WM via a Unix domain socket using JSON messages -- changes apply immediately without restart where possible
  3. The WM runs perfectly without GTK3 installed -- the config GUI is an optional separate package
  4. Changes made in the config GUI are persisted to the config file so they survive WM restarts

**Plans**: 3 plans

Plans:

- [ ] 09-01: TBD
- [ ] 09-02: TBD
- [ ] 09-03: TBD

## Progress

**Execution Order:**
Phases execute in numeric order: 1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 7 -> 8 -> 8.5 -> 9

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Build Infrastructure + RAII Foundation | 0/2 | Planning complete | - |
| 2. Event Loop Modernization | 0/2 | Not started | - |
| 3. Client Lifecycle with RAII | 0/3 | Planning complete | - |
| 4. Border + Xft Font Rendering | 3/3 | Complete | 2026-05-07 |
| 5. Configuration System | 0/3 | Planned | - |
| 6. EWMH Compliance | 0/3 | Planned | - |
| 7. Root Menu + Application Discovery | 6/6 | Complete    | 2026-07-08 |
| 8. Xrandr + VNC + Focus/Rules | 14/14 | Verified with gaps | 2026-08-30 |
| 8.5 v1.0 Closeout *(INSERTED)* | 2/5 | In Progress|  |
| 9. Config GUI + IPC | 0/3 | Not started | - |
