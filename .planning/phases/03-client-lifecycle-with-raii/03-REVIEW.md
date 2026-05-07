---
phase: 03-client-lifecycle-with-raii
reviewed: 2026-05-07T12:00:00Z
depth: standard
files_reviewed: 10
files_reviewed_list:
  - include/x11wrap.h
  - include/Client.h
  - include/Manager.h
  - src/Client.cpp
  - src/Border.cpp
  - src/Events.cpp
  - src/Manager.cpp
  - src/Buttons.cpp
  - tests/test_client.cpp
  - CMakeLists.txt
findings:
  critical: 3
  warning: 6
  info: 4
  total: 13
status: issues_found
---

# Phase 3: Code Review Report

**Reviewed:** 2026-05-07T12:00:00Z
**Depth:** standard
**Files Reviewed:** 10
**Status:** issues_found

## Summary

Reviewed 10 files implementing the Client lifecycle with RAII ownership model for a modernized wm2 window manager. The codebase replaces the upstream raw-pointer `delete this` pattern with `std::unique_ptr` ownership, introduces RAII wrappers for X11 resources, and implements a self-pipe signal mechanism. Three critical bugs were found: an out-of-bounds vector access in the window circulate function, missing BadWindow error suppression during the DestroyNotify destructor path, and a use-after-free risk in the circulate loop's unsigned arithmetic. Six warnings cover X11 resource leaks, a redundant unreparent call, and logic issues.

## Critical Issues

### CR-01: Out-of-bounds access in circulate() when client list is empty

**File:** `src/Buttons.cpp:54-55`
**Severity:** BLOCKER

**Issue:** The `circulate()` function accesses `m_clients[j]` at line 55 without checking whether `m_clients` is non-empty. When no clients exist (`m_clients.size() == 0`), `i = -1`, so `j = i + 1 = 0`, and `m_clients[0]` is accessed on an empty vector, causing undefined behavior (crash). Additionally, the loop condition at line 57 uses `m_clients.size() - 1` which underflows to `SIZE_MAX` when the vector is empty (unsigned arithmetic), making the check never trigger.

```cpp
// Lines 54-59: j starts at 0, m_clients[0] accessed unconditionally
for (j = i + 1;
     !m_clients[j]->isNormal() || m_clients[j]->isTransient();  // UB if empty
     ++j) {
    if (static_cast<size_t>(j) >= m_clients.size() - 1) j = -1;  // size()-1 wraps if empty
    if (j == i) return;
}
```

**Fix:**
```cpp
if (m_clients.empty()) return;  // Guard at top of circulate(), after !c block opens

// Also fix the loop to check bounds BEFORE accessing:
for (j = i + 1; ; ++j) {
    if (static_cast<size_t>(j) >= m_clients.size()) j = 0;
    if (j == i) return;
    if (m_clients[j]->isNormal() && !m_clients[j]->isTransient()) break;
}
```

### CR-02: BadWindow errors not suppressed during DestroyNotify destructor path

**File:** `src/Events.cpp:267-274`
**Severity:** BLOCKER

**Issue:** When `eventDestroy` fires, the X11 window has already been destroyed. The `vec.erase(it)` call at line 261 triggers `~Client()`, which calls `unreparent()`, which calls `XReparentWindow()` and `XConfigureWindow()` on the now-destroyed `m_window`. These X11 calls generate `BadWindow` errors. However, `ignoreBadWindowErrors` is only set to `true` at line 272, AFTER the erase/destructor has already run. During the destructor execution, `ignoreBadWindowErrors` is `false`, so the error handler will log errors (and could potentially misbehave during initialization).

```cpp
if (!removeFrom(m_clients)) {
    removeFrom(m_hiddenClients);
}
// ~Client() runs here, calls unreparent(), XReparentWindow on destroyed window
// ignoreBadWindowErrors is STILL false here!

ignoreBadWindowErrors = true;   // Too late -- errors already generated
XSync(display(), false);
ignoreBadWindowErrors = false;
```

**Fix:**
```cpp
ignoreBadWindowErrors = true;   // Move BEFORE the erase

if (!removeFrom(m_clients)) {
    removeFrom(m_hiddenClients);
}
// ~Client() now runs with BadWindow errors suppressed

XSync(display(), false);
ignoreBadWindowErrors = false;
```

### CR-03: Unsigned underflow in circulate() bounds check

**File:** `src/Buttons.cpp:51`
**Severity:** BLOCKER

**Issue:** At line 51, `static_cast<size_t>(i) >= m_clients.size() - 1` performs unsigned subtraction on `m_clients.size()`. When `m_clients` has exactly one element, `size() - 1 = 0`, and if `i == 0`, the condition correctly resets to `-1`. But when `m_clients` is empty (which can happen if all clients are hidden), `size() - 1` underflows to `SIZE_MAX`, so the condition is always true and `i` is always reset to `-1`. This interacts with CR-01 to create the out-of-bounds access.

**Fix:** Same fix as CR-01 -- add an empty check and use a different bounds pattern.

## Warnings

### WR-01: m_menuWindow X11 resource never destroyed (resource leak)

**File:** `src/Manager.cpp:286`
**Issue:** `m_menuWindow` is created via `XCreateSimpleWindow` at line 286 but is never destroyed with `XDestroyWindow`. It is declared as a raw `Window` type (line 128 of `Manager.h`), not wrapped in any RAII class. Neither `release()` nor `~WindowManager()` calls `XDestroyWindow` on it. While the X server will clean up when the connection closes, this is inconsistent with the RAII approach used for all other X11 resources.

**Fix:** Either wrap `m_menuWindow` in a RAII class or add `XDestroyWindow(display(), m_menuWindow)` to `release()`.

### WR-02: Double unreparent in release() and ~Client()

**File:** `src/Manager.cpp:153-158` and `src/Client.cpp:59-61`
**Issue:** `WindowManager::release()` explicitly calls `c->unreparent()` for every client at line 154. Then when `m_clients.clear()` runs at line 158, `~Client()` fires, and it also calls `unreparent()` (line 60 of `Client.cpp`) if the client is not withdrawn. After `release()` calls `unreparent()`, the client's state remains Normal (not Withdrawn), because `unreparent()` does not change `m_state`. So `~Client()` will call `unreparent()` a second time, issuing redundant XReparentWindow calls and XSync.

**Fix:** Either have `release()` set the client to Withdrawn state after unreparenting, or remove the explicit unreparent loop from `release()` and rely solely on `~Client()`.

### WR-03: Client destructor accesses WindowManager after eventDestroy cleared activeClient

**File:** `src/Client.cpp:65-67` and `src/Events.cpp:249-251`
**Issue:** In `eventDestroy`, line 250 sets `m_activeClient = nullptr` before erasing the client. Then `~Client()` fires and checks `activeClient() == this` at line 65. Since `m_activeClient` was already cleared, this check is a no-op. However, the destructor at line 66 then calls `windowManager()->setActiveClient(nullptr)` which is redundant. This is not a bug, but it indicates the eventDestroy and destructor have overlapping cleanup responsibilities that could lead to future bugs if the order changes.

**Fix:** Remove the active client check from `~Client()` since `eventDestroy` already handles it, or document the invariant clearly.

### WR-04: const_cast of std::string::c_str() passed to XRotDrawString / XRotTextWidth

**File:** `src/Border.cpp:133,173,182,189`
**Issue:** `const_cast<char*>(m_label.c_str())` is passed to `XRotDrawString` and `XRotTextWidth`. While the upstream xvertext library's API likely takes a non-const `char*` for historical reasons, if the library ever writes through this pointer, it would corrupt the `std::string`'s internal buffer. This is a latent safety issue dependent on third-party code behavior.

**Fix:** Copy the string to a local `char[]` buffer before passing to the rotated font functions.

### WR-05: circulate() loop can infinite-loop when all clients are transient or hidden

**File:** `src/Buttons.cpp:54-59`
**Issue:** If all clients in `m_clients` are transient or not in Normal state, the loop at line 54-59 will cycle through the entire vector and eventually `j == i`, causing `return` at line 58. But the `return` means `c` is never set (remains `nullptr`), and `c->activateAndWarp()` at line 64 would dereference null. This is a narrow edge case but causes a crash.

**Fix:** Add a null check on `c` before calling `c->activateAndWarp()` at line 64.

### WR-06: Implicit Cursor conversion operator allows accidental XID misuse

**File:** `include/x11wrap.h:94`
**Issue:** `UniqueCursor::operator Cursor()` is a non-explicit conversion operator. This allows `UniqueCursor` objects to silently decay to `Cursor` XID values in any context, including assignments to unrelated variables. While the code currently benefits from this at `installCursorOnWindow`, it creates risk of accidentally passing a cursor where a different XID type is expected.

**Fix:** Consider making `operator Cursor()` `explicit` and using `.get()` at call sites.

## Info

### IN-01: Redundant variable nh in menu()

**File:** `src/Buttons.cpp:111-113`
**Issue:** `nh` is computed at line 111 as `clients.size() + 1`, and then `n` is computed at line 113 as `clients.size() + 1`. They start with the same value. `n` is later incremented by 1 when `allowExit` is true. The variable `nh` is only used at line 245 for alignment. The naming is unclear.

**Fix:** Add a comment explaining that `nh` is the boundary between hidden and visible clients in the menu, distinct from `n` which includes the exit option.

### IN-02: Unused return value of menuLabel()

**File:** `src/Manager.cpp:554-558`
**Issue:** `WindowManager::menuLabel(int i)` is declared and defined but never called -- the `menu()` function in `Buttons.cpp` uses a lambda `menuLabelFn` instead. This dead code should be removed.

**Fix:** Remove `menuLabel()` declaration from `Manager.h:135` and definition from `Manager.cpp:554-558`.

### IN-03: Magic number 80ms and 400ms for focus timers

**File:** `src/Manager.cpp:99,575`
**Issue:** The pointer-stopped delay (80ms) and auto-raise delay (400ms) are hardcoded as magic numbers. These are user-facing timing parameters that should be named constants or configurable values.

**Fix:** Define `constexpr auto kPointerStoppedDelayMs = 80;` and `constexpr auto kAutoRaiseDelayMs = 400;`.

### IN-04: Test file does not test actual Client class functionality

**File:** `tests/test_client.cpp`
**Issue:** Despite the file being named `test_client.cpp`, it tests RAII wrappers (`ServerGrab`), `ClientState` enum values, and raw X11 window operations. It does not instantiate or test the `Client` class itself. The test name is misleading about coverage scope.

**Fix:** Rename to `test_x11_lifecycle.cpp` or add actual Client class tests.

---

_Reviewed: 2026-05-07T12:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
