# Phase 6: EWMH Compliance - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-07
**Phase:** 06-ewmh-compliance
**Areas discussed:** Dock + notification handling, Fullscreen + maximize behavior, Activation policy

---

## Dock + notification handling

| Option | Description | Selected |
|--------|-------------|----------|
| Honor requested position | The dock tells wm2 where it wants to be; wm2 respects that | |
| Auto-position at screen edge | wm2 reads strut hints to position dock and adjust workarea | ✓ |

**User's choice:** "Window manager must manage. Window may want a placement, but the manager decides."
**Notes:** WM is in charge of positioning — reads dock strut hints to understand intent, then positions the dock and adjusts the workarea.

| Option | Description | Selected |
|--------|-------------|----------|
| Reserve space via struts | Read _NET_WM_STRUT/PARTIAL, adjust _NET_WORKAREA | ✓ |
| Docks float freely | Don't read struts, don't adjust workarea | |

| Option | Description | Selected |
|--------|-------------|----------|
| No frame, float above, honor position | Notification windows get no decoration, float above, position honored | ✓ |
| Normal window treatment | Notifications get wm2 tab border like everything else | |

| Option | Description | Selected |
|--------|-------------|----------|
| DOCK/DIALOG/NOTIFICATION/NORMAL/UTILITY/SPLASH/TOOLBAR as proposed | Specific handling per type as outlined | ✓ |
| Adjust the type list | User wants changes to type handling | |

---

## Fullscreen + maximize behavior

| Option | Description | Selected |
|--------|-------------|----------|
| Strip border, cover full screen | Remove wm2 border entirely, cover all screen geometry including docks | ✓ |
| Strip border, cover workarea only | Remove border but leave docks visible | |

| Option | Description | Selected |
|--------|-------------|----------|
| Keep tab, fill workarea | Maximized windows keep wm2 tab+border, expand to workarea | ✓ |
| Strip border like fullscreen, cover workarea | Same as fullscreen but only workarea | |
| Keep tab, fill full screen | Keep border, cover entire screen including docks | |

**User's note on fullscreen:** "Full screen is full screen. Window decoration etc. is gone."

| Option | Description | Selected |
|--------|-------------|----------|
| EWMH only (app-driven) | No wm2 mouse/keyboard bindings for fullscreen/maximize | |
| Also add mouse toggle | Provide mouse-driven toggle for both states | ✓ |

**User's choice for mouse gesture:** "Circular motion on the screen with right button presses toggles both full screen and exit fullscreen"

**User's choice for maximize mouse gesture:** "Click on anywhere on border and window title"

---

## Activation policy

| Option | Description | Selected |
|--------|-------------|----------|
| Always honor (Phase 8 refines) | Phase 6 grants all _NET_ACTIVE_WINDOW requests; Phase 8 adds nuance | ✓ |
| Basic timestamp check now | Phase 6 implements timestamp sanity check already | |
| You decide | Let Claude pick the approach | |

**Notes:** Simple, correct for EWMH compliance. Phase 8 adds focus stealing prevention with timestamp checks and user-interaction heuristics.

---

## Claude's Discretion

- Exact circular gesture detection algorithm (radius, angle, timing)
- How to detect border/tab click for maximize toggle
- Atom struct organization (extend Atoms vs separate EwmhAtoms)
- STRUT vs STRUT_PARTIAL preference
- State storage for fullscreen/maximize
- Pre-fullscreen/maximize geometry storage for restore
- _NET_WM_STATE add/remove/toggle semantics

## Deferred Ideas

None — discussion stayed within phase scope.
