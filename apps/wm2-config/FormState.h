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
    std::vector<std::string> systemFilePaths;   // those that exist, lowest first
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
    // the file agree again.
    std::vector<std::string> divergentKeys() const;

private:
    FormField* mutableField(const std::string& key);

    std::vector<FormField> m_fields;
};


// The effective value of one key in `config`, spelled the way the config file
// spells it. A thin wrapper over configValueForKey() that returns the empty
// string rather than leaving an output parameter untouched, because every call
// site here wants a value it can put straight into a widget.
std::string formValueForKey(const Config& config, const std::string& key);
