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
#include <cstring>

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

    // Rule grouping state is PER FILE (D-20). A rule left open at the end of
    // the system config file must not swallow the first match key of the user
    // config file, and a trailing action there must not be the boundary that
    // opens the user file's first rule. The accumulated `rules` vector is
    // deliberately NOT reset -- rules append across layers in file order.
    RuleParseState ruleState;

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

        applyKeyValue(key, value, ruleState);
    }
}

// =============================================================================
// Config::applyKeyValue - Apply a single key=value pair
// =============================================================================

void Config::applyKeyValue(const std::string& key, const std::string& value) {
    applyKeyValue(key, value, ruleParseState);
}

void Config::applyKeyValue(const std::string& key, const std::string& value,
                           RuleParseState& ruleState) {
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
    if (key == "focus-stealing-prevention") { focusStealingPrevention = parseBool(value); return; }

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

    // Manual menu entries (APPS-04): menu-entry-name= starts a new pending
    // AppEntry pushed onto manualMenuEntries; menu-entry-command= and
    // menu-entry-category= fill in manualMenuEntries.back(). This
    // accumulator pattern avoids numbered keys (menu-entry-1-name=) --
    // each menu-entry-name= opens a new "currently open entry".
    if (key == "menu-entry-name") {
        AppEntry entry;
        entry.name = value;
        entry.category = "Custom";  // D-07 default, applied at creation time
        entry.source = AppEntry::Source::Manual;
        manualMenuEntries.push_back(entry);
        return;
    }

    if (key == "menu-entry-command") {
        if (manualMenuEntries.empty()) {
            std::fprintf(stderr, "wm2: warning: menu-entry-command with no preceding menu-entry-name\n");
            return;
        }
        // Manual entries are user-typed directly into their own config
        // file (a trusted source, per the config-manual-entries threat
        // disposition) -- simple whitespace tokenization is sufficient,
        // unlike .desktop's Exec= quoting/field-code handling.
        std::vector<std::string> tokens;
        std::istringstream ss(value);
        std::string token;
        while (ss >> token) {
            tokens.push_back(token);
        }
        manualMenuEntries.back().execArgv = tokens;
        return;
    }

    if (key == "menu-entry-category") {
        if (manualMenuEntries.empty()) {
            std::fprintf(stderr, "wm2: warning: menu-entry-category with no preceding menu-entry-name\n");
            return;
        }
        manualMenuEntries.back().category = value;
        return;
    }

    // =========================================================================
    // Window rules (RULES-01, D-20/D-21/D-22)
    //
    // Rules are repeated ORDERED key groups in this same key=value file. There
    // is deliberately no second file format and no numbered-key scheme
    // (rule-1-match-class=); this follows the menu-entry accumulator above,
    // which is the precedent D-20 names.
    //
    // The grouping rule is one sentence a non-programmer can hold:
    //
    //     A NEW RULE BEGINS AT THE FIRST MATCH-OR-MODE LINE THAT FOLLOWS AN
    //     ACTION LINE, OR AT THE VERY FIRST RULE LINE IN THE FILE.
    //
    // Consecutive match and mode lines attach to the rule currently open and
    // are AND-ed (D-21); action lines attach to that same open rule. Keys that
    // are not rule keys are transparent -- they leave the grouping state
    // exactly as they found it. So:
    //
    //     rule-match-class = Firefox      # opens rule 1
    //     rule-match-name  = navigator    # AND-ed into rule 1
    //     rule-position    = 100,100      # action on rule 1
    //     rule-size        = 800x600      # action on rule 1
    //     rule-match-type  = dialog       # action boundary crossed -> rule 2
    //     rule-no-decorate = true         # action on rule 2
    //
    // ruleState is per-file when the caller is applyFile(), and a member when
    // the caller is the public single-pair applyKeyValue(). Both are explicit
    // for the same reason: implicit "is a rule open?" state derived from
    // rules.empty() would let a system file's last rule absorb the first match
    // key of the user file.
    //
    // Deliberately NO CLI flags for any of this. Repeated ordered groups do not
    // map onto getopt's scalar option model -- there is no way to express "this
    // --rule-position belongs to that --rule-match-class" on a command line --
    // and the existing enable/negate flag pairs exist for scalar booleans only.
    // The omission is a decision, not an oversight.
    // =========================================================================

    const bool isRuleMatchKey =
        (key == "rule-match-class" || key == "rule-match-name" ||
         key == "rule-match-type"  || key == "rule-match-mode");

    const bool isRuleActionKey =
        (key == "rule-no-decorate" || key == "rule-position" ||
         key == "rule-size"        || key == "rule-skip-taskbar");

    if (isRuleMatchKey) {
        if (!ruleState.ruleOpen || ruleState.ruleLastWasAction) {
            rules.push_back(WindowRule());
            ruleState.ruleOpen = true;
        }
        ruleState.ruleLastWasAction = false;

        WindowRule& rule = rules.back();

        if (key == "rule-match-class") {
            rule.hasMatchClass = true;
            rule.matchClass = value;
            return;
        }

        if (key == "rule-match-name") {
            rule.hasMatchName = true;
            rule.matchName = value;
            return;
        }

        if (key == "rule-match-type") {
            // Exactly the four types the WM distinguishes. UTILITY, SPLASH and
            // TOOLBAR are collapsed into Normal at runtime (D-04), so accepting
            // those spellings here would hand the user a rule that silently
            // never fires -- a warning is strictly more useful than that.
            std::string lower = toLower(value);
            if      (lower == "normal")       rule.matchType = RuleWindowType::Normal;
            else if (lower == "dock")         rule.matchType = RuleWindowType::Dock;
            else if (lower == "dialog")       rule.matchType = RuleWindowType::Dialog;
            else if (lower == "notification") rule.matchType = RuleWindowType::Notification;
            else {
                std::fprintf(stderr,
                             "wm2: warning: config key 'rule-match-type': unknown window type '%s' "
                             "(accepted: normal, dock, dialog, notification)\n",
                             value.c_str());
                return;  // criterion left unset -- see the comment above
            }
            rule.hasMatchType = true;
            return;
        }

        // rule-match-mode
        std::string lower = toLower(value);
        if      (lower == "exact")     rule.mode = RuleMatchMode::Exact;
        else if (lower == "substring") rule.mode = RuleMatchMode::Substring;
        else {
            std::fprintf(stderr,
                         "wm2: warning: config key 'rule-match-mode': unknown match mode '%s' "
                         "(accepted: exact, substring)\n",
                         value.c_str());
        }
        return;
    }

    if (isRuleActionKey) {
        if (!ruleState.ruleOpen) {
            std::fprintf(stderr,
                         "wm2: warning: config key '%s': action with no preceding rule-match-* key\n",
                         key.c_str());
            // An orphan action is still a syntactic action BOUNDARY even though
            // it contributes nothing, so the next match key opens a fresh rule
            // rather than silently inheriting whatever came before.
            ruleState.ruleLastWasAction = true;
            return;
        }
        ruleState.ruleLastWasAction = true;

        WindowRule& rule = rules.back();

        if (key == "rule-no-decorate") {
            rule.noDecorate = parseBool(value) ? RuleTriState::On : RuleTriState::Off;
            return;
        }

        if (key == "rule-skip-taskbar") {
            rule.skipTaskbar = parseBool(value) ? RuleTriState::On : RuleTriState::Off;
            return;
        }

        if (key == "rule-position") {
            // "X,Y"
            auto comma = value.find(',');
            if (comma == std::string::npos) {
                std::fprintf(stderr,
                             "wm2: warning: config key 'rule-position': expected X,Y but got '%s'\n",
                             value.c_str());
                return;
            }
            try {
                int x = clampInt(std::stoi(value.substr(0, comma)), -65535, 65535);
                int y = clampInt(std::stoi(value.substr(comma + 1)), -65535, 65535);
                rule.posX = x;
                rule.posY = y;
                rule.hasPosition = true;
            } catch (const std::invalid_argument&) {
                std::fprintf(stderr, "wm2: warning: config key 'rule-position': invalid integer '%s'\n", value.c_str());
            } catch (const std::out_of_range&) {
                std::fprintf(stderr, "wm2: warning: config key 'rule-position': value out of range '%s'\n", value.c_str());
            }
            return;
        }

        // rule-size: "WxH"
        std::string lower = toLower(value);
        auto sep = lower.find('x');
        if (sep == std::string::npos) {
            std::fprintf(stderr,
                         "wm2: warning: config key 'rule-size': expected WxH but got '%s'\n",
                         value.c_str());
            return;
        }
        try {
            int w = clampInt(std::stoi(lower.substr(0, sep)), 1, 65535);
            int h = clampInt(std::stoi(lower.substr(sep + 1)), 1, 65535);
            rule.width = w;
            rule.height = h;
            rule.hasSize = true;
        } catch (const std::invalid_argument&) {
            std::fprintf(stderr, "wm2: warning: config key 'rule-size': invalid integer '%s'\n", value.c_str());
        } catch (const std::out_of_range&) {
            std::fprintf(stderr, "wm2: warning: config key 'rule-size': value out of range '%s'\n", value.c_str());
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
    cfg.applyCliArgs(argc, argv);

    return cfg;
}

// =============================================================================
// Config::applyCliArgs - CLI argument parsing with getopt_long
// =============================================================================

static struct option longOptions[] = {
    // String settings with required_argument
    {"tab-foreground",        required_argument, nullptr, 0},
    {"tab-background",        required_argument, nullptr, 0},
    {"frame-background",      required_argument, nullptr, 0},
    {"button-background",     required_argument, nullptr, 0},
    {"borders",               required_argument, nullptr, 0},
    {"menu-foreground",       required_argument, nullptr, 0},
    {"menu-background",       required_argument, nullptr, 0},
    {"menu-highlight",        required_argument, nullptr, 0},
    {"menu-borders",          required_argument, nullptr, 0},
    {"frame-thickness",       required_argument, nullptr, 0},
    {"auto-raise-delay",      required_argument, nullptr, 0},
    {"pointer-stopped-delay", required_argument, nullptr, 0},
    {"destroy-window-delay",  required_argument, nullptr, 0},
    {"new-window-command",    required_argument, nullptr, 0},

    // Boolean flags: presence = enable (per D-03)
    {"click-to-focus",        no_argument, nullptr, 0},
    {"raise-on-focus",        no_argument, nullptr, 0},
    {"auto-raise",            no_argument, nullptr, 0},
    {"exec-using-shell",      no_argument, nullptr, 0},
    {"focus-stealing-prevention", no_argument, nullptr, 0},

    // Boolean negations: --no-xxx = disable (per D-03)
    {"no-click-to-focus",     no_argument, nullptr, 0},
    {"no-raise-on-focus",     no_argument, nullptr, 0},
    {"no-auto-raise",         no_argument, nullptr, 0},
    {"no-exec-using-shell",   no_argument, nullptr, 0},
    {"no-focus-stealing-prevention", no_argument, nullptr, 0},

    {nullptr, 0, nullptr, 0}
};

void Config::applyCliArgs(int argc, char** argv) {
    optind = 1;  // Reset for re-parsing

    while (true) {
        int optionIndex = 0;
        int c = getopt_long(argc, argv, "", longOptions, &optionIndex);

        if (c == -1) break;  // No more options

        if (c == '?') {
            // getopt_long already printed an error message
            std::fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
            std::exit(2);
        }

        // c == 0: long option matched
        const char* name = longOptions[optionIndex].name;

        // Boolean enable (--click-to-focus)
        if (std::strcmp(name, "click-to-focus") == 0)        clickToFocus = true;
        else if (std::strcmp(name, "raise-on-focus") == 0)   raiseOnFocus = true;
        else if (std::strcmp(name, "auto-raise") == 0)       autoRaise = true;
        else if (std::strcmp(name, "exec-using-shell") == 0) execUsingShell = true;
        else if (std::strcmp(name, "focus-stealing-prevention") == 0)
            focusStealingPrevention = true;

        // Boolean disable (--no-click-to-focus)
        else if (std::strcmp(name, "no-click-to-focus") == 0)   clickToFocus = false;
        else if (std::strcmp(name, "no-raise-on-focus") == 0)   raiseOnFocus = false;
        else if (std::strcmp(name, "no-auto-raise") == 0)       autoRaise = false;
        else if (std::strcmp(name, "no-exec-using-shell") == 0) execUsingShell = false;
        else if (std::strcmp(name, "no-focus-stealing-prevention") == 0)
            focusStealingPrevention = false;

        // String settings
        else if (std::strcmp(name, "tab-foreground") == 0)      tabForeground = optarg;
        else if (std::strcmp(name, "tab-background") == 0)      tabBackground = optarg;
        else if (std::strcmp(name, "frame-background") == 0)    frameBackground = optarg;
        else if (std::strcmp(name, "button-background") == 0)   buttonBackground = optarg;
        else if (std::strcmp(name, "borders") == 0)             borders = optarg;
        else if (std::strcmp(name, "menu-foreground") == 0)     menuForeground = optarg;
        else if (std::strcmp(name, "menu-background") == 0)     menuBackground = optarg;
        else if (std::strcmp(name, "menu-highlight") == 0)      menuHighlight = optarg;
        else if (std::strcmp(name, "menu-borders") == 0)        menuBorders = optarg;
        else if (std::strcmp(name, "new-window-command") == 0)  newWindowCommand = optarg;

        // Integer settings (with clamping and error handling)
        else if (std::strcmp(name, "frame-thickness") == 0) {
            try {
                frameThickness = clampInt(std::stoi(optarg), 1, 50);
            } catch (const std::exception&) {
                std::fprintf(stderr, "wm2: warning: invalid --frame-thickness value '%s'\n", optarg);
            }
        }
        else if (std::strcmp(name, "auto-raise-delay") == 0) {
            try {
                autoRaiseDelay = clampInt(std::stoi(optarg), 1, 60000);
            } catch (const std::exception&) {
                std::fprintf(stderr, "wm2: warning: invalid --auto-raise-delay value '%s'\n", optarg);
            }
        }
        else if (std::strcmp(name, "pointer-stopped-delay") == 0) {
            try {
                pointerStoppedDelay = clampInt(std::stoi(optarg), 1, 60000);
            } catch (const std::exception&) {
                std::fprintf(stderr, "wm2: warning: invalid --pointer-stopped-delay value '%s'\n", optarg);
            }
        }
        else if (std::strcmp(name, "destroy-window-delay") == 0) {
            try {
                destroyWindowDelay = clampInt(std::stoi(optarg), 1, 60000);
            } catch (const std::exception&) {
                std::fprintf(stderr, "wm2: warning: invalid --destroy-window-delay value '%s'\n", optarg);
            }
        }
    }
}
