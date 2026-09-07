#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "Config.h"
#include "ConfigProtocol.h"   // the one-line bound the accumulated entry list has to fit inside
#include "ConfigFileWriter.h" // kConfigFileMaxValueBytes -- the per-value bound the wire must respect too

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

// Helper: a manual menu entry value the RENDERER cannot express.
//
// configMenuEntriesValue() renders the manual entries as ';'-separated
// records and parseMenuEntriesValue() splits on ';' to read them back, so a
// ';' inside a name, a command or a category is not a character the round
// trip survives: `get menu-entries` would print a value `set menu-entries`
// refuses, and the two halves of one protocol would disagree about a list
// neither of them mistyped.
//
// REFUSED AT THE SOURCE rather than escaped in the renderer, because the wire
// parser already cannot PRODUCE such an entry -- it splits the value on ';'
// before any of it reaches the accumulator -- so the file was the only route
// that could store one, and refusing it there is what makes "everything
// stored is renderable" true of the whole program rather than of one function.
static bool menuEntryValueIsRenderable(const char* key, const std::string& value) {
    if (value.find(';') == std::string::npos) return true;
    std::fprintf(stderr,
                 "wm2: warning: %s: ';' separates menu entries and cannot appear "
                 "in a value; entry skipped\n", key);
    return false;
}

// Helper: keep the accumulated manual entry list inside ONE protocol reply.
//
// C2. The whole list travels as one value under `menu-entries`, so a file with
// enough individually valid `menu-entry-*` groups in it loads perfectly and
// then makes `get menu-entries` produce a reply both clients reject as
// TooLong -- and wm2-config disconnects during its opening read of a file this
// parser was entirely happy with. The bound is applied HERE, where the list is
// built, so what the window manager HOLDS is always what the wire can carry.
//
// Entries are dropped from the END, so the file's own order decides which
// survive, and the drop is announced rather than silent.
//
// The longest prefix that fits is found by BISECTION rather than by popping one
// entry at a time and re-rendering: a pathological file with tens of thousands
// of groups in it would make the naive loop quadratic in the number of entries,
// and a config parser is not a place to leave that. Rendering is monotonic in
// the prefix length -- every entry adds bytes and none removes any -- which is
// what makes the bisection exact rather than approximate.
static void boundMenuEntriesToOneReply(Config& config) {
    if (config.manualMenuEntries.empty()) return;

    const auto fits = [&config](std::size_t count) {
        Config scratch;
        scratch.manualMenuEntries.assign(
            config.manualMenuEntries.begin(),
            config.manualMenuEntries.begin() + static_cast<std::ptrdiff_t>(count));
        return configProtocolValueReplyLength(kMenuEntriesKey,
                                              configMenuEntriesValue(scratch)) <=
               kConfigProtocolMaxMenuEntriesReply;
    };

    const std::size_t total = config.manualMenuEntries.size();
    if (fits(total)) return;

    std::size_t lo = 0, hi = total;
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo + 1) / 2;   // always >= lo + 1
        if (fits(mid)) lo = mid;
        else           hi = mid - 1;
    }

    const std::size_t dropped = total - lo;
    config.manualMenuEntries.resize(lo);
    std::fprintf(stderr,
                 "wm2: warning: %zu menu %s dropped: the whole menu entry list "
                 "is sent to a settings client as one line and only the first "
                 "%zu of %zu fit in %zu bytes\n",
                 dropped, dropped == 1 ? "entry" : "entries", lo, total,
                 kConfigProtocolMaxMenuEntriesReply);
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

    // Applied per FILE, after the accumulator has seen every line of it. The
    // entry list appends across layers, so checking here bounds the running
    // total after each layer rather than each layer in isolation.
    boundMenuEntriesToOneReply(*this);
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

    // String settings (fonts). Fontconfig patterns, taken verbatim -- no
    // trimming, lowercasing or validation beyond what applyFile() already
    // applies to every line. See include/Config.h for why there is no separate
    // size key.
    if (key == "tab-font")            { tabFont = value; return; }
    if (key == "menu-font")           { menuFont = value; return; }

    // String settings (commands)
    if (key == "new-window-command")  { newWindowCommand = value; return; }

    // Boolean settings
    if (key == "click-to-focus")      { clickToFocus = parseBool(value); return; }
    if (key == "raise-on-focus")      { raiseOnFocus = parseBool(value); return; }
    if (key == "auto-raise")          { autoRaise = parseBool(value); return; }
    if (key == "exec-using-shell")    { execUsingShell = parseBool(value); return; }
    if (key == "focus-stealing-prevention") { focusStealingPrevention = parseBool(value); return; }

    // Integer settings (with clamping and error handling).
    //
    // The bounds come from configKeySpecFor(), which is a view of the same
    // option table --help and getopt_long() are generated from (plan 09-04).
    // They used to be spelled out here AND in applyCliArgs() AND described in
    // the summary prose, three places that could disagree about what "1 to 50"
    // meant. Behaviour is unchanged: the numbers in the table are the numbers
    // that were here.
    if (key == "auto-raise-delay" || key == "pointer-stopped-delay" ||
        key == "destroy-window-delay" || key == "frame-thickness") {
        const ConfigKeySpec* spec = configKeySpecFor(key);
        const int lo = spec ? spec->minValue : 1;
        const int hi = spec ? spec->maxValue : 60000;
        try {
            const int parsed = clampInt(std::stoi(value), lo, hi);
            if      (key == "auto-raise-delay")      autoRaiseDelay = parsed;
            else if (key == "pointer-stopped-delay") pointerStoppedDelay = parsed;
            else if (key == "destroy-window-delay")  destroyWindowDelay = parsed;
            else                                     frameThickness = parsed;
        } catch (const std::invalid_argument&) {
            std::fprintf(stderr, "wm2: warning: config key '%s': invalid integer '%s'\n", key.c_str(), value.c_str());
        } catch (const std::out_of_range&) {
            std::fprintf(stderr, "wm2: warning: config key '%s': value out of range '%s'\n", key.c_str(), value.c_str());
        }
        return;
    }

    // Manual menu entries (APPS-04): menu-entry-name= starts a new pending
    // AppEntry pushed onto manualMenuEntries; menu-entry-command= and
    // menu-entry-category= fill in manualMenuEntries.back(). This
    // accumulator pattern avoids numbered keys (menu-entry-1-name=) --
    // each menu-entry-name= opens a new "currently open entry".
    if (key == "menu-entry-name") {
        // Skipped WHOLE, so no entry is opened: a name that cannot be rendered
        // must not become a stored entry, and a half-formed entry carrying the
        // command and category lines that follow would be worse than none. The
        // consequence is stated rather than hidden -- those following lines
        // then attach to the previous entry, or warn about having no name to
        // attach to, which is the behaviour any other unusable name already has.
        if (!menuEntryValueIsRenderable("menu-entry-name", value)) return;

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
        //
        // The split moved into configTokeniseCommand() for plan 09-07: the
        // settings window's menu-entry dialog has to produce the same argument
        // vector this line produces, and "the same way" is only true of one
        // function with two callers.
        if (!menuEntryValueIsRenderable("menu-entry-command", value)) return;
        manualMenuEntries.back().execArgv = configTokeniseCommand(value);
        return;
    }

    if (key == "menu-entry-category") {
        if (manualMenuEntries.empty()) {
            std::fprintf(stderr, "wm2: warning: menu-entry-category with no preceding menu-entry-name\n");
            return;
        }
        if (!menuEntryValueIsRenderable("menu-entry-category", value)) return;
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
    //     rule-match-class    = Firefox      # opens rule 1
    //     rule-match-instance = navigator    # AND-ed into rule 1
    //     rule-position       = 100,100      # action on rule 1
    //     rule-size           = 800x600      # action on rule 1
    //     rule-match-type     = dialog       # action boundary crossed -> rule 2
    //     rule-no-decorate    = true         # action on rule 2
    //
    // The three text criteria are named for what they compare (D-8.5-01):
    // rule-match-class tests both WM_CLASS fields, rule-match-instance tests the
    // instance name, rule-match-title tests the window title. The old
    // `rule-match-name` spelling -- which compared the instance name while its
    // name said WM_NAME -- was removed in plan 08.5-01 with no alias, before
    // v1.0 made the mistake permanent.
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
        (key == "rule-match-class" || key == "rule-match-instance" ||
         key == "rule-match-title" || key == "rule-match-type" ||
         key == "rule-match-mode");

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

        if (key == "rule-match-instance") {
            rule.hasMatchInstance = true;
            rule.matchInstance = value;
            return;
        }

        if (key == "rule-match-title") {
            rule.hasMatchTitle = true;
            rule.matchTitle = value;
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

// =============================================================================
// The CLI surface, declared ONCE (plan 08-13)
//
// Every long option the binary accepts is one row of kOptionSpecs below, and
// BOTH the getopt_long() array and the --help output are generated from those
// same rows. Before this the table was hand-written and the usage text did not
// exist at all: an unrecognised option produced a getopt error followed by
// advice to try `--help`, and `--help` was not in the table, so following the
// advice produced the same error again. A second hand-maintained list of option
// names -- one for parsing, one for printing -- would drift back into that state
// the first time a setting was added to only one of them.
//
// The --no-<name> negation of each boolean (D-03) is DERIVED here rather than
// listed, for the same reason: the two spellings of one setting cannot fall out
// of step if only one of them is ever written down.
// =============================================================================

enum class OptType { String, Integer, Boolean, Help };

// The bounds are MACHINE-READABLE (plan 09-04), not merely described in the
// summary prose. Config::applyKeyValue(), Config::applyCliArgs() and the
// configuration socket's `set` all read them from here, so "1 to 50" is written
// down exactly once. Meaningless for a String or Boolean row, where both are 0.
struct OptionSpec {
    const char* name;
    OptType type;
    int minValue;
    int maxValue;
    const char* summary;
};

static const OptionSpec kOptionSpecs[] = {
    // String settings
    {"tab-foreground",            OptType::String,  0, 0, "colour of the tab label text"},
    {"tab-background",            OptType::String,  0, 0, "colour behind the tab label"},
    {"frame-background",          OptType::String,  0, 0, "colour of the window frame"},
    {"button-background",         OptType::String,  0, 0, "colour of the tab button"},
    {"borders",                   OptType::String,  0, 0, "colour of frame and tab borders"},
    {"menu-foreground",           OptType::String,  0, 0, "colour of root menu text"},
    {"menu-background",           OptType::String,  0, 0, "colour behind root menu text"},
    {"menu-highlight",            OptType::String,  0, 0, "colour of the selected menu row"},
    {"menu-borders",              OptType::String,  0, 0, "colour of the root menu border"},
    {"tab-font",                  OptType::String,  0, 0, "fontconfig pattern for the sideways tab label"},
    {"menu-font",                 OptType::String,  0, 0, "fontconfig pattern for the root menu"},
    {"new-window-command",        OptType::String,  0, 0, "command the menu's New entry runs"},

    // Integer settings
    {"frame-thickness",           OptType::Integer, 1, 50, "frame width in pixels (1-50)"},
    {"auto-raise-delay",          OptType::Integer, 1, 60000, "milliseconds before auto-raise (1-60000)"},
    {"pointer-stopped-delay",     OptType::Integer, 1, 60000, "milliseconds of pointer stillness (1-60000)"},
    {"destroy-window-delay",      OptType::Integer, 1, 60000, "milliseconds a tab-button press must be held to delete (1-60000)"},

    // Boolean settings. Each generates BOTH --name and --no-name.
    {"click-to-focus",            OptType::Boolean, 0, 0, "click a window to focus it instead of following the pointer"},
    {"raise-on-focus",            OptType::Boolean, 0, 0, "raise a window when it takes focus"},
    {"auto-raise",                OptType::Boolean, 0, 0, "raise the window under a stopped pointer"},
    {"exec-using-shell",          OptType::Boolean, 0, 0, "run new-window-command through /bin/sh (shell-evaluated)"},
    {"focus-stealing-prevention", OptType::Boolean, 0, 0, "refuse focus to windows that map without user interaction"},

    // Actions
    {"help",                      OptType::Help,    0, 0, "print this message and exit"},
};

// One getopt-visible flag: the spec it belongs to, whether it is that spec's
// --no- negation, and the spelling getopt_long() should match.
struct CliOption {
    const OptionSpec* spec;
    bool negated;
    std::string flag;
};

static std::vector<CliOption> buildLongOptions() {
    std::vector<CliOption> out;
    for (const OptionSpec& spec : kOptionSpecs) {
        out.push_back({&spec, false, spec.name});
        if (spec.type == OptType::Boolean) {
            out.push_back({&spec, true, std::string("no-") + spec.name});
        }
    }
    return out;
}

static std::vector<struct option> toGetoptTable(const std::vector<CliOption>& longOptions) {
    std::vector<struct option> table;
    table.reserve(longOptions.size() + 1);
    for (const CliOption& opt : longOptions) {
        const bool takesValue = (opt.spec->type == OptType::String ||
                                 opt.spec->type == OptType::Integer);
        // The flag string is owned by `longOptions`, which outlives every use of
        // this table in applyCliArgs().
        table.push_back({opt.flag.c_str(),
                         takesValue ? required_argument : no_argument,
                         nullptr, 0});
    }
    table.push_back({nullptr, 0, nullptr, 0});
    return table;
}

// Usage, generated from the very rows getopt_long() is driven by, so an option
// the binary accepts cannot be missing here and an option printed here cannot
// be unrecognised.
static void printUsage(const char* argv0, const std::vector<CliOption>& longOptions) {
    std::printf("Usage: %s [OPTION]...\n\n", argv0 ? argv0 : "wm2-born-again");
    std::printf("A minimalist X11 window manager with sideways tabs.\n");
    std::printf("Settings may also be given as key=value lines in\n");
    std::printf("  $XDG_CONFIG_HOME/wm2-born-again/config\n");
    std::printf("Command-line options take precedence over the config file.\n");

    struct Group { const char* title; OptType type; };
    static const Group groups[] = {
        {"String settings (--name=VALUE)",  OptType::String},
        {"Integer settings (--name=N)",     OptType::Integer},
        {"Boolean settings (--name enables, --no-name disables)", OptType::Boolean},
        {"Actions",                         OptType::Help},
    };

    for (const Group& group : groups) {
        bool printedHeading = false;
        for (const CliOption& opt : longOptions) {
            if (opt.spec->type != group.type) continue;
            if (opt.negated) continue;   // printed as part of its enable row

            if (!printedHeading) {
                std::printf("\n%s:\n", group.title);
                printedHeading = true;
            }

            // Every name printed here is a name getopt_long() was handed, taken
            // from the same list, rather than one re-derived from the spec. The
            // difference is not cosmetic: with the negations re-derived, removing
            // them from the parse table left the usage text still advertising
            // --no-<name> for options the binary would then reject. Found by
            // mutation M7.
            std::printf("  --%-28s %s\n", opt.flag.c_str(), opt.spec->summary);

            if (opt.spec->type == OptType::Boolean) {
                for (const CliOption& other : longOptions) {
                    if (other.spec == opt.spec && other.negated) {
                        std::printf("  --%-28s (disable the above)\n", other.flag.c_str());
                    }
                }
            }
        }
    }

    std::printf("\nWindow rules and manual menu entries are config-file only;\n");
    std::printf("they are repeated ordered key groups and do not map onto flags.\n");
}

void Config::applyCliArgs(int argc, char** argv) {
    optind = 1;  // Reset for re-parsing

    const std::vector<CliOption> longOptions = buildLongOptions();
    const std::vector<struct option> getoptTable = toGetoptTable(longOptions);

    while (true) {
        int optionIndex = 0;
        int c = getopt_long(argc, argv, "", getoptTable.data(), &optionIndex);

        if (c == -1) break;  // No more options

        if (c == '?') {
            // getopt_long already printed an error message. The advice below
            // now names a flag that actually exists -- see kOptionSpecs.
            std::fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
            std::exit(2);
        }

        // c == 0: long option matched
        const CliOption& matched = longOptions[static_cast<size_t>(optionIndex)];
        const char* name = matched.spec->name;

        if (matched.spec->type == OptType::Help) {
            printUsage(argv[0], longOptions);
            // Exits from inside Config::load(), which src/main.cpp calls before
            // the app cache is touched and before WindowManager is constructed
            // -- so --help works with no DISPLAY and no X server at all.
            std::exit(0);
        }

        if (matched.spec->type == OptType::Boolean) {
            const bool enable = !matched.negated;
            if      (std::strcmp(name, "click-to-focus") == 0)   clickToFocus = enable;
            else if (std::strcmp(name, "raise-on-focus") == 0)   raiseOnFocus = enable;
            else if (std::strcmp(name, "auto-raise") == 0)       autoRaise = enable;
            else if (std::strcmp(name, "exec-using-shell") == 0) execUsingShell = enable;
            else if (std::strcmp(name, "focus-stealing-prevention") == 0)
                focusStealingPrevention = enable;
            continue;
        }

        // String settings
        if (std::strcmp(name, "tab-foreground") == 0)           tabForeground = optarg;
        else if (std::strcmp(name, "tab-background") == 0)      tabBackground = optarg;
        else if (std::strcmp(name, "frame-background") == 0)    frameBackground = optarg;
        else if (std::strcmp(name, "button-background") == 0)   buttonBackground = optarg;
        else if (std::strcmp(name, "borders") == 0)             borders = optarg;
        else if (std::strcmp(name, "menu-foreground") == 0)     menuForeground = optarg;
        else if (std::strcmp(name, "menu-background") == 0)     menuBackground = optarg;
        else if (std::strcmp(name, "menu-highlight") == 0)      menuHighlight = optarg;
        else if (std::strcmp(name, "menu-borders") == 0)        menuBorders = optarg;
        else if (std::strcmp(name, "tab-font") == 0)            tabFont = optarg;
        else if (std::strcmp(name, "menu-font") == 0)           menuFont = optarg;
        else if (std::strcmp(name, "new-window-command") == 0)  newWindowCommand = optarg;

        // Integer settings (with clamping and error handling).
        //
        // Bounds read from the matched row rather than repeated per option --
        // `matched.spec` IS the row, so there is nothing to look up and nothing
        // that can name a different range from the one --help printed.
        else if (matched.spec->type == OptType::Integer) {
            try {
                const int parsed = clampInt(std::stoi(optarg),
                                            matched.spec->minValue,
                                            matched.spec->maxValue);
                if      (std::strcmp(name, "frame-thickness") == 0)       frameThickness = parsed;
                else if (std::strcmp(name, "auto-raise-delay") == 0)      autoRaiseDelay = parsed;
                else if (std::strcmp(name, "pointer-stopped-delay") == 0) pointerStoppedDelay = parsed;
                else if (std::strcmp(name, "destroy-window-delay") == 0)  destroyWindowDelay = parsed;
            } catch (const std::exception&) {
                std::fprintf(stderr, "wm2: warning: invalid --%s value '%s'\n", name, optarg);
            }
        }
    }
}


// =============================================================================
// The settable surface, as a view of kOptionSpecs (plan 09-04)
// =============================================================================

const std::vector<ConfigKeySpec>& configKeySpecs() {
    static const std::vector<ConfigKeySpec> specs = [] {
        std::vector<ConfigKeySpec> out;
        for (const OptionSpec& spec : kOptionSpecs) {
            // --help is an action, not a setting: it has no value to get, no
            // value to set and no line in the config file.
            if (spec.type == OptType::Help) continue;

            ConfigValueKind kind = ConfigValueKind::String;
            if (spec.type == OptType::Boolean)      kind = ConfigValueKind::Boolean;
            else if (spec.type == OptType::Integer) kind = ConfigValueKind::Integer;

            out.push_back({spec.name, kind, spec.minValue, spec.maxValue, spec.summary});
        }
        return out;
    }();
    return specs;
}


const ConfigKeySpec* configKeySpecFor(const std::string& key) {
    for (const ConfigKeySpec& spec : configKeySpecs()) {
        if (spec.name == key) return &spec;
    }
    return nullptr;
}


bool configValueForKey(const Config& config, const std::string& key,
                       std::string& out) {
    // Spelled the way the config FILE spells it, because that is what makes
    // `wm2-ctl get frame-thickness` and the line in the file the same answer to
    // the same question -- and what lets `set` compare what the parser did with
    // what the caller asked for.
    if (key == "tab-foreground")    { out = config.tabForeground;   return true; }
    if (key == "tab-background")    { out = config.tabBackground;   return true; }
    if (key == "frame-background")  { out = config.frameBackground; return true; }
    if (key == "button-background") { out = config.buttonBackground; return true; }
    if (key == "borders")           { out = config.borders;         return true; }
    if (key == "menu-foreground")   { out = config.menuForeground;  return true; }
    if (key == "menu-background")   { out = config.menuBackground;  return true; }
    if (key == "menu-highlight")    { out = config.menuHighlight;   return true; }
    if (key == "menu-borders")      { out = config.menuBorders;     return true; }
    if (key == "tab-font")          { out = config.tabFont;         return true; }
    if (key == "menu-font")         { out = config.menuFont;        return true; }
    if (key == "new-window-command"){ out = config.newWindowCommand; return true; }

    if (key == "click-to-focus")    { out = config.clickToFocus ? "true" : "false"; return true; }
    if (key == "raise-on-focus")    { out = config.raiseOnFocus ? "true" : "false"; return true; }
    if (key == "auto-raise")        { out = config.autoRaise ? "true" : "false"; return true; }
    if (key == "exec-using-shell")  { out = config.execUsingShell ? "true" : "false"; return true; }
    if (key == "focus-stealing-prevention") {
        out = config.focusStealingPrevention ? "true" : "false";
        return true;
    }

    if (key == "auto-raise-delay")      { out = std::to_string(config.autoRaiseDelay); return true; }
    if (key == "pointer-stopped-delay") { out = std::to_string(config.pointerStoppedDelay); return true; }
    if (key == "destroy-window-delay")  { out = std::to_string(config.destroyWindowDelay); return true; }
    if (key == "frame-thickness")       { out = std::to_string(config.frameThickness); return true; }

    // Not a single setting. `out` is deliberately left untouched, so a caller
    // can tell "no such key" from "set to the empty string".
    return false;
}


// -----------------------------------------------------------------------------
// The manual menu entries, as one value (plan 09-05, D-12)
//
// See the comment block in include/Config.h for the grammar and for why the
// list travels whole rather than a row at a time. Both functions below go
// through Config::applyKeyValue() in the one direction that matters -- parsing
// -- so a menu entry that arrives over the socket is built by exactly the same
// accumulator that builds one from a file, with the same D-07 category default
// and the same whitespace tokenisation of the command (which is NOT a shell
// evaluation: threat T-9-28).
// -----------------------------------------------------------------------------

std::vector<std::string> configTokeniseCommand(const std::string& command)
{
    // Whitespace only, no shell, no quotes, no field codes -- the split the
    // menu-entry accumulator has always performed, hoisted here in plan 09-07
    // so the settings window's dialog can call the same function rather than
    // grow a second one that agrees with it until it does not.
    std::vector<std::string> tokens;
    std::istringstream ss(command);
    std::string token;
    while (ss >> token) tokens.push_back(token);
    return tokens;
}


std::string configMenuEntriesValue(const Config& config) {
    std::string out;
    for (const AppEntry& entry : config.manualMenuEntries) {
        if (!out.empty()) out += ';';
        out += "menu-entry-name=" + entry.name;

        std::string command;
        for (const std::string& token : entry.execArgv) {
            if (!command.empty()) command += ' ';
            command += token;
        }
        out += ";menu-entry-command=" + command;
        out += ";menu-entry-category=" + entry.category;
    }
    return out;
}

bool parseMenuEntriesValue(const std::string& value,
                           std::vector<AppEntry>& out,
                           std::string& reasonOut) {
    // Parsed into a SCRATCH Config, so the accumulator state and the D-07
    // default live where they always live and this function owns no second
    // copy of either.
    Config scratch;
    scratch.manualMenuEntries.clear();

    std::size_t pos = 0;
    while (pos <= value.size()) {
        const std::size_t sep = value.find(';', pos);
        const std::string record = (sep == std::string::npos)
            ? value.substr(pos)
            : value.substr(pos, sep - pos);

        if (!record.empty()) {
            const std::size_t eq = record.find('=');
            if (eq == std::string::npos) {
                reasonOut = "expected key=value in '" + record + "'";
                return false;
            }
            const std::string key = record.substr(0, eq);
            const std::string val = record.substr(eq + 1);

            if (key != "menu-entry-name" && key != "menu-entry-command" &&
                key != "menu-entry-category") {
                reasonOut = "'" + key + "' is not a menu entry key";
                return false;
            }
            // The accumulator's own precondition, checked HERE rather than
            // left to it: the file parser answers a command or a category with
            // no preceding name by warning to a stderr nobody is reading and
            // carrying on, which over the socket would acknowledge a list that
            // silently lost a record.
            if (key != "menu-entry-name" && scratch.manualMenuEntries.empty()) {
                reasonOut = "'" + key + "' before any menu-entry-name";
                return false;
            }
            // THE WIRE PATH DOES NOT TRIM AND THE FILE PATH DOES (W-03).
            // applyFile() trims every value before applyKeyValue() sees it;
            // this function hands the value over verbatim. So a name of
            // " Mail" is accepted live and saved as `menu-entry-name =  Mail`,
            // and the next reload reads it back as "Mail" -- `get
            // menu-entries` then disagrees with what the window manager held a
            // moment ago. Refused here so the two grammars carry the same set
            // of names and categories.
            //
            // menu-entry-command is exempt: applyKeyValue tokenises it on
            // whitespace, so its outer spaces survive nothing on either route
            // and it round-trips exactly.
            if (key != "menu-entry-command" && !val.empty() &&
                (val.front() == ' ' || val.front() == '\t' ||
                 val.back()  == ' ' || val.back()  == '\t')) {
                reasonOut = "'" + key + "' begins or ends with a space, which "
                            "the configuration file cannot preserve";
                return false;
            }
            // THE OTHER TWO RULES OF THE SAME CLASS (X2). The trim rule above
            // was the only one this parser had, and the file imposes three.
            //
            // Config::applyFile() SKIPS a line whose value is longer than
            // kConfigFileMaxValueBytes, and configFileWrite() refuses to write
            // one; the file is one value per line, so it can hold no line break
            // at all, and the writer refuses that too. Without these, `set
            // menu-entries` acknowledged a list the file can neither preserve
            // nor reproduce -- applied live, then lost at the next reload or
            // refused at Save with a message about a rule the client never saw.
            //
            // BOTH APPLY TO ALL THREE KEYS, menu-entry-command included. The
            // command is exempt from the trim rule for a reason that does not
            // extend to these: applyKeyValue() tokenises it on whitespace, so
            // its outer spaces survive nothing on either route. A line break in
            // it is one more token separator on the wire and the END OF THE
            // LINE in the file, which are not the same entry -- and the writer,
            // in menuEntriesAreAcceptable(), already measures and scans the
            // rendered command exactly as it measures and scans the name.
            if (val.size() > kConfigFileMaxValueBytes) {
                reasonOut = "'" + key + "' is longer than " +
                            std::to_string(kConfigFileMaxValueBytes) +
                            " bytes, which the configuration file cannot preserve";
                return false;
            }
            if (val.find('\n') != std::string::npos ||
                val.find('\r') != std::string::npos) {
                reasonOut = "'" + key + "' contains a newline, which the "
                            "configuration file cannot hold";
                return false;
            }
            scratch.applyKeyValue(key, val);
        }

        if (sep == std::string::npos) break;
        pos = sep + 1;
    }

    out = scratch.manualMenuEntries;
    return true;
}
