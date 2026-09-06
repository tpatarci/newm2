#pragma once

// The root menu's TOP-LEVEL INDEX MODEL, lifted out of the modal loop in
// WindowManager::menu() so that a test can reach it.
//
// This header is deliberately free of every project header and of Xlib itself:
// the model is arithmetic over four counts, and nothing about "which row is
// row 5" needs a display. test_menupaint links Catch2 alone for this file's
// cases, and that constraint is the whole reason the decision lives in a header
// rather than inside the loop body. Same precedent, same reason, as
// include/MenuPaint.h (08.5-13 Task 1).
//
// WHY IT WAS WORTH EXTRACTING. Four separate places in menu() index the same
// list -- the label lambda, the right-alignment test in the row painter, the
// category hit test in the pointer policy, and the selection dispatch after the
// loop -- and each carried its own copy of the arithmetic in the form
// `idx < nh + numCategories`. Adding D-11's Configure row meant editing all
// four consistently, and four copies of a bound is four chances to shift a row
// under the user's pointer. There is now one copy, and a display-free case
// pins every index in it.

// Which kind of thing a given row of the root menu's top level is.
//
// `OutOfRange` rather than `None`: Xlib defines None as a macro (`#define None
// 0L`), so an enumerator of that name does not survive the preprocessor in any
// translation unit that includes Xlib -- which is every caller of this header
// except the test.
enum class RootMenuSlot {
    OutOfRange,     // no such row
    Create,         // the "New" entry, always row 0
    HiddenClient,   // one of the iconified clients
    Category,       // one of the application-category rows (opens a submenu)
    ConfigureGui,   // D-11's Configure entry, present only when the GUI is
    Exit            // the "[Exit wm2]" row, present only on a corner press
};

// The label the Configure row carries, and the binary it launches.
//
// Both live here, beside the model, because two call sites need each of them
// and a second spelling of either is a second answer: WindowManager's startup
// probe asks whether `kRootMenuConfigureBinary` is on PATH, and the menu's
// selection dispatch execs that same name. If those two ever disagreed the
// entry would appear and then launch nothing, which is the one failure mode
// this arrangement makes unrepresentable.
inline constexpr const char* kRootMenuConfigureLabel  = "Configure...";
inline constexpr const char* kRootMenuConfigureBinary = "wm2-config";

// The "[Exit wm2]" row's label, unchanged from what menu() spelled inline.
inline constexpr const char* kRootMenuExitLabel = "[Exit wm2]";


// The top level of the root menu, as counts rather than as strings.
//
// ORDER IS THE CONTRACT, and it is D-11's: create, then the hidden clients,
// then the application categories, then Configure, then Exit. Configure sits at
// the END of the top level and BEFORE the exit slot when that slot is present,
// so that a window manager on a host with no settings window installed lays out
// exactly as it did before -- every other row at the index it already had, the
// exit row included.
struct RootMenuLayout {
    int  hiddenClients   = 0;      // iconified clients, one row each
    int  categories      = 0;      // application-category rows, one each
    bool hasConfigureGui = false;  // D-11: was wm2-config on PATH at startup?
    bool allowExit       = false;  // the corner press that offers "[Exit wm2]"

    // Row 0 is always the create entry; the hidden clients follow it.
    int firstHiddenIndex() const { return 1; }
    int firstCategoryIndex() const { return 1 + hiddenClients; }

    // -1 when the row is absent, which is a value a caller can compare an index
    // against safely -- a real row index is never negative.
    int configureIndex() const {
        return hasConfigureGui ? firstCategoryIndex() + categories : -1;
    }
    int exitIndex() const { return allowExit ? count() - 1 : -1; }

    int count() const {
        return 1 + hiddenClients + categories
             + (hasConfigureGui ? 1 : 0)
             + (allowExit ? 1 : 0);
    }

    RootMenuSlot slotAt(int i) const
    {
        if (i < 0 || i >= count()) return RootMenuSlot::OutOfRange;
        if (i == 0) return RootMenuSlot::Create;
        if (i < firstCategoryIndex()) return RootMenuSlot::HiddenClient;
        if (i < firstCategoryIndex() + categories) return RootMenuSlot::Category;
        if (i == configureIndex()) return RootMenuSlot::ConfigureGui;
        if (i == exitIndex()) return RootMenuSlot::Exit;
        // Unreachable: the four arms above partition [0, count()). Kept as a
        // total function rather than as an assertion, because the alternative
        // menu() shipped until this extraction was to fall off the end of an
        // if/else chain and index the hidden-client vector with whatever index
        // arrived -- reachable only through a bug, and undefined behaviour when
        // it was. A row that names itself as nothing draws an empty label and
        // does nothing when released.
        return RootMenuSlot::OutOfRange;
    }

    // The position of row `i` within its own group. Only meaningful for the
    // slot kind named; the caller has already asked slotAt().
    int hiddenClientAt(int i) const { return i - firstHiddenIndex(); }
    int categoryAt(int i) const { return i - firstCategoryIndex(); }
};
