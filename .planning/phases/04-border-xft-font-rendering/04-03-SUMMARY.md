---
phase: 04-border-xft-font-rendering
plan: 03
subsystem: border-xft-font-rendering
tags: [xft, fontconfig, menu, geometry, xvertext-removal, utf8]
requirements-completed: [VISL-02, VISL-04]

# Dependency graph
requires:
  - phase: 04-border-xft-font-rendering/01
    provides: [XftFontPtr, XftDrawPtr, XftColorWrap, make_xft_font_name, hasShapeExtension]
provides:
  - Xft-based menu text rendering with XftDrawStringUtf8
  - Xft-based geometry display (move/resize coordinates)
  - Explicit highlight draw/erase replacing XOR raster ops
  - Complete removal of xvertext (Rotated.C) from build
  - Complete removal of core X font calls from all src/ files
affects: []

# Tech tracking
tech-stack:
  added: []
  patterns: [explicit highlight draw/erase replacing GXxor, XftDrawChange for window reuse, lazy XftDraw creation]

key-files:
  created: []
  modified: [include/Manager.h, src/Manager.cpp, src/Buttons.cpp, CMakeLists.txt]

key-decisions:
  - "Menu highlight uses explicit draw with gray60 XftColor and erase with gray80 background instead of XOR raster ops"
  - "XftDraw created lazily on first menu/showGeometry call, XftDrawChange used for window reuse"
  - "Menu border outline drawn with four 1px XftDrawRect calls instead of XDrawRectangle"
  - "xvertext copyright notice (XV_COPYRIGHT) removed from startup banner along with Rotated.h include"

patterns-established:
  - "XftDraw lifecycle: create lazily, rebind with XftDrawChange on window resize/reuse"
  - "Menu highlight: XftDrawRect with highlight color (draw) and background color (erase)"
  - "Text measurement: XftTextExtentsUtf8 for all menu label width calculations"

# Metrics
duration: 8min
tasks: 2
files: 4
completed: "2026-05-07"
---

# Phase 4 Plan 03: Menu Xft Rendering Summary

**Replaced all core X font rendering in menu and geometry display with Xft, removed the xvertext library (Rotated.C) from the build, and eliminated all XDrawString/XTextWidth calls from src/**

## Performance

- **Duration:** 8 min
- **Started:** 2026-05-07T18:56:19Z
- **Completed:** 2026-05-07T19:52:55Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments
- Root menu text renders with Xft antialiased fonts via XftDrawStringUtf8
- Menu highlight uses explicit draw (gray60) and erase (gray80) with XftDrawRect, replacing XOR raster ops
- Geometry display (move/resize coordinates) uses Xft text rendering
- Rotated.C (500-line xvertext library) removed from build and upstream-wm2 directory removed from include paths
- No core X font calls (XDrawString, XTextWidth, XLoadQueryFont) remain anywhere in src/
- No xvertext references (XRot*, Rotated.h) remain anywhere in src/ or include/

## Task Commits

Each task was committed atomically:

1. **Task 1: Rewrite Manager.h menu members, Manager.cpp initialization, and Buttons.cpp Xft rendering** - `e618def` (feat)
2. **Task 2: Remove Rotated.C from build, remove xvertext dependency** - `f408e5d` (feat)

## Files Created/Modified
- `include/Manager.h` - Replaced GCPtr/FontStructPtr menu members with XftFont*, XftDrawPtr, XftColorWrap; added Xft.h include
- `src/Manager.cpp` - Rewrote initialiseScreen() for Xft font loading and color allocation; removed Rotated.h include; updated release() for Xft cleanup; removed XV_COPYRIGHT
- `src/Buttons.cpp` - Replaced all XDrawString with XftDrawStringUtf8, XTextWidth with XftTextExtentsUtf8, XFillRectangle with XftDrawRect; added lazy XftDraw creation
- `CMakeLists.txt` - Removed upstream-wm2/Rotated.C from source list, removed -w compile flags, removed upstream-wm2 from include directories

## Decisions Made
- Menu highlight uses gray60 XftColor for selection and gray80 background for deselection, replacing the GXxor approach which is incompatible with Xft/XRender
- XftDraw created lazily at menu/showGeometry entry, using XftDrawChange to rebind when the menu window is resized or reused
- Menu border outline drawn with four 1px XftDrawRect calls (top, bottom, left, right) instead of XDrawRectangle
- The xvertext copyright string (XV_COPYRIGHT macro from Rotated.h) was replaced with inline text in the startup banner since Rotated.h is no longer included

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] XV_COPYRIGHT macro undefined after Rotated.h removal**
- **Found during:** Task 1 (Manager.cpp compilation)
- **Issue:** XV_COPYRIGHT was defined in Rotated.h; removing the include caused compilation error
- **Fix:** Replaced the format string that used XV_COPYRIGHT with inline text, removing the xvertext attribution since the library is no longer part of the build
- **Files modified:** src/Manager.cpp
- **Verification:** Build succeeded, all tests pass
- **Committed in:** e618def

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Minor -- XV_COPYRIGHT removal is a natural consequence of removing Rotated.h.

## Issues Encountered

None -- all changes compiled and passed tests on first attempt after the XV_COPYRIGHT fix.

## Next Phase Readiness
- Phase 4 is complete: all text rendering uses Xft, xvertext is fully removed
- The codebase has zero core X font usage in application code (only x11wrap.h infrastructure retains XLoadQueryFont for the make_font_struct factory)
- VISL-02 (all text rendering uses Xft) and VISL-04 (fontconfig patterns) are fulfilled

---
*Phase: 04-border-xft-font-rendering*
*Completed: 2026-05-07*

## Self-Check: PASSED

All 5 modified/created files verified present. Both task commits (e618def, f408e5d) verified in git log.
