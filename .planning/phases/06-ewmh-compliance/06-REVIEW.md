---
phase: 06-ewmh-compliance
reviewed: 2026-05-08T00:00:00Z
depth: standard
files_reviewed: 10
files_reviewed_list:
  - CMakeLists.txt
  - include/Border.h
  - include/Client.h
  - include/Manager.h
  - src/Border.cpp
  - src/Buttons.cpp
  - src/Client.cpp
  - src/Events.cpp
  - src/Manager.cpp
  - tests/test_ewmh.cpp
findings:
  critical: 2
  warning: 6
  info: 3
  total: 11
status: issues_found
---

# Phase 6: Code Review Report

**Reviewed:** 2026-05-08T00:00:00Z
**Depth:** standard
**Files Reviewed:** 10
**Status:** issues_found

## Summary

Reviewed 10 files for EWMH compliance additions to the wm2-born-again window manager. The EWMH atom interning, property setup, and client message handling are structurally sound. However, two critical bugs were found in the fullscreen/maximize interaction logic and the strut property change handler, along with several warnings around edge cases in gesture detection, workarea computation, and state management.

## Critical Issues

### CR-01: Fullscreen and maximize states can collide, corrupting saved geometry

**File:** `src/Client.cpp:311-433`
**Issue:** `applyWmState()` applies fullscreen first, then conditionally applies maximize. However, `setFullscreen(true)` calls `stripForFullscreen()` which reparents the window to root and covers the full screen. After that, if `fullscreen` is true, `setMaximized()` is skipped (correct). But `setMaximized()` saves the current geometry to `m_preMaximized*` the first time it transitions from non-maximized to maximized (line 318-324). If a window is first maximized, then set to fullscreen (which sets `m_isMaximizedVert/Horz` to their current values via `applyWmState`), and then fullscreen is removed while the maximize flags are still set, `setMaximized()` is called again -- but it checks `if (newVert == m_isMaximizedVert && newHorz == m_isMaximizedHorz) return;` which means the maximized state persists but the maximized geometry was never actually set (the window was fullscreen at the time). The restore-from-fullscreen will put back the pre-fullscreen geometry, but the `m_preMaximized*` values will be stale from before the fullscreen transition.

Additionally, `setFullscreen()` does not clear the maximize flags, so a window can simultaneously be `m_isFullscreen == true` AND `m_isMaximizedVert == true`. When restoring from fullscreen at line 293, the border is restored at pre-fullscreen geometry, but the maximize flags remain set, creating an inconsistent state where the window appears normal-sized but the WM believes it is maximized.

**Fix:**
```cpp
void Client::setFullscreen(bool fullscreen)
{
    if (m_isFullscreen == fullscreen) return;

    if (fullscreen) {
        // Save pre-fullscreen geometry
        m_preFullscreenX = m_x;
        m_preFullscreenY = m_y;
        m_preFullscreenW = m_w;
        m_preFullscreenH = m_h;

        // Clear maximize state when entering fullscreen
        m_isMaximizedVert = false;
        m_isMaximizedHorz = false;

        m_isFullscreen = true;
        // ... rest of fullscreen setup ...
    } else {
        m_isFullscreen = false;
        // ... rest of fullscreen restore ...
    }
    updateNetWmState();
}
```

### CR-02: Dock strut property changes on non-dock windows trigger workarea recalculation

**File:** `src/Client.cpp:1322-1325`
**Issue:** The `eventProperty` handler calls `windowManager()->updateWorkarea()` whenever `_NET_WM_STRUT` or `_NET_WM_STRUT_PARTIAL` changes on ANY client, regardless of whether that client is actually a dock window. The `updateWorkarea()` function in Manager.cpp:596-663 iterates all clients and filters by `isDock()`, so this is not functionally incorrect for the result, but the real bug is more subtle: the strut values are read via `XGetWindowProperty` from the raw X11 window, but the `getWindowType()` call at line 1317 -- which updates `m_windowType` -- is called for `_NET_WM_WINDOW_TYPE` changes. If a window first sets strut properties and THEN changes its type to Dock, the workarea is correctly recalculated. But if the type changes FROM Dock to something else, the workarea is not recalculated (no strut property change occurs), leaving stale dock strut offsets in the workarea.

The actual critical bug: when `eventProperty` handles `_NET_WM_WINDOW_TYPE` change at line 1316-1319, it calls `getWindowType()` but does NOT call `updateWorkarea()`. If a dock window changes its type to Normal, the dock strut remains in the workarea until the next unrelated property change on any window triggers a recalculation.

**Fix:**
```cpp
// In Client::eventProperty, after the getWindowType() call:
if (a == Atoms::net_wmWindowType) {
    WindowType oldType = m_windowType;
    getWindowType();
    if (oldType != m_windowType && (oldType == WindowType::Dock || m_windowType == WindowType::Dock)) {
        windowManager()->updateWorkarea();
    }
    return;
}
```

## Warnings

### WR-01: Gesture detection uses `event.xbutton.time` after the event loop exits, but `event` may contain a ButtonPress, not ButtonRelease

**File:** `src/Client.cpp:1427-1439`
**Issue:** After the `while (!done)` gesture detection loop at line 1403, the code constructs a synthetic `releaseEv` for `releaseGrab()` using `event.xbutton.time`. It then checks `if (event.type == ButtonRelease)` at line 1436. However, `releaseGrab()` expects a genuine XButtonEvent with proper state fields. The synthetic `releaseEv` has `state = Button3Mask` hardcoded, but `releaseGrab()` internally calls `nobuttons()` which checks `(state & (state - 1)) == 0`. If `Button3Mask` is 0x400 (typical), `Button3Mask & (Button3Mask - 1)` is non-zero only for powers of two. `Button3Mask` (0x400) is a power of two, so this works. But the real issue is: when the loop exits via `ButtonPress` (another button was pressed, line 1421), the code still calls `releaseGrab(&releaseEv)` and then checks `event.type == ButtonRelease` -- the grab is released with a synthetic event, but the actual grab state in the X server may not be properly cleaned because `releaseGrab()` calls `XUngrabPointer(display(), e->time)` using a potentially stale time from the last `XMaskEvent` result (which was a ButtonPress, not a MotionNotify). The grab IS released, but with a potentially incorrect timestamp.

**Fix:** The grab release logic should differentiate between the two exit paths (normal release vs. cancel by extra button press). Consider releasing the grab unconditionally with `CurrentTime` when the gesture is cancelled, and only use the actual release event time on success.

### WR-02: Workarea computation can produce negative width/height

**File:** `src/Manager.cpp:653-658`
**Issue:** The workarea is computed as:
```cpp
long workarea[4] = {
    static_cast<long>(left),
    static_cast<long>(top),
    static_cast<long>(screenW - left - right),
    static_cast<long>(screenH - top - bottom)
};
```
If dock windows on opposite sides have overlapping or excessive strut values, `screenW - left - right` can be negative. The individual strut values are clamped to `screenW`/`screenH`, but the combination of left+right is not checked against screenW. A negative workarea width/height violates the EWMH spec and could confuse clients or panels.

**Fix:**
```cpp
long ww = std::max(0L, static_cast<long>(screenW - left - right));
long wh = std::max(0L, static_cast<long>(screenH - top - bottom));
long workarea[4] = { left, top, ww, wh };
```

### WR-03: `sendMessage` sends ClientMessage to `m_window` instead of root

**File:** `src/Client.cpp:447`
**Issue:** `XSendEvent(display(), m_window, false, mask, &ev)` sends WM_PROTOCOLS messages to the client's own window with `mask = 0L`. While this works (the client window receives the event via XSendEvent, not via the event mask), the EWMH convention for `_NET_ACTIVE_WINDOW` client messages is that they are sent to the root window. More critically, for WM_DELETE_WINDOW/WM_TAKE_FOCUS, the standard practice is to send to the client window with `NoEventMask` (mask=0), which is what happens here. This is technically correct for ICCCM protocols but the function is also called for `_NET_WM_STATE` handling via `applyWmState` -- no, actually `sendMessage` is only called for WM_PROTOCOLS. This is a false alarm for the WM case, but the function signature takes generic `Atom a` which could be misused. Not a bug today, but a latent risk.

**Fix:** Consider renaming to `sendProtocolMessage` or adding a comment clarifying this is only for WM_PROTOCOLS messages.

### WR-04: `clearFocus()` uses a static local Window that is never destroyed

**File:** `src/Manager.cpp:668-700`
**Issue:** The static local `w` at line 668 is created once and never destroyed. It is a 1x1 override-redirect input-only window used as a focus fallback. While this is intentional (the window persists for the lifetime of the X connection), it is never cleaned up in `release()`. On WM exit, the X connection is closed, which implicitly destroys all resources. This is acceptable for a window manager that exits by closing the display, but if `release()` were ever called without exiting (e.g., for testing), this window would leak.

**Fix:** Minor -- store the focus fallback window as a member and destroy it in `release()`.

### WR-05: `_NET_ACTIVE_WINDOW` client message only activates Normal-state clients

**File:** `src/Events.cpp:304-309`
**Issue:** When handling `_NET_ACTIVE_WINDOW` client messages, the code checks `if (c && c->isNormal())`. Per the EWMH spec, `_NET_ACTIVE_WINDOW` should activate the window, which may include mapping it if it is iconic. The current code silently ignores activation requests for hidden/iconic clients, which violates the EWMH spec expectation. A taskbar clicking on a minimized window's entry would send `_NET_ACTIVE_WINDOW` expecting the window to be unhidden and activated.

**Fix:**
```cpp
if (e->message_type == Atoms::net_activeWindow) {
    if (c) {
        if (c->isHidden()) c->unhide(true);
        if (c->isNormal()) c->activate();
    }
    return;
}
```

### WR-06: `eventDestroy` accesses `c` after the unique_ptr holding it has been erased

**File:** `src/Events.cpp:282-283`
**Issue:** After `removeFrom` erases the unique_ptr at line 272-274, the Client object is destroyed (destructor runs). Then at line 282, the code calls `c->isDock()`. Since `c` is a dangling pointer (the Client has been deleted), this is undefined behavior. The `isDock()` method reads `m_windowType` which is stored in the destroyed object's memory. In practice this often works because the memory has not been reused, but it is formally undefined behavior and could manifest as a crash under memory sanitizers or with certain allocator behaviors.

**Fix:** Check `isDock()` before erasing the unique_ptr:
```cpp
bool wasDock = c->isDock();

if (!removeFrom(m_clients)) {
    removeFrom(m_hiddenClients);
}

updateClientList();

if (wasDock) {
    updateWorkarea();
}
```

## Info

### IN-01: `_NET_WM_NAME` length is hardcoded to 13

**File:** `src/Manager.cpp:418-420`
**Issue:** The WM name "wm2-born-again" is passed with a hardcoded length of 13. While this matches the string length, using `std::strlen(wmName)` would be more maintainable if the name ever changes.

**Fix:** Use `static_cast<int>(std::strlen(wmName))` instead of the literal `13`.

### IN-02: Test file has duplicated atom interning boilerplate

**File:** `tests/test_ewmh.cpp`
**Issue:** Each test case re-interns all atoms independently. This is 25+ individual `XInternAtom` calls repeated across test cases. While this keeps tests self-contained, it makes the test file verbose and increases maintenance burden when atoms are added.

**Fix:** Consider a shared helper function or Catch2 fixture that interns all EWMH atoms once per test case.

### IN-03: `MaximizedHorz` is misspelled in atom name

**File:** `include/Manager.h:215`
**Issue:** The atom name is `_NET_WM_STATE_MAXIMIZED_HORZ` which uses the abbreviation "HORZ" instead of "HORIZ". This matches the actual EWMH spec (`_NET_WM_STATE_MAXIMIZED_HORZ`), so it is correct. Noting for clarity since it looks like a typo but is spec-compliant.

**Fix:** No fix needed -- matches EWMH spec.

---

_Reviewed: 2026-05-08T00:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
