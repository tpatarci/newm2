#pragma once

// The root menu's Expose-to-paint decision, lifted out of the modal loop in
// WindowManager::menu() so that a test can reach it.
//
// This header is deliberately free of any project header. test_menupaint links
// Catch2 and X11 only, with include/ on the header path and no production
// objects, so anything reachable from there must depend on nothing but Xlib and
// the C library. That constraint is the whole reason this decision lives in a
// header rather than inside the loop body.
//
// 08.5-13 Task 1 extracted this FAITHFULLY: the function reproduces the inline
// code's mapping exactly, defect included. The defect is named at the site
// where it lives.

#include <X11/Xlib.h>


// Which popup, if any, an Expose event asks the modal loop to paint.
//
// `Foreign` exists so that the loop's silent fallthrough becomes a value a test
// can assert on. It is not an error code: at 08.5-13 Task 1 the caller does
// with it exactly what the inline code did -- nothing.
// `NoTarget` rather than `None`: Xlib defines None as a macro (`#define None
// 0L`), so an enumerator of that name does not survive the preprocessor.
enum class MenuPaintTarget {
    NoTarget,   // the event carries no window at all
    Outer,      // the outer root menu
    Submenu,    // the category flyout, and only while a category is open
    Foreign     // some other window -- see the RECORDED DEFECT note below
};


// The mapping the modal loop performed inline at src/Buttons.cpp:541-549 before
// this extraction:
//
//     if (event.xexpose.window == m_menuWindow) {
//         outerDrawn = true; paintOuter();
//     } else if (event.xexpose.window == m_submenuWindow && openCat >= 0) {
//         subDrawn = true; paintSub();
//     }
//
// Note what the `else if` does NOT have: an else. Any Expose for a window that
// is neither popup falls off the end of the chain and is DISCARDED. The modal
// loop selects ExposureMask (MenuMask, src/Buttons.cpp:15), so those events are
// genuinely delivered to it and genuinely dropped.
//
// RECORDED DEFECT, ledger entry 9 against src/Buttons.cpp:530. Interleaving:
// the menu grab is active in the modal loop; another managed client is
// uncovered and the server sends it an Expose; the loop's mask matches the
// event, sees a window that is not the menu, and drops it on the floor rather
// than routing it to WindowManager::eventExposure(); the client's frame and tab
// label stay blank until some later unrelated Expose repaints them.
//
// 08.5-13 Task 1 PRESERVES that discard deliberately. The point of this task is
// to make the fallthrough visible as a named value while changing nothing about
// what the program does; the fix belongs to Task 3, after a case that fails
// without it exists.
//
// `openCategory` is the loop's `openCat`: an index into m_appCategories, or -1
// when no submenu is open. The submenu window keeps its X id after the flyout
// closes, so the index -- not the id alone -- is what says the submenu is live.
// An Expose for the submenu window while no category is open is therefore
// Foreign, exactly as the inline chain treated it.
inline MenuPaintTarget menuPaintTargetFor(Window eventWindow,
                                          Window menuWindow,
                                          Window submenuWindow,
                                          int openCategory)
{
    // An event with no window has no paint target. Checked first so that a
    // popup id which is itself None -- the state the popups are in before
    // initialiseScreen() creates them -- cannot be matched by an empty event.
    if (eventWindow == None) return MenuPaintTarget::NoTarget;

    if (eventWindow == menuWindow) return MenuPaintTarget::Outer;

    if (eventWindow == submenuWindow && openCategory >= 0) {
        return MenuPaintTarget::Submenu;
    }

    return MenuPaintTarget::Foreign;
}
