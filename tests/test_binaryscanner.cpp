#include <catch2/catch_test_macros.hpp>

#include "AppEntry.h"
#include "BinaryScanner.h"

#include <cstdio>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

// Helper: write a temp file with given raw bytes and return its path.
static std::string writeTempFile(const std::string& name, const std::string& content) {
    static int counter = 0;
    std::string path = "/tmp/wm2-test-binscan-" + std::to_string(++counter) + "-" + name;
    std::ofstream out(path, std::ios::binary);
    out << content;
    out.close();
    return path;
}

static void removeTempFile(const std::string& path) {
    std::remove(path.c_str());
}

// =============================================================================
// Test 1: readNeededLibraries() on a real CLI binary
// =============================================================================
TEST_CASE("readNeededLibraries succeeds on a real CLI ELF binary", "[binaryscanner]") {
    std::string candidate = "/bin/ls";
    if (access(candidate.c_str(), F_OK) != 0) {
        candidate = "/usr/bin/ls";
    }
    REQUIRE(access(candidate.c_str(), F_OK) == 0);

    auto result = BinaryScanner::readNeededLibraries(candidate);
    REQUIRE(result.has_value());
    REQUIRE_FALSE(BinaryScanner::isGuiBinary(*result));
}

// =============================================================================
// Test 2: shebang scripts are rejected (not ELF)
// =============================================================================
TEST_CASE("readNeededLibraries returns nullopt for a shebang script", "[binaryscanner]") {
    std::string path = writeTempFile("shebang.sh", "#!/bin/sh\necho hi\n");
    chmod(path.c_str(), 0755);

    auto result = BinaryScanner::readNeededLibraries(path);
    REQUIRE_FALSE(result.has_value());

    removeTempFile(path);
}

// =============================================================================
// Test 3: invalid ELF magic bytes are rejected cleanly
// =============================================================================
TEST_CASE("readNeededLibraries returns nullopt for bad ELF magic", "[binaryscanner]") {
    std::string content(64, '\0');  // 4 zero magic bytes + padding, not "\x7fELF"
    std::string path = writeTempFile("badmagic.bin", content);

    auto result = BinaryScanner::readNeededLibraries(path);
    REQUIRE_FALSE(result.has_value());

    removeTempFile(path);
}

// =============================================================================
// Test 4: empty (0-byte) file does not crash and returns nullopt
// =============================================================================
TEST_CASE("readNeededLibraries returns nullopt for an empty file", "[binaryscanner]") {
    std::string path = writeTempFile("empty.bin", "");

    auto result = BinaryScanner::readNeededLibraries(path);
    REQUIRE_FALSE(result.has_value());

    removeTempFile(path);
}

// =============================================================================
// Test 5: isGuiBinary() substring classification
// =============================================================================
TEST_CASE("isGuiBinary classifies known GUI toolkit substrings", "[binaryscanner]") {
    REQUIRE(BinaryScanner::isGuiBinary({"libX11.so.6"}));
    REQUIRE(BinaryScanner::isGuiBinary({"libgtk-3.so.0"}));
    REQUIRE_FALSE(BinaryScanner::isGuiBinary({"libc.so.6"}));
    REQUIRE_FALSE(BinaryScanner::isGuiBinary({}));
}

// =============================================================================
// Test 6: readNeededLibraries() on a nonexistent path
// =============================================================================
TEST_CASE("readNeededLibraries returns nullopt for a nonexistent path", "[binaryscanner]") {
    auto result = BinaryScanner::readNeededLibraries("/nonexistent/path/does-not-exist-binscan");
    REQUIRE_FALSE(result.has_value());
}

// =============================================================================
// Test 7: scanUsrBin() completes without crashing and only produces
// well-formed BinaryScan entries (machine-dependent contents of /usr/bin
// mean we can't assert a specific app is present).
// =============================================================================
TEST_CASE("scanUsrBin produces only well-formed BinaryScan entries", "[binaryscanner]") {
    std::vector<AppEntry> entries = BinaryScanner::scanUsrBin({});

    for (const AppEntry& entry : entries) {
        REQUIRE(entry.source == AppEntry::Source::BinaryScan);
        REQUIRE_FALSE(entry.execArgv.empty());
        REQUIRE_FALSE(entry.name.empty());
    }
}

// =============================================================================
// Test 8: scanUsrBin() honors existingNames and never duplicates a name
// already covered by a .desktop entry.
// =============================================================================
TEST_CASE("scanUsrBin excludes names already present in existingNames", "[binaryscanner]") {
    std::vector<AppEntry> baseline = BinaryScanner::scanUsrBin({});
    if (baseline.empty()) {
        SUCCEED("no GUI binaries found in /usr/bin on this machine -- nothing to exclude");
        return;
    }

    std::vector<std::string> existingNames = {baseline.front().name};
    std::vector<AppEntry> filtered = BinaryScanner::scanUsrBin(existingNames);

    for (const AppEntry& entry : filtered) {
        REQUIRE(entry.name != baseline.front().name);
    }
    REQUIRE(filtered.size() == baseline.size() - 1);
}
