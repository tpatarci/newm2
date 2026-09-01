// The two decisions the root menu's open-paint-sample path used to fuse into
// one boolean, tested as separate program units (08.5-13).
//
// THIS BINARY CANNOT FLAKE, AND THAT IS ITS ENTIRE PURPOSE. It opens no
// display, spawns no window manager, takes no fixture, drives no XTest, polls
// nothing and reads no clock. Every input is a synthetic histogram or a plain
// integer written out in the case itself. Whatever it reproduces, it reproduces
// identically on every run and on every host -- which is what makes a result
// here evidence rather than a sample.
//
// A case that needs a display belongs in tests/test_wm_runtime.cpp. One such
// case placed here destroys the only property this binary has.

#include <catch2/catch_test_macros.hpp>

#include "MenuPaint.h"
#include "support/PixelVerdict.h"


TEST_CASE("verdict-target-vocabulary is reachable without a display", "[menupaint]")
{
    // The trivial case that makes the target real and discoverable under its
    // ctest label. It asserts only that both units are linkable and answer at
    // all; the cases that assert on WHAT they answer are 08.5-13 Task 2's.
    const PixelHistogram empty;
    REQUIRE(classify(empty, 0x00cc00UL) != PaintVerdict::Painted);

    REQUIRE(menuPaintTargetFor(None, 0x200001UL, 0x200002UL, -1) ==
            MenuPaintTarget::NoTarget);
}
