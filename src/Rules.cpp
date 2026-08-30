#include "Rules.h"

// =============================================================================
// Window-rule matching and folding (RULES-01, D-21, D-22)
//
// No X11 include here, deliberately -- see the header comment. The matcher is
// pure string and enum work over the three facts plan 08-10 reads off each
// window, which is what keeps tests/test_rules.cpp display-free.
// =============================================================================

// Helper: compare one criterion against one window string, in the rule's mode.
//
// D-21 fixes this at exact-or-substring. There is no pattern-matching engine of
// any kind, and adding one is an explicit phase prohibition: it would hand a
// hand-rolled parser (the project's established pattern for the config file and
// for .desktop entries) a matching language it has no business owning. The
// prohibition is enforced by a literal grep over this file, so the tokens it
// searches for are kept out of the prose here on purpose.
static bool criterionMatches(const std::string& criterion,
                             const std::string& windowText,
                             RuleMatchMode mode) {
    if (mode == RuleMatchMode::Exact) {
        return windowText == criterion;
    }
    return windowText.find(criterion) != std::string::npos;
}

bool ruleMatches(const WindowRule& rule, const RuleWindowFacts& facts) {
    // A rule with no criteria matches NOTHING, not everything. A config typo
    // that drops the only match line must not silently apply that rule's
    // actions to every window on the screen.
    //
    // EVERY CRITERION MUST BE COUNTED HERE. A criterion added to the matcher
    // below but forgotten in this guard produces a rule that sets only that
    // criterion and is then discarded as criterion-free -- which looks exactly
    // like a rule that does not match, and is the one failure in this file that
    // no positive test would catch.
    if (!rule.hasMatchClass && !rule.hasMatchInstance &&
        !rule.hasMatchTitle && !rule.hasMatchType) {
        return false;
    }

    // AND semantics (D-21): every criterion the user set must match.
    if (rule.hasMatchClass) {
        // The class criterion is the forgiving one -- it is tested against both
        // WM_CLASS fields, because a user writing "Firefox" means either the
        // instance name or the class name and should not have to know which.
        const bool hit =
            criterionMatches(rule.matchClass, facts.instanceName, rule.mode) ||
            criterionMatches(rule.matchClass, facts.className, rule.mode);
        if (!hit) return false;
    }

    if (rule.hasMatchInstance) {
        // The instance criterion is the precise one: instance name only.
        if (!criterionMatches(rule.matchInstance, facts.instanceName, rule.mode)) {
            return false;
        }
    }

    if (rule.hasMatchTitle) {
        // The title as it stood when the window was mapped (D-8.5-03).
        if (!criterionMatches(rule.matchTitle, facts.title, rule.mode)) {
            return false;
        }
    }

    if (rule.hasMatchType) {
        if (facts.type != rule.matchType) return false;
    }

    return true;
}

RuleOutcome applyRules(const std::vector<WindowRule>& rules,
                       const RuleWindowFacts& facts) {
    RuleOutcome outcome;

    // D-22: every matching rule applies, in file order, and the last one to set
    // a given action wins. Note what is NOT happening here -- a rule that does
    // not mention an action leaves that action exactly as an earlier rule left
    // it. That is the whole reason the boolean actions are tri-state.
    for (const WindowRule& rule : rules) {
        if (!ruleMatches(rule, facts)) continue;

        if (rule.noDecorate != RuleTriState::Unset) {
            outcome.noDecorate = rule.noDecorate;
        }
        if (rule.skipTaskbar != RuleTriState::Unset) {
            outcome.skipTaskbar = rule.skipTaskbar;
        }
        if (rule.hasPosition) {
            outcome.hasPosition = true;
            outcome.posX = rule.posX;
            outcome.posY = rule.posY;
        }
        if (rule.hasSize) {
            outcome.hasSize = true;
            outcome.width = rule.width;
            outcome.height = rule.height;
        }
    }

    return outcome;
}
