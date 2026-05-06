# Research Summary: wm2-born-again

**Synthesized:** 2026-05-06

## Executive Summary

wm2-born-again is a modernization of the classic wm2 floating X11 window manager for VPS/VNC use. The research consistently points to a phased modernization: first establish C++17 build infrastructure with RAII wrappers, then progressively modernize the event loop, client lifecycle, border rendering (Xft), configuration system, EWMH compliance, and application discovery. The approach is conservative at the foundation (keep Xlib, preserve the sideways-tab visual identity, single-threaded event-driven model) and progressive at the feature level (EWMH, Xft, runtime config, AI-assisted app discovery). The single highest risk is Xft rendering inside X Shape extension windows — this needs a proof-of-concept before committing.

## Key Findings

**Stack:** C++17 with Xlib (not XCB), Xft/fontconfig, CMake 3.16+, key=value config format, GTK3 for separate config GUI binary. All available on Ubuntu 22.04+.

**Table Stakes:** EWMH Tier 1 atoms, Xft/UTF-8 rendering, runtime config file, _NET_WM_WINDOW_TYPE handling.

**Watch Out For:** Xft+Shape compatibility (needs PoC), reparenting race conditions (wm2 lacks XGrabServer — ICCCM violation), border geometry fragility (746 lines of pixel-precise calculations), VNC extension fallbacks.

## Phased Build Order

1. Build Infrastructure + RAII Foundation
2. Event Loop Modernization
3. Client Lifecycle with RAII
4. Border + Xft Font Rendering (highest risk)
5. Configuration System
6. EWMH Compliance
7. Root Menu + Application Scanner
8. Xrandr Support + VNC Compatibility
9. IPC + Config GUI

## Confidence

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | All mature, well-documented, available on Ubuntu 22.04+ |
| Features | HIGH | EWMH v1.5 is authoritative; cross-referenced against 3 reference WMs |
| Architecture | HIGH | Proven pattern from i3/dwm/openbox |
| Pitfalls | HIGH | Well-documented X11 patterns; concrete bugs found in original wm2 code |

## Research Flags

**Needs research:**
- Phase 4 (Border/Xft) — Xft rendering inside shaped windows is poorly documented; build a PoC first
- Phase 7 (App Scanner) — Binary scan heuristics for identifying GUI apps are novel
- Phase 9 (Config GUI) — GTK3 performance over SSH X forwarding is unvalidated

**Standard patterns (skip research):**
- Phases 1-3, 5-6 — boilerplate, well-specified, reference implementations exist
