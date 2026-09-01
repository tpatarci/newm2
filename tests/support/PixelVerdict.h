#pragma once

// The menu paint-completion decision, lifted out of openRootMenu()'s third
// stage (tests/test_wm_runtime.cpp) so that a test can reach it without a
// display.
//
// This header is PURE. It opens no display, makes no Xlib call, spawns no
// window manager, takes no fixture and reads no clock. Its input is a
// histogram -- a plain map from pixel value to count -- and an expected pixel
// value; its output is a name. That is what lets test_menupaint reproduce a
// defect identically on every run and every host, which is what makes its
// result evidence rather than a sample.
//
// 08.5-13 Task 1 extracted this FAITHFULLY. See the note on classify() for what
// "faithfully" costs here.

#include <map>


// A histogram of pixel values, exactly as captureRoot() builds one.
using PixelHistogram = std::map<unsigned long, long>;


// What a capture of a menu rectangle actually shows.
//
// The vocabulary is deliberately larger than the decision it currently drives.
// The two states that matter most to name are the ones the old rule could not
// tell apart:
//
//   * `NoPixels`       -- nothing was captured at all; the window is not there.
//   * `BackgroundOnly` -- the rectangle is one flat sheet of the EXPECTED
//                         colour. This is a real, legitimate intermediate
//                         state, not an error: WindowManager creates the popup
//                         with XSetWindowBackground() (src/Manager.cpp:699), so
//                         the SERVER fills the background when the window is
//                         mapped, while paintOuter() draws the rows later and
//                         only from the Expose arm. "Mapped, filled, rows not
//                         yet drawn" is therefore something the menu genuinely
//                         passes through.
//
// A test that cannot distinguish those two cannot say whether the window
// manager failed to map a menu or merely failed to draw its rows.
enum class PaintVerdict {
    NoPixels,        // the capture came back empty
    BackgroundOnly,  // one flat fill of the expected value; rows not yet drawn
    WrongFill,       // dominated by a value that is not the expected one
    Mixed,           // content, but nothing holds a decisive share
    Painted          // the expected value dominates by a stated margin
};


inline const char* describeVerdict(PaintVerdict v)
{
    switch (v) {
    case PaintVerdict::NoPixels:       return "NoPixels";
    case PaintVerdict::BackgroundOnly: return "BackgroundOnly";
    case PaintVerdict::WrongFill:      return "WrongFill";
    case PaintVerdict::Mixed:          return "Mixed";
    case PaintVerdict::Painted:        return "Painted";
    }
    return "?";
}


// THE DEFECT UNDER TEST, preserved deliberately at 08.5-13 Task 1.
//
// The rule this replaces is, in full:
//
//     captureRoot(d, rectOut).size() >= 2
//
// It asks one question -- does the sampled rectangle hold at least two distinct
// pixel values -- and that question is wrong on its own terms, independent of
// any flake:
//
//   * a HALF-DRAWN menu satisfies it;
//   * root pixels BLEEDING THROUGH an unfilled window satisfy it;
//   * a menu painted in ENTIRELY THE WRONG COLOUR satisfies it.
//
// It is a non-specific NEGATIVE standing where a positive criterion belongs,
// and note what it never once looks at: `expectedPixel`. A predicate for "has
// the menu been painted in the configured colour" that does not read the
// configured colour cannot answer that question at any rate of success.
//
// So this task keeps it. The rule has exactly TWO answers, and everything that
// is not "two or more distinct values" collapses into a single bucket: an
// unmapped window with no pixels at all and a server-filled background with its
// rows not yet drawn come back identical. The richer vocabulary above is
// declared but three of its five names are unreachable from here, which is not
// an oversight -- it is the measurement of how much the old rule could not say.
//
// Task 3 replaces the body with a positive criterion. Task 2's cases fail
// against THIS body first, so that the replacement is falsifiable.
inline PaintVerdict classify(const PixelHistogram& h, unsigned long expectedPixel)
{
    (void)expectedPixel;  // never consulted -- see above

    if (h.size() >= 2) return PaintVerdict::Painted;
    return PaintVerdict::NoPixels;
}


// The question openRootMenu()'s third stage actually asks. Kept separate from
// classify() so the stage can wait on completeness while a failure report names
// the verdict it last saw.
inline bool paintComplete(const PixelHistogram& h, unsigned long expectedPixel)
{
    return classify(h, expectedPixel) == PaintVerdict::Painted;
}
