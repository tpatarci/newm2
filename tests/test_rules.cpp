#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "Rules.h"

#include <string>
#include <vector>

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
    r.noDecorate = RuleTriState::True;  // an action, but nothing to match on

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
    broad.noDecorate = RuleTriState::True;
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
    REQUIRE(out.noDecorate == RuleTriState::True);
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
    first.noDecorate = RuleTriState::True;
    first.skipTaskbar = RuleTriState::True;

    WindowRule second = classRule("Firefox");
    second.noDecorate = RuleTriState::False;

    RuleOutcome out = applyRules({first, second}, facts("navigator", "Firefox"));

    REQUIRE(out.noDecorate == RuleTriState::False);
    REQUIRE(out.skipTaskbar == RuleTriState::True);
}
