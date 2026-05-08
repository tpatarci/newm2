---
phase: 06-ewmh-compliance
plan: 03
subsystem: wm-protocol
tags: [ewmh, x11, xlib, mouse-gesture, fullscreen, maximize, circular-gesture, button2, state-machine-tests]

# Dependency graph
requires:
  - phase: 06-ewmh-compliance/02
    provides: "Fullscreen/maximize state management, setFullscreen/toggleFullscreen/setMaximized/toggleMaximized, border strip/restore"
provides:
  - "Circular right-button gesture detection (>= 270 degree sweep, >= 20px radius, 2s timeout) for fullscreen toggle"
  - "Middle-button (Button2) click on tab for maximize toggle"
  - "Button3 dispatch refactored: root -> circulate, client -> gesture detection"
  - "Client::eventButton() extended with Button2 (maximize) and Button3 (fullscreen gesture) dispatch"
  - "Border::eventButton() handles Button2 on m_tab -> toggleMaximized()"
  - "ensureVisible() guards against repositioning fullscreen/maximized windows"
  - "7 new EWMH state machine and workarea protocol tests"
affects: []

# Tech tracking
tech-stack:
  added: []
  patterns: [circular-mouse-gesture, atan2-angular-sweep, button-dispatch-refactor, ewmh-state-protocol-tests]

key-files:
  created: []
  modified:
    - src/Buttons.cpp
    - src/Client.cpp
    - src/Border.cpp
    - include/Client.h
    - tests/test_ewmh.cpp

key-decisions:
  - "Button3 dispatch refactored: root window circulates (unchanged), client windows dispatch to Client::eventButton() for gesture detection"
  - "Button2 on tab dispatches through Border::eventButton() where it calls toggleMaximized(); Button2 on other border windows is a no-op"
  - "Gesture detection uses atan2-based angular sweep computation with centroid radius check and 2-second timeout"
  - "releaseGrab called with synthesized XButtonEvent since gesture detection loop consumes the original event"
  - "ensureVisible() skips repositioning for fullscreen and fully-maximized windows"

patterns-established:
  - "Mouse gesture detection: grab pointer, accumulate motion points, compute geometric properties, toggle state if threshold exceeded"
  - "Button dispatch hierarchy: root events handled in WindowManager::eventButton(), client events dispatched to Client::eventButton(), border-specific events dispatched to Border::eventButton()"

requirements-completed: [EWMH-06]

# Metrics
duration: 5min
completed: 2026-05-08
---

# Phase 6 Plan 3: Mouse-Driven Fullscreen Gesture and Maximize Toggle Summary

**Circular right-button gesture for fullscreen toggle (270-degree sweep, 20px radius, 2s timeout), middle-button maximize toggle on tab, and 7 EWMH state machine protocol tests**

## Performance

- **Duration:** 5 min
- **Started:** 2026-05-08T06:12:55Z
- **Completed:** 2026-05-08T06:18:10Z
- **Tasks:** 1
- **Files modified:** 4 (src/Buttons.cpp, src/Client.cpp, src/Border.cpp, include/Client.h)
- **Files created:** 0 (extended existing tests/test_ewmh.cpp)

## Accomplishments
- Right-button circular gesture detection implemented in Client::detectFullscreenGesture() with pointer grab, motion tracking, centroid computation, angular sweep via atan2, and configurable thresholds (20px radius, 270-degree sweep, 2-second timeout)
- Button3 dispatch refactored in WindowManager::eventButton(): root right-click circulates (preserved), client right-click dispatches to Client::eventButton() for gesture detection
- Button2 (middle-click) on tab toggles maximize via Border::eventButton()
- Client::eventButton() extended with Button2 and Button3 dispatch alongside existing Button1 handling
- ensureVisible() guards against repositioning fullscreen and fully-maximized windows
- 7 new EWMH tests added: state property format, add/remove/toggle semantics, fullscreen round-trip, maximize both axes, workarea dock strut subtraction, STRUT_PARTIAL preference, strut clamping

## Task Commits

This is a non-git execution. No commits were made. All changes are in the working tree.

## Files Created/Modified
- `src/Buttons.cpp` - Refactored eventButton() dispatch: root events handled first (Button1=menu, Button3=circulate), then client events dispatched to Client::eventButton()
- `src/Client.cpp` - Added <cmath> include, extended eventButton() with Button2 (maximize dispatch) and Button3 (gesture detection) handling, implemented detectFullscreenGesture() with pointer grab and angular sweep, added fullscreen/maximize guard to ensureVisible()
- `include/Client.h` - Added detectFullscreenGesture() private method declaration
- `src/Border.cpp` - Added Button2 handling on m_tab: calls m_client->toggleMaximized() for middle-click maximize toggle
- `tests/test_ewmh.cpp` - Added 7 tests: state property format, add/remove/toggle semantics, fullscreen round-trip, maximize both axes, workarea dock strut subtraction, STRUT_PARTIAL preference over STRUT, strut clamping

## Decisions Made
- **Button3 dispatch refactored at WindowManager level:** Instead of catching all Button3 events globally, the refactored code checks the window first: root events go to menu/circulate, client events go to Client::eventButton(). This cleanly separates root circulate from client gesture detection.
- **Button2 dispatch through Border:** Button2 on tab goes through Border::eventButton() where it triggers toggleMaximized(). Button2 on m_parent, m_resize, or m_button is a natural no-op (falls through without matching m_button).
- **Gesture detection grabs pointer and loops:** detectFullscreenGesture() grabs the pointer, enters a local event loop to collect motion points, then computes geometric properties on release. The grab ensures no other interactions interfere during gesture tracking.
- **Synthesized releaseGrab event:** The gesture detection loop consumes the original XButtonEvent, so a synthesized XButtonEvent is passed to releaseGrab() for proper pointer ungrab.
- **ensureVisible guard:** Fullscreen windows and fully-maximized windows (both vert and horz) skip the ensureVisible() repositioning logic to avoid conflicting with their special geometry.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None - implementation followed the plan closely.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- All EWMH mouse-driven interactions are complete (Plan 01-03).
- Phase 6 EWMH compliance is now complete: all atoms, root properties, window types, fullscreen/maximize state, dock struts, and mouse gestures are implemented.
- 104 tests pass (97 existing + 7 new EWMH state/workarea tests).

---
*Phase: 06-ewmh-compliance*
*Completed: 2026-05-08*

## Self-Check: PASSED

All files exist and contain expected content:
- src/Buttons.cpp: FOUND (contains circulate on root, dispatch to client)
- src/Client.cpp: FOUND (contains detectFullscreenGesture, toggleFullscreen)
- src/Border.cpp: FOUND (contains Button2 on m_tab)
- include/Client.h: FOUND (contains detectFullscreenGesture declaration)
- tests/test_ewmh.cpp: FOUND (contains 7 new state/workarea tests)
- Build: PASS (all targets compiled)
- Tests: PASS (104/104, including 16 EWMH tests)
- Grep checks: detectFullscreenGesture=2, Button2 in Border.cpp=2, circulate in Buttons.cpp=2
