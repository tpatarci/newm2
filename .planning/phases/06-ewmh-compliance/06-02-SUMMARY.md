---
phase: 06-ewmh-compliance
plan: 02
subsystem: wm-protocol
tags: [ewmh, x11, xlib, window-type, fullscreen, maximize, dock-strut, net-wm-state, border-strip]

# Dependency graph
requires:
  - phase: 06-ewmh-compliance/01
    provides: "24 EWMH atoms, updateClientList/updateActiveWindow/updateWorkarea helpers, _NET_CLIENT_LIST sync, _NET_ACTIVE_WINDOW tracking"
provides:
  - "WindowType enum (Normal, Dock, Dialog, Notification) with getWindowType() property reader"
  - "DOCK/NOTIFICATION windows skip border decoration in manage() -- no frame, direct root child"
  - "Border::stripForFullscreen() and restoreFromFullscreen() for fullscreen border strip/restore"
  - "Client::setFullscreen/toggleFullscreen with pre-fullscreen geometry save/restore"
  - "Client::setMaximized/toggleMaximized with workarea-aware fill and geometry save/restore"
  - "Client::applyWmState() with add/remove/toggle semantics for _NET_WM_STATE ClientMessage"
  - "Client::updateNetWmState() setting _NET_WM_STATE property (fullscreen, maximized, hidden)"
  - "WindowManager::updateWorkarea() with dock strut subtraction via _NET_WM_STRUT_PARTIAL/_NET_WM_STRUT"
  - "eventClient() dispatches _NET_WM_STATE to Client::applyWmState()"
  - "PropertyNotify watched for _NET_WM_WINDOW_TYPE and _NET_WM_STRUT/_NET_WM_STRUT_PARTIAL changes"
affects: [06-ewmh-compliance-plan-03]

# Tech tracking
tech-stack:
  added: []
  patterns: [window-type-classification, fullscreen-border-strip-restore, maximize-workarea-fill, ewmh-state-add-remove-toggle, dock-strut-subtraction]

key-files:
  created: []
  modified:
    - include/Client.h
    - src/Client.cpp
    - src/Events.cpp
    - src/Manager.cpp
    - include/Border.h
    - src/Border.cpp

key-decisions:
  - "DOCK and NOTIFICATION windows skip all border creation/reparenting in manage(), mapped directly to root"
  - "Fullscreen strips border via reparent-to-root; restore reparents back into frame"
  - "Maximize reads workarea from root window property to determine fill area"
  - "applyWmState() processes both prop1 and prop2 for simultaneous state changes, fullscreen takes priority over maximize"
  - "updateWorkarea() iterates all clients (both normal and hidden) for dock struts with STRUT_PARTIAL preferred over STRUT"
  - "updateNetWmState() called from hide(), unhide(), setFullscreen(), setMaximized()"
  - "eventDestroy calls updateWorkarea() when a dock client is destroyed"

patterns-established:
  - "EWMH state management: Client stores state in bool members, applies via setter methods, updates _NET_WM_STATE via updateNetWmState()"
  - "Fullscreen lifecycle: save geometry -> strip border -> reparent to root -> resize full screen; reverse for restore"
  - "Maximize lifecycle: save geometry -> read workarea -> configure border to fill; reverse for restore"
  - "Dock strut reading: prefer _NET_WM_STRUT_PARTIAL (12 values), fallback to _NET_WM_STRUT (4 values), clamp to screen dimensions"

requirements-completed: [EWMH-04, EWMH-05, EWMH-06, EWMH-09]

# Metrics
duration: 7min
completed: 2026-05-08
---

# Phase 6 Plan 2: EWMH Window Types, Fullscreen/Maximize State, Dock Struts Summary

**EWMH window type classification (DOCK/DIALOG/NOTIFICATION), fullscreen with border strip/restore, maximize with workarea fill, _NET_WM_STATE add/remove/toggle dispatch, and dock strut subtraction for _NET_WORKAREA**

## Performance

- **Duration:** 7 min
- **Started:** 2026-05-08T06:02:14Z
- **Completed:** 2026-05-08T06:09:15Z
- **Tasks:** 1
- **Files modified:** 6 (include/Client.h, src/Client.cpp, src/Events.cpp, src/Manager.cpp, include/Border.h, src/Border.cpp)

## Accomplishments
- WindowType enum classifies windows as Normal, Dock, Dialog, or Notification during manage()
- DOCK and NOTIFICATION windows skip all border decoration -- no frame, mapped directly as root children
- Fullscreen strips border entirely (reparent to root, resize to full screen including docks) and restores border + geometry on exit
- Maximized windows keep tab+border, expand to fill workarea (screen minus dock struts)
- _NET_WM_STATE ClientMessage dispatch with correct add/remove/toggle semantics from EWMH spec
- updateNetWmState() maintains _NET_WM_STATE property on client windows (fullscreen, maximized, hidden)
- updateWorkarea() reads _NET_WM_STRUT_PARTIAL (fallback to _NET_WM_STRUT) from dock windows, clamped to screen dimensions
- PropertyNotify watched for runtime _NET_WM_WINDOW_TYPE and _NET_WM_STRUT changes
- Workarea recalculated when dock windows appear or are destroyed

## Task Commits

This is a non-git execution. No commits were made. All changes are in the working tree.

## Files Created/Modified
- `include/Client.h` - Added WindowType enum, EWMH state members (m_isFullscreen, m_isMaximizedVert/Horz, pre-state geometry), public query methods (isFullscreen, isMaximized, isDock, isNotification), EWMH state management method declarations (setFullscreen, toggleFullscreen, setMaximized, toggleMaximized, applyWmState, updateNetWmState), private getWindowType declaration
- `src/Client.cpp` - Implemented getWindowType() reading _NET_WM_WINDOW_TYPE, added DOCK/NOTIFICATION skip-decoration in manage(), implemented setFullscreen/toggleFullscreen, setMaximized/toggleMaximized, updateNetWmState(), applyWmState() with add/remove/toggle, added PropertyNotify handlers for window type and strut changes, added updateNetWmState() calls in hide() and unhide()
- `src/Events.cpp` - Added _NET_WM_STATE handler in eventClient() dispatching to Client::applyWmState(), added updateWorkarea() call in eventDestroy() for dock clients, reduced fprintf noise for known EWMH messages
- `src/Manager.cpp` - Replaced updateWorkarea() stub with full dock strut subtraction implementation (_NET_WM_STRUT_PARTIAL preferred, fallback to _NET_WM_STRUT, clamped to screen dimensions)
- `include/Border.h` - Added stripForFullscreen() and restoreFromFullscreen() declarations
- `src/Border.cpp` - Implemented stripForFullscreen() (unmap frame, reparent child to root) and restoreFromFullscreen() (reparent child back to frame, configure position, resize, map)

## Decisions Made
- **DOCK/NOTIFICATION skip border entirely in manage():** Rather than creating and then hiding a border, these windows never get border objects. The early return in manage() skips all border/reparenting code.
- **Fullscreen via reparent-to-root:** Strip border by reparenting the client window directly to root at (0,0). Restore by reparenting back into the frame. This is the EWMH-correct approach (D-05: fullscreen covers full screen including dock areas).
- **Maximize reads workarea from root property:** setMaximized() reads _NET_WORKAREA from the root window to determine the fill area. This means maximize always respects the current dock strut state.
- **applyWmState processes both props:** The EWMH spec allows simultaneous state changes via data.l[1] and data.l[2]. applyWmState() checks both and applies fullscreen before maximize, with fullscreen taking priority.
- **updateWorkarea iterates both client vectors:** Dock clients might be in either m_clients or m_hiddenClients. The implementation checks both to catch all docks.
- **Strut values clamped to screen dimensions:** Security mitigation (T-06-05) prevents absurd strut values from consuming the entire screen.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None - implementation followed the plan closely.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- EWMH window type handling, fullscreen/maximize state, and dock struts are complete.
- Plan 03 can now add: circular right-button gesture for fullscreen toggle in Buttons.cpp, maximize click toggle on border/tab area, and any remaining mouse-driven interactions.
- All existing 97 tests continue to pass.

---
*Phase: 06-ewmh-compliance*
*Completed: 2026-05-08*

## Self-Check: PASSED

All files exist and contain expected content:
- include/Client.h: FOUND (contains WindowType enum, m_isFullscreen)
- src/Client.cpp: FOUND (contains getWindowType, setFullscreen, applyWmState, updateNetWmState)
- src/Events.cpp: FOUND (contains net_wmState dispatch, updateWorkarea on dock destroy)
- src/Manager.cpp: FOUND (contains net_wmStrutPartial in updateWorkarea)
- include/Border.h: FOUND (contains stripForFullscreen)
- src/Border.cpp: FOUND (contains stripForFullscreen implementation)
- Build: PASS (all targets compiled)
- Tests: PASS (97/97, including 9 EWMH tests)
