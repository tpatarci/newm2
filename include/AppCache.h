#pragma once

#include "AppEntry.h"

#include <ctime>
#include <string>
#include <vector>

// On-disk cache for discovered application entries (APPS-03), X11-free
// (mirrors DesktopEntry.h/BinaryScanner.h's separation from the
// presentation tier -- unit-testable without Xvfb).
//
// The cache is a small, purpose-built JSON-shaped serializer -- not a
// general-purpose JSON library -- sufficient for exactly the CacheData
// schema below (see 07-RESEARCH.md's "Don't Hand-Roll" table: a full JSON
// DOM library is disproportionate to this problem size).
namespace AppCache {

struct CacheData {
    // Unix timestamp of the scan that produced `entries`. 0 means "never
    // scanned" (matches a missing/unreadable cache file, per read()'s
    // silent-skip-on-missing-file convention).
    time_t scannedAt = 0;
    std::vector<AppEntry> entries;
};

// $XDG_CONFIG_HOME (or $HOME/.config fallback) + "/wm2-born-again/appcache.json".
std::string defaultCachePath();

// D-06: true if /usr/bin or any DesktopEntry::xdgApplicationsDirs() entry
// has an mtime newer than cachedScanTime (i.e. entries may have been
// added/removed since the cache was last built). Directory-mtime-only
// invalidation catches additions/removals but not in-place edits to an
// existing .desktop file's contents -- a deliberate, documented D-06
// tradeoff (see 07-RESEARCH.md Pitfall 3), not a bug.
bool needsRescan(time_t cachedScanTime);

// Reads and parses `path`. Returns CacheData{} (scannedAt=0, entries
// empty) without throwing if the file cannot be opened or is otherwise
// unreadable, matching Config::applyFile's silent-skip-on-missing-file
// convention. Individual malformed entries are skipped with a warning
// rather than aborting the whole read.
CacheData read(const std::string& path);

// Serializes `data` to `path` in the minimal JSON-shaped format read()
// expects. Creates the parent directory if it does not already exist.
void write(const std::string& path, const CacheData& data);

// D-07/D-08: merges auto-discovered entries (Desktop + BinaryScan sources)
// with user-authored manual entries. A manual entry whose name exactly
// matches an auto-discovered entry's name replaces it entirely (D-08);
// otherwise the manual entry is appended. Implemented in Task 2.
std::vector<AppEntry> mergeEntries(const std::vector<AppEntry>& autoDiscovered,
                                    const std::vector<AppEntry>& manualEntries);

// The auto-discovered half of loadOrRescan() below, on its own: reads the
// cache at cachePath, rescans and persists a fresh one when needsRescan() says
// the cache is stale or empty, and returns what discovery found WITHOUT any
// manual entries merged in.
//
// Split out for plan 09-05 (CGUI-04). Manual menu entries can now change while
// the window manager is running, and rebuilding the merged list then means
// merging the new manual entries onto the auto-discovered ones AGAIN -- which
// requires having kept them. They cannot be recovered from the merged list,
// because D-08's name-match rule REPLACES an auto-discovered entry rather than
// shadowing it, so the original is gone.
std::vector<AppEntry> loadAutoDiscovered(const std::string& cachePath);

// Orchestrates the full load: reads the cache at cachePath, rescans
// (DesktopEntry::scanAll() + BinaryScanner::scanUsrBin()) and persists a
// fresh cache if needsRescan() says the cache is stale or empty, then
// returns mergeEntries() of the auto-discovered list with manualEntries.
// Implemented in Task 2.
std::vector<AppEntry> loadOrRescan(const std::string& cachePath,
                                    const std::vector<AppEntry>& manualEntries);

} // namespace AppCache
