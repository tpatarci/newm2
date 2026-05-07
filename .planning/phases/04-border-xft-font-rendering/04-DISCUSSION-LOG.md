# Phase 4: Border + Xft Font Rendering - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-07
**Phase:** 04-border-xft-font-rendering
**Areas discussed:** Sideways text rendering, Menu font scope, Shape fallback look

---

## Sideways text rendering

### Font choice

| Option | Description | Selected |
|--------|-------------|----------|
| Noto Sans | Gold standard for Unicode coverage — Latin, Arabic, CJK, Cyrillic, etc. | ✓ |
| DejaVu Sans | Broad coverage, pre-installed on Ubuntu, less CJK | |
| Other font | User has a specific font in mind | |

**User's choice:** Noto Sans
**Notes:** User initially considered making the font environment-sensitive/configurable but decided against it — locked font, not configurable. Rationale: simplicity. Phase 5 config will NOT include font setting.

### Font fallback chain

| Option | Description | Selected |
|--------|-------------|----------|
| Require fonts-noto | Full coverage guaranteed, user must install | |
| Noto → DejaVu → any Sans | Graceful degradation — always works | ✓ |
| Generic Sans-12 | Simplest, coverage varies by system | |

**User's choice:** Noto → DejaVu → any Sans

### Rotation method

| Option | Description | Selected |
|--------|-------------|----------|
| Xft matrix transform | Pure Xft, no new deps, stays lightweight | ✓ |
| Render-to-pixmap + rotate | Uses XRender, more code, slightly blurrier | |
| Cairo | Best quality, adds dependency | |

**User's choice:** Xft matrix transform

### PoC validation

| Option | Description | Selected |
|--------|-------------|----------|
| Plan and execute | Faster start, fix issues during execution | |
| PoC first, then plan | Validate approach before full planning | ✓ |

**User's choice:** PoC first, then plan
**Notes:** STATE.md flags Xft rendering inside shaped windows as poorly documented. User chose to validate first.

### Font size

| Option | Description | Selected |
|--------|-------------|----------|
| Match current size | Match Lucida Bold 14pt visual size | ✓ |
| Optimize for Noto | Pick best size for Noto Sans specifically | |
| Specific size | User has exact size in mind | |

**User's choice:** Match current size

---

## Menu font scope

| Option | Description | Selected |
|--------|-------------|----------|
| Both tabs and menu | Fulfills VISL-02 literally, removes all core X fonts | ✓ |
| Tabs only | Smaller scope, less risk, menu later | |

**User's choice:** Both tabs and menu

---

## Shape fallback look

| Option | Description | Selected |
|--------|-------------|----------|
| Rectangular + tab strip | Full border, plain rectangle tab with label | ✓ |
| Plain border, no tab | Most minimal fallback | |
| Horizontal label fallback | Wider left edge, text reads left-to-right | |

**User's choice:** Rectangular + tab strip

---

## Claude's Discretion

- Exact XftFont creation parameters (weight, spacing)
- RAII wrapper design for Xft resources
- Tab width calculation with new font metrics
- Resize handle appearance in fallback mode
- Text truncation/ellipsis behavior
- Default fontconfig pattern syntax
- PoC structure (standalone binary vs test case)

## Deferred Ideas

None — discussion stayed within phase scope.
