#include "FormState.h"

#include <sys/stat.h>

#include <algorithm>


namespace {

bool fileExists(const std::string& path)
{
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

const char* kConfigLeaf = "/wm2-born-again/config";

}  // namespace


std::string formValueForKey(const Config& config, const std::string& key)
{
    std::string out;
    if (!configValueForKey(config, key, out)) return std::string();
    return out;
}


// =============================================================================
// The layered read
// =============================================================================

ConfigLayers configLayersFromDisk()
{
    ConfigLayers layers;

    // Config::load()'s own order, minus the CLI layer, which a settings window
    // has no business having an opinion about: a value the user put on the
    // window manager's command line is not in any file and cannot be edited by
    // writing one. When connected, the window manager's own reported values
    // take over from these anyway (D-04), and those DO include its CLI layer.
    for (const std::string& dir : xdgConfigDirs()) {
        const std::string path = dir + kConfigLeaf;
        if (!fileExists(path)) continue;
        layers.systemFilePaths.push_back(path);
        layers.belowUser.applyFile(path);
    }

    layers.withUser = layers.belowUser;
    layers.userFilePath = xdgConfigHome() + kConfigLeaf;
    layers.withUser.applyFile(layers.userFilePath);

    return layers;
}


// =============================================================================
// The model
// =============================================================================

void FormState::seedFromLayers(const ConfigLayers& layers)
{
    m_fields.clear();

    // Which layer produced a key's effective value is decided by COMPARISON,
    // not by re-parsing the files a second time: if the value with the user
    // file differs from the value without it, the user file set it; otherwise
    // the deepest system file that changed it did, and if none did, nothing
    // did and the value is the built-in default. Re-parsing to find out would
    // mean a second implementation of the layering, which is the one thing
    // this function exists to avoid having.
    for (const std::string& key : configFileManagedKeys()) {
        FormField field;
        field.key = key;
        field.effective = formValueForKey(layers.withUser, key);
        field.belowUser = formValueForKey(layers.belowUser, key);
        field.current = field.effective;

        if (field.effective != field.belowUser) {
            field.source = ValueSource::UserFile;
            field.sourceDetail = layers.userFilePath;
        } else {
            const Config builtIn;
            const std::string compiled = formValueForKey(builtIn, key);
            if (field.belowUser != compiled) {
                field.source = ValueSource::SystemFile;
                // The LAST system file that set it wins, and the layered apply
                // walks them lowest-precedence first, so the innermost one
                // that changed the value is found by walking backwards.
                field.sourceDetail = layers.systemFilePaths.empty()
                                         ? std::string()
                                         : layers.systemFilePaths.back();
            } else {
                field.source = ValueSource::BuiltIn;
                field.sourceDetail.clear();
            }
        }

        m_fields.push_back(field);
    }

    // The menu entries, from the same two layered reads (D-12). Not a field:
    // the file spells them as an ordered group and the writer rewrites them as
    // a block, so they carry their own current/effective/belowUser triple.
    m_menuEffective = layers.withUser.manualMenuEntries;
    m_menuBelowUser = layers.belowUser.manualMenuEntries;
    m_menuCurrent = m_menuEffective;
    m_menuDirty = false;
    m_menuResetRequested = false;
}


void FormState::adoptEffective(const std::string& key, const std::string& value,
                               ValueSource source, const std::string& sourceDetail)
{
    FormField* field = mutableField(key);
    if (!field) return;

    field->effective = value;
    field->source = source;
    field->sourceDetail = sourceDetail;

    // An untouched field follows; an edited one keeps the edit and stays dirty
    // (D-08). Discarding somebody's unsaved typing because a reload happened
    // elsewhere would be a worse surprise than a value that is briefly stale,
    // and the window marks the difference rather than hiding it.
    if (!field->dirty) field->current = value;
}


bool FormState::manages(const std::string& key) const
{
    return field(key) != nullptr;
}


const FormField* FormState::field(const std::string& key) const
{
    for (const FormField& f : m_fields) {
        if (f.key == key) return &f;
    }
    return nullptr;
}


FormField* FormState::mutableField(const std::string& key)
{
    for (FormField& f : m_fields) {
        if (f.key == key) return &f;
    }
    return nullptr;
}


std::string FormState::value(const std::string& key) const
{
    // The menu entries answer for their own key even though they are not a
    // FormField: over the socket they ARE one value, and every caller that
    // wants "what should I send for this key" -- live apply, revert, the
    // discard path -- then needs no special case for the Menu page.
    if (key == kMenuEntriesKey) {
        Config rendered;
        rendered.manualMenuEntries = m_menuCurrent;
        return configMenuEntriesValue(rendered);
    }

    const FormField* f = field(key);
    return f ? f->current : std::string();
}


bool FormState::setValue(const std::string& key, const std::string& value)
{
    FormField* f = mutableField(key);
    if (!f) return false;

    // A reset and an ordinary edit are contradictory requests; the last one
    // expressed wins, so typing a value cancels a pending reset. Tested BEFORE
    // the no-change test, or typing the very value a pending reset is already
    // showing would leave the removal silently in place -- and "I typed this
    // value" means "put this value in my file", which is the opposite of "take
    // this key out of my file".
    const bool cancelsReset = f->resetRequested;
    if (!cancelsReset && f->current == value) return false;

    f->resetRequested = false;
    f->current = value;
    // Dirty means "a save would write something". Typing a value back to what
    // is already in force is therefore not dirty, even though the form did
    // change and the caller does want to hear about it -- which is why the
    // return value and this flag are two different things.
    f->dirty = (f->current != f->effective);
    return true;
}


bool FormState::requestReset(const std::string& key)
{
    FormField* f = mutableField(key);
    if (!f) return false;

    f->resetRequested = true;
    f->dirty = true;
    // What removal will ACTUALLY produce -- the layer below the user file --
    // rather than the built-in default, which on a host with a system-wide
    // configuration would be a value the user is never going to see (D-13).
    f->current = f->belowUser;
    return true;
}


bool FormState::dirty() const
{
    for (const FormField& f : m_fields) {
        if (f.dirty) return true;
    }
    return m_menuDirty;
}


std::vector<ConfigEdit> FormState::edits() const
{
    std::vector<ConfigEdit> out;
    for (const FormField& f : m_fields) {
        if (!f.dirty) continue;          // untouched keys are absent entirely
        ConfigEdit edit;
        edit.key = f.key;
        if (f.resetRequested) {
            edit.remove = true;          // remove wins over value, by contract
        } else {
            edit.value = f.current;
        }
        out.push_back(edit);
    }
    return out;
}


void FormState::markSaved()
{
    if (m_menuDirty) {
        // A reset removed the menu-entry lines, so what is in force now is what
        // the layers below produce; an ordinary edit becomes the effective list.
        m_menuEffective = m_menuResetRequested ? m_menuBelowUser : m_menuCurrent;
        m_menuCurrent = m_menuEffective;
        m_menuDirty = false;
        m_menuResetRequested = false;
    }

    for (FormField& f : m_fields) {
        if (!f.dirty) continue;
        if (f.resetRequested) {
            // The key is gone from the user file, so what is in force now is
            // whatever the layers below produce.
            f.effective = f.belowUser;
            f.current = f.belowUser;
            f.source = (f.belowUser == formValueForKey(Config(), f.key))
                           ? ValueSource::BuiltIn
                           : ValueSource::SystemFile;
        } else {
            f.effective = f.current;
            f.source = ValueSource::UserFile;
        }
        f.dirty = false;
        f.resetRequested = false;
    }
}


void FormState::revert()
{
    for (FormField& f : m_fields) {
        f.current = f.effective;
        f.dirty = false;
        f.resetRequested = false;
    }
    m_menuCurrent = m_menuEffective;
    m_menuDirty = false;
    m_menuResetRequested = false;
}


std::vector<std::string> FormState::divergentKeys() const
{
    std::vector<std::string> out;
    for (const FormField& f : m_fields) {
        if (f.current != f.effective) out.push_back(f.key);
    }
    if (!menuEntriesEqual(m_menuCurrent, m_menuEffective)) {
        out.push_back(kMenuEntriesKey);
    }
    return out;
}


// =============================================================================
// The manual menu entries (D-12)
// =============================================================================

bool menuEntriesEqual(const std::vector<AppEntry>& a,
                      const std::vector<AppEntry>& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].name != b[i].name) return false;
        if (a[i].category != b[i].category) return false;
        if (a[i].execArgv != b[i].execArgv) return false;
    }
    return true;
}


bool FormState::setMenuEntries(const std::vector<AppEntry>& entries)
{
    // A reset and an ordinary edit are contradictory requests and the last one
    // expressed wins -- the same rule setValue() applies to a single setting,
    // and tested in the same order, so editing a row after "reset this page"
    // cancels the removal rather than leaving it silently in place.
    const bool cancelsReset = m_menuResetRequested;
    if (!cancelsReset && menuEntriesEqual(m_menuCurrent, entries)) return false;

    m_menuResetRequested = false;
    m_menuCurrent = entries;
    m_menuDirty = !menuEntriesEqual(m_menuCurrent, m_menuEffective);
    return true;
}


void FormState::adoptEffectiveMenuEntries(const std::vector<AppEntry>& entries)
{
    m_menuEffective = entries;
    // D-08's prohibition, in the one line that keeps it: a list the user has
    // been editing is NOT replaced by what arrived. The window marks the
    // difference instead.
    if (!m_menuDirty) m_menuCurrent = entries;
}


void FormState::requestMenuReset()
{
    m_menuResetRequested = true;
    m_menuDirty = true;
    // What removal will ACTUALLY produce: the entries the layers below the user
    // file define, which on a host with a system-wide configuration is not the
    // empty list.
    m_menuCurrent = m_menuBelowUser;
}


std::vector<std::string> FormState::requestResetAll(const std::vector<std::string>& keys)
{
    // D-13's per-page half, built from the per-setting reset rather than beside
    // it: one meaning of reset, one code path, and a page that adds a control
    // gets it covered without touching this.
    std::vector<std::string> moved;
    for (const std::string& key : keys) {
        if (key == kMenuEntriesKey) {
            const std::vector<AppEntry> before = m_menuCurrent;
            requestMenuReset();
            if (!menuEntriesEqual(before, m_menuCurrent)) moved.push_back(key);
            continue;
        }
        const FormField* f = field(key);
        if (!f) continue;
        const std::string before = f->current;
        if (!requestReset(key)) continue;
        if (value(key) != before) moved.push_back(key);
    }
    return moved;
}
