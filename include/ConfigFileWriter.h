#pragma once

// The GUI's half of the config file: a surgical edit that replaces, appends or
// removes only the lines for the keys it manages, and leaves every other byte
// of the file exactly where it was (D-02).
//
// This header is deliberately free of X11, of Xft, of GTK and of GLib. It
// includes AppEntry.h -- plain data with no X11 dependency of its own, the same
// row shape the parser's menu-entry accumulator builds -- and the standard
// library, and nothing else. wm2-config compiles it, and so does a Catch2 case
// that must be runnable on a host with no X server; a dependency added here is
// a dependency added to both.
//
// ---------------------------------------------------------------------------
// WHY SURGICAL, AND WHAT THAT COSTS
// ---------------------------------------------------------------------------
//
// D-02: power users and the GUI share ONE file. A save that re-serialised the
// whole Config struct would be far simpler and would silently destroy the
// user's comments, their blank-line grouping, their `rule-*` groups and any key
// this version does not know about. So the writer never serialises a Config.
// It reads the file as lines, rewrites the lines it owns, and copies every
// other line through byte for byte.
//
// The read half classifies lines with the SAME rules Config::applyFile() uses
// (blank, comment, no-equals, key/value split on the first '=' with both sides
// trimmed), so a line the parser reads is a line the writer recognises. The
// two four-line trim helpers are duplicated into ConfigFileWriter.cpp rather
// than hoisted into a shared string header, following this project's
// per-translation-unit convention for small static helpers.
//
// D-13: removing a key DELETES its line rather than writing the built-in
// default. That is the whole point of "reset to defaults": a value set only in
// the system-wide layer must show through again afterwards, which writing the
// built-in default would permanently prevent.
//
// D-04: the only path this writer may ever write is the user file it was
// handed, plus a temporary file beside it. It never writes the system-wide
// configuration file.

#include "AppEntry.h"

#include <string>
#include <vector>


// One requested change to one managed key.
//
// `remove` wins over `value`: an edit with remove set deletes the key's line
// and ignores `value` entirely, so a caller cannot half-express "reset this"
// by leaving a stale value in the struct.
struct ConfigEdit {
    std::string key;
    std::string value;
    bool remove = false;
};


// Why a save did not happen, or that it did.
//
// Every failure path returns one of these AND a human-readable sentence in the
// caller's errorOut. Nothing here throws and nothing calls exit(), matching the
// project's `wm2: warning:` convention -- a config save that fails is a message
// for the user, not a reason to end the process.
// The longest value a single key may carry, in bytes.
//
// Exposed rather than kept file-local so the settings window's own dialogs can
// refuse an over-long field WHERE IT IS TYPED instead of letting it be
// accepted, applied live, and then refused at Save time by a message naming a
// bound the user never saw (IN-05).
inline constexpr std::size_t kConfigFileMaxValueBytes = 256;


enum class ConfigWriteResult {
    Ok,
    InvalidEdit,      // an edit the writer refuses to perform -- see below
    ReadFailed,       // the target exists but could not be read
    DirectoryFailed,  // the target's parent directory could not be created
    TempFailed,       // the temporary file beside the target could not be made
    WriteFailed,      // writing or flushing the temporary file failed
    RenameFailed      // the temporary file could not be renamed over the target
};


// The single list of keys the GUI owns. Every other key in the file -- unknown
// keys, `rule-*` groups, anything a future version adds -- is pass-through.
//
// `rule-*` is deliberately NOT here. The rules editor is a Deferred Idea in
// 09-CONTEXT.md, and the writer leaving rule lines untouched is exactly what
// lets a later phase add that editor with no file-format work: the rules a
// power user hand-wrote survive every save the GUI makes in the meantime.
//
// `menu-entry-*` is not here either. Those keys are an ordered three-line group
// rather than an independent setting, so they are rewritten as a block through
// configFileWrite()'s menuEntries parameter instead of through an edit.
const std::vector<std::string>& configFileManagedKeys();


// True if `key` is one of the keys above. Exact match after trimming, which is
// how Config::applyKeyValue() compares -- the parser does not lowercase keys,
// so neither does this.
bool configFileKeyIsManaged(const std::string& key);


// Saves `edits` into the file at `path`.
//
// Behaviour, in one paragraph: every line of the existing file is emitted
// unchanged except the lines for managed keys named in `edits` (replaced in
// place, keeping their position and their existing indentation and spacing
// around the '='), the lines for managed keys marked for removal (deleted), and
// the `menu-entry-*` lines when rewriteMenuEntries is true (replaced as one
// contiguous block at the position of the first existing menu-entry line).
// Managed keys named in `edits` that the file did not contain are appended at
// the end under a single generated section comment, which a second save does
// not duplicate. A missing parent directory is created; a missing file is
// treated as an empty one.
//
// Atomicity: the new text is written to a temporary file created in the
// TARGET'S OWN DIRECTORY, flushed with fsync(), and only then renamed over the
// target. A cross-directory temporary would make the rename non-atomic, which
// is the entire reason the temporary exists. An interruption at any point
// before the rename therefore leaves the original file exactly as it was
// (T-9-07), and the outcome is binary: either the whole edit set is in the file
// or none of it is (T-9-10).
//
// Refusals (ConfigWriteResult::InvalidEdit), all fail-closed and all before any
// file is touched:
//   * an edit naming a key configFileManagedKeys() does not contain -- the
//     writer must never rewrite a line it does not manage, and a caller asking
//     it to is a bug worth surfacing rather than silently ignoring;
//   * a key or value carrying a newline or a carriage return, which would
//     inject an extra line into the file;
//   * a value longer than 256 bytes, which Config::applyFile() drops with a
//     warning -- writing one would produce a file line the window manager will
//     never read back.
//
// `menuEntries` is written only when `rewriteMenuEntries` is true; when it is
// false the parameter is ignored and any menu-entry lines already in the file
// pass through untouched.
ConfigWriteResult configFileWrite(const std::string& path,
                                  const std::vector<ConfigEdit>& edits,
                                  const std::vector<AppEntry>& menuEntries,
                                  bool rewriteMenuEntries,
                                  std::string& errorOut);
