# Phase 5: Configuration System - Context

**Gathered:** 2026-05-07
**Status:** Ready for planning

<domain>
## Phase Boundary

Replace compile-time `#define` configuration in `Config.h` with a runtime key=value config file, sensible built-in defaults, and long GNU-style CLI option overrides. Config is read once at startup; restart required for changes.

Requirements: CONF-01, CONF-02, CONF-03, CONF-04.

**In scope:**
- Config file at `$XDG_CONFIG_HOME/wm2-born-again/config` (defaulting to `~/.config/wm2-born-again/config`)
- System-wide config from `$XDG_CONFIG_DIRS/wm2-born-again/config` (defaulting to `/etc/xdg/wm2-born-again/config`) loaded first, user config overrides
- Key=value format with comments and sensible built-in defaults
- Configurable settings: colors (tab fg/bg, frame bg, button bg, borders, menu fg/bg/borders), focus policy (click-to-focus, raise-on-focus, auto-raise), frame thickness, timing delays (auto-raise delay, pointer-stopped delay, destroy window delay), menu new-window command, exec-using-shell
- Long GNU-style CLI options (`--key=value`) that override config file settings
- Boolean flags as presence=enable, `--no-xxx` for explicit disable
- Color values accept X11 named colors and hex (`#RRGGBB`)
- Config precedence: CLI > user config file > system config file > built-in defaults

**Out of scope:**
- Font configuration (locked to Noto Sans per Phase 4 D-01)
- Live config reload / file watching
- GUI config tool (Phase 9)
- Window rules (Phase 8)
- EWMH atoms (Phase 6)

</domain>

<decisions>
## Implementation Decisions

### Font Configurability
- **D-01:** Font is NOT configurable. Phase 4 D-01 stands — Noto Sans with fallback chain (Noto Sans → DejaVu Sans → any Sans) is hardcoded. The config system does not include any font-related keys. CONF-02's "fonts" mention is overridden.

### CLI Flag Design
- **D-02:** Long GNU-style options only (`--key=value`). No short flags (`-f`). Matches X11 WM conventions (openbox, fluxbox).
- **D-03:** Boolean settings use flag-presence=enable pattern. E.g., `--click-to-focus`, `--raise-on-focus`, `--auto-raise`. Omitting a flag falls back to config file or built-in default. `--no-xxx` supported for explicit disable (e.g., `--no-auto-raise`).
- **D-04:** Color values accept X11 named colors (e.g., `black`, `gray80`) and hex (`#RRGGBB`). Same format in config file and CLI. Uses XLookupColor/XParseColor for resolution.

### Config Reload Behavior
- **D-05:** Config is read once at startup. No file watching, no hot reload. To apply config changes, restart the WM. Matches success criteria #4 literally ("Changing the config file and restarting the WM picks up the new settings").

### XDG Compliance
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

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Project Context
- `.planning/PROJECT.md` — Project definition, core value, constraints, key decisions
- `.planning/REQUIREMENTS.md` — CONF-01 through CONF-04 requirements with traceability
- `.planning/ROADMAP.md` — Phase 5 goal, success criteria, and requirements mapping

### Prior Phase Decisions
- `.planning/phases/04-border-xft-font-rendering/04-CONTEXT.md` — Phase 4 D-01 (font locked to Noto Sans, not configurable)
- `.planning/phases/03-client-lifecycle-with-raii/03-CONTEXT.md` — Phase 3 decisions (ClientState enum, unique_ptr ownership)
- `.planning/phases/02-event-loop-modernization/02-CONTEXT.md` — Phase 2 decisions (poll()-based loop, chrono timers)
- `.planning/phases/01-build-infrastructure-raii-foundation/01-CONTEXT.md` — Phase 1 decisions (RAII wrappers in x11wrap.h, C++17 idioms)

### Codebase Analysis
- `.planning/codebase/STACK.md` — Compile-time config constants in Config.h (full list of CONFIG_* macros)
- `.planning/codebase/CONVENTIONS.md` — Naming patterns, CONFIG_ prefix convention, Xlib True/False usage

### Source Files (Primary Targets)
- `include/Config.h` — Current compile-time configuration (all #define macros to become runtime)
- `src/Manager.cpp` — WindowManager constructor reads config, initialiseScreen() uses config values, color/font allocation
- `include/Manager.h` — WindowManager class with config-consuming members (menu colors, focus flags)
- `src/Buttons.cpp` — Root menu, focus behavior, timing delays (consumers of config values)
- `src/main.cpp` — Entry point; CLI argument parsing goes here
- `CMakeLists.txt` — Build config; may need no new dependencies (config parsing is hand-rolled)

### Upstream Reference
- `upstream-wm2/Config.h` — Original compile-time configuration with comments explaining each setting

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `include/x11wrap.h` — Existing RAII wrappers. Config system doesn't need new X11 wrappers but follows the same patterns for any new resource types.
- `src/Manager.cpp:allocateColour()` — Named color allocation via XAllocNamedColor. Config system's color values flow through this existing method.
- `src/main.cpp` — Entry point (currently minimal). CLI parsing is added here.
- `include/Manager.h:152-155` — Timer deadline members (`m_autoRaiseDeadline`, `m_pointerStoppedDeadline`) — these will read from config instead of hardcoded constants.

### Established Patterns
- `m_` prefix for member variables — retained for config values stored in WindowManager
- RAII for all resources — no new X11 resources in this phase (config is pure data)
- `std::string` for string values, `bool` for booleans, `int` for numeric values — consistent with C++17 modernization
- Xlib `True`/`False` being replaced with C++ `bool` throughout (Phase 1 decision)

### Integration Points
- `include/Config.h` — All `#define` constants become runtime values. This file either becomes a thin wrapper providing defaults, or is removed entirely with defaults moved to the config loader.
- `src/Manager.cpp:initialiseScreen()` — Where config values are consumed for color allocation, timing setup, focus mode selection.
- `src/Buttons.cpp:menu()` — Where `CONFIG_NEW_WINDOW_COMMAND`, `CONFIG_EVERYTHING_ON_ROOT_MENU` are used.
- `src/Buttons.cpp` focus/move/resize — Where timing delays and focus policy are consumed.
- `src/main.cpp:main()` — Where CLI argument parsing is added before WindowManager construction.

</code_context>

<specifics>
## Specific Ideas

No specific visual or behavioral references — the upstream wm2 Config.h constants define the defaults, and the config system exposes them at runtime. The user interface is a plain text file.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope.

</deferred>

---

*Phase: 5-Configuration System*
*Context gathered: 2026-05-07*
