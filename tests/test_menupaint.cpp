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
#include "RootMenuModel.h"
#include "support/PixelVerdict.h"

#include <string>


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


// ---------------------------------------------------------------------------
// The root menu's top-level INDEX MODEL (plan 09-09, D-11)
//
// These cases exist because the runtime cases cannot ask the question directly.
// The menu paints its labels with Xft, so the server keeps pixels and not
// strings: [wm_config_runtime] can prove that a row APPEARED, by measuring the
// popup one row taller, and that selecting the last row execs a binary of the
// right name -- but "the exit row is still at the index it was at" is a claim
// about arithmetic, and asserting it through a screen capture would mean
// pressing in the screen corner, which warps the pointer and opens a submenu
// nobody asked for.
//
// So the arithmetic moved into include/RootMenuModel.h, src/Buttons.cpp reads
// it, and these cases pin it. They open no display, spawn nothing and read no
// clock, exactly like the rest of this binary -- which is what makes a result
// here evidence rather than a sample.
// ---------------------------------------------------------------------------


TEST_CASE("the Configure row sits at the end of the top level, before Exit",
          "[menupaint]")
{
    RootMenuLayout layout;
    layout.hiddenClients   = 2;
    layout.categories      = 3;
    layout.hasConfigureGui = true;
    layout.allowExit       = true;

    //  0        New
    //  1, 2     the two hidden clients
    //  3, 4, 5  the three categories
    //  6        Configure...
    //  7        [Exit wm2]
    REQUIRE(layout.count() == 8);
    CHECK(layout.configureIndex() == 6);
    CHECK(layout.exitIndex() == 7);

    CHECK(layout.slotAt(0) == RootMenuSlot::Create);
    CHECK(layout.slotAt(1) == RootMenuSlot::HiddenClient);
    CHECK(layout.slotAt(2) == RootMenuSlot::HiddenClient);
    CHECK(layout.slotAt(3) == RootMenuSlot::Category);
    CHECK(layout.slotAt(5) == RootMenuSlot::Category);
    CHECK(layout.slotAt(6) == RootMenuSlot::ConfigureGui);
    CHECK(layout.slotAt(7) == RootMenuSlot::Exit);

    // Positions within their own group, which is what the label lambda and the
    // selection dispatch index the two vectors with.
    CHECK(layout.hiddenClientAt(1) == 0);
    CHECK(layout.hiddenClientAt(2) == 1);
    CHECK(layout.categoryAt(3) == 0);
    CHECK(layout.categoryAt(5) == 2);
}


TEST_CASE("without the settings window on PATH every other row keeps the index "
          "it had, the exit row included", "[menupaint]")
{
    RootMenuLayout without;
    without.hiddenClients = 2;
    without.categories    = 3;
    without.allowExit     = true;

    RootMenuLayout with = without;
    with.hasConfigureGui = true;

    // The whole promise of D-11's "otherwise no entry": one more row, and
    // nothing before it moved.
    CHECK(without.count() == 7);
    CHECK(with.count() == without.count() + 1);
    CHECK(without.configureIndex() == -1);

    for (int i = 0; i < without.count() - 1; ++i) {
        INFO("row " << i);
        CHECK(with.slotAt(i) == without.slotAt(i));
    }

    // The exit row is the one that DOES move, because it is defined as the
    // last row and the new entry goes in front of it. Both spellings of its
    // position are asserted, so a future change that appended Configure AFTER
    // Exit would fail here rather than in a screenshot.
    CHECK(without.exitIndex() == 6);
    CHECK(with.exitIndex() == 7);
    CHECK(with.slotAt(with.exitIndex()) == RootMenuSlot::Exit);
    CHECK(with.configureIndex() < with.exitIndex());
}


TEST_CASE("with no exit slot the Configure row is simply the last row",
          "[menupaint]")
{
    // The ordinary press: not in the screen's bottom-right corner, so menu()
    // offers no exit row at all. This is the layout every [wm_config_runtime]
    // case measures, which is why that suite can release on the last row and
    // expect the settings window.
    RootMenuLayout layout;
    layout.hiddenClients   = 0;
    layout.categories      = 2;
    layout.hasConfigureGui = true;

    REQUIRE(layout.count() == 4);
    CHECK(layout.exitIndex() == -1);
    CHECK(layout.configureIndex() == layout.count() - 1);
    CHECK(layout.slotAt(layout.count() - 1) == RootMenuSlot::ConfigureGui);
}


TEST_CASE("an empty menu is one row, and every index outside it names nothing",
          "[menupaint]")
{
    RootMenuLayout layout;   // no hidden clients, no categories, no GUI, no exit

    REQUIRE(layout.count() == 1);
    CHECK(layout.slotAt(0) == RootMenuSlot::Create);
    CHECK(layout.slotAt(-1) == RootMenuSlot::OutOfRange);
    CHECK(layout.slotAt(1) == RootMenuSlot::OutOfRange);

    // rowAt() answers -1 for "the pointer is on no row", and the dispatch
    // switch is handed that value on every menu dismissed without a selection.
    CHECK(layout.slotAt(-1) == RootMenuSlot::OutOfRange);
}


TEST_CASE("the label the Configure row draws and the binary it launches are "
          "one decision", "[menupaint]")
{
    // Two call sites in src/Manager.cpp and src/Buttons.cpp read these: the
    // startup probe asks whether the binary is on PATH, and the selection
    // dispatch execs it. A second spelling of either would make the entry
    // appear and then launch nothing, which no runtime case could distinguish
    // from a host where the program is broken.
    CHECK(std::string(kRootMenuConfigureLabel) == "Configure...");
    CHECK(std::string(kRootMenuConfigureBinary) == "wm2-config");
    CHECK(std::string(kRootMenuExitLabel) == "[Exit wm2]");
}
