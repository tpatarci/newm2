---
phase: 02-event-loop-modernization
fixed_at: 2026-05-07T05:38:13Z
review_path: .planning/phases/02-event-loop-modernization/02-REVIEW.md
iteration: 1
findings_in_scope: 7
fixed: 7
skipped: 0
status: all_fixed
---

# Phase 2: Code Review Fix Report

**Fixed at:** 2026-05-07T05:38:13Z
**Source review:** .planning/phases/02-event-loop-modernization/02-REVIEW.md
**Iteration:** 1

**Summary:**
- Findings in scope: 7
- Fixed: 7
- Skipped: 0

## Fixed Issues

### CR-01: Double-free of colormap resources in Client destructor + release()

**Files modified:** `src/Client.cpp`
**Commit:** 5febcb5
**Applied fix:** Made the destructor call `release()` (guarded by `m_window != None`) instead of duplicating cleanup. Added early return in `release()` when `m_window == None` to prevent double-invocation. Removed the redundant colormap cleanup block from the destructor.

### CR-02: Null pointer dereference on m_focusCandidate in checkDelaysForFocus()

**Files modified:** `src/Manager.cpp`, `src/Events.cpp`
**Commit:** c10d24b
**Applied fix:** Added null guard `if (!m_focusCandidate || !m_focusChanging) return;` at the top of `checkDelaysForFocus()`. In `eventDestroy()`, replaced the incomplete `m_focusChanging = false` with a full `stopConsideringFocus()` call plus `m_focusCandidate = nullptr` when the destroyed client is the focus candidate.

### CR-03/WR-07: Memory leak in spawn() using putenv()

**Files modified:** `src/Manager.cpp`
**Commit:** 98dff24
**Applied fix:** Replaced `new char[]` allocation + `putenv()` with `setenv("DISPLAY", displayName, 1)`. This eliminates the manual allocation and putenv's ownership transfer semantics. Covers both CR-03 and WR-07 since they are the same issue.

### WR-01: Unchecked malloc return in getColormaps()

**Files modified:** `src/Client.cpp`
**Commit:** be4c9c8
**Applied fix:** Added null check after `malloc()` for `m_windowColormaps`. If allocation fails, resets `m_colormapWinCount` to 0, frees `m_colormapWindows`, nulls the pointer, and returns early to prevent null pointer dereference in the subsequent loop.

### WR-03: move() and resize() inner loops use select() for sleep

**Files modified:** `src/Client.cpp`
**Commit:** 25a724e
**Applied fix:** Replaced `select(0, nullptr, nullptr, nullptr, &sleepval)` with `poll(nullptr, 0, 50)` in both `move()` and `resize()`. Added `#include <poll.h>`. Removed the now-unused `struct timeval sleepval` declarations from both functions.

### WR-06: Potential integer overflow in computePollTimeout()

**Files modified:** `src/Manager.cpp`
**Commit:** 44e4f53
**Applied fix:** Clamped the computed millisecond value to a 30-second maximum before casting to `int`, preventing overflow if a deadline is very far in the future or uninitialized.

## Skipped Issues

None -- all in-scope findings were fixed.

---

_Fixed: 2026-05-07T05:38:13Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
