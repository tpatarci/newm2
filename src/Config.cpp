#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "Config.h"

#include <getopt.h>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

// Helper: trim leading/trailing whitespace in-place
static void trim(std::string& s) {
    auto a = s.find_first_not_of(" \t");
    auto b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) {
        s.clear();
    } else {
        s = s.substr(a, b - a + 1);
    }
}

// Helper: convert string to lowercase
static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Helper: clamp an integer to [min, max]
static int clampInt(int value, int minVal, int maxVal) {
    if (value < minVal) return minVal;
    if (value > maxVal) return maxVal;
    return value;
}

// Helper: parse a boolean string value
static bool parseBool(const std::string& value) {
    std::string lower = toLower(value);
    return (lower == "true" || lower == "1");
}

// =============================================================================
// XDG path resolution
// =============================================================================

std::string xdgConfigHome() {
    const char* home = std::getenv("XDG_CONFIG_HOME");
    if (home && home[0] == '/') return home;

    const char* userHome = std::getenv("HOME");
    if (!userHome) userHome = "/tmp";
    return std::string(userHome) + "/.config";
}

std::vector<std::string> xdgConfigDirs() {
    const char* dirs = std::getenv("XDG_CONFIG_DIRS");
    std::vector<std::string> result;

    if (dirs && dirs[0] != '\0') {
        std::istringstream ss(dirs);
        std::string dir;
        while (std::getline(ss, dir, ':')) {
            if (!dir.empty() && dir[0] == '/') {
                result.push_back(dir);
            }
        }
    }

    if (result.empty()) {
        result.push_back("/etc/xdg");
    }
    return result;
}

// =============================================================================
// Config::applyFile - Parse a key=value config file
// =============================================================================

void Config::applyFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;  // File doesn't exist -- skip silently

    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        ++lineNum;

        // Skip lines > 4096 chars
        if (line.size() > 4096) {
            std::fprintf(stderr, "wm2: warning: config line %d: line too long, skipping\n", lineNum);
            continue;
        }

        // Strip leading whitespace
        auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;  // blank line
        if (line[start] == '#') continue;           // comment

        // Strip trailing whitespace
        auto end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);

        // Split on first '='
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            std::fprintf(stderr, "wm2: warning: config line %d: missing '='\n", lineNum);
            continue;
        }

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        // Trim key and value
        trim(key);
        trim(value);

        // Reject values > 256 chars
        if (value.size() > 256) {
            std::fprintf(stderr, "wm2: warning: config line %d: value too long, skipping\n", lineNum);
            continue;
        }

        applyKeyValue(key, value);
    }
}

// =============================================================================
// Config::applyKeyValue - Apply a single key=value pair
// =============================================================================

void Config::applyKeyValue(const std::string& key, const std::string& value) {
    // String settings (colors)
    if (key == "tab-foreground")      { tabForeground = value; return; }
    if (key == "tab-background")      { tabBackground = value; return; }
    if (key == "frame-background")    { frameBackground = value; return; }
    if (key == "button-background")   { buttonBackground = value; return; }
    if (key == "borders")             { borders = value; return; }
    if (key == "menu-foreground")     { menuForeground = value; return; }
    if (key == "menu-background")     { menuBackground = value; return; }
    if (key == "menu-highlight")      { menuHighlight = value; return; }
    if (key == "menu-borders")        { menuBorders = value; return; }

    // String settings (commands)
    if (key == "new-window-command")  { newWindowCommand = value; return; }

    // Boolean settings
    if (key == "click-to-focus")      { clickToFocus = parseBool(value); return; }
    if (key == "raise-on-focus")      { raiseOnFocus = parseBool(value); return; }
    if (key == "auto-raise")          { autoRaise = parseBool(value); return; }
    if (key == "exec-using-shell")    { execUsingShell = parseBool(value); return; }

    // Integer settings (with clamping and error handling)
    if (key == "auto-raise-delay" || key == "pointer-stopped-delay" ||
        key == "destroy-window-delay") {
        try {
            int parsed = std::stoi(value);
            parsed = clampInt(parsed, 1, 60000);
            if (key == "auto-raise-delay")       autoRaiseDelay = parsed;
            else if (key == "pointer-stopped-delay") pointerStoppedDelay = parsed;
            else                                   destroyWindowDelay = parsed;
        } catch (const std::invalid_argument&) {
            std::fprintf(stderr, "wm2: warning: config key '%s': invalid integer '%s'\n", key.c_str(), value.c_str());
        } catch (const std::out_of_range&) {
            std::fprintf(stderr, "wm2: warning: config key '%s': value out of range '%s'\n", key.c_str(), value.c_str());
        }
        return;
    }

    if (key == "frame-thickness") {
        try {
            int parsed = std::stoi(value);
            frameThickness = clampInt(parsed, 1, 50);
        } catch (const std::invalid_argument&) {
            std::fprintf(stderr, "wm2: warning: config key 'frame-thickness': invalid integer '%s'\n", value.c_str());
        } catch (const std::out_of_range&) {
            std::fprintf(stderr, "wm2: warning: config key 'frame-thickness': value out of range '%s'\n", value.c_str());
        }
        return;
    }

    // Unknown key -- warn but don't abort
    std::fprintf(stderr, "wm2: warning: unknown config key '%s'\n", key.c_str());
}

// =============================================================================
// Config::load - Load config from all sources in precedence order
// =============================================================================

Config Config::load(int argc, char** argv) {
    Config cfg;  // Start with built-in defaults

    // Layer 1: System config files (lowest precedence)
    for (const auto& dir : xdgConfigDirs()) {
        cfg.applyFile(dir + "/wm2-born-again/config");
    }

    // Layer 2: User config file
    cfg.applyFile(xdgConfigHome() + "/wm2-born-again/config");

    // Layer 3: CLI overrides (highest precedence)
    // Stub -- Plan 02 implements this
    (void)argc;
    (void)argv;

    return cfg;
}

// =============================================================================
// Config::applyCliArgs - CLI argument parsing (stub for Plan 02)
// =============================================================================

void Config::applyCliArgs(int argc, char** argv) {
    // Stub -- Plan 02 implements getopt_long CLI parsing
    (void)argc;
    (void)argv;
}
