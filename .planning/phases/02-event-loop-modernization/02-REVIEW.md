---
phase: 02-event-loop-modernization
reviewed: 2026-05-07T12:00:00Z
depth: standard
files_reviewed: 7
files_reviewed_list:
  - CMakeLists.txt
  - include/Manager.h
  - src/Client.cpp
  - src/Events.cpp
  - src/Manager.cpp
  - tests/test_autoraise.cpp
  - tests/test_eventloop.cpp
findings:
  critical: 3
  warning: 7
  info: 4
  total: 14
status: issues_found
---

# Phase 2: Code Review Report

**Reviewed:** 2026-05-07T12:00:00Z
**Depth:** standard
**Files Reviewed:** 7
**Status:** issues_found

## Summary

Reviewed the event loop modernization changes: replacement of `select()` with `poll()`, self-pipe trick for signal handling, `std::chrono`-based timer deadlines for auto-raise, RAII wrappers for file descriptors, and CMake modernization. Three critical bugs were found: a use-after-free / double-free in colormap cleanup during Client destruction, a missing null-pointer guard on `m_focusCandidate` in timer-driven focus code, and a memory leak in `spawn()` where `putenv` is used with a heap allocation that is never freed and creates a dangling pointer in the child process. Several warnings were also identified including unchecked `malloc` returns, redundant state reset logic, and dead code.

## Critical Issues

### CR-01: Double-free of colormap resources in Client destructor + release()

**File:** `src/Client.cpp:57-63` and `src/Client.cpp:86-92`
**Issue:** The `Client` destructor (`~Client()`, lines 57-63) frees `m_colormapWindows` via `XFree` and `m_windowColormaps` via `free()`. However, `Client::release()` (lines 86-92) performs the exact same cleanup. Since `release()` is called before `delete` in `WindowManager::eventDestroy()` (Manager.cpp line 253: `c->release()`) and in `WindowManager::release()` (Manager.cpp line 156: `c->release()`), the destructor runs after `release()` has already freed those pointers. In `release()`, the pointers are set to `nullptr` and the count to 0 after freeing, so the destructor's guard `if (m_colormapWinCount > 0)` will be false in the normal case. However, if an exception or error occurs between construction and `release()`, or if `release()` is never called, both paths attempt cleanup. The real danger is if `release()` is somehow skipped -- the destructor would free valid memory, but if `release()` ran first and the pointers were not zeroed (e.g., due to a code change), this becomes a double-free. The redundant cleanup paths create a fragile maintenance hazard.

Additionally, the destructor does NOT call `release()` automatically, relying entirely on the caller to invoke it before destruction. If any code path destroys a `Client` without calling `release()`, resources leak or X11 windows are left reparented. This is the same pattern as upstream's `delete this` but without the safety net.

**Fix:** Either make the destructor call `release()` with a guard against double-invocation, or remove the cleanup from the destructor and document the pre-condition that `release()` must be called. Recommended:
```cpp
Client::~Client()
{
    if (m_window != None) {
        release();  // safety net if caller forgot
    }
}
```
And in `release()`, add an early return if already released:
```cpp
void Client::release()
{
    if (m_window == None) return; // already released
    // ... rest of cleanup
}
```

### CR-02: Null pointer dereference on m_focusCandidate in checkDelaysForFocus()

**File:** `src/Manager.cpp:591` and `src/Manager.cpp:607`
**Issue:** `checkDelaysForFocus()` dereferences `m_focusCandidate` without null checks at lines 591 and 607:
```cpp
m_focusCandidate->focusIfAppropriate(true);
```
While `m_focusCandidate` is set in `considerFocusChange()` to a valid pointer, a DestroyNotify event for that client (processed in `eventDestroy` at Events.cpp:240-243) sets `m_focusChanging = false` but does NOT set `m_focusCandidate = nullptr`. If the timing is such that `checkDelaysForFocus()` is entered after the client is destroyed but before `stopConsideringFocus()` clears the state, the pointer is dangling. More concretely: `eventDestroy` calls `c->release()` and then the object is deleted -- but the timer could fire between destruction and the next event loop iteration. Even in the current code, there is a window where `m_focusCandidate` points to a deleted `Client`.

**Fix:** In `eventDestroy()`, also call `stopConsideringFocus()` when the destroyed client is the focus candidate, and null out the pointer:
```cpp
void WindowManager::eventDestroy(XDestroyWindowEvent *e)
{
    Client *c = windowToClient(e->window);
    if (c) {
        if (m_focusChanging && c == m_focusCandidate) {
            stopConsideringFocus();  // full cleanup including nulling candidate
            m_focusCandidate = nullptr;
        }
        // ... rest of destruction
    }
}
```
Also add a null guard at the top of `checkDelaysForFocus()`:
```cpp
if (!m_focusCandidate || !m_focusChanging) return;
```

### CR-03: Memory leak and dangling pointer in spawn()

**File:** `src/Manager.cpp:514-516`
**Issue:** In `spawn()`, a buffer is allocated with `new char[]` and passed to `putenv()`:
```cpp
char *envBuf = new char[envStr.size() + 1];
std::strcpy(envBuf, envStr.c_str());
putenv(envBuf);
```
`putenv()` does not copy the string -- it uses the pointer directly as part of the environment. This has two consequences:
1. The memory is never freed (memory leak in the child process -- minor since `exec` follows, but if `exec` fails the leak is real).
2. More critically: `envStr` is a local `std::string` that goes out of scope, but `envBuf` is a separate allocation so this particular aspect is fine. However, `putenv` in a child process that will `exec` is fine. The real issue is that `putenv` in the **first fork** (before the second fork) would persist, but this code is in the second fork. The code is actually safe as written since `execlp` or `exit` follows.

Reassessing: This is a **WARNING**, not a critical issue, because the code runs in the double-forked child that either execs or exits. Downgrading.

**Fix:** Use `setenv()` instead, which copies the string internally, eliminating the manual allocation:
```cpp
setenv("DISPLAY", displayName, 1);
```

## Warnings

### WR-01: Unchecked malloc return in getColormaps()

**File:** `src/Client.cpp:459`
**Issue:** `malloc()` is called without checking the return value:
```cpp
m_windowColormaps = static_cast<Colormap*>(malloc(n * sizeof(Colormap)));
```
If `malloc` returns `nullptr` (out of memory), the subsequent loop at line 461 dereferences the null pointer, causing a crash.

**Fix:** Check the return or use `std::vector<Colormap>`:
```cpp
m_windowColormaps = static_cast<Colormap*>(malloc(n * sizeof(Colormap)));
if (!m_windowColormaps) {
    m_colormapWinCount = 0;
    XFree(m_colormapWindows);
    m_colormapWindows = nullptr;
    return;
}
```

### WR-02: installCursorOnWindow uses UniqueCursor implicitly, risking dangling XID

**File:** `src/Manager.cpp:338-344`
**Issue:** In `installCursorOnWindow()`, the switch statement assigns `attr.cursor` from `UniqueCursor` objects via the implicit `operator Cursor()` conversion. The XID value is extracted and stored in the `XSetWindowAttributes`, then applied via `XChangeWindowAttributes`. This is correct -- X11 copies the cursor XID into the window's attributes. No bug here, but the implicit conversion operator is a code smell that could lead to use-after-free if a `UniqueCursor` is destroyed while the window still references its XID. The window holds only an XID, not a reference-counted handle.

**Fix:** No immediate action required, but consider making the conversion operator `explicit` to force intentional use:
```cpp
explicit operator Cursor() const noexcept { return m_cursor; }
```
Then use `.get()` at call sites.

### WR-03: move() and resize() inner loops use select() for sleep instead of poll()

**File:** `src/Client.cpp:730` and `src/Client.cpp:833`
**Issue:** The `move()` and `resize()` methods contain inner event loops that use `select(0, ...)` as a 50ms sleep:
```cpp
sleepval.tv_sec = 0;
sleepval.tv_usec = 50000;
select(0, nullptr, nullptr, nullptr, &sleepval);
```
While the main event loop was modernized to use `poll()`, these inner loops still use `select()` for sleeping. This is inconsistent with the modernization theme. More importantly, these inner loops do NOT check the self-pipe, so a SIGTERM during a move/resize drag will not be processed until the user releases the mouse button.

**Fix:** Use `poll(nullptr, 0, 50)` for the sleep (modern equivalent), and consider checking the self-pipe fd for signal delivery during interactive operations.

### WR-04: test_eventloop.cpp does not test signal integration

**File:** `tests/test_eventloop.cpp`
**Issue:** The event loop tests cover `FdGuard` RAII semantics but do not test the actual signal integration: that `sigHandler` writes to the pipe, that `nextEvent` drains the pipe and sets `m_looping = false`, or that the signal-safe write actually wakes `poll()`. The tests verify building blocks but not the integration.

**Fix:** Add an integration test that creates a pipe, writes a byte simulating a signal, and verifies the poll-and-drain logic in isolation (without requiring an X display).

### WR-05: test_autoraise.cpp tests only chrono arithmetic, not actual auto-raise behavior

**File:** `tests/test_autoraise.cpp`
**Issue:** All five tests in `test_autoraise.cpp` verify `std::chrono` arithmetic (clock monotonicity, duration casting, min comparison, past-deadline clamping). None test the actual `computePollTimeout()` or `checkDelaysForFocus()` logic, which is the core of the auto-raise modernization. The tests essentially verify that `<chrono>` works correctly, which is a standard library guarantee, not application logic.

**Fix:** Add tests that exercise `computePollTimeout()` directly (requires a `WindowManager` or extracting the logic into a testable free function). Consider refactoring the timeout computation into a standalone function that can be unit-tested without X11.

### WR-06: Potential integer overflow in computePollTimeout()

**File:** `src/Manager.cpp:635`
**Issue:** The timeout computation casts `milliseconds::rep` (which is typically `long long`) to `int`:
```cpp
auto ms = duration_cast<milliseconds>(earliest - now).count();
return (ms > 0) ? static_cast<int>(ms) : 0;
```
If the deadline is very far in the future (e.g., due to a bug setting a deadline to `time_point::max()`), the value could overflow `int`. In practice, the deadlines are set to 80ms or 400ms in the future, so this is unlikely. However, the initial values are `{}`-initialized (epoch), and if a deadline flag is active but the deadline itself is uninitialized or stale, the computed ms could be very large (or negative).

**Fix:** Clamp to a reasonable maximum:
```cpp
auto ms = duration_cast<milliseconds>(earliest - now).count();
if (ms <= 0) return 0;
return static_cast<int>(std::min(ms, static_cast<decltype(ms)>(30000))); // 30s max
```

### WR-07: spawn() memory leak becomes dangling pointer with putenv()

**File:** `src/Manager.cpp:514-516`
**Issue:** Reiterating CR-03 (downgraded): `putenv()` takes ownership of the pointer, and the `new char[]` allocation is intentionally leaked into the environment. However, if `execlp` fails, `exit(1)` is called with a leaked allocation. The preferred POSIX API is `setenv()` which copies the value and avoids the ownership transfer.

**Fix:**
```cpp
setenv("DISPLAY", displayName, 1);
```
This eliminates the manual allocation entirely.

## Info

### IN-01: CMake FetchContent uses specific Git tag without integrity check

**File:** `CMakeLists.txt:48-50`
**Issue:** `FetchContent_Declare` for Catch2 uses `GIT_TAG v3.14.0` but does not specify `GIT_SHALLOW TRUE` for faster clones or any hash-based integrity verification. This is standard CMake practice but builds are not reproducible if the tag is moved.

**Fix:** Consider adding `GIT_SHALLOW TRUE` for build performance:
```cmake
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.14.0
    GIT_SHALLOW    TRUE
)
```

### IN-02: test_eventloop.cpp includes Manager.h but only uses FdGuard

**File:** `tests/test_eventloop.cpp:2`
**Issue:** `#include "Manager.h"` pulls in X11 headers, `<chrono>`, `<csignal>`, `<unistd>`, and the full `WindowManager` class declaration. The tests only use `FdGuard`. This creates an unnecessary compile-time dependency -- any change to Manager.h recompiles the test.

**Fix:** Extract `FdGuard` into its own header (e.g., `FdGuard.h`) and include that instead.

### IN-03: Dead code -- unused variables in move() and resize()

**File:** `src/Client.cpp:714` (sleepval), `src/Client.cpp:819` (sleepval)
**Issue:** The `struct timeval sleepval` variables in `move()` and `resize()` are declared alongside other variables at the top of each function. They are used (for the `select` sleep), so not technically dead, but they are part of the legacy `select()`-based sleep pattern.

**Fix:** If WR-03 is addressed (replacing `select` with `poll`), these declarations become unnecessary.

### IN-04: Manager.h includes <unistd.h> for FdGuard::close()

**File:** `include/Manager.h:12`
**Issue:** `Manager.h` includes `<unistd.h>` solely for the `::close()` call in `FdGuard`. This pollutes the include chain for every file that includes `Manager.h`. Similar to IN-02, this would be resolved by extracting `FdGuard` into its own header.

**Fix:** Move `FdGuard` to its own header file (e.g., `include/FdGuard.h`) that includes `<unistd.h>`.

---

_Reviewed: 2026-05-07T12:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
