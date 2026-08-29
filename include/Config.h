#pragma once

#include "AppEntry.h"
#include "Rules.h"

#include <string>
#include <vector>

// Parser bookkeeping for the repeated rule-* key groups (RULES-01, D-20).
//
// Two pieces of state, both explicit, because the grouping rule needs both:
// whether a rule is currently open for keys to attach to, and whether the last
// rule key seen was an action (which is what makes the NEXT match key open a
// fresh group). A file boundary resets both -- see Config::applyFile.
struct RuleParseState {
    bool ruleOpen = false;
    bool ruleLastWasAction = false;
};

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
    // FOCUS-01 (plan 08-08): focus-stealing prevention, ON by default.
    //
    // Unlike the three above, this one defaults to its safe value rather than to
    // the previous behaviour, because it IS the mitigation: a default of false
    // would ship the feature switched off. The switch exists at all for D-19's
    // reason -- a user whose important legacy applications set no
    // _NET_WM_USER_TIME may prefer the old unconditional grant to a correct
    // refusal, and that is their informed choice to make (threat T-8-OFF).
    bool focusStealingPrevention = true;
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

    // Window rules (RULES-01), in file order across every config layer. The
    // fold in src/Rules.cpp is later-wins, so this order is load-bearing.
    std::vector<WindowRule> rules;

    // Grouping state for the sequential applyKeyValue() entry point below.
    // Bookkeeping, not a setting: applyFile() uses its own per-file instance.
    RuleParseState ruleParseState;

    // Load config: defaults -> system config -> user config -> CLI overrides
    static Config load(int argc, char** argv);

    // Apply a config file (only sets keys present in the file)
    void applyFile(const std::string& path);

    // Apply a single key=value pair. Rule grouping uses the member state above,
    // so repeated sequential calls behave exactly like consecutive lines of one
    // file -- which is what makes rule parsing deterministic in unit tests.
    void applyKeyValue(const std::string& key, const std::string& value);

    // As above, but with caller-owned grouping state. applyFile() uses this so
    // an open rule cannot survive the end of the file that opened it.
    void applyKeyValue(const std::string& key, const std::string& value,
                       RuleParseState& ruleState);

    // Apply CLI arguments (getopt_long)
    void applyCliArgs(int argc, char** argv);
};

// XDG path resolution (exposed for testing)
std::string xdgConfigHome();
std::vector<std::string> xdgConfigDirs();
