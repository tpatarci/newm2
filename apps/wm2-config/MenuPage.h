#pragma once

// The Menu page: the user's own root-menu entries (D-12, T-9-40, T-9-44,
// plan 09-07).
//
// A list of rows with Add, Edit and Remove, and a dialog behind all three. The
// rows map ONE TO ONE onto the file's three-key `menu-entry-*` groups, in the
// order the parser's accumulator requires -- a name opens an entry, then its
// command, then its category -- which is the grammar include/ConfigProtocol.h
// froze for the socket as well.
//
// ---------------------------------------------------------------------------
// WHOLESALE, NEVER PER ROW
// ---------------------------------------------------------------------------
//
// Add, Edit and Remove each send the WHOLE list. That is not laziness: it is
// what the protocol's `menu-entries` value is, and it is what makes the
// operation idempotent and free of row identity. A per-row protocol would need
// the window manager and this page to agree about what row 2 is, across a
// reload that may have renumbered them.
//
// ---------------------------------------------------------------------------
// THE CATEGORY LIST IS THE WINDOW MANAGER'S ANSWER
// ---------------------------------------------------------------------------
//
// D-12's dropdown offers "the categories the window manager currently shows".
// This page therefore ASKS -- the read-only `menu-categories` key -- rather
// than re-running the .desktop and /usr/bin discovery scan itself. A second
// implementation of discovery would be a second answer, and the two would
// disagree the first time a .desktop file appeared. With nothing to ask, the
// list falls back to the categories the file already uses plus the Custom
// default, and the dialog says that is what it is doing.
//
// The model this page needs but a display-free case must be able to hold --
// the dialog's contents, the fallback list, the protocol's line bound -- lives
// in MenuModel.h and is free of GTK.

#include "FormState.h"
#include "MenuModel.h"

#include <gtk/gtk.h>

#include <functional>
#include <string>
#include <vector>


class MenuPage {
public:
    // The whole entry list changed and must reach the running window manager
    // now (D-05). The key is always kMenuEntriesKey; the signature matches the
    // other pages' so the window wires all three the same way.
    using CommitHandler =
        std::function<void(const std::string& key, const std::string& value)>;

    // Something worth telling the user, for the window's status line.
    using StatusHandler = std::function<void(const std::string& message)>;

    MenuPage(FormState& form, CommitHandler onCommit, StatusHandler onStatus);
    ~MenuPage();

    MenuPage(const MenuPage&) = delete;
    MenuPage& operator=(const MenuPage&) = delete;

    GtkWidget* widget() const { return m_root; }

    // Re-render the row list from the model. Called after a revert, after a
    // save, and when a reload notice moves the effective list underneath an
    // open window (D-08).
    //
    // It touches the LIST ONLY. An entry dialog that happens to be open is not
    // reachable from here -- its contents are a MenuEntryDraft on the stack of
    // the call that opened it -- which is how D-08's "a reload must not discard
    // what somebody is typing" is kept by construction rather than by a flag.
    void refreshFromForm();

    // What the running window manager says its root menu shows, in the menu's
    // own order. Handed in by the window when the answer arrives, and again
    // after a reload notice.
    void setCategoriesFromWindowManager(const std::vector<std::string>& categories);

    // No window manager to ask: the dropdown falls back and says so.
    void setFileOnly();

    // D-13's per-page half. The Menu page's one "setting" is the entry list,
    // so this is FormState::requestResetAll() over kMenuEntriesKey.
    void resetAll();

private:
    void buildList(GtkWidget* column);
    void buildButtons(GtkWidget* column);

    // The rows the page is showing, from the model.
    const std::vector<AppEntry>& rows() const { return m_form.menuEntries(); }

    // Index of the selected row, or -1.
    int selectedRow() const;
    void updateButtonSensitivity();

    // Send the whole list, having first checked it fits in one protocol line
    // (T-9-44). Returns false and says why when it does not, leaving the model
    // untouched -- a refusal the user can act on rather than an opaque framing
    // error from the window manager.
    bool commitRows(const std::vector<AppEntry>& next);

    // The Add / Edit dialog. True when the user accepted, with `draft` holding
    // what they entered. Modal to the settings window.
    bool runEntryDialog(const char* title, MenuEntryDraft& draft);

    // The categories the dropdown offers right now, and whether they came from
    // a running window manager or from the file.
    std::vector<std::string> categories() const;

    void add();
    void edit();
    void remove();

    static void onAddClicked(GtkButton* button, gpointer userData);
    static void onEditClicked(GtkButton* button, gpointer userData);
    static void onRemoveClicked(GtkButton* button, gpointer userData);
    static void onResetAllClicked(GtkButton* button, gpointer userData);
    static void onRowActivated(GtkTreeView* view, GtkTreePath* path,
                               GtkTreeViewColumn* column, gpointer userData);
    static void onSelectionChanged(GtkTreeSelection* selection, gpointer userData);

    FormState&    m_form;
    CommitHandler m_onCommit;
    StatusHandler m_onStatus;

    GtkWidget* m_root = nullptr;
    GtkWidget* m_view = nullptr;
    GtkListStore* m_store = nullptr;
    GtkWidget* m_edit = nullptr;
    GtkWidget* m_remove = nullptr;
    GtkWidget* m_note = nullptr;

    std::vector<std::string> m_wmCategories;
    bool m_haveWmCategories = false;
};
