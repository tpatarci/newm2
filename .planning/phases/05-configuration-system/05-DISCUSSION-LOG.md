# Phase 5: Configuration System - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-07
**Phase:** 5-Configuration System
**Areas discussed:** Font configurability conflict, CLI flag design, Config reload behavior, XDG & system-wide config

---

## Font Configurability Conflict

| Option | Description | Selected |
|--------|-------------|----------|
| Make fonts configurable | Config gets a 'font' key (fontconfig pattern). Overrides hardcoded Noto Sans. | |
| Keep fonts locked | Honor Phase 4 decision. Noto Sans stays hardcoded. Config handles everything else. | ✓ |

**User's choice:** Keep fonts locked (Recommended)
**Notes:** Phase 4 D-01 stands. CONF-02's "fonts" mention is overridden. Config system will NOT include font settings.

---

## CLI Flag Design

### Flag Style

| Option | Description | Selected |
|--------|-------------|----------|
| Long GNU-style only | Only --long-name=value. Self-documenting, matches X11 WM conventions. | ✓ |
| Long + short flags | Both --long=value and -s value. More convenient but doubles maintenance. | |

**User's choice:** Long GNU-style only (Recommended)

### Boolean Flag Handling

| Option | Description | Selected |
|--------|-------------|----------|
| Flag presence = enable | --click-to-focus enables, omit falls back, --no-auto-raise disables. | ✓ |
| Enum-style for booleans | --focus=click, --focus=follows, --focus=auto-raise. | |

**User's choice:** Flag presence = enable (Recommended)

### Color CLI Format

| Option | Description | Selected |
|--------|-------------|----------|
| Named + hex values | X11 color names ('black') and hex (#RRGGBB). Same as config file. | ✓ |
| Hex values only | Only #RRGGBB. More predictable but less convenient. | |

**User's choice:** Named + hex values (Recommended)

---

## Config Reload Behavior

| Option | Description | Selected |
|--------|-------------|----------|
| Startup-only | Config read once at startup. Restart to apply changes. Matches success criteria. | ✓ |
| Hot reload some settings | WM watches config file. Some settings apply immediately. More complex. | |

**User's choice:** Startup-only (Recommended)
**Notes:** Matches the literal success criteria #4: "Changing the config file and restarting the WM picks up the new settings."

---

## XDG & System-wide Config

| Option | Description | Selected |
|--------|-------------|----------|
| Fixed path only | ~/.config/wm2-born-again/config only. What REQUIREMENTS says literally. | |
| Full XDG compliance | Honor $XDG_CONFIG_HOME and $XDG_CONFIG_DIRS. System config first, user overrides. | ✓ |
| XDG_CONFIG_HOME only | Honor $XDG_CONFIG_HOME but no system-wide config. | |

**User's choice:** Full XDG compliance (Recommended)
**Notes:** System config loaded first as baseline, user config overrides on top. Precedence: CLI > user config > system config > built-in defaults.

---

## Claude's Discretion

No areas where user said "you decide" — all decisions were user-selected from options.

## Deferred Ideas

None — discussion stayed within phase scope.
