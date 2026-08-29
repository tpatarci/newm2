#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "Config.h"
#include "Rules.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

// =============================================================================
// Window-rule tests (RULES-01)
//
// Deliberately display-free: nothing in this file, in include/Rules.h or in
// src/Rules.cpp touches X11, so the whole suite runs with no DISPLAY at all and
// finishes in milliseconds. That is the point of keeping the model plain data
// -- the parser edge cases below are exactly the ones that never get covered
// when the only way to reach them is through a live server.
//
// Section 1 (this file, below): the matcher and the later-wins fold.
// Section 2: the rule-* config-file parser.
// =============================================================================

// Helper: write a temp config file and return its path (copied from
// tests/test_config.cpp so the two files read the same way)
static std::string writeTempConfig(const std::string& content) {
    static int counter = 0;
    std::string path = "/tmp/wm2-test-rules-" + std::to_string(++counter) + ".cfg";
    std::ofstream out(path);
    out << content;
    out.close();
    return path;
}

// Helper: clean up temp file
static void removeTempFile(const std::string& path) {
    std::remove(path.c_str());
}

// Helper: capture everything written to stderr for the lifetime of the object.
//
// The warning-emitting cases assert on the captured TEXT rather than merely on
// "parsing did not crash". A warning that is never emitted is precisely the
// silent-failure mode these tests exist to catch, and it is invisible to any
// assertion made only about the resulting Config.
class StderrCapture {
public:
    StderrCapture() {
        static int counter = 0;
        m_path = "/tmp/wm2-test-rules-stderr-" + std::to_string(++counter) + ".txt";
        std::fflush(stderr);
        m_saved = dup(STDERR_FILENO);
        m_fd = open(m_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (m_fd >= 0) dup2(m_fd, STDERR_FILENO);
    }

    StderrCapture(const StderrCapture&) = delete;
    StderrCapture& operator=(const StderrCapture&) = delete;

    ~StderrCapture() {
        std::fflush(stderr);
        if (m_saved >= 0) {
            dup2(m_saved, STDERR_FILENO);
            close(m_saved);
        }
        if (m_fd >= 0) close(m_fd);
        std::remove(m_path.c_str());
    }

    std::string text() const {
        std::fflush(stderr);
        std::ifstream in(m_path);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

private:
    std::string m_path;
    int m_saved = -1;
    int m_fd = -1;
};

// Helper: count non-overlapping occurrences of a substring
static int countOccurrences(const std::string& haystack, const std::string& needle) {
    int n = 0;
    for (std::string::size_type at = haystack.find(needle);
         at != std::string::npos;
         at = haystack.find(needle, at + needle.size())) {
        ++n;
    }
    return n;
}

// Helper: a rule carrying only a class criterion
static WindowRule classRule(const std::string& text,
                            RuleMatchMode mode = RuleMatchMode::Substring) {
    WindowRule r;
    r.hasMatchClass = true;
    r.matchClass = text;
    r.mode = mode;
    return r;
}

// Helper: the three facts the matcher is given about a window
static RuleWindowFacts facts(const std::string& instance, const std::string& cls,
                             RuleWindowType type = RuleWindowType::Normal) {
    RuleWindowFacts f;
    f.instanceName = instance;
    f.className = cls;
    f.type = type;
    return f;
}

// =============================================================================
// Section 1: the matcher and the fold
// =============================================================================

// -----------------------------------------------------------------------------
// Test 1: a lone class criterion matches its class and rejects everything else
// -----------------------------------------------------------------------------
TEST_CASE("A rule with only a class criterion matches that class and nothing else", "[rules]") {
    WindowRule r = classRule("Firefox");

    REQUIRE(ruleMatches(r, facts("navigator", "Firefox")));
    REQUIRE_FALSE(ruleMatches(r, facts("xterm", "XTerm")));
}

// -----------------------------------------------------------------------------
// Test 2: several criteria are AND-ed, not OR-ed (D-21)
//
// The type criterion is asserted here rather than in a case of its own: without
// it, deleting the type check from the matcher leaves the whole suite green.
// -----------------------------------------------------------------------------
TEST_CASE("A class criterion and a name criterion are AND-ed", "[rules]") {
    WindowRule r = classRule("Firefox");
    r.hasMatchName = true;
    r.matchName = "navigator";

    REQUIRE(ruleMatches(r, facts("navigator", "Firefox")));

    // Class matches, name does not -- not enough.
    REQUIRE_FALSE(ruleMatches(r, facts("download-manager", "Firefox")));

    // Name matches, class does not -- also not enough.
    REQUIRE_FALSE(ruleMatches(r, facts("navigator", "XTerm")));

    // A third criterion AND-s in the same way: with a type criterion added, a
    // window matching both text criteria but carrying a different type is
    // rejected, and one carrying the right type is accepted.
    r.hasMatchType = true;
    r.matchType = RuleWindowType::Dialog;
    REQUIRE_FALSE(ruleMatches(r, facts("navigator", "Firefox", RuleWindowType::Normal)));
    REQUIRE(ruleMatches(r, facts("navigator", "Firefox", RuleWindowType::Dialog)));

    // ... and a type criterion alone is a complete rule on its own.
    WindowRule typeOnly;
    typeOnly.hasMatchType = true;
    typeOnly.matchType = RuleWindowType::Dock;
    REQUIRE(ruleMatches(typeOnly, facts("panel", "Panel", RuleWindowType::Dock)));
    REQUIRE_FALSE(ruleMatches(typeOnly, facts("panel", "Panel", RuleWindowType::Normal)));
}

// -----------------------------------------------------------------------------
// Test 3: exact vs substring comparison (D-21 -- no glob, no regex)
// -----------------------------------------------------------------------------
TEST_CASE("Exact mode requires whole-string equality, substring mode accepts a partial match", "[rules]") {
    WindowRule exact = classRule("Firefox", RuleMatchMode::Exact);
    REQUIRE(ruleMatches(exact, facts("navigator", "Firefox")));
    REQUIRE_FALSE(ruleMatches(exact, facts("navigator", "Firefox-esr")));

    WindowRule substring = classRule("Firefox", RuleMatchMode::Substring);
    REQUIRE(ruleMatches(substring, facts("navigator", "Firefox-esr")));
}

// -----------------------------------------------------------------------------
// Test 4: substring is the default, so an unqualified rule is forgiving
// -----------------------------------------------------------------------------
TEST_CASE("Substring is the default match mode", "[rules]") {
    WindowRule r;
    REQUIRE(r.mode == RuleMatchMode::Substring);

    r.hasMatchClass = true;
    r.matchClass = "Fire";
    REQUIRE(ruleMatches(r, facts("navigator", "Firefox")));
}

// -----------------------------------------------------------------------------
// Test 5: the class criterion is dual (instance OR class); the name criterion
// is not -- it tests the instance name only. The asymmetry is deliberate and
// is negatively asserted here so it cannot drift into "both are dual".
// -----------------------------------------------------------------------------
TEST_CASE("The class criterion is tested against both the instance and the class name", "[rules]") {
    WindowRule byClassName = classRule("Firefox");
    REQUIRE(ruleMatches(byClassName, facts("navigator", "Firefox")));

    WindowRule byInstanceName = classRule("navigator");
    REQUIRE(ruleMatches(byInstanceName, facts("navigator", "Firefox")));

    WindowRule byName;
    byName.hasMatchName = true;
    byName.matchName = "Firefox";
    REQUIRE_FALSE(ruleMatches(byName, facts("navigator", "Firefox")));
}

// -----------------------------------------------------------------------------
// Test 6: a rule with no criteria matches NOTHING, not everything
// -----------------------------------------------------------------------------
TEST_CASE("A rule with no criteria matches nothing", "[rules]") {
    WindowRule r;
    r.noDecorate = RuleTriState::On;  // an action, but nothing to match on

    REQUIRE_FALSE(ruleMatches(r, facts("navigator", "Firefox")));
    REQUIRE_FALSE(ruleMatches(r, facts("", "")));

    // ... and therefore it contributes nothing to the fold either.
    RuleOutcome out = applyRules({r}, facts("navigator", "Firefox"));
    REQUIRE(out.noDecorate == RuleTriState::Unset);
}

// -----------------------------------------------------------------------------
// Test 7: the fold is per-action last-wins (D-22), and a non-matching rule
// contributes nothing at all
// -----------------------------------------------------------------------------
TEST_CASE("Folding matching rules yields the last value set for each action", "[rules]") {
    WindowRule broad = classRule("Firefox");
    broad.noDecorate = RuleTriState::On;
    broad.hasPosition = true;
    broad.posX = 100;
    broad.posY = 100;

    WindowRule specific = classRule("Firefox");
    specific.hasMatchName = true;
    specific.matchName = "navigator";
    specific.hasPosition = true;
    specific.posX = 640;
    specific.posY = 480;

    WindowRule unrelated = classRule("XTerm");
    unrelated.hasSize = true;
    unrelated.width = 1;
    unrelated.height = 1;

    RuleOutcome out = applyRules({broad, specific, unrelated},
                                 facts("navigator", "Firefox"));

    // Only the broad rule set no-decorate, so its value survives.
    REQUIRE(out.noDecorate == RuleTriState::On);
    // Both set position; the later one wins.
    REQUIRE(out.hasPosition);
    REQUIRE(out.posX == 640);
    REQUIRE(out.posY == 480);
    // The non-matching rule's size never enters the fold.
    REQUIRE_FALSE(out.hasSize);
    // An action no rule mentioned stays unset.
    REQUIRE(out.skipTaskbar == RuleTriState::Unset);
}

// -----------------------------------------------------------------------------
// Test 8: a later rule can explicitly set an action back to its negative value,
// and doing so must not disturb an action it never mentioned. This is the case
// a plain bool cannot express: with `bool noDecorate` the second rule's
// default-false skipTaskbar would silently clobber the first rule's true.
// -----------------------------------------------------------------------------
TEST_CASE("A later rule setting an action false overrides an earlier true", "[rules]") {
    WindowRule first = classRule("Firefox");
    first.noDecorate = RuleTriState::On;
    first.skipTaskbar = RuleTriState::On;

    WindowRule second = classRule("Firefox");
    second.noDecorate = RuleTriState::Off;

    RuleOutcome out = applyRules({first, second}, facts("navigator", "Firefox"));

    REQUIRE(out.noDecorate == RuleTriState::Off);
    REQUIRE(out.skipTaskbar == RuleTriState::On);
}

// =============================================================================
// Section 2: the rule-* config-file parser (D-20)
//
// The grouping rule under test, in one sentence: a new rule begins at the first
// match-or-mode line that follows an action line, or at the very first rule line
// in the file.
// =============================================================================

// -----------------------------------------------------------------------------
// Test 9: two match lines and two action lines make ONE rule carrying all four
// -----------------------------------------------------------------------------
TEST_CASE("A class line, a name line and two action lines produce one rule", "[rules]") {
    std::string path = writeTempConfig(
        "rule-match-class = Firefox\n"
        "rule-match-name  = navigator\n"
        "rule-position    = 100,200\n"
        "rule-size        = 800x600\n"
    );

    Config cfg;
    cfg.applyFile(path);

    REQUIRE(cfg.rules.size() == 1);
    const WindowRule& r = cfg.rules[0];
    REQUIRE(r.hasMatchClass);
    REQUIRE(r.matchClass == "Firefox");
    REQUIRE(r.hasMatchName);
    REQUIRE(r.matchName == "navigator");
    REQUIRE(r.hasPosition);
    REQUIRE(r.posX == 100);
    REQUIRE(r.posY == 200);
    REQUIRE(r.hasSize);
    REQUIRE(r.width == 800);
    REQUIRE(r.height == 600);

    removeTempFile(path);
}

// -----------------------------------------------------------------------------
// Test 10: a match line AFTER an action line opens the next rule
// -----------------------------------------------------------------------------
TEST_CASE("A match line following an action line opens a second rule", "[rules]") {
    std::string path = writeTempConfig(
        "rule-match-class  = Firefox\n"
        "rule-no-decorate  = true\n"
        "rule-match-type   = dialog\n"
        "rule-skip-taskbar = true\n"
    );

    Config cfg;
    cfg.applyFile(path);

    REQUIRE(cfg.rules.size() == 2);

    REQUIRE(cfg.rules[0].hasMatchClass);
    REQUIRE(cfg.rules[0].matchClass == "Firefox");
    REQUIRE(cfg.rules[0].noDecorate == RuleTriState::On);
    // The second rule's criterion must NOT have leaked backwards.
    REQUIRE_FALSE(cfg.rules[0].hasMatchType);
    REQUIRE(cfg.rules[0].skipTaskbar == RuleTriState::Unset);

    REQUIRE(cfg.rules[1].hasMatchType);
    REQUIRE(cfg.rules[1].matchType == RuleWindowType::Dialog);
    REQUIRE(cfg.rules[1].skipTaskbar == RuleTriState::On);
    // ... and the first rule's criterion must not have leaked forwards.
    REQUIRE_FALSE(cfg.rules[1].hasMatchClass);
    REQUIRE(cfg.rules[1].noDecorate == RuleTriState::Unset);

    removeTempFile(path);
}

// -----------------------------------------------------------------------------
// Test 11: consecutive match and mode lines attach to the SAME rule
// -----------------------------------------------------------------------------
TEST_CASE("Consecutive match and mode lines attach to the same rule", "[rules]") {
    std::string path = writeTempConfig(
        "rule-match-class = Firefox\n"
        "rule-match-name  = navigator\n"
        "rule-match-type  = normal\n"
        "rule-match-mode  = exact\n"
        "rule-no-decorate = true\n"
    );

    Config cfg;
    cfg.applyFile(path);

    REQUIRE(cfg.rules.size() == 1);
    const WindowRule& r = cfg.rules[0];
    REQUIRE(r.hasMatchClass);
    REQUIRE(r.hasMatchName);
    REQUIRE(r.hasMatchType);
    REQUIRE(r.matchType == RuleWindowType::Normal);
    REQUIRE(r.mode == RuleMatchMode::Exact);
    REQUIRE(r.noDecorate == RuleTriState::On);

    // All four criteria are AND-ed, and the mode really did take effect.
    REQUIRE(ruleMatches(r, facts("navigator", "Firefox", RuleWindowType::Normal)));
    REQUIRE_FALSE(ruleMatches(r, facts("navigator", "Firefox-esr", RuleWindowType::Normal)));

    removeTempFile(path);
}

// -----------------------------------------------------------------------------
// Test 12: an action with no rule open warns, produces no rule, and is still an
// action boundary -- the next match key opens a fresh rule
// -----------------------------------------------------------------------------
TEST_CASE("An action with no preceding match line warns and produces no rule", "[rules]") {
    std::string orphanOnly = writeTempConfig(
        "rule-position = 10,10\n"
    );

    std::string text;
    Config cfg;
    {
        StderrCapture capture;
        cfg.applyFile(orphanOnly);
        text = capture.text();
    }

    REQUIRE(cfg.rules.empty());
    REQUIRE(text.find("rule-position") != std::string::npos);
    REQUIRE(text.find("no preceding") != std::string::npos);
    // Exactly one warning, not one per subsequent line.
    REQUIRE(countOccurrences(text, "wm2: warning:") == 1);

    // The orphan is still a syntactic action boundary: what follows starts a
    // clean rule and does not inherit the orphaned action.
    std::string thenARule = writeTempConfig(
        "rule-position    = 10,10\n"
        "rule-match-class = Firefox\n"
        "rule-no-decorate = true\n"
    );

    Config cfg2;
    {
        StderrCapture capture;
        cfg2.applyFile(thenARule);
    }

    REQUIRE(cfg2.rules.size() == 1);
    REQUIRE(cfg2.rules[0].matchClass == "Firefox");
    REQUIRE(cfg2.rules[0].noDecorate == RuleTriState::On);
    REQUIRE_FALSE(cfg2.rules[0].hasPosition);

    removeTempFile(orphanOnly);
    removeTempFile(thenARule);
}

// -----------------------------------------------------------------------------
// Test 13: an unrecognised window type or match mode warns, names the accepted
// values, and leaves the criterion at its default rather than inventing one
// -----------------------------------------------------------------------------
TEST_CASE("An unrecognised window type warns and leaves the type criterion unset", "[rules]") {
    // "utility" is the trap: the WM collapses it into Normal, so accepting it
    // would give the user a rule that silently never fires.
    std::string path = writeTempConfig(
        "rule-match-type  = utility\n"
        "rule-no-decorate = true\n"
    );

    std::string text;
    Config cfg;
    {
        StderrCapture capture;
        cfg.applyFile(path);
        text = capture.text();
    }

    REQUIRE(text.find("rule-match-type") != std::string::npos);
    REQUIRE(text.find("utility") != std::string::npos);
    REQUIRE(text.find("notification") != std::string::npos);  // accepted values listed

    REQUIRE(cfg.rules.size() == 1);
    REQUIRE_FALSE(cfg.rules[0].hasMatchType);
    // With no criterion set at all, the rule matches nothing -- it cannot
    // silently apply its action to every window.
    REQUIRE_FALSE(ruleMatches(cfg.rules[0], facts("xterm", "XTerm")));

    // The same treatment for an unrecognised match mode.
    std::string modePath = writeTempConfig(
        "rule-match-class = Firefox\n"
        "rule-match-mode  = fuzzy\n"
    );

    std::string modeText;
    Config modeCfg;
    {
        StderrCapture capture;
        modeCfg.applyFile(modePath);
        modeText = capture.text();
    }

    REQUIRE(modeText.find("rule-match-mode") != std::string::npos);
    REQUIRE(modeText.find("substring") != std::string::npos);
    REQUIRE(modeCfg.rules.size() == 1);
    REQUIRE(modeCfg.rules[0].mode == RuleMatchMode::Substring);

    removeTempFile(path);
    removeTempFile(modePath);
}

// -----------------------------------------------------------------------------
// Test 14: a malformed position or size warns, leaves THAT action unset, and
// does not abort the rest of the file
// -----------------------------------------------------------------------------
TEST_CASE("A malformed position or size warns and leaves that action unset", "[rules]") {
    std::string path = writeTempConfig(
        "rule-match-class = Firefox\n"
        "rule-position    = notanumber\n"
        "rule-size        = 800x600\n"
    );

    std::string text;
    Config cfg;
    {
        StderrCapture capture;
        cfg.applyFile(path);
        text = capture.text();
    }

    REQUIRE(text.find("rule-position") != std::string::npos);
    REQUIRE(cfg.rules.size() == 1);
    REQUIRE_FALSE(cfg.rules[0].hasPosition);
    // Parsing continued: the following line still took effect.
    REQUIRE(cfg.rules[0].hasSize);
    REQUIRE(cfg.rules[0].width == 800);

    // A position missing its separator is malformed too.
    std::string noComma = writeTempConfig(
        "rule-match-class = Firefox\n"
        "rule-position    = 100\n"
    );
    Config noCommaCfg;
    {
        StderrCapture capture;
        noCommaCfg.applyFile(noComma);
    }
    REQUIRE(noCommaCfg.rules.size() == 1);
    REQUIRE_FALSE(noCommaCfg.rules[0].hasPosition);

    // And the same for a size that is not WxH.
    std::string badSize = writeTempConfig(
        "rule-match-class  = XTerm\n"
        "rule-size         = huge\n"
        "rule-skip-taskbar = true\n"
    );

    std::string sizeText;
    Config sizeCfg;
    {
        StderrCapture capture;
        sizeCfg.applyFile(badSize);
        sizeText = capture.text();
    }

    REQUIRE(sizeText.find("rule-size") != std::string::npos);
    REQUIRE(sizeCfg.rules.size() == 1);
    REQUIRE_FALSE(sizeCfg.rules[0].hasSize);
    REQUIRE(sizeCfg.rules[0].skipTaskbar == RuleTriState::On);

    removeTempFile(path);
    removeTempFile(noComma);
    removeTempFile(badSize);
}

// -----------------------------------------------------------------------------
// Test 15: every rule key is handled explicitly, so a rules-only config file is
// not a wall of unknown-key warnings
// -----------------------------------------------------------------------------
TEST_CASE("A config file of only rule lines produces no unknown-key warnings", "[rules]") {
    std::string path = writeTempConfig(
        "rule-match-class  = Firefox\n"
        "rule-match-name   = navigator\n"
        "rule-match-type   = normal\n"
        "rule-match-mode   = substring\n"
        "rule-no-decorate  = true\n"
        "rule-position     = 10,20\n"
        "rule-size         = 300x400\n"
        "rule-skip-taskbar = true\n"
    );

    std::string text;
    Config cfg;
    {
        StderrCapture capture;
        cfg.applyFile(path);
        text = capture.text();
    }

    REQUIRE(text.find("unknown config key") == std::string::npos);
    // All eight keys landed, and no warning of any kind was emitted.
    REQUIRE(text.find("wm2: warning:") == std::string::npos);
    REQUIRE(cfg.rules.size() == 1);
    REQUIRE(cfg.rules[0].hasMatchClass);
    REQUIRE(cfg.rules[0].hasMatchName);
    REQUIRE(cfg.rules[0].hasMatchType);
    REQUIRE(cfg.rules[0].mode == RuleMatchMode::Substring);
    REQUIRE(cfg.rules[0].noDecorate == RuleTriState::On);
    REQUIRE(cfg.rules[0].hasPosition);
    REQUIRE(cfg.rules[0].hasSize);
    REQUIRE(cfg.rules[0].skipTaskbar == RuleTriState::On);

    removeTempFile(path);
}

// -----------------------------------------------------------------------------
// Test 16: rule values pass through the SAME value-length limit as every other
// config value (threat T-8-CFG)
// -----------------------------------------------------------------------------
TEST_CASE("A rule value longer than the value-length limit is rejected", "[rules]") {
    std::string longValue(300, 'a');

    std::string path = writeTempConfig(
        "rule-match-class = " + longValue + "\n"
        "rule-match-name  = navigator\n"
    );

    Config cfg;
    {
        StderrCapture capture;
        cfg.applyFile(path);
    }

    // The over-long line never reached the rule dispatch at all...
    REQUIRE(cfg.rules.size() == 1);
    REQUIRE_FALSE(cfg.rules[0].hasMatchClass);
    // ... and the next line parsed normally.
    REQUIRE(cfg.rules[0].hasMatchName);
    REQUIRE(cfg.rules[0].matchName == "navigator");

    removeTempFile(path);
}

// -----------------------------------------------------------------------------
// Test 17: rules accumulate across the system-then-user precedence chain in
// file order, and the group state does NOT survive the file boundary
// -----------------------------------------------------------------------------
TEST_CASE("Rules append across the system-then-user chain without merging groups", "[rules]") {
    // The dangerous case: the system file ends on a MATCH line, so a rule is
    // still open when the file ends. Without a per-file reset the user file's
    // first match key would be AND-ed into that system rule -- silently
    // narrowing a rule the user cannot see and never wrote.
    std::string systemFile = writeTempConfig(
        "rule-match-class = Firefox\n"
        "rule-position    = 100,100\n"
        "rule-match-name  = navigator\n"
    );
    std::string userFile = writeTempConfig(
        "rule-match-class = XTerm\n"
        "rule-no-decorate = true\n"
    );

    Config cfg;
    cfg.applyFile(systemFile);   // layer 1, as Config::load() applies it
    cfg.applyFile(userFile);     // layer 2

    REQUIRE(cfg.rules.size() == 3);

    // System rule 1, untouched.
    REQUIRE(cfg.rules[0].matchClass == "Firefox");
    REQUIRE(cfg.rules[0].hasPosition);

    // System rule 2 closed at the file boundary: the user file's class did not
    // get AND-ed into it.
    REQUIRE(cfg.rules[1].hasMatchName);
    REQUIRE(cfg.rules[1].matchName == "navigator");
    REQUIRE_FALSE(cfg.rules[1].hasMatchClass);

    // User rule, appended after the system rules in file order.
    REQUIRE(cfg.rules[2].hasMatchClass);
    REQUIRE(cfg.rules[2].matchClass == "XTerm");
    REQUIRE(cfg.rules[2].noDecorate == RuleTriState::On);

    // The mirror case: a system file ending on an ACTION line followed by a
    // user file beginning with a match key gives two rules, not one.
    std::string sysEndsOnAction = writeTempConfig(
        "rule-match-class = A\n"
        "rule-no-decorate = true\n"
    );
    std::string userStartsOnMatch = writeTempConfig(
        "rule-match-class = B\n"
    );

    Config cfg2;
    cfg2.applyFile(sysEndsOnAction);
    cfg2.applyFile(userStartsOnMatch);

    REQUIRE(cfg2.rules.size() == 2);
    REQUIRE(cfg2.rules[0].matchClass == "A");
    REQUIRE(cfg2.rules[1].matchClass == "B");
    REQUIRE(cfg2.rules[1].noDecorate == RuleTriState::Unset);

    removeTempFile(systemFile);
    removeTempFile(userFile);
    removeTempFile(sysEndsOnAction);
    removeTempFile(userStartsOnMatch);
}
