#pragma once

// The Behaviour page: how the desktop behaves (D-09, D-05, D-13, plan 09-07).
//
// The nine settings D-09 assigns here and nothing else: the four focus-policy
// booleans, the three delays, the command the root menu's New entry runs, and
// the flag that decides whether a shell interprets that command.
//
// Built to the same shape as AppearancePage -- a control, the reset affordance
// beside it, static signal handlers taking their Row through `user_data`
// (09-RESEARCH.md Pattern 1), and an `m_updating` flag that makes every handler
// a no-op while the page is writing into its own widgets. That shape is
// repeated here rather than hoisted into a shared base class because the two
// pages differ in what their rows MEAN, not in how a row is wired, and a base
// class covering three rows of boilerplate would be read more often than it
// saved.
//
// ---------------------------------------------------------------------------
// WHERE THE WORDING COMES FROM
// ---------------------------------------------------------------------------
//
// Every control's tooltip is ASSEMBLED AT RUNTIME from the summary that the
// window manager's own option table already carries -- configKeySpecFor(key)
// ->summary, the same sentence `wm2-born-again --help` prints. So the window,
// the usage text and the release notes cannot come to describe one setting in
// three different ways: there is one sentence and three readers of it.
//
// The LABEL above each control is a short human phrase, because a summary
// written for `--help` is written for somebody who already knows what the
// setting is. A label extends its summary; it never contradicts it.

#include "FormState.h"

#include <gtk/gtk.h>

#include <functional>
#include <string>
#include <vector>


class BehaviourPage {
public:
    // A control committed a value (D-05: box ticked, spin adjusted, field
    // left). The owner sends it to the running window manager at once and
    // touches no file.
    using CommitHandler =
        std::function<void(const std::string& key, const std::string& value)>;

    // Something worth telling the user, for the window's status line.
    using StatusHandler = std::function<void(const std::string& message)>;

    BehaviourPage(FormState& form, CommitHandler onCommit, StatusHandler onStatus);
    ~BehaviourPage();

    BehaviourPage(const BehaviourPage&) = delete;
    BehaviourPage& operator=(const BehaviourPage&) = delete;

    // The page's root widget, owned by the notebook it is added to.
    GtkWidget* widget() const { return m_root; }

    // Re-render every control from the model: after a revert, after a save, and
    // when a reload notice moves the effective values underneath an open
    // window (D-08).
    void refreshFromForm();

    // D-13's per-page half: every setting on this page marked for removal from
    // the user file, through the SAME per-setting reset the arrow beside each
    // control performs.
    void resetAll();

    // The keys this page owns, in the order it presents them. What "Reset all
    // on this page" means (D-13).
    const std::vector<std::string>& keys() const { return m_keys; }

private:
    // What kind of control a key gets, and therefore how its value is read
    // back out of the toolkit's vocabulary and into the config file's.
    enum class Kind { Boolean, Delay, Command };

    struct Row {
        std::string key;
        Kind        kind = Kind::Boolean;
        GtkWidget*  label = nullptr;     // nullptr for a boolean: the box IS its label
        GtkWidget*  control = nullptr;
        GtkWidget*  reset = nullptr;
        std::string labelText;
        BehaviourPage* owner = nullptr;  // for the static callbacks
    };

    void addBooleanRow(GtkWidget* grid, int line, const std::string& key,
                       const std::string& label, const std::string& extra);
    void addDelayRow(GtkWidget* grid, int line, const std::string& key,
                     const std::string& label, const std::string& extra);
    void addCommandRow(GtkWidget* grid, int line, const std::string& key,
                       const std::string& label, const std::string& extra);
    GtkWidget* addResetButton(Row* row);
    void renderRow(Row& row);
    // D-08: the file moved underneath an edit this user has not saved. Marked
    // rather than replaced, which is the whole of that decision.
    void markRow(Row& row, bool stale);
    void commit(Row& row, const std::string& value);
    void reset(Row& row);

    static void onToggled(GtkToggleButton* button, gpointer userData);
    static void onSpinValueChanged(GtkSpinButton* spin, gpointer userData);
    static void onEntryActivate(GtkEntry* entry, gpointer userData);
    static gboolean onEntryFocusOut(GtkWidget* entry, GdkEvent* event,
                                    gpointer userData);
    static void onResetClicked(GtkButton* button, gpointer userData);
    static void onResetAllClicked(GtkButton* button, gpointer userData);

    FormState&    m_form;
    CommitHandler m_onCommit;
    StatusHandler m_onStatus;
    GtkWidget*    m_root = nullptr;
    std::vector<Row*> m_rows;
    std::vector<std::string> m_keys;

    // True while the page is writing values INTO its own widgets. Every handler
    // returns early when it is set: a programmatic gtk_toggle_button_set_active
    // emits the same "toggled" a user's click does, and without this a refresh
    // would commit every value it displayed straight back to the window
    // manager.
    bool m_updating = false;
};


// The tooltip for one setting: the option table's own summary, extended by
// `extra` where a sentence written for `--help` is too terse for somebody who
// has never read `--help`.
//
// Free rather than a member so a reader can see at a glance that nothing here
// invents wording: the first sentence is always the binary's own.
std::string behaviourTooltipFor(const std::string& key, const std::string& extra);
