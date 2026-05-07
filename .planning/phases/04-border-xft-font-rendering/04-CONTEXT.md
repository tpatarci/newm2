# Phase 4: Border + Xft Font Rendering - Context

**Gathered:** 2026-05-07
**Status:** Ready for planning

<domain>
## Phase Boundary

Replace the xvertext bitmap font rotation library with modern Xft/fontconfig rendering for window tab labels, preserving wm2's classic sideways-tab visual identity. Also replace core X fonts in the root menu with Xft. Add Shape extension fallback for VNC compatibility.

Requirements: VISL-01 through VISL-05.

**In scope:**
- Xft + fontconfig replacing xvertext for rotated tab label rendering
- Xft + fontconfig replacing core X fonts for root menu text
- UTF-8 tab label support (non-ASCII window names)
- Font specified by fontconfig pattern (not XLFD)
- Shape extension used when available, rectangular fallback when not
- PoC validation of Xft rendering inside shaped windows before full planning
- Remove xvertext (Rotated.C/Rotated.h) dependency entirely

**Out of scope:**
- Runtime font configuration (locked font, not configurable)
- Xrandr support (Phase 8)
- EWMH compliance (Phase 6)
- Keyboard shortcuts (v2)

</domain>

<decisions>
## Implementation Decisions

### Font Choice
- **D-01:** Font is **Noto Sans**, locked and not configurable. Rationale: best Unicode coverage across Latin, Arabic, CJK, Cyrillic, and other scripts. Phase 5 config system will NOT include a font setting.
- **D-02:** Font fallback chain: **Noto Sans → DejaVu Sans → any Sans** via fontconfig. Tries Noto first, falls back to DejaVu (pre-installed on Ubuntu), then any system Sans. Graceful degradation — always works.
- **D-03:** Font size should **match the current visual size** of Lucida Bold 14pt. The researcher/planner should compare Noto Sans point sizes against the current bitmap Lucida's visual footprint and pick the closest match (likely 12pt or 13pt in Noto Sans).

### Text Rendering Approach
- **D-04:** Sideways text uses **Xft matrix transform** (FcMatrix + XftFont to create a rotated font variant). Pure Xft — no new dependencies (no cairo). Stays lightweight for VPS.
- **D-05:** **Build a PoC first** to validate that Xft text renders correctly inside shaped X11 windows. This is flagged as poorly documented (STATE.md blocker). PoC scope: create a shaped window, render rotated Xft text inside it, verify correctness. If the PoC reveals issues, pivot to render-to-pixmap+XRender approach before full planning.

### Menu Font Scope
- **D-06:** **Both border tabs AND root menu** switch to Xft in this phase. Fulfills VISL-02 ("all text rendering") literally. Removes the last core X font dependency entirely. Menu uses horizontal Xft text (no rotation needed) — simpler than tab rendering.

### Shape Extension Fallback
- **D-07:** When Shape extension is unavailable, use **rectangular border + tab strip**. The tab is a plain rectangle (no shaping) on the left side with the label. Button and resize handle remain. Closest to the original visual identity.

### Claude's Discretion
- Exact XftFont creation parameters (weight, spacing) for Noto Sans variant
- RAII wrapper design for Xft resources (XftFont, XftDraw, XftColor) — follow existing x11wrap.h patterns
- Pixel-level tab width calculation with the new font metrics
- Resize handle appearance in rectangular fallback mode
- Text truncation/ellipsis behavior (keep current "..." approach from fixTabHeight)
- Default fontconfig pattern syntax (e.g., "Noto Sans:bold:size=12")
- How to structure the PoC (standalone binary vs. test case)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Project Context
- `.planning/PROJECT.md` — Project definition, core value, constraints, key decisions
- `.planning/REQUIREMENTS.md` — VISL-01 through VISL-05 requirements with traceability
- `.planning/ROADMAP.md` — Phase 4 goal, success criteria, and requirements mapping

### Prior Phase Decisions
- `.planning/phases/01-build-infrastructure-raii-foundation/01-CONTEXT.md` — Phase 1 decisions (RAII wrappers via unique_ptr+custom deleters in x11wrap.h, C++17 idioms)
- `.planning/phases/03-client-lifecycle-with-raii/03-CONTEXT.md` — Phase 3 decisions (unique_ptr ownership, ServerGrab RAII, ClientState enum, hash map lookup)

### Codebase Analysis
- `.planning/codebase/CONCERNS.md` — Known issues: Xft rendering in shaped windows (poorly documented), rotated font bitmap creation performance, border shape management fragility

### Source Files (Primary Targets)
- `src/Border.cpp` — Tab label rendering, shape management, frame construction (715 lines)
- `include/Border.h` — Border class with static font/GC resources, shape methods
- `src/Manager.cpp` — Root menu rendering, color allocation, font loading
- `include/Manager.h` — WindowManager with menu font/GC members (lines 127-132)
- `include/x11wrap.h` — RAII wrapper types; new Xft wrappers to be added here
- `upstream-wm2/Rotated.C` — Current xvertext implementation (to be removed)
- `upstream-wm2/Rotated.h` — Current xvertext API (to be removed)
- `CMakeLists.txt` — Build config; needs xft and fontconfig pkg-config entries

### Upstream Reference
- `upstream-wm2/Border.C` — Original border rendering for behavioral reference
- `upstream-wm2/Config.h` — Original font/color constants for visual comparison

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `include/x11wrap.h` — Existing RAII wrappers (DisplayPtr, UniqueCursor, GCPtr, FontStructPtr). XftFont, XftDraw, XftColor wrappers follow the same custom-deleter pattern.
- `include/Manager.h:15-28` — `FdGuard` RAII pattern for non-X11 resources — demonstrates the constructor/destructor pattern for new RAII types.
- `src/Border.cpp:22-68` — Static resource initialization in Border constructor (font, colors, GC). Same pattern for Xft resources.
- `src/Manager.cpp:311-322` — `allocateColour()` method for named color allocation. XftColor will need similar but with XftColorAllocName instead of XAllocNamedColor.

### Established Patterns
- `m_` prefix for member variables — retained
- RAII for all resources — Xft types follow this
- Static resources shared across Border instances (m_tabFont, m_drawGC, colors) — same pattern for Xft resources
- Reference counting for static resources via m_borderCount — retained
- `std::vector<char>` buffer for mutable string passing — no longer needed with Xft's UTF-8 API (XftDrawStringUtf8 takes const char*)

### Integration Points
- `src/Border.cpp:127-138` — `drawLabel()` is the primary rewrite target. Currently calls XClearWindow + XRotDrawString. Replaces with XftDrawRect (clear) + XftDrawStringUtf8.
- `src/Border.cpp:168-206` — `fixTabHeight()` uses XRotTextWidth for label measurement. Replaces with XftTextExtents32.
- `src/Border.cpp:33-36` — Font loading in constructor (XRotLoadFont with XLFD pattern). Replaces with XftFontOpenName with fontconfig pattern.
- `src/Manager.cpp:278-280` — Menu font/GC initialization. Switches to XftFont + XftDraw.
- `include/Manager.h:127-132` — Menu resource members (m_menuGC, m_menuFont, etc.). Replaces with Xft equivalents.
- `CMakeLists.txt:11-12` — pkg-config entries. Adds `xft` and `fontconfig`.

### Key Technical Notes
- Xft rendering inside shaped windows requires creating an XftDraw from the tab window's Drawable. The tab window has both ShapeBounding and ShapeClip masks. Xft draws through XRender which should respect the shape mask, but this is the poorly-documented part that needs PoC validation.
- The FcMatrix approach creates a new font pattern with a transformation matrix, then opens it. This pre-rotates all glyphs. Alternative (XRender picture transform) rotates the entire drawing surface.
- The `Rotated.C` file (~500 lines) is third-party code that gets completely removed. The `#include "Rotated.h"` in Border.cpp is replaced with Xft headers.

</code_context>

<specifics>
## Specific Ideas

No specific visual references — the upstream wm2 behavior is the target. The classic sideways-tab look must be visually indistinguishable from the original at first glance.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope.

</deferred>

---

*Phase: 4-Border + Xft Font Rendering*
*Context gathered: 2026-05-07*
