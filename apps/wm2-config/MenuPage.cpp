#include "MenuPage.h"

#include <string>
#include <vector>


namespace {

enum Column { ColName = 0, ColCommand, ColCategory, ColCount };

// The command as one line, which is what the list shows and what the dialog
// puts back into its field. The inverse of configTokeniseCommand() for a
// command with no runs of spaces in it, and deliberately lossy for one that
// has: two spaces the user typed are not a difference the argument vector
// records, so they are not a difference worth preserving.
std::string commandLine(const AppEntry& entry)
{
    std::string out;
    for (const std::string& token : entry.execArgv) {
        if (!out.empty()) out += " ";
        out += token;
    }
    return out;
}

// The dialog's live argument-vector readout (T-9-40). A static member would be
// three lines longer for nothing: this handler needs the two widgets and
// nothing else about the page.
struct ArgvReadout {
    GtkWidget* commandEntry = nullptr;
    GtkWidget* display = nullptr;
};

void onCommandChanged(GtkEditable* editable, gpointer userData)
{
    ArgvReadout* readout = static_cast<ArgvReadout*>(userData);
    if (!readout || !readout->display) return;

    MenuEntryDraft draft;
    draft.command = gtk_entry_get_text(GTK_ENTRY(editable));

    const std::string shown = draft.argvDisplay();
    const std::string text =
        shown.empty()
            ? std::string("Runs nothing yet.")
            : "Runs directly, not through a shell: " + shown;

    char* markup = g_markup_printf_escaped("<small>%s</small>", text.c_str());
    gtk_label_set_markup(GTK_LABEL(readout->display), markup);
    g_free(markup);
}

}  // namespace


MenuPage::MenuPage(FormState& form, CommitHandler onCommit, StatusHandler onStatus)
    : m_form(form), m_onCommit(std::move(onCommit)), m_onStatus(std::move(onStatus))
{
    m_root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(m_root, 12);
    gtk_widget_set_margin_bottom(m_root, 12);
    gtk_widget_set_margin_start(m_root, 12);
    gtk_widget_set_margin_end(m_root, 12);

    GtkWidget* heading = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(heading), "<b>Your own root-menu entries</b>");
    gtk_widget_set_halign(heading, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(m_root), heading, FALSE, FALSE, 0);

    GtkWidget* blurb = gtk_label_new(
        "These appear in the root menu alongside the applications found on "
        "this system. An entry whose name matches one that was found replaces "
        "it.");
    gtk_label_set_line_wrap(GTK_LABEL(blurb), TRUE);
    gtk_label_set_xalign(GTK_LABEL(blurb), 0.0f);
    gtk_widget_set_sensitive(blurb, FALSE);
    gtk_box_pack_start(GTK_BOX(m_root), blurb, FALSE, FALSE, 0);

    buildList(m_root);
    buildButtons(m_root);

    m_note = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(m_note), TRUE);
    gtk_label_set_xalign(GTK_LABEL(m_note), 0.0f);
    gtk_widget_set_sensitive(m_note, FALSE);
    gtk_box_pack_start(GTK_BOX(m_root), m_note, FALSE, FALSE, 0);

    refreshFromForm();
}


MenuPage::~MenuPage()
{
    if (m_store) g_object_unref(m_store);
    m_store = nullptr;
}


void MenuPage::buildList(GtkWidget* column)
{
    m_store = gtk_list_store_new(ColCount, G_TYPE_STRING, G_TYPE_STRING,
                                 G_TYPE_STRING);
    m_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(m_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(m_view), TRUE);

    struct { const char* title; int column; } columns[] = {
        {"Name",     ColName},
        {"Command",  ColCommand},
        {"Category", ColCategory},
    };
    for (const auto& spec : columns) {
        GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* col = gtk_tree_view_column_new_with_attributes(
            spec.title, renderer, "text", spec.column, nullptr);
        gtk_tree_view_column_set_resizable(col, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(m_view), col);
    }

    // Double-clicking a row edits it, which is what a list of editable rows
    // does everywhere else and costs one signal.
    g_signal_connect(m_view, "row-activated",
                     G_CALLBACK(&MenuPage::onRowActivated), this);
    GtkTreeSelection* selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(m_view));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(selection, "changed",
                     G_CALLBACK(&MenuPage::onSelectionChanged), this);

    GtkWidget* scroller = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(scroller), FALSE);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroller),
                                        GTK_SHADOW_IN);
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_container_add(GTK_CONTAINER(scroller), m_view);
    gtk_box_pack_start(GTK_BOX(column), scroller, TRUE, TRUE, 0);
}


void MenuPage::buildButtons(GtkWidget* column)
{
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(column), bar, FALSE, FALSE, 0);

    GtkWidget* add = gtk_button_new_with_label("Add...");
    gtk_widget_set_tooltip_text(add,
                                "Add an entry to the root menu. It appears the "
                                "next time you open the menu; nothing is "
                                "written to a file until you press Save.");
    g_signal_connect(add, "clicked", G_CALLBACK(&MenuPage::onAddClicked), this);
    gtk_box_pack_start(GTK_BOX(bar), add, FALSE, FALSE, 0);

    m_edit = gtk_button_new_with_label("Edit...");
    g_signal_connect(m_edit, "clicked", G_CALLBACK(&MenuPage::onEditClicked), this);
    gtk_box_pack_start(GTK_BOX(bar), m_edit, FALSE, FALSE, 0);

    // No confirmation, by decision (D-12). That is only defensible because the
    // removal is unsaved and Revert brings the row back -- which a case
    // asserts, so the choice rests on a proof rather than on a hope.
    m_remove = gtk_button_new_with_label("Remove");
    gtk_widget_set_tooltip_text(m_remove,
                                "Remove the selected entry. Revert brings it "
                                "back until you press Save.");
    g_signal_connect(m_remove, "clicked", G_CALLBACK(&MenuPage::onRemoveClicked), this);
    gtk_box_pack_start(GTK_BOX(bar), m_remove, FALSE, FALSE, 0);

    GtkWidget* resetAll = gtk_button_new_with_label("Reset this page");
    gtk_widget_set_tooltip_text(resetAll,
                                "Remove every entry you added here. Saving then "
                                "takes those lines out of your configuration "
                                "file.");
    g_signal_connect(resetAll, "clicked",
                     G_CALLBACK(&MenuPage::onResetAllClicked), this);
    gtk_box_pack_end(GTK_BOX(bar), resetAll, FALSE, FALSE, 0);

    updateButtonSensitivity();
}


void MenuPage::refreshFromForm()
{
    // The LIST only. An open entry dialog keeps its contents through this,
    // because its contents are a MenuEntryDraft this function cannot reach
    // (D-08's standing prohibition, kept structurally).
    const int wasSelected = selectedRow();

    gtk_list_store_clear(m_store);
    for (const AppEntry& entry : rows()) {
        GtkTreeIter iter;
        gtk_list_store_append(m_store, &iter);
        gtk_list_store_set(m_store, &iter,
                           ColName,     entry.name.c_str(),
                           ColCommand,  commandLine(entry).c_str(),
                           ColCategory, entry.category.c_str(),
                           -1);
    }

    if (wasSelected >= 0 && wasSelected < static_cast<int>(rows().size())) {
        GtkTreePath* path = gtk_tree_path_new_from_indices(wasSelected, -1);
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(m_view), path, nullptr, FALSE);
        gtk_tree_path_free(path);
    }

    updateButtonSensitivity();
}


int MenuPage::selectedRow() const
{
    GtkTreeSelection* selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(m_view));
    GtkTreeModel* model = nullptr;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return -1;

    GtkTreePath* path = gtk_tree_model_get_path(model, &iter);
    if (!path) return -1;
    const gint* indices = gtk_tree_path_get_indices(path);
    const int row = indices ? indices[0] : -1;
    gtk_tree_path_free(path);
    return row;
}


void MenuPage::updateButtonSensitivity()
{
    const gboolean any = selectedRow() >= 0 ? TRUE : FALSE;
    if (m_edit)   gtk_widget_set_sensitive(m_edit, any);
    if (m_remove) gtk_widget_set_sensitive(m_remove, any);
}


void MenuPage::setCategoriesFromWindowManager(const std::vector<std::string>& categories)
{
    m_wmCategories = categories;
    m_haveWmCategories = !categories.empty();
    if (m_note) gtk_label_set_text(GTK_LABEL(m_note), "");
}


void MenuPage::setFileOnly()
{
    m_wmCategories.clear();
    m_haveWmCategories = false;
    if (m_note) gtk_label_set_text(GTK_LABEL(m_note), kMenuCategoryFileOnlyReason);
}


std::vector<std::string> MenuPage::categories() const
{
    // The window manager's answer when there is one. menuCategoriesFrom() is
    // the FALLBACK and nothing else -- it sees only what the file contains,
    // because with nothing running there is no discovery result to consult and
    // re-running the scan here would be a second answer.
    if (m_haveWmCategories) return m_wmCategories;
    return menuCategoriesFrom(rows());
}


bool MenuPage::commitRows(const std::vector<AppEntry>& next)
{
    Config rendered;
    rendered.manualMenuEntries = next;
    const std::string value = configMenuEntriesValue(rendered);

    // T-9-44, checked BEFORE the model moves: a list that cannot be sent must
    // not become the list the page is showing, or Save would write a file the
    // window manager can be told about only by restarting it.
    std::string reason;
    if (!menuEntriesValueFits(value, reason)) {
        if (m_onStatus) m_onStatus(reason);
        return false;
    }

    if (!m_form.setMenuEntries(next)) return false;

    refreshFromForm();
    if (m_onCommit) m_onCommit(kMenuEntriesKey, m_form.value(kMenuEntriesKey));
    return true;
}


bool MenuPage::runEntryDialog(const char* title, MenuEntryDraft& draft)
{
    GtkWidget* parent = gtk_widget_get_toplevel(m_root);
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        title,
        GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : nullptr,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL |
                                    GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_OK",     GTK_RESPONSE_ACCEPT,
        nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_margin_top(grid, 12);
    gtk_widget_set_margin_bottom(grid, 12);
    gtk_widget_set_margin_start(grid, 12);
    gtk_widget_set_margin_end(grid, 12);
    gtk_container_add(GTK_CONTAINER(content), grid);

    int line = 0;

    GtkWidget* nameLabel = gtk_label_new("Name");
    gtk_widget_set_halign(nameLabel, GTK_ALIGN_START);
    GtkWidget* nameEntry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(nameEntry), draft.name.c_str());
    gtk_entry_set_width_chars(GTK_ENTRY(nameEntry), 30);
    gtk_entry_set_activates_default(GTK_ENTRY(nameEntry), TRUE);
    gtk_widget_set_tooltip_text(nameEntry,
                                "What the menu row says. If it matches an "
                                "application already found on this system, this "
                                "entry replaces that one in the menu.");
    gtk_grid_attach(GTK_GRID(grid), nameLabel, 0, line, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), nameEntry, 1, line++, 1, 1);

    GtkWidget* commandLabel = gtk_label_new("Command");
    gtk_widget_set_halign(commandLabel, GTK_ALIGN_START);
    GtkWidget* commandEntry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(commandEntry), draft.command.c_str());
    gtk_entry_set_width_chars(GTK_ENTRY(commandEntry), 30);
    gtk_entry_set_activates_default(GTK_ENTRY(commandEntry), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(commandEntry),
                                "monospace");
    gtk_grid_attach(GTK_GRID(grid), commandLabel, 0, line, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), commandEntry, 1, line++, 1, 1);

    // T-9-40 made visible. The command is split on whitespace and run directly;
    // showing the resulting arguments is the honest form of "this is not
    // shell-evaluated", because a user who typed a semicolon can see for
    // themselves that it stayed inside one argument.
    GtkWidget* argvDisplay = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(argvDisplay), TRUE);
    gtk_label_set_xalign(GTK_LABEL(argvDisplay), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(argvDisplay), TRUE);
    gtk_grid_attach(GTK_GRID(grid), argvDisplay, 1, line++, 1, 1);

    ArgvReadout readout;
    readout.commandEntry = commandEntry;
    readout.display = argvDisplay;
    g_signal_connect(commandEntry, "changed",
                     G_CALLBACK(&onCommandChanged), &readout);
    onCommandChanged(GTK_EDITABLE(commandEntry), &readout);

    GtkWidget* categoryLabel = gtk_label_new("Category");
    gtk_widget_set_halign(categoryLabel, GTK_ALIGN_START);
    // WITH an entry: D-12 asks for a dropdown of what the window manager shows
    // AND free text, because a user inventing a category is how a category
    // comes to exist at all.
    GtkWidget* categoryBox = gtk_combo_box_text_new_with_entry();
    const std::vector<std::string> offered = categories();
    for (const std::string& category : offered) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(categoryBox),
                                       category.c_str());
    }
    GtkWidget* categoryEntry = gtk_bin_get_child(GTK_BIN(categoryBox));
    gtk_entry_set_text(GTK_ENTRY(categoryEntry),
                       draft.category.empty() ? "Custom" : draft.category.c_str());
    gtk_entry_set_activates_default(GTK_ENTRY(categoryEntry), TRUE);
    gtk_grid_attach(GTK_GRID(grid), categoryLabel, 0, line, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), categoryBox,   1, line++, 1, 1);

    if (!m_haveWmCategories) {
        GtkWidget* why = gtk_label_new(kMenuCategoryFileOnlyReason);
        gtk_label_set_line_wrap(GTK_LABEL(why), TRUE);
        gtk_label_set_xalign(GTK_LABEL(why), 0.0f);
        gtk_widget_set_sensitive(why, FALSE);
        gtk_grid_attach(GTK_GRID(grid), why, 1, line++, 1, 1);
    }

    gtk_widget_show_all(dialog);

    bool accepted = false;
    for (;;) {
        const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
        if (response != GTK_RESPONSE_ACCEPT) break;

        MenuEntryDraft entered;
        entered.name = gtk_entry_get_text(GTK_ENTRY(nameEntry));
        entered.command = gtk_entry_get_text(GTK_ENTRY(commandEntry));
        entered.category = gtk_entry_get_text(GTK_ENTRY(categoryEntry));

        std::string reason;
        if (!entered.complete(reason)) {
            // Said in the dialog rather than in the window's status line: the
            // dialog is modal, so the status line is behind it.
            gtk_label_set_text(GTK_LABEL(argvDisplay), reason.c_str());
            continue;
        }

        draft = entered;
        accepted = true;
        break;
    }

    gtk_widget_destroy(dialog);
    return accepted;
}


void MenuPage::add()
{
    MenuEntryDraft draft;
    draft.category = "Custom";        // D-07's default, offered rather than assumed
    if (!runEntryDialog("Add a menu entry", draft)) return;

    std::vector<AppEntry> next = rows();
    next.push_back(draft.toEntry());
    commitRows(next);
}


void MenuPage::edit()
{
    const int row = selectedRow();
    if (row < 0 || row >= static_cast<int>(rows().size())) return;

    // The row is captured BY VALUE before the dialog runs. gtk_dialog_run()
    // spins a nested main loop, so the socket source keeps firing and a reload
    // notice can move the list underneath -- after which `row` is an index into
    // a list that no longer exists. The entry is found again by identity below.
    const AppEntry original = rows()[row];

    MenuEntryDraft draft = MenuEntryDraft::fromEntry(original);
    if (!runEntryDialog("Edit a menu entry", draft)) return;

    std::vector<AppEntry> next = rows();
    bool replaced = false;
    for (AppEntry& candidate : next) {
        if (candidate.name != original.name ||
            candidate.category != original.category ||
            candidate.execArgv != original.execArgv) {
            continue;
        }
        candidate = draft.toEntry();
        replaced = true;
        break;
    }
    if (!replaced) {
        // The row this edit began on is gone -- a reload replaced the list
        // while the dialog was open. Appending is the only honest outcome:
        // silently dropping what the user typed would be worse, and guessing
        // which of the new rows they meant would be a guess.
        next.push_back(draft.toEntry());
        if (m_onStatus) {
            m_onStatus("The entry list changed while that dialog was open, so "
                       "this entry was added rather than replacing the row you "
                       "started from.");
        }
    }
    commitRows(next);
}


void MenuPage::remove()
{
    const int row = selectedRow();
    if (row < 0 || row >= static_cast<int>(rows().size())) return;

    std::vector<AppEntry> next = rows();
    next.erase(next.begin() + row);
    commitRows(next);
}


void MenuPage::resetAll()
{
    // One code path with the per-setting reset, through the model (D-13).
    const std::vector<std::string> moved =
        m_form.requestResetAll({kMenuEntriesKey});
    refreshFromForm();
    for (const std::string& key : moved) {
        if (m_onCommit) m_onCommit(key, m_form.value(key));
    }
}


// --- The static callbacks (09-RESEARCH.md Pattern 1) -------------------------

void MenuPage::onAddClicked(GtkButton*, gpointer userData)
{
    static_cast<MenuPage*>(userData)->add();
}

void MenuPage::onEditClicked(GtkButton*, gpointer userData)
{
    static_cast<MenuPage*>(userData)->edit();
}

void MenuPage::onRemoveClicked(GtkButton*, gpointer userData)
{
    static_cast<MenuPage*>(userData)->remove();
}

void MenuPage::onResetAllClicked(GtkButton*, gpointer userData)
{
    static_cast<MenuPage*>(userData)->resetAll();
}

void MenuPage::onRowActivated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*,
                              gpointer userData)
{
    static_cast<MenuPage*>(userData)->edit();
}

void MenuPage::onSelectionChanged(GtkTreeSelection*, gpointer userData)
{
    static_cast<MenuPage*>(userData)->updateButtonSensitivity();
}
