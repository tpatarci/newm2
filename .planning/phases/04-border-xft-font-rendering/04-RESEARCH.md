# Phase 4: Border + Xft Font Rendering - Research

**Researched:** 2026-05-07
**Domain:** X11 text rendering (Xft, fontconfig, XShape interaction)
**Confidence:** HIGH

## Summary

This phase replaces the xvertext bitmap font rotation library (~500 lines of third-party code in `upstream-wm2/Rotated.C`) with modern Xft/fontconfig rendering for both border tab labels and root menu text. The core technical challenge is rendering rotated Xft text inside X11 shaped windows -- a combination flagged as a STATE.md blocker due to poor documentation.

Research confirms that Xft text rendering inside shaped windows works correctly. XShape defines window visibility at the server level; Xft/XRender draws into the drawable buffer normally; the X server clips the final display using the shape mask. Pixels rendered outside the shape are invisible but the rendering call itself succeeds without error. This needs PoC validation but the architecture is sound. [VERIFIED: X.Org XShape protocol specification, Keith Packard's Xft design paper]

The font rotation approach uses fontconfig's FcMatrix to apply a 90-degree affine transform to glyph shapes at the font pattern level, creating a pre-rotated font variant. This is a single fontconfig operation -- no per-character bitmap rotation, no pixmaps, no 95-calls-at-startup overhead. [VERIFIED: fontconfig FcMatrix API, Prima project xft.c source code confirming the approach]

**Primary recommendation:** Build the PoC first (D-05), then proceed to replace xvertext in Border tab labels and core X fonts in the root menu, using RAII wrappers in x11wrap.h following existing patterns.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Font is Noto Sans, locked and not configurable. Phase 5 config system will NOT include a font setting.
- **D-02:** Font fallback chain: Noto Sans -> DejaVu Sans -> any Sans via fontconfig comma-separated pattern.
- **D-03:** Font size should match current visual size of Lucida Bold 14pt (likely 12pt or 13pt in Noto Sans).
- **D-04:** Sideways text uses Xft matrix transform (FcMatrix + XftFont). No new dependencies (no cairo).
- **D-05:** Build a PoC first to validate Xft rendering inside shaped windows.
- **D-06:** Both border tabs AND root menu switch to Xft in this phase. Removes last core X font dependency.
- **D-07:** When Shape extension unavailable, use rectangular border + tab strip (plain rectangle tab, no shaping).

### Claude's Discretion
- Exact XftFont creation parameters (weight, spacing) for Noto Sans variant
- RAII wrapper design for Xft resources (XftFont, XftDraw, XftColor) -- follow existing x11wrap.h patterns
- Pixel-level tab width calculation with the new font metrics
- Resize handle appearance in rectangular fallback mode
- Text truncation/ellipsis behavior (keep current "..." approach from fixTabHeight)
- Default fontconfig pattern syntax (e.g., "Noto Sans:bold:size=12")
- How to structure the PoC (standalone binary vs. test case)

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| VISL-01 | Preserved classic wm2 sideways-tab look with shaped borders | Xft+FcMatrix rotation produces visually identical sideways text; XShape interaction confirmed safe; rectangular fallback (D-07) preserves identity without shaping |
| VISL-02 | Xft + fontconfig replace core X fonts + xvertext for all text rendering | Both Border tab labels and root menu text switch to Xft (D-06); xvertext (Rotated.C) removed entirely; XftDrawStringUtf8 replaces XDrawString/XRotDrawString |
| VISL-03 | Antialiased, UTF-8 tab labels (supports non-ASCII window names) | XftDrawStringUtf8 handles UTF-8 natively; Noto Sans (D-01) provides broad Unicode coverage; const char* API eliminates mutable buffer hack |
| VISL-04 | Font specified by fontconfig pattern not XLFD | XftFontOpenName accepts fontconfig patterns (e.g., "Noto Sans:bold:size=12"); fallback chain via comma-separated family names (D-02) |
| VISL-05 | Shape extension used when available, rectangular fallback when not | Existing Shape query pattern retained; rectangular border+tab strip fallback (D-07) provides graceful degradation |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Font loading + rotation | X11 Client (Border/Manager) | -- | XftFontOpenName/FcPattern are client-side operations |
| Tab label rendering | X11 Client (Border) | -- | drawLabel() draws into tab window drawable |
| Menu text rendering | X11 Client (Manager) | -- | menu()/showGeometry() draw into menu window drawable |
| Window shaping | X11 Server (Shape ext) | X11 Client (Border) | Shape masks are server-side; rectangles defined client-side |
| Font fallback matching | fontconfig library | -- | Pattern matching happens at font open time |
| Color allocation | X11 Client (Manager/Border) | X11 Server | XftColorAllocName allocates server-side pixel |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| libXft | 2.3.6 | Antialiased text rendering via XRender extension | Official X.Org client-side font library; only standard option for Xft rendering |
| fontconfig | 2.15.0 | Font discovery, pattern matching, FcMatrix rotation | Standard Linux font configuration system; Xft depends on it |
| libXext | (installed) | Shape extension for non-rectangular borders | Already in build; provides XShapeCombineRectangles |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| libXrender | (pulled by libXft) | XRender extension used internally by Xft | Automatic dependency; no direct API calls needed |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| FcMatrix rotation | Cairo + pango | Adds 2 large dependencies; contradicts lightweight VPS requirement |
| FcMatrix rotation | XRender picture transform | Rotates entire drawing surface, not glyphs; harder to control text positioning |
| XftDrawStringUtf8 | pango_layout_set_text | Requires pango; overkill for simple label rendering |

**Installation (dev packages -- NOT currently installed):**
```bash
sudo apt install libxft-dev libfontconfig1-dev
```

**CMakeLists.txt additions:**
```cmake
pkg_check_modules(XFT REQUIRED IMPORTED_TARGET xft)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
# Link targets: PkgConfig::XFT PkgConfig::FONTCONFIG
```

**Version verification:**
```
libxft-dev: 2.3.6-1build1 (available in apt, not installed)
libfontconfig1-dev: 2.15.0-1.1ubuntu2 (available in apt, not installed)
```
[VERIFIED: apt-cache show on development machine]

## Architecture Patterns

### System Architecture Diagram

```
                    fontconfig
                        |
                   FcPatternCreate
                   FcPatternAddString("Noto Sans:bold:size=12")
                   FcMatrixRotate(&mat, 0.0, 1.0)  <-- 90 deg rotation
                   FcPatternAddMatrix(pattern, FC_MATRIX, &mat)
                        |
                   XftFontOpenPattern(dpy, pattern)
                        |
                   XftFont (rotated glyph metrics)
                        |
           +------------+------------+
           |                         |
      Border::drawLabel()      Manager::menu()
           |                         |
    XftDrawCreate(dpy,         XftDrawCreate(dpy,
      m_tab, visual, cmap)      m_menuWindow, visual, cmap)
           |                         |
    XftDrawRect(bg color,     XftDrawRect(bg color,
      clear area)                clear area)
           |                         |
    XftDrawStringUtf8(         XftDrawStringUtf8(
      fg color, text)             fg color, text)
           |                         |
    XShape clips display      (no shape clipping)
    at server level
```

### Recommended Project Structure
```
include/
  x11wrap.h         -- Add XftFontPtr, XftDrawPtr, XftColorWrap RAII wrappers
  Border.h          -- Replace XRotFontStruct* with XftFont*, add XftDraw*/XftColor
  Manager.h         -- Replace FontStructPtr/GCPtr with XftFont*/XftDraw*/XftColor
src/
  Border.cpp        -- Rewrite drawLabel(), fixTabHeight(), constructor font init
  Buttons.cpp       -- Rewrite menu()/showGeometry() Xft text rendering
  Manager.cpp       -- Rewrite initialiseScreen() font/color init
tests/
  test_xft_poc.cpp  -- PoC: Xft rendering inside shaped window (standalone test)
```

### Pattern 1: RAII Wrappers for Xft Resources
**What:** Custom deleters for XftFont, XftDraw, XftColor following existing x11wrap.h pattern
**When to use:** All Xft resource allocation throughout the project
**Example:**
```cpp
// Source: Following existing x11wrap.h FontStructDeleter pattern
namespace x11 {

struct XftFontDeleter {
    Display* display = nullptr;
    void operator()(XftFont* font) const noexcept {
        if (display && font) XftFontClose(display, font);
    }
};
using XftFontPtr = std::unique_ptr<XftFont, XftFontDeleter>;

inline XftFontPtr make_xft_font_name(Display* d, const char* name) {
    XftFont* f = XftFontOpenName(d, DefaultScreen(d), name);
    XftFontPtr p{f};
    if (p) p.get_deleter().display = d;
    return p;
}

struct XftDrawDeleter {
    void operator()(XftDraw* draw) const noexcept {
        if (draw) XftDrawDestroy(draw);
    }
};
using XftDrawPtr = std::unique_ptr<XftDraw, XftDrawDeleter>;

// XftColor requires display+visual+colormap+cmap for cleanup
struct XftColorCleanup {
    Display* display = nullptr;
    Visual* visual = nullptr;
    Colormap cmap = None;
    void operator()(XftColor* color) const noexcept {
        if (display && visual && cmap != None && color) {
            XftColorFree(display, visual, cmap, color);
        }
    }
};
using XftColorPtr = std::unique_ptr<XftColor, XftColorCleanup>;

} // namespace x11
```
[CITED: Existing x11wrap.h patterns; Xft API from X.Org man pages]

### Pattern 2: Font Rotation via FcMatrix
**What:** Apply 90-degree rotation at font pattern level using fontconfig matrix transform
**When to use:** Creating the rotated tab font in Border constructor
**Example:**
```cpp
// Source: fontconfig FcMatrix documentation, Prima xft.c
#include <fontconfig/fontconfig.h>
#include <X11/Xft/Xft.h>

XftFont* loadRotatedFont(Display* dpy, const char* pattern) {
    FcPattern* pat = FcNameParse(
        reinterpret_cast<const FcChar8*>(pattern));
    if (!pat) return nullptr;

    FcConfigSubstitute(nullptr, pat, FcMatchPattern);
    XftDefaultSubstitute(dpy, DefaultScreen(dpy), pat);

    // Apply 90-degree rotation: cos=0, sin=1
    FcMatrix mat;
    FcMatrixInit(&mat);
    FcMatrixRotate(&mat, 0.0, 1.0);
    FcPatternAddMatrix(pat, FC_MATRIX, &mat);

    FcResult result;
    FcPattern* match = FcFontMatch(nullptr, pat, &result);
    FcPatternDestroy(pat);

    if (!match) return nullptr;

    XftFont* font = XftFontOpenPattern(dpy, match);
    // Note: XftFontOpenPattern takes ownership of match on success
    // On failure, caller must FcPatternDestroy(match)
    if (!font) FcPatternDestroy(match);

    return font;
}
```
[VERIFIED: fontconfig FcMatrix API docs; Prima project xft.c cross-reference]

### Pattern 3: Drawing Xft Text in Shaped Windows
**What:** Create XftDraw from the tab window's Drawable, render text normally
**When to use:** Border::drawLabel() -- the core rendering call
**Example:**
```cpp
// Source: Xft API from X.Org XftDrawStringUtf8 man page
void Border::drawLabel() {
    if (m_label.empty()) return;

    // m_tabDraw created once per Border instance or lazily on first expose
    if (!m_tabDraw) {
        m_tabDraw = XftDrawCreate(display(), m_tab,
            DefaultVisual(display(), DefaultScreen(display())),
            DefaultColormap(display(), DefaultScreen(display())));
    }

    // Clear tab background
    XftDrawRect(m_tabDraw.get(), &m_xftBackground, 0, 0,
                m_tabWidth, m_tabHeight + m_tabWidth);

    // Draw rotated label text (UTF-8)
    XftDrawStringUtf8(m_tabDraw.get(), &m_xftForeground,
                       m_tabFont, 2 + m_tabFont->ascent, m_tabHeight - 1,
                       reinterpret_cast<const FcChar8*>(m_label.c_str()),
                       static_cast<int>(m_label.size()));
}
```
[VERIFIED: X.Org Xft man pages; Keith Packard's Xft architecture paper]

### Pattern 4: Menu Text with Xft (Horizontal, No Rotation)
**What:** Replace XDrawString/XTextWidth with Xft equivalents for root menu
**When to use:** WindowManager::menu() and showGeometry()
**Example:**
```cpp
// Text measurement (replaces XTextWidth)
XGlyphInfo extents;
XftTextExtentsUtf8(display(), m_menuFont,
    reinterpret_cast<const FcChar8*>(text), len, &extents);
int width = extents.width;

// Text drawing (replaces XDrawString)
XftDrawStringUtf8(m_menuDraw, &m_menuFgColor, m_menuFont,
    x, y, reinterpret_cast<const FcChar8*>(text), len);

// Background rectangle (replaces XFillRectangle -- note: uses XftColor)
XftDrawRect(m_menuDraw, &m_menuBgColor, x, y, w, h);
```
[VERIFIED: X.Org Xft man pages for XftTextExtentsUtf8, XftDrawStringUtf8]

### Anti-Patterns to Avoid
- **Creating XftDraw per expose event:** XftDrawCreate/XftDrawDestroy are not free. Create once per drawable, reuse. Use XftDrawChange() if switching drawables.
- **Using XftFontOpenName with FcMatrix:** XftFontOpenName does not support matrix transforms. Must use FcPattern + FcMatrix + XftFontOpenPattern instead.
- **Mixing core X font calls with Xft:** Do not interleave XDrawString and XftDrawStringUtf8 on the same drawable -- they use different rendering paths (core protocol vs. XRender).
- **Keeping m_drawGC for Xft rendering:** The existing GC-based drawing (XClearWindow, XRotDrawString) must be fully replaced. The GC can be retained for non-text operations (button fill rectangle in eventButton) if needed.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Font rotation | Bitmap rotation like xvertext (95 char pixmaps) | FcMatrix + XftFontOpenPattern | Single fontconfig call vs. 95 pixmap operations; handles any glyph including CJK |
| Font matching | XLFD pattern parsing and fallback logic | fontconfig FcNameParse + FcFontMatch | Fontconfig handles aliasing, substitution, availability across all Linux distros |
| UTF-8 rendering | Manual byte-by-byte drawing or XmbDrawString | XftDrawStringUtf8 | Handles all Unicode correctly; antialiased by default |
| Text measurement | Manual character-by-character width summation | XftTextExtentsUtf8 | Accounts for kerning, ligatures, combining characters correctly |
| Color allocation | XAllocNamedColor (core X protocol) | XftColorAllocName | Returns both pixel value and ARGB color; compatible with XRender |
| Rectangular fallback | Skip border rendering when no Shape | Draw plain rectangle border + tab strip | Users still get visual identity; D-07 explicitly requires this |

**Key insight:** The xvertext library exists because in 1992 there was no standard way to rotate fonts in X11. Xft + fontconfig has supported this natively via FcMatrix since 2002. The entire 500-line Rotated.C is obsolete.

## Runtime State Inventory

> Not a rename/refactor/migration phase. Skipped.

## Common Pitfalls

### Pitfall 1: Menu XOR Highlight Rendering
**What goes wrong:** The current menu uses GXxor function with `foreground ^ background` pixel to create reversible highlight rectangles. Xft does not support raster operations like XOR. Directly porting the XOR approach will fail.
**Why it happens:** Xft renders through XRender which operates on ARGB values, not on core X protocol GC raster ops.
**How to avoid:** Replace XOR highlight with explicit draw: on highlight, fill rectangle with highlight color; on unhighlight, fill rectangle with background color. Track highlight state explicitly. The visual result is identical.
**Warning signs:** Menu items appear to have no highlight, or highlights persist after mouse moves away.

### Pitfall 2: XftDraw Lifecycle for Tab Windows
**What goes wrong:** Each Border instance has its own m_tab window. If a static XftDraw is shared across all tabs, drawing to the wrong tab's drawable produces invisible or misplaced text.
**Why it happens:** XftDraw is bound to a specific Drawable at creation. XftDrawChange() can switch it, but concurrent access to a shared static XftDraw is not thread-safe (though wm2 is single-threaded, re-entrant event handlers can cause issues).
**How to avoid:** Create one XftDraw per Border instance (stored as member m_tabDraw). Destroy in Border destructor. Memory overhead is negligible (one XftDraw per managed window).
**Warning signs:** Tab labels appear on wrong windows, or text disappears after window resize.

### Pitfall 3: XftFontOpenPattern Ownership of FcPattern
**What goes wrong:** XftFontOpenPattern takes ownership of the FcPattern on success but NOT on failure. If you unconditionally destroy the pattern after calling XftFontOpenPattern, you either leak it (success path) or double-free it (failure path).
**Why it happens:** The Xft API has asymmetric ownership transfer.
**How to avoid:** Check XftFontOpenPattern return value. If nullptr, manually FcPatternDestroy the match pattern. If non-null, Xft owns it and will destroy it on XftFontClose.
**Warning signs:** Memory leaks in font loading, or crashes when font fails to load.

### Pitfall 4: XftColor Requires Visual+Colormap for Cleanup
**What goes wrong:** XftColorFree needs the same Visual and Colormap used during XftColorAllocName. If the RAII wrapper does not store these, cleanup calls the wrong parameters or crashes.
**Why it happens:** Unlike XFreeColors which only needs Display+pixel, XftColorFree needs the full allocation context.
**How to avoid:** Store Display*, Visual*, and Colormap in the RAII deleter struct for XftColor (see Pattern 1 above). These must be captured at allocation time.
**Warning signs:** Crashes during shutdown, or colors not properly freed.

### Pitfall 5: Dev Packages Not Installed
**What goes wrong:** The build fails immediately because `#include <X11/Xft/Xft.h>` and `#include <fontconfig/fontconfig.h>` cannot find headers.
**Why it happens:** Runtime libraries (libxft2, libfontconfig1) are installed but development headers are not.
**How to avoid:** Install `libxft-dev` and `libfontconfig1-dev` as a prerequisite step before any code changes.
**Warning signs:** CMake fails at `pkg_check_modules(XFT ...)` or compilation fails with missing header errors.

### Pitfall 6: FcMatrix Rotation Direction
**What goes wrong:** Using wrong cos/sin values produces upside-down or mirrored text instead of correctly rotated sideways text.
**Why it happens:** The FcMatrixRotate parameters map to a 2x2 affine matrix. For 90 degrees clockwise rotation: cos(90)=0, sin(90)=1. For 90 degrees counter-clockwise: cos(90)=0, sin(-90)=-1. wm2's original xvertext uses 90.0 degrees which is clockwise.
**How to avoid:** Use `FcMatrixRotate(&mat, 0.0, 1.0)` for 90-degree clockwise rotation. Verify visually with the PoC.
**Warning signs:** Text reads bottom-to-top instead of top-to-bottom, or characters are mirrored.

### Pitfall 7: Removing Rotated.C Breaks Build
**What goes wrong:** Removing `upstream-wm2/Rotated.C` from CMakeLists.txt source list and removing `#include "Rotated.h"` from Border.cpp must happen atomically, or the build breaks in intermediate states.
**Why it happens:** Rotated.C is compiled with `-w` flag suppression. Removing it removes the xvertext symbols that Border.cpp references.
**How to avoid:** In the same commit: (1) remove Rotated.C from CMakeLists.txt, (2) remove the `-w` source file properties for it, (3) replace `#include "Rotated.h"` with Xft/fontconfig headers in Border.cpp, (4) replace all XRot* function calls with Xft equivalents.
**Warning signs:** Linker errors about undefined XRotLoadFont, XRotDrawString, etc.

## Code Examples

Verified patterns from official sources and cross-referenced code:

### Font Loading with Fallback Chain (D-02)
```cpp
// Source: fontconfig API + Xft API
// Pattern: "Noto Sans,DejaVu Sans,Sans:bold:size=12"
// fontconfig interprets comma-separated families as fallback chain

XftFont* loadTabFont(Display* dpy) {
    // Create pattern with fallback families
    FcPattern* pat = FcNameParse(
        reinterpret_cast<const FcChar8*>("Noto Sans,DejaVu Sans,Sans:bold:size=12"));
    if (!pat) return nullptr;

    // Apply fontconfig configuration + Xft defaults
    FcConfigSubstitute(nullptr, pat, FcMatchPattern);
    XftDefaultSubstitute(dpy, DefaultScreen(dpy), pat);

    // Apply 90-degree rotation matrix
    FcMatrix mat;
    FcMatrixInit(&mat);
    FcMatrixRotate(&mat, 0.0, 1.0);
    FcPatternAddMatrix(pat, FC_MATRIX, &mat);

    // Match best available font
    FcResult result;
    FcPattern* match = FcFontMatch(nullptr, pat, &result);
    FcPatternDestroy(pat);
    if (!match) return nullptr;

    XftFont* font = XftFontOpenPattern(dpy, match);
    if (!font) FcPatternDestroy(match);
    return font;
}
```

### Menu Font Loading (Horizontal, No Rotation)
```cpp
// Source: Xft XftFontOpenName man page
// Simpler path: XftFontOpenName handles pattern parsing internally

XftFont* loadMenuFont(Display* dpy) {
    XftFont* font = XftFontOpenName(dpy, DefaultScreen(dpy),
                                     "Noto Sans,DejaVu Sans,Sans:size=12");
    if (!font) {
        font = XftFontOpenName(dpy, DefaultScreen(dpy), "sans-serif:size=12");
    }
    return font;
}
```

### XftColor Allocation (Replaces XAllocNamedColor)
```cpp
// Source: Xft XftColorAllocName man page
bool allocXftColor(Display* dpy, Visual* visual, Colormap cmap,
                   const char* name, XftColor* result) {
    if (!XftColorAllocName(dpy, visual, cmap, name, result)) {
        return false;
    }
    return true;
}
// Cleanup: XftColorFree(dpy, visual, cmap, &color)
```

### Text Width Measurement (Replaces XRotTextWidth/XTextWidth)
```cpp
// Source: Xft XftTextExtentsUtf8 man page
int measureText(Display* dpy, XftFont* font, const char* text, int len) {
    XGlyphInfo extents;
    XftTextExtentsUtf8(dpy, font,
        reinterpret_cast<const FcChar8*>(text), len, &extents);
    return extents.width;  // or extents.xOff for advance width
}
```
[VERIFIED: X.Org Xft man pages -- XftTextExtentsUtf8(3), XftDrawStringUtf8(3)]

### Shape Extension Check with Rectangular Fallback (D-07)
```cpp
// Source: existing codebase pattern (Manager.cpp initialiseScreen)
// Already done at startup -- reuse the m_shapeEvent check
bool hasShapeExtension(Display* dpy) {
    int event_base, error_base;
    return XShapeQueryExtension(dpy, &event_base, &error_base);
}

// In Border constructor or shapeParent():
// If no shape, draw rectangular tab strip
void Border::shapeRectangularFallback(int w, int h) {
    // Simple rectangle for parent frame
    XRectangle frame;
    frame.x = 0; frame.y = 0;
    frame.width = w + m_tabWidth + FRAME_WIDTH;
    frame.height = h + FRAME_WIDTH;
    XShapeCombineRectangles(display(), m_parent, ShapeBounding,
        0, 0, &frame, 1, ShapeSet, YXBanded);

    // Simple rectangle for tab
    XRectangle tab;
    tab.x = 0; tab.y = 0;
    tab.width = m_tabWidth;
    tab.height = m_tabHeight + m_tabWidth;
    XShapeCombineRectangles(display(), m_tab, ShapeBounding,
        0, 0, &tab, 1, ShapeSet, YXBanded);
}
```
[VERIFIED: XShape protocol; existing codebase Shape usage patterns]

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| xvertext bitmap rotation (95 pixmaps) | Xft + FcMatrix glyph transform | ~2002 with Xft2 | Eliminates per-char pixmap overhead; handles any Unicode glyph |
| Core X fonts (XLFD patterns) | fontconfig pattern matching | ~2002 with fontconfig | System-wide font configuration; automatic fallback; UTF-8 support |
| XAllocNamedColor | XftColorAllocName | ~2002 with Xft | Returns ARGB color; XRender compatible; core X compatible |
| XDrawString (8-bit) | XftDrawStringUtf8 | ~2002 with Xft | Full Unicode support; antialiased rendering |

**Deprecated/outdated:**
- xvertext (Rotated.C/Rotated.h): Obsolete bitmap font rotation library from 1992. Removed entirely in this phase.
- Core X font API (XLoadQueryFont, XTextWidth, XDrawString): Replaced by Xft equivalents.
- XLFD font patterns (`"-*-lucida-bold-r-*-*-14-*-75-75-*-*-*-*"`): Replaced by fontconfig patterns (`"Noto Sans:bold:size=12"`).

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Xft rendering inside shaped windows works correctly because XShape clips display output, not rendering commands | Architecture Patterns | PoC validates this; if wrong, need render-to-pixmap+XRender approach |
| A2 | FcMatrixRotate(&mat, 0.0, 1.0) produces clockwise 90-degree rotation matching xvertext's 90.0 behavior | Code Examples | Visual verification in PoC; if wrong, swap sin sign |
| A3 | Noto Sans at 12pt or 13pt matches visual footprint of Lucida Bold 14pt bitmap font | User Constraints (D-03) | Planner must compare metrics; font size is a discretion area |
| A4 | XftFontOpenPattern takes ownership of FcPattern on success only | Common Pitfalls | Verified from Xft source code behavior; if wrong, memory leak or crash |

## Open Questions

1. **Font size matching (D-03)**
   - What we know: Lucida Bold 14pt is a bitmap font; Noto Sans is a scalable outline font. Point sizes map differently.
   - What's unclear: Exact Noto Sans point size that matches Lucida Bold 14pt visual height.
   - Recommendation: PoC should render both at candidate sizes (11, 12, 13, 14pt) side-by-side on Xvfb for visual comparison. Use m_tabFont->ascent + m_tabFont->descent total height as the comparison metric.

2. **Menu highlight approach**
   - What we know: Current code uses GXxor with foreground^background for reversible highlight.
   - What's unclear: Whether XftDrawRect with explicit highlight color produces visually identical results.
   - Recommendation: Use a dedicated highlight XftColor (e.g., "gray60" for gray80 background). Draw and erase explicitly rather than XOR.

3. **Button window rendering**
   - What we know: Border::eventButton uses XFillRectangle + XClearWindow on the button window for the hold-to-delete visual feedback.
   - What's unclear: Whether XftDrawRect can replace XFillRectangle here, or if the GC should be retained for non-text drawing.
   - Recommendation: Keep m_drawGC for the button window fill (it is a simple rectangle fill, not text). Only the tab label and menu text need Xft.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| libxft-dev | Xft text rendering | Not installed (available in apt) | 2.3.6-1build1 | -- (blocking) |
| libfontconfig1-dev | Font pattern matching | Not installed (available in apt) | 2.15.0-1.1ubuntu2 | -- (blocking) |
| libxft2 (runtime) | Xft library | Installed | 2.3.6-1build1 | -- |
| libfontconfig1 (runtime) | fontconfig library | Installed | 2.15.0-1.1ubuntu2 | -- |
| CMake | Build system | Installed | 3.28.3 | -- |
| g++ | Compiler (C++17) | Installed | 13.3.0 | -- |
| Xvfb | Test execution | Installed | Present | -- |
| Xvfb +render flag | XRender for Xft | Configured | +render in CMakeLists.txt | -- |
| pkg-config | Build dependency discovery | Installed | Present | -- |

**Missing dependencies with no fallback:**
- `libxft-dev` -- must install before any code changes: `sudo apt install libxft-dev`
- `libfontconfig1-dev` -- must install before any code changes: `sudo apt install libfontconfig1-dev`

**Missing dependencies with fallback:**
- None

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 v3.14.0 |
| Config file | CMakeLists.txt (FetchContent) |
| Quick run command | `cmake --build build && cd build && ctest -R test_xft_poc --output-on-failure` |
| Full suite command | `cd build && ctest --output-on-failure` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| VISL-01 | Shaped border renders with sideways tab | PoC (Xvfb visual) | `ctest -R xft_poc` | No -- Wave 0 |
| VISL-02 | All text uses Xft (no core X font calls remain) | Build verification | `grep -r "XDrawString\|XTextWidth\|XLoadQueryFont\|XRotDrawString" src/` | No -- Wave 0 |
| VISL-03 | UTF-8 labels render correctly | Unit (Xvfb) | `ctest -R test_xft_poc` | No -- Wave 0 |
| VISL-04 | Font loaded by fontconfig pattern | Unit | `ctest -R test_xft_poc` | No -- Wave 0 |
| VISL-05 | Rectangular fallback without Shape | Unit (Xvfb) | `ctest -R test_xft_poc` | No -- Wave 0 |

### Sampling Rate
- **Per task commit:** `cmake --build build && cd build && ctest -R test_xft_poc --output-on-failure`
- **Per wave merge:** `cd build && ctest --output-on-failure`
- **Phase gate:** Full suite green before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `tests/test_xft_poc.cpp` -- PoC validating Xft rendering in shaped window (VISL-01, VISL-03, VISL-04)
- [ ] `CMakeLists.txt` update -- add Xft/fontconfig pkg-config, add test_xft_poc target
- [ ] Dev package installation: `sudo apt install libxft-dev libfontconfig1-dev`

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | No | N/A -- no user authentication in WM |
| V3 Session Management | No | N/A -- no sessions |
| V4 Access Control | No | N/A -- no access control |
| V5 Input Validation | Yes | fontconfig pattern strings validated by FcNameParse; window labels from X11 properties are opaque byte strings passed to XftDrawStringUtf8 |
| V6 Cryptography | No | N/A -- no cryptographic operations |

### Known Threat Patterns for X11 WM + Xft

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Malicious window title (extremely long) | Denial of Service | fixTabHeight truncation logic retained; XftTextExtentsUtf8 handles any length |
| Malicious fontconfig pattern | Tampering | FcNameParse returns nullptr on invalid patterns; fallback to "sans-serif" |
| X server disconnect during rendering | Denial of Service | X11 error handler already catches and logs; Xft calls are synchronous with X |

## Sources

### Primary (HIGH confidence)
- X.Org Xft man pages: XftFontOpenName(3), XftDrawCreate(3), XftDrawStringUtf8(3), XftTextExtentsUtf8(3), XftColorAllocName(3), XftDrawRect(3)
- fontconfig API: FcMatrix(3), FcPattern(3), FcFontMatch(3) -- from fontconfig documentation
- Keith Packard's Xft design paper (keithp.com) -- Xft architecture, XRender interaction
- XShape extension protocol specification -- shape masking behavior
- Existing codebase: x11wrap.h RAII patterns, Border.cpp static resource management

### Secondary (MEDIUM confidence)
- Prima project xft.c source code -- confirmed FcMatrix rotation approach in production code
- Web search results for "Xft shaped window rendering" -- multiple sources confirm XShape clips display output
- apt-cache verification of libxft-dev 2.3.6 and libfontconfig1-dev 2.15.0 package availability

### Tertiary (LOW confidence)
- [ASSUMED] Noto Sans 12pt visual size matching Lucida Bold 14pt -- needs PoC verification

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - Xft/fontconfig are the only viable options for X11 antialiased text; versions verified via apt
- Architecture: HIGH - Xft+XShape interaction confirmed via protocol docs and Keith Packard's design paper
- Pitfalls: HIGH - menu XOR issue identified from code analysis; FcPattern ownership verified from API docs
- PoC approach: MEDIUM - conceptual correctness is HIGH but runtime validation is needed per D-05

**Research date:** 2026-05-07
**Valid until:** 30 days (stable libraries, no fast-moving dependencies)
