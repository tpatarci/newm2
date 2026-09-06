#include "AppearancePage.h"

#include <cstdio>


// =============================================================================
// The colour vocabularies
// =============================================================================

std::string configColourFromRgba(const GdkRGBA& rgba)
{
    auto channel = [](double v) {
        if (v < 0.0) v = 0.0;
        if (v > 1.0) v = 1.0;
        return static_cast<int>(v * 255.0 + 0.5);
    };
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X",
                  channel(rgba.red), channel(rgba.green), channel(rgba.blue));
    return std::string(buffer);
}


bool rgbaFromConfigColour(const std::string& spelling, GdkRGBA& out)
{
    GdkRGBA parsed;
    if (!gdk_rgba_parse(&parsed, spelling.c_str())) return false;
    parsed.alpha = 1.0;
    out = parsed;
    return true;
}


// =============================================================================
// The page
// =============================================================================

AppearancePage::AppearancePage(FormState& form, CommitHandler onCommit,
                               StatusHandler onStatus)
    : m_form(form), m_onCommit(std::move(onCommit)), m_onStatus(std::move(onStatus))
{
    m_root = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m_root),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_margin_top(grid, 12);
    gtk_widget_set_margin_bottom(grid, 12);
    gtk_widget_set_margin_start(grid, 12);
    gtk_widget_set_margin_end(grid, 12);
    gtk_container_add(GTK_CONTAINER(m_root), grid);

    int line = 0;

    // D-09 assigns the nine colours to this page, in the two groups the frame
    // and the menu form -- the same grouping include/Config.h uses, so somebody
    // reading the file and somebody reading the window see the same shape.
    GtkWidget* windowsHeading = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(windowsHeading), "<b>Window frames</b>");
    gtk_widget_set_halign(windowsHeading, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), windowsHeading, 0, line++, 4, 1);

    addColourRow(grid, line++, "tab-foreground",   "Tab label");
    addColourRow(grid, line++, "tab-background",   "Tab background");
    addColourRow(grid, line++, "frame-background", "Frame");
    addColourRow(grid, line++, "button-background", "Tab button");
    addColourRow(grid, line++, "borders",          "Borders");

    GtkWidget* menuHeading = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(menuHeading), "<b>Root menu</b>");
    gtk_widget_set_halign(menuHeading, GTK_ALIGN_START);
    gtk_widget_set_margin_top(menuHeading, 12);
    gtk_grid_attach(GTK_GRID(grid), menuHeading, 0, line++, 4, 1);

    addColourRow(grid, line++, "menu-foreground", "Menu text");
    addColourRow(grid, line++, "menu-background", "Menu background");
    addColourRow(grid, line++, "menu-highlight",  "Selected row");
    addColourRow(grid, line++, "menu-borders",    "Menu border");

    refreshFromForm();
}


AppearancePage::~AppearancePage()
{
    // The widgets belong to the container hierarchy GTK owns; these small
    // structs do not, and they outlive nothing.
    for (Row* row : m_rows) delete row;
    m_rows.clear();
}


void AppearancePage::addColourRow(GtkWidget* grid, int line,
                                  const std::string& key,
                                  const std::string& label)
{
    Row* row = new Row();
    row->key = key;
    row->kind = Kind::Colour;
    row->owner = this;

    GtkWidget* name = gtk_label_new(label.c_str());
    gtk_widget_set_halign(name, GTK_ALIGN_START);

    row->chooser = gtk_color_button_new();
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(row->chooser), FALSE);
    gtk_color_button_set_title(GTK_COLOR_BUTTON(row->chooser), label.c_str());
    gtk_widget_set_tooltip_text(row->chooser,
                                "Pick a colour. The change reaches a running "
                                "desktop at once; nothing is written to a file "
                                "until you press Save.");

    // D-10: the config-file spelling, shown and EDITABLE beside the chooser, so
    // a power user can paste a value and a non-programmer never has to see one.
    // Narrow and monospaced so it reads as secondary to the chooser rather than
    // as competing with it.
    row->raw = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(row->raw), 10);
    gtk_entry_set_max_width_chars(GTK_ENTRY(row->raw), 12);
    gtk_widget_set_hexpand(row->raw, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(row->raw), "monospace");

    // D-13: "put this back", not "delete this". The undo arrow and the wording
    // both say restore; what it actually does is remove the line from the
    // user's file, which is what lets a system-wide value show through again.
    row->reset = gtk_button_new_from_icon_name("edit-undo-symbolic",
                                               GTK_ICON_SIZE_BUTTON);
    gtk_button_set_relief(GTK_BUTTON(row->reset), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(row->reset,
                                "Put this setting back to the default. Saving "
                                "then removes it from your configuration file "
                                "rather than writing the default into it.");

    gtk_grid_attach(GTK_GRID(grid), name,         0, line, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), row->chooser, 1, line, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), row->raw,     2, line, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), row->reset,   3, line, 1, 1);

    g_signal_connect(row->chooser, "color-set",
                     G_CALLBACK(&AppearancePage::onColourSet), row);
    g_signal_connect(row->raw, "activate",
                     G_CALLBACK(&AppearancePage::onRawActivate), row);
    g_signal_connect(row->raw, "focus-out-event",
                     G_CALLBACK(&AppearancePage::onRawFocusOut), row);
    g_signal_connect(row->reset, "clicked",
                     G_CALLBACK(&AppearancePage::onResetClicked), row);

    m_rows.push_back(row);
}


void AppearancePage::renderRow(Row& row)
{
    const FormField* field = m_form.field(row.key);
    if (!field) return;

    m_updating = true;

    gtk_entry_set_text(GTK_ENTRY(row.raw), field->current.c_str());

    GdkRGBA rgba;
    if (rgbaFromConfigColour(field->current, rgba)) {
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(row.chooser), &rgba);
    }

    // DISC-08: the raw field's tooltip names the layer the value came from, so
    // a key that is set only system-wide explains itself rather than looking
    // like something this user chose.
    std::string origin;
    switch (field->source) {
    case ValueSource::BuiltIn:
        origin = "The built-in default; nothing sets this.";
        break;
    case ValueSource::SystemFile:
        origin = "Set system-wide, in " + field->sourceDetail +
                 ". Editing it writes an override into your own file; "
                 "resetting it lets the system value show through again.";
        break;
    case ValueSource::UserFile:
        origin = "Set in your own configuration file, " + field->sourceDetail + ".";
        break;
    case ValueSource::WindowManager:
        origin = "In use by the running window manager.";
        break;
    }
    const std::string tip =
        "The spelling that goes into the configuration file. Type one here and "
        "it is the same as choosing it.\n" + origin;
    gtk_widget_set_tooltip_text(row.raw, tip.c_str());

    m_updating = false;
}


void AppearancePage::refreshFromForm()
{
    for (Row* row : m_rows) renderRow(*row);
}


void AppearancePage::commit(Row& row, const std::string& value)
{
    // setValue() answers false for a value the form is already showing, which
    // is what stops GTK's own idempotent "color-set" (emitted when the user
    // re-picks the colour that was already selected) from sending a redundant
    // message to the window manager.
    if (!m_form.setValue(row.key, value)) return;

    renderRow(row);
    if (m_onCommit) m_onCommit(row.key, value);
}


void AppearancePage::reset(Row& row)
{
    if (!m_form.requestReset(row.key)) return;

    renderRow(row);
    // The desktop follows the form immediately, exactly as an edit does: the
    // value the reset produced is in force at once, and the REMOVAL of the line
    // happens at the next Save (D-05 and D-13 are independent).
    if (m_onCommit) m_onCommit(row.key, m_form.value(row.key));
}


void AppearancePage::commitRawField(GtkWidget* entry)
{
    Row* row = rowFor(entry);
    if (!row) return;

    const std::string typed = gtk_entry_get_text(GTK_ENTRY(entry));

    GdkRGBA rgba;
    if (!rgbaFromConfigColour(typed, rgba)) {
        // Put back what is actually in force rather than leaving a value on
        // screen that nothing is drawn in. The window manager would refuse this
        // too; refusing it here means the desktop never even flickers.
        if (m_onStatus) {
            m_onStatus("'" + typed + "' is not a colour this program understands "
                       "-- try a spelling like #C8CACC.");
        }
        renderRow(*row);
        return;
    }

    // The value is sent AS TYPED, not as the chooser would re-spell it: a user
    // who wrote a colour name meant that name, and re-spelling it would make
    // their file disagree with what they entered.
    commit(*row, typed);
}


AppearancePage::Row* AppearancePage::rowFor(GtkWidget* widget)
{
    for (Row* row : m_rows) {
        if (row->chooser == widget || row->raw == widget || row->reset == widget) {
            return row;
        }
    }
    return nullptr;
}


// --- The static callbacks (Pattern 1) ---------------------------------------
//
// Each carries its Row through user_data, and the Row carries its owner. A
// lambda would need the same indirection with less to read.

void AppearancePage::onColourSet(GtkColorButton* button, gpointer userData)
{
    Row* row = static_cast<Row*>(userData);
    if (!row || !row->owner || row->owner->m_updating) return;

    GdkRGBA rgba;
    // The CHOOSER INTERFACE method, not the button's own getter: same reason
    // the font control uses gtk_font_chooser_get_font() below -- the
    // button-specific accessors predate the interface and are deprecated.
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
    row->owner->commit(*row, configColourFromRgba(rgba));
}


void AppearancePage::onRawActivate(GtkEntry* entry, gpointer userData)
{
    Row* row = static_cast<Row*>(userData);
    if (!row || !row->owner || row->owner->m_updating) return;
    row->owner->commitRawField(GTK_WIDGET(entry));
}


gboolean AppearancePage::onRawFocusOut(GtkWidget* entry, GdkEvent*,
                                       gpointer userData)
{
    Row* row = static_cast<Row*>(userData);
    if (row && row->owner && !row->owner->m_updating) {
        row->owner->commitRawField(entry);
    }
    return GDK_EVENT_PROPAGATE;   // never swallow a focus change
}


void AppearancePage::onResetClicked(GtkButton*, gpointer userData)
{
    Row* row = static_cast<Row*>(userData);
    if (!row || !row->owner || row->owner->m_updating) return;
    row->owner->reset(*row);
}
