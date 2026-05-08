---
phase: 06-ewmh-compliance
plan: 01
subsystem: wm-protocol
tags: [ewmh, x11, xlib, atoms, root-properties, net-supported, net-client-list, net-active-window]

# Dependency graph
requires:
  - phase: 05-configuration-system
    provides: "Config infrastructure, build system with CMake/Catch2/Xvfb"
provides:
  - "All 24 EWMH atoms interned as Atoms::net_* static members"
  - "_NET_SUPPORTING_WM_CHECK with self-referencing child window and UTF8_STRING _NET_WM_NAME"
  - "_NET_SUPPORTED atom array on root window listing all supported EWMH atoms"
  - "_NET_CLIENT_LIST maintained in sync with client lifecycle (manage/hide/unhide/destroy)"
  - "_NET_ACTIVE_WINDOW tracking focused client, updated on activate/clearFocus"
  - "_NET_NUMBER_OF_DESKTOPS=1, _NET_CURRENT_DESKTOP=0, _NET_WORKAREA=[0,0,W,H]"
  - "updateClientList(), updateActiveWindow(), updateWorkarea() public helper methods"
  - "eventClient() dispatches _NET_ACTIVE_WINDOW ClientMessage to Client::activate()"
  - "9 EWMH property protocol tests on Xvfb"
affects: [06-ewmh-compliance-plan-02, 06-ewmh-compliance-plan-03]

# Tech tracking
tech-stack:
  added: []
  patterns: [ewmh-atom-interning, wm-check-window-self-reference, net-client-list-sync, net-active-window-tracking]

key-files:
  created:
    - tests/test_ewmh.cpp
  modified:
    - include/Manager.h
    - src/Manager.cpp
    - src/Events.cpp
    - src/Client.cpp
    - CMakeLists.txt

key-decisions:
  - "EWMH helpers (updateClientList, updateActiveWindow, updateWorkarea) moved to public section of WindowManager since Client::activate() needs to call updateActiveWindow()"
  - "updateWorkarea() is a stub that sets full screen geometry; dock strut subtraction deferred to Plan 02"
  - "Tests verify X11 property protocol patterns directly via raw Xlib, not the WM binary"

patterns-established:
  - "EWMH atom interning: All _NET_* atoms follow same XInternAtom pattern as ICCCM atoms"
  - "EWMH property helpers: public methods on WindowManager that call XChangeProperty on root"
  - "Client lifecycle EWMH sync: updateClientList() called after every vector mutation"

requirements-completed: [EWMH-01, EWMH-02, EWMH-03, EWMH-07, EWMH-08]

# Metrics
duration: 15min
completed: 2026-05-08
---

# Phase 6 Plan 1: EWMH Foundation Summary

**EWMH root window properties with 24 interned atoms, WM check child window, _NET_CLIENT_LIST lifecycle sync, and _NET_ACTIVE_WINDOW focus tracking**

## Performance

- **Duration:** 15 min
- **Started:** 2026-05-08T05:40:50Z
- **Completed:** 2026-05-08T05:56:21Z
- **Tasks:** 1 (TDD: RED + GREEN)
- **Files modified:** 5 (Manager.h, Manager.cpp, Events.cpp, Client.cpp, CMakeLists.txt)
- **Files created:** 1 (tests/test_ewmh.cpp)

## Accomplishments
- All 24 EWMH atoms interned and accessible as Atoms::net_* static members
- _NET_SUPPORTING_WM_CHECK set on root (pointing to check window) and check window (self-reference) with _NET_WM_NAME set using UTF8_STRING encoding
- _NET_CLIENT_LIST maintained in sync across 4 lifecycle events: client creation, hide, unhide, destroy
- _NET_ACTIVE_WINDOW tracks focused client via Client::activate() and clearFocus()
- Single-desktop atoms set correctly: _NET_NUMBER_OF_DESKTOPS=1, _NET_CURRENT_DESKTOP=0, _NET_WORKAREA=[0,0,W,H]
- eventClient() now handles _NET_ACTIVE_WINDOW ClientMessage by dispatching to Client::activate()
- WM check window properly cleaned up in WindowManager::release()

## Task Commits

This is a non-git execution. No commits were made. All changes are in the working tree.

## Files Created/Modified
- `include/Manager.h` - Added 24 EWMH atoms to Atoms struct, m_wmCheckWindow member, public EWMH helper declarations (updateClientList, updateActiveWindow, updateWorkarea), private setupEwmhProperties()
- `src/Manager.cpp` - Added 24 EWMH atom static definitions, atom interning in constructor, setupEwmhProperties() implementation, updateClientList/updateActiveWindow/updateWorkarea implementations, updateClientList calls in addToHiddenList/removeFromHiddenList/windowToClient, updateActiveWindow(None) in clearFocus, m_wmCheckWindow cleanup in release()
- `src/Events.cpp` - Added updateClientList() call in eventDestroy after client removal, added _NET_ACTIVE_WINDOW handler in eventClient()
- `src/Client.cpp` - Added updateActiveWindow(m_window) call in Client::activate() after setActiveClient()
- `tests/test_ewmh.cpp` - 9 EWMH tests: atom interning, WM check self-reference, UTF8_STRING WM_NAME, _NET_SUPPORTED array, desktop atoms, workarea, client list round-trip, active window
- `CMakeLists.txt` - Added test_ewmh executable with Catch2 + X11 linking and Xvfb fixture

## Decisions Made
- **EWMH helpers in public section:** updateClientList, updateActiveWindow, and updateWorkarea moved from private to public because Client::activate() (in Client.cpp) needs to call updateActiveWindow(). This follows the same pattern as other public methods called from Client (installColormap, allocateColour, etc.)
- **updateWorkarea as stub:** Implemented with full screen geometry only. Dock strut subtraction will be added in Plan 02 when _NET_WM_STRUT reading is implemented. Marked with TODO comment.
- **Test approach:** Tests verify the X11 property protocol patterns directly using raw Xlib calls on Xvfb, not the WM binary. This is because the WM cannot be unit-tested on Xvfb (it requires being the window manager). Integration testing of the WM itself requires a running instance.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Moved EWMH helpers to public section**
- **Found during:** Task 1 (GREEN phase build)
- **Issue:** Build failed because Client::activate() cannot call private WindowManager::updateActiveWindow()
- **Fix:** Moved updateClientList, updateActiveWindow, updateWorkarea to public section of WindowManager class
- **Files modified:** include/Manager.h
- **Verification:** Build succeeds, all 97 tests pass

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Minimal -- the helpers need to be callable from Client regardless. No scope creep.

## Issues Encountered
None - implementation followed the plan closely with the one access-modifier fix.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- EWMH foundation is complete. All atoms, root properties, and lifecycle hooks are in place.
- Plan 02 can now add: _NET_WM_WINDOW_TYPE reading in Client, _NET_WM_STATE handling (fullscreen/maximize), _NET_WM_STRUT reading for docks, and updateWorkarea() with actual dock strut subtraction.
- Plan 03 can add: fullscreen circular gesture detection and maximize click toggle in Buttons.cpp.

---
*Phase: 06-ewmh-compliance*
*Completed: 2026-05-08*

## Self-Check: PASSED

All files exist and contain expected content:
- tests/test_ewmh.cpp: FOUND
- .planning/phases/06-ewmh-compliance/06-01-SUMMARY.md: FOUND
- include/Manager.h: FOUND (contains net_supported)
- src/Manager.cpp: FOUND (contains setupEwmhProperties)
- src/Events.cpp: FOUND (contains net_activeWindow)
- src/Client.cpp: FOUND (contains updateActiveWindow)
- CMakeLists.txt: FOUND (contains test_ewmh)
- Build: PASS (all targets compiled)
- Tests: PASS (97/97, including 9 EWMH tests)
