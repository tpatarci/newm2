# Phase 5: Configuration System - Research

**Researched:** 2026-05-07
**Domain:** Runtime configuration for an X11 window manager (C++17, no external deps)
**Confidence:** HIGH

## Summary

Phase 5 replaces the remaining hardcoded values scattered through the modernized codebase (colors, timing, focus policy, frame thickness, spawn command) with a runtime configuration system. The old `Config.h` with `#define` macros was already removed during Phase 1 modernization, and its constants were inlined as string literals and numeric values throughout `Manager.cpp`, `Border.cpp`, `Buttons.cpp`, and `Events.cpp`. The config system must collect these into a single struct, read them from a key=value file using XDG paths, and allow CLI overrides via `getopt_long()`.

The implementation is self-contained: no external config parsing library is needed. The key=value format is simple enough to hand-roll (loop over lines, split on first `=`, trim whitespace). Color parsing delegates to Xlib's `XLookupColor`/`XParseColor` (already used by `allocateColour()`). CLI parsing uses POSIX `getopt_long()` which is part of glibc and requires only `#include <getopt.h>` and `_GNU_SOURCE`.

**Primary recommendation:** Create a `Config` struct with all settings and their built-in defaults. Parse config files (system then user) into it. Then apply CLI overrides. Pass the final `Config` to the `WindowManager` constructor. The struct lives in a new header `include/Config.h` (replacing the old one's purpose) with implementation in `src/Config.cpp`.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Font is NOT configurable. Phase 4 D-01 stands -- Noto Sans with fallback chain (Noto Sans -> DejaVu Sans -> any Sans) is hardcoded. The config system does not include any font-related keys. CONF-02's "fonts" mention is overridden.
- **D-02:** Long GNU-style options only (`--key=value`). No short flags (`-f`). Matches X11 WM conventions (openbox, fluxbox).
- **D-03:** Boolean settings use flag-presence=enable pattern. E.g., `--click-to-focus`, `--raise-on-focus`, `--auto-raise`. Omitting a flag falls back to config file or built-in default. `--no-xxx` supported for explicit disable (e.g., `--no-auto-raise`).
- **D-04:** Color values accept X11 named colors (e.g., `black`, `gray80`) and hex (`#RRGGBB`). Same format in config file and CLI. Uses XLookupColor/XParseColor for resolution.
- **D-05:** Config is read once at startup. No file watching, no hot reload. To apply config changes, restart the WM.
- **D-06:** Full XDG Base Directory compliance. Config path resolved via `$XDG_CONFIG_HOME/wm2-born-again/config` (default `~/.config/wm2-born-again/config`). System-wide config from `$XDG_CONFIG_DIRS/wm2-born-again/config` (default `/etc/xdg/wm2-born-again/config`) loaded first as baseline, user config overrides on top.
- **D-07:** Precedence order: CLI options > user config file > system config file > built-in defaults. Lower-precedence sources fill in gaps; higher-precedence sources override.

### Claude's Discretion
- Exact config key names (e.g., `tab-foreground` vs `tab_fg` vs `TabForeground`)
- Config file comment syntax (# or ; or both)
- Whether to use getopt_long or hand-rolled CLI parser
- Config parsing library (hand-rolled simple parser vs library)
- Error handling for malformed config (skip line, warn, abort?)
- Whether to generate a default config file on first run
- Exact CLI flag names and their mapping to config keys
- Validation rules for each setting (frame thickness range, delay ranges, color validity)

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| CONF-01 | Runtime config file at ~/.config/wm2-born-again/config (key=value format) | XDG Base Directory spec verified; config file path resolved via `std::getenv("XDG_CONFIG_HOME")` with `~/.config` fallback; key=value parsing hand-rolled (Section: Config File Parser) |
| CONF-02 | Config file supports: fonts, colors, focus policy, frame thickness, delays, menu command | Font excluded per D-01; full setting inventory documented (Section: Complete Setting Inventory); all other settings mapped to config keys |
| CONF-03 | Defaults work without config file (sensible built-in defaults) | Config struct initialized with upstream defaults from original Config.h; WM runs without any config file (Section: Built-in Defaults) |
| CONF-04 | Command-line options override config file settings | getopt_long() verified available on target platform; CLI parsing design documented (Section: CLI Parsing); precedence order enforced per D-07 |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Config file parsing | Application startup | -- | Pure data transformation; no X11 dependency |
| CLI argument parsing | Application startup | -- | getopt_long() before X11 connection |
| XDG path resolution | Application startup | -- | Environment variable reads only |
| Color validation | Application startup | X11 API | Needs X display for XLookupColor; deferred until display opened |
| Config consumption | WindowManager init | Border init | Colors allocated in initialiseScreen(), Border constructor reads color strings |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| glibc getopt_long | 2.35+ (Ubuntu 22.04) | CLI long-option parsing | Part of glibc; no external dependency; requires `_GNU_SOURCE` and `#include <getopt.h>` [VERIFIED: man page on system] |
| Xlib XLookupColor/XParseColor | X11R6+ | Color name/hex resolution | Already used in `allocateColour()`; same code path [VERIFIED: codebase grep] |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| std::getenv | C++17 stdlib | XDG environment variable reads | All XDG path resolution |
| std::filesystem (optional) | C++17 stdlib | Directory creation for config path | If generating default config file; can use mkdir() instead for simplicity |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| getopt_long | Hand-rolled CLI parser | getopt_long handles `--key=value` natively; hand-rolled is more code for no benefit |
| Hand-rolled key=value parser | libconfig / inih / toml11 | External deps rejected (project constraint: no new deps beyond X11/Xft/Xrandr/fontconfig); format is trivially simple |
| std::filesystem::create_directories | mkdir -p via system() | std::filesystem requires linking `-lstdc++fs` on older GCC; POSIX mkdir() is simpler and already available |

**Installation:**
No new packages required. All functionality uses glibc and C++17 standard library.

## Architecture Patterns

### System Architecture Diagram

```
main(argc, argv)
    |
    v
+---------------------------+
| 1. Parse CLI args         |  getopt_long() -> cliOverrides
|    (--key=value, --no-xx) |
+---------------------------+
    |
    v
+---------------------------+
| 2. Build Config struct    |  defaults -> system config -> user config -> CLI overrides
|    (precedence chain)     |
+---------------------------+
    |
    v
+---------------------------+
| 3. Validate Config        |  Range checks, color format checks
|    (warn on invalid)      |
+---------------------------+
    |
    v
+---------------------------+
| 4. Create WindowManager   |  Config passed to constructor
|    (config consumed in    |  -> initialiseScreen() allocates colors
|     initialiseScreen())   |
+---------------------------+
    |
    v
+---------------------------+
| 5. Event loop             |  Config values used for timing, focus
+---------------------------+
```

### Recommended Project Structure
```
include/
    Config.h          # Config struct definition + defaults + CLI parsing API
src/
    Config.cpp        # Config file parsing, XDG path resolution, CLI parsing
    main.cpp          # Modified: calls config parsing before WindowManager
```

### Pattern 1: Config Struct with Built-in Defaults
**What:** A simple struct holding all config values, initialized to upstream defaults. No inheritance, no virtual dispatch.
**When to use:** This is the only pattern needed -- one struct, one source of truth.
**Example:**
```cpp
// include/Config.h
#pragma once
#include <string>
#include <chrono>

struct Config {
    // Colors (tab)
    std::string tabForeground   = "black";
    std::string tabBackground   = "gray80";
    std::string frameBackground = "gray95";
    std::string buttonBackground = "gray95";
    std::string borders         = "black";

    // Colors (menu)
    std::string menuForeground  = "black";
    std::string menuBackground  = "gray80";
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

    // Parsing entry points
    static Config load(int argc, char** argv);
};
```
```cpp
// src/Config.cpp
#include "Config.h"
#include <getopt.h>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <algorithm>

// XDG path resolution
static std::string xdgConfigHome() {
    const char* home = std::getenv("XDG_CONFIG_HOME");
    if (home && home[0] == '/') return home;
    const char* userHome = std::getenv("HOME");
    if (!userHome) userHome = "/tmp";
    return std::string(userHome) + "/.config";
}

static std::vector<std::string> xdgConfigDirs() {
    const char* dirs = std::getenv("XDG_CONFIG_DIRS");
    if (dirs && dirs[0] != '\0') {
        // Parse colon-separated list
        std::vector<std::string> result;
        std::istringstream ss(dirs);
        std::string dir;
        while (std::getline(ss, dir, ':')) {
            if (!dir.empty() && dir[0] == '/') result.push_back(dir);
        }
        if (!result.empty()) return result;
    }
    return {"/etc/xdg"};
}
```

### Pattern 2: Precedence Chain Loading
**What:** Load config sources in order from lowest to highest precedence; each source only sets values it explicitly defines.
**When to use:** Always -- this is the core loading algorithm per D-07.
**Example:**
```cpp
Config Config::load(int argc, char** argv) {
    Config cfg;  // Start with built-in defaults

    // Layer 1: System config file (lowest precedence)
    for (const auto& dir : xdgConfigDirs()) {
        std::string path = dir + "/wm2-born-again/config";
        cfg.applyFile(path);  // only sets keys present in file
    }

    // Layer 2: User config file
    std::string userPath = xdgConfigHome() + "/wm2-born-again/config";
    cfg.applyFile(userPath);

    // Layer 3: CLI overrides (highest precedence)
    cfg.applyCliArgs(argc, argv);

    return cfg;
}
```

### Pattern 3: getopt_long for GNU-style CLI Options
**What:** Use `getopt_long()` with a static array of `struct option` entries.
**When to use:** CLI parsing in `main()`.
**Example:**
```cpp
static struct option longOptions[] = {
    // String/integer settings with required_argument
    {"tab-foreground",      required_argument, nullptr, 0},
    {"tab-background",      required_argument, nullptr, 0},
    {"frame-background",    required_argument, nullptr, 0},
    {"button-background",   required_argument, nullptr, 0},
    {"borders",             required_argument, nullptr, 0},
    {"menu-foreground",     required_argument, nullptr, 0},
    {"menu-background",     required_argument, nullptr, 0},
    {"menu-borders",        required_argument, nullptr, 0},
    {"frame-thickness",     required_argument, nullptr, 0},
    {"auto-raise-delay",    required_argument, nullptr, 0},
    {"pointer-stopped-delay", required_argument, nullptr, 0},
    {"destroy-window-delay", required_argument, nullptr, 0},
    {"new-window-command",  required_argument, nullptr, 0},

    // Boolean flags: presence = enable
    {"click-to-focus",      no_argument, nullptr, 0},
    {"raise-on-focus",      no_argument, nullptr, 0},
    {"auto-raise",          no_argument, nullptr, 0},
    {"exec-using-shell",    no_argument, nullptr, 0},

    // Boolean negations: --no-xxx = disable
    {"no-click-to-focus",   no_argument, nullptr, 0},
    {"no-raise-on-focus",   no_argument, nullptr, 0},
    {"no-auto-raise",       no_argument, nullptr, 0},
    {"no-exec-using-shell", no_argument, nullptr, 0},

    {nullptr, 0, nullptr, 0}
};
```

### Anti-Patterns to Avoid
- **Config singleton / global mutable state:** Do NOT make Config a global or singleton. Pass it as a const reference to WindowManager. The config is read once and frozen.
- **Config validation requiring X11 display:** Do not call XLookupColor during config file parsing (display may not be open yet). Validate color string format only (non-empty, starts with `#` or is alphanumeric). Actual X11 color allocation happens in `initialiseScreen()`.
- **Config keys that duplicate Phase 4 D-01:** Never add font config keys -- they are explicitly locked out.
- **Missing config file treated as error:** The WM MUST work without any config file. Only warn if a config file exists but is unreadable.

## Complete Setting Inventory

### Settings That Become Configurable (from hardcoded values in codebase)

| Config Key | Type | Default | Current Location | Notes |
|---|---|---|---|---|
| `tab-foreground` | color | `"black"` | Border.cpp:72,134 | Tab label text |
| `tab-background` | color | `"gray80"` | Border.cpp:73,137 | Tab fill |
| `frame-background` | color | `"gray95"` | Border.cpp:63 | Window frame |
| `button-background` | color | `"gray95"` | Border.cpp:64 | Tab button |
| `borders` | color | `"black"` | Border.cpp:65 | Border lines |
| `menu-foreground` | color | `"black"` | Manager.cpp:310 | Menu text |
| `menu-background` | color | `"gray80"` | Manager.cpp:311 | Menu fill |
| `menu-highlight` | color | `"gray60"` | Manager.cpp:312 | Menu selection |
| `menu-borders` | color | `"black"` | Manager.cpp:284 | Menu border |
| `click-to-focus` | bool | `false` | Not yet implemented | Phase 2 comment says "focus follows pointer" |
| `raise-on-focus` | bool | `false` | Not yet implemented | |
| `auto-raise` | bool | `false` | Manager.cpp:582-584 | Currently hardcoded timing active |
| `auto-raise-delay` | int (ms) | `400` | Manager.cpp:584,635 | |
| `pointer-stopped-delay` | int (ms) | `80` | Manager.cpp:630, Events.cpp:101 | |
| `destroy-window-delay` | int (ms) | `1500` | Border.cpp:924 | Button hold timer |
| `frame-thickness` | int (px) | `7` | Border.h:12 (`FRAME_WIDTH` constexpr) | Used extensively in shape math |
| `new-window-command` | string | `"xterm"` | Manager.cpp:549 | execlp command |
| `exec-using-shell` | bool | `false` | Not yet implemented | Upstream CONFIG_EXEC_USING_SHELL |

### Settings Excluded (D-01: Font NOT configurable)

| Excluded Key | Reason | Hardcoded At |
|---|---|---|
| menu-font | D-01: locked to Noto Sans | Manager.cpp:297-298 |
| tab-font | D-01: locked to Noto Sans bold | Border.cpp:37 |

### Settings Excluded (Out of scope per CONTEXT.md)

| Excluded Key | Reason | Phase |
|---|---|---|
| Window rules | Phase 8 | Phase 8 |
| EWMH atoms | Phase 6 | Phase 6 |
| Live reload | D-05: restart required | N/A |

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Color name resolution | Custom hex parser + named color lookup table | Xlib `XLookupColor()` / `XParseColor()` | X11 has ~700 named colors; handles all rgb.txt entries; already wrapped in `allocateColour()` [VERIFIED: Manager.cpp:323-335] |
| CLI long-option parsing | Custom argv scanner | `getopt_long()` (glibc) | Handles `--key=value`, `--no-key`, error reporting, `--` terminator [VERIFIED: man page on system] |
| Directory existence check | `stat()` + `mkdir()` chain | `std::filesystem::exists()` or single `mkdir()` call | Simple enough to use POSIX `mkdir()` with `errno == EEXIST` check; avoid std::filesystem dependency on GCC < 9 |

**Key insight:** The X11 color allocation functions are the single most important "don't hand-roll" item. The existing `allocateColour()` method in Manager.cpp already wraps `XAllocNamedColor()`. The config system just needs to pass the config color string through the same code path.

## Common Pitfalls

### Pitfall 1: FRAME_WIDTH is a constexpr Used in Shape Math
**What goes wrong:** `FRAME_WIDTH` (7) appears in 40+ places in Border.cpp for shape rectangle calculations. If you simply make it a runtime variable, the constexpr in Border.h cannot be used in compile-time contexts.
**Why it happens:** The shape math relies on frame thickness for bounding box and clip rectangle computation.
**How to avoid:** Remove the `constexpr` and make `FRAME_WIDTH` a plain `int` variable initialized from the Config struct. Pass it to Border (or Border reads it from WindowManager). The math all uses runtime values anyway (it's all inside function bodies, not template parameters).
**Warning signs:** Compile errors about "constexpr variable cannot have non-constant initializer" or shape calculations still using the old value.

### Pitfall 2: Config Parsing Before X11 Display is Open
**What goes wrong:** Trying to validate colors via `XAllocNamedColor()` during config file parsing, before the display connection is established.
**Why it happens:** Color validation seems like it should happen at parse time.
**How to avoid:** Parse config into string values. Validate only format (non-empty, reasonable length). Actual X11 color allocation happens in `initialiseScreen()` via existing `allocateColour()` method. If a color is invalid at allocation time, the existing `fatal()` handler catches it.
**Warning signs:** Segfault or "can't open display" error during config parsing.

### Pitfall 3: Border Static Members Initialized from Config
**What goes wrong:** Border uses static members (`m_frameBackgroundPixel`, `m_buttonBackgroundPixel`, `m_borderPixel`, `m_xftForeground`, `m_xftBackground`) that are allocated from hardcoded color strings in the Border constructor. If config changes the colors, these statics need to be updated.
**Why it happens:** The first Border instance allocates the static color resources; subsequent instances reuse them.
**How to avoid:** The config must be consumed before the first Border is created. Since `WindowManager::initialiseScreen()` runs before `scanInitialWindows()` (which creates Client/Border objects), and `initialiseScreen()` is where menu colors are allocated, the timing is correct. Pass color strings from Config through to Border, which allocates them on first construction.
**Warning signs:** Colors not changing when config is modified.

### Pitfall 4: XDG_CONFIG_DIRS is Colon-Separated, Not a Single Path
**What goes wrong:** Treating `$XDG_CONFIG_DIRS` as a single path instead of parsing it as a colon-separated list.
**Why it happens:** Most systems have it unset (defaults to `/etc/xdg`), so the multi-path case is rarely tested.
**How to avoid:** Parse `XDG_CONFIG_DIRS` with `std::getline(ss, token, ':')` and iterate over all entries. Skip relative paths (per spec). Load from each in order (first = highest precedence among system dirs).
**Warning signs:** Config not loading when `$XDG_CONFIG_DIRS=/usr/local/etc/xdg:/etc/xdg`.

### Pitfall 5: getopt_long Requires _GNU_SOURCE
**What goes wrong:** Compilation fails with `getopt_long` undeclared because `_GNU_SOURCE` is not defined.
**Why it happens:** `getopt_long()` is a GNU extension, not POSIX. It requires `_GNU_SOURCE` to be defined before including `<getopt.h>`.
**How to avoid:** Add `#define _GNU_SOURCE` at the top of `src/Config.cpp` (before any includes), or add it to CMakeLists.txt via `target_compile_definitions`.
**Warning signs:** "error: 'getopt_long' was not declared in this scope".

### Pitfall 6: spawn() Hardcodes execlp("xterm")
**What goes wrong:** `WindowManager::spawn()` in Manager.cpp:549 uses `execlp("xterm", "xterm", ...)` with a hardcoded command. When `new-window-command` config is set to something else (e.g., "alacritty" or "xterm -e bash"), the spawn function must handle multi-word commands.
**Why it happens:** The original upstream used `execlp` with a single command. `execlp` cannot handle arguments in a single string.
**How to avoid:** When `execUsingShell` is true, use `execl("/bin/sh", "sh", "-c", command, nullptr)`. When false, split the command string on whitespace and use `execvp`. The Shell approach is simpler and handles all cases.
**Warning signs:** Commands with arguments (e.g., "xterm -e vim") fail silently or launch wrong program.

### Pitfall 7: Missing Config Directory Not an Error
**What goes wrong:** Trying to open a config file that doesn't exist and treating it as a fatal error.
**Why it happens:** It's natural to check file open success and abort.
**How to avoid:** If the config file doesn't exist, silently skip it (this is the expected case -- CONF-03 says defaults work without config). Only warn if the file exists but cannot be read (permissions, corruption).
**Warning signs:** WM refuses to start on a fresh install with no config file.

## Code Examples

### Config File Format (key=value)
```ini
# wm2-born-again configuration file
# Lines starting with # are comments
# Empty lines are ignored

# Colors (X11 named colors or #RRGGBB hex)
tab-foreground = black
tab-background = gray80
frame-background = gray95
button-background = gray95
borders = black

menu-foreground = black
menu-background = gray80
menu-highlight = gray60
menu-borders = black

# Focus policy
# click-to-focus = false
# raise-on-focus = false
# auto-raise = false

# Timing (milliseconds)
auto-raise-delay = 400
pointer-stopped-delay = 80
destroy-window-delay = 1500

# Frame
frame-thickness = 7

# Commands
new-window-command = xterm
# exec-using-shell = false
```

### Config File Line Parser
```cpp
// src/Config.cpp -- applies a single config file to the Config struct
void Config::applyFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;  // File doesn't exist -- skip silently

    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        ++lineNum;

        // Strip leading/trailing whitespace
        auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;  // blank line
        if (line[start] == '#') continue;  // comment

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
        auto trim = [](std::string& s) {
            auto a = s.find_first_not_of(" \t");
            auto b = s.find_last_not_of(" \t");
            if (a == std::string::npos) s.clear();
            else s = s.substr(a, b - a + 1);
        };
        trim(key);
        trim(value);

        applyKeyValue(key, value);
    }
}
```

### XDG Path Resolution
```cpp
// Source: XDG Base Directory Specification v0.8
// https://specifications.freedesktop.org/basedir/latest/
static std::string xdgConfigHome() {
    const char* home = std::getenv("XDG_CONFIG_HOME");
    if (home && home[0] == '/') return home;  // must be absolute per spec
    const char* userHome = std::getenv("HOME");
    if (!userHome) userHome = "/tmp";  // fallback for unusual environments
    return std::string(userHome) + "/.config";
}

static std::vector<std::string> xdgConfigDirs() {
    const char* dirs = std::getenv("XDG_CONFIG_DIRS");
    std::vector<std::string> result;
    if (dirs && dirs[0] != '\0') {
        std::istringstream ss(dirs);
        std::string dir;
        while (std::getline(ss, dir, ':')) {
            if (!dir.empty() && dir[0] == '/') {  // skip relative paths per spec
                result.push_back(dir);
            }
        }
    }
    if (result.empty()) result.push_back("/etc/xdg");
    return result;
}
```

### getopt_long CLI Parsing Loop
```cpp
// Source: getopt_long(3) man page
// Requires: #define _GNU_SOURCE before #include <getopt.h>
void Config::applyCliArgs(int argc, char** argv) {
    optind = 1;  // reset for re-parsing (if needed)

    while (true) {
        int optionIndex = 0;
        int c = getopt_long(argc, argv, "", longOptions, &optionIndex);

        if (c == -1) break;  // no more options
        if (c == '?') {
            // getopt_long already printed error message
            std::fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
            std::exit(2);
        }

        // c == 0 means long option matched
        std::string name = longOptions[optionIndex].name;

        // Boolean enable (--click-to-focus)
        if (name == "click-to-focus")        clickToFocus = true;
        else if (name == "raise-on-focus")   raiseOnFocus = true;
        else if (name == "auto-raise")       autoRaise = true;
        else if (name == "exec-using-shell") execUsingShell = true;

        // Boolean disable (--no-click-to-focus)
        else if (name == "no-click-to-focus")   clickToFocus = false;
        else if (name == "no-raise-on-focus")   raiseOnFocus = false;
        else if (name == "no-auto-raise")       autoRaise = false;
        else if (name == "no-exec-using-shell") execUsingShell = false;

        // String settings
        else if (name == "tab-foreground")      tabForeground = optarg;
        else if (name == "tab-background")      tabBackground = optarg;
        else if (name == "frame-background")    frameBackground = optarg;
        else if (name == "button-background")   buttonBackground = optarg;
        else if (name == "borders")             borders = optarg;
        else if (name == "menu-foreground")     menuForeground = optarg;
        else if (name == "menu-background")     menuBackground = optarg;
        else if (name == "menu-borders")        menuBorders = optarg;
        else if (name == "new-window-command")  newWindowCommand = optarg;

        // Integer settings
        else if (name == "frame-thickness")     frameThickness = std::stoi(optarg);
        else if (name == "auto-raise-delay")    autoRaiseDelay = std::stoi(optarg);
        else if (name == "pointer-stopped-delay") pointerStoppedDelay = std::stoi(optarg);
        else if (name == "destroy-window-delay")  destroyWindowDelay = std::stoi(optarg);
    }
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Compile-time `#define` macros in Config.h | Runtime key=value config file + CLI overrides | Phase 5 (this phase) | Users can customize without recompiling |
| `execlp("xterm", ...)` hardcoded | Configurable `new-window-command` | Phase 5 | Users can set preferred terminal |
| FRAME_WIDTH as `constexpr int` | Runtime int from config | Phase 5 | Frame thickness configurable at runtime |

**Deprecated/outdated:**
- `upstream-wm2/Config.h` with `#define` macros: replaced by runtime config struct
- CONFIG_PROD_SHAPE: removed during modernization (was a workaround for SunOS 4.x)
- Font configuration macros: removed per Phase 4 D-01

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `getopt_long()` is available on all target platforms (Ubuntu 22.04+) | CLI Parsing | Low -- it's part of glibc, always available on Linux |
| A2 | `#define _GNU_SOURCE` before `#include <getopt.h>` is sufficient for getopt_long | CLI Parsing | Low -- verified via man page on this system |
| A3 | Focus policy booleans (clickToFocus, raiseOnFocus, autoRaise) are not yet consumed in the event loop code -- they will be in Phase 8 (FOCUS-02) | Setting Inventory | Medium -- if focus code exists, config integration changes |
| A4 | `destroy-window-delay` (1500ms) in Border.cpp:924 is the only consumer of CONFIG_DESTROY_WINDOW_DELAY | Setting Inventory | Low -- grep found only one usage |
| A5 | Border.h `FRAME_WIDTH` constexpr can be safely changed to a runtime int without breaking shape calculations | Pitfall 1 | Medium -- shape math is complex; need to verify all usages are in function bodies |

## Open Questions (RESOLVED)

1. **How should Border access Config values?** RESOLVED: Border accesses config via `windowManager()->config()` accessor. Plan 05-03 Task 2 implements this pattern (`windowManager()->config().tabForeground.c_str()`). For FRAME_WIDTH, it's a runtime `extern int` initialized from config in the first Border constructor.

2. **Should execUsingShell default to true or false?** RESOLVED: Default to `false` (matching upstream). Plan 05-01 Task 1 sets `bool execUsingShell = false` in the Config struct. Document that `exec-using-shell = true` enables multi-word commands.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| getopt_long (glibc) | CLI parsing | ✓ | glibc 2.39 (util-linux 2.39.3) | -- |
| C++17 std::string/std::vector | Config parsing | ✓ | GCC (CMake C++17) | -- |
| std::ifstream | File I/O | ✓ | C++17 stdlib | -- |
| XLookupColor/XParseColor | Color validation | ✓ | libX11 | -- |
| pkg-config | Build system | ✓ | -- | -- |

**Missing dependencies with no fallback:**
- None -- all dependencies are available.

**Missing dependencies with fallback:**
- None.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 v3.14.0 |
| Config file | CMakeLists.txt (FetchContent) |
| Quick run command | `cmake --build build && cd build && ctest -R test_config --output-on-failure` |
| Full suite command | `cmake --build build && cd build && ctest --output-on-failure` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| CONF-01 | Config file parsed from XDG paths | unit | `ctest -R test_config` | Wave 0 |
| CONF-02 | All configurable settings read from file | unit | `ctest -R test_config` | Wave 0 |
| CONF-03 | Defaults work without config file | unit | `ctest -R test_config` | Wave 0 |
| CONF-04 | CLI overrides config file values | unit | `ctest -R test_config` | Wave 0 |

### Sampling Rate
- **Per task commit:** `ctest -R test_config --output-on-failure`
- **Per wave merge:** `ctest --output-on-failure`
- **Phase gate:** Full suite green before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `tests/test_config.cpp` -- covers CONF-01 through CONF-04 (pure unit tests, no X11 display needed)
- [ ] Framework install: none needed -- Catch2 already in CMakeLists.txt

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | N/A -- no auth in config system |
| V3 Session Management | no | N/A |
| V4 Access Control | no | N/A |
| V5 Input Validation | yes | Manual validation of config values (ranges, color format) |
| V6 Cryptography | no | N/A |

### Known Threat Patterns for C++ Config Parsing

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Path traversal in config path | Tampering | Use fixed subdirectory `wm2-born-again/config`, do not allow `..` in app dir name |
| Integer overflow in delay values | Denial of Service | Clamp int values to reasonable ranges (e.g., delays 1-60000ms, thickness 1-50px) |
| Excessively long config values | Denial of Service | Reject lines > 4096 chars, values > 256 chars |
| Command injection in new-window-command | Elevation of Privilege | Document that this runs as the WM user; no additional sanitization needed (user config is trusted per XDG model) |

## Sources

### Primary (HIGH confidence)
- Codebase analysis: `src/Manager.cpp`, `src/Border.cpp`, `src/Buttons.cpp`, `src/Events.cpp`, `src/main.cpp`, `include/Border.h`, `include/Manager.h`, `include/x11wrap.h` -- all config value consumers identified
- XDG Base Directory Specification v0.8: https://specifications.freedesktop.org/basedir/latest/ -- XDG_CONFIG_HOME, XDG_CONFIG_DIRS semantics verified
- System man pages: getopt(3) on Ubuntu -- getopt_long API verified

### Secondary (MEDIUM confidence)
- Upstream `upstream-wm2/Config.h` -- original default values and comments used to define built-in defaults

### Tertiary (LOW confidence)
- None -- all findings verified from codebase or official documentation

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - getopt_long is part of glibc; XLookupColor already used in codebase
- Architecture: HIGH - config loading pattern is straightforward; integration points verified in codebase
- Pitfalls: HIGH - all pitfalls derived from actual codebase analysis (not hypothetical)

**Research date:** 2026-05-07
**Valid until:** 2026-06-07 (stable -- no fast-moving dependencies)
