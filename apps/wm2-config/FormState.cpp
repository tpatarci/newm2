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
    return false;
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
}


std::vector<std::string> FormState::divergentKeys() const
{
    std::vector<std::string> out;
    for (const FormField& f : m_fields) {
        if (f.current != f.effective) out.push_back(f.key);
    }
    return out;
}
