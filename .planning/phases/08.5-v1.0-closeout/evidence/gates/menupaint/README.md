# menupaint — dismembering the menu open-paint-sample path

Round record for `08.5-13`. Every figure below is traceable to a committed log
in this directory; nothing is restated from memory.

| | |
|---|---|
| round base | `e835737b2251ec2d7c145d74a153b5181da5f144` (`ROUND-BASE.txt`) |
| Task 1 tip | `99f6f72b1cdd0d788a4b263efe3fa5ed0bbc6bca` (`PLAN-13-T1-TIP.txt`) |
| host | `Linux 6.8.0-124-generic x86_64` / `tomislav-HP-Z440-Workstation` |
| debug suite at the round base | 320 tests |

## What was fused

`openRootMenu()` (`tests/test_wm_runtime.cpp`) ran four stages behind one
boolean, so a failure at any one of them surfaced identically:

1. `findOpenMenu()` — has the window manager mapped a menu?
2. `serverRect(...) && w > 1 && h > 1` — has the server realised its geometry?
3. `captureRoot(d, rect).size() >= 2` — a stand-in for "has it been painted?"
4. the caller then samples with `captureRoot()` and takes a dominant pixel.

A test that cannot say which stage failed cannot attribute its own flake.

## What is now separate

- **`include/MenuPaint.h`** — production. The modal loop's Expose-to-paint
  decision as a pure function returning a named target: outer, submenu, foreign
  or none. The silent fallthrough at `src/Buttons.cpp:530` is now an assertable
  value rather than the absence of an `else`.
- **`tests/support/PixelVerdict.h`** — the test instrument. The
  paint-completion decision as a pure function over a histogram, returning a
  named verdict instead of a pixel count.
- **`test_menupaint`** — the binary that cannot flake. It opens no display,
  spawns no window manager, takes no fixture, drives no XTest, polls nothing and
  reads no clock. Its inputs are synthetic histograms and plain integers, so a
  defect it reproduces is reproduced identically on every run and every host.
  That is what makes a result here evidence rather than a sample.

## The cases

| case | what it fixes in place | at the round base |
|---|---|---|
| `verdict-rejects-bleed-through` | a rectangle that is 98% root pixels with a sliver of menu background is not a painted menu | RED |
| `verdict-rejects-wrong-colour` | a fully painted menu in entirely the wrong colour is not a painted menu | RED |
| `verdict-names-background-only` | server-filled with rows not yet drawn is a real, legitimate state and has its own name, distinct from an empty capture | RED |
| `foreign-expose-is-named` | an Expose for neither popup is named `Foreign`, so a later narrowing of the mapping cannot quietly drop the verdict | GREEN, and stays green |

RED output: `red-before-fix.log`, captured and committed at Task 2, before any
fix existed.

<!-- Task 3 and Task 4 complete this record. -->
