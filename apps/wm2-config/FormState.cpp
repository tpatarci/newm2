#include "FormState.h"

#include "MenuModel.h"   // menuEntryEqual -- the one spelling of "the same row"

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
    // Read a second time, for its key NAMES rather than for its values. Not a
    // second implementation of the layering: configFileKeysIn() classifies
    // lines with the writer's own splitter, which is already required to
    // classify them exactly as Config::applyFile() does.
    layers.userFileKeys = configFileKeysIn(layers.userFilePath);

    return layers;
}


bool ConfigLayers::userFileSets(const std::string& key) const
{
    return std::find(userFileKeys.begin(), userFileKeys.end(), key) !=
           userFileKeys.end();
}


// =============================================================================
// The model
// =============================================================================

void FormState::seedFromLayers(const ConfigLayers& layers)
{
    m_fields.clear();

    // The two paths a saved field's tooltip may name, kept for markSaved(),
    // which runs long after the layered read has gone out of scope (A2). The
    // LAST system file that set anything wins, and the layered apply walks them
    // lowest-precedence first, so the innermost is the back one.
    m_userFilePath = layers.userFilePath;
    m_systemFilePath = layers.systemFilePaths.empty()
                           ? std::string()
                           : layers.systemFilePaths.back();

    // Which layer produced a key's effective value is decided in two different
    // ways, and the split is the point (C5).
    //
    // THE USER FILE IS DECIDED BY PRESENCE. Whether the user's own file sets a
    // key is a question about the FILE, and comparison cannot answer it: a file
    // that explicitly sets a key to the value the layer below already produced
    // is still setting it, and reporting that as built-in or system-provided
    // names the wrong source in the tooltip -- and makes the override baffling
    // the day the system layer moves. configLayersFromDisk() reads the user
    // file's key names with the parser's own line rules for exactly this.
    //
    // THE SYSTEM/BUILT-IN SPLIT IS DECIDED BY COMPARISON, as it always was. No
    // file below the user's may be written, so all the form needs of them is
    // whether one of them changed the value, and re-parsing every system file
    // to find out would mean a second implementation of the layering -- which
    // is the one thing this function exists to avoid having.
    for (const std::string& key : configFileManagedKeys()) {
        FormField field;
        field.key = key;
        field.effective = formValueForKey(layers.withUser, key);
        field.belowUser = formValueForKey(layers.belowUser, key);
        field.current = field.effective;

        if (layers.userFileSets(key)) {
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
                field.sourceDetail = m_systemFilePath;
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
    if (!field->dirty) {
        field->current = value;
        field->staleUnderEdit = false;
        return;
    }

    // The mark is the DISAGREEMENT, not the edit. A reload that happens to
    // bring exactly what the user typed is not something they need told about,
    // and marking it would make the mark mean "edited" -- which `dirty`
    // already means, and which every control on the page would then wear after
    // any reload at all.
    field->staleUnderEdit = (field->current != value);
}


void FormState::refreshLowerLayers(const ConfigLayers& layers)
{
    // The lower layers moved, so which system file a later save should NAME may
    // have moved with them. Bookkeeping rather than a field value: no field's
    // source, current value or mark is touched here (A2).
    m_userFilePath = layers.userFilePath;
    m_systemFilePath = layers.systemFilePaths.empty()
                           ? std::string()
                           : layers.systemFilePaths.back();

    for (FormField& f : m_fields) {
        const std::string below = formValueForKey(layers.belowUser, f.key);
        if (below == f.belowUser) continue;
        f.belowUser = below;

        // A pending reset is DISPLAYING what removal will produce, and removal
        // now produces something else. Updated rather than preserved, because
        // that value was derived by requestReset() and not typed by anybody --
        // the D-08 prohibition is about not discarding somebody's typing, and
        // there is none here to discard. An ordinary edit is left exactly as it
        // was, mark and all.
        if (f.resetRequested) f.current = below;
    }

    // The menu entries carry the same triple for the same reason (D-12), so
    // they are refreshed by the same rule rather than beside it.
    m_menuBelowUser = layers.belowUser.manualMenuEntries;
    if (m_menuResetRequested) m_menuCurrent = m_menuBelowUser;
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
    // The user has now looked at the control and decided, so a mark left over
    // from a reload is old news.
    f->staleUnderEdit = false;
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
    f->staleUnderEdit = false;
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
        m_menuStaleUnderEdit = false;
    }

    for (FormField& f : m_fields) {
        if (!f.dirty) continue;
        if (f.resetRequested) {
            // The key is gone from the user file, so what is in force now is
            // whatever the layers below produce -- and the tooltip has to name
            // THAT file rather than go on naming the one the key was just
            // removed from (A2).
            f.effective = f.belowUser;
            f.current = f.belowUser;
            if (f.belowUser == formValueForKey(Config(), f.key)) {
                f.source = ValueSource::BuiltIn;
                f.sourceDetail.clear();
            } else {
                f.source = ValueSource::SystemFile;
                f.sourceDetail = m_systemFilePath;
            }
        } else {
            f.effective = f.current;
            f.source = ValueSource::UserFile;
            // ...and the user file is where it now IS. Left as it was, a field
            // saved from a built-in default read "Set in your own configuration
            // file, ." with an empty path.
            f.sourceDetail = m_userFilePath;
        }
        f.dirty = false;
        f.resetRequested = false;
        f.staleUnderEdit = false;
    }
}


void FormState::revert()
{
    for (FormField& f : m_fields) {
        f.current = f.effective;
        f.dirty = false;
        f.resetRequested = false;
        f.staleUnderEdit = false;
    }
    m_menuCurrent = m_menuEffective;
    m_menuDirty = false;
    m_menuResetRequested = false;
    m_menuStaleUnderEdit = false;
}


std::vector<std::pair<std::string, std::string>>
revertAndCollectRestores(FormState& form)
{
    // Collected BEFORE the revert -- these are the keys that moved -- and read
    // AFTER it, so each pair carries the value the desktop must go back to
    // rather than the one it is leaving (D-07, D-05).
    const std::vector<std::string> divergent = form.divergentKeys();
    form.revert();

    std::vector<std::pair<std::string, std::string>> restores;
    restores.reserve(divergent.size());
    for (const std::string& key : divergent) {
        restores.emplace_back(key, form.value(key));
    }
    return restores;
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
        if (!menuEntryEqual(a[i], b[i])) return false;
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
    m_menuStaleUnderEdit = false;
    m_menuCurrent = entries;
    m_menuDirty = !menuEntriesEqual(m_menuCurrent, m_menuEffective);
    return true;
}


std::vector<AppEntry> FormState::userMenuEntries() const
{
    // The user layer is the SUFFIX of the shown list that follows the entries
    // the layers below contribute, because Config::applyFile() appends across
    // layers in file order and the lower layers are read first.
    //
    // The prefix is CHECKED rather than assumed. When it does not match, the
    // shown list did not come from stacking the user file on top of the layers
    // this process just read -- a running window manager reporting entries from
    // a system file that has since changed under it is the ordinary way that
    // happens -- and there is then no honest way to say which rows the user
    // file owns. The whole list is treated as user-owned in that case, which is
    // the previous behaviour and errs towards keeping rows the user can see
    // rather than towards silently dropping them from their own file.
    if (m_menuBelowUser.size() <= m_menuCurrent.size()) {
        bool below = true;
        for (std::size_t i = 0; i < m_menuBelowUser.size(); ++i) {
            if (menuEntryEqual(m_menuBelowUser[i], m_menuCurrent[i])) continue;
            below = false;
            break;
        }
        if (below) {
            return std::vector<AppEntry>(
                m_menuCurrent.begin() +
                    static_cast<std::ptrdiff_t>(m_menuBelowUser.size()),
                m_menuCurrent.end());
        }
    }
    return m_menuCurrent;
}


void FormState::adoptEffectiveMenuEntries(const std::vector<AppEntry>& entries)
{
    m_menuEffective = entries;
    // D-08's prohibition, in the one line that keeps it: a list the user has
    // been editing is NOT replaced by what arrived. The window marks the
    // difference instead.
    if (!m_menuDirty) {
        m_menuCurrent = entries;
        m_menuStaleUnderEdit = false;
        return;
    }
    m_menuStaleUnderEdit = !menuEntriesEqual(m_menuCurrent, entries);
}


void FormState::requestMenuReset()
{
    m_menuResetRequested = true;
    m_menuDirty = true;
    m_menuStaleUnderEdit = false;
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
