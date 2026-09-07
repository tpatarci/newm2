#include "AppearancePage.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>


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


bool configCanonicalColour(const std::string& spelling, std::string& out)
{
    // THE COMPOSITION IS THE POINT (X1). Parse with the toolkit's grammar,
    // which is the broader of the two, and then spell the result with the
    // config file's -- so every colour that reaches the form, the running
    // window manager and the user's file is one XParseColor can read, whatever
    // the user typed. The two halves are the same two functions the chooser
    // path uses, in the same order, which is what keeps the button and the raw
    // field from writing different files for the same colour.
    GdkRGBA rgba;
    if (!rgbaFromConfigColour(spelling, rgba)) return false;
    out = configColourFromRgba(rgba);
    return true;
}


// =============================================================================
// The font vocabularies
// =============================================================================

namespace {

// Split "a:b:c" into its parts, empty parts dropped.
std::vector<std::string> splitOnColon(const std::string& text)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t colon = text.find(':', start);
        const std::string part = (colon == std::string::npos)
                                     ? text.substr(start)
                                     : text.substr(start, colon - start);
        if (!part.empty()) parts.push_back(part);
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    return parts;
}

bool equalsIgnoringCase(const std::string& a, const char* b)
{
    return g_ascii_strcasecmp(a.c_str(), b) == 0;
}

}  // namespace


std::string fontDescriptionFromConfigPattern(const std::string& pattern)
{
    const std::vector<std::string> parts = splitOnColon(pattern);
    if (parts.empty()) return std::string();

    // The first element is the family LIST; Pango takes one family, so the
    // chooser is shown the first. The rest of the chain is not lost -- it stays
    // in the raw field, which is what is saved and sent.
    std::string families = parts[0];
    const std::size_t comma = families.find(',');
    std::string family = (comma == std::string::npos) ? families
                                                      : families.substr(0, comma);

    std::string style;
    std::string size;
    for (std::size_t i = 1; i < parts.size(); ++i) {
        const std::string& token = parts[i];
        if (equalsIgnoringCase(token, "bold")) { style += " Bold"; continue; }
        if (equalsIgnoringCase(token, "italic") ||
            equalsIgnoringCase(token, "oblique")) { style += " Italic"; continue; }
        if (token.rfind("size=", 0) == 0)      { size = token.substr(5); continue; }
        if (token.rfind("pixelsize=", 0) == 0) { size = token.substr(10); continue; }
        // Anything else -- weight=200, slant=100, a foundry -- has no Pango
        // description spelling, and inventing one would misrepresent the
        // pattern. It is dropped from the CHOOSER only; the raw field still
        // carries it verbatim.
    }

    std::string description = family + style;
    if (!size.empty()) description += " " + size;
    return description;
}


std::string configPatternFromFontDescription(const std::string& description)
{
    PangoFontDescription* desc = pango_font_description_from_string(description.c_str());
    if (!desc) return std::string();

    const char* family = pango_font_description_get_family(desc);
    std::string pattern = family ? family : "Sans";

    if (pango_font_description_get_weight(desc) >= PANGO_WEIGHT_BOLD) {
        pattern += ":bold";
    }
    if (pango_font_description_get_style(desc) != PANGO_STYLE_NORMAL) {
        pattern += ":italic";
    }

    const gint size = pango_font_description_get_size(desc);
    if (size > 0) {
        // Points, which is what the config file's size= means; an absolute
        // (pixel) size is converted rather than emitted as a point size it is
        // not.
        const int points = pango_font_description_get_size_is_absolute(desc)
                               ? static_cast<int>(size / PANGO_SCALE * 72.0 / 96.0 + 0.5)
                               : static_cast<int>(size / PANGO_SCALE);
        if (points > 0) pattern += ":size=" + std::to_string(points);
    }

    pango_font_description_free(desc);
    return pattern;
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
    // A PERSISTENT scrollbar, not GTK's overlay one. The page is taller than
    // the window can be on a 1024x768 VNC session -- the display this project
    // is actually used on -- so there is always more below the fold, and an
    // overlay bar that only appears once the pointer is already moving makes
    // that look like a page which simply ends. The one thing a settings window
    // must never do is hide a setting.
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

    GtkWidget* textHeading = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(textHeading), "<b>Text and size</b>");
    gtk_widget_set_halign(textHeading, GTK_ALIGN_START);
    gtk_widget_set_margin_top(textHeading, 12);
    gtk_grid_attach(GTK_GRID(grid), textHeading, 0, line++, 4, 1);

    addFontRow(grid, line++, "tab-font",  "Tab label font");
    addFontRow(grid, line++, "menu-font", "Root menu font");
    addThicknessRow(grid, line++, "frame-thickness", "Frame thickness");

    // D-13's per-page half, built from the same per-setting reset each row's
    // arrow performs: one meaning of reset, one code path.
    GtkWidget* resetAll = gtk_button_new_with_label("Reset this page");
    gtk_widget_set_halign(resetAll, GTK_ALIGN_START);
    gtk_widget_set_margin_top(resetAll, 18);
    gtk_widget_set_tooltip_text(resetAll,
                                "Put every setting on this page back to the "
                                "default. Saving then removes them from your "
                                "configuration file rather than writing the "
                                "defaults into it.");
    g_signal_connect(resetAll, "clicked",
                     G_CALLBACK(&AppearancePage::onResetAllClicked), this);
    gtk_grid_attach(GTK_GRID(grid), resetAll, 0, line++, 2, 1);

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
    row->labelText = label;

    GtkWidget* name = gtk_label_new(label.c_str());
    gtk_widget_set_halign(name, GTK_ALIGN_START);
    row->label = name;

    row->chooser = gtk_color_button_new();
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(row->chooser), FALSE);
    // A SWATCH, not a bar. Left to itself the button expands to the grid
    // column's width and reads as a coloured banner rather than as something
    // to press; at this size it reads as the sample it is, and the label
    // beside it keeps its own column.
    gtk_widget_set_size_request(row->chooser, 72, -1);
    gtk_widget_set_halign(row->chooser, GTK_ALIGN_START);
    gtk_widget_set_valign(row->chooser, GTK_ALIGN_CENTER);
    gtk_color_button_set_title(GTK_COLOR_BUTTON(row->chooser), label.c_str());
    gtk_widget_set_tooltip_text(row->chooser,
                                "Pick a colour. The change reaches a running "
                                "desktop at once; nothing is written to a file "
                                "until you press Save.");

    row->raw = addRawField(row);
    row->reset = addResetButton(row);

    gtk_grid_attach(GTK_GRID(grid), name,         0, line, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), row->chooser, 1, line, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), row->raw,     2, line, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), row->reset,   3, line, 1, 1);

    g_signal_connect(row->chooser, "color-set",
                     G_CALLBACK(&AppearancePage::onColourSet), row);

    m_rows.push_back(row);
    m_keys.push_back(row->key);
}


// The config-file spelling, shown and EDITABLE beside every chooser (D-10), and
// the reset affordance beside that (D-13). Built once here rather than per row
// type, because a colour and a font differ in what they mean, not in how the
// user is offered them.
GtkWidget* AppearancePage::addRawField(Row* row)
{
    GtkWidget* raw = gtk_entry_new();
    const bool wide = (row->kind == Kind::Font);
    gtk_entry_set_width_chars(GTK_ENTRY(raw), wide ? 26 : 10);
    gtk_entry_set_max_width_chars(GTK_ENTRY(raw), wide ? 34 : 12);
    gtk_widget_set_hexpand(raw, FALSE);
    // Left-aligned and no wider than its content needs. D-10 asks for the
    // config-file spelling to be readable BESIDE the chooser and clearly
    // secondary to it; a field stretched across the window would be neither.
    gtk_widget_set_halign(raw, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(raw), "monospace");

    g_signal_connect(raw, "activate",
                     G_CALLBACK(&AppearancePage::onRawActivate), row);
    g_signal_connect(raw, "focus-out-event",
                     G_CALLBACK(&AppearancePage::onRawFocusOut), row);
    return raw;
}


GtkWidget* AppearancePage::addResetButton(Row* row)
{
    // "Put this back", not "delete this". The undo arrow and the wording both
    // say restore; what it actually does is remove the line from the user's
    // file, which is what lets a system-wide value show through again.
    GtkWidget* reset = gtk_button_new_from_icon_name("edit-undo-symbolic",
                                                     GTK_ICON_SIZE_BUTTON);
    gtk_button_set_relief(GTK_BUTTON(reset), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(reset,
                                "Put this setting back to the default. Saving "
                                "then removes it from your configuration file "
                                "rather than writing the default into it.");
    g_signal_connect(reset, "clicked",
                     G_CALLBACK(&AppearancePage::onResetClicked), row);
    return reset;
}


void AppearancePage::addFontRow(GtkWidget* grid, int line,
                                const std::string& key,
                                const std::string& label)
{
    Row* row = new Row();
    row->key = key;
    row->kind = Kind::Font;
    row->owner = this;
    row->labelText = label;

    GtkWidget* name = gtk_label_new(label.c_str());
    gtk_widget_set_halign(name, GTK_ALIGN_START);
    row->label = name;

    row->chooser = gtk_font_button_new();
    gtk_font_chooser_set_level(GTK_FONT_CHOOSER(row->chooser),
                               static_cast<GtkFontChooserLevel>(
                                   GTK_FONT_CHOOSER_LEVEL_FAMILY |
                                   GTK_FONT_CHOOSER_LEVEL_STYLE |
                                   GTK_FONT_CHOOSER_LEVEL_SIZE));
    gtk_font_button_set_title(GTK_FONT_BUTTON(row->chooser), label.c_str());
    gtk_widget_set_halign(row->chooser, GTK_ALIGN_START);
    gtk_widget_set_valign(row->chooser, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(row->chooser,
                                "Pick a font. The change reaches a running "
                                "desktop at once; nothing is written to a file "
                                "until you press Save.");

    row->raw = addRawField(row);
    row->reset = addResetButton(row);

    gtk_grid_attach(GTK_GRID(grid), name,         0, line, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), row->chooser, 1, line, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), row->raw,     2, line, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), row->reset,   3, line, 1, 1);

    g_signal_connect(row->chooser, "font-set",
                     G_CALLBACK(&AppearancePage::onFontSet), row);

    m_rows.push_back(row);
    m_keys.push_back(row->key);
}


void AppearancePage::addThicknessRow(GtkWidget* grid, int line,
                                     const std::string& key,
                                     const std::string& label)
{
    Row* row = new Row();
    row->key = key;
    row->kind = Kind::Thickness;
    row->owner = this;
    row->labelText = label;

    GtkWidget* name = gtk_label_new(label.c_str());
    gtk_widget_set_halign(name, GTK_ALIGN_START);
    row->label = name;

    // The range is the PARSER'S range, read from the same key table the window
    // manager validates against, so the control cannot ask for a value the
    // window manager will refuse. Spelling 1 and 50 here would be a third copy
    // of a bound that already exists twice.
    const ConfigKeySpec* spec = configKeySpecFor(key);
    const double lo = spec ? spec->minValue : 1;
    const double hi = spec ? spec->maxValue : 50;

    row->chooser = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, lo, hi, 1);
    gtk_scale_set_digits(GTK_SCALE(row->chooser), 0);
    gtk_scale_set_draw_value(GTK_SCALE(row->chooser), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(row->chooser), GTK_POS_RIGHT);
    gtk_widget_set_hexpand(row->chooser, TRUE);
    // Stored as well as set, because renderRow() appends the origin line to it
    // and would otherwise replace it (A3).
    row->baseTooltip =
        "How thick a window's frame is, in pixels. "
        "Released, the change reaches a running desktop "
        "at once and every open window is re-framed.";
    gtk_widget_set_tooltip_text(row->chooser, row->baseTooltip.c_str());

    row->reset = addResetButton(row);

    gtk_grid_attach(GTK_GRID(grid), name,         0, line, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), row->chooser, 1, line, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), row->reset,   3, line, 1, 1);

    // D-05 says a slider commits on RELEASE, not on every pixel of a drag: a
    // frame thickness applied continuously would re-frame every window on
    // screen dozens of times per drag.
    gtk_widget_add_events(row->chooser, GDK_BUTTON_RELEASE_MASK | GDK_KEY_RELEASE_MASK);
    g_signal_connect(row->chooser, "button-release-event",
                     G_CALLBACK(&AppearancePage::onScaleReleased), row);
    g_signal_connect(row->chooser, "key-release-event",
                     G_CALLBACK(&AppearancePage::onScaleReleased), row);

    m_rows.push_back(row);
    m_keys.push_back(row->key);
}


void AppearancePage::renderRow(Row& row)
{
    const FormField* field = m_form.field(row.key);
    if (!field) return;

    m_updating = true;

    if (row.raw) gtk_entry_set_text(GTK_ENTRY(row.raw), field->current.c_str());

    switch (row.kind) {
    case Kind::Colour: {
        GdkRGBA rgba;
        if (rgbaFromConfigColour(field->current, rgba)) {
            gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(row.chooser), &rgba);
        }
        break;
    }
    case Kind::Font: {
        const std::string description =
            fontDescriptionFromConfigPattern(field->current);
        if (!description.empty()) {
            gtk_font_chooser_set_font(GTK_FONT_CHOOSER(row.chooser),
                                      description.c_str());
        }
        break;
    }
    case Kind::Thickness: {
        // The parser's own clamp, applied to what the model holds rather than
        // trusted: a file can contain anything, and a range widget handed a
        // value outside its bounds silently takes a bound instead.
        const int value = std::atoi(field->current.c_str());
        gtk_range_set_value(GTK_RANGE(row.chooser), static_cast<double>(value));
        break;
    }
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
    if (row.raw) {
        const std::string tip =
            "The spelling that goes into the configuration file. Type one here "
            "and it is the same as choosing it.\n" + origin;
        gtk_widget_set_tooltip_text(row.raw, tip.c_str());
    } else {
        // APPENDED, not substituted: this control carries its own sentence and
        // gtk_widget_set_tooltip_text() replaces whatever is there, so the row
        // lost the explanation it was built with the first time it rendered
        // (A3).
        const std::string tip = row.baseTooltip.empty()
                                    ? origin
                                    : row.baseTooltip + "\n" + origin;
        gtk_widget_set_tooltip_text(row.chooser, tip.c_str());
    }

    markRow(row, field->staleUnderEdit);

    m_updating = false;
}


void AppearancePage::markRow(Row& row, bool stale)
{
    if (!row.label || !GTK_IS_LABEL(row.label)) return;

    if (stale) {
        char* markup = g_markup_printf_escaped("<i>%s</i>", row.labelText.c_str());
        gtk_label_set_markup(GTK_LABEL(row.label), markup);
        g_free(markup);
        gtk_widget_set_tooltip_text(
            row.label,
            "The configuration changed elsewhere while you were editing this. "
            "Your unsaved value is still here: Save keeps it, Revert takes "
            "what the file says.");
        return;
    }
    gtk_label_set_text(GTK_LABEL(row.label), row.labelText.c_str());
    gtk_widget_set_tooltip_text(row.label, nullptr);
}


void AppearancePage::resetAll()
{
    const std::vector<std::string> moved = m_form.requestResetAll(m_keys);
    refreshFromForm();
    // Every setting that moved reaches the desktop at once, exactly as a single
    // reset does; the REMOVAL of the lines happens at the next Save (D-13).
    for (const std::string& key : moved) {
        if (m_onCommit) m_onCommit(key, m_form.value(key));
    }
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

    if (row->kind == Kind::Colour) {
        std::string canonical;
        if (!configCanonicalColour(typed, canonical)) {
            // Put back what is actually in force rather than leaving a value on
            // screen that nothing is drawn in. The window manager would refuse
            // this too; refusing it here means the desktop never even flickers.
            if (m_onStatus) {
                m_onStatus("'" + typed + "' is not a colour this program "
                           "understands -- try a spelling like #C8CACC.");
            }
            renderRow(*row);
            return;
        }
        // A COLOUR IS THE ONE KIND COMMITTED RE-SPELLED (X1), and the exception
        // to the rule stated below. The toolkit's grammar is broader than
        // XParseColor's -- it reads CSS, and the X server reads none of it --
        // so a value taken as typed here could be one the running window
        // manager refuses, the form keeps anyway, and Save writes into the
        // user's file, where the next startup hands it to
        // Border::allocateXftColors() and the desktop does not come up.
        // Canonicalising means the form, the desktop and the file always hold a
        // spelling the server can parse, and always the same one.
        commit(*row, canonical);
        return;
    } else if (row->kind == Kind::Font) {
        if (typed.empty()) {
            if (m_onStatus) m_onStatus("A font pattern cannot be empty.");
            renderRow(*row);
            return;
        }
        // No further judgement here. Whether a PATTERN resolves to a usable
        // face is fontconfig's question and the window manager's to answer --
        // it refuses a font with no face and keeps the one it has (plan 09-05)
        // -- and a second opinion in this process could only ever disagree
        // with the first.
    }

    // The value is sent AS TYPED, not as the chooser would re-spell it: a user
    // who wrote a fallback chain of four families meant exactly that, and
    // re-spelling it would make their file disagree with what they entered.
    // The colour arm above is the one exception, and says why.
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


void AppearancePage::onFontSet(GtkFontButton* button, gpointer userData)
{
    Row* row = static_cast<Row*>(userData);
    if (!row || !row->owner || row->owner->m_updating) return;

    // THE CHOOSER INTERFACE METHOD. The button-specific font-name getter
    // predates GtkFontChooser and has been deprecated since GTK 3.22
    // (09-RESEARCH.md Pitfall 5); it still WORKS on the 3.24 both Ubuntu
    // targets ship, so nothing at runtime would notice it, which is why a
    // [wm2_config_smoke] case reads this file and fails if its name appears.
    //
    // That case is why the deprecated spelling is described here rather than
    // written out: the project already learned (CMakeLists.txt, D-33) that a
    // comment quoting the token a grep-shaped guard forbids defeats the guard
    // it was trying to explain.
    gchar* description = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(button));
    if (!description) return;

    const std::string pattern = configPatternFromFontDescription(description);
    g_free(description);
    if (pattern.empty()) return;

    row->owner->commit(*row, pattern);
}


gboolean AppearancePage::onScaleReleased(GtkWidget* scale, GdkEvent*,
                                         gpointer userData)
{
    Row* row = static_cast<Row*>(userData);
    if (row && row->owner && !row->owner->m_updating) {
        const int value =
            static_cast<int>(gtk_range_get_value(GTK_RANGE(scale)) + 0.5);
        row->owner->commit(*row, std::to_string(value));
    }
    return GDK_EVENT_PROPAGATE;
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


void AppearancePage::onResetAllClicked(GtkButton*, gpointer userData)
{
    static_cast<AppearancePage*>(userData)->resetAll();
}
