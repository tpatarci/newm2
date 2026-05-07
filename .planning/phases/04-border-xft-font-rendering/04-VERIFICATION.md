---
phase: 04-border-xft-font-rendering
verified: 2026-05-07T20:30:00Z
status: human_needed
score: 4/4 must-haves verified (automated portion)
overrides_applied: 0
human_verification:
  - test: "Run wm2-born-again on an Xvfb display, open a terminal window, and visually inspect the window border tab"
    expected: "Tab shows sideways (rotated 90 degrees) text with antialiased rendering, shaped border with diagonal cutoff at bottom of tab, visually matching classic wm2 style"
    why_human: "Visual indistinguishability from original wm2 is a success criterion that cannot be verified programmatically"
  - test: "Set a window title containing non-ASCII characters (e.g., xterm -title 'Café 日本語') and inspect the tab label"
    expected: "Tab label renders accented characters and CJK glyphs correctly without garbling or missing glyphs"
    why_human: "Correct rendering of non-ASCII glyphs requires visual inspection; automated tests only verify no crash"
  - test: "Run wm2-born-again on a display without Shape extension (simulated by commenting out XShapeQueryExtension or using a minimal VNC server) and verify window borders"
    expected: "Windows show rectangular frames with a plain tab strip, no crashes or rendering artifacts"
    why_human: "Graceful degradation without artifacts is a visual criterion"
---

# Phase 4: Border + Xft Font Rendering Verification Report

**Phase Goal:** Window borders render with the classic wm2 sideways-tab look using Xft for antialiased UTF-8 text, with graceful fallback when the Shape extension is unavailable.
**Verified:** 2026-05-07T20:30:00Z
**Status:** human_needed
**Re-verification:** No -- initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Window frames display the classic wm2 sideways-tab visual identity with shaped borders | VERIFIED (code) / HUMAN (visual) | Border.cpp:393-446 shapeParent() creates shaped border with diagonal tab using XShapeCombineRectangles; Border.cpp:449-501 shapeTab() shapes the tab window; m_tabFont loaded via FcMatrix 90-degree rotation (x11wrap.h:296-324); drawLabel() (Border.cpp:233-262) renders rotated text. Visual indistinguishability requires human inspection. |
| 2 | Tab labels render with antialiased Xft fonts using fontconfig patterns, no core X fonts or XLFD strings anywhere | VERIFIED | grep -rn "XDrawString|XTextWidth|XLoadQueryFont" src/ returns zero matches. grep -rn "XRot" src/ include/ returns zero matches. Font patterns use fontconfig syntax: "Noto Sans,DejaVu Sans,Sans:bold:size=12" (Border.cpp:37), "Noto Sans,DejaVu Sans,Sans:size=12" (Manager.cpp:298). No XLFD patterns found. |
| 3 | Window titles containing non-ASCII characters display correctly in tab labels | VERIFIED (code) / HUMAN (visual) | drawLabel() uses XftDrawStringUtf8 (Border.cpp:257) with reinterpret_cast<const FcChar8*> -- handles UTF-8 natively. fixTabHeight() uses XftTextExtentsUtf8 for measurement (Border.cpp:300, 313, 323). PoC test renders accented and CJK text without crash (test_xft_poc.cpp:185-231). Correct glyph rendering requires visual confirmation. |
| 4 | On displays without Shape extension, windows fall back to rectangular frames without crashes or rendering artifacts | VERIFIED | shapeAvailable() gates 4 methods: shapeParent (Border.cpp:395), shapeTab (Border.cpp:451), resizeTab (Border.cpp:506), setFrameVisibility (Border.cpp:634). Rectangular fallbacks: shapeParentRectangular (Border.cpp:151-165), shapeTabRectangular (Border.cpp:168-186), setFrameVisibilityRectangular (Border.cpp:189-199). hasShapeExtension() in Manager.h:50 returns false when m_shapeEvent < 0. |

**Score:** 4/4 truths verified at code level; visual confirmation deferred to human testing.

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `include/x11wrap.h` | XftFontPtr, XftDrawPtr, XftColorWrap RAII wrappers | VERIFIED | Lines 277-406: XftFontDeleter, XftFontPtr, make_xft_font_name, make_xft_font_rotated (with FcMatrix rotation), XftDrawPtr, XftColorWrap. All in namespace x11. |
| `include/Border.h` | XftFont* m_tabFont, XftDrawPtr m_tabDraw, rectangular fallback declarations | VERIFIED | Lines 85-108: XftFont* m_tabFont, x11::XftDrawPtr m_tabDraw, XftColor statics, shapeAvailable(), shapeParentRectangular(), shapeTabRectangular(), setFrameVisibilityRectangular(). No XRotFontStruct. |
| `include/Manager.h` | hasShapeExtension(), XftFont* m_menuFont, XftDrawPtr m_menuDraw, XftColorWrap members | VERIFIED | Line 50: hasShapeExtension(). Lines 129-135: XftFont* m_menuFont, x11::XftDrawPtr m_menuDraw, x11::XftColorWrap m_menuFgColor/m_menuBgColor/m_menuHlColor. No GCPtr m_menuGC, no FontStructPtr. |
| `src/Border.cpp` | Xft-based tab label rendering, fontconfig pattern loading, rectangular fallback | VERIFIED | 1000 lines. Uses make_xft_font_rotated (line 36), XftDrawStringUtf8 (line 257), XftTextExtentsUtf8 (lines 56, 252, 300, 313, 323). shapeAvailable() gates at lines 395, 451, 506, 634. No XRot* calls. |
| `src/Buttons.cpp` | Xft-based menu text rendering and geometry display | VERIFIED | Uses XftTextExtentsUtf8 (lines 137, 273, 326), XftDrawStringUtf8 (lines 280, 285, 349), XftDrawRect (lines 241, 248, 256, 260, 262, 264, 266, 292, 346). No XDrawString, XTextWidth, XFillRectangle. |
| `src/Manager.cpp` | Xft font/color initialization, no Rotated.h include | VERIFIED | No Rotated.h include. Uses make_xft_font_name (line 297), XftColorWrap (lines 310-312), XftFontClose in release() (line 156). |
| `CMakeLists.txt` | Xft/fontconfig pkg-config, no Rotated.C | VERIFIED | pkg_check_modules(XFT) at line 13, pkg_check_modules(FONTCONFIG) at line 14. PkgConfig::XFT and PkgConfig::FONTCONFIG in target_link_libraries (lines 35-36). Rotated.C count: 0. |
| `tests/test_xft_poc.cpp` | PoC tests for Xft rendering in shaped windows | VERIFIED | 6 test cases: fontconfig pattern load, rotated font via FcMatrix, shaped window rendering, XftColor allocation, text measurement, UTF-8 rendering. All pass. |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| src/Border.cpp | include/x11wrap.h | #include x11wrap.h -> Xft RAII wrappers | WIRED | grep confirms x11::XftFontPtr, x11::XftDrawPtr usage |
| src/Border.cpp | libfontconfig | make_xft_font_rotated -> FcMatrix rotation | WIRED | Encapsulated in x11wrap.h:296-324, called at Border.cpp:36 |
| src/Buttons.cpp | include/x11wrap.h | XftDrawPtr, XftColorWrap | WIRED | m_menuDraw, m_menuFgColor, m_menuBgColor, m_menuHlColor used throughout |
| src/Manager.cpp | include/x11wrap.h | make_xft_font_name for menu font | WIRED | Manager.cpp:297 calls make_xft_font_name |
| CMakeLists.txt | libXft | pkg-config link target | WIRED | PkgConfig::XFT in target_link_libraries |
| src/Border.cpp | include/Manager.h | hasShapeExtension() | WIRED | Border.cpp:147 calls windowManager()->hasShapeExtension() |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|--------------------|--------|
| Border::drawLabel() | m_label (tab text) | m_client->label() from WM_NAME property | Yes -- XftDrawStringUtf8 renders actual UTF-8 text | FLOWING |
| Border::fixTabHeight() | m_tabHeight | XftTextExtentsUtf8 on m_label | Yes -- real font metrics determine tab height | FLOWING |
| Buttons::menu() | menu labels | menuLabelFn() -> client->label() or "New" | Yes -- real client labels rendered | FLOWING |
| Manager::initialiseScreen() | m_menuFont | make_xft_font_name with fontconfig | Yes -- real font loaded, non-null checked | FLOWING |
| Border constructor | m_tabFont | make_xft_font_rotated with FcMatrix | Yes -- real rotated font loaded, non-null checked | FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Build succeeds | cmake --build . 2>&1 | All targets built successfully | PASS |
| All tests pass | ctest --output-on-failure | 53/53 tests passed, 0 failed | PASS |
| No xvertext in src/ | grep -rn "XRot" src/ include/ | Zero matches | PASS |
| No core X fonts in src/ | grep -rn "XDrawString\|XTextWidth\|XLoadQueryFont" src/ | Zero matches | PASS |
| Rotated.C not in build | grep -c "Rotated.C" CMakeLists.txt | Returns 0 | PASS |
| Xft/fontconfig linked | grep PkgConfig::XFT/XFONTCONFIG CMakeLists.txt | Both present | PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| VISL-01 | 04-02 | Classic wm2 sideways-tab visual identity with shaped borders | SATISFIED (code) | Border.cpp shapeParent/shapeTab create shaped borders; FcMatrix rotation for sideways text |
| VISL-02 | 04-01, 04-03 | Xft + fontconfig replace core X fonts + xvertext for all text rendering | SATISFIED | Zero core X font calls in src/; zero xvertext calls; all text via XftDrawStringUtf8 |
| VISL-03 | 04-02 | Antialiased, UTF-8 tab labels (supports non-ASCII window names) | SATISFIED (code) | XftDrawStringUtf8 used for all text rendering; PoC test passes with accented and CJK text |
| VISL-04 | 04-01, 04-03 | Font specified by fontconfig pattern (not XLFD) | SATISFIED | Font patterns: "Noto Sans,DejaVu Sans,Sans:bold:size=12", "sans-serif:bold:size=12", "sans-serif:size=12". No XLFD patterns found. |
| VISL-05 | 04-02 | Shape extension used when available, rectangular fallback when not | SATISFIED | shapeAvailable() gates 4 methods; rectangular fallback implementations provided for parent, tab, and frame visibility |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| (none) | - | - | - | No anti-patterns detected |

No TODO/FIXME/placeholder comments found in phase-modified files. No empty return statements in rendering code. No hardcoded empty data flowing to user-visible output.

### Human Verification Required

### 1. Classic wm2 Visual Identity

**Test:** Run wm2-born-again on an Xvfb or physical display. Open a terminal window (xterm). Inspect the window border.
**Expected:** The border shows a sideways tab on the left side with rotated antialiased text. The tab has a diagonal cutoff at its bottom edge. The overall appearance is visually indistinguishable from classic wm2 at first glance.
**Why human:** Visual indistinguishability is a subjective criterion that cannot be verified programmatically. The code implements the correct geometry (shaped rectangles, diagonal, button cutout) and rotated font rendering, but pixel-exact matching requires human eyes.

### 2. Non-ASCII Character Rendering

**Test:** Launch `xterm -title "Cafe \xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"` and inspect the tab label.
**Expected:** The tab label renders "Cafe" with e-acute and Japanese characters correctly, without garbling, replacement characters, or missing glyphs.
**Why human:** While the code correctly uses XftDrawStringUtf8 and the PoC test confirms no crashes, correct glyph rendering depends on available system fonts and fontconfig fallback behavior which must be visually verified.

### 3. Shape Extension Fallback

**Test:** Run wm2-born-again on a display without the Shape extension (or mock it by forcing m_shapeEvent = -1). Open a window.
**Expected:** Windows display rectangular frames with a plain tab strip. No crashes occur. The tab text is still readable. No rendering artifacts (tearing, clipping errors) appear.
**Why human:** While the code correctly implements rectangular fallback paths, artifact-free rendering under fallback conditions is a visual criterion.

### Gaps Summary

No code-level gaps found. All 4 ROADMAP success criteria have full code-level implementation evidence. The phase goal is achieved from an implementation standpoint -- Xft font rendering is complete, xvertext is fully removed, UTF-8 support is wired through all text rendering paths, fontconfig patterns are used exclusively, and rectangular fallback is implemented.

Three items require human visual verification (visual identity match, non-ASCII rendering quality, fallback appearance) which by nature cannot be automated.

---

_Verified: 2026-05-07T20:30:00Z_
_Verifier: Claude (gsd-verifier)_
