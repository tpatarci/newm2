# Phase 7: Root Menu + Application Discovery - Research

**Researched:** 2026-07-08
**Domain:** XDG desktop-entry parsing, ELF static binary inspection, JSON/serialization for a lightweight cache, nested X11 popup menus
**Confidence:** MEDIUM-HIGH (spec mechanics are HIGH confidence official-spec facts; heuristic scoring thresholds and exact cache schema are project-specific design choices, MEDIUM)

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

- **D-01:** The binary scanner uses heuristics only — no runtime LLM/network call. Classification signals: linked GUI toolkit libraries (libX11, libgtk, libQt, SDL, etc. via ldd/ELF inspection), executable bit set, not a shell script, filename/path heuristics to exclude obvious CLI tools. This keeps the WM fully offline-capable on a fresh VPS with no API key requirement.
- **D-02:** "AI-powered" in PROJECT.md/ROADMAP.md refers to the classifier being designed with AI-assisted development (showcase framing), not a runtime AI API call. **Flag for `/gsd-transition` or a PROJECT.md update**: the current PROJECT.md wording ("AI-powered scan of /bin and /sbin to auto-discover applications") and ROADMAP.md Phase 7 goal ("AI-powered binary discovery") imply live AI classification — downstream agents and future readers should not assume a runtime API dependency exists.
- **D-03:** Discovered/categorized apps appear as **nested submenus** off the existing root menu. Top-level menu retains today's flat entries (hidden windows list, "New xterm", Exit) plus new category entries (e.g., Internet, Graphics, System). Hovering a category opens a submenu listing its apps — matches conventional WM root-menu UX (fluxbox/openbox style).
- **D-04:** This requires extending `WindowManager::menu()` (currently a single flat popup loop in `src/Buttons.cpp:104`) with submenu/hover-to-expand support — new capability, not present in current menu code.
- **D-05:** Standard XDG filtering: skip entries with `NoDisplay=true` or `Hidden=true`; honor `OnlyShowIn`/`NotShowIn` (treat wm2-born-again as always shown unless explicitly excluded via `NotShowIn`); skip entries whose `TryExec` binary is not found on PATH.
- **D-06:** `appcache.json` rebuilds automatically at every WM startup by comparing mtimes of `.desktop` directories and `/usr/bin` against the cached scan timestamp. If unchanged, the cache loads instantly without rescanning. No explicit "Rescan" menu item needed for Phase 7 (may be added later if startup rescan proves too slow in practice).
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

### Deferred Ideas (OUT OF SCOPE)

None — discussion stayed within phase scope.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| APPS-01 | Parse XDG .desktop files from $XDG_DATA_DIRS/applications/ for menu entries | Desktop Entry Spec fields/parsing rules below; hand-rolled parser recommended (Don't Hand-Roll table explains why a library is *not* worth it here) |
| APPS-02 | AI-powered (heuristic, per D-01/D-02) binary scan of /usr/bin/ for apps lacking .desktop files | ELF DT_NEEDED static-parse approach below; measured performance data for /usr/bin scan cost |
| APPS-03 | Cached results at ~/.config/wm2-born-again/appcache.json | Cache schema + invalidation mechanics below; JSON library tradeoff analysis |
| APPS-04 | User can manually add/remove entries via config file | Extends Phase 5 `Config::applyKeyValue` pattern; name-based override semantics (D-08) |
| APPS-05 | Root menu shows discovered apps organized by category | Nested-submenu extension of `WindowManager::menu()`; fluxbox/openbox prior art cited |
</phase_requirements>

## Summary

Phase 7 adds three independent subsystems that all feed one presentation layer (the root menu): an XDG `.desktop` file parser, a heuristic ELF-based GUI/CLI binary classifier for `/usr/bin`, and a JSON (or hand-rolled) on-disk cache — plus a config-file extension for manual entries. All four converge into a `WindowManager::menu()` extension that must gain submenu/hover-to-expand behavior it does not currently have.

The codebase's own precedent is decisive here: Phase 5 hand-rolled its config parser instead of pulling in a library, explicitly to avoid new dependencies for a "must remain lightweight" VPS constraint. The same reasoning applies to `.desktop` parsing (simple enough to hand-roll — it's the same key=value-with-sections shape already handled) and to the cache format (small data volume, no need for a full JSON DOM library). The one genuinely novel piece of engineering is the ELF binary classifier: reading `.dynamic` section `DT_NEEDED` entries directly (via mmap + raw struct casts, no `libelf` dependency — `libelf-dev` is not even installed on this dev machine) is measurably faster than shelling out to `readelf`/`ldd` per file — a full `/usr/bin` scan (2402 entries on this dev box) spawning subprocesses took **11.4 seconds real time**; an in-process ELF parse of the same set will be at least an order of magnitude faster since it avoids ~2400 process forks.

**Primary recommendation:** Hand-roll both the `.desktop` parser and the JSON-like cache serializer (a small custom writer/reader is sufficient and adds zero new dependencies); write a from-scratch minimal ELF `.dynamic`-section reader (no `libelf`) for the GUI/CLI heuristic; extend `WindowManager::menu()` with a second popup window (or an expanding rectangle within the same window) for one level of submenu nesting, modeled structurally (not code-copied) on fluxbox's recursive `[submenu]...[end]` nesting.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| .desktop file parsing (APPS-01) | Application/Backend logic (new `DesktopEntry`/`AppScanner` module) | — | Pure data parsing, no X11 dependency; testable in isolation like `Config.cpp` |
| Binary ELF classification (APPS-02) | Application/Backend logic (new `AppScanner` module) | Filesystem/OS | Reads raw file bytes; no X11 dependency; isolated, testable unit |
| appcache.json read/write (APPS-03) | Application/Backend logic (new `AppCache` module) | Filesystem/OS | Persistence layer; independent of X11 and of the menu rendering code |
| Manual config entries (APPS-04) | Application/Backend logic (`Config` extension) | — | Directly extends the existing `Config::applyKeyValue` key=value parser (Phase 5) |
| Categorized nested root menu (APPS-05) | WindowManager/X11 presentation (`Buttons.cpp:menu()`) | Border/Xft rendering | Only this layer touches X11 windows/XftDraw; must consume the merged app list produced by the other tiers |
| Merge/override logic (D-07/D-08: name-based override, category default) | Application/Backend logic (`AppScanner`/`AppCache` merge step) | — | Pure data merge (auto-discovered + manual entries), belongs before the presentation tier, not inside `menu()` |

**Why this matters here:** the biggest risk for this phase is conflating "scan/parse/cache" (pure backend logic, easily unit-testable on Xvfb-less Catch2 tests, like `test_config.cpp`) with "render/interact" (X11-only, requires Xvfb). Keep AppScanner/DesktopEntry/AppCache as free-standing classes with no `Display*` dependency; only `WindowManager::menu()` should touch X11. This mirrors the existing `Config` vs `WindowManager` separation already established in Phase 5.

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Hand-rolled `.desktop` parser | N/A (project code) | Parse `[Desktop Entry]` sections, extract Name/Exec/Icon/Categories/NoDisplay/Hidden/OnlyShowIn/NotShowIn/TryExec | Matches Phase 5 precedent (`Config::applyFile`); format is simple INI-like key=value with one relevant section — a library (e.g. libgio's `GKeyFile` via GLib) would drag in a large, heavyweight dependency (glib-2.0 is already on the dev box only because of GNOME apps, not a WM dependency) for a ~150-line parser |
| Hand-rolled ELF `.dynamic` reader | N/A (project code) | Read `DT_NEEDED` entries from `/usr/bin` binaries to detect GUI toolkit linkage | No `libelf-dev` package is installed on the target dev machine `[VERIFIED: dpkg -l | grep libelf-dev — empty]`; a raw `Elf64_Ehdr`/`Elf64_Shdr`/`Elf64_Dyn` struct-cast reader (per `elf(5)` layout) avoids adding a new pkg-config dependency and avoids spawning `readelf`/`objdump` subprocesses (measured 11.4s for ~2400 files via subprocess spawn vs an estimated sub-second in-process read) |
| Hand-rolled cache serializer | N/A (project code) | Persist `appcache.json` (or equivalent) | No JSON library is currently vendored (`STACK.md`, confirmed: no nlohmann/rapidjson/jsoncpp in `CMakeLists.txt` or `dpkg`); given the small data volume (hundreds of app entries), a simple line-oriented or minimal-JSON-subset writer avoids a new dependency entirely, matching the project's stated "must remain lightweight" constraint |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| nlohmann/json (single header, via CMake `FetchContent`) | 3.11.x (verify at planning time) | Full JSON parse/serialize with DOM API | Only if the planner decides real interoperability with other JSON tools matters (e.g., a future config-GUI IPC in Phase 9 already uses JSON per CGUI-02) — otherwise skip; adds STL/exception overhead not needed for a scan cache `[ASSUMED — package name/version not verified via authoritative registry in this session]` |
| `libelf`/`elfutils` (`libelf-dev` via pkg-config) | N/A | Alternative to hand-rolled ELF parsing | Only if maintenance burden of hand-rolled struct parsing becomes a problem; NOT currently installed on dev machine, would be a *new* build dependency — avoid unless hand-rolled parsing proves too fragile |
| `desktop-file-utils` (`desktop-file-validate`) | N/A (dev/test tool only, not linked) | Validate hand-rolled `.desktop` parser against real-world files during test-writing | Useful as a *test fixture generator/validator* during development, not a runtime dependency |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Hand-rolled `.desktop` parser | GLib's `GKeyFile` (`glib-2.0` via pkg-config) | Correct, spec-compliant, but pulls in the entire glib-2.0 dependency chain (gobject, gio) purely for key-file parsing — disproportionate for a project whose stated goal is a minimal WM; also glib is a GNOME-ecosystem dependency the project has deliberately avoided everywhere else |
| Hand-rolled ELF reader | `libelf`/`elfutils`, or shelling out to `readelf`/`objdump` | `libelf` adds a new pkg-config dependency not currently present; shelling out to `readelf` per binary is measurably slow (11.4s wall-clock for ~2400 files on this dev machine due to process-spawn overhead) and adds an external-tool runtime dependency (`binutils`) that may not be installed on a minimal VPS |
| Hand-rolled cache format | nlohmann/json | nlohmann is a legitimate, extremely widely-used single-header library, but it is DOM-based (full parse tree, STL/exceptions) and ~10x slower than SAX-style parsers on large payloads; for a cache of a few hundred small records this overhead doesn't matter in absolute terms, but it's still a new dependency that a hand-rolled reader avoids entirely — use it if the planner values a standard, tool-interoperable format (e.g., users inspecting/editing appcache.json with `jq`) over dependency minimalism |

**Installation:**
```bash
# No new runtime dependencies required if hand-rolled parsers/serializer are chosen (recommended).
# If nlohmann/json is chosen instead, add via CMake FetchContent (same pattern as Catch2 in CMakeLists.txt):
#   FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3)
#   FetchContent_MakeAvailable(json)
#   target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)
```

**Version verification:** No new packages recommended by default (hand-rolled path). If nlohmann/json is chosen, verify the current tag at planning/implementation time via `git ls-remote --tags https://github.com/nlohmann/json.git` — this project has no npm/pip/cargo registry to query against since it's a C++ CMake `FetchContent` dependency (git tag, not a package-manager registry entry). `[ASSUMED]` version 3.11.x based on training data; must be confirmed at implementation time.

## Package Legitimacy Audit

This phase's **recommended path adds zero new external dependencies** (hand-rolled `.desktop` parser, hand-rolled ELF `.dynamic` reader, hand-rolled cache serializer — all project code, not packages). The Package Legitimacy Gate (npm/PyPI/crates registry checks) does not apply to C++ header-only libraries fetched via CMake `FetchContent` from a git tag rather than a package-manager registry, so `npm view` / `pip index versions` / `cargo search` are not meaningful checks here.

If the planner chooses the **optional** `nlohmann/json` path instead of hand-rolling the cache serializer:

| Package | Registry | Age | Downloads | Source Repo | Verdict | Disposition |
|---------|----------|-----|-----------|-------------|---------|-------------|
| nlohmann/json | GitHub (not npm/PyPI/crates — CMake FetchContent from git tag) | ~11 years (repo created 2013) | N/A (not registry-distributed; ~40k GitHub stars) | github.com/nlohmann/json | Not applicable — no `gsd-tools query package-legitimacy check` verdict possible for a non-registry C++ dependency; well-known, long-lived, widely-used project by direct inspection | If chosen: `checkpoint:human-verify` before adding — confirm the exact git tag/commit hash pinned in `CMakeLists.txt` matches the official repo's release tags (protects against a supply-chain substitution of the FetchContent URL) |

**Packages removed due to [SLOP] verdict:** none (no packages recommended by default).
**Packages flagged as suspicious [SUS]:** none.

*nlohmann/json's legitimacy as a widely-used project is `[ASSUMED]` from training knowledge / general web presence, not verified against an authoritative package registry (none exists for this C++ FetchContent pattern) — the planner should still gate its addition behind a `checkpoint:human-verify` task per the note above if it is chosen over the hand-rolled recommendation.*

## Architecture Patterns

### System Architecture Diagram

```
Startup (main.cpp)
    │
    ▼
Config::load()  ──────────────────────────────► manual menu-entry keys parsed
    │                                             (extends Config::applyKeyValue,
    │                                              Config.cpp:136)
    ▼
AppCache::loadOrRescan(appcachePath)
    │
    ├─ mtime check: XDG .desktop dirs + /usr/bin vs cached scan timestamp
    │       │
    │       ├─ unchanged ──► parse appcache.json ──► in-memory AppEntry list
    │       │
    │       └─ changed/missing ──► full rescan:
    │              │
    │              ├─ DesktopEntryParser: walk $XDG_DATA_DIRS/applications/*.desktop
    │              │      → parse Name/Exec/Icon/Categories/NoDisplay/Hidden/
    │              │        OnlyShowIn/NotShowIn/TryExec
    │              │      → filter per D-05 XDG visibility rules
    │              │
    │              ├─ BinaryScanner: walk /usr/bin/*
    │              │      → skip shebang scripts, non-executables
    │              │      → read ELF .dynamic section DT_NEEDED entries
    │              │        (mmap + raw struct parse, no libelf)
    │              │      → classify GUI vs CLI by known toolkit lib substrings
    │              │      → synthesize AppEntry for GUI binaries lacking a
    │              │        matching .desktop entry
    │              │
    │              └─ write appcache.json with new scan timestamp
    │
    ▼
Merge step (AppEntry list ⊕ manual Config entries)
    │
    ├─ manual entry name matches auto-discovered name → manual entry wins (D-08)
    ├─ manual entry has no category= key → placed in "Custom" category (D-07)
    │
    ▼
Categorized AppEntry list (grouped by Category string) passed into WindowManager
    │
    ▼
WindowManager::menu() [src/Buttons.cpp:104]
    │
    ├─ top level: existing flat entries (New xterm, hidden windows, Exit)
    │              + one entry per category (Internet, Graphics, ... , Custom)
    │
    └─ hover/click on a category ──► submenu popup window
             │
             └─ click an app entry ──► spawn()-style double-fork + execvp(Exec argv)
                                       (reuses src/Manager.cpp:770 spawn() pattern,
                                        extended to accept an arbitrary argv instead
                                        of only m_config.newWindowCommand)
```

### Recommended Project Structure
```
src/
├── DesktopEntry.cpp      # .desktop file parser (pure data, no X11) — Name/Exec/Icon/
│                         #   Categories/NoDisplay/Hidden/OnlyShowIn/NotShowIn/TryExec
├── BinaryScanner.cpp      # ELF .dynamic-section reader + GUI/CLI heuristic classifier
├── AppCache.cpp           # appcache.json read/write + mtime-based invalidation
├── AppEntry.h             # shared struct: {name, exec, icon, category, source}
├── Buttons.cpp            # extended: submenu rendering/hover, category dispatch
├── Config.cpp             # extended: menu-entry-name=/menu-entry-command=/
│                         #   menu-entry-category= key parsing
└── Manager.cpp            # extended: spawn()-style exec generalized for menu entries

include/
├── DesktopEntry.h
├── BinaryScanner.h
├── AppCache.h
└── AppEntry.h

tests/
├── test_desktopentry.cpp  # pure data parsing tests (no Xvfb needed, like test_config.cpp)
├── test_binaryscanner.cpp # ELF parsing tests against synthetic/real /usr/bin samples
└── test_appcache.cpp      # cache read/write/invalidation tests
```

### Pattern 1: Free-standing data classes, X11-free (mirrors `Config`)
**What:** `DesktopEntry`/`BinaryScanner`/`AppCache` are plain C++ classes/structs with `std::string`/`std::vector` members, no `Display*` or `Window` dependency.
**When to use:** Any logic that doesn't directly manipulate X11 state — matches the existing `Config` class, which is deliberately X11-free and unit-testable without Xvfb (`test_config.cpp` links no X11).
**Example:**
```cpp
// Source: project precedent — include/Config.h:6-44 (existing pattern to follow)
struct AppEntry {
    std::string name;
    std::string exec;       // already unquoted/unescaped/field-code-stripped
    std::string icon;
    std::string category;   // "Custom" default per D-07, or XDG category
    enum class Source { Desktop, BinaryScan, Manual } source;
};
```

### Pattern 2: mtime-based cache invalidation (extends Phase 5 XDG path resolution)
**What:** Compare the latest mtime across all `.desktop` directories and `/usr/bin` against a stored `scannedAt` timestamp in the cache file; rescan if any directory's mtime is newer.
**When to use:** At every WM startup, before menu construction (D-06).
**Example:**
```cpp
// Source: project pattern — reuses xdgConfigDirs()-style enumeration from src/Config.cpp:60
// (new function, e.g. xdgDataDirs(), following the same pattern)
bool needsRescan(const std::string& cachePath, time_t cachedScanTime) {
    struct stat st;
    if (stat("/usr/bin", &st) == 0 && st.st_mtime > cachedScanTime) return true;
    for (const auto& dir : xdgApplicationsDirs()) {  // new helper, mirrors xdgConfigDirs()
        if (stat(dir.c_str(), &st) == 0 && st.st_mtime > cachedScanTime) return true;
    }
    return false;
}
```

### Pattern 3: ELF `.dynamic` section reader (no libelf)
**What:** mmap the candidate binary, parse `Elf64_Ehdr` for `e_shoff`/`e_phoff`, locate the `SHT_DYNAMIC` section (or `PT_DYNAMIC` program header — more robust for stripped binaries, works without section headers), walk `Elf64_Dyn` entries until `DT_NULL`, and for each `DT_NEEDED` resolve the library name from `.dynstr`.
**When to use:** For every ELF candidate in `/usr/bin` during a rescan.
**Example:**
```cpp
// Source: [CITED: man7.org elf(5) — struct layout] + [CITED: WebSearch synthesis of
//          codestudy.net ELF-parsing guide and finixbit/elf-parser reference implementation]
// Elf64_Dyn as defined by elf(5):
typedef struct {
    int64_t  d_tag;
    union { uint64_t d_val; uint64_t d_ptr; } d_un;
} Elf64_Dyn;
// DT_NEEDED == 1, DT_NULL == 0 (per elf.h) — walk until DT_NULL, resolve d_un.d_val
// as a byte offset into the .dynstr section for each DT_NEEDED entry.
```

### Pattern 4: Nested submenu extension of existing popup (structural reference only)
**What:** `WindowManager::menu()` currently renders one flat popup window with a single click-to-select loop (`src/Buttons.cpp:104-314`). Adding category submenus requires either (a) a second popup `Window` created/positioned relative to the hovered category row, or (b) reusing `m_menuWindow` and redrawing its contents when a category is hovered, tracking a "menu stack" (top-level vs. active category).
**When to use:** For D-03/D-04's category hover-to-expand requirement.
**Prior art (structural reference, not code-copying — this is a from-scratch reimplementation):**
- **fluxbox** (`~/.fluxbox/menu`): flat bracket-tag DSL (`[begin]`/`[submenu]`/`[end]`), parsed recursively — "no limit to the number of levels or nested submenus." `[CITED: fluxbox.org/help/man-fluxbox-menu, fluxbox-menu(5) manpage]`
- **openbox** (`~/.config/openbox/menu.xml`): genuine XML with nested `<menu>` elements; commonly delegates XDG category generation to an external `xdg_menu --format openbox3-pipe` pipe-menu generator rather than hardcoding categories. `[CITED: ArchWiki Xdg-menu]`
- Both support unlimited submenu nesting recursively; Phase 7 only needs **one level** (category → apps), so the simplest viable design is a second popup window spawned on hover, not a general recursive menu-stack architecture.

### Anti-Patterns to Avoid
- **Shelling out to `ldd`/`readelf` per binary at runtime:** measured at 11.4s wall-clock for ~2400 files on this dev machine due to process-spawn overhead alone — unacceptable for a VPS with 512MB RAM and a "fast startup" requirement (D-06). Parse ELF in-process instead.
- **Executing `ldd` on untrusted/arbitrary binaries:** `ldd` has historically been able to execute the target binary's code in certain configurations (via `LD_TRACE_LOADED_OBJECTS` edge cases on some ELF interpreters) — prefer static section parsing (`readelf -d`-equivalent logic) which never loads/executes the target.
- **Passing raw `Exec=` strings to `/bin/sh -c` unconditionally:** matches a real-world attack pattern where attackers embed shell commands in a `.desktop` file's `Exec` key disguised as an innocuous entry. Parse `Exec` into an argv array and use `execvp()` directly (not through a shell) unless the project's existing `execUsingShell` config flag (`src/Manager.cpp:783`) is explicitly set by the user for that specific launch path.
- **Building a general N-level recursive menu-stack architecture when only one level is required:** D-03/D-04 only need category → app (one level); resist the urge to build fluxbox-style infinite nesting when the phase's actual requirement is bounded.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Exec field code / quoting validation | A fully spec-compliant field-code expander from scratch without reading the reserved-character/escaping rules first | The documented reserved-character list and escaping order from the Desktop Entry Spec (see Common Pitfalls below) — implement it precisely, don't improvise a subset | The spec's escaping rules are subtle and ordered (string-level backslash escaping happens *before* quoting); an ad-hoc implementation is a known source of the exact kind of security bug this phase must avoid |
| Double-fork zombie-avoidance for spawning selected apps | A new process-spawning mechanism | `WindowManager::spawn()` (`src/Manager.cpp:770`) — already implements double-fork + `execl`/`execlp`, DISPLAY env propagation, and closing the X connection fd in the child | This exact problem (avoid zombies, don't inherit the X connection) is already solved and tested for `newWindowCommand`; generalize it to accept an arbitrary argv rather than reimplementing |
| XDG directory precedence/enumeration | A new ad-hoc `$XDG_DATA_DIRS`/`$XDG_DATA_HOME` splitter | `xdgConfigDirs()`-style helper already exists in `src/Config.cpp:60` for `$XDG_CONFIG_DIRS` — write an analogous `xdgDataDirs()`/`xdgApplicationsDirs()` following the exact same colon-split-with-absolute-path-filter pattern | Consistency with the existing, already-tested XDG path resolution code; avoids subtly different edge-case handling between the two XDG variable families |

**Key insight:** This phase's temptation is to reach for "the standard library" for each sub-problem (GLib for `.desktop` parsing, `libelf` for ELF, `nlohmann/json` for the cache) — but every one of those libraries is disproportionate to the actual problem size here (hundreds of small records, a handful of fields per `.desktop` file, one section of one ELF file per binary). The project's own Phase 5 precedent (hand-rolled config parser) is the right model to follow throughout.

## Common Pitfalls

### Pitfall 1: Exec field code expansion done naively
**What goes wrong:** A naive `%f`/`%F`/`%u`/`%U` substitution that doesn't strip unused field codes, allows multiple file/url field codes, or expands field codes inside quoted arguments produces malformed or exploitable command lines.
**Why it happens:** The spec's rules are non-obvious: field codes not listed in the spec make the *entire command line invalid* (must not process it at all, not just ignore the unknown code); at most one `%f`/`%u`/`%F`/`%U` per command line; field codes must never be expanded while inside a quoted argument.
**How to avoid:** Since Phase 7 doesn't need to open files with these apps (menu launch only, no drag-and-drop/file-association support), the simplest safe approach is to strip all recognized field codes (`%f %F %u %U %i %c %k %v %m` etc.) unconditionally before executing — Phase 7's use case never supplies a file argument, so no field-code expansion is actually needed, only removal.
**Warning signs:** Any `.desktop` entry whose `Exec=` still contains a literal `%` character after your stripping pass — check for unescaped single `%` (must be `%%` for literal percent per spec).

### Pitfall 2: Executing Exec strings through a shell by default
**What goes wrong:** Passing the raw `Exec=` value to `/bin/sh -c` (mirroring `m_config.execUsingShell` for `newWindowCommand`) re-introduces shell metacharacter injection risk for a field that is explicitly documented as a known real-world attack vector (malicious `.desktop` files with weaponized `Exec` keys).
**Why it happens:** It's the path of least resistance since `spawn()` already supports a shell-exec code path.
**How to avoid:** Parse `Exec=` into an argv array (respecting the spec's quoting rules) and call `execvp()` directly with that argv — never route auto-discovered or `.desktop`-sourced Exec strings through `/bin/sh -c`. Manual config entries can reuse the existing `execUsingShell` flag if the user explicitly opts in, since those are user-authored, not externally supplied.
**Warning signs:** Any code path that does `sh -c` with a string built from parsed `.desktop`/config file content without first tokenizing it into a fixed argv.

### Pitfall 3: Directory mtime doesn't catch in-place file edits reliably in all cases
**What goes wrong:** D-06's cache invalidation strategy (compare `.desktop` dirs' and `/usr/bin`'s mtime against the cached scan timestamp) correctly catches file **additions and removals** (these always bump the containing directory's mtime), but editing an *existing* `.desktop` file's contents in place (e.g., `vim` in-place edit that doesn't rename/recreate the file) only bumps that file's own mtime, not necessarily its parent directory's mtime on all filesystems/edit patterns (e.g., some editors preserve the original inode via `write()`+`ftruncate()` rather than rename-swap).
**Why it happens:** POSIX directory mtime only updates when the directory's *entry list* changes (add/remove/rename a file), not when a file's contents change.
**How to avoid:** This is an accepted, documented limitation per D-06 ("no explicit Rescan menu item needed for Phase 7 — may be added later if startup rescan proves too slow in practice"); the phase's success criteria only requires catching *new* apps, not in-place `.desktop` edits. Document this explicitly as a known limitation rather than silently under-covering it; if stronger guarantees are wanted later, compare each `.desktop` file's own mtime individually (still cheap — 179 files on this dev machine) rather than only the directory's mtime, since 179 individual `stat()` calls costs negligible time versus scanning 2400 binaries.
**Warning signs:** User reports "I edited a .desktop file and it didn't show up until I touched the directory" — expected under the current design; not a bug unless individual-file mtime checking is added later.

### Pitfall 4: Scanning `/usr/bin` by shelling out per file
**What goes wrong:** Spawning `readelf`/`ldd`/`file` as a subprocess for every one of ~2400 files in `/usr/bin` costs multiple seconds of wall-clock time purely from process-spawn overhead — measured at **11.4s real / 4.8s user / 10.0s sys** on this dev machine for a `readelf -d` loop.
**Why it happens:** It's the simplest thing to write, and each individual invocation seems fast in isolation.
**How to avoid:** Parse the ELF `.dynamic` section directly in-process (mmap + raw struct read, Pattern 3 above). This eliminates ~2400 `fork`/`exec` pairs and the associated `sys` time overhead (9.97s of the 11.4s measured above was kernel/syscall time, almost entirely fork/exec/wait overhead, not actual file I/O).
**Warning signs:** Startup rescan taking multiple seconds when profiled — the fix is almost always "stop shelling out per file."

## Code Examples

### Reading `.dynamic` DT_NEEDED entries (structural sketch, not copy-paste-ready)
```cpp
// Source: [CITED: man7.org/linux/man-pages/man5/elf.5.html — Elf64_Dyn layout]
// and [CITED: WebSearch synthesis referencing github.com/finixbit/elf-parser as a
// dependency-free reference implementation of the same technique]
//
// 1. mmap the file read-only.
// 2. Validate ELF magic (0x7f 'E' 'L' 'F') before any further parsing — never trust
//    file extension or executable bit alone.
// 3. Read Elf64_Ehdr; get e_shoff/e_shnum/e_shentsize (or e_phoff/e_phnum for the
//    PT_DYNAMIC approach, which also works on stripped binaries).
// 4. Locate the SHT_DYNAMIC section and its linked .dynstr string table (or find
//    PT_DYNAMIC and read its p_vaddr-mapped .dynamic content directly).
// 5. Cast that byte range to Elf64_Dyn[]; iterate until d_tag == DT_NULL (0).
// 6. For each d_tag == DT_NEEDED (1), read the null-terminated string at
//    .dynstr + d_un.d_val.
// 7. Compare resolved names against a known GUI-stack substring list:
//    libX11, libgtk-3, libgtk-4, libQt5*, libQt6*, libwayland-client, libSDL2.
```

### Reusing `spawn()` for arbitrary menu-entry Exec (structural sketch)
```cpp
// Source: project code — src/Manager.cpp:770-791 (WindowManager::spawn()), generalized
void WindowManager::spawnArgv(const std::vector<std::string>& argv) {
    // Same double-fork pattern as spawn(): fork() twice, close connection fd in
    // grandchild, setenv DISPLAY, then execvp(argv[0], argv.data()) instead of the
    // hardcoded m_config.newWindowCommand execl/execlp calls.
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|---------------|--------|
| Compile-time root menu (upstream wm2: hardcoded "New xterm"/hidden windows/Exit only) | Runtime-populated, categorized, XDG+heuristic-discovered menu | This phase (Phase 7) | Root menu becomes data-driven; requires the AppScanner/AppCache/DesktopEntry subsystems described above |
| Flat single-level popup menu (`src/Buttons.cpp:104`, current codebase) | Nested category submenus | This phase | `WindowManager::menu()` needs a second popup/hover-state mechanism it does not currently have |

**Deprecated/outdated:**
- Shelling out to `ldd` for dependency inspection is discouraged for untrusted binaries in favor of static section parsing (`readelf`-equivalent logic) — not because `ldd` is deprecated per se, but because it is documented as capable of executing target code in some configurations, which is inappropriate when scanning arbitrary `/usr/bin` contents.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | nlohmann/json version ~3.11.x is current | Standard Stack / Supporting | Low — this is an optional alternative, not the recommended path; if chosen, verify the tag at implementation time via `git ls-remote` |
| A2 | nlohmann/json is a legitimate, non-malicious, actively maintained project | Package Legitimacy Audit | Low-Medium — it is an extremely well-known project, but this session did not run an authoritative registry check (none exists for CMake FetchContent-from-git dependencies); mitigated by requiring `checkpoint:human-verify` on the pinned git tag/commit if this path is chosen |
| A3 | The GUI-toolkit library substring list (libX11, libgtk-3/4, libQt5/6, libwayland-client, SDL2) is sufficient to classify the majority of real-world GUI binaries in /usr/bin | Architecture Patterns / Don't Hand-Roll | Medium — some GUI apps could be missed (e.g., unusual toolkits, Electron apps that bundle their own copy of libraries rather than dynamically linking system ones) or false-positived (a CLI tool that happens to link libX11 for clipboard support); this is explicitly flagged in CONTEXT.md as "Claude's Discretion" for exact scoring thresholds, and should be validated empirically against this project's actual `/usr/bin` (2402 files, 1865 ELF / 534 scripts measured on this dev machine) during implementation |
| A4 | Directory-mtime-only cache invalidation (not per-file mtime) is an acceptable limitation for Phase 7 | Common Pitfalls / Pitfall 3 | Low — D-06 explicitly scopes this as acceptable ("may be added later if startup rescan proves too slow"), but flagging here in case the planner wants to upgrade to per-file mtime checking (measured cheap: 179 desktop files on this dev machine) |

## Open Questions

1. **Exact submenu rendering mechanism: second popup window vs. redrawn single window with a menu stack**
   - What we know: The existing `menu()` uses one `m_menuWindow` (`include/Manager.h:139`) reused across calls via `XftDrawChange`. A second-level submenu could either be a second, separately-managed `Window` (simpler grab/ungrab semantics but more X11 resource bookkeeping) or a state machine within the same popup that redraws its contents when a category row is hovered (matches "minimal chrome" visual philosophy more directly but is more code).
   - What's unclear: Which approach better preserves the existing grab/ungrab and click-to-select loop structure in `Buttons.cpp:104-314` without a large rewrite.
   - Recommendation: Planner should prototype the simpler "second popup window positioned adjacent to the hovered category row" first — it reuses the exact same `attemptGrab`/`releaseGrab`/`XftDraw` patterns already in `menu()`, just instantiated twice, rather than requiring a new stateful redraw model.

2. **Where exactly manual config entries and auto-discovered entries get merged relative to `WindowManager` construction**
   - What we know: `main.cpp` currently does `Config::load()` then constructs `WindowManager`. CONTEXT.md leaves "where app scanning/cache-loading happens" (main.cpp before construction, vs. lazily on first menu open) as Claude's discretion.
   - What's unclear: Whether a slow-ish first-menu-open rescan (if the cache is stale) would introduce a noticeable delay the *first* time a user right-clicks vs. a slightly slower startup that's invisible to the user.
   - Recommendation: Scan eagerly at startup (in `main.cpp`, before or during `WindowManager` construction) rather than lazily on first menu open — a slightly slower `wm2-born-again` process start is less user-visible than a laggy first right-click, and D-06 already frames this as a startup-time cost ("appcache.json rebuilds automatically at every WM startup").

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| `readelf`/`objdump` (binutils) | Reference/manual verification during development only (not a runtime dependency if hand-rolled ELF parsing is used) | ✓ | readelf from binutils (present) | N/A — not a runtime dependency in the recommended design |
| `libelf-dev` (pkg-config `libelf`) | Only needed if the planner chooses `libelf` over hand-rolled ELF parsing | ✗ (not installed: `dpkg -l | grep libelf-dev` empty; only runtime `libelf1` is present, no headers) | — | Hand-rolled ELF `.dynamic` reader (recommended; avoids this dependency entirely) |
| `nlohmann/json` (CMake FetchContent) | Only needed if the planner chooses JSON-library cache serialization over hand-rolled | ✗ (not vendored yet) | — | Hand-rolled minimal cache serializer (recommended) |
| XDG `.desktop` files under `/usr/share/applications` | APPS-01 scan target | ✓ | 179 `.desktop` files present on this dev machine | N/A — if the directory is empty/absent, the scanner should simply produce zero desktop-sourced entries and continue (not fatal) |
| `/usr/bin` | APPS-02 scan target | ✓ | 2402 entries (1865 ELF binaries, 534 shebang scripts, measured on this dev machine) | N/A — always present on any Linux system |
| Xvfb (for menu-rendering tests) | Submenu rendering tests (X11-dependent) | ✓ (already used by existing test suite per `CMakeLists.txt`) | — | N/A — already a project-established test dependency |

**Missing dependencies with no fallback:** none.

**Missing dependencies with fallback:** `libelf-dev` and `nlohmann/json` — both have a hand-rolled fallback that is in fact the *recommended* primary path, not a degraded fallback.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 v3.14.0 (already vendored via CMake `FetchContent`, per `CMakeLists.txt:46-51`) |
| Config file | `CMakeLists.txt` (test targets added alongside existing `test_config`, `test_client`, etc.) |
| Quick run command | `ctest -R "desktopentry\|binaryscanner\|appcache" --output-on-failure` (after adding the new test executables below) |
| Full suite command | `ctest --output-on-failure` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| APPS-01 | `.desktop` file parses Name/Exec/Icon/Categories/NoDisplay/Hidden/OnlyShowIn/NotShowIn/TryExec correctly; entries filtered per D-05 | unit | `ctest -R desktopentry --output-on-failure` | ❌ Wave 0 |
| APPS-01 | Malformed/malicious `.desktop` Exec fields (unescaped shell metacharacters, unknown field codes) are rejected or neutralized, not passed to execvp raw | unit | `ctest -R desktopentry --output-on-failure` | ❌ Wave 0 |
| APPS-02 | ELF `.dynamic` reader correctly extracts DT_NEEDED library names from a real binary (e.g., `/bin/ls` vs a GTK app) | unit | `ctest -R binaryscanner --output-on-failure` | ❌ Wave 0 |
| APPS-02 | Shebang scripts and non-executable files are skipped without attempting ELF parse | unit | `ctest -R binaryscanner --output-on-failure` | ❌ Wave 0 |
| APPS-03 | `appcache.json` round-trips (write then read produces identical AppEntry list) | unit | `ctest -R appcache --output-on-failure` | ❌ Wave 0 |
| APPS-03 | Cache invalidation triggers rescan when a `.desktop` dir or `/usr/bin` mtime is newer than the cached scan timestamp | unit | `ctest -R appcache --output-on-failure` | ❌ Wave 0 |
| APPS-04 | Manual config `menu-entry-*` keys parse correctly and merge with auto-discovered entries per D-07/D-08 (name override, default Custom category) | unit | `ctest -R "config\|appcache"` | ❌ Wave 0 (extends existing `test_config.cpp`) |
| APPS-05 | Root menu displays category submenus with correct entries (manual UAT — X11 rendering is not easily assertable in an automated Catch2 test without a screenshot-diffing harness, which this project does not have) | manual-only | N/A — verified via `/gsd-verify-work` conversational UAT, matching how Phase 4/6 visual changes were validated | ❌ (no automated coverage; documented as manual-only, consistent with project's existing lack of screenshot-diff tooling) |

### Sampling Rate
- **Per task commit:** `ctest -R "desktopentry|binaryscanner|appcache|config" --output-on-failure`
- **Per wave merge:** `ctest --output-on-failure` (full suite, including existing Xvfb-based smoke/client/ewmh tests)
- **Phase gate:** Full suite green before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `tests/test_desktopentry.cpp` — covers APPS-01 (pure data parsing, no Xvfb needed, follows `test_config.cpp` pattern)
- [ ] `tests/test_binaryscanner.cpp` — covers APPS-02 (needs real ELF file fixtures — can use `/bin/true`, `/bin/ls`, or synthetic minimal ELF byte arrays as test fixtures; no Xvfb needed)
- [ ] `tests/test_appcache.cpp` — covers APPS-03 (pure data, no Xvfb needed)
- [ ] `CMakeLists.txt` additions — three new `add_executable` blocks matching the `test_config` pattern (Catch2, no X11 link needed for desktopentry/appcache; binaryscanner needs no X11 either, just filesystem access)
- [ ] No framework install needed — Catch2 already vendored

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | N/A — local single-user WM, no authentication surface |
| V3 Session Management | no | N/A |
| V4 Access Control | no | N/A — no privilege boundary within this phase (all code runs as the invoking user) |
| V5 Input Validation | yes | Hand-rolled `.desktop` Exec-field validation per Desktop Entry Spec reserved-character/escaping rules (see Common Pitfalls #1/#2); reject unknown field codes; never pass raw untrusted Exec strings to `/bin/sh -c` |
| V6 Cryptography | no | N/A — no cryptographic operations in this phase |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Malicious `.desktop` file with weaponized `Exec=` key (documented real-world attack pattern: attacker embeds shell command in `Exec` while keeping the rest of the file looking legitimate) | Tampering / Elevation of Privilege | Parse `Exec=` into a fixed argv and call `execvp()` directly (never `/bin/sh -c` with an unvalidated string); reject command lines containing field codes not defined by the spec; validate that the resolved binary path exists before offering the entry in the menu (this is what `TryExec` already does per D-05, extend the same distrust to `Exec`'s own binary component) |
| Path traversal / symlink tricks via `Icon=` field pointing outside expected icon directories | Tampering | Out of scope for Phase 7's actual behavior since icons are explicitly not rendered ("Icons/icon tray" is a documented project-wide Out of Scope item in REQUIREMENTS.md) — if `Icon=` is parsed at all, treat it as an opaque display string only, never open/read the referenced file as a path in this phase |
| Config-file manual entries (`menu-entry-command=`) executing arbitrary commands | N/A (by design — this is a deliberate user-authored trust boundary, not an attack surface) | These are already user-authored (the user edits their own config file), so no additional validation beyond what Phase 5 already applies to config values; the `execUsingShell` flag remains an explicit, user-controlled opt-in for this path only |
| Symlink/TOCTOU on `/usr/bin` scan between `stat()` (executable-bit check) and the later ELF-read `open()`/`mmap()` | Tampering (theoretical, low practical risk for a local single-user scan of system binaries) | Use `open()` then `fstat()` on the resulting fd (not `stat()` then separate `open()`) to avoid a race between the permission check and the read; low real-world severity since this is a local root-owned directory scan, but cheap to get right |

## Sources

### Primary (HIGH confidence)
- `man7.org/linux/man-pages/man5/elf.5.html` — `Elf64_Dyn`/`Elf64_Ehdr` struct layout, DT_NEEDED/DT_NULL semantics (official Linux man page, cited via WebSearch synthesis)
- Project codebase: `src/Buttons.cpp`, `src/Manager.cpp`, `src/Config.cpp`, `include/Manager.h`, `include/Config.h`, `CMakeLists.txt` — read directly in this session
- Direct tool verification on this dev machine: `dpkg -l | grep libelf-dev` (absent), `find /usr/share/applications -name '*.desktop' | wc -l` (179), `/usr/bin` file-type census (1865 ELF / 534 scripts / 2402 total), `time (readelf -d loop)` = 11.4s real

### Secondary (MEDIUM confidence)
- `specifications.freedesktop.org/desktop-entry/latest/` and `specifications.freedesktop.org/desktop-entry/latest/recognized-keys.html` (via WebSearch synthesis) — Desktop Entry Spec key fields, NoDisplay/Hidden/OnlyShowIn/NotShowIn/TryExec semantics
- `specifications.freedesktop.org/desktop-entry/latest/exec-variables.html` (via WebSearch synthesis) — Exec field code and quoting rules
- `specifications.freedesktop.org/menu/latest/category-registry.html` (via WebSearch synthesis) — registered Main Categories list
- `specifications.freedesktop.org/basedir/latest/` (via WebSearch synthesis) — XDG_DATA_HOME/XDG_DATA_DIRS defaults
- `fluxbox.org/help/man-fluxbox-menu/`, `fluxbox-menu(5)` manpage (via WebSearch synthesis) — nested submenu DSL prior art
- ArchWiki `Xdg-menu` page (via WebSearch synthesis) — openbox menu.xml structure and xdg_menu pipe-menu pattern
- `linuxsecurity.com/features/understanding-malicious-desktop-files` (via WebSearch synthesis) — real-world malicious `.desktop` Exec-key attack pattern
- `github.com/finixbit/elf-parser`, codestudy.net ELF-parsing guide (via WebSearch synthesis) — reference implementation pattern for hand-rolled ELF DT_NEEDED extraction

### Tertiary (LOW confidence)
- nlohmann/json version/currency claim (A1 in Assumptions Log) — not verified against an authoritative registry in this session
- Exact GUI-toolkit-library substring list completeness (A3 in Assumptions Log) — reasonable starting set based on WebSearch synthesis of real-world `ldd` output examples, not exhaustively validated against this project's actual heuristic scoring implementation

## Metadata

**Confidence breakdown:**
- Standard stack: MEDIUM — hand-rolled approach is well-justified by direct codebase precedent and measured performance data (HIGH), but exact appcache.json schema/versioning is a design choice left to the planner (MEDIUM)
- Architecture: MEDIUM-HIGH — the architectural separation (X11-free data classes vs. X11-only presentation) directly mirrors the existing `Config`/`WindowManager` split (HIGH); the exact submenu rendering mechanism is an open question (MEDIUM)
- Pitfalls: HIGH — Exec field-code/quoting rules and the malicious-.desktop-file attack pattern are both officially documented / directly cited; the mtime-invalidation limitation was independently confirmed as a POSIX filesystem semantic

**Research date:** 2026-07-08
**Valid until:** 2026-08-07 (30 days — freedesktop.org specs are stable/rarely-changing; ELF format is decades-stable; re-verify nlohmann/json version and package-legitimacy status if that optional path is chosen closer to implementation time)
