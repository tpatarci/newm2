#pragma once

#include "AppEntry.h"

#include <string>
#include <vector>

struct Config {
    // Colors (tab)
    std::string tabForeground   = "black";
    std::string tabBackground   = "gray80";
    // Colors (frame)
    std::string frameBackground = "gray95";
    std::string buttonBackground = "gray95";
    std::string borders         = "black";
    // Colors (menu)
    std::string menuForeground  = "black";
    std::string menuBackground  = "gray80";
    std::string menuHighlight   = "gray60";
    std::string menuBorders     = "black";
    // Focus policy
    //
    // D-17: these three values were CORRECTED to describe what the binary
    // actually does. Until FOCUS-02 wired them up (plan 08-07) nothing in the
    // runtime read them, so the shipped defaults could say all-false while the
    // WM unconditionally performed pointer focus with auto-raise, and raising
    // was fused into focusing. The literal previous values had never reflected
    // reality; resolving that contradiction in their favour would have silently
    // changed behaviour for every existing user the moment the gates landed.
    //
    // So: pointer focus (click-to-focus off), auto-raise on, raise-on-focus on
    // -- which is exactly what a user with no config file got before, and gets
    // now.
    bool clickToFocus = false;
    bool raiseOnFocus = true;
    bool autoRaise    = true;
    // Timing (milliseconds)
    int autoRaiseDelay      = 400;
    int pointerStoppedDelay = 80;
    int destroyWindowDelay  = 1500;
    // Frame
    int frameThickness = 7;
    // Commands
    std::string newWindowCommand = "xterm";
    bool execUsingShell = false;

    // Manual menu entries (APPS-04)
    std::vector<AppEntry> manualMenuEntries;

    // Load config: defaults -> system config -> user config -> CLI overrides
    static Config load(int argc, char** argv);

    // Apply a config file (only sets keys present in the file)
    void applyFile(const std::string& path);

    // Apply a single key=value pair
    void applyKeyValue(const std::string& key, const std::string& value);

    // Apply CLI arguments (getopt_long)
    void applyCliArgs(int argc, char** argv);
};

// XDG path resolution (exposed for testing)
std::string xdgConfigHome();
std::vector<std::string> xdgConfigDirs();
