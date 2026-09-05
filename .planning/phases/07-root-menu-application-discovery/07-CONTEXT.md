# Phase 7: Root Menu + Application Discovery - Context

**Gathered:** 2026-07-08
**Status:** Ready for planning

<domain>
## Phase Boundary

The root menu shows all installed GUI applications organized by category, combining XDG .desktop entries with heuristic-based binary discovery and user customizations from the config file.

Requirements: APPS-01 through APPS-05.

**In scope:**
- Parse XDG .desktop files from `$XDG_DATA_DIRS/applications/` for menu entries
- Heuristic binary scan of `/usr/bin/` for GUI apps lacking .desktop files
- Cache scan results at `~/.config/wm2-born-again/appcache.json`
- User-added/removed menu entries via the config file (Phase 5 key=value format)
- Root menu organized by category (nested submenus)
- Standard XDG filtering semantics (NoDisplay, Hidden, OnlyShowIn/NotShowIn, TryExec)

**Out of scope:**
- Live/AI runtime classification via an external LLM API call — heuristics only (see D-01)
- Window rules (Phase 8)
- Keyboard-driven menu navigation (v2)
- Config GUI editing of menu entries (Phase 9 — config file editing only here)

</domain>

<decisions>
## Implementation Decisions

### AI Scanner Approach
- **D-01:** The binary scanner uses heuristics only — no runtime LLM/network call. Classification signals: linked GUI toolkit libraries (libX11, libgtk, libQt, SDL, etc. via ldd/ELF inspection), executable bit set, not a shell script, filename/path heuristics to exclude obvious CLI tools. This keeps the WM fully offline-capable on a fresh VPS with no API key requirement.
- **D-02:** "AI-powered" in PROJECT.md/ROADMAP.md refers to the classifier being designed with AI-assisted development (showcase framing), not a runtime AI API call. **Flag for `/gsd-transition` or a PROJECT.md update**: the current PROJECT.md wording ("AI-powered scan of /bin and /sbin to auto-discover applications") and ROADMAP.md Phase 7 goal ("AI-powered binary discovery") imply live AI classification — downstream agents and future readers should not assume a runtime API dependency exists.

### Root Menu Structure
- **D-03:** Discovered/categorized apps appear as **nested submenus** off the existing root menu. Top-level menu retains today's flat entries (hidden windows list, "New xterm", Exit) plus new category entries (e.g., Internet, Graphics, System). Hovering a category opens a submenu listing its apps — matches conventional WM root-menu UX (fluxbox/openbox style).
- **D-04:** This requires extending `WindowManager::menu()` (currently a single flat popup loop in `src/Buttons.cpp:104`) with submenu/hover-to-expand support — new capability, not present in current menu code.

### Desktop Entry + Cache Semantics
- **D-05:** Standard XDG filtering: skip entries with `NoDisplay=true` or `Hidden=true`; honor `OnlyShowIn`/`NotShowIn` (treat wm2-born-again as always shown unless explicitly excluded via `NotShowIn`); skip entries whose `TryExec` binary is not found on PATH.
- **D-06:** `appcache.json` rebuilds automatically at every WM startup by comparing mtimes of `.desktop` directories and `/usr/bin` against the cached scan timestamp. If unchanged, the cache loads instantly without rescanning. No explicit "Rescan" menu item needed for Phase 7 (may be added later if startup rescan proves too slow in practice).

### Manual Entries via Config
- **D-07:** Manual entries (added via the Phase 5 config file) default into their own **"Custom" submenu category**, unless the user specifies a `category=` key to merge the entry into an existing XDG category (e.g., Internet).
- **D-08:** Name-based override: if a manual entry's name matches an auto-discovered entry, the manual entry wins — this lets users override an auto-discovered app's command/icon/category, or effectively "remove" one by pointing the override at a no-op command.

### Claude's Discretion
- Exact heuristic scoring/thresholds for GUI-vs-CLI binary classification (which libs count as "GUI", how many signals required)
- .desktop file parsing library vs hand-rolled parser (matches Phase 5's hand-rolled config parser precedent)
- appcache.json schema (fields, versioning for future-proofing)
- Exact submenu hover/expand timing and rendering (reuse Border/Xft patterns from Phase 4)
- Config key names for manual menu entries (e.g., `menu-entry-name=`, `menu-entry-command=`, `menu-entry-category=`)
- XDG category → display name mapping (freedesktop.org Main Categories vs custom labels)
- Where exactly in `src/Buttons.cpp:menu()` to hook in categorized/discovered entries vs the existing hidden-windows list

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Project Context
- `.planning/PROJECT.md` — Project definition, core value, constraints, key decisions (note D-02 flag above re: "AI-powered" wording)
- `.planning/REQUIREMENTS.md` — APPS-01 through APPS-05 requirements with traceability
- `.planning/ROADMAP.md` — Phase 7 goal, success criteria, and requirements mapping (note D-02 flag above)

### Prior Phase Decisions
- `.planning/phases/05-configuration-system/05-CONTEXT.md` — Config file format (key=value, XDG paths, precedence), CLI flag conventions — manual menu entries extend this same config file
- `.planning/phases/06-ewmh-compliance/06-CONTEXT.md` — Window type handling, atom patterns (for reference on root window property conventions)
- `.planning/phases/04-border-xft-font-rendering/04-CONTEXT.md` — Xft rendering patterns for menu/tab text (submenu rendering reuses these)

### Codebase Analysis
- `.planning/codebase/ARCHITECTURE.md` — Event flow; root menu is currently a flat popup (line 27, 96)
- `.planning/codebase/STACK.md` — Current dependencies (no XDG desktop-file or JSON parsing library yet — new dependency decision needed in planning)
- `.planning/codebase/CONCERNS.md` — Existing technical debt to be aware of when extending Buttons.cpp

### Source Files (Primary Targets)
- `src/Buttons.cpp:104` — `WindowManager::menu()` — current flat menu implementation, primary extension point for submenus
- `include/Manager.h:152` — `menu(XButtonEvent *e)` declaration
- `src/Manager.cpp:803` — `WindowManager::menuLabel(int i)` — existing label lookup, pattern for new categorized label lookup
- `include/Config.h` / config loader from Phase 5 — where manual menu entry config keys are parsed
- `src/main.cpp` — Entry point; where startup app scan/cache-load is triggered

### Upstream Reference
- `upstream-wm2/Manager.C` — Original single-level root menu implementation for comparison

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/Buttons.cpp:104-` — `menu()` already handles popup positioning, XftDraw creation/rebinding, grab/ungrab, and click-to-select loop. New categorized menu extends this rather than replacing it.
- `src/Manager.cpp:803` — `menuLabel()` pattern for resolving display labels — reusable for category and app entry labels.
- Phase 5 config parser (key=value, XDG paths) — directly reusable for both reading manual menu entries and, potentially, for appcache.json if a similar simple format is preferred over JSON (planner's call).
- `include/x11wrap.h` RAII wrappers — no new X11 resource types anticipated; submenus are additional popup windows using the same patterns as `m_menuWindow`.

### Established Patterns
- `m_` prefix for member variables — retained
- `std::vector<Client*>` / `std::vector<...>` for menu-backing lists — categorized apps follow the same vector-based approach
- XftTextExtentsUtf8 for menu label sizing — reused for app entry labels (already handles UTF-8 desktop names)

### Integration Points
- `src/Buttons.cpp:menu()` — primary integration point for submenu rendering and category dispatch
- `src/main.cpp` — where app scanning/cache-loading happens before WindowManager construction (or lazily on first menu open — planner's call given D-06 startup-rescan decision)
- Phase 5 config loader — extension point for parsing manual menu entry keys

</code_context>

<specifics>
## Specific Ideas

- Nested submenus should visually match wm2's existing minimal-chrome popup style — no new visual language, just an additional level of the same popup mechanism.
- "Custom" is the literal default category name for user-added config entries when no category is specified.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope.

</deferred>

---

*Phase: 7-Root Menu + Application Discovery*
*Context gathered: 2026-07-08*
