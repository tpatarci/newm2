---
phase: 04-border-xft-font-rendering
plan: 02
subsystem: border-xft-font-rendering
tags: [xft, fontconfig, border, tab-label, utf8, shape-fallback, rotated-text]
requirements-completed: [VISL-01, VISL-03, VISL-05]

# Dependency graph
requires:
  - phase: 04-border-xft-font-rendering/01
    provides: [XftFontPtr, XftDrawPtr, XftColorWrap, make_xft_font_rotated, make_xft_font_name, hasShapeExtension]
provides:
  - Xft-based border tab label rendering with FcMatrix rotated fonts
  - UTF-8 window title display via XftDrawStringUtf8
  - Rectangular border fallback when Shape extension unavailable
  - Static XftColor allocation/deallocation for tab foreground/background
  - Per-instance XftDraw lifecycle management
affects: [04-03-menu-xft-rendering]

# Tech tracking
tech-stack:
  added: []
  patterns: [extent-based text measurement for rotated Xft fonts, XftDraw per-instance lifecycle, XftColor static allocation with refcount cleanup]

key-files:
  created: []
  modified: [include/Border.h, src/Border.cpp]

key-decisions:
  - "Rotated Xft font tab width calculated via XftTextExtentsUtf8 on sample glyph (M) rather than font->height which is zero"
  - "XftDraw x-offset for rotated text uses extents.height (the rotated glyph vertical extent) instead of font->ascent"
  - "Tab window background pixel uses m_xftBackground.pixel from XftColorAllocName"

patterns-established:
  - "Rotated font measurement: use XftTextExtentsUtf8 on any single character to get extents.height as the effective glyph vertical extent"
  - "Xft text positioning in drawLabel: x = 2 + extents.height (not 2 + font->ascent)"
  - "Shape extension gating: every shape method checks shapeAvailable() first and delegates to rectangular fallback"

# Metrics
duration: 7min
tasks: 2
files: 2
completed: "2026-05-07"
---

# Phase 4 Plan 02: Border Xft Rendering Summary

**Replaced xvertext bitmap rotation with Xft/fontconfig rendering for border tab labels, added UTF-8 support, and implemented rectangular border fallback for Shape-less environments**

## Performance

- **Duration:** 7 min
- **Started:** 2026-05-07T18:45:42Z
- **Completed:** 2026-05-07T18:52:42Z
- **Tasks:** 2
- **Files modified:** 2

## Accomplishments
- Border tab labels now render with antialiased Xft fonts using FcMatrix rotation
- UTF-8 window titles (including non-ASCII characters) display correctly via XftDrawStringUtf8
- Rectangular border fallback provides usable window frames when Shape extension is unavailable
- Zero xvertext (XRot*) calls remain in Border.cpp or Border.h

## Task Commits

Each task was committed atomically:

1. **Task 1: Rewrite Border.h and Border.cpp static members, constructor, font loading, drawLabel, fixTabHeight** - `c57f9c7` (feat)
2. **Task 2: Add rectangular border fallback when Shape extension unavailable** - `a5afc0d` (feat)

## Files Created/Modified
- `include/Border.h` - Replaced XRotFontStruct* with XftFont*, added XftDrawPtr m_tabDraw, XftColor static members, rectangular fallback declarations
- `src/Border.cpp` - Rewrote font loading, drawLabel(), fixTabHeight(), destructor; added allocateXftColors(), shapeAvailable(), rectangular fallback methods and shape method gating

## Decisions Made
- Used XftTextExtentsUtf8 on a single "M" character to measure rotated font height for m_tabWidth (rotated fonts have zero height in their struct fields)
- XftDraw x-offset for drawLabel uses extents.height from the label text rather than font->ascent (which is zero for rotated fonts)
- Tab window creation uses m_xftBackground.pixel as background color instead of a separate m_backgroundPixel member
- GC foreground/background pixel values set inline from allocateColour() results rather than stored in separate static members

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None - all changes compiled and passed tests on first attempt.

## Next Phase Readiness
- Border tab rendering is fully Xft-based, ready for Plan 03 (Menu Xft rendering) which completes the full Xft migration
- The rectangular fallback methods provide graceful degradation for VNC environments without Shape extension
- Key note for Plan 03: the same XftColorWrap and XftDrawPtr patterns established here apply to menu rendering

---
*Phase: 04-border-xft-font-rendering*
*Completed: 2026-05-07*

## Self-Check: PASSED

All created/modified files verified present. Both task commits verified in git log.
