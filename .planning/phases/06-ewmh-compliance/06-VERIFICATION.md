---
phase: 06-ewmh-compliance
verified: 2026-05-08T12:00:00Z
status: gaps_found
score: 12/16 must-haves verified
overrides_applied: 0
gaps:
  - truth: "Fullscreen/maximize states are mutually exclusive with consistent geometry"
    status: partial
    reason: "setFullscreen() does not clear maximize flags when entering fullscreen. A window can be simultaneously m_isFullscreen==true AND m_isMaximizedVert==true. When fullscreen is later removed, the maximize flags are stale -- the window appears normal-sized but the WM believes it is maximized. Additionally, m_preMaximized* geometry values may be stale if the window was maximized before going fullscreen."
    artifacts:
      - path: "src/Client.cpp"
        issue: "setFullscreen() at line 270-302 does not clear m_isMaximizedVert/m_isMaximizedHorz when entering fullscreen"
      - path: "src/Client.cpp"
        issue: "applyWmState() at line 429-432 skips setMaximized when fullscreen is true, but does not clear stale maximize flags"
    missing:
      - "Clear m_isMaximizedVert and m_isMaximizedHorz to false in setFullscreen(true) path"
      - "Call updateNetWmState() to reflect cleared maximize atoms"
  - truth: "Window type changes trigger workarea recalculation"
    status: failed
    reason: "eventProperty() calls getWindowType() on _NET_WM_WINDOW_TYPE changes but does NOT call updateWorkarea(). If a dock window changes its type to Normal, the stale dock strut remains in _NET_WORKAREA until the next unrelated property change triggers recalculation."
    artifacts:
      - path: "src/Client.cpp"
        issue: "Lines 1316-1319: getWindowType() called but updateWorkarea() not called when transitioning to/from Dock"
    missing:
      - "In eventProperty _NET_WM_WINDOW_TYPE handler, call updateWorkarea() when type changes to or from Dock"
  - truth: "Dock workarea recalculation uses live Client object"
    status: failed
    reason: "In eventDestroy(), c->isDock() is called AFTER the unique_ptr erasure at line 272-274, which destroys the Client. This is use-after-free / undefined behavior. The raw pointer 'c' is dangling."
    artifacts:
      - path: "src/Events.cpp"
        issue: "Line 282: c->isDock() called after unique_ptr erasure destroys Client at lines 272-274"
    missing:
      - "Save bool wasDock = c->isDock() BEFORE the removeFrom lambda erases the unique_ptr"
  - truth: "Workarea width/height are never negative"
    status: partial
    reason: "Individual strut values are clamped to screen dimensions, but the combination (screenW - left - right) can produce negative values when opposing docks have overlapping or excessive struts. A negative workarea violates the EWMH spec."
    artifacts:
      - path: "src/Manager.cpp"
        issue: "Line 656: screenW - left - right can be negative; no std::max(0L, ...) guard"
    missing:
      - "Clamp workarea width/height: std::max(0L, screenW - left - right)"
---

# Phase 6: EWMH Compliance Verification Report

**Phase Goal:** The WM speaks EWMH so modern applications, panels, and taskbars interact with it correctly on a single-desktop setup.
**Verified:** 2026-05-08T12:00:00Z
**Status:** gaps_found
**Re-verification:** No -- initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | WM reports correctly via _NET_SUPPORTING_WM_CHECK and _NET_WM_NAME | VERIFIED | setupEwmhProperties() creates check window with self-reference and UTF8_STRING WM name (Manager.cpp:401-420). 2 tests pass. |
| 2 | _NET_CLIENT_LIST stays in sync across client lifecycle | VERIFIED | updateClientList() called from windowToClient (line 556), eventDestroy (Events.cpp:279), addToHiddenList (line 729), removeFromHiddenList (line 743), and manage() DOCK/NOTIFICATION path (Client.cpp:121). Test passes. |
| 3 | _NET_ACTIVE_WINDOW reflects focused client | VERIFIED | updateActiveWindow(m_window) in Client::activate() (line 249), updateActiveWindow(None) in clearFocus() (line 673). Test passes. |
| 4 | Single-desktop atoms set correctly | VERIFIED | _NET_NUMBER_OF_DESKTOPS=1 (Manager.cpp:444-447), _NET_CURRENT_DESKTOP=0 (Manager.cpp:448-451). Tests pass. |
| 5 | _NET_WORKAREA set with screen geometry and dock struts | VERIFIED | setupEwmhProperties() sets initial workarea (Manager.cpp:453-459). updateWorkarea() reads STRUT_PARTIAL/STRUT from dock clients with clamping (Manager.cpp:596-663). Tests pass. |
| 6 | _NET_SUPPORTED lists all supported atoms | VERIFIED | 23-atom supported array set on root window (Manager.cpp:423-441). Test passes. |
| 7 | Dock windows skip frame decoration | VERIFIED | manage() checks WindowType::Dock/Notification and returns early without border (Client.cpp:118-128). |
| 8 | Dialog/notification window types handled | VERIFIED | WindowType enum with Dock/Dialog/Notification (Client.h:24). Dialog gets default decoration. Notification skips decoration. UTILITY/SPLASH/TOOLBAR treated as Normal. |
| 9 | Fullscreen via _NET_WM_STATE works | PARTIAL | setFullscreen() implemented with border strip/restore (Client.cpp:270-302). BUT does not clear maximize flags -- inconsistent state when both are true. |
| 10 | Maximize via _NET_WM_STATE works | VERIFIED | setMaximized() reads workarea and fills it (Client.cpp:311-364). toggleMaximized() toggles both axes. Tests pass. |
| 11 | _NET_WM_STATE add/remove/toggle semantics correct | VERIFIED | applyWmState() handles actions 0/1/2 with both prop1 and prop2 (Client.cpp:397-433). Test passes. |
| 12 | Circular right-button gesture toggles fullscreen | VERIFIED | detectFullscreenGesture() with atan2 sweep, 20px radius, 270-degree threshold, 2s timeout (Client.cpp:1382-1480). |
| 13 | Middle-button on tab toggles maximize | VERIFIED | Border::eventButton() checks Button2 on m_tab, calls toggleMaximized() (Border.cpp:932-935). |
| 14 | Root right-click still circulates | VERIFIED | Buttons.cpp refactored: root Button3 -> circulate (line 20). Client Button3 -> gesture. |
| 15 | Window type changes update workarea | FAILED | eventProperty handles _NET_WM_WINDOW_TYPE change but does not call updateWorkarea() (Client.cpp:1316-1319). |
| 16 | Dock destruction updates workarea safely | FAILED | c->isDock() called after Client is destroyed by unique_ptr erasure (Events.cpp:282). Use-after-free. |

**Score:** 12/16 truths verified (4 with issues)

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `include/Manager.h` | EWMH atoms, helpers, m_wmCheckWindow | VERIFIED | 24 net_* atoms (lines 206-228), utf8_string (line 229), updateClientList/updateActiveWindow/updateWorkarea public (lines 81-83), m_wmCheckWindow (line 148), setupEwmhProperties private (line 157) |
| `src/Manager.cpp` | Atom interning, setupEwmhProperties, lifecycle sync | VERIFIED | 24 atoms interned (lines 133-156), setupEwmhProperties (401-470), updateClientList (571-585), updateActiveWindow (588-593), updateWorkarea with dock struts (596-663), cleanup in release (221-223) |
| `src/Events.cpp` | _NET_ACTIVE_WINDOW + _NET_WM_STATE dispatch | VERIFIED | _NET_ACTIVE_WINDOW handler (304-309), _NET_WM_STATE handler (312-319), updateClientList in eventDestroy (279) |
| `include/Client.h` | WindowType enum, state members, method declarations | VERIFIED | WindowType enum (line 24), m_isFullscreen/m_isMaximizedVert/Horz (lines 151-153), setFullscreen/toggleFullscreen/setMaximized/toggleMaximized/applyWmState/updateNetWmState (lines 91-96), getWindowType (line 181), detectFullscreenGesture (line 185) |
| `src/Client.cpp` | Window type, fullscreen/maximize, state machine, gesture | VERIFIED | getWindowType (579-605), setFullscreen (270-302), setMaximized (311-364), applyWmState (397-433), detectFullscreenGesture (1382-1480), ensureVisible guard (843), hide/unhide call updateNetWmState (789, 806) |
| `include/Border.h` | stripForFullscreen/restoreFromFullscreen declarations | VERIFIED | Lines 31-32 |
| `src/Border.cpp` | Fullscreen strip/restore, Button2 maximize | VERIFIED | stripForFullscreen (878-891), restoreFromFullscreen (894-913), Button2 on m_tab (932-935) |
| `src/Buttons.cpp` | Refactored Button3 dispatch | VERIFIED | Root: Button1=menu, Button3=circulate. Client events dispatched to Client::eventButton (lines 16-26) |
| `tests/test_ewmh.cpp` | 16 EWMH tests | VERIFIED | 16 TEST_CASEs, all pass (824 lines) |
| `CMakeLists.txt` | test_ewmh registration | VERIFIED | test_ewmh target with Catch2 + X11, Xvfb fixture |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| src/Manager.cpp | include/Manager.h | Atom static member definitions | WIRED | 24 Atom Atoms::net_* definitions at lines 26-49 |
| src/Events.cpp | include/Manager.h | eventClient dispatches net_activeWindow | WIRED | Atoms::net_activeWindow checked at line 304 |
| src/Events.cpp | src/Client.cpp | eventClient dispatches net_wmState to applyWmState | WIRED | c->applyWmState(action, prop1, prop2) at line 317 |
| src/Client.cpp | src/Border.cpp | setFullscreen calls stripForFullscreen | WIRED | m_border->stripForFullscreen() at line 284 |
| src/Client.cpp | src/Manager.cpp | setMaximized reads workarea, calls updateWorkarea | WIRED | XGetWindowProperty for net_workarea at line 335 |
| src/Buttons.cpp | src/Client.cpp | eventButton dispatches to Client::eventButton | WIRED | c->eventButton(e) at line 25 |
| src/Border.cpp | src/Client.cpp | Button2 on tab calls toggleMaximized | WIRED | m_client->toggleMaximized() at line 934 |
| src/Client.cpp | src/Manager.cpp | updateClientList/updateActiveWindow lifecycle calls | WIRED | Multiple call sites verified |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|--------------|--------|--------------------|--------|
| updateClientList() | m_clients + m_hiddenClients | Client lifecycle events | Yes -- iterates live vectors | FLOWING |
| updateActiveWindow() | m_window from Client::activate() | Focus change events | Yes -- reads from active Client | FLOWING |
| updateWorkarea() | Dock client strut properties | _NET_WM_STRUT_PARTIAL/_NET_WM_STRUT on dock windows | Yes -- XGetWindowProperty reads real strut data | FLOWING |
| updateNetWmState() | m_isFullscreen, m_isMaximized*, isHidden() | Client state changes | Yes -- reads live state members | FLOWING |
| detectFullscreenGesture() | Pointer motion points | XMaskEvent motion stream | Yes -- accumulates real motion events | FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Build succeeds | cmake --build build | All targets built | PASS |
| All 104 tests pass | ctest --output-on-failure | 104/104 passed | PASS |
| All 16 EWMH tests pass | ctest -R "NET_\|EWMH" | 16/16 passed | PASS |
| EWMH atom count >= 23 | grep -c 'XInternAtom.*_NET_' src/Manager.cpp | 23 | PASS |
| updateClientList call sites >= 5 | grep -rc 'updateClientList' in 3 files | 6 (1 def + 5 calls) | PASS |
| Gesture method defined | grep -c 'detectFullscreenGesture' src/Client.cpp | 2 (1 def + 1 call) | PASS |
| Button2 in Border | grep -c 'Button2' src/Border.cpp | 2 (handler + state mask) | PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| EWMH-01 | 06-01 | Set _NET_SUPPORTED listing all supported EWMH atoms | SATISFIED | 23-atom array in setupEwmhProperties (Manager.cpp:423-441) |
| EWMH-02 | 06-01 | Set _NET_SUPPORTING_WM_CHECK for WM identification | SATISFIED | Self-referencing check window with UTF8_STRING name (Manager.cpp:404-420) |
| EWMH-03 | 06-01 | Maintain _NET_CLIENT_LIST updated on map/unmap/destroy | SATISFIED | updateClientList() called from 5 lifecycle sites |
| EWMH-04 | 06-02 | Handle _NET_ACTIVE_WINDOW (client activation requests) | SATISFIED | eventClient handler (Events.cpp:304-309). NOTE: Does not unhide iconic clients (WR-05). |
| EWMH-05 | 06-02 | Handle _NET_WM_WINDOW_TYPE (DOCK, DIALOG, NOTIFICATION) | SATISFIED | WindowType enum + getWindowType() + manage() skip-decoration |
| EWMH-06 | 06-02, 06-03 | Handle _NET_WM_STATE (fullscreen, maximize) | SATISFIED | setFullscreen/setMaximized/applyWmState + gesture + Button2 toggle |
| EWMH-07 | 06-01 | Set _NET_WM_NAME with UTF-8 encoding | SATISFIED | Atoms::utf8_string used for WM name (Manager.cpp:418-420) |
| EWMH-08 | 06-01 | Set single-desktop atoms | SATISFIED | _NET_NUMBER_OF_DESKTOPS=1, _NET_CURRENT_DESKTOP=0 (Manager.cpp:444-451) |
| EWMH-09 | 06-02 | Set _NET_WORKAREA for panel/taskbar compatibility | SATISFIED | updateWorkarea() with dock strut subtraction (Manager.cpp:596-663) |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| src/Events.cpp | 282 | Use-after-free: c->isDock() after unique_ptr erasure | BLOCKER | Undefined behavior when dock window is destroyed. May crash under sanitizers. |
| src/Client.cpp | 270-302 | Fullscreen does not clear maximize flags | WARNING | Inconsistent state: window can be fullscreen AND maximized simultaneously. Stale geometry on restore. |
| src/Client.cpp | 1316-1319 | Window type change does not update workarea | WARNING | Dock struts persist in _NET_WORKAREA after window type changes from Dock to Normal. |
| src/Manager.cpp | 653-658 | Workarea can have negative width/height | WARNING | EWMH spec violation when opposing docks have overlapping struts. |
| src/Events.cpp | 304-309 | _NET_ACTIVE_WINDOW does not unhide iconic clients | WARNING | EWMH spec non-compliance: taskbar clicks on minimized windows silently ignored. |
| src/Manager.cpp | 418-420 | WM name length hardcoded to 13 | INFO | "wm2-born-again" length matches, but not maintainable if name changes. |

### Human Verification Required

### 1. Visual fullscreen/maximize state transitions

**Test:** Launch the WM on Xvfb, create a window via xterm, send _NET_WM_STATE_FULLSCREEN via xdototool or wmctrl, then send maximize hints, then remove fullscreen.
**Expected:** Window goes fullscreen (no border, covers entire screen). Maximize is cleared. On fullscreen exit, window returns to pre-fullscreen geometry (not maximized).
**Why human:** Cannot run the WM binary in verification environment. Visual behavior requires running WM.

### 2. Circular gesture detection

**Test:** Launch WM, create a window, right-click and drag in a circle on the window.
**Expected:** Window toggles fullscreen when gesture completes. Right-click on root still circulates.
**Why human:** Requires running WM with pointer input and visual feedback.

### 3. Button2 maximize toggle on tab

**Test:** Launch WM, create a window, middle-click on the tab area.
**Expected:** Window toggles maximize. Left-click on tab still drags.
**Why human:** Requires running WM with multi-button mouse input.

### 4. Dock window strut integration

**Test:** Launch WM with a panel (e.g., lxpanel or tint2), check _NET_WORKAREA via xprop.
**Expected:** _NET_WORKAREA excludes dock strut area. Closing the panel restores full screen workarea.
**Why human:** Requires running WM with actual dock application.

### Gaps Summary

The phase delivers substantial EWMH compliance: all 9 EWMH requirements have implementation evidence, 16 tests pass, and the core protocol interactions (atom interning, root properties, client list sync, active window tracking, window types, fullscreen/maximize state, dock struts) are wired correctly end-to-end.

However, the code review identified 2 critical and 6 warning issues, and 4 of those remain unfixed in the codebase:

1. **Use-after-free in eventDestroy** (BLOCKER): `c->isDock()` is called after the `unique_ptr` holding the Client is erased, triggering `~Client()`. This is undefined behavior that may crash under address sanitizer or with certain allocator behavior.

2. **Fullscreen/maximize state collision** (WARNING): `setFullscreen(true)` does not clear maximize flags, allowing a window to simultaneously be fullscreen AND maximized. When fullscreen exits, the maximize flags are stale and the window's geometry state is inconsistent.

3. **Window type change does not update workarea** (WARNING): When a dock window changes its `_NET_WM_WINDOW_TYPE` from Dock to Normal, the workarea retains the stale dock strut offset. The `_NET_WM_WINDOW_TYPE` property handler calls `getWindowType()` but does not call `updateWorkarea()`.

4. **Workarea can be negative** (WARNING): With overlapping struts on opposing edges, `screenW - left - right` can produce negative values, violating the EWMH spec.

The core EWMH "speaks the protocol" goal is substantially achieved -- tools like wmctrl and xprop would see the correct atoms and properties. The gaps are edge-case bugs in the behavioral layer rather than missing protocol support.

---

_Verified: 2026-05-08T12:00:00Z_
_Verifier: Claude (gsd-verifier)_
