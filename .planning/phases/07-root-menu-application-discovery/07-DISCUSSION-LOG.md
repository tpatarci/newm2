# Phase 7: Root Menu + Application Discovery - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-07-08
**Phase:** 7-Root Menu + Application Discovery
**Areas discussed:** AI scanner mechanics, Root menu structure, Desktop entry + cache semantics, Manual entries via config

---

## AI Scanner Mechanics

| Option | Description | Selected |
|--------|-------------|----------|
| Heuristics only | Static/dynamic ELF/ldd inspection for GUI toolkit linkage, no network call, works offline | ✓ |
| Real LLM classification | Call Claude/Gemini API at scan time to classify binaries | |
| Hybrid: heuristics + LLM fallback | Heuristics for obvious cases, LLM only for ambiguous ones | |

**User's choice:** Heuristics only (Recommended)
**Notes:** Avoids runtime API key/network dependency on resource-constrained VPS. "AI-powered" framing in PROJECT.md/ROADMAP.md now refers to AI-assisted design of the classifier, not a live AI call — flagged in CONTEXT.md for a future PROJECT.md wording update.

---

## Root Menu Structure

| Option | Description | Selected |
|--------|-------------|----------|
| Nested submenus | Category names at top level, hover to expand into app list | ✓ |
| Flat list with headers | Single popup with non-clickable category dividers | |
| Separate "Applications" entry | One new top-level entry opens a second-level categorized menu | |

**User's choice:** Nested submenus (Recommended)
**Notes:** Matches conventional WM root-menu UX (fluxbox/openbox style); requires extending `WindowManager::menu()` with hover-to-expand support, which doesn't exist today.

---

## Desktop Entry + Cache Semantics

| Option | Description | Selected |
|--------|-------------|----------|
| Standard XDG filtering + startup rescan | Honor NoDisplay/Hidden/OnlyShowIn/TryExec; mtime-based cache rebuild every startup | ✓ |
| Standard filtering + explicit rescan only | Same filtering, cache only rebuilt via a "Rescan Applications" menu item | |
| Minimal filtering + startup rescan | Only NoDisplay/Hidden honored, skip OnlyShowIn/TryExec | |

**User's choice:** Standard XDG filtering + startup rescan (Recommended)
**Notes:** Full XDG compliance for entry filtering; cache stays fresh automatically without requiring user action.

---

## Manual Entries via Config

| Option | Description | Selected |
|--------|-------------|----------|
| Custom category + name-based override | Manual entries default to "Custom" submenu, optional category= key to merge; name match overrides auto-discovered entry | ✓ |
| Custom category only, no override | Always separate "Custom" category, no override/removal of auto-discovered entries | |
| Pinned at top, no override | Manual entries as ungrouped block above categories, no override mechanism | |

**User's choice:** Custom category + name-based override (Recommended)
**Notes:** Lets users override or effectively remove an unwanted auto-discovered entry by pointing an override at a no-op command.

---

## Claude's Discretion

- Heuristic scoring/thresholds for GUI-vs-CLI classification
- .desktop parsing approach (library vs hand-rolled, matching Phase 5 precedent)
- appcache.json schema and versioning
- Submenu hover/expand timing and rendering details
- Config key names for manual menu entries
- XDG category → display name mapping
- Exact hook point in `src/Buttons.cpp:menu()` for categorized/discovered entries

## Deferred Ideas

None — discussion stayed within phase scope.
