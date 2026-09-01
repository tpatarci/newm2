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


// ---------------------------------------------------------------------------
// Fixed values, shared by the histogram cases.
//
// These are pixel VALUES, not colours the server resolved -- nothing here talks
// to a server. They stand for the roles the real capture sees:
//
//   kConfigured  the colour the flaky runtime case configures with
//                --menu-background=#00cc00, and therefore the value the paint
//                verdict is asked about
//   kShipped     the shipped silver (08.5-02), which is what the menu is when
//                nothing configured it -- and, in the wrong-colour case, what
//                the sample came back holding instead
//   kRoot        the xvfb root, black, which is what a rectangle holds BEFORE
//                the server has filled the menu's background
//   kInk         the menu's label text, drawn over the background
// ---------------------------------------------------------------------------
constexpr unsigned long kConfigured = 0x00cc00UL;
constexpr unsigned long kShipped    = 0xc8caccUL;
constexpr unsigned long kRoot       = 0x000000UL;
constexpr unsigned long kInk        = 0x222222UL;


TEST_CASE("verdict-rejects-bleed-through", "[menupaint]")
{
    // The shape a sample taken BEFORE the server fill actually produces: the
    // rectangle is still mostly root, with a minority of the menu's own
    // background showing through at the edge that has been filled.
    //
    // The old rule counts two distinct values, says "painted", and hands the
    // caller a rectangle that is 98% root. Nothing about that rectangle
    // supports the claim, and no number of retries would make it support it --
    // the predicate is not measuring the thing it is named after.
    PixelHistogram h;
    h[kRoot]       = 9800;
    h[kConfigured] =  200;

    INFO("verdict was " << describeVerdict(classify(h, kConfigured)));
    CHECK(classify(h, kConfigured) != PaintVerdict::Painted);
}


TEST_CASE("verdict-rejects-wrong-colour", "[menupaint]")
{
    // A fully painted menu -- background filled, labels drawn -- in ENTIRELY
    // THE WRONG COLOUR. This is the case that matters most to the flaky runtime
    // case, whose whole subject is whether the CONFIGURED colour reached the
    // menu: here it plainly did not, and the old rule reports success anyway,
    // because two distinct values is all it ever asks for.
    PixelHistogram h;
    h[kShipped] = 9700;
    h[kInk]     =  300;

    INFO("verdict was " << describeVerdict(classify(h, kConfigured)));
    CHECK(classify(h, kConfigured) != PaintVerdict::Painted);
}


TEST_CASE("verdict-names-background-only", "[menupaint]")
{
    // The server-filled, rows-blank state. It is REAL and it is legitimate:
    // WindowManager sets the popup's background pixel (src/Manager.cpp:699), so
    // the server fills the window on map, while paintOuter() draws the rows
    // later and only from the Expose arm. A menu that has been mapped and
    // filled but never exposed looks exactly like this.
    //
    // The old rule sees one distinct value, answers "not complete", and gives
    // that answer the same name it gives an empty capture -- so a failure
    // cannot say whether the window manager never mapped a menu or merely never
    // drew its rows. Those are different defects with different owners.
    PixelHistogram h;
    h[kConfigured] = 10000;

    const PaintVerdict v = classify(h, kConfigured);
    INFO("verdict was " << describeVerdict(v));

    CHECK(v == PaintVerdict::BackgroundOnly);
    CHECK(v != PaintVerdict::Painted);
    CHECK(v != PaintVerdict::NoPixels);
}


TEST_CASE("foreign-expose-is-named", "[menupaint]")
{
    // GREEN at the round base, and it must stay green. It exists so that a
    // later narrowing of the mapping cannot quietly drop the Foreign verdict --
    // which would put the discard back out of a test's reach.
    constexpr Window kMenu    = 0x200001UL;
    constexpr Window kSubmenu = 0x200002UL;
    constexpr Window kClient  = 0x400007UL;

    // A managed client uncovered while the menu is up. The modal loop selects
    // ExposureMask, so this event IS delivered to it.
    CHECK(menuPaintTargetFor(kClient, kMenu, kSubmenu, -1) == MenuPaintTarget::Foreign);
    CHECK(menuPaintTargetFor(kClient, kMenu, kSubmenu, 2)  == MenuPaintTarget::Foreign);

    // The submenu keeps its X id after the flyout closes, so the id alone does
    // not make it live. With no category open it is foreign, exactly as the
    // inline `else if (... && openCat >= 0)` chain treated it.
    CHECK(menuPaintTargetFor(kSubmenu, kMenu, kSubmenu, -1) == MenuPaintTarget::Foreign);

    // And the two popups are still named, so the case cannot be satisfied by a
    // mapping that answers Foreign to everything.
    CHECK(menuPaintTargetFor(kMenu, kMenu, kSubmenu, -1)   == MenuPaintTarget::Outer);
    CHECK(menuPaintTargetFor(kSubmenu, kMenu, kSubmenu, 0) == MenuPaintTarget::Submenu);
}
