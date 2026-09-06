# menupaint — dismembering the menu open-paint-sample path

Round record for `08.5-13`. Every figure below is traceable to a committed log
in this directory; nothing is restated from memory.

| | |
|---|---|
| round base | `e835737b2251ec2d7c145d74a153b5181da5f144` (`ROUND-BASE.txt`) |
| Task 1 tip | `99f6f72b1cdd0d788a4b263efe3fa5ed0bbc6bca` (`PLAN-13-T1-TIP.txt`) |
| host | `Linux 6.8.0-124-generic x86_64` / `<workstation>` |
| debug suite at the round base | 320 tests |
| debug suite after the round | 325 tests |

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
  That is what makes a result here evidence rather than a sample. Ten
  consecutive runs at Task 2 returned ten identical exit codes.

Each of `openRootMenu()`'s three stages also now names itself on expiry, so a
red run says which stage ran out of budget instead of returning a bare `false`.

## Why the old predicate was wrong on its own terms

Independent of any flake. `captureRoot(d, rect).size() >= 2` asks one question —
does the rectangle hold at least two distinct pixel values — and that question
is not the one the caller needs answered. It is a non-specific NEGATIVE standing
where a positive criterion belongs, and it never once reads the expected colour:
a predicate for "has the menu been painted in the configured colour" that does
not look at the configured colour cannot answer that question at any rate of
success.

Three cases in `test_menupaint` are three different wrong answers it gave, each
reproduced with no display and no clock:

| case | histogram | old answer | correct answer |
|---|---|---|---|
| `verdict-rejects-bleed-through` | 9800 root pixels, 200 menu pixels | `Painted` | not painted — 98% of it is root |
| `verdict-rejects-wrong-colour` | 9700 of the shipped silver, 300 ink, expecting green | `Painted` | not painted — the configured colour is absent |
| `verdict-names-background-only` | 10000 pixels of exactly the expected colour | `NoPixels` | `BackgroundOnly` — mapped and server-filled, rows not yet drawn |

The third is the one that matters most for diagnosis. `WindowManager` sets the
popup's background pixel (`src/Manager.cpp:699`), so the SERVER fills the window
on map while `paintOuter()` draws the rows later and only from the `Expose` arm.
"Filled but never exposed" is a state the menu genuinely passes through, and the
old rule gave it the same name it gave an unmapped window — so a failure could
not say whether the window manager never mapped a menu or merely never drew its
rows. Those are different defects with different owners.

The replacement is positive: complete means the dominant pixel EQUALS the
expected value and holds at least `kDominanceFloor` (0.55) of the sample.

`red-before-fix.log` is the failing output, captured and committed at Task 2
before any fix existed. `green-after-fix.log` is the full suite afterwards.

## The partition, and its result

Fixed in `08.5-13-PLAN.md` before anything was measured. Given both captures at
one instant — the root-framebuffer readback and a direct readback of the menu
window — exactly one bucket applies, and there is no fourth:

| root readback | direct readback | attribution |
|---|---|---|
| disagrees with direct | — | the READBACK PATH — a test-instrument defect |
| agrees, wrong colour | agrees, wrong colour | PRODUCTION did not paint |
| agrees, right colour | agrees, right colour | the sample ran early — the completion predicate |

**Result: thirty consecutive passes.** `discriminator.log` records 30 runs, 60
capture lines (each run takes one shipped and one configured capture), and every
line reads `root=Painted direct=Painted ... agree=yes`. No run landed in any
bucket, because no run was red.

The log carries one further fact worth naming. The two configurations produce
DIFFERENT dominant pixels, thirty times each:

```
30 rootDom=0xc8cacc directDom=0xc8cacc     (shipped, no flag)
30 rootDom=0x00cc00 directDom=0x00cc00     (--menu-background=#00cc00)
```

Before this round, the attribution measurement recorded the shipped run and the
`--menu-background=#00cc00` run returning the BYTE-IDENTICAL histogram
`{0xdcdee0 x3000, 0x000000 x1428, 0xc8cacc x512, 0x222222 x17}` — the green run
containing no green at all, because both were sampling a client frame
(`0xDCDEE0` is `frameBackground`, `0xC8CACC` also `tabBackground`) rather than a
menu. That the configured colour now appears, and appears as the dominant pixel
of both readings, is direct evidence that the sample is reading the menu.

The cause removed to get there: `findOpenMenu()` had a silent fallback to "the
first viewable child of root larger than 1x1", returned whenever nothing
contained the press point. Because that is non-`None` the enclosing `pollUntil`
was satisfied on its FIRST iteration and never retried, so the twenty-second
stage budget was never spent. It now returns the window containing the press
point, or `None`.

## What this does not establish

- **This is not a gate rate, and no reliability is claimed here.** Thirty
  consecutive passes are counts. The arithmetic, stated rather than implied: at
  the one-in-three rate `08.5-11` observed, thirty consecutive passes have
  probability `(2/3)^30 ≈ 5.2 × 10⁻⁶`, so a defect at that rate is effectively
  excluded. Thirty passes bound a SMALL residual rate only weakly — by the rule
  of three the 95% upper bound is `3/30 = 0.10`, and a rate of 5% would still
  produce thirty consecutive passes 21% of the time. A residual rate anywhere
  below roughly one in ten is entirely consistent with what was measured.
- **The round's positive claim is the SPLIT and the three deterministic cases**,
  not the run count. Those three cases hold regardless of how many times the
  end-to-end case is run, on every host, forever; the thirty runs are a
  supplement to the partition, never a substitute for it.
- The `FOREIGN-DISPATCHED` gate in the plan is a WIRING check, and the plan says
  so. It was observed to match the Task 1 tree as well, where the foreign
  `Expose` was still being discarded — it cannot distinguish naming the verdict
  from acting on it. The behavioural claim rests on the code (`eventExposure()`
  is called from the `Foreign` arm) and on `foreign-expose-is-named`.
- One failure was observed in the FULL debug suite at the round base, in a
  different case: `exec-using-shell ...` (`tests/test_wm_runtime.cpp`), where
  `openRootMenu()` returned false after 25.70 s — the full stage budget SPENT,
  which is the opposite signature to the flake attributed here. It passed alone
  on two re-runs and is recorded in the ledger; it belongs to the
  deferred-item-17 family (the menu genuinely never opens) and is not addressed
  by this round.
- This round does not re-open the round-3 finding and does not rule the open
  `08.5-10` Task 3 checkpoint.
