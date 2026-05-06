---
phase: 01-build-infrastructure-raii-foundation
reviewed: 2026-05-06T00:00:00Z
depth: standard
files_reviewed: 15
files_reviewed_list:
  - .gitignore
  - CMakeLists.txt
  - include/Border.h
  - include/Client.h
  - include/Cursors.h
  - include/Manager.h
  - include/x11wrap.h
  - src/Border.cpp
  - src/Buttons.cpp
  - src/Client.cpp
  - src/Events.cpp
  - src/Manager.cpp
  - src/main.cpp
  - tests/test_raii.cpp
  - tests/test_smoke.cpp
findings:
  critical: 3
  warning: 8
  info: 4
  total: 15
status: issues_found
---

# Phase 1: Code Review Report

**Reviewed:** 2026-05-06
**Depth:** standard
**Files Reviewed:** 15
**Status:** issues_found

## Summary

Reviewed 15 source files comprising the build infrastructure (CMakeLists.txt, .gitignore), RAII wrappers for X11 resources (x11wrap.h), core window manager classes (Manager, Client, Border), event handling (Buttons.cpp, Events.cpp), entry point (main.cpp), and test files. The RAII wrappers are well-designed, but several correctness bugs and memory management issues were found in the core logic. Three critical bugs involve potential undefined behavior that can crash the window manager at runtime.

## Critical Issues

### CR-01: Circulate function has out-of-bounds access when client list is empty

**File:** `src/Buttons.cpp:54-58`
**Issue:** The `circulate()` function accesses `m_clients[j]` without verifying that `j` is a valid index. When `m_clients` is empty (or has only one entry), `j` starts at `i + 1` which is `0`, and the while-loop condition `!m_clients[j]->isNormal()` dereferences `m_clients[0]` without checking that `m_clients` is non-empty. Additionally, the loop increments `j` then checks `if (static_cast<size_t>(j) >= m_clients.size() - 1) j = -1;` but `m_clients.size() - 1` underflows to `SIZE_MAX` when the vector is empty (since `size()` returns an unsigned type), making the guard ineffective. This will crash with a segfault when no normal clients exist and the user right-clicks.

**Fix:**
```cpp
void WindowManager::circulate(bool activeFirst)
{
    Client *c = nullptr;
    if (activeFirst) c = m_activeClient;

    if (!c) {
        if (m_clients.empty()) return;  // guard

        int i = -1;

        if (!m_activeClient) {
            i = -1;
        } else {
            for (size_t idx = 0; idx < m_clients.size(); ++idx) {
                if (m_clients[idx] == m_activeClient) {
                    i = static_cast<int>(idx);
                    break;
                }
            }

            if (static_cast<size_t>(i) >= static_cast<int>(m_clients.size()) - 1) i = -1;
        }

        int j = i + 1;
        while (true) {
            if (static_cast<size_t>(j) >= m_clients.size()) j = 0;
            if (j == i) return;
            if (m_clients[j]->isNormal() && !m_clients[j]->isTransient()) break;
            ++j;
        }

        c = m_clients[j];
    }

    c->activateAndWarp();
}
```

### CR-02: Client::release() does not delete the Client object -- memory leak and dangling pointers

**File:** `src/Client.cpp:67-93` and `src/Events.cpp:224-247`
**Issue:** The upstream wm2 used `delete this` inside `Client::release()`. The modernized version calls `c->release()` from `WindowManager::eventDestroy()` after removing the client from `m_clients`, but `release()` never calls `delete this`, and the caller does not `delete` the object either. Every destroyed client leaks memory. This is not just a leak -- it is also a design regression from upstream where the client self-destructed.

**Fix:** In `Events.cpp`, after `c->release()` on line 241, add `delete c;`:
```cpp
// In WindowManager::eventDestroy, after line 241:
c->release();
delete c;  // actually free the Client memory

ignoreBadWindowErrors = true;
```

### CR-03: spawn() leaks memory from `new char[]` for DISPLAY environment variable

**File:** `src/Manager.cpp:486-488`
**Issue:** In `WindowManager::spawn()`, `putenv(envBuf)` is called with a buffer allocated via `new char[]`. The child process execs `xterm`, which replaces the process image and the OS reclaims the memory. But in the intermediate child (the one that calls `fork()` then `exit(0)` on line 496), the `envBuf` pointer is lost. More importantly, `putenv()` does not copy the string -- it takes ownership of the pointer. If the second fork fails, the first child exits and the buffer is leaked. Additionally, if `putenv` is called multiple times over the window manager's lifetime from the parent process context (it is not here, but the pattern is fragile), this would be a leak. The `new[]` should use `malloc()` to match `putenv()` semantics, or the string should be managed properly.

**Fix:**
```cpp
// Use malloc to match putenv semantics (POSIX says putenv takes ownership)
char *envBuf = static_cast<char*>(malloc(envStr.size() + 1));
if (envBuf) {
    std::strcpy(envBuf, envStr.c_str());
    putenv(envBuf);
}
```

## Warnings

### WR-01: UniqueCursor/UniqueFont/UniquePixmap/UniqueColormap implicit conversion operator allows unsafe XID passing

**File:** `include/x11wrap.h:94,144,194,244`
**Issue:** Each RAII wrapper has `operator XID() const noexcept` which allows implicit conversion. This means passing a `UniqueCursor` to a function that takes a `Cursor` will work silently, but the user may not realize the wrapper is still managing the resource. This is a design choice but creates risk: if someone writes `Cursor c = myUniqueCursor;` they now have a raw copy of the XID that could be used after the wrapper resets it. The `get()` method is safer for explicit access.

**Fix:** Consider removing the implicit `operator XID()` and requiring explicit `.get()` calls at call sites, or at minimum add a comment documenting the risk.

### WR-02: Client::manage() hardcodes screen 0 instead of using WindowManager's screen

**File:** `src/Client.cpp:180`
**Issue:** `DisplayWidth(display(), 0)` and `DisplayHeight(display(), 0)` hardcode screen index 0, but `WindowManager::m_screenNumber` could theoretically differ (though currently also set to 0). This inconsistency will produce wrong behavior if multi-screen support is ever added or if the screen index changes.

**Fix:**
```cpp
int dw = DisplayWidth(display(), windowManager()->screen());
int dh = DisplayHeight(display(), windowManager()->screen());
```

### WR-03: Client::ensureVisible() also hardcodes screen 0

**File:** `src/Client.cpp:633-634`
**Issue:** Same as WR-02. `DisplayWidth(display(), 0)` and `DisplayHeight(display(), 0)` hardcode screen 0.

**Fix:** Same as WR-02 -- use `windowManager()->screen()`.

### WR-04: Client::release() frees colormap data but destructor also frees it -- potential double-free

**File:** `src/Client.cpp:57-64` and `src/Client.cpp:86-92`
**Issue:** `Client::release()` frees `m_colormapWindows` and `m_windowColormaps` and sets them to null/zero. The destructor also checks `m_colormapWinCount > 0` and frees the same pointers. This is safe only because `release()` zeroes the count. However, if `release()` is never called (e.g., if the Client is destroyed via some other path), the destructor frees correctly. The logic is correct but fragile -- any future change that breaks the invariant will introduce a double-free. Consider using RAII (e.g., `std::vector<Window>` and `std::vector<Colormap>`) instead.

**Fix:** Replace raw pointer arrays with `std::vector<Window>` for `m_colormapWindows` and `std::vector<Colormap>` for `m_windowColormaps`, eliminating manual `malloc`/`free`/`XFree`.

### WR-05: WindowManager::windowToClient creates Client with raw `new` but there is no corresponding `delete`

**File:** `src/Manager.cpp:368-370`
**Issue:** `Client *newC = new Client(this, w);` allocates on the heap. As identified in CR-02, the `release()` method does not `delete this`, and the event handlers do not `delete` either. Every Client created is leaked. This is the allocation-site counterpart to CR-02.

**Fix:** This is resolved by fixing CR-02 (adding `delete c` after `c->release()` in `eventDestroy`). For additional safety, consider using `std::unique_ptr<Client>` in `m_clients` and transferring ownership appropriately.

### WR-06: GCDeleter does not null-check its stored display pointer against the GC's actual display

**File:** `include/x11wrap.h:19-25`
**Issue:** `GCDeleter` stores a raw `Display*` that is set at creation time in `make_gc()`. If the `DisplayPtr` that owns the display is destroyed before the `GCPtr`, the `GCDeleter` will call `XFreeGC` with a dangling `Display*`, causing undefined behavior. The comment in `test_smoke.cpp:41` notes "declared first = destroyed last" but this relies on member declaration order within a single scope, not across all scopes. If a `GCPtr` outlives its `DisplayPtr`, this is a use-after-free.

**Fix:** This is inherent to the RAII design with raw X11 handles. Consider documenting the lifetime requirement prominently, or having `GCPtr` / `FontStructPtr` hold a `std::shared_ptr` to the display.

### WR-07: Menu selection off-by-one for hidden vs. non-hidden clients

**File:** `src/Buttons.cpp:272-281`
**Issue:** The menu selection logic at lines 272-281 uses `nh` (number of hidden clients + 1 for the "New" entry) to distinguish hidden from normal clients. Line 275: `if (selecting < nh)` maps to `clients[selecting - 1]` for unhide. Line 277: `if (selecting < n)` also maps to `clients[selecting - 1]` for mapRaised/ensureVisible. But the `clients` vector contains only hidden clients (populated at line 108-110). A non-hidden client can never be in this vector, so line 278 `clients[selecting - 1]->mapRaised()` would unhide a hidden client thinking it is a normal one. This appears to be an inherited upstream logic issue where the menu was supposed to show all clients, not just hidden ones.

**Fix:** Verify the intended menu behavior. If the menu should show all clients (not just hidden), populate the `clients` vector with `m_clients` entries too. If hidden-only is intended, remove the dead branch at lines 277-279.

### WR-08: Border::drawLabel passes non-null-terminated data via const_cast

**File:** `src/Border.cpp:133-134`
**Issue:** `const_cast<char*>(m_label.c_str())` is passed to `XRotDrawString` along with an explicit length. While `c_str()` returns a null-terminated string and the length parameter should prevent overreads, the `const_cast` is a code smell that suggests the XRotDrawString API may modify the buffer. If it does, this modifies `m_label`'s internal buffer through the back door, violating `std::string` invariants.

**Fix:** If `XRotDrawString` does not modify its string argument (likely, given it is a drawing function), the upstream `Rotated.h` declaration should be updated to take `const char*`. If it might modify the buffer, copy to a local char array before passing.

## Info

### IN-01: WindowManager::initialiseScreen uses hardcoded screen index 0

**File:** `src/Manager.cpp:207-208`
**Issue:** `int i = 0; m_screenNumber = i;` assigns a constant. This is vestigial from upstream where the loop variable `i` was used. The variable `i` serves no purpose and should be replaced with `m_screenNumber = 0` or `m_screenNumber = DefaultScreen(display())`.

**Fix:** Replace `int i = 0; m_screenNumber = i;` with `m_screenNumber = DefaultScreen(display());`.

### IN-02: CMakeLists.txt has build-tree relative include path for Xvfb start fixture

**File:** `CMakeLists.txt:79`
**Issue:** The Xvfb start test command uses `${CMAKE_BINARY_DIR}/xvfb.pid` for the PID file, which is correct. However, the `kill` command in the cleanup fixture (line 84) uses `2>/dev/null || true` to suppress errors. If the Xvfb process was already killed by a signal or crashed, `kill` will try to signal a PID from a stale file. This is benign but worth noting.

**Fix:** No fix needed -- this is a test infrastructure best practice.

### IN-03: Cursor bitmap data in Cursors.h uses static arrays without include guard deduplication protection

**File:** `include/Cursors.h:9-83`
**Issue:** The cursor bitmap arrays are declared `static` in a header, which means each translation unit that includes this header gets its own copy. This wastes a small amount of memory. The arrays should be in an anonymous namespace in a single `.cpp` file, or declared `inline` (C++17) in the header.

**Fix:** Move the bitmap arrays to `Manager.cpp` (the only consumer) or wrap them in `inline constexpr` for C++17.

### IN-04: WindowManager::menuLabel is a dead function

**File:** `src/Manager.cpp:504-508`
**Issue:** The `menuLabel()` method has a comment "Stub -- Buttons.cpp will use a different approach" and always returns `m_menuCreateLabel`. The actual menu in `Buttons.cpp` uses a lambda `menuLabelFn` instead. This dead method should be removed to avoid confusion.

**Fix:** Remove `menuLabel()` declaration from `Manager.h` and its definition from `Manager.cpp`.

---

_Reviewed: 2026-05-06_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
