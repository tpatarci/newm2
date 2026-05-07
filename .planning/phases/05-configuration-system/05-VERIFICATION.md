---
phase: 05-configuration-system
verified: 2026-05-07T22:27:10Z
status: human_needed
score: 8/9 must-haves verified
overrides_applied: 0
gaps:
  - truth: "WM reads config from XDG-compliant path with key=value format supporting fonts, colors, focus policy, frame thickness, delays, and menu command"
    status: partial
    reason: "Fonts excluded per D-01 decision (locked to Noto Sans from Phase 4). All other categories (9 colors, 3 timing, 3 bools, 1 frame thickness, 1 command + shell bool) are configurable. CONF-02 requirement explicitly lists 'fonts' as a configurable category."
    artifacts:
      - path: "include/Config.h"
        issue: "No font-related config keys present (by design per D-01)"
    missing:
      - "Font configuration keys (e.g., tab-font, menu-font) -- excluded by D-01 decision but required by CONF-02"
deferred:
  - truth: "Focus policy booleans (clickToFocus, raiseOnFocus, autoRaise) are stored in Config but not yet wired to focus behavior"
    addressed_in: "Phase 8"
    evidence: "Phase 8 covers FOCUS-02: Configurable focus policy (click-to-focus, focus-follows-pointer, auto-raise)"
human_verification:
  - test: "Start WM on Xvfb with no config file -- verify it starts without errors"
    expected: "WM window appears with default gray95 frame, black tab foreground, xterm as spawn command"
    why_human: "Requires running X11 display server -- cannot verify WM startup behavior via grep/unit tests alone"
  - test: "Create config file with tab-foreground=red, restart WM, open a client window"
    expected: "Tab label foreground color should be red instead of default black"
    why_human: "Visual verification of X11 color allocation from config string -- XftColorAllocName succeeds at runtime only"
  - test: "Start WM with --frame-thickness=3, open a client window"
    expected: "Window frame should be noticeably thinner (3px instead of 7px default)"
    why_human: "Visual verification of FRAME_WIDTH runtime change -- shape math produces visible frame dimensions"
  - test: "Start WM with --new-window-command=alacritty, click root menu 'New'"
    expected: "alacritty terminal opens instead of xterm"
    why_human: "Requires running X11 session with alacritty installed -- spawn() behavior verified by code review but execution requires runtime environment"
---

# Phase 5: Configuration System Verification Report

**Phase Goal:** Users can configure the WM at runtime via a config file with sensible defaults, overriding settings from the command line without recompiling.
**Verified:** 2026-05-07T22:27:10Z
**Status:** human_needed
**Re-verification:** No -- initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | WM reads config from XDG-compliant path (key=value format) | VERIFIED | `src/Config.cpp:51-78` xdgConfigHome()/xdgConfigDirs() use `getenv("XDG_CONFIG_HOME")` and `getenv("XDG_CONFIG_DIRS")`; applyFile() parses key=value at lines 84-130 |
| 2 | WM works without any config file using built-in defaults | VERIFIED | `include/Config.h:6-31` all 18 members have in-class initializers; test case "Config load with no config files returns pure defaults" passes (line 320); applyFile silently returns on missing file (line 86) |
| 3 | System config from XDG_CONFIG_DIRS loaded as baseline, user config overrides | VERIFIED | `src/Config.cpp:194-208` Config::load() iterates xdgConfigDirs first, then user config; test case "Config load precedence" passes (line 254) |
| 4 | Precedence: CLI > user config > system config > defaults | VERIFIED | `src/Config.cpp:194-208` load() applies in order: defaults, system dirs, user config, CLI args; test "CLI overrides config file values" passes (line 653) |
| 5 | All 18 configurable settings have correct built-in defaults matching upstream | VERIFIED | `include/Config.h:8-31` verified against upstream values: tabForeground="black", frameBackground="gray95", autoRaiseDelay=400, frameThickness=7, newWindowCommand="xterm", etc. Test "Config defaults match upstream Config.h" passes (line 32) |
| 6 | CLI options override config file values (--key=value, boolean flags) | VERIFIED | `src/Config.cpp:215-318` applyCliArgs() uses getopt_long with 26-entry option table; 16 CLI test cases pass covering string/int/bool enable/bool disable/multiple/precedence |
| 7 | WM uses config values for colors, timing, frame thickness, spawn command at runtime | VERIFIED | `src/Manager.cpp:285,311-313` uses m_config for menu colors; `src/Manager.cpp:550-558` uses m_config for spawn; `src/Manager.cpp:592,638` uses m_config for timing; `src/Border.cpp:67-77` uses config for frame colors; `src/Border.cpp:37` sets FRAME_WIDTH from config |
| 8 | FRAME_WIDTH is runtime value, not compile-time constant | VERIFIED | `include/Border.h:12` declares `extern int FRAME_WIDTH`; `src/Border.cpp:11` defines `int FRAME_WIDTH = 7`; `src/Border.cpp:37` overwrites from config |
| 9 | Config file supports fonts, colors, focus policy, frame thickness, delays, menu command (CONF-02 full scope) | PARTIAL | Colors (9), focus policy (3 bools), frame thickness, delays (3), menu command all configurable. **Fonts excluded per D-01** -- no font config keys exist |

**Score:** 8/9 truths verified (1 partial)

### Deferred Items

| # | Item | Addressed In | Evidence |
|---|------|-------------|----------|
| 1 | Focus policy booleans (clickToFocus, raiseOnFocus, autoRaise) stored but not wired to behavior | Phase 8 | Phase 8 FOCUS-02: "Configurable focus policy (click-to-focus, focus-follows-pointer, auto-raise)" |

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `include/Config.h` | Config struct with 18 settings and built-in defaults | VERIFIED | 44 lines, struct with 18 members + 4 methods + 2 free functions |
| `src/Config.cpp` | XDG path resolution, key=value parser, CLI parsing | VERIFIED | 320 lines, all functions implemented: xdgConfigHome, xdgConfigDirs, applyFile, applyKeyValue, load, applyCliArgs |
| `tests/test_config.cpp` | 35 unit tests for CONF-01 through CONF-04 | VERIFIED | 809 lines, 35 test cases, 123 assertions, all passing |
| `CMakeLists.txt` | Config.cpp in WM binary, test_config target | VERIFIED | Line 24: src/Config.cpp in WM sources; lines 164-172: test_config executable with Catch2 |
| `src/main.cpp` | Entry point calling Config::load() | VERIFIED | 9 lines, calls Config::load(argc, argv) then WindowManager(config) |
| `include/Manager.h` | WindowManager accepts Config const ref | VERIFIED | Line 37: `WindowManager(const Config& config)`; line 40: `const Config& config() const`; line 94: `Config m_config` |
| `include/Border.h` | FRAME_WIDTH is extern int, not constexpr | VERIFIED | Line 12: `extern int FRAME_WIDTH` |
| `src/Manager.cpp` | Config values used for colors, timing, spawn | VERIFIED | 9 m_config references: 4 menu colors, 2 timing, 3 spawn-related |
| `src/Border.cpp` | Config-driven color allocation, FRAME_WIDTH from config | VERIFIED | 11 config references: 5 pixel colors, 2 Xft colors, FRAME_WIDTH init, destroyWindowDelay |
| `src/Events.cpp` | Config-driven pointerStoppedDelay | VERIFIED | Line 101: `std::chrono::milliseconds(m_config.pointerStoppedDelay)` |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| src/main.cpp | Config::load() | Direct call before WM construction | WIRED | Line 6: `Config config = Config::load(argc, argv)` |
| src/main.cpp | WindowManager | Config passed by const ref | WIRED | Line 7: `WindowManager manager(config)` |
| src/Manager.cpp | Config struct members | m_config member accessed in initialiseScreen/spawn/considerFocusChange/checkDelaysForFocus | WIRED | 9 references to m_config.* confirmed |
| src/Border.cpp | Config color strings | windowManager()->config().memberName.c_str() | WIRED | 7 config color references confirmed |
| src/Border.cpp | Config frameThickness | FRAME_WIDTH global set from config | WIRED | Line 37: `FRAME_WIDTH = windowManager()->config().frameThickness` |
| src/Events.cpp | Config timing | m_config.pointerStoppedDelay | WIRED | Line 101 confirmed |
| src/Config.cpp | $XDG_CONFIG_HOME | std::getenv | WIRED | Line 52: `getenv("XDG_CONFIG_HOME")` |
| src/Config.cpp | key=value file | std::ifstream + find('=') | WIRED | Lines 84-130: full parser implementation |
| src/Config.cpp | getopt_long | applyCliArgs() with struct option array | WIRED | Lines 215-318: 26-entry option table, getopt_long loop |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|--------------------|--------|
| src/Manager.cpp:initialiseScreen | m_config.menuForeground etc. | Config::load() -> applyFile/applyCliArgs | Config loaded from XDG files or defaults | FLOWING |
| src/Manager.cpp:spawn | m_config.newWindowCommand | Config::load() | Used in execlp/execl call | FLOWING |
| src/Manager.cpp:considerFocusChange | m_config.autoRaiseDelay | Config::load() | Used in chrono::milliseconds() | FLOWING |
| src/Border.cpp:constructor | config().frameThickness via FRAME_WIDTH | Config::load() -> global FRAME_WIDTH | Set at line 37, used in 40+ shape math lines | FLOWING |
| src/Border.cpp:allocateXftColors | config().tabForeground/tabBackground | Config::load() | Used in XftColorAllocName() | FLOWING |
| src/Events.cpp:MotionNotify handler | m_config.pointerStoppedDelay | Config::load() | Used in chrono::milliseconds() | FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Config test suite passes | `cd build && ./test_config` | "All tests passed (123 assertions in 35 test cases)" | PASS |
| Full test suite passes | `cd build && ctest --output-on-failure` | "100% tests passed, 0 tests failed out of 88" | PASS |
| Build succeeds | `cmake --build build` | All targets built successfully | PASS |
| Config struct has 18 members | `grep -c '=' include/Config.h` (member declarations) | 18 initialized members confirmed | PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| CONF-01 | 05-01, 05-03 | Runtime config file at ~/.config/wm2-born-again/config (key=value format) | SATISFIED | xdgConfigHome() + applyFile() + Config::load() fully implemented and tested |
| CONF-02 | 05-01, 05-03 | Config file supports: fonts, colors, focus policy, frame thickness, delays, menu command | PARTIAL | Colors, focus policy, frame thickness, delays, menu command all supported. **Fonts excluded per D-01** (locked to Noto Sans from Phase 4) |
| CONF-03 | 05-01, 05-03 | Defaults work without config file | SATISFIED | 18 in-class initializers in Config.h; test "Config load with no config files returns pure defaults" passes |
| CONF-04 | 05-02, 05-03 | Command-line options override config file settings | SATISFIED | applyCliArgs() with getopt_long; 16 CLI test cases pass; precedence test confirms CLI > file |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| src/Config.cpp | 248 | `optind = 1` instead of `optind = 0` for full glibc reinit | Info | Works on glibc (target platform); portability concern only |
| include/Config.h | 6-31 | All public members, no encapsulation | Info | Acceptable for plain data struct per IN-02 review finding |
| src/Border.cpp | 11,37 | FRAME_WIDTH global duplicating config.frameThickness | Info | Redundant storage per IN-03 review finding; works correctly for singleton WM |

### Code Review Findings Status

The code review (05-REVIEW.md) found 2 critical issues and 6 warnings. Assessment:

- **CR-01 (FRAME_WIDTH global state):** Not a blocker. WM is architecturally a singleton -- one WindowManager per X display. The global is set once from the first Border constructor and remains consistent. The review suggests removing the global entirely, which is a valid future improvement but not required for phase goal achievement.
- **CR-02 (getopt global state):** Not a blocker. All 35 tests pass consistently (verified with 20 consecutive runs per summary). The `optind = 1` reset works correctly on glibc (Ubuntu 22.04+ target). Using `optind = 0` would be more portable but is not needed for the target platform.

### Human Verification Required

### 1. WM Startup Without Config File

**Test:** Start WM on Xvfb with no config file present
**Expected:** WM starts cleanly with default colors (gray95 frame, black tab text, gray80 tab background), default frame thickness (7px), xterm as spawn command
**Why human:** Requires running X11 display server -- cannot verify WM startup behavior via unit tests alone

### 2. Config File Color Override

**Test:** Create config file at ~/.config/wm2-born-again/config with `tab-foreground=red`, restart WM, open a client window
**Expected:** Tab label foreground color renders in red
**Why human:** Visual verification of X11 color allocation from config string -- XftColorAllocName succeeds at runtime only

### 3. CLI Frame Thickness Override

**Test:** Start WM with `wm2-born-again --frame-thickness=3`, open a client window
**Expected:** Window frame noticeably thinner (3px vs 7px default)
**Why human:** Visual verification of FRAME_WIDTH runtime change -- shape math produces visible frame dimensions

### 4. Config-Driven Spawn Command

**Test:** Start WM with `wm2-born-again --new-window-command=alacritty`, click root menu "New"
**Expected:** alacritty terminal opens instead of xterm
**Why human:** Requires running X11 session with alacritty installed -- spawn() execution verified by code review but runtime behavior needs human confirmation

### Gaps Summary

**Font Configuration (CONF-02 partial):** The requirement CONF-02 states the config file should support "fonts, colors, focus policy, frame thickness, delays, menu command." All categories except fonts are fully implemented. Font configuration was explicitly excluded by Phase 5 decision D-01, which locks fonts to Noto Sans (established in Phase 4). This is a documented deviation between the requirement and implementation. The ROADMAP Phase 5 SC1 also mentions "fonts" as configurable. No later phase in the roadmap currently plans to add font configuration.

**All other requirements are fully satisfied:** XDG-compliant config file reading (CONF-01), sensible defaults without config (CONF-03), and CLI overrides (CONF-04) are all verified with passing tests and code-level evidence. The Config struct is fully wired into the WM: 9 color strings flow to X11 color allocation, 3 timing values flow to chrono::milliseconds calls, frame thickness sets the runtime FRAME_WIDTH global, and the spawn command is used in execlp/execl.

---

_Verified: 2026-05-07T22:27:10Z_
_Verifier: Claude (gsd-verifier)_
