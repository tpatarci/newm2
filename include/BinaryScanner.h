#pragma once

#include "AppEntry.h"

#include <optional>
#include <string>
#include <vector>

// Heuristic ELF binary scanner for /usr/bin (APPS-02).
//
// This module is deliberately X11-free -- mirrors DesktopEntry's separation
// from the presentation tier -- and never spawns a subprocess (no fork(),
// popen(), system(), or ldd/readelf invocation) to classify a binary: all
// inspection is done in-process via mmap + raw ELF struct parsing.
namespace BinaryScanner {

// Reads the DT_NEEDED entries out of an ELF64 binary's .dynamic section
// (via the PT_DYNAMIC program header, so it works on stripped binaries too).
// Returns std::nullopt if the file cannot be opened, is not a regular file,
// is too small/truncated to hold an ELF header, does not start with the ELF
// magic bytes, is not a 64-bit ELF object, or has no PT_DYNAMIC segment
// (e.g. a statically-linked binary -- not an error, just nothing to report).
std::optional<std::vector<std::string>> readNeededLibraries(const std::string& path);

// Classifies a binary as GUI (true) or CLI (false) based on whether any of
// its needed libraries contains a known GUI-toolkit substring.
bool isGuiBinary(const std::vector<std::string>& neededLibs);

// Walks /usr/bin, skipping shebang scripts and non-executable/non-regular
// files before any ELF parsing is attempted, and synthesizes an AppEntry
// (Source::BinaryScan) for every GUI binary whose bare name is not already
// present in existingNames (the set of names already covered by a .desktop
// entry, per DesktopEntry::scanAll()).
std::vector<AppEntry> scanUsrBin(const std::vector<std::string>& existingNames);

}  // namespace BinaryScanner
