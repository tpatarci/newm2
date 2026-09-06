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
    // ------------------------------------------------------------------
    // Palette (plan 08.5-02)
    //
    // One silver family, cool-cast, with black ink. Replaces the neutral
    // gray80/gray95 defaults the project carried from upstream.
    //
    // THE COOL CAST IS THE WHOLE TRICK, and it is deliberate: every silver
    // below has R < G < B by 2 per channel. That reads as aluminium rather
    // than concrete, costs exactly nothing to draw, and degrades gracefully --
    // a 16-bit VNC session (RGB565) quantises the cast away and leaves plain
    // neutral gray, which is merely the old look rather than a broken one.
    //
    // The frame is LIGHTER than the tab on purpose. That value order is what
    // says "lit from above", and together with the 1 px bevel highlight in
    // Border::drawBevel() it is where the metallic impression comes from. A
    // banded gradient was considered and rejected: across a ~21 px tab, two or
    // three bands are ~7 px each, which reads as stripes rather than sheen and
    // either collapses or visibly bands once VNC quantises it.
    //
    // Every value keeps black text above 8.8:1 contrast.
    // ------------------------------------------------------------------

    // Colors (tab)
    std::string tabForeground   = "#000000";
    std::string tabBackground   = "#C8CACC";
    // Colors (frame)
    std::string frameBackground = "#DCDEE0";
    std::string buttonBackground = "#DCDEE0";
    std::string borders         = "#000000";
    // Colors (menu)
    std::string menuForeground  = "#000000";
    std::string menuBackground  = "#C8CACC";
    std::string menuHighlight   = "#A8ACB0";
    std::string menuBorders     = "#000000";

    // ------------------------------------------------------------------
    // Fonts (plan 09-01, CONF-02)
    //
    // The fonts this window manager draws with lived as string literals in the
    // code that draws with them -- rung 1 of Border::loadTabFont() and the
    // menu-font load in WindowManager::initialiseScreen() -- until this plan.
    // They are here now because the Phase 9 configuration GUI edits fonts
    // (CGUI-03) and nothing it could edit existed: `grep -ci font
    // src/Config.cpp` returned 0.
    //
    // DISC-05a: the value grammar is a fontconfig pattern, handed to
    // XftFontOpenName unchanged. That is the same grammar the two literals
    // already used, and it is why there is no separate size key -- a size key
    // would be a second way of saying something the pattern already says, and
    // the two could disagree.
    //
    // DISC-05b: each default IS the literal the binary hardcoded before this
    // plan, character for character. A user with no config file sees no change
    // whatsoever, which is the entire promise this plan makes to them. Change
    // one of these strings and you have changed the shipped look of the window
    // manager, not merely a default.
    //
    // D-8.5-01: `tab-font` is a permanent spelling. Key names are free to choose
    // before v1.0 and fixed after, and there will be no deprecated aliases.
    // ------------------------------------------------------------------
    std::string tabFont = "Ubuntu,Noto Sans,DejaVu Sans,Sans:bold:size=12";

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

    // How long a tab-button press must be HELD before it deletes rather than
    // hides. 1500 here was upstream wm2's CONFIG_DESTROY_WINDOW_DELAY, carried
    // over verbatim from 1997 and never revisited until an operator sat with it
    // in a real session and reported it as "way too long" -- this is software
    // for adept users, and a delay tuned for hesitancy taxes every close.
    //
    // 400 is not the minimum the parser accepts (that is 1), and the floor is
    // real: the failure is ASYMMETRIC. A false hide costs nothing -- the window
    // is one menu row away. A false DELETE sends WM_DELETE_WINDOW and can lose
    // the user's work. An ordinary click runs 50-150 ms, so a threshold much
    // under ~250 ms would let a slightly sticky click destroy a window.
    //
    // 400 sits clear of that, is 3.75x faster than the value it replaces, and
    // matches autoRaiseDelay above -- one fewer arbitrary constant in the file.
    int destroyWindowDelay  = 400;
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
