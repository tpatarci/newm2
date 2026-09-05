#pragma once

#include <string>
#include <vector>

// Shared data model for the window-rules subsystem (Phase 8, RULES-01).
// Plain data, no X11/Xlib dependency -- mirrors include/AppEntry.h's convention
// so it stays unit-testable without Xvfb.
//
// The absence of an X11 include here is deliberate and load-bearing, not an
// accident of the current implementation: tests/test_rules.cpp links only
// src/Rules.cpp and src/Config.cpp and runs with no DISPLAY at all. Every
// parser and matcher edge case is therefore reachable in milliseconds. Adding
// an X11 include to this header or to src/Rules.cpp would drag the whole rules
// suite behind a live X server and quietly retire that coverage.
//
// Decisions implemented here:
//   D-20  Rules are repeated ordered key groups in the existing key=value
//         config file -- no new file format, no numbered keys. The parser
//         itself lives beside the manual menu entries in src/Config.cpp.
//   D-21  Every criterion a rule sets must match (AND). Comparison is exact or
//         substring only; there is deliberately no pattern-matching engine.
//   D-22  When several rules match one window they all apply in file order,
//         and the last rule to set a given action wins.
//
// The workspace action named in the original RULES-02 wording is deliberately
// absent from the action set below: the WM reports a single desktop by design,
// so the action has nothing to target. That exclusion is recorded as an
// amendment to the requirement text in plan 08-10 (D-23) rather than being
// left to live only in a planning document.

// How a criterion's text is compared against a window's text.
enum class RuleMatchMode {
    Substring,   // default -- "Firefox" matches "Firefox-esr"
    Exact        // whole-string equality
};

// The rule vocabulary for window types. Exactly the four values
// Client::getWindowType() actually distinguishes (include/Client.h): UTILITY,
// SPLASH and TOOLBAR collapse into Normal at runtime (D-04, src/Client.cpp), so
// offering those names here would hand the user a rule that silently never
// fires.
enum class RuleWindowType {
    Normal,
    Dock,
    Dialog,
    Notification
};

// A window as the matcher sees it -- the facts plan 08-10 reads off each window
// when it takes it under management. No Window handle, no Display.
//
// `title` was added by plan 08.5-01 and is the reason RULES-01 was Pending
// through the whole of Phase 8. It is the title as it stands AT MAP TIME
// (D-8.5-03): the WM folds rules once, when a window appears, and does not
// re-fold when a title later changes. Re-applying a rule's position and size on
// rename would make a window jump every time a document is saved under a new
// name or a browser tab is switched.
struct RuleWindowFacts {
    std::string instanceName;   // WM_CLASS res_name
    std::string className;      // WM_CLASS res_class
    std::string title;          // _NET_WM_NAME, or WM_NAME when that is absent
    RuleWindowType type = RuleWindowType::Normal;
};

// A tri-state boolean action.
//
// Unset is not cosmetic. The later-wins fold (D-22) cannot distinguish "this
// rule did not mention no-decorate" from "this rule turned no-decorate off" if
// the field is a plain bool -- every later rule would carry a default-false
// value and unconditionally clobber whatever an earlier rule had set.
// The enumerator names avoid True/False deliberately: Xlib #defines both as
// object-like macros, and Config.h includes this header, so any translation
// unit that includes X11 before Config.h would have the enumerators textually
// replaced by 0 and 1 and fail to compile.
enum class RuleTriState {
    Unset,
    Off,
    On
};

// One rule: the criteria that select a window, and the actions to apply to it.
struct WindowRule {
    // --- Criteria -------------------------------------------------------
    // Each carries an explicit "was set" flag, so a criterion the user never
    // wrote is distinguishable from one they wrote as an empty string.

    // The three text criteria, and the config keys they are spelled with.
    //
    // THE KEY NAMES WERE CORRECTED IN PLAN 08.5-01 (D-8.5-01), before v1.0
    // shipped and while it was still free to do so. `rule-match-name` matched
    // the WM_CLASS *instance name* while its name said WM_NAME, and that
    // mismatch had already put a false promise into docs/RELEASE-NOTES.md --
    // documented as matching the window title, which it never did. Renaming is
    // free today and breaking the day after release, so it was taken now. There
    // is deliberately NO alias for the old spelling: it falls through to the
    // parser's existing unknown-key warning, which is the visible failure this
    // rename is willing to pay for.

    // `rule-match-class` -- tested against BOTH WM_CLASS fields, because users
    // say "Firefox" meaning either. The forgiving one.
    bool hasMatchClass = false;
    std::string matchClass;

    // `rule-match-instance` -- tested against the instance name only. The
    // asymmetry with matchClass is deliberate: this is the precise one.
    bool hasMatchInstance = false;
    std::string matchInstance;

    // `rule-match-title` -- tested against the window title (plan 08.5-01).
    // Map-time only; see RuleWindowFacts above.
    bool hasMatchTitle = false;
    std::string matchTitle;

    bool hasMatchType = false;
    RuleWindowType matchType = RuleWindowType::Normal;

    // Per-rule comparison mode for the two text criteria.
    RuleMatchMode mode = RuleMatchMode::Substring;

    // --- Actions --------------------------------------------------------
    // Three shipping actions, applied to real windows by plan 08-10. None of
    // them runs a command, a shell string or any external program, and none
    // ever will: that is an explicit phase prohibition (threat T-8-SHELL), and
    // the existing shell-launch setting stays confined to manual menu entries.
    //
    // The two source-level guards on this file are literal greps for the
    // process-spawning and pattern-engine tokens, so those tokens are kept out
    // of the prose here on purpose -- a comment that trips the guard it
    // describes defeats the guard.
    RuleTriState noDecorate  = RuleTriState::Unset;
    RuleTriState skipTaskbar = RuleTriState::Unset;

    bool hasPosition = false;
    int posX = 0;
    int posY = 0;

    bool hasSize = false;
    int width = 0;
    int height = 0;
};

// The resolved result of folding every matching rule in file order: for each
// action, the value from the last matching rule that set it. Actions no
// matching rule set stay unset, which is what lets a user layer one broad rule
// plus a narrow override without the override erasing the broad one.
struct RuleOutcome {
    RuleTriState noDecorate  = RuleTriState::Unset;
    RuleTriState skipTaskbar = RuleTriState::Unset;

    bool hasPosition = false;
    int posX = 0;
    int posY = 0;

    bool hasSize = false;
    int width = 0;
    int height = 0;
};

// True when every criterion the rule sets matches the window (D-21). A rule
// with no criteria at all matches nothing.
bool ruleMatches(const WindowRule& rule, const RuleWindowFacts& facts);

// Fold an ordered rule list against one window, later-wins per action (D-22).
RuleOutcome applyRules(const std::vector<WindowRule>& rules,
                       const RuleWindowFacts& facts);
