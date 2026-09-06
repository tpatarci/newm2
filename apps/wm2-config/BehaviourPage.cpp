#include "BehaviourPage.h"

#include <cstdlib>
#include <string>


// =============================================================================
// Wording
// =============================================================================

std::string behaviourTooltipFor(const std::string& key, const std::string& extra)
{
    // THE BINARY'S OWN SENTENCE FIRST. configKeySpecFor() is a view of the very
    // option table `--help` is generated from, so the tooltip and the usage
    // text are one string with two readers rather than two strings that drift.
    const ConfigKeySpec* spec = configKeySpecFor(key);
    std::string tip = spec ? spec->summary : std::string();
    if (!tip.empty()) {
        tip[0] = static_cast<char>(g_ascii_toupper(tip[0]));
        tip += ".";
    }
    if (!extra.empty()) {
        if (!tip.empty()) tip += "\n";
        tip += extra;
    }
    return tip;
}


namespace {

// The config file's spelling of a boolean, which is what the window manager's
// strict parser accepts over the socket -- not "1"/"0", which the FILE tolerates
// and a `set` deliberately does not.
const char* boolSpelling(bool on) { return on ? "true" : "false"; }

bool spellingIsTrue(const std::string& value)
{
    return value == "true" || value == "1" || value == "yes" || value == "on";
}

}  // namespace


// =============================================================================
// The page
// =============================================================================

BehaviourPage::BehaviourPage(FormState& form, CommitHandler onCommit,
                             StatusHandler onStatus)
    : m_form(form), m_onCommit(std::move(onCommit)), m_onStatus(std::move(onStatus))
{
    m_root = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m_root),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    // A PERSISTENT scrollbar, for the reason the Appearance page gives: on the
    // 1024x768 VNC session this project is used from there is more below the
    // fold, and a bar that appears only once the pointer is already moving
    // makes that look like a page which simply ends.
    gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(m_root), FALSE);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_margin_top(grid, 12);
    gtk_widget_set_margin_bottom(grid, 12);
    gtk_widget_set_margin_start(grid, 12);
    gtk_widget_set_margin_end(grid, 12);
    gtk_container_add(GTK_CONTAINER(m_root), grid);

    int line = 0;

    GtkWidget* focusHeading = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(focusHeading), "<b>Focus</b>");
    gtk_widget_set_halign(focusHeading, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), focusHeading, 0, line++, 4, 1);

    addBooleanRow(grid, line++, "click-to-focus",
                  "Click a window to focus it",
                  "Off, focus follows the pointer: whatever is under the "
                  "pointer receives what you type.");
    addBooleanRow(grid, line++, "raise-on-focus",
                  "Bring a window to the front when it takes focus",
                  "Off, a window can hold focus while another stays in front "
                  "of it.");
    addBooleanRow(grid, line++, "auto-raise",
                  "Bring the window under a resting pointer to the front",
                  "Uses the two delays below: how long the pointer must be "
                  "still, and how long it must then rest.");
    addBooleanRow(grid, line++, "focus-stealing-prevention",
                  "Do not let a window that opens by itself take focus",
                  "Leave this on unless an older program you rely on keeps "
                  "opening windows you then have to click.");

    GtkWidget* timingHeading = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(timingHeading), "<b>Timing</b>");
    gtk_widget_set_halign(timingHeading, GTK_ALIGN_START);
    gtk_widget_set_margin_top(timingHeading, 12);
    gtk_grid_attach(GTK_GRID(grid), timingHeading, 0, line++, 4, 1);

    addDelayRow(grid, line++, "auto-raise-delay",
                "Raise after the pointer has rested for",
                "Only used when raising under a resting pointer is on.");
    addDelayRow(grid, line++, "pointer-stopped-delay",
                "Treat the pointer as still after",
                "How long the pointer must stop moving before the delay above "
                "starts counting.");
    addDelayRow(grid, line++, "destroy-window-delay",
                "Hold the tab button this long to close a window",
                "A shorter press hides the window instead, which is always "
                "undoable from the root menu. Closing one is not.");

    GtkWidget* commandHeading = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(commandHeading), "<b>New window</b>");
    gtk_widget_set_halign(commandHeading, GTK_ALIGN_START);
    gtk_widget_set_margin_top(commandHeading, 12);
    gtk_grid_attach(GTK_GRID(grid), commandHeading, 0, line++, 4, 1);

    addCommandRow(grid, line++, "new-window-command",
                  "Command",
                  "Split on spaces and run directly, so quotes, pipes and "
                  "semicolons in it are ordinary characters -- unless the box "
                  "below is ticked.");
    // T-9-41. The label states the CONSEQUENCE rather than naming the flag: a
    // checkbox reading "exec using shell" tells a non-programmer nothing, and
    // this is the one setting on this page that changes what a string they typed
    // is allowed to do. The setting stays off by default, as it already was.
    addBooleanRow(grid, line++, "exec-using-shell",
                  "Run that command through a shell (/bin/sh -c)",
                  "With this on, characters like ; | > and ` in the command "
                  "above are interpreted by the shell rather than passed on as "
                  "text. Leave it off unless you need a pipeline.");

    // D-13's per-page half -- "Reset all" -- is added to all three pages
    // together in the same change that gives FormState the one code path they
    // share (plan 09-07 task 3), rather than three times here.

    refreshFromForm();
}


BehaviourPage::~BehaviourPage()
{
    for (Row* row : m_rows) delete row;
    m_rows.clear();
}


// "Put this back", not "delete this" -- the same affordance and the same
// wording the Appearance page uses, because it is the same operation: the line
// leaves the user's file on the next save, which is what lets a system-wide
// value show through again.
GtkWidget* BehaviourPage::addResetButton(Row* row)
{
    GtkWidget* reset = gtk_button_new_from_icon_name("edit-undo-symbolic",
                                                     GTK_ICON_SIZE_BUTTON);
    gtk_button_set_relief(GTK_BUTTON(reset), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(reset,
                                "Put this setting back to the default. Saving "
                                "then removes it from your configuration file "
                                "rather than writing the default into it.");
    g_signal_connect(reset, "clicked",
                     G_CALLBACK(&BehaviourPage::onResetClicked), row);
    return reset;
}


void BehaviourPage::addBooleanRow(GtkWidget* grid, int line,
                                  const std::string& key,
                                  const std::string& label,
                                  const std::string& extra)
{
    Row* row = new Row();
    row->key = key;
    row->kind = Kind::Boolean;
    row->labelText = label;
    row->owner = this;

    // No raw field: a boolean's config-file spelling is "true" or "false", and
    // the tick IS that spelling. A text box beside it saying `false` would be a
    // second control for one bit, and the two could disagree on screen.
    row->control = gtk_check_button_new_with_label(label.c_str());
    gtk_widget_set_halign(row->control, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(row->control,
                                behaviourTooltipFor(key, extra).c_str());
    row->reset = addResetButton(row);

    gtk_grid_attach(GTK_GRID(grid), row->control, 0, line, 3, 1);
    gtk_grid_attach(GTK_GRID(grid), row->reset,   3, line, 1, 1);

    g_signal_connect(row->control, "toggled",
                     G_CALLBACK(&BehaviourPage::onToggled), row);

    m_rows.push_back(row);
    m_keys.push_back(key);
}


void BehaviourPage::addDelayRow(GtkWidget* grid, int line,
                                const std::string& key,
                                const std::string& label,
                                const std::string& extra)
{
    Row* row = new Row();
    row->key = key;
    row->kind = Kind::Delay;
    row->labelText = label;
    row->owner = this;

    row->label = gtk_label_new(label.c_str());
    gtk_widget_set_halign(row->label, GTK_ALIGN_START);

    // THE PARSER'S OWN RANGE, read from the same key table the window manager
    // validates a `set` against, so the control cannot ask for a value the
    // window manager will refuse.
    //
    // The clamp is a CONVENIENCE AND NOT THE VALIDATION. The window manager
    // checks every set regardless (plan 09-04) and refuses what is out of
    // range; a GUI that were the only validator would be a GUI whose bugs
    // became the window manager's. A [wm2_config_smoke] case asserts the
    // refusal independently of this control.
    const ConfigKeySpec* spec = configKeySpecFor(key);
    const double lo = spec ? spec->minValue : 1;
    const double hi = spec ? spec->maxValue : 1;

    row->control = gtk_spin_button_new_with_range(lo, hi, 10);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(row->control), 0);
    // A typed value outside the range is corrected to the nearest bound rather
    // than left on screen as something that will be refused.
    gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(row->control),
                                      GTK_UPDATE_IF_VALID);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(row->control), TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(row->control), 8);
    gtk_widget_set_halign(row->control, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(row->control,
                                behaviourTooltipFor(key, extra).c_str());

    GtkWidget* unit = gtk_label_new("milliseconds");
    gtk_widget_set_halign(unit, GTK_ALIGN_START);
    gtk_widget_set_sensitive(unit, FALSE);

    row->reset = addResetButton(row);

    gtk_grid_attach(GTK_GRID(grid), row->label,   0, line, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), row->control, 1, line, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), unit,         2, line, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), row->reset,   3, line, 1, 1);

    // D-05 asks a control to commit on RELEASE rather than continuously. A spin
    // button has no drag: every change it emits is one completed adjustment --
    // an arrow pressed, or a typed value the update policy has already accepted
    // -- so "value-changed" IS the release for this control.
    g_signal_connect(row->control, "value-changed",
                     G_CALLBACK(&BehaviourPage::onSpinValueChanged), row);

    m_rows.push_back(row);
    m_keys.push_back(key);
}


void BehaviourPage::addCommandRow(GtkWidget* grid, int line,
                                  const std::string& key,
                                  const std::string& label,
                                  const std::string& extra)
{
    Row* row = new Row();
    row->key = key;
    row->kind = Kind::Command;
    row->labelText = label;
    row->owner = this;

    row->label = gtk_label_new(label.c_str());
    gtk_widget_set_halign(row->label, GTK_ALIGN_START);

    row->control = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(row->control), 28);
    gtk_widget_set_hexpand(row->control, TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(row->control),
                                "monospace");
    gtk_widget_set_tooltip_text(row->control,
                                behaviourTooltipFor(key, extra).c_str());

    row->reset = addResetButton(row);

    gtk_grid_attach(GTK_GRID(grid), row->label,   0, line, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), row->control, 1, line, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), row->reset,   3, line, 1, 1);

    // D-05: a text field commits when it is left, or when Return is pressed in
    // it. Committing per keystroke would send the window manager every prefix
    // of what the user is still typing.
    g_signal_connect(row->control, "activate",
                     G_CALLBACK(&BehaviourPage::onEntryActivate), row);
    g_signal_connect(row->control, "focus-out-event",
                     G_CALLBACK(&BehaviourPage::onEntryFocusOut), row);

    m_rows.push_back(row);
    m_keys.push_back(key);
}


void BehaviourPage::renderRow(Row& row)
{
    const FormField* field = m_form.field(row.key);
    if (!field) return;

    m_updating = true;

    switch (row.kind) {
    case Kind::Boolean:
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(row.control),
                                     spellingIsTrue(field->current) ? TRUE : FALSE);
        break;
    case Kind::Delay:
        // The model holds whatever a file contained, which can be anything; a
        // range widget handed a value outside its bounds silently takes a bound
        // instead, which is the correct outcome here and is why the value is
        // not pre-checked.
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(row.control),
                                  static_cast<double>(std::atoi(field->current.c_str())));
        break;
    case Kind::Command:
        gtk_entry_set_text(GTK_ENTRY(row.control), field->current.c_str());
        break;
    }

    m_updating = false;
}


void BehaviourPage::refreshFromForm()
{
    for (Row* row : m_rows) renderRow(*row);
}


void BehaviourPage::commit(Row& row, const std::string& value)
{
    // setValue() answers false for a value the form is already showing, which
    // is what stops an idempotent widget signal -- and a "toggled" emitted by
    // the page's own refresh -- from sending a redundant message.
    if (!m_form.setValue(row.key, value)) return;

    renderRow(row);
    if (m_onCommit) m_onCommit(row.key, value);
}


void BehaviourPage::reset(Row& row)
{
    if (!m_form.requestReset(row.key)) return;

    renderRow(row);
    // The desktop follows the form immediately, exactly as an edit does: the
    // value the reset produced is in force at once, and the REMOVAL of the line
    // happens at the next Save (D-05 and D-13 are independent).
    if (m_onCommit) m_onCommit(row.key, m_form.value(row.key));
}


// --- The static callbacks (09-RESEARCH.md Pattern 1) -------------------------

void BehaviourPage::onToggled(GtkToggleButton* button, gpointer userData)
{
    Row* row = static_cast<Row*>(userData);
    if (!row || !row->owner || row->owner->m_updating) return;
    row->owner->commit(*row, boolSpelling(gtk_toggle_button_get_active(button)));
}


void BehaviourPage::onSpinValueChanged(GtkSpinButton* spin, gpointer userData)
{
    Row* row = static_cast<Row*>(userData);
    if (!row || !row->owner || row->owner->m_updating) return;
    const int value = gtk_spin_button_get_value_as_int(spin);
    row->owner->commit(*row, std::to_string(value));
}


void BehaviourPage::onEntryActivate(GtkEntry* entry, gpointer userData)
{
    Row* row = static_cast<Row*>(userData);
    if (!row || !row->owner || row->owner->m_updating) return;
    row->owner->commit(*row, gtk_entry_get_text(entry));
}


gboolean BehaviourPage::onEntryFocusOut(GtkWidget* entry, GdkEvent*,
                                        gpointer userData)
{
    Row* row = static_cast<Row*>(userData);
    if (row && row->owner && !row->owner->m_updating) {
        row->owner->commit(*row, gtk_entry_get_text(GTK_ENTRY(entry)));
    }
    return GDK_EVENT_PROPAGATE;   // never swallow a focus change
}


void BehaviourPage::onResetClicked(GtkButton*, gpointer userData)
{
    Row* row = static_cast<Row*>(userData);
    if (!row || !row->owner || row->owner->m_updating) return;
    row->owner->reset(*row);
}
