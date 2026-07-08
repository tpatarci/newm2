#include <catch2/catch_test_macros.hpp>

#include "AppEntry.h"
#include "DesktopEntry.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

// Helper: write a temp .desktop file and return its path (mirrors
// tests/test_config.cpp's writeTempConfig)
static std::string writeTempDesktopFile(const std::string& content) {
    static int counter = 0;
    std::string path = "/tmp/wm2-test-desktopentry-" + std::to_string(++counter) + ".desktop";
    std::ofstream out(path);
    out << content;
    out.close();
    return path;
}

// Helper: clean up temp file (mirrors tests/test_config.cpp's removeTempFile)
static void removeTempFile(const std::string& path) {
    std::remove(path.c_str());
}

// =============================================================================
// Test (a): a well-formed entry parses with correct execArgv and category
// =============================================================================
TEST_CASE("parseFile parses a well-formed entry", "[desktopentry]") {
    std::string path = writeTempDesktopFile(
        "[Desktop Entry]\n"
        "Name=Firefox\n"
        "Exec=firefox %u\n"
        "Icon=firefox\n"
        "Categories=Network;WebBrowser;\n"
    );

    auto result = DesktopEntry::parseFile(path);
    REQUIRE(result.has_value());
    REQUIRE(result->name == "Firefox");
    REQUIRE(result->execArgv.size() == 1);
    REQUIRE(result->execArgv[0] == "firefox");
    REQUIRE(result->icon == "firefox");
    REQUIRE(result->category == "Network");
    REQUIRE(result->source == AppEntry::Source::Desktop);

    removeTempFile(path);
}

// =============================================================================
// Test (b): NoDisplay=true excludes the entry (D-05)
// =============================================================================
TEST_CASE("parseFile excludes NoDisplay=true entries", "[desktopentry]") {
    std::string path = writeTempDesktopFile(
        "[Desktop Entry]\n"
        "Name=Hidden App\n"
        "Exec=hiddenapp\n"
        "NoDisplay=true\n"
    );

    auto result = DesktopEntry::parseFile(path);
    REQUIRE_FALSE(result.has_value());

    removeTempFile(path);
}

// =============================================================================
// Test (c): Hidden=true excludes the entry (D-05)
// =============================================================================
TEST_CASE("parseFile excludes Hidden=true entries", "[desktopentry]") {
    std::string path = writeTempDesktopFile(
        "[Desktop Entry]\n"
        "Name=Hidden App\n"
        "Exec=hiddenapp\n"
        "Hidden=true\n"
    );

    auto result = DesktopEntry::parseFile(path);
    REQUIRE_FALSE(result.has_value());

    removeTempFile(path);
}

// =============================================================================
// Test (d): NotShowIn=wm2-born-again excludes the entry (D-05)
// =============================================================================
TEST_CASE("parseFile excludes NotShowIn=wm2-born-again entries", "[desktopentry]") {
    std::string path = writeTempDesktopFile(
        "[Desktop Entry]\n"
        "Name=Excluded App\n"
        "Exec=excludedapp\n"
        "NotShowIn=wm2-born-again;\n"
    );

    auto result = DesktopEntry::parseFile(path);
    REQUIRE_FALSE(result.has_value());

    removeTempFile(path);
}

// =============================================================================
// Test (e): unresolvable TryExec excludes the entry (D-05)
// =============================================================================
TEST_CASE("parseFile excludes entries with unresolvable TryExec", "[desktopentry]") {
    std::string path = writeTempDesktopFile(
        "[Desktop Entry]\n"
        "Name=Missing Binary App\n"
        "Exec=missingbinaryapp\n"
        "TryExec=/nonexistent/binary/xyz123\n"
    );

    auto result = DesktopEntry::parseFile(path);
    REQUIRE_FALSE(result.has_value());

    removeTempFile(path);
}

// =============================================================================
// Test (f): unrecognized field code rejects the whole entry (T-7-01)
// =============================================================================
TEST_CASE("parseFile rejects unrecognized Exec field codes", "[desktopentry]") {
    std::string path = writeTempDesktopFile(
        "[Desktop Entry]\n"
        "Name=Bad Field Code App\n"
        "Exec=app %z\n"
    );

    auto result = DesktopEntry::parseFile(path);
    REQUIRE_FALSE(result.has_value());

    // Also verify directly at the parseExec level.
    auto execResult = DesktopEntry::parseExec("app %z");
    REQUIRE_FALSE(execResult.has_value());

    removeTempFile(path);
}

// =============================================================================
// Test (g): quoted Exec argument is never split/interpreted by a shell
// =============================================================================
TEST_CASE("parseExec treats a quoted argument as one literal token", "[desktopentry]") {
    auto result = DesktopEntry::parseExec(
        "app \"arg with a space and a semicolon; rm -rf ~\"");

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);
    REQUIRE((*result)[0] == "app");
    REQUIRE((*result)[1] == "arg with a space and a semicolon; rm -rf ~");
}

// =============================================================================
// Test (h1): xdgDataDirs() honors $XDG_DATA_DIRS when set
// =============================================================================
TEST_CASE("xdgDataDirs parses colon-separated XDG_DATA_DIRS", "[desktopentry][xdg]") {
    char* orig = std::getenv("XDG_DATA_DIRS");
    std::string origStr;
    if (orig) origStr = orig;

    setenv("XDG_DATA_DIRS", "/usr/local/share/custom:/usr/share/custom", 1);

    auto dirs = DesktopEntry::xdgDataDirs();
    REQUIRE(dirs.size() == 2);
    REQUIRE(dirs[0] == "/usr/local/share/custom");
    REQUIRE(dirs[1] == "/usr/share/custom");

    // Restore
    if (origStr.empty()) {
        unsetenv("XDG_DATA_DIRS");
    } else {
        setenv("XDG_DATA_DIRS", origStr.c_str(), 1);
    }
}

// =============================================================================
// Test (h2): xdgDataDirs() falls back to documented defaults when unset
// =============================================================================
TEST_CASE("xdgDataDirs defaults to /usr/local/share and /usr/share", "[desktopentry][xdg]") {
    char* orig = std::getenv("XDG_DATA_DIRS");
    std::string origStr;
    if (orig) origStr = orig;

    unsetenv("XDG_DATA_DIRS");

    auto dirs = DesktopEntry::xdgDataDirs();
    REQUIRE(dirs.size() == 2);
    REQUIRE(dirs[0] == "/usr/local/share");
    REQUIRE(dirs[1] == "/usr/share");

    // Restore
    if (origStr.empty()) {
        unsetenv("XDG_DATA_DIRS");
    } else {
        setenv("XDG_DATA_DIRS", origStr.c_str(), 1);
    }
}

// =============================================================================
// Test (h3): xdgApplicationsDirs() appends "/applications" and includes the
// XDG_DATA_HOME-derived user directory first
// =============================================================================
TEST_CASE("xdgApplicationsDirs appends applications to each data dir and prepends user dir", "[desktopentry][xdg]") {
    char* origDirs = std::getenv("XDG_DATA_DIRS");
    char* origHome = std::getenv("XDG_DATA_HOME");

    std::string origDirsStr, origHomeStr;
    if (origDirs) origDirsStr = origDirs;
    if (origHome) origHomeStr = origHome;

    setenv("XDG_DATA_DIRS", "/usr/local/share/custom", 1);
    setenv("XDG_DATA_HOME", "/custom/data/home", 1);

    auto dirs = DesktopEntry::xdgApplicationsDirs();
    REQUIRE(dirs.size() == 2);
    REQUIRE(dirs[0] == "/custom/data/home/applications");
    REQUIRE(dirs[1] == "/usr/local/share/custom/applications");

    // Restore
    if (origDirsStr.empty()) unsetenv("XDG_DATA_DIRS");
    else setenv("XDG_DATA_DIRS", origDirsStr.c_str(), 1);
    if (origHomeStr.empty()) unsetenv("XDG_DATA_HOME");
    else setenv("XDG_DATA_HOME", origHomeStr.c_str(), 1);
}

// =============================================================================
// Test: scanAll() skips missing directories without error (not asserting
// specific content since it depends on the host's real XDG data, only that
// it doesn't throw/crash)
// =============================================================================
TEST_CASE("scanAll does not throw when XDG data dirs are empty/missing", "[desktopentry]") {
    char* origDirs = std::getenv("XDG_DATA_DIRS");
    char* origHome = std::getenv("XDG_DATA_HOME");

    std::string origDirsStr, origHomeStr;
    if (origDirs) origDirsStr = origDirs;
    if (origHome) origHomeStr = origHome;

    setenv("XDG_DATA_DIRS", "/tmp/wm2-noexist-data-xyz", 1);
    setenv("XDG_DATA_HOME", "/tmp/wm2-noexist-data-home-xyz", 1);

    auto apps = DesktopEntry::scanAll();
    REQUIRE(apps.empty());

    // Restore
    if (origDirsStr.empty()) unsetenv("XDG_DATA_DIRS");
    else setenv("XDG_DATA_DIRS", origDirsStr.c_str(), 1);
    if (origHomeStr.empty()) unsetenv("XDG_DATA_HOME");
    else setenv("XDG_DATA_HOME", origHomeStr.c_str(), 1);
}
