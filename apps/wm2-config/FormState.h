#pragma once

// The settings window's model, with no widgets in it (plan 09-06).
//
// Deliberately free of GTK, of GLib and of X11. It includes Config.h and
// ConfigFileWriter.h -- both already display-free by construction -- and the
// standard library, and nothing else. That is what lets the Catch2 cases for
// reset, save and revert run on a host with no X server and no toolkit, which
// in turn is what makes the smoke test's SKIP path honest: the half of
// wm2-config's behaviour that does not need a screen is proven even where the
// GUI was never built.
//
// ---------------------------------------------------------------------------
// WHAT ONE FIELD KNOWS, AND WHY EACH PART OF IT EXISTS
// ---------------------------------------------------------------------------
//
// `effective`   the value in force -- what the running window manager reports
//               when connected, and what the layered file load produces when
//               it is not (D-04). This is also the value Revert restores to.
//
// `belowUser`   the value the layers BELOW the user file produce: the built-in
//               default, or a system-wide file's override of it. Resetting a
//               setting removes it from the user file (D-13), so this -- not
//               the built-in default -- is what the user will actually see
//               afterwards, and so it is what the form shows the moment reset
//               is pressed. Showing the built-in default there would be a lie
//               on any host with a system-wide configuration.
//
// `source`      which layer `effective` came from, so DISC-08's tooltip can
//               name it. A key set only in the system layer is displayed as its
//               effective value with the layer named, editing it writes an
//               override into the user file, and resetting removes that
//               override so the system value shows through again.
//
// `current`     what the form is showing right now.
//
// `dirty`       the user changed this since the last save.
//
// `resetRequested` the user asked for this key to be REMOVED from their file on
//               the next save. Mutually exclusive with an ordinary edit: typing
//               a value after pressing reset cancels the reset, because the two
//               requests contradict each other and the last one expressed wins.

#include "Config.h"
#include "ConfigFileWriter.h"

#include <string>
#include <utility>
#include <vector>


// Which configuration layer a value came from (DISC-08).
enum class ValueSource {
    BuiltIn,        // nothing set it; this is the compiled-in default
    SystemFile,     // a file under one of the XDG system directories
    UserFile,       // the user's own file -- the only file the GUI writes
    WindowManager   // the running window manager reported it over the socket
};


// One managed setting, as the form sees it.
struct FormField {
    std::string  key;
    std::string  effective;
    std::string  belowUser;
    std::string  current;
    ValueSource  source = ValueSource::BuiltIn;
    std::string  sourceDetail;      // the file's path, when the source is a file
    bool         dirty = false;
    bool         resetRequested = false;

    // D-08: the effective value MOVED underneath an edit this user has not
    // saved, and the two now disagree. Distinct from `dirty`, which is merely
    // "a save would write something" and is true of every ordinary edit: this
    // one says something happened ELSEWHERE that the user should look at, so
    // the control is marked and the window says so in one line.
    //
    // Cleared the moment the user touches the control again -- they have now
    // seen it and decided -- and by a revert or a save, after which there is no
    // longer an edit to mark.
    bool         staleUnderEdit = false;
};


// Where the layered read found each value, and which file a save may write.
//
// Resolved with the SAME xdgConfigHome() / xdgConfigDirs() the window manager
// uses, by calling them, rather than by reimplementing the convention. A second
// implementation of "which file wins" is how a settings window ends up editing
// a file the window manager does not read.
struct ConfigLayers {
    Config       withUser;      // system layers, then the user file on top
    Config       belowUser;     // system layers only
    std::string  userFilePath;  // the one path a save may write

    // The keys the USER FILE actually contains, read with the parser's own
    // line rules. Provenance is a question about the file, not about the value
    // (C5): a user file that sets a key to the value the layer below already
    // produced is still setting it, and a comparison cannot see that.
    std::vector<std::string> userFileKeys;

    std::vector<std::string> systemFilePaths;   // those that exist, lowest first

    // True when the user file names `key` at all.
    bool userFileSets(const std::string& key) const;
};

// Perform the layered read. Never writes anything, never touches the user's
// real directories beyond reading them.
ConfigLayers configLayersFromDisk();


// The model.
class FormState {
public:
    // Seed every key configFileManagedKeys() names from a layered read. The
    // form's current value becomes the effective one and nothing is dirty.
    void seedFromLayers(const ConfigLayers& layers);

    // Replace one key's effective value -- what a connected window manager
    // reports, or what a reload notice brought (D-08). An UNTOUCHED field
    // follows the new value; a field the user has edited keeps the edit and
    // stays dirty, because silently discarding somebody's unsaved typing is
    // worse than showing them a value that is briefly out of date.
    void adoptEffective(const std::string& key, const std::string& value,
                        ValueSource source, const std::string& sourceDetail);

    // Re-take every field's `belowUser` snapshot -- and the menu entries' one --
    // from a FRESH layered read (D-08).
    //
    // seedFromLayers() takes those snapshots once, and they are what D-13's
    // Reset shows and live-applies. When a system file changes while this
    // window is open, the window manager re-reads it and reports new EFFECTIVE
    // values, which adoptEffective() takes -- but nothing was moving the lower
    // layers underneath them, so Reset went on offering the value that file used
    // to hold. Nothing else is touched: an edit stays dirty, its stale mark
    // stays up, and `effective` remains the reporter's business.
    //
    // The one value that DOES follow is a field with a reset already pending,
    // because what such a field shows is derived -- "what removing this key will
    // produce" -- rather than typed, and removing it now produces something
    // else.
    void refreshLowerLayers(const ConfigLayers& layers);

    // True for a key this form manages.
    bool manages(const std::string& key) const;

    // The field, or nullptr for a key this form does not manage.
    const FormField* field(const std::string& key) const;

    // Every field, in configFileManagedKeys() order.
    const std::vector<FormField>& fields() const { return m_fields; }

    // What the form is showing. Empty for an unmanaged key.
    std::string value(const std::string& key) const;

    // Record a user edit. Returns false for an unmanaged key or for a value
    // that is already what the form shows -- which is what stops an idempotent
    // widget signal (GTK emits "color-set" for a re-selection of the same
    // colour) from sending a redundant set message.
    bool setValue(const std::string& key, const std::string& value);

    // Ask for this key to be removed from the user file on the next save
    // (D-13). The form immediately shows the value that removal will produce,
    // which is the layer below the user file rather than the built-in default.
    // Returns false for an unmanaged key.
    bool requestReset(const std::string& key);

    // Is there anything a save would write?
    bool dirty() const;

    // The writer's edit list: changed keys as values, reset keys as removals,
    // untouched keys absent entirely. Empty when nothing is dirty -- and a
    // caller MUST treat that as "write nothing at all" rather than as "write an
    // empty edit set", because the writer rewrites the file either way and a
    // save that changed nothing must not touch the file's modification time.
    std::vector<ConfigEdit> edits() const;

    // After a successful write: the values the user chose become the effective
    // ones, a reset field falls back to the layer below, and nothing is dirty.
    void markSaved();

    // Put every field back to its effective value and forget every edit (D-05).
    void revert();

    // The keys whose current value differs from the effective one -- what a
    // Revert must send back to the running window manager so the desktop and
    // the file agree again. `menu-entries` is included in this list whenever
    // the row list has moved, so the Menu page needs no separate revert path.
    std::vector<std::string> divergentKeys() const;

    // ---------------------------------------------------------------------
    // The manual menu entries (D-12, plan 09-07)
    // ---------------------------------------------------------------------
    //
    // NOT a FormField, because they are not a single setting: the file spells
    // them as an ordered three-key group, the writer rewrites them as a block
    // rather than as an edit, and configFileManagedKeys() therefore does not
    // name them. They live here anyway, in the same model, so that Save,
    // Revert and Reset mean exactly the same thing on the Menu page as they do
    // on the other two -- one dirtiness, one revert, one save.
    //
    // Over the socket they travel as ONE value under `menu-entries` (the
    // grammar include/ConfigProtocol.h froze), which is why value() and
    // divergentKeys() answer for that key even though the form does not
    // "manage" it in configFileManagedKeys()'s sense.

    // What the page is showing right now: EVERY layer's entries, in file
    // order, because that is what the running window manager holds and what
    // `set menu-entries` has to carry.
    const std::vector<AppEntry>& menuEntries() const { return m_menuCurrent; }

    // The entries the USER FILE owns, which is what a save may write and all
    // that a save may write (C1).
    //
    // Config::applyFile() APPENDS menu entries across layers -- the accumulator
    // is deliberately never cleared per file -- so the shown list is the system
    // layers' entries followed by the user's. Handing that merged list to
    // configFileWrite() as the user file's block copies the inherited entries
    // into the user file, where the next layered load reads them a second time.
    std::vector<AppEntry> userMenuEntries() const;

    // Replace the whole list -- what Add, Edit and Remove each do, because the
    // protocol replaces wholesale and a per-row protocol would need a row
    // identity for the two ends to keep in sync. False for a list that is
    // already what the form shows.
    bool setMenuEntries(const std::vector<AppEntry>& entries);

    // Would a save rewrite the menu-entry block? False means the writer must be
    // told NOT to touch those lines, so a file whose entries the user never
    // opened passes through byte for byte.
    bool menuEntriesChanged() const { return m_menuDirty; }

    // D-08's mark, for the Menu page's one "setting": the entry list moved
    // underneath rows the user has changed and not saved.
    bool menuEntriesStaleUnderEdit() const { return m_menuStaleUnderEdit; }

    // Replace the effective list -- what a connected window manager reports,
    // or what a reload notice brought (D-08). An UNTOUCHED list follows the new
    // one; a list the user has edited keeps the edit and stays dirty, exactly
    // as adoptEffective() treats a single setting.
    void adoptEffectiveMenuEntries(const std::vector<AppEntry>& entries);

    // Ask for the manual entries to be REMOVED from the user file on the next
    // save (D-13's Reset applied to this page's one "setting").
    void requestMenuReset();

    // D-13's per-page half, built from the per-setting reset so there is one
    // meaning of reset and one code path. Returns the keys whose value moved,
    // which is what the window must send back to the running desktop.
    std::vector<std::string> requestResetAll(const std::vector<std::string>& keys);

private:
    FormField* mutableField(const std::string& key);

    std::vector<FormField> m_fields;

    // Which file a saved field's tooltip should name (A2). markSaved() has to
    // rewrite `sourceDetail` beside `source` -- an edit is now set in the user
    // file, a reset is now whatever the layer below says -- and the layered
    // read is long gone by then, so the two paths it can name are kept here.
    // Retaken by refreshLowerLayers(), because a system file appearing or
    // disappearing under an open window is exactly what that function is for.
    std::string m_userFilePath;
    std::string m_systemFilePath;   // the innermost system file, "" if there is none

    std::vector<AppEntry> m_menuEffective;   // what is in force
    std::vector<AppEntry> m_menuBelowUser;   // what removing the lines produces
    std::vector<AppEntry> m_menuCurrent;     // what the page is showing
    bool m_menuDirty = false;
    bool m_menuResetRequested = false;
    bool m_menuStaleUnderEdit = false;
};


// True when the two lists are the same rows in the same order. Compared field
// by field rather than through the rendered value, because two entries that
// render alike (a command with runs of spaces in it, say) ARE the same entry
// and a rendered comparison would be the weaker of the two.
bool menuEntriesEqual(const std::vector<AppEntry>& a,
                      const std::vector<AppEntry>& b);


// D-07's Discard, and D-05's Revert, in one function because they are one
// operation: put the form back to the last saved state, and hand back every
// key whose value MOVED so the caller can send it to the running window
// manager. Without that send-back, closing the settings window can leave a
// desktop matching no file at all -- the state a user cannot reason about
// later, and the whole reason D-07 names three responses rather than two.
//
// The list is collected BEFORE the revert and read AFTER it, so each pair
// carries the value the desktop must go back to rather than the one it is
// leaving. A caller with no connection simply sends nothing; the form is put
// back either way.
std::vector<std::pair<std::string, std::string>>
revertAndCollectRestores(FormState& form);


// The effective value of one key in `config`, spelled the way the config file
// spells it. A thin wrapper over configValueForKey() that returns the empty
// string rather than leaving an output parameter untouched, because every call
// site here wants a value it can put straight into a widget.
std::string formValueForKey(const Config& config, const std::string& key);
