#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "Config.h"

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <sys/wait.h>

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

// =============================================================================
// CLI Tests (CONF-04): Command-line option parsing via applyCliArgs()
// =============================================================================

// Test 20: CLI --tab-foreground=red sets tabForeground to "red"
TEST_CASE("CLI --tab-foreground sets string value", "[config][cli]") {
    Config cfg;
    char* argv[] = { const_cast<char*>("wm2"), const_cast<char*>("--tab-foreground=red"), nullptr };
    cfg.applyCliArgs(2, argv);
    REQUIRE(cfg.tabForeground == "red");
}

// Test 21: CLI --frame-thickness=3 sets frameThickness to 3
TEST_CASE("CLI --frame-thickness sets integer value", "[config][cli]") {
    Config cfg;
    char* argv[] = { const_cast<char*>("wm2"), const_cast<char*>("--frame-thickness=3"), nullptr };
    cfg.applyCliArgs(2, argv);
    REQUIRE(cfg.frameThickness == 3);
}

// Test 22: CLI --auto-raise sets autoRaise to true
TEST_CASE("CLI --auto-raise enables boolean", "[config][cli]") {
    Config cfg;
    REQUIRE(cfg.autoRaise == false);
    char* argv[] = { const_cast<char*>("wm2"), const_cast<char*>("--auto-raise"), nullptr };
    cfg.applyCliArgs(2, argv);
    REQUIRE(cfg.autoRaise == true);
}

// Test 23: CLI --no-auto-raise sets autoRaise to false (even if config set it true)
TEST_CASE("CLI --no-auto-raise disables boolean", "[config][cli]") {
    Config cfg;
    cfg.autoRaise = true;  // Simulate config file setting it
    char* argv[] = { const_cast<char*>("wm2"), const_cast<char*>("--no-auto-raise"), nullptr };
    cfg.applyCliArgs(2, argv);
    REQUIRE(cfg.autoRaise == false);
}

// Test 24: CLI --click-to-focus sets clickToFocus to true
TEST_CASE("CLI --click-to-focus enables boolean", "[config][cli]") {
    Config cfg;
    char* argv[] = { const_cast<char*>("wm2"), const_cast<char*>("--click-to-focus"), nullptr };
    cfg.applyCliArgs(2, argv);
    REQUIRE(cfg.clickToFocus == true);
}

// Test 25: CLI --no-click-to-focus sets clickToFocus to false
TEST_CASE("CLI --no-click-to-focus disables boolean", "[config][cli]") {
    Config cfg;
    cfg.clickToFocus = true;
    char* argv[] = { const_cast<char*>("wm2"), const_cast<char*>("--no-click-to-focus"), nullptr };
    cfg.applyCliArgs(2, argv);
    REQUIRE(cfg.clickToFocus == false);
}

// Test 26: Multiple CLI options on same command line all applied
TEST_CASE("CLI multiple options all applied", "[config][cli]") {
    Config cfg;
    char* argv[] = {
        const_cast<char*>("wm2"),
        const_cast<char*>("--tab-foreground=white"),
        const_cast<char*>("--frame-thickness=12"),
        const_cast<char*>("--auto-raise"),
        const_cast<char*>("--new-window-command=alacritty"),
        nullptr
    };
    cfg.applyCliArgs(5, argv);
    REQUIRE(cfg.tabForeground == "white");
    REQUIRE(cfg.frameThickness == 12);
    REQUIRE(cfg.autoRaise == true);
    REQUIRE(cfg.newWindowCommand == "alacritty");
}

// Test 27: Precedence: CLI overrides config file values
TEST_CASE("CLI overrides config file values", "[config][cli]") {
    Config cfg;
    // Simulate config file setting thickness=10
    cfg.frameThickness = 10;
    cfg.tabForeground = "blue";
    cfg.autoRaise = true;

    // CLI sets different values
    char* argv[] = {
        const_cast<char*>("wm2"),
        const_cast<char*>("--frame-thickness=3"),
        const_cast<char*>("--tab-foreground=red"),
        const_cast<char*>("--no-auto-raise"),
        nullptr
    };
    cfg.applyCliArgs(4, argv);

    REQUIRE(cfg.frameThickness == 3);       // CLI wins
    REQUIRE(cfg.tabForeground == "red");    // CLI wins
    REQUIRE(cfg.autoRaise == false);        // CLI wins
}

// Test 28: Unknown option prints error to stderr and exits with code 2
TEST_CASE("CLI unknown option causes exit", "[config][cli]") {
    // Fork a child process to test exit behavior
    int pipefd[2];
    REQUIRE(pipe(pipefd) == 0);

    pid_t pid = fork();
    REQUIRE(pid >= 0);

    if (pid == 0) {
        // Child process
        close(pipefd[0]);
        // Redirect stderr to pipe
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        Config cfg;
        char* argv[] = { const_cast<char*>("wm2"), const_cast<char*>("--unknown-option"), nullptr };
        cfg.applyCliArgs(2, argv);
        _exit(0);  // Should not reach here
    }

    // Parent process
    close(pipefd[1]);

    // Read stderr from child
    char buf[1024] = {};
    ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    // Verify child exited with code 2
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) == 2);

    // Verify error message was printed
    std::string output(buf, n > 0 ? n : 0);
    REQUIRE(output.find("Try") != std::string::npos);
    REQUIRE(output.find("--help") != std::string::npos);
}

// Test 29: -- terminates option parsing (per GNU convention)
TEST_CASE("CLI -- terminates option parsing", "[config][cli]") {
    Config cfg;
    char* argv[] = {
        const_cast<char*>("wm2"),
        const_cast<char*>("--frame-thickness=5"),
        const_cast<char*>("--"),
        const_cast<char*>("--tab-foreground=blue"),
        nullptr
    };
    cfg.applyCliArgs(4, argv);

    REQUIRE(cfg.frameThickness == 5);
    // --tab-foreground=blue should NOT be parsed (after --)
    REQUIRE(cfg.tabForeground == "black");  // default remains
}

// Test 30: CLI --new-window-command="alacritty" sets newWindowCommand
TEST_CASE("CLI --new-window-command sets string value", "[config][cli]") {
    Config cfg;
    char* argv[] = { const_cast<char*>("wm2"), const_cast<char*>("--new-window-command=alacritty"), nullptr };
    cfg.applyCliArgs(2, argv);
    REQUIRE(cfg.newWindowCommand == "alacritty");
}

// Test 31: CLI --auto-raise-delay=200 sets autoRaiseDelay to 200
TEST_CASE("CLI --auto-raise-delay sets integer value", "[config][cli]") {
    Config cfg;
    char* argv[] = { const_cast<char*>("wm2"), const_cast<char*>("--auto-raise-delay=200"), nullptr };
    cfg.applyCliArgs(2, argv);
    REQUIRE(cfg.autoRaiseDelay == 200);
}

// Test 32: CLI --exec-using-shell sets execUsingShell to true
TEST_CASE("CLI --exec-using-shell enables boolean", "[config][cli]") {
    Config cfg;
    char* argv[] = { const_cast<char*>("wm2"), const_cast<char*>("--exec-using-shell"), nullptr };
    cfg.applyCliArgs(2, argv);
    REQUIRE(cfg.execUsingShell == true);
}

// Test 33: CLI --no-exec-using-shell sets execUsingShell to false
TEST_CASE("CLI --no-exec-using-shell disables boolean", "[config][cli]") {
    Config cfg;
    cfg.execUsingShell = true;
    char* argv[] = { const_cast<char*>("wm2"), const_cast<char*>("--no-exec-using-shell"), nullptr };
    cfg.applyCliArgs(2, argv);
    REQUIRE(cfg.execUsingShell == false);
}

// Test 34: Integer CLI values are clamped same as config file values
TEST_CASE("CLI integer values are clamped to valid ranges", "[config][cli]") {
    Config cfg;

    // Thickness clamped to 1-50
    char* argv1[] = { const_cast<char*>("wm2"), const_cast<char*>("--frame-thickness=100"), nullptr };
    cfg.applyCliArgs(2, argv1);
    REQUIRE(cfg.frameThickness == 50);

    // Reset
    Config cfg2;
    char* argv2[] = { const_cast<char*>("wm2"), const_cast<char*>("--frame-thickness=-5"), nullptr };
    cfg2.applyCliArgs(2, argv2);
    REQUIRE(cfg2.frameThickness == 1);

    // Delay clamped to 1-60000
    Config cfg3;
    char* argv3[] = { const_cast<char*>("wm2"), const_cast<char*>("--auto-raise-delay=70000"), nullptr };
    cfg3.applyCliArgs(2, argv3);
    REQUIRE(cfg3.autoRaiseDelay == 60000);

    Config cfg4;
    char* argv4[] = { const_cast<char*>("wm2"), const_cast<char*>("--auto-raise-delay=0"), nullptr };
    cfg4.applyCliArgs(2, argv4);
    REQUIRE(cfg4.autoRaiseDelay == 1);
}

// Test 35: No CLI args leaves config unchanged
TEST_CASE("CLI no args leaves config unchanged", "[config][cli]") {
    Config cfg;
    char* argv[] = { const_cast<char*>("wm2"), nullptr };
    cfg.applyCliArgs(1, argv);

    REQUIRE(cfg.tabForeground == "black");
    REQUIRE(cfg.frameThickness == 7);
    REQUIRE(cfg.autoRaise == false);
    REQUIRE(cfg.newWindowCommand == "xterm");
}
