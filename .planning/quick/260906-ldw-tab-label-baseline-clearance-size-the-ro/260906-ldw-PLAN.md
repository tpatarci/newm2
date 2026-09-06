---
quick_id: 260906-ldw
type: quick
description: "Tab label baseline clearance: size the rotated tab from a sample with ascender and descender, fix the baseline per font so descenders end 5 px short of the frame edge"
files_modified:
  - src/Border.cpp
  - include/Border.h
  - tests/test_wm_runtime.cpp
autonomous: true
requirements: []
estimate:
  tasks: 2
  confidence: medium
must_haves:
  truths:
    - "With the default tab font, a title containing g, j, p, q, y and the stem of t is drawn with every foreground pixel at least 5 px away from the frame edge on the baseline side of the tab, and 2 px away from the tab's outer edge on the ascender side."
    - "The baseline position does not depend on the title: two frames whose titles differ only in whether they contain descenders draw their ascender tops on the same column."
    - "The button square and the client inset still move together with the tab thickness (the existing difference-based cases stay green), and the frame's shape, bevel and diagonal are unchanged."
    - "A pixel-level case fails when the baseline change is reverted."
  artifacts:
    - src/Border.cpp
    - tests/test_wm_runtime.cpp
    - .planning/quick/260906-ldw-tab-label-baseline-clearance-size-the-ro/evidence/tab-before.png
    - .planning/quick/260906-ldw-tab-label-baseline-clearance-size-the-ro/evidence/tab-after.png
---

<objective>
The sideways tab label reads bottom to top, so the BASELINE side of every glyph faces the frame. Today the tab strip is sized from a capital "M" (no descender) plus 4 px, and the rotated label is placed at `x = 2 + extents.width` of the LABEL ITSELF, so (a) any glyph with a descender (g, j, p, q, y, the foot of t) runs past the strip into the black frame line, and (b) the baseline shifts from title to title. The operator measured it on the 09-06 screenshot and asked for about 5 px of clear tab between the letter bottoms and the frame.

Make the strip thick enough for a full ascender-plus-descender glyph box, and fix the baseline once per font so every title draws its ascender tops 2 px inside the outer edge and its deepest descender 5 px short of the frame edge.
</objective>

<execution_context>
Read before editing:
- src/Border.cpp lines 268-325 (loadTabFont, the rotated "M" sample), 590-612 (the second copy of the same sizing, for live font reload), 850-912 (drawLabel, rotated path), 1019-1060 (fixTabHeight, along-string extent -- NOT to change)
- include/Border.h lines 100-125 (xIndent, buttonDrawInset/Size, m_tabWidth static), the `m_tabWidth` declaration near line 213
- tests/test_wm_runtime.cpp around lines 1250-1340 (the two difference-based tab cases) and around 2638 (recorded extents table)
- tests/test_wm_fallbacks.cpp around 520-540 (prints "M"/"Hello" extents; it asserts usability, not values -- confirm it does not pin the sample)
- tests/support/WmFixture.h (how tests find the frame, tab and button sub-windows)
- docs/RELEASE-NOTES.md and its doc-parity guard in the test suite (grep for RELEASE-NOTES in tests/) before touching docs; this change is visual and should need no notes text unless the guard says otherwise
</execution_context>

<context>
Geometry, to be CONFIRMED by measurement, not assumed: for the 90-degree rotated Xft face, XGlyphInfo.width is the across-strip thickness of the ink and XGlyphInfo.x is the offset from the origin to the ink's left edge, which for the bottom-to-top reading direction is the ASCENDER side. Print extents for "M", "g", "Mg" and "gjpqy" on the loaded rotated face once (a scratch printf, or the spike test) and check: "Mg".width should exceed "M".width by the descender depth, and "M".x should be about equal to "M".width (no descender) while "g".x is smaller than "g".width. If the sign is the other way round, mirror the formulas below so the 2 px side is still the ascender side and the 5 px side is still the frame side.

Formulas (rotated branch, both sizing sites):
- sample = "Mg" (ASCII, one ascender, one descender; measured with XftTextExtentsUtf8, length 2)
- m_tabWidth = extents.width + 2 + 5, then the existing minimum `TAB_TOP_HEIGHT * 2 + 8` still applies
- new static `m_tabBaseline = 2 + extents.x` (or the mirrored equivalent), computed alongside m_tabWidth in both sizing sites; the unrotated branch leaves it unused
- drawLabel rotated path: draw at `x = m_tabBaseline` instead of `2 + extents.width` of the label; the y argument (`m_tabHeight - 1`) and everything else stay byte-for-byte
Do not change fixTabHeight (along-string length), the shape rectangles, the bevel, the diagonal, or the horizontal fallback path.
</context>

<tasks>

<task type="auto">
  <name>Task 1: Size the strip from an ascender-plus-descender sample and fix the baseline per font</name>
  <files>src/Border.cpp, include/Border.h, tests/test_wm_runtime.cpp</files>
  <action>
1. RED first. Add one case to tests/test_wm_runtime.cpp in the `[wm_config_runtime]`-adjacent tab group (pick the label the existing tab cases use). Start the fixture WM, map a client whose WM_NAME is "gjpqy settings" (descenders and a t), wait for the frame, locate the tab sub-window the way the existing cases do, and XGetImage the tab strip (the rotated part only, i.e. the columns of width m_tabWidth; derive the width from xIndent minus FRAME_WIDTH minus 1 as the existing cases do, and the frame thickness from the default). Assert:
   (a) at least one pixel in the strip is the tab foreground colour (the label rendered);
   (b) the 5 columns nearest the frame edge (the baseline side) contain NO tab-foreground pixel across the whole label run (exclude the top button box and the bottom diagonal by restricting rows to the label run, e.g. rows between the button and the diagonal start);
   (c) the 2 columns at the outer edge contain no foreground pixel either.
   Map a second client titled "MMMMM settings" (no descenders) and assert the first foreground column (ascender top) is the same column index in both tabs.
   Run the case and record that it FAILS on (b) before the change.
2. GREEN. In src/Border.cpp change BOTH rotated sizing sites (loadTabFont and the live-reload copy) to measure the sample "Mg", set m_tabWidth = extents.width + 7, and compute the new `static int m_tabBaseline` per the formulas in <context> (after confirming the sign by measurement). Declare m_tabBaseline in include/Border.h next to m_tabWidth and define it next to m_tabWidth's definition. In drawLabel's rotated path, replace the x argument `2 + extents.width` with `m_tabBaseline`; the label extents call there becomes unnecessary -- remove it if nothing else uses it. Keep the comments' AXIS history intact and add a short comment saying why the sample carries a descender and why the baseline is per font, not per title.
3. Mutation check: revert only the drawLabel x-argument change (keep the width change) and confirm the new case reddens on (b); restore. Then revert only the width change and confirm it reddens; restore.
4. Run `ctest --test-dir build/debug -L wm_config_runtime` and every label the existing tab cases carry; then the full suite once (`ctest --test-dir build/debug --output-on-failure --no-tests=error`). Baseline 526/526 before this task.
  </action>
  <verify>
    <automated>cmake --build build/debug --parallel && ctest --test-dir build/debug --output-on-failure --no-tests=error</automated>
    <fails_when>Any case fails, or the new case passes with the drawLabel x change reverted.</fails_when>
  </verify>
  <done>Both sizing sites use "Mg" + 7; m_tabBaseline exists and drives the rotated label's x; the new pixel case is green and was shown red under both mutations; full suite green.</done>
</task>

<task type="auto">
  <name>Task 2: Before/after evidence for the operator</name>
  <files>.planning/quick/260906-ldw-tab-label-baseline-clearance-size-the-ro/evidence/</files>
  <action>
1. tab-before.png: crop the tab region (about 40x240 px at the top-left) from `.planning/phases/09-config-gui-ipc/evidence/wm2-config/appearance-page-default-size.png` (captured before this change) and scale it 8x with `convert ... -crop 40x240+0+0 +repage -scale 800%`.
2. tab-after.png: on a free Xvfb display (>= :140, `-nolisten tcp`, no access-control flag), run `build/debug/wm2-born-again` with an empty XDG_CONFIG_HOME temp dir, map `xmessage -title "wm2-born-again settings" hello` (or the settings window itself, `build/debug/wm2-config`, with GDK_BACKEND=x11), wait for the frame, `import -window root` a full screenshot, then crop and scale the same region the same way. Kill xmessage/wm2-config, the WM and Xvfb by the PIDs you started, in that order.
3. Also save the uncropped after-screenshot as tab-after-full.png. No hostnames, addresses or real paths beyond the home directory name may appear in any image.
  </action>
  <verify>
    <automated>ls .planning/quick/260906-ldw-tab-label-baseline-clearance-size-the-ro/evidence/tab-before.png .planning/quick/260906-ldw-tab-label-baseline-clearance-size-the-ro/evidence/tab-after.png</automated>
    <fails_when>Either file is missing.</fails_when>
  </verify>
  <done>Three images exist under the quick task's evidence directory; the after image shows clear tab between the letter bottoms and the frame line.</done>
</task>

</tasks>

<verification>
- Full debug suite green (527 expected: 526 + 1 new case).
- `bash scripts/gates/build-all.sh asan` green (Border.cpp changed).
- The evidence pair exists and the after crop shows the gap.
</verification>

<success_criteria>
- Descenders no longer touch the frame; 5 px clear on the baseline side, 2 px on the ascender side, baseline constant across titles.
- Nothing else about the frame changed; existing difference-based tab cases still green.
</success_criteria>
