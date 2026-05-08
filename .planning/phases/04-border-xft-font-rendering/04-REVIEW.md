---
phase: 04-border-xft-font-rendering
reviewed: 2026-05-07T12:00:00Z
depth: standard
files_reviewed: 9
files_reviewed_list:
  - CMakeLists.txt
  - include/Border.h
  - include/Manager.h
  - include/x11wrap.h
  - src/Border.cpp
  - src/Buttons.cpp
  - src/Manager.cpp
  - tests/test_xft_poc.cpp
  - .gitignore
findings:
  critical: 2
  warning: 5
  info: 3
  total: 10
status: issues_found
---

# Phase 4: Code Review Report

**Reviewed:** 2026-05-07
**Depth:** standard
**Files Reviewed:** 9
**Status:** issues_found

## Summary

Reviewed the Xft/fontconfig migration phase covering RAII wrappers for Xft resources, rotated font rendering via FcMatrix, menu/geometry display with Xft, and rectangular border fallback. Found 2 critical issues (a stack buffer overflow in geometry display and a use-after-destroy order in Border destructor), 5 warnings (missing null checks, fragile index logic, unused memory), and 3 info items.

## Critical Issues

### CR-01: Stack buffer overflow in showGeometry

**File:** `src/Buttons.cpp:321-322`
**Issue:** `showGeometry` formats two integers into a `char string[20]` buffer using `sprintf("%d %d\n", x, y)`. With 32-bit int values, the worst case is `"-2147483648 -2147483648\n"` which is 24 characters plus null terminator = 25 bytes. The 20-byte buffer overflows by at least 5 bytes, corrupting the stack. This is reachable every time the user moves or resizes a window, with coordinates controlled by window position (which can be large on high-resolution displays or multi-monitor setups).

**Fix:**
```cpp
char string[32];  // sufficient for any two 32-bit ints + space + newline + null
std::snprintf(string, sizeof(string), "%d %d", x, y);
```

### CR-02: XftDraw destroyed after its bound window in Border destructor

**File:** `src/Border.cpp:97-104`
**Issue:** `XDestroyWindow(display(), m_tab)` is called at line 97, but `m_tabDraw.reset()` (which calls `XftDrawDestroy`) is called at line 104, after the tab window has already been destroyed. The `XftDraw` object was created bound to `m_tab` (see `drawLabel()` at line 239). Xft internally tracks the drawable and may issue X protocol requests during destroy that reference the now-destroyed window, causing X errors or undefined behavior. Compare with `Manager::release()` which correctly destroys `m_menuDraw` at line 152 before `m_menuWindow` at line 161-163.

**Fix:**
```cpp
Border::~Border()
{
    m_tabDraw.reset();  // destroy XftDraw BEFORE the window it's bound to

    if (m_parent != root()) {
        if (!m_parent) {
            std::fprintf(stderr, "wm2: zero parent in Border::~Border\n");
        } else {
            XDestroyWindow(display(), m_tab);
            XDestroyWindow(display(), m_button);
            XDestroyWindow(display(), m_parent);
            XDestroyWindow(display(), m_resize);
        }
    }

    if (--m_borderCount == 0) {
        // ... static cleanup ...
    }
}
```

## Warnings

### WR-01: No null check on m_tabFont in drawLabel and fixTabHeight

**File:** `src/Border.cpp:252, 300, 313, 323`
**Issue:** `drawLabel()` and `fixTabHeight()` pass `m_tabFont` to `XftTextExtentsUtf8` without null checks. If the static font pointer were somehow null (e.g., after a resource cleanup race or if a future code path reinitializes statics), these would pass null to Xft, causing a crash. The constructor guards against null font loading with `fatal()`, but defensive programming would make this more robust against future changes.

**Fix:** Add an early return in `drawLabel()`:
```cpp
void Border::drawLabel()
{
    if (m_label.empty() || !m_tabFont) return;
    // ...
}
```
And in `fixTabHeight()`:
```cpp
void Border::fixTabHeight(int maxHeight)
{
    m_tabHeight = 0x7fff;
    maxHeight -= m_tabWidth;
    m_label = m_client->label();
    if (!m_tabFont) { m_tabHeight = maxHeight; return; }
    // ...
}
```

### WR-02: m_tabWidth could remain -1 if shapeAvailable() is false and no font loads

**File:** `src/Border.cpp:11, 58-61`
**Issue:** `m_tabWidth` is initialized to `-1` (line 11). It gets set to a valid positive value at line 58 only inside the `if (m_tabFont == nullptr)` block of the constructor. If the first Border is constructed, `m_tabFont` gets set, and subsequent Border instances skip that block entirely. The value is set correctly for the first instance, but the static initialization pattern means `m_tabWidth` transitions from `-1` to a valid value. If `m_tabFont` were somehow already set (e.g., from a previous partially-constructed Border), `m_tabWidth` would remain `-1`. This is unlikely but the sentinel value `-1` being used as a valid `m_tabWidth` in geometry calculations (e.g., `xIndent()` at Border.h:49) could cause negative window offsets.

**Fix:** Initialize `m_tabWidth` to a safe default, or add an assertion after the constructor:
```cpp
// After the if (m_tabFont == nullptr) block:
if (m_tabWidth <= 0) {
    windowManager()->fatal("tab width not initialized");
}
```

### WR-03: Fragile menu index arithmetic in menu() selection dispatch

**File:** `src/Buttons.cpp:126-129, 301-315`
**Issue:** The `menuLabelFn` lambda and selection dispatch at lines 306-315 use raw index arithmetic with `clients[selecting - 1]` that depends on the exact relationship between `n`, `nh`, and `clients.size()`. The current code is safe because the menu only contains hidden clients plus "New" and optionally "Exit", and the exit case is checked first at line 301. However, the `else if (selecting < n)` branch at line 311 accesses `clients[selecting - 1]` which would be out of bounds if a visible client entry were ever added to the menu. The index arithmetic is error-prone and would silently corrupt memory if the menu contents change.

**Fix:** Add bounds checking before accessing `clients`:
```cpp
} else if (selecting >= nh && selecting < n) {
    int clientIdx = selecting - 1;
    if (clientIdx >= 0 && clientIdx < static_cast<int>(clients.size())) {
        clients[clientIdx]->mapRaised();
        clients[clientIdx]->ensureVisible();
    }
}
```

### WR-04: m_drawGC static shared with raw pointer semantics, not thread-safe

**File:** `src/Border.h:86, src/Border.cpp:13, 78-84, 107`
**Issue:** `m_drawGC` is a static `x11::GCPtr` (unique_ptr) shared across all Border instances. It is initialized in the first Border constructor and reset when `m_borderCount` drops to 0. If a Border is destroyed while another Border's method is executing and using `m_drawGC.get()`, the reset at line 107 could invalidate the GC mid-use. This is currently single-threaded so not a race condition per se, but it is a use-after-free risk if the destruction order is wrong. Additionally, the `m_drawGC` is used in `eventButton()` at line 918 via `m_drawGC.get()` for `XFillRectangle` -- if that button event fires while the last Border is being destroyed, it could use a freed GC.

**Fix:** This is an architectural concern. A short-term improvement would be to not reset `m_drawGC` in the destructor and instead let it persist until program exit, or to verify the calling context ensures no concurrent use during shutdown.

### WR-05: XftDrawChange called on m_menuDraw without verifying m_menuWindow is still valid

**File:** `src/Buttons.cpp:176, 342`
**Issue:** `XftDrawChange(m_menuDraw.get(), m_menuWindow)` is called at line 176 and 342 to rebind the existing XftDraw to the menu window. If `m_menuWindow` were `None` (e.g., if called after `release()` has run), this would pass an invalid XID to Xft. The `menu()` function checks `e->window == m_menuWindow` at line 108 but doesn't verify `m_menuWindow != None`. Similarly, `showGeometry()` has no guard at all.

**Fix:** Add a guard at the start of `showGeometry()`:
```cpp
void WindowManager::showGeometry(int x, int y)
{
    if (m_menuWindow == None) return;
    // ...
}
```

## Info

### IN-01: Unused variable `width` in menu() measurement loop

**File:** `src/Buttons.cpp:132, 139`
**Issue:** `width` is declared at line 132 and assigned at line 139 inside the loop, but the only purpose is to compare against `maxWidth`. It shadows the class-level concept of width but is local scope. This is not a bug, but the variable name `width` is used for both the measurement loop result and the conceptual menu width, which could confuse readers.

**Fix:** Rename to `entryWidth` for clarity:
```cpp
int entryWidth = 0, maxWidth = 10;
for (int i = 0; i < n; ++i) {
    // ...
    entryWidth = extents.width;
    if (entryWidth > maxWidth) maxWidth = entryWidth;
}
```

### IN-02: Unused header includes in x11wrap.h

**File:** `include/x11wrap.h:5`
**Issue:** `fontconfig/fontconfig.h` is included in `x11wrap.h` but only used by the `make_xft_font_rotated` function (which uses `FcPattern`, `FcNameParse`, etc.). The non-rotated Xft wrappers don't need fontconfig directly. This coupling forces all translation units including `x11wrap.h` to have fontconfig headers available, even if they only use the Xft RAII types.

**Fix:** Consider moving `make_xft_font_rotated` to a separate header (e.g., `x11font.h`) or forward-declaring the fontconfig types. This is a minor code organization issue.

### IN-03: Memory allocation failure not checked for FcNameParse result

**File:** `include/x11wrap.h:297`
**Issue:** `make_xft_font_rotated` calls `FcNameParse` and checks for null return at line 298, which is correct. However, `FcConfigSubstitute` and `XftDefaultSubstitute` at lines 300-301 could theoretically fail silently on memory exhaustion. This is a minor robustness concern -- fontconfig handles allocation internally and the null check on `FcFontMatch` at line 312 provides a secondary guard.

**Fix:** No action required. The existing null checks provide adequate protection for the documented failure modes.

---

_Reviewed: 2026-05-07_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
