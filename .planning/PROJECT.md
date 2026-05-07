# wm2-born-again

## What This Is

A modernized resurrection of wm2, Chris Cannam's minimalist X11 window manager from 1997, adapted for use on VPS droplets (DigitalOcean-style) accessed via VNC/RDP. Retains wm2's distinctive sideways-tab visual identity while updating the internals for modern Linux, adding runtime configuration, and introducing AI-powered application discovery for the root menu. Also serves as a showcase project for AI-assisted software development.

## Core Value

A lightweight, visually distinctive window manager that works well on resource-constrained VPS instances via remote desktop — simple enough for non-programmers to configure, reliable enough for daily use.

## Requirements

### Validated

<!-- From upstream wm2 — existing capabilities to preserve -->

- ✓ Window frame decoration with shaped borders and sideways tabs — existing (Border.C)
- ✓ Move, resize, hide, restore windows — existing (Client.C, Buttons.C)
- ✓ Root menu for launching xterm and recovering hidden windows — existing (Manager.C)
- ✓ Focus-follows-pointer and click-to-focus policies — existing (Config.h)
- ✓ WM_PROTOCOLS support (WM_DELETE_WINDOW, WM_TAKE_FOCUS) — existing (Client.C)
- ✓ Transient window handling — existing (Client.C, Manager.C)
- ✓ Colormap management — existing (Client.C, Manager.C)
- ✓ Graceful WM exit via root menu — existing (Manager.C)

### Active

- ✓ Builds and runs on modern Linux (Ubuntu 22.04+) with modern X11 libraries — Validated in Phase 1
- ✓ CMake build system with pkg-config for X11 dependency discovery — Validated in Phase 1
- ✓ Modern C++ (C++17) — std::vector, std::string, bool, RAII for X11 resources — Validated in Phase 1
- ✓ RAII client lifecycle — unique_ptr ownership, ClientState enum, ServerGrab, vector colormaps, automated tests — Validated in Phase 3
- [ ] EWMH compliance (_NET_SUPPORTED, _NET_WM_STATE, _NET_ACTIVE_WINDOW, etc.)
- [ ] Xft/fontconfig for font rendering (replacing core X fonts + xvertext)
- [ ] Xrandr support for display configuration
- [ ] Runtime configuration file (~/.config/wm2-born-again/config)
- [ ] GUI configuration tool for non-programmers (GTK or similar lightweight toolkit)
- [ ] AI-powered scan of /bin and /sbin to auto-discover applications for root menu
- [ ] Preserved classic wm2 visual identity — sideways tabs, shaped frames, minimal chrome
- [ ] VNC/XRDP compatibility (graceful fallback when X extensions unavailable)
- [ ] Command-line options (replacing compile-time-only configuration)

### Out of Scope

- Wayland support — X11 only; VPS remote desktop protocols all use X11
- Icons/icon tray — wm2's philosophy is "no icons"
- Virtual desktops/workspaces — keep it simple for VPS use
- Compositing/transparency — adds complexity, not needed for VPS
- Multi-screen support — VPS typically has single virtual display
- Theming engine — config file for colors/fonts is sufficient
- Session management (XSMP) — out of scope for v1
- Keyboard shortcuts/bindings — defer to v2

## Context

**Original source:** wm2 by Chris Cannam (1996-1997), derived from 9wm by David Hogan. MIT license. ~3,157 lines of pre-standard C++. Uses Xlib, X Shape extension, and a bundled font rotation library (xvertext 2.0).

**Target environment:** Ubuntu 22.04+ VPS instances (DigitalOcean droplets) accessed via VNC or XRDP. Resource-constrained — lightweight matters. Users range from developers to non-technical people who want a desktop on their server.

**Remote desktop compatibility:** Must work with TigerVNC, TightVNC, XRDP, and X2Go. The Shape extension is required for wm2's visual identity; must handle gracefully when unavailable. Font rendering must work with minimal server-side font packages.

**AI showcase:** This project is intentionally being developed with AI assistance (Claude Code) as a demonstration that AI-assisted programming can deliver quality results on real, non-trivial software. The AI-powered menu scanner is both a practical feature and a meta-demonstration of this principle.

**Existing codebase analysis:** Full codebase map in `.planning/codebase/` — 7 documents covering stack, architecture, structure, conventions, testing, integrations, and concerns (especially 296 lines of technical debt and issues documented in CONCERNS.md).

## Constraints

- **Tech stack**: C++17, X11/Xlib, Xft, Xrandr, fontconfig — must remain lightweight for VPS use
- **Build system**: CMake with pkg-config — standard for modern Linux packaging
- **Target OS**: Ubuntu 22.04+ (primary), should build on any modern Linux with X11
- **License**: MIT (matching upstream wm2)
- **Performance**: Must run comfortably in 512MB RAM VPS with VNC server
- **Remote desktop**: Must work over VNC/XRDP without requiring special server configuration
- **Visual identity**: Preserve wm2's classic sideways-tab look — this is non-negotiable

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Keep wm2's visual identity | Distinctive, minimal, and the whole point of wm2 — not just another tiling WM | — Pending |
| Replace xvertext with Xft | Core X fonts are deprecated; Xft supports antialiasing, UTF-8, fontconfig | — Pending |
| Add EWMH compliance | Modern apps and pagers expect _NET_* hints; without them panels/taskbars break | — Pending |
| Config file + GUI tool | Power users edit text; non-programmers use GUI; both write same format | — Pending |
| AI menu discovery | Scans installed binaries to auto-populate root menu — novel feature, fits AI showcase | — Pending |
| Xlib not XCB | XCB is lower-level but more complex; wm2's Xlib code works well and is well-understood. Can revisit later. | — Pending |
| GTK for config GUI | Lightweight, available on most VPS setups, accessible to non-technical users | — Pending |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd-complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2026-05-06 after Phase 1 completion*
