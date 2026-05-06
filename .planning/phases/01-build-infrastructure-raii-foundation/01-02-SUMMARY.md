---
phase: 01-build-infrastructure-raii-foundation
plan: 02
subsystem: wm-core
tags: [c++17, raii, x11, xlib, xshape, xvertext, window-manager, event-loop, border-decoration]

# Dependency graph
requires: ["01-01"]
provides:
  - Working wm2-born-again binary that opens X display and manages windows
  - Modernized headers (Manager.h, Client.h, Border.h) with C++17 types
  - Full source implementations (main.cpp, Manager.cpp, Client.cpp, Border.cpp, Buttons.cpp, Events.cpp)
affects: [02-event-loop-modernization, 03-client-lifecycle, 04-border-xft, all subsequent phases]

# Tech tracking
tech-stack:
  added: []
  patterns: [std::vector replacing listmacro2.h, std::string replacing char*/NewString, std::unique_ptr<Border> replacing raw new/delete, x11::DisplayPtr/GCPtr/UniqueCursor/FontStructPtr for RAII resource management, std::vector<XRectangle> replacing custom RectangleList macro, enum class for protocol flags]

key-files:
  created:
    - include/Manager.h
    - include/Client.h
    - include/Border.h
  modified:
    - src/main.cpp
    - src/Manager.cpp
    - src/Client.cpp
    - src/Border.cpp
    - src/Buttons.cpp
    - src/Events.cpp

key-decisions:
  - "Client::display() moved from private to public section so Border can call it (Border delegates display/root access through Client)"
  - "Border::root() made const for const-correctness in hasWindow()"
  - "Client::activeClient() made const to allow isActive() const to call it"
  - "installCursorOnWindow() added to Manager for Border to set cursor on resize handle without accessing private cursor members"
  - "std::vector<XRectangle> used in Border.cpp instead of custom RectangleList macro from listmacro2.h"
  - "malloc() retained only for Colormap array allocation in Client::getColormaps() (X11 Colormap* array, not string duplication)"
  - "Shape extension check warns but continues on missing extension (graceful fallback per CONCERNS.md)"

patterns-established:
  - "Event dispatch: WindowManager::loop() switches on event type, calls event*() methods, delegates to Client event handlers"
  - "Menu pattern: std::vector<Client*> for temporary menu client list, lambda for label resolution"
  - "Border static resources: m_borderCount reference count, RAII cleanup when last Border destroyed"
  - "Property reading: XGetWindowProperty wrapped in helper, result copied to std::string, XFree called on raw pointer"

requirements-completed: [BLD-02, BLD-04]

# Metrics
duration: 22min
completed: 2026-05-06
---

# Phase 1 Plan 02: Modernized C++17 WM Source Files Summary

**All 9 source/header files compile into a working wm2-born-again binary that opens X display, manages windows with shaped tab borders, and runs a select()-based event loop -- no upstream legacy types remain**

## Performance

- **Duration:** 22 min
- **Started:** 2026-05-06T20:37:05Z
- **Completed:** 2026-05-06T20:59:55Z
- **Tasks:** 3
- **Files modified:** 9

## Accomplishments
- Working WM binary starts on Xvfb, prints banner, detects Shape extension, runs event loop, exits cleanly on signal
- All 7 RAII wrapper types from Plan 01 consumed: DisplayPtr, GCPtr, FontStructPtr, UniqueCursor, UniqueFont, UniquePixmap, UniqueColormap
- Full upstream wm2 functionality modernized: window management, border decoration with shaped tabs, root menu, move/resize, focus tracking
- Zero manual XCloseDisplay/XFreeCursor/XFreeGC/XFreeFont calls -- all cleanup via RAII
- All code under src/ and include/ uses C++17 idioms exclusively (std::string, std::vector, std::unique_ptr, bool, enum class)

## Task Commits

Each task was committed atomically:

1. **Task 1: Create modernized headers** - `f978c86` (feat)
2. **Task 2: Create core event loop implementations** - `93db67b` (feat)
3. **Task 3: Create client lifecycle, border, and menu** - `9c08be8` (feat)

## Files Created/Modified
- `include/Manager.h` - WindowManager class with RAII members, std::vector<Client*>, Atoms struct
- `include/Client.h` - Client class with std::string members, std::unique_ptr<Border>, enum class Protocol
- `include/Border.h` - Border class with std::string m_label, static x11::GCPtr, constexpr frame dimensions
- `src/main.cpp` - Entry point creating WindowManager
- `src/Manager.cpp` - Full WindowManager lifecycle (display open, screen init, atoms, cursors, colors, fonts, event loop)
- `src/Client.cpp` - Client lifecycle (manage, release, activate, move, resize, property handling, event handlers)
- `src/Border.cpp` - Border frame creation with Shape extension, rotated text labels, static resource management
- `src/Buttons.cpp` - Root menu, button event dispatch, circulate, spawn, geometry display
- `src/Events.cpp` - select()-based event loop with dispatch for all core X11 events

## Decisions Made
- Client::display() moved from private to public so Border can call it (Border delegates through Client)
- installCursorOnWindow() added to Manager for Border to set cursor on resize handle
- std::vector<XRectangle> replaces custom RectangleList macro in Border.cpp
- Shape extension missing produces warning but not fatal exit (graceful fallback)
- malloc() retained only for Colormap array allocation (not for strings)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] XV_COPYRIGHT undeclared in Manager.cpp**
- **Found during:** Task 2 (build phase)
- **Issue:** XV_COPYRIGHT macro defined in Rotated.h, not included by Manager.cpp
- **Fix:** Added `#include "Rotated.h"` to Manager.cpp
- **Files modified:** src/Manager.cpp
- **Verification:** Build succeeds
- **Committed in:** 93db67b (Task 2 commit)

**2. [Rule 3 - Blocking] Incomplete type WindowManager in Client.h inline methods**
- **Found during:** Task 3 (build phase)
- **Issue:** `fatal()` and `display()` inline methods in Client.h need WindowManager complete type, but only forward-declared
- **Fix:** Moved implementations to Client.cpp (out-of-line)
- **Files modified:** include/Client.h, src/Client.cpp
- **Verification:** Build succeeds
- **Committed in:** 9c08be8 (Task 3 commit)

**3. [Rule 3 - Blocking] isActive() const calls non-const activeClient()**
- **Found during:** Task 3 (build phase)
- **Issue:** `isActive()` is const but `activeClient()` was not const
- **Fix:** Made `activeClient()` const
- **Files modified:** include/Client.h, src/Client.cpp
- **Verification:** Build succeeds
- **Committed in:** 9c08be8 (Task 3 commit)

**4. [Rule 3 - Blocking] Jump to case label crosses initialization in Client::move()**
- **Found during:** Task 3 (build phase)
- **Issue:** Variable `state` declared in ButtonRelease case, but MotionNotify case can jump past it
- **Fix:** Wrapped ButtonRelease case body in braces to create scope
- **Files modified:** src/Client.cpp
- **Verification:** Build succeeds
- **Committed in:** 9c08be8 (Task 3 commit)

**5. [Rule 3 - Blocking] Border accessing private Client::display()**
- **Found during:** Task 3 (build phase)
- **Issue:** Border::display() calls m_client->display() which was private
- **Fix:** Moved display() to public section of Client
- **Files modified:** include/Client.h
- **Verification:** Build succeeds
- **Committed in:** 9c08be8 (Task 3 commit)

**6. [Rule 3 - Blocking] Border::root() called from const hasWindow()**
- **Found during:** Task 3 (build phase)
- **Issue:** hasWindow() is const but root() was not const
- **Fix:** Made root() const
- **Files modified:** include/Border.h, src/Border.cpp
- **Verification:** Build succeeds
- **Committed in:** 9c08be8 (Task 3 commit)

**7. [Rule 3 - Blocking] Variable 'i' scope in Buttons.cpp menu()**
- **Found during:** Task 3 (build phase)
- **Issue:** Variable `i` declared in nested scope inside ButtonRelease case but used after scope exit
- **Fix:** Renamed to `sel` and declared at function scope of the case block
- **Files modified:** src/Buttons.cpp
- **Verification:** Build succeeds
- **Committed in:** 9c08be8 (Task 3 commit)

**8. [Rule 3 - Blocking] Border accessing private WindowManager cursor members**
- **Found during:** Task 3 (implementation phase)
- **Issue:** Border::shapeResize() needed to install cursor on resize handle window, but m_vhCursor is private
- **Fix:** Added installCursorOnWindow() method to WindowManager, used in Border instead of direct member access
- **Files modified:** include/Manager.h, src/Manager.cpp, src/Border.cpp
- **Verification:** Build succeeds
- **Committed in:** 9c08be8 (Task 3 commit)

---

**Total deviations:** 8 auto-fixed (all blocking -- compile errors from C++17 strictness)
**Impact on plan:** All auto-fixes were compile-error corrections needed for C++17 compliance. No scope creep, no architectural changes.

## Issues Encountered
- C++17 stricter about const-correctness and variable scope in switch statements compared to pre-standard C++ -- 8 compile errors fixed during implementation
- All fixes were mechanical (const qualifiers, scope braces, access specifier changes)

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Working WM binary ready for Phase 2 (event loop modernization to poll())
- Client lifecycle ready for Phase 3 (unique_ptr<Client>, hash map lookup)
- Border decoration ready for Phase 4 (Xft/fontconfig replacing xvertext)

---
*Phase: 01-build-infrastructure-raii-foundation*
*Completed: 2026-05-06*
