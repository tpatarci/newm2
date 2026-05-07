---
phase: 04-border-xft-font-rendering
plan: 01
subsystem: border-xft-font-rendering
tags: [xft, fontconfig, raii, shape, poc]
dependency_graph:
  requires: []
  provides: [XftFontPtr, XftDrawPtr, XftColorWrap, make_xft_font_name, make_xft_font_rotated, hasShapeExtension]
  affects: [x11wrap.h, Manager.h, CMakeLists.txt]
tech_stack:
  added: [libxft-dev, libfontconfig1-dev, Xft/fontconfig headers]
  patterns: [XftFontPtr unique_ptr + custom deleter, XftDrawPtr move-only RAII, XftColorWrap with Display/Visual/Colormap cleanup, FcMatrix 90-degree rotation]
key_files:
  created: [tests/test_xft_poc.cpp]
  modified: [include/x11wrap.h, include/Manager.h, CMakeLists.txt, .gitignore]
decisions:
  - Rotated Xft fonts have zero ascent/descent/height; text measurement must use XftTextExtentsUtf8 instead
  - Xft include dirs propagated to all test targets since x11wrap.h unconditionally includes Xft.h
  - test_xft_poc links against Xext for Shape extension testing
metrics:
  duration: 16min
  tasks: 3
  files: 5
  tests_added: 5
  completed: "2026-05-07"
---

# Phase 4 Plan 01: Xft PoC and RAII Wrappers Summary

Validated the core technical assumption that Xft text renders correctly inside shaped X11 windows, added three RAII wrapper types (XftFontPtr, XftDrawPtr, XftColorWrap) to x11wrap.h, and installed the Xft/fontconfig build dependencies needed by Plans 02 and 03.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | Install dev packages, update CMakeLists.txt, create test_xft_poc target | 8da4914 | CMakeLists.txt, tests/test_xft_poc.cpp |
| 2 | Add Xft RAII wrappers to x11wrap.h, add hasShapeExtension() to Manager.h | ed09d44 | include/x11wrap.h, include/Manager.h, CMakeLists.txt |
| 3 | Implement Xft PoC test validating rendering in shaped window | e5fcd21 | tests/test_xft_poc.cpp, CMakeLists.txt |

## Key Findings

### Rotated Font Metrics
Rotated Xft fonts (via FcMatrix) have zero `ascent`, `descent`, and `height` fields in the XftFont struct. This is expected because glyph metrics exist in a rotated coordinate system. Text measurement must use `XftTextExtentsUtf8` which correctly returns non-zero width/height for rotated text. The plan for Border tab rendering (Plan 02) must use extent-based measurement instead of font metrics.

### Xft + Shape Interaction
Confirmed assumption A1: Xft text renders inside shaped X11 windows without errors. XShape clips display output at the server level, not rendering commands. The rendering call succeeds regardless of the shape mask.

### Build Propagation
Adding Xft/fontconfig headers to x11wrap.h required propagating the XFT include directories (which include freetype2 headers) to all test targets that include x11wrap.h. Without this, compilation fails with "ft2build.h: No such file or directory".

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Xft include dirs needed on all test targets**
- **Found during:** Task 2
- **Issue:** x11wrap.h now includes Xft.h which requires freetype2 headers; existing test targets (test_raii, test_smoke, etc.) failed to compile
- **Fix:** Added ${XFT_INCLUDE_DIRS} to all test targets that include x11wrap.h
- **Files modified:** CMakeLists.txt
- **Commit:** ed09d44

**2. [Rule 3 - Blocking] test_xft_poc needed Xext link for Shape extension**
- **Found during:** Task 3
- **Issue:** Linker error -- undefined reference to XShapeQueryExtension and XShapeCombineRectangles
- **Fix:** Added PkgConfig::XEXT to test_xft_poc link libraries
- **Files modified:** CMakeLists.txt
- **Commit:** e5fcd21

**3. [Rule 1 - Bug] Test crashes from RAII destruction order with raw Display***
- **Found during:** Task 3
- **Issue:** Tests using raw Display* + XCloseDisplay() crashed because RAII wrappers (XftFontPtr, XftColorWrap) tried to use the display pointer in their destructors after XCloseDisplay was called
- **Fix:** Replaced raw Display* with x11::DisplayPtr in all tests; also adapted rotated font metrics test to use XftTextExtentsUtf8 instead of font->ascent (which is zero for rotated fonts)
- **Files modified:** tests/test_xft_poc.cpp
- **Commit:** e5fcd21

### Test Adjustments

- Rotated font test (Test 2): Changed assertion from `font->ascent > 0` to `extents.width > 0 && extents.height > 0` since rotated Xft fonts have zero struct metrics
- Empty string measurement (Test 5): Removed the empty-string XftTextExtentsUtf8 test since it's not essential and replaced it with a shorter-vs-longer text comparison test

## Test Results

All 53 tests pass (48 existing + 5 new Xft PoC tests):

- Xft font loads by fontconfig pattern -- PASS
- Rotated font loads via FcMatrix -- PASS
- Xft text renders inside shaped window -- PASS (validates A1, D-05)
- XftColor allocation and cleanup -- PASS
- Xft text measurement with XftTextExtentsUtf8 -- PASS
- UTF-8 string rendering does not crash -- PASS (validates VISL-03)

## Artifacts for Downstream Plans

- **Plan 02 (Border Xft rendering):** XftFontPtr, XftDrawPtr, XftColorWrap, make_xft_font_rotated in x11wrap.h; hasShapeExtension() in Manager.h
- **Plan 03 (Menu Xft rendering):** make_xft_font_name in x11wrap.h; XftDrawPtr, XftColorWrap for menu rendering
- **Key note for Plan 02:** Rotated font metrics are zero -- use XftTextExtentsUtf8 for all measurements, not font->ascent/descent

## Self-Check: PASSED

All created/modified files verified present. All 3 task commits verified in git log.
