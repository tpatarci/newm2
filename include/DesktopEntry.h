#pragma once

#include "AppEntry.h"

#include <optional>
#include <string>
#include <vector>

// XDG Desktop Entry parsing (APPS-01), X11-free (mirrors include/Config.h's
// dependency-free convention -- unit-testable without Xvfb).
//
// Recognized Exec= field codes (Desktop Entry Spec):
//   %f %F %u %U %d %D %n %N %i %c %k %v %m
// Field codes never receive a file/url argument in this phase (menu launch
// only), so every recognized code is stripped entirely rather than expanded.
// `%%` unescapes to a literal `%`. Any `%`-prefixed character NOT in the
// recognized set (including a bare trailing `%`) invalidates the WHOLE Exec
// value -- parseExec() returns std::nullopt for the entire entry rather than
// silently dropping the unrecognized sequence. This is the concrete
// enforcement of T-7-01 (no unknown field code may reach execvp()).
namespace DesktopEntry {

// Is `bin` an executable this user can run? A value containing a '/' is tested
// as a path; anything else is looked up in each $PATH element in turn. An empty
// value, and an unset or empty $PATH, are both false.
//
// TWO CALLERS, ONE ANSWER. The .desktop `TryExec` filter uses it to decide
// whether an entry's program is installed (D-05), and WindowManager's startup
// probe uses it to decide whether to put D-11's Configure entry on the root
// menu. It was file-local until plan 09-09 and is declared here rather than
// copied, because a second implementation of "is this program installed" is a
// second answer that disagrees with the first the moment either is edited.
//
// The lookup reads $PATH at the moment it is called. That is what makes D-11's
// once-at-startup probe the deliberate behaviour it is described as: putting
// the settings window on PATH after the window manager started changes nothing
// until it restarts, because nothing calls this again.
bool findOnPath(const std::string& bin);

// Tokenize a .desktop Exec= value into a fixed argv, respecting Desktop
// Entry Spec quoting (a "..."-delimited run is one token) and stripping all
// recognized field codes. Returns std::nullopt if quoting is unbalanced, an
// unrecognized field code is present, or the resulting argv is empty.
std::optional<std::vector<std::string>> parseExec(const std::string& execValue);

// Parse a single .desktop file's [Desktop Entry] section into an AppEntry.
// Returns std::nullopt if the file is unreadable, or the entry fails D-05
// filtering (NoDisplay, Hidden, NotShowIn, unresolvable TryExec), or its
// Exec= value fails parseExec().
std::optional<AppEntry> parseFile(const std::string& path);

// Walk every directory returned by xdgApplicationsDirs(), parse every
// *.desktop file found, and return the successfully-parsed, D-05-filtered
// entries. Missing directories are skipped silently (not an error).
std::vector<AppEntry> scanAll();

// $XDG_DATA_DIRS (colon-split, absolute-path filter), falling back to
// {"/usr/local/share", "/usr/share"} when unset -- same shape as
// xdgConfigDirs() in src/Config.cpp.
std::vector<std::string> xdgDataDirs();

// xdgDataDirs() with "/applications" appended to each entry, plus
// $XDG_DATA_HOME (or $HOME/.local/share fallback) + "/applications"
// prepended so user-local desktop files are included.
std::vector<std::string> xdgApplicationsDirs();

} // namespace DesktopEntry
