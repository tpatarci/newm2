#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "Config.h"

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

// Helper: write a temp config file and return its path
static std::string writeTempConfig(const std::string& content) {
    static int counter = 0;
    std::string path = "/tmp/wm2-test-config-" + std::to_string(++counter) + ".cfg";
    std::ofstream out(path);
    out << content;
    out.close();
    return path;
}

// Helper: clean up temp file
static void removeTempFile(const std::string& path) {
    std::remove(path.c_str());
}

// =============================================================================
// Test 1: Config struct initializes with all 18 default values
// =============================================================================
TEST_CASE("Config defaults match upstream Config.h", "[config]") {
    Config cfg;

    // Colors (tab) - 2 settings
    REQUIRE(cfg.tabForeground == "black");
    REQUIRE(cfg.tabBackground == "gray80");

    // Colors (frame) - 3 settings
    REQUIRE(cfg.frameBackground == "gray95");
    REQUIRE(cfg.buttonBackground == "gray95");
    REQUIRE(cfg.borders == "black");

    // Colors (menu) - 4 settings
    REQUIRE(cfg.menuForeground == "black");
    REQUIRE(cfg.menuBackground == "gray80");
    REQUIRE(cfg.menuHighlight == "gray60");
    REQUIRE(cfg.menuBorders == "black");

    // Focus policy - 3 settings
    REQUIRE(cfg.clickToFocus == false);
    REQUIRE(cfg.raiseOnFocus == false);
    REQUIRE(cfg.autoRaise == false);

    // Timing (milliseconds) - 3 settings
    REQUIRE(cfg.autoRaiseDelay == 400);
    REQUIRE(cfg.pointerStoppedDelay == 80);
    REQUIRE(cfg.destroyWindowDelay == 1500);

    // Frame - 1 setting
    REQUIRE(cfg.frameThickness == 7);

    // Commands - 2 settings
    REQUIRE(cfg.newWindowCommand == "xterm");
    REQUIRE(cfg.execUsingShell == false);
}

// =============================================================================
// Test 2: applyFile() parses a key=value file and overrides defaults
// =============================================================================
TEST_CASE("applyFile parses key=value and overrides defaults", "[config]") {
    std::string path = writeTempConfig(
        "tab-foreground = white\n"
        "frame-thickness = 10\n"
        "auto-raise-delay = 200\n"
        "click-to-focus = true\n"
        "new-window-command = alacritty\n"
    );

    Config cfg;
    cfg.applyFile(path);

    REQUIRE(cfg.tabForeground == "white");
    REQUIRE(cfg.frameThickness == 10);
    REQUIRE(cfg.autoRaiseDelay == 200);
    REQUIRE(cfg.clickToFocus == true);
    REQUIRE(cfg.newWindowCommand == "alacritty");

    // Unset keys retain defaults
    REQUIRE(cfg.tabBackground == "gray80");
    REQUIRE(cfg.autoRaise == false);

    removeTempFile(path);
}

// =============================================================================
// Test 3: applyFile() skips comment lines (# prefix) and blank lines
// =============================================================================
TEST_CASE("applyFile skips comments and blank lines", "[config]") {
    std::string path = writeTempConfig(
        "# This is a comment\n"
        "\n"
        "  # Indented comment\n"
        "\t\n"
        "tab-foreground = red\n"
        "  \n"
        "# Another comment\n"
        "frame-thickness = 5\n"
    );

    Config cfg;
    cfg.applyFile(path);

    REQUIRE(cfg.tabForeground == "red");
    REQUIRE(cfg.frameThickness == 5);

    removeTempFile(path);
}

// =============================================================================
// Test 4: applyFile() warns on lines without '=' but continues parsing
// =============================================================================
TEST_CASE("applyFile warns on lines without equals sign", "[config]") {
    std::string path = writeTempConfig(
        "this line has no equals\n"
        "tab-foreground = blue\n"
        "another bad line\n"
        "frame-thickness = 3\n"
    );

    Config cfg;
    cfg.applyFile(path);

    // Should still parse valid lines after warnings
    REQUIRE(cfg.tabForeground == "blue");
    REQUIRE(cfg.frameThickness == 3);

    removeTempFile(path);
}

// =============================================================================
// Test 5: applyFile() silently skips non-existent files (no error)
// =============================================================================
TEST_CASE("applyFile silently skips non-existent files", "[config]") {
    Config cfg;
    // Should not throw or crash
    cfg.applyFile("/tmp/wm2-nonexistent-config-file-xyz123.cfg");

    // All defaults should remain
    REQUIRE(cfg.tabForeground == "black");
    REQUIRE(cfg.frameThickness == 7);
    REQUIRE(cfg.newWindowCommand == "xterm");
}

// =============================================================================
// Test 6: xdgConfigHome() returns $XDG_CONFIG_HOME when set and absolute
// =============================================================================
TEST_CASE("xdgConfigHome returns XDG_CONFIG_HOME when set and absolute", "[config][xdg]") {
    // Save original
    char* origXdg = std::getenv("XDG_CONFIG_HOME");

    std::string origXdgStr;
    if (origXdg) origXdgStr = origXdg;

    setenv("XDG_CONFIG_HOME", "/custom/config", 1);

    std::string result = xdgConfigHome();
    REQUIRE(result == "/custom/config");

    // Restore
    if (origXdgStr.empty()) {
        unsetenv("XDG_CONFIG_HOME");
    } else {
        setenv("XDG_CONFIG_HOME", origXdgStr.c_str(), 1);
    }
}

// =============================================================================
// Test 7: xdgConfigHome() falls back to $HOME/.config when XDG_CONFIG_HOME unset
// =============================================================================
TEST_CASE("xdgConfigHome falls back to HOME/.config", "[config][xdg]") {
    char* origXdg = std::getenv("XDG_CONFIG_HOME");
    char* origHome = std::getenv("HOME");

    std::string origXdgStr, origHomeStr;
    if (origXdg) origXdgStr = origXdg;
    if (origHome) origHomeStr = origHome;

    unsetenv("XDG_CONFIG_HOME");
    setenv("HOME", "/home/testuser", 1);

    std::string result = xdgConfigHome();
    REQUIRE(result == "/home/testuser/.config");

    // Restore
    if (origXdgStr.empty()) {
        unsetenv("XDG_CONFIG_HOME");
    } else {
        setenv("XDG_CONFIG_HOME", origXdgStr.c_str(), 1);
    }
    if (!origHomeStr.empty()) {
        setenv("HOME", origHomeStr.c_str(), 1);
    }
}

// =============================================================================
// Test 8: xdgConfigDirs() parses colon-separated $XDG_CONFIG_DIRS
// =============================================================================
TEST_CASE("xdgConfigDirs parses colon-separated XDG_CONFIG_DIRS", "[config][xdg]") {
    char* orig = std::getenv("XDG_CONFIG_DIRS");
    std::string origStr;
    if (orig) origStr = orig;

    setenv("XDG_CONFIG_DIRS", "/usr/local/etc/xdg:/etc/xdg", 1);

    auto dirs = xdgConfigDirs();
    REQUIRE(dirs.size() == 2);
    REQUIRE(dirs[0] == "/usr/local/etc/xdg");
    REQUIRE(dirs[1] == "/etc/xdg");

    // Restore
    if (origStr.empty()) {
        unsetenv("XDG_CONFIG_DIRS");
    } else {
        setenv("XDG_CONFIG_DIRS", origStr.c_str(), 1);
    }
}

// =============================================================================
// Test 9: xdgConfigDirs() defaults to {"/etc/xdg"} when env unset
// =============================================================================
TEST_CASE("xdgConfigDirs defaults to /etc/xdg", "[config][xdg]") {
    char* orig = std::getenv("XDG_CONFIG_DIRS");
    std::string origStr;
    if (orig) origStr = orig;

    unsetenv("XDG_CONFIG_DIRS");

    auto dirs = xdgConfigDirs();
    REQUIRE(dirs.size() == 1);
    REQUIRE(dirs[0] == "/etc/xdg");

    // Restore
    if (origStr.empty()) {
        unsetenv("XDG_CONFIG_DIRS");
    } else {
        setenv("XDG_CONFIG_DIRS", origStr.c_str(), 1);
    }
}

// =============================================================================
// Test 10: Config::load applies system config first, user config second
// =============================================================================
TEST_CASE("Config load precedence: user config overrides system config", "[config]") {
    // Set up system config dir with a test config
    char* origDirs = std::getenv("XDG_CONFIG_DIRS");
    char* origHome = std::getenv("XDG_CONFIG_HOME");
    char* origUserHome = std::getenv("HOME");

    std::string origDirsStr, origHomeStr, origUserHomeStr;
    if (origDirs) origDirsStr = origDirs;
    if (origHome) origHomeStr = origHome;
    if (origUserHome) origUserHomeStr = origUserHome;

    // System config: tab-foreground = system-color
    std::string sysDir = "/tmp/wm2-test-sys-" + std::to_string(std::rand());
    std::string sysSubdir = sysDir + "/wm2-born-again";
    std::string userDir = "/tmp/wm2-test-user-" + std::to_string(std::rand());
    std::string userSubdir = userDir + "/wm2-born-again";

    // Create directories
    std::system(("mkdir -p " + sysSubdir).c_str());
    std::system(("mkdir -p " + userSubdir).c_str());

    // Write system config
    {
        std::ofstream out(sysSubdir + "/config");
        out << "tab-foreground = system-color\n";
        out << "frame-thickness = 15\n";
    }

    // Write user config (overrides some, adds new)
    {
        std::ofstream out(userSubdir + "/config");
        out << "tab-foreground = user-color\n";
        out << "auto-raise = true\n";
    }

    setenv("XDG_CONFIG_DIRS", sysDir.c_str(), 1);
    setenv("XDG_CONFIG_HOME", userDir.c_str(), 1);

    // Load with zero CLI args
    char* argv[] = {(char*)"wm2-born-again"};
    Config cfg = Config::load(1, argv);

    // User config overrides system for tab-foreground
    REQUIRE(cfg.tabForeground == "user-color");
    // System config value preserved (not overridden by user)
    REQUIRE(cfg.frameThickness == 15);
    // User config adds new setting
    REQUIRE(cfg.autoRaise == true);
    // Defaults preserved where neither config sets
    REQUIRE(cfg.tabBackground == "gray80");

    // Clean up
    std::system(("rm -rf " + sysDir).c_str());
    std::system(("rm -rf " + userDir).c_str());

    // Restore
    if (origDirsStr.empty()) unsetenv("XDG_CONFIG_DIRS");
    else setenv("XDG_CONFIG_DIRS", origDirsStr.c_str(), 1);
    if (origHomeStr.empty()) unsetenv("XDG_CONFIG_HOME");
    else setenv("XDG_CONFIG_HOME", origHomeStr.c_str(), 1);
    if (!origUserHomeStr.empty()) setenv("HOME", origUserHomeStr.c_str(), 1);
}

// =============================================================================
// Test 11: Config::load works with zero config files present
// =============================================================================
TEST_CASE("Config load with no config files returns pure defaults", "[config]") {
    char* origDirs = std::getenv("XDG_CONFIG_DIRS");
    char* origHome = std::getenv("XDG_CONFIG_HOME");

    std::string origDirsStr, origHomeStr;
    if (origDirs) origDirsStr = origDirs;
    if (origHome) origHomeStr = origHome;

    // Point to non-existent dirs
    setenv("XDG_CONFIG_DIRS", "/tmp/wm2-noexist-sys-xyz", 1);
    setenv("XDG_CONFIG_HOME", "/tmp/wm2-noexist-user-xyz", 1);

    char* argv[] = {(char*)"wm2-born-again"};
    Config cfg = Config::load(1, argv);

    // All defaults
    REQUIRE(cfg.tabForeground == "black");
    REQUIRE(cfg.frameThickness == 7);
    REQUIRE(cfg.autoRaiseDelay == 400);
    REQUIRE(cfg.newWindowCommand == "xterm");
    REQUIRE(cfg.autoRaise == false);

    // Restore
    if (origDirsStr.empty()) unsetenv("XDG_CONFIG_DIRS");
    else setenv("XDG_CONFIG_DIRS", origDirsStr.c_str(), 1);
    if (origHomeStr.empty()) unsetenv("XDG_CONFIG_HOME");
    else setenv("XDG_CONFIG_HOME", origHomeStr.c_str(), 1);
}

// =============================================================================
// Test 12: applyKeyValue handles all 18 settings (string, int, bool)
// =============================================================================
TEST_CASE("applyKeyValue handles all 18 settings", "[config]") {
    Config cfg;

    // String settings (9 colors + 1 command = 10 string settings, but menu-highlight is 1 of them)
    cfg.applyKeyValue("tab-foreground", "red");
    REQUIRE(cfg.tabForeground == "red");

    cfg.applyKeyValue("tab-background", "blue");
    REQUIRE(cfg.tabBackground == "blue");

    cfg.applyKeyValue("frame-background", "green");
    REQUIRE(cfg.frameBackground == "green");

    cfg.applyKeyValue("button-background", "yellow");
    REQUIRE(cfg.buttonBackground == "yellow");

    cfg.applyKeyValue("borders", "purple");
    REQUIRE(cfg.borders == "purple");

    cfg.applyKeyValue("menu-foreground", "cyan");
    REQUIRE(cfg.menuForeground == "cyan");

    cfg.applyKeyValue("menu-background", "magenta");
    REQUIRE(cfg.menuBackground == "magenta");

    cfg.applyKeyValue("menu-highlight", "orange");
    REQUIRE(cfg.menuHighlight == "orange");

    cfg.applyKeyValue("menu-borders", "pink");
    REQUIRE(cfg.menuBorders == "pink");

    cfg.applyKeyValue("new-window-command", "alacritty");
    REQUIRE(cfg.newWindowCommand == "alacritty");

    // Bool settings (4)
    cfg.applyKeyValue("click-to-focus", "true");
    REQUIRE(cfg.clickToFocus == true);

    cfg.applyKeyValue("raise-on-focus", "true");
    REQUIRE(cfg.raiseOnFocus == true);

    cfg.applyKeyValue("auto-raise", "true");
    REQUIRE(cfg.autoRaise == true);

    cfg.applyKeyValue("exec-using-shell", "true");
    REQUIRE(cfg.execUsingShell == true);

    // Int settings (4)
    cfg.applyKeyValue("auto-raise-delay", "500");
    REQUIRE(cfg.autoRaiseDelay == 500);

    cfg.applyKeyValue("pointer-stopped-delay", "100");
    REQUIRE(cfg.pointerStoppedDelay == 100);

    cfg.applyKeyValue("destroy-window-delay", "2000");
    REQUIRE(cfg.destroyWindowDelay == 2000);

    cfg.applyKeyValue("frame-thickness", "12");
    REQUIRE(cfg.frameThickness == 12);

    // Unknown key should not crash
    cfg.applyKeyValue("unknown-key", "value");
    // No assertion needed -- just checking it doesn't crash
}

// =============================================================================
// Test 13: Boolean config values accept "true"/"false" and "1"/"0"
// =============================================================================
TEST_CASE("Boolean config values accept multiple formats", "[config]") {
    Config cfg;

    // "true" / "false"
    cfg.applyKeyValue("click-to-focus", "true");
    REQUIRE(cfg.clickToFocus == true);

    cfg.applyKeyValue("click-to-focus", "false");
    REQUIRE(cfg.clickToFocus == false);

    // "1" / "0"
    cfg.applyKeyValue("raise-on-focus", "1");
    REQUIRE(cfg.raiseOnFocus == true);

    cfg.applyKeyValue("raise-on-focus", "0");
    REQUIRE(cfg.raiseOnFocus == false);

    // Case insensitive
    cfg.applyKeyValue("auto-raise", "True");
    REQUIRE(cfg.autoRaise == true);

    cfg.applyKeyValue("auto-raise", "FALSE");
    REQUIRE(cfg.autoRaise == false);
}

// =============================================================================
// Test 14: Integer values out of range are clamped
// =============================================================================
TEST_CASE("Integer values are clamped to valid ranges", "[config]") {
    Config cfg;

    // Delays clamped to 1-60000
    cfg.applyKeyValue("auto-raise-delay", "0");
    REQUIRE(cfg.autoRaiseDelay == 1);

    cfg.applyKeyValue("auto-raise-delay", "70000");
    REQUIRE(cfg.autoRaiseDelay == 60000);

    cfg.applyKeyValue("pointer-stopped-delay", "-5");
    REQUIRE(cfg.pointerStoppedDelay == 1);

    cfg.applyKeyValue("pointer-stopped-delay", "99999");
    REQUIRE(cfg.pointerStoppedDelay == 60000);

    cfg.applyKeyValue("destroy-window-delay", "-100");
    REQUIRE(cfg.destroyWindowDelay == 1);

    cfg.applyKeyValue("destroy-window-delay", "100000");
    REQUIRE(cfg.destroyWindowDelay == 60000);

    // Frame thickness clamped to 1-50
    cfg.applyKeyValue("frame-thickness", "0");
    REQUIRE(cfg.frameThickness == 1);

    cfg.applyKeyValue("frame-thickness", "100");
    REQUIRE(cfg.frameThickness == 50);

    cfg.applyKeyValue("frame-thickness", "-10");
    REQUIRE(cfg.frameThickness == 1);
}

// =============================================================================
// Test 15: Lines > 4096 chars are skipped with warning
// =============================================================================
TEST_CASE("Lines exceeding 4096 chars are skipped", "[config]") {
    std::string longLine(4100, 'x');
    longLine += " = value";

    std::string path = writeTempConfig(longLine + "\ntab-foreground = green\n");

    Config cfg;
    cfg.applyFile(path);

    // Long line should be skipped, valid line should parse
    REQUIRE(cfg.tabForeground == "green");

    removeTempFile(path);
}

// =============================================================================
// Test 16: Values > 256 chars are rejected with warning
// =============================================================================
TEST_CASE("Values exceeding 256 chars are rejected", "[config]") {
    std::string longValue(300, 'a');

    std::string path = writeTempConfig(
        "tab-foreground = " + longValue + "\n"
        "tab-background = blue\n"
    );

    Config cfg;
    cfg.applyFile(path);

    // Long value should be rejected (default kept)
    REQUIRE(cfg.tabForeground == "black");
    // Normal value should parse
    REQUIRE(cfg.tabBackground == "blue");

    removeTempFile(path);
}

// =============================================================================
// Test 17: xdgConfigHome ignores relative XDG_CONFIG_HOME
// =============================================================================
TEST_CASE("xdgConfigHome ignores relative XDG_CONFIG_HOME", "[config][xdg]") {
    char* origXdg = std::getenv("XDG_CONFIG_HOME");
    char* origHome = std::getenv("HOME");

    std::string origXdgStr, origHomeStr;
    if (origXdg) origXdgStr = origXdg;
    if (origHome) origHomeStr = origHome;

    // Set relative path (should be ignored)
    setenv("XDG_CONFIG_HOME", "relative/config", 1);
    setenv("HOME", "/home/testuser2", 1);

    std::string result = xdgConfigHome();
    REQUIRE(result == "/home/testuser2/.config");

    // Restore
    if (origXdgStr.empty()) unsetenv("XDG_CONFIG_HOME");
    else setenv("XDG_CONFIG_HOME", origXdgStr.c_str(), 1);
    if (!origHomeStr.empty()) setenv("HOME", origHomeStr.c_str(), 1);
}

// =============================================================================
// Test 18: xdgConfigDirs skips relative paths
// =============================================================================
TEST_CASE("xdgConfigDirs skips relative paths", "[config][xdg]") {
    char* orig = std::getenv("XDG_CONFIG_DIRS");
    std::string origStr;
    if (orig) origStr = orig;

    // Mix absolute and relative paths
    setenv("XDG_CONFIG_DIRS", "relative/path:/etc/xdg:another/relative", 1);

    auto dirs = xdgConfigDirs();
    REQUIRE(dirs.size() == 1);
    REQUIRE(dirs[0] == "/etc/xdg");

    // Restore
    if (origStr.empty()) unsetenv("XDG_CONFIG_DIRS");
    else setenv("XDG_CONFIG_DIRS", origStr.c_str(), 1);
}

// =============================================================================
// Test 19: applyKeyValue handles non-numeric int values gracefully
// =============================================================================
TEST_CASE("applyKeyValue handles non-numeric int values gracefully", "[config]") {
    Config cfg;

    // Should not crash, should warn and keep default
    cfg.applyKeyValue("frame-thickness", "not-a-number");
    REQUIRE(cfg.frameThickness == 7);

    cfg.applyKeyValue("auto-raise-delay", "abc");
    REQUIRE(cfg.autoRaiseDelay == 400);
}
