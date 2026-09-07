---
quick_id: 260906-ldw
type: quick
subsystem: ui
tags: [x11, xft, rotated-text, glyph-metrics, window-decoration, pixel-test]

provides:
  - "The rotated tab strip is sized from a full ascender-plus-descender glyph box instead of a capital M"
  - "Border::m_tabBaseline -- a per-font baseline column that the rotated label is drawn on, replacing a per-title origin"
  - "A pixel-level [wm_tablabel] case asserting 5 px of clear tab on the frame side and 2 px on the outer side"
affects: [tab rendering, frame geometry, any future change to tab thickness or the tab font ladder]

actuals:
  tokens: 6400
  tasks: 2
  commits: 2
plan_head_before: d6afa5978566ffc69bf3511289f20c5bac3c16a9

tech-stack:
  added: []
  patterns:
    - "Font-derived layout constants are measured once per face and stored as statics, never recomputed per title"
    - "Tests derive m_tabWidth from the client's inset in its frame rather than pinning the value the host's font happens to produce"

key-files:
  created:
    - .planning/quick/260906-ldw-tab-label-baseline-clearance-size-the-ro/evidence/tab-before.png
    - .planning/quick/260906-ldw-tab-label-baseline-clearance-size-the-ro/evidence/tab-after.png
    - .planning/quick/260906-ldw-tab-label-baseline-clearance-size-the-ro/evidence/tab-after-full.png
  modified:
    - src/Border.cpp
    - include/Border.h
    - tests/test_wm_runtime.cpp

key-decisions:
  - "The sizing sample is the whole printable-ASCII repertoire, not the \"Mg\" the plan proposed -- measured, \"Mg\" bounds only itself and would have left common titles on the tab's outer edge"
  - "Two existing cases that had the old 16 px tab width baked into them as literals were re-derived from a measured tab width rather than re-tuned to the new number"

requirements-completed: []

duration: 95min
completed: 2026-09-06
status: complete
---

# Quick task 260906-ldw: Tab label baseline clearance Summary

**The sideways tab is now sized from a full ascender-plus-descender glyph box and its label is drawn on a per-font baseline column, so descenders stop 6 px short of the frame line instead of running into it, and the baseline no longer moves when the window title changes.**

## Performance

- **Duration:** ~95 min
- **Tasks:** 2 of 2
- **Files modified:** 3 source/test files, 3 evidence images added
- **Suite:** 527/527 debug (baseline 526 + 1 new case); `scripts/gates/build-all.sh asan` green, no sanitizer findings

## Accomplishments

- Both rotated sizing sites (`Border::loadTabFont()` and the live-reload copy in `reloadTabFont()`) now measure a sample that bounds the across-strip glyph box, and set `m_tabWidth` to that envelope plus the two clearances.
- A new static `Border::m_tabBaseline` is computed beside `m_tabWidth`; `drawLabel()`'s rotated path draws at that column instead of at `2 + the width of the label itself`. The label's own extents call in `drawLabel()` is gone -- nothing else used it.
- A new pixel-level `[wm_tablabel]` case asserts both clearances and the baseline's independence from the title, and was shown red under both mutations the plan asked for.
- Before/after evidence captured on an isolated Xvfb display.

## Measured extents (the sign check the plan required)

Rotated face at the shipped pattern (`Ubuntu,Noto Sans,DejaVu Sans,Sans:bold:size=12`), size 12, via `XftTextExtentsUtf8`. `XGlyphInfo` places ink at `[origin.x - x, origin.x - x + width)`:

| sample | width | x | above baseline | below baseline |
|---|---|---|---|---|
| `M` | 12 | 12 | 12 | 0 |
| `g` | 12 | 9 | 9 | 3 |
| `Mg` | 15 | 12 | 12 | 3 |
| `gjpqy settings` | 16 | 13 | 13 | 3 |
| `MMMMM settings` | 16 | 13 | 13 | 3 |
| `static routines` | 13 | 13 | 13 | 0 |
| `Hello` | 14 | 14 | 14 | 0 |
| printable ASCII (0x21..0x7e) | 18 | 14 | 14 | 4 |

**The sign is as the plan predicted, not mirrored.** `"M"` (no descender) comes back with `x == width` (12 and 12), while `"g"` comes back with `x` 9 against `width` 12 -- so `x` is the distance from the draw origin to the ASCENDER edge and `width - x` is the descender depth on the FRAME side. No formula needed mirroring.

Resulting constants on this host: `m_tabWidth` 16 -> **25** (`18 + 2 + 5`), `m_tabBaseline` = **16** (`2 + 14`).

## Task Commits

1. **Task 1: Size the strip from an ascender-plus-descender sample and fix the baseline per font** -- `94cfaa3` (fix)
2. **Task 2: Before/after evidence for the operator** -- `93c6575` (docs)

## RED -> GREEN -> mutations

Titles: `"gjpqy settings"` (g, j, p, q, y and two t stems) and `"static routines"` (identical tallest glyph -- the dot of an `i` at 13 px -- and no descender at all). One window, retitled, as the sibling cases in the file do.

| state | tab width | `gjpqy settings` ink columns | `static routines` ink columns | verdict |
|---|---|---|---|---|
| **RED** (before the change) | 16 | 6..15 | 3..14 | FAILED: last ink column 15 for both, limit 10; ascender columns 6 vs 3 |
| **GREEN** (after) | 25 | 4..18 | 4..15 | PASSED: frame-side clear 6 px, outer clear 4 px, ascender column 4 for both |
| **M1** -- revert only `drawLabel`'s x argument | 25 | 6..20 | 3..14 | RED on the frame-side clearance (20 > 19) and on baseline invariance (6 vs 3) |
| **M2** -- revert only the width sizing | 16 | 2..15 | 2..13 | RED on the frame-side clearance for BOTH titles (15 and 13 against a limit of 10) |

Counted pixels are exact matches of the configured tab foreground; anti-aliased fringe pixels are blends and are deliberately not counted, so what is asserted is that no SOLID label pixel reaches either edge.

**Final suite: 527/527 debug tests passed** (`ctest --test-dir build/debug --output-on-failure --no-tests=error`), up from the 526/526 baseline by exactly the new case. `bash scripts/gates/build-all.sh asan` reported `build-all OK: asan`, `no sanitizer findings`.

## Files Created/Modified

- `src/Border.cpp` -- shared `kTabSample` / `kTabOuterClearance` / `kTabFrameClearance` at file scope with the measurement table that justifies them; both rotated sizing sites; `m_tabBaseline` definition; `drawLabel()`'s rotated draw origin.
- `include/Border.h` -- `static int m_tabBaseline;` next to `m_tabWidth`.
- `tests/test_wm_runtime.cpp` -- the new clearance case plus its `inkColumnCounts()` / `observeTabInk()` helpers; two existing cases re-derived from a measured tab width.
- `.planning/quick/.../evidence/tab-before.png`, `tab-after.png`, `tab-after-full.png`.

## Decisions Made

**The sample is the printable-ASCII repertoire, not `"Mg"`.** The plan specified `sample = "Mg"`, and the measurement above is why that could not stand: `"Mg"` reaches 12 px above the baseline, but the dot of an `i` reaches 13 and the stem of an `l` and the bracket `(` reach 14. With `"Mg"` sizing the baseline lands on column 14, and the plan's own test title `"gjpqy settings"` puts ink on column 1 -- one pixel from the tab's outer edge, failing the plan's own assertion (c). A title like `notes.txt (modified)` would have sat hard on the edge. The repertoire bounds both edges for every ASCII title and costs one extents call per font load.

**The clearances are named constants, not literals.** `kTabOuterClearance = 2` and `kTabFrameClearance = 5` are used both to size the strip and to place the baseline, so the two can never drift apart.

## Deviations from Plan

### 1. [Rule 1 - Bug] The plan's `"Mg"` sample does not bound the repertoire

- **Found during:** Task 1, at the measurement step the plan's `<context>` asked for.
- **Issue:** `"Mg"` gives 12 px above and 3 below. The plan's own test title reaches 13 above, and ordinary title characters (`l`, `b`, `(`, `[`) reach 14 above and 4 below. Sizing from `"Mg"` satisfies the descender half of the ask and leaves the ascender half broken -- measured, `"gjpqy settings"` would have drawn ink on column 1 of the tab.
- **Fix:** The sample is the printable-ASCII repertoire (`0x21..0x7e`) as a `static const char[]` with a compile-time length. The formulas are otherwise exactly the plan's: `m_tabWidth = extents.width + 2 + 5`, `m_tabBaseline = 2 + extents.x`.
- **Cost:** the tab thickens from 16 px to 25 px on this host. That is the price of a full glyph box plus the two clearances; it is visible in the after evidence and the operator should look at it.
- **Committed in:** `94cfaa3`.

### 2. [Rule 1 - Bug] Two existing cases had the old 16 px tab width baked in as literals

Both failed on the first full-suite run after the change. Neither failure was about the thing its case tests -- both were font-derived constants written out as the numbers this host's font happened to produce, the exact hardcoding the sibling tab cases explicitly avoid.

- **`The window button answers across the whole tab-top square...`** pinned `buttonDrawSize()` as the literal `8` (it is `m_tabWidth - 8`) and its near-miss press offsets as `12`, `13`, `15`, `30` (they are functions of `buttonHitSize() == m_tabWidth`). **Fix:** the case now measures the tab width from the client's inset in its frame (`xIndent() == m_tabWidth + FRAME_WIDTH + 1`) and expresses every offset against it. The deliberately-pinned 1 px sliver at the notch's inner edge still reads as not-part-of-any-window at the parameterised position, which is evidence the parameterisation tracks the real geometry rather than merely passing.
- **`A long title on a short window is shortened to fit its tab, still legibly`** asserted `longObs.tabLength > shortObs.tabLength * 2`. The tab window is `m_tabHeight + 2 + m_tabWidth` tall and `m_tabHeight` carries another `m_tabWidth`, so that ratio is mostly a ratio of constants and moves whenever the tab thickness moves. It read `115 > 140` for a tab that had grown from 70 px to 115 px in a 129 px frame. **Fix:** the claim ("the tab grew to use the room it has") is now measured against the frame -- the long tab must reach 80% of the frame height and the short one must not. 89% against 54% here, and 95% against 47% under the old thickness, so the assertion is stable across tab thicknesses and still fails for a stub tab.
- **Committed in:** `94cfaa3`.

---

**Total deviations:** 2 auto-fixed (both Rule 1). **Impact:** no scope creep. Deviation 1 is the plan's own measurement step doing its job; deviation 2 removes host-font hardcoding from two cases rather than re-tuning them to a new magic number.

## Issues Encountered

None beyond the two deviations. The evidence capture ran on a dedicated Xvfb display (`:142`, `-nolisten tcp`, no access-control flag) with an empty `XDG_CONFIG_HOME`; the client, window manager and X server were stopped by the PIDs captured at start, in that order, and all three confirmed stopped.

## Known Stubs

None.

## Threat Flags

None -- no new network, auth, file-access or schema surface. The change is confined to glyph metrics and a draw origin.

## Next Steps

- The operator should eyeball `evidence/tab-after.png` against `evidence/tab-before.png` and say whether the 25 px tab reads right. If it is too fat, the lever is `kTabFrameClearance` in `src/Border.cpp` (currently 5) or a narrower sample -- but a narrower sample brings back the class of defect this task removed, so the clearance is the honest knob.

## Self-Check: PASSED

All three modified source/test files, all three evidence images and both commits (`94cfaa3`, `93c6575`) verified present on disk and in git history. PLAN.md, SUMMARY.md and STATE.md deliberately left uncommitted for the orchestrator.
