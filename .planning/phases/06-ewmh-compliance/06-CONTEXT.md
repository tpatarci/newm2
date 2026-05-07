# Phase 6: EWMH Compliance - Context

**Gathered:** 2026-05-07
**Status:** Ready for planning

<domain>
## Phase Boundary

Make the window manager speak EWMH (Extended Window Manager Hints) so modern applications, panels, and taskbars interact with it correctly on a single-desktop setup. This covers: WM identification, client list tracking, window type handling, fullscreen/maximize state, activation requests, and single-desktop atoms.

Requirements: EWMH-01 through EWMH-09.

**In scope:**
- Set _NET_SUPPORTED listing all supported EWMH atoms on the root window
- Set _NET_SUPPORTING_WM_CHECK + _NET_WM_NAME for WM identification
- Maintain _NET_CLIENT_LIST in sync (updated on map/unmap/destroy)
- Handle _NET_ACTIVE_WINDOW client messages (grant activation requests)
- Handle _NET_WM_WINDOW_TYPE (DOCK, DIALOG, NOTIFICATION, NORMAL, UTILITY, SPLASH, TOOLBAR)
- Handle _NET_WM_STATE (_NET_WM_STATE_FULLSCREEN, _NET_WM_STATE_MAXIMIZED_VERT/HORZ)
- Read _NET_WM_STRUT/_NET_WM_STRUT_PARTIAL from dock windows to reserve screen space
- Set _NET_WM_NAME with UTF-8 encoding on the WM check window
- Set single-desktop atoms (_NET_NUMBER_OF_DESKTOPS=1, _NET_CURRENT_DESKTOP=0, _NET_WORKAREA)
- Mouse-driven fullscreen toggle (circular right-button gesture)
- Mouse-driven maximize toggle (click on border/tab area)

**Out of scope:**
- Focus stealing prevention (Phase 8)
- Keyboard shortcuts for fullscreen/maximize (Phase 8 / v2)
- Virtual desktops/workspaces (out of scope per PROJECT.md)
- Multi-monitor EWMH (single screen only per PROJECT.md)
- _NET_WM_PING for hung window detection (v2: PLSH-02)
- Startup notification _NET_STARTUP_INFO (v2: PLSH-01)

</domain>

<decisions>
## Implementation Decisions

### Window Type Handling
- **D-01:** DOCK windows get no frame decoration. The WM manages their position by reading _NET_WM_STRUT/_NET_WM_STRUT_PARTIAL to determine which screen edge the dock occupies. Docks float above normal windows.
- **D-02:** NOTIFICATION windows get no frame decoration. They float above other windows. The WM honors their requested position (notification daemons position themselves).
- **D-03:** DIALOG windows get normal wm2 frame decoration with transient stacking (keep above their transient-for parent).
- **D-04:** UTILITY, SPLASH, and TOOLBAR window types are treated as NORMAL — standard wm2 tab+border decoration.

### Fullscreen Behavior
- **D-05:** When a window goes fullscreen (via _NET_WM_STATE_FULLSCREEN from app, or mouse gesture), the wm2 border/tab is stripped entirely and the window covers the full screen geometry including dock areas.
- **D-06:** Fullscreen is toggled via a **circular right-button mouse gesture** — drawing a circle with the right button held toggles fullscreen on/off. This works in addition to EWMH client message requests from apps.

### Maximize Behavior
- **D-07:** Maximized windows (via _NET_WM_STATE_MAXIMIZED_VERT + _NET_WM_STATE_MAXIMIZED_HORZ) keep the wm2 sideways tab+border but expand to fill the workarea (screen geometry minus dock struts).
- **D-08:** Maximize is toggled via **clicking on the border/tab area**. Clicking the tab or frame toggles maximize on/off. This works in addition to EWMH client message requests from apps.

### Dock Strut Handling
- **D-09:** The WM reads _NET_WM_STRUT (and _NET_WM_STRUT_PARTIAL if available) from dock windows to determine reserved screen edges. The _NET_WORKAREA property on the root window is set to the screen area minus dock struts. Normal client windows and maximized windows respect the workarea.

### Activation Policy
- **D-10:** Phase 6 always honors _NET_ACTIVE_WINDOW client messages — when an app requests activation, the WM grants it (sets focus, raises the window). Phase 8 adds focus stealing prevention with timestamp checks and user-interaction heuristics.

### Root Window Properties
- **D-11:** The following properties are set on the root window at startup and maintained throughout:
  - _NET_SUPPORTED: list of all EWMH atoms the WM supports
  - _NET_SUPPORTING_WM_CHECK: window ID of the WM check window
  - _NET_WM_NAME: "wm2-born-again" (UTF-8) on the check window
  - _NET_NUMBER_OF_DESKTOPS: 1
  - _NET_CURRENT_DESKTOP: 0
  - _NET_WORKAREA: [x, y, width, height] adjusted for dock struts
  - _NET_CLIENT_LIST: array of client window IDs, updated on map/unmap/destroy
  - _NET_ACTIVE_WINDOW: currently focused client window ID (or None)

### Claude's Discretion
- Exact detection algorithm for circular right-button gesture (minimum radius, angular threshold, timing window)
- How to detect border/tab click for maximize toggle (which window/button combination)
- Atom interning strategy (all in Atoms struct, or separate EWMH namespace)
- Whether to read _NET_WM_STRUT or _NET_WM_STRUT_PARTIAL first (prefer PARTIAL, fall back to STRUT)
- How to handle windows that set both DOCK and another window type
- State storage for fullscreen/maximize (in Client class or separate)
- Whether to store pre-fullscreen/maximize geometry for restore
- Event processing for _NET_WM_STATE client messages (add/remove/toggle semantics per EWMH spec)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Project Context
- `.planning/PROJECT.md` — Project definition, core value, constraints, key decisions
- `.planning/REQUIREMENTS.md` — EWMH-01 through EWMH-09 requirements with traceability
- `.planning/ROADMAP.md` — Phase 6 goal, success criteria, and requirements mapping

### Prior Phase Decisions
- `.planning/phases/05-configuration-system/05-CONTEXT.md` — Phase 5 decisions (Config struct, XDG paths, CLI overrides)
- `.planning/phases/04-border-xft-font-rendering/04-CONTEXT.md` — Phase 4 decisions (Xft rendering, Noto Sans font, shaped windows)
- `.planning/phases/03-client-lifecycle-with-raii/03-CONTEXT.md` — Phase 3 decisions (unique_ptr ownership, ClientState enum, hash map lookup)

### Codebase Analysis
- `.planning/codebase/INTEGRATIONS.md` — Current ICCCM atom usage (WM_STATE, WM_PROTOCOLS, etc.) and event handling patterns
- `.planning/codebase/ARCHITECTURE.md` — Event flow, window lifecycle, atoms used (currently ICCCM only — no _NET_* support)

### Source Files (Primary Targets)
- `include/Manager.h` — Atoms struct (lines 185-193), WindowManager class, event handlers, client lists
- `src/Manager.cpp` — Atom interning (lines 96-103), root window setup, client list management
- `src/Events.cpp` — eventClient() handler (line 284) — currently only handles WM_CHANGE_STATE, needs EWMH extension
- `src/Client.cpp` — Client lifecycle, property handling, state management, window operations
- `include/Client.h` — Client class with state, properties, window operations
- `src/Border.cpp` — Frame decoration (for fullscreen strip / maximize resize)
- `include/Border.h` — Border class with reparent, shape methods
- `src/Buttons.cpp` — Mouse event handling (for fullscreen/maximize mouse gestures)

### Upstream Reference
- `upstream-wm2/General.h` — Original ICCCM atom definitions (lines 52-61)
- `upstream-wm2/Manager.C` — Original WM identification via _WM2_RUNNING selection

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `include/Manager.h:185-193` — `Atoms` struct with static Atom members. EWMH atoms are added to this struct (or a parallel `EwmhAtoms` struct). Same intern-once, use-everywhere pattern.
- `src/Manager.cpp:96-103` — Atom interning via `XInternAtom()` in WindowManager constructor. EWMH atoms follow the same pattern.
- `src/Manager.cpp:367-368` — `XChangeProperty()` on root window for `_WM2_RUNNING`. Same pattern for _NET_* root window properties.
- `src/Events.cpp:284-297` — `eventClient()` handles `WM_CHANGE_STATE` ClientMessage. EWMH _NET_ACTIVE_WINDOW and _NET_WM_STATE also come as ClientMessage events — extend this handler.
- `src/Client.cpp:349` — `XChangeProperty()` for WM_STATE on client window. Same pattern for per-client EWMH properties.
- `include/x11wrap.h` — RAII wrappers for X11 resources. No new X11 resource types needed for EWMH (atoms are just `Atom` integers, properties are set on existing windows).

### Established Patterns
- `m_` prefix for member variables — retained
- Atoms as static members of `Atoms` struct, interned once in constructor — EWMH atoms follow this
- Client events dispatched through WindowManager event loop — EWMH client messages flow through same `eventClient()`
- Properties set/changed via `XChangeProperty()` — all EWMH root/client properties use this
- `std::vector<std::unique_ptr<Client>>` for client list — _NET_CLIENT_LIST iterates this
- `std::unordered_map<Window, Client*>` for O(1) lookup — used for EWMH message target resolution

### Integration Points
- `src/Events.cpp:284` — `eventClient()` is the primary extension point for _NET_ACTIVE_WINDOW and _NET_WM_STATE client messages
- `src/Manager.cpp:96-103` — Atom interning block is where EWMH atoms are added
- `src/Manager.cpp:initialiseScreen()` — Where root window properties (_NET_SUPPORTED, _NET_SUPPORTING_WM_CHECK, desktop atoms, _NET_WORKAREA) are set after screen initialization
- `src/Client.cpp:manage()` / `~Client()` — Where _NET_CLIENT_LIST is updated (add on manage, remove on destroy)
- `src/Client.cpp:setState()` — Where client state changes happen — potential hook for fullscreen/maximize state transitions
- `src/Border.cpp` — Fullscreen needs to remove border decoration; maximize needs to resize frame to workarea
- `src/Buttons.cpp` — Mouse event handling — circular gesture detection and border/tab click detection for fullscreen/maximize toggles
- `include/Manager.h:111-114` — Client vectors and map — _NET_CLIENT_LIST reads from m_clients + m_hiddenClients

</code_context>

<specifics>
## Specific Ideas

- Circular right-button mouse gesture for fullscreen toggle — this is a distinctive wm2-born-again interaction pattern
- Click on border/tab area for maximize toggle — intuitive since the tab IS the wm2 identity
- WM name reported as "wm2-born-again" via _NET_WM_NAME (UTF-8)

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope.

</deferred>

---

*Phase: 6-EWMH Compliance*
*Context gathered: 2026-05-07*
