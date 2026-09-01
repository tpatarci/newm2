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
// 08.5-13 Task 1 extracted this FAITHFULLY, defect and all; Task 3 replaced the
// body once cases existed that failed without the replacement. See the note on
// classify() for what the old rule was and why it was wrong.

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


// WHAT THIS REPLACED, AND WHY IT WAS WRONG ON ITS OWN TERMS.
//
// Until 08.5-13 Task 3 the rule was, in full:
//
//     captureRoot(d, rectOut).size() >= 2
//
// It asked one question -- does the sampled rectangle hold at least two
// distinct pixel values -- and that question is wrong independent of any flake:
//
//   * a HALF-DRAWN menu satisfies it;
//   * root pixels BLEEDING THROUGH an unfilled window satisfy it;
//   * a menu painted in ENTIRELY THE WRONG COLOUR satisfies it.
//
// It was a non-specific NEGATIVE standing where a positive criterion belongs,
// and note what it never once looked at: `expectedPixel`. A predicate for "has
// the menu been painted in the configured colour" that does not read the
// configured colour cannot answer that question at any rate of success. The
// three RED cases in tests/test_menupaint.cpp are three different wrong answers
// it gave, each reproduced without a display.
//
// THE RULE NOW: the expected value must DOMINATE, by a stated margin. That is a
// positive criterion -- it says what the rectangle must be, not merely what it
// must not be -- and every intermediate state on the way there has its own
// name, so a caller reporting a failure can say which one it saw.


// The share of the sample the expected value must hold before the rectangle is
// called painted.
//
// Chosen on this measurement's own terms. A drawn menu is a solid field of the
// background colour with rows of text over it, a one-pixel border, and at most
// one highlight band: glyph ink is sparse, so the background keeps the large
// majority of the rectangle -- high, but nowhere near unity. A bleed-through
// sample is the opposite shape, the expected value holding a small minority
// while root still owns the rest. 0.55 sits in the wide gap between those two,
// comfortably below the fraction a real menu leaves uncovered and far above
// anything a partially filled window produces.
//
// It has NOTHING to do with the 8000 ms harness deadline and is not derived
// from it; that constant is a duration and this one is a proportion.
//
// It is a compile-time constant and is deliberately NOT settable from the
// environment. A completion criterion a test run can relax is not a criterion.
constexpr double kDominanceFloor = 0.55;


inline PaintVerdict classify(const PixelHistogram& h, unsigned long expectedPixel)
{
    if (h.empty()) return PaintVerdict::NoPixels;

    long total = 0;
    unsigned long dominant = 0;
    long dominantCount = -1;
    for (const auto& kv : h) {
        total += kv.second;
        if (kv.second > dominantCount) {
            dominantCount = kv.second;
            dominant = kv.first;
        }
    }
    if (total <= 0) return PaintVerdict::NoPixels;

    // One flat sheet and nothing else. If it is the expected colour this is the
    // server-filled, rows-not-yet-drawn state -- REAL and legitimate, because
    // WindowManager sets the popup's background pixel (src/Manager.cpp:699) so
    // the server fills the window on map, while paintOuter() draws the rows
    // later and only from the Expose arm. Naming it is what lets a failure say
    // "the window manager mapped and the server filled, but no rows were ever
    // drawn" instead of "the menu did not appear".
    if (h.size() == 1) {
        return dominant == expectedPixel ? PaintVerdict::BackgroundOnly
                                         : PaintVerdict::WrongFill;
    }

    const double share = static_cast<double>(dominantCount) /
                         static_cast<double>(total);

    // Nothing holds a decisive share: the rectangle has content but is not any
    // one thing yet. A sample caught mid-fill lands here.
    if (share < kDominanceFloor) return PaintVerdict::Mixed;

    if (dominant != expectedPixel) return PaintVerdict::WrongFill;

    return PaintVerdict::Painted;
}
