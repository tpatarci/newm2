#pragma once

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
    bool clickToFocus = false;
    bool raiseOnFocus = false;
    bool autoRaise    = false;
    // Timing (milliseconds)
    int autoRaiseDelay      = 400;
    int pointerStoppedDelay = 80;
    int destroyWindowDelay  = 1500;
    // Frame
    int frameThickness = 7;
    // Commands
    std::string newWindowCommand = "xterm";
    bool execUsingShell = false;

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
