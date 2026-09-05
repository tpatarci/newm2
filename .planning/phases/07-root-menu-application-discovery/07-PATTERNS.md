# Phase 7: Root Menu + Application Discovery - Pattern Map

**Mapped:** 2026-07-08
**Files analyzed:** 11 (5 new source, 4 new headers, 3 new test files, plus modifications to 4 existing files)
**Analogs found:** 11 / 11 (all have at least a role-match analog; no true "no analog" files this phase)

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|--------------------|------|-----------|-----------------|----------------|
| `include/AppEntry.h` | model | transform | `include/Config.h` (plain struct, no X11) | role-match |
| `include/DesktopEntry.h` / `src/DesktopEntry.cpp` | service (parser) | file-I/O + transform | `include/Config.h` / `src/Config.cpp` (`applyFile`, key=value parsing) | exact |
| `include/BinaryScanner.h` / `src/BinaryScanner.cpp` | service (scanner) | file-I/O + transform | `src/Config.cpp` (XDG dir enumeration `xdgConfigDirs()`); no direct ELF-parsing analog exists | role-match (partial — novel binary-format logic) |
| `include/AppCache.h` / `src/AppCache.cpp` | service (cache/serializer) | file-I/O + CRUD (read/write cache) | `src/Config.cpp` (`applyFile`/`load` layered read pattern) | role-match |
| `src/Config.cpp` (extended) | config/model | CRUD (parse new `menu-entry-*` keys) | itself — `Config::applyKeyValue` (`src/Config.cpp:136-188`) | exact (same file, additive) |
| `src/Buttons.cpp` (extended `menu()`) | controller (event loop) | request-response (X11 event-driven) | itself — `WindowManager::menu()` (`src/Buttons.cpp:104-314`) | exact (same file, additive — submenu extension) |
| `src/Manager.cpp` (new `spawnArgv()`) | service (process control) | event-driven (fork/exec) | itself — `WindowManager::spawn()` (`src/Manager.cpp:770-800`) | exact (same file, generalized) |
| `src/main.cpp` (extended) | config/wiring | request-response (startup sequencing) | itself — current `main()` (`src/main.cpp:5-9`) | exact (same file, additive) |
| `tests/test_desktopentry.cpp` | test | transform | `tests/test_config.cpp` | exact |
| `tests/test_binaryscanner.cpp` | test | transform | `tests/test_config.cpp` | role-match |
| `tests/test_appcache.cpp` | test | CRUD | `tests/test_config.cpp` | exact |

## Pattern Assignments

### `include/AppEntry.h` (model, transform)

**Analog:** `include/Config.h`

**Struct/aggregate pattern** (lines 6-31 of `include/Config.h`):
```cpp
#pragma once

#include <string>
#include <vector>

struct Config {
    // Colors (tab)
    std::string tabForeground   = "black";
    ...
    // Commands
    std::string newWindowCommand = "xterm";
    bool execUsingShell = false;
    ...
};
```
Copy this shape for `AppEntry`: a plain `#pragma once` header, `std::string`/`bool` members with inline default initializers, no methods beyond simple accessors, no X11 includes. RESEARCH.md's suggested shape (`name`, `exec`, `icon`, `category`, `Source` enum) should follow the same "plain data + inline defaults" convention as `Config`.

---

### `include/DesktopEntry.h` / `src/DesktopEntry.cpp` (service/parser, file-I/O + transform)

**Analog:** `include/Config.h` + `src/Config.cpp`

**Imports pattern** (`src/Config.cpp` lines 1-15):
```cpp
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "Config.h"

#include <getopt.h>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>
```
`DesktopEntry.cpp` needs no `getopt.h` (no CLI parsing) but should keep the same `<fstream>`/`<sstream>`/`<cctype>` set for line-based parsing.

**File-parsing loop pattern** (`src/Config.cpp` lines 84-130, `Config::applyFile`):
```cpp
void Config::applyFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;  // File doesn't exist -- skip silently

    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        ++lineNum;
        if (line.size() > 4096) {
            std::fprintf(stderr, "wm2: warning: config line %d: line too long, skipping\n", lineNum);
            continue;
        }
        auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;  // blank line
        if (line[start] == '#') continue;           // comment
        auto end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            std::fprintf(stderr, "wm2: warning: config line %d: missing '='\n", lineNum);
            continue;
        }
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        trim(key); trim(value);
        if (value.size() > 256) { /* warn, skip */ continue; }
        applyKeyValue(key, value);
    }
}
```
Copy this structure directly for `.desktop` `[Desktop Entry]` section parsing: line-by-line, trim, `key=value` split on first `=`, warn-and-skip-not-abort on malformed lines. Add section-header handling (`[Desktop Entry]` / `[Desktop Action ...]`) as a new branch (line starts with `[`) alongside the existing comment (`#`) and blank-line skips. Bound checks (line length, value length) should be retained for defense-in-depth given this parses externally-supplied files.

**Key dispatch pattern** (`src/Config.cpp` lines 136-188, `Config::applyKeyValue`):
```cpp
void Config::applyKeyValue(const std::string& key, const std::string& value) {
    if (key == "tab-foreground")      { tabForeground = value; return; }
    ...
    if (key == "click-to-focus")      { clickToFocus = parseBool(value); return; }
    ...
    // Unknown key -- warn but don't abort
    std::fprintf(stderr, "wm2: warning: unknown config key '%s'\n", key.c_str());
}
```
Use the same `if (key == "...") { field = value; return; }` chain for `Name=`, `Exec=`, `Icon=`, `Categories=`, `NoDisplay=`, `Hidden=`, `OnlyShowIn=`, `NotShowIn=`, `TryExec=`. Reuse the local `parseBool()` helper (`src/Config.cpp:42-45`) for `NoDisplay`/`Hidden`. **Deviation required per RESEARCH.md Pitfall 1/2:** unlike `Config`, `Exec=` must NOT be stored as a raw string passed to a shell — tokenize into argv and strip field codes (`%f %F %u %U` etc.) before storing, and reject the whole entry if an unrecognized field code appears.

**Error handling pattern:** `fprintf(stderr, "wm2: warning: ...")` — never fatal, never throws — matches `WindowManager::fatal()` convention of only using `fatal()` for unrecoverable init-time failures; parse errors in a single `.desktop` file are recoverable (skip that entry, continue scanning others).

---

### `include/BinaryScanner.h` / `src/BinaryScanner.cpp` (service/scanner, file-I/O + transform)

**Analog:** `src/Config.cpp` (XDG dir enumeration) — no direct ELF-parsing precedent exists in this codebase.

**Directory enumeration pattern to reuse** (`src/Config.cpp` lines 60-78, `xdgConfigDirs()`):
```cpp
std::vector<std::string> xdgConfigDirs() {
    const char* dirs = std::getenv("XDG_CONFIG_DIRS");
    std::vector<std::string> result;
    if (dirs && dirs[0] != '\0') {
        std::istringstream ss(dirs);
        std::string dir;
        while (std::getline(ss, dir, ':')) {
            if (!dir.empty() && dir[0] == '/') {
                result.push_back(dir);
            }
        }
    }
    if (result.empty()) {
        result.push_back("/etc/xdg");
    }
    return result;
}
```
Follow this exact colon-split-with-absolute-path-filter shape for a new `xdgDataDirs()`/`xdgApplicationsDirs()` helper (used by both `DesktopEntry` scanning and cache invalidation mtime checks), per RESEARCH.md's "Don't Hand-Roll" table.

**No existing ELF/mmap analog in this codebase** — this is genuinely novel logic (RESEARCH.md Pattern 3). Use `open()` + `fstat()` (not `stat()` then separate `open()`, per RESEARCH.md's TOCTOU note) + `mmap()`, validate ELF magic bytes before any struct casts, and follow the project's existing error-handling convention of `fprintf(stderr, "wm2: warning: ...")` + continue (skip the file) rather than `fatal()` — a single malformed/corrupt binary must never abort the whole `/usr/bin` scan.

---

### `include/AppCache.h` / `src/AppCache.cpp` (service/cache, file-I/O + CRUD)

**Analog:** `src/Config.cpp` (`Config::load` layered read + `applyFile` read pattern)

**Layered load pattern** (`src/Config.cpp` lines 194-209, `Config::load`):
```cpp
Config Config::load(int argc, char** argv) {
    Config cfg;  // Start with built-in defaults
    for (const auto& dir : xdgConfigDirs()) {
        cfg.applyFile(dir + "/wm2-born-again/config");
    }
    cfg.applyFile(xdgConfigHome() + "/wm2-born-again/config");
    cfg.applyCliArgs(argc, argv);
    return cfg;
}
```
Model `AppCache::loadOrRescan()` the same way: start from defaults (empty list), attempt to load `~/.config/wm2-born-again/appcache.json`, fall back to a fresh rescan if missing/stale/unparseable — mirrors "skip silently if file doesn't exist" from `applyFile` (`src/Config.cpp:86`).

**Serialization format:** follow the same hand-rolled key=value-per-line philosophy as `Config` rather than introducing a JSON library (RESEARCH.md's recommended path) — reuse `trim()` and line-based parsing exactly as in `DesktopEntry`/`Config`.

---

### `src/Config.cpp` extended (manual menu entries, config/model, CRUD)

**Analog:** itself — `Config::applyKeyValue` (`src/Config.cpp:136-188`)

Add new keys (`menu-entry-name=`, `menu-entry-command=`, `menu-entry-category=`) using the exact same `if (key == "...") { ...; return; }` chain shown above. Since a single manual entry spans **three** keys (name/command/category), this needs a small deviation from the existing 1-key-1-field mapping — RESEARCH.md leaves exact key naming to planner discretion; recommend an indexed or "current entry accumulator" pattern (e.g., `menu-entry-name=` starts a new pending entry, subsequent `menu-entry-command=`/`menu-entry-category=` fill in the most recently started entry) to keep it declarative in the config file without needing numbered keys like `menu-entry-1-name=`.

**Struct addition:** add a `std::vector<AppEntry> manualMenuEntries;` member to `Config` following the existing member-list style in `include/Config.h:6-31` (grouped by comment banner, e.g. `// Manual menu entries`).

---

### `src/Buttons.cpp` extended `menu()` (controller, request-response / X11 event-driven)

**Analog:** itself — `WindowManager::menu()` (`src/Buttons.cpp:104-314`)

**Core popup lifecycle pattern to replicate for a submenu** (lines 164-180):
```cpp
XMoveResizeWindow(display(), m_menuWindow, x, y, maxWidth, totalHeight);
XSelectInput(display(), m_menuWindow, MenuMask);
XMapRaised(display(), m_menuWindow);

if (!m_menuDraw) {
    m_menuDraw = x11::XftDrawPtr(XftDrawCreate(display(), m_menuWindow,
        DefaultVisual(display(), m_screenNumber),
        DefaultColormap(display(), m_screenNumber)));
} else {
    XftDrawChange(m_menuDraw.get(), m_menuWindow);
}

if (attemptGrab(m_menuWindow, None, MenuGrabMask, e->time) != GrabSuccess) {
    XUnmapWindow(display(), m_menuWindow);
    return;
}
```
Per RESEARCH.md's Open Question #1 recommendation: instantiate this **twice** (a second `Window`/`XftDraw`/color-set for the submenu, e.g. `m_submenuWindow`) rather than rewriting `menu()` into a stateful redraw model — this is the path of least structural change to the existing grab/ungrab/event loop.

**Label-sizing loop pattern** (lines 130-140) and **event-switch pattern** (lines 186-297, the `ButtonPress`/`ButtonRelease`/`MotionNotify`/`Expose` cases) should be copied near-verbatim for the submenu's own click-to-select loop, parameterized over the category's app list instead of `clients`.

**Dispatch/selection pattern** (lines 299-313):
```cpp
if (selecting == n - 1 && allowExit) {
    m_signalled = 1;
    return;
}
if (selecting >= 0) {
    if (selecting == 0) {
        spawn();
    } else if (selecting < nh) {
        clients[selecting - 1]->unhide(true);
    } else if (selecting < n) {
        clients[selecting - 1]->mapRaised();
        clients[selecting - 1]->ensureVisible();
    }
}
```
New category rows (appended after the hidden-clients block, before/instead of the exit-right-edge entry) should dispatch into the submenu-open path instead of `spawn()`/`unhide()`; app rows inside the submenu dispatch to the new `spawnArgv()` (see below).

**Error handling:** unknown/default event types use `fprintf(stderr, "wm2: unknown event type %d\n", event.type)` (line 192) — keep this catch-all default case in any new event switch.

---

### `src/Manager.cpp` new `spawnArgv()` (service, event-driven fork/exec)

**Analog:** itself — `WindowManager::spawn()` (`src/Manager.cpp:770-800`)

**Double-fork exec pattern** (lines 770-800):
```cpp
void WindowManager::spawn()
{
    // Double-fork to avoid zombies (from 9wm)
    char *displayName = DisplayString(display());

    if (fork() == 0) {
        if (fork() == 0) {
            close(ConnectionNumber(display()));
            if (displayName && displayName[0] != '\0') {
                setenv("DISPLAY", displayName, 1);
            }
            if (m_config.execUsingShell) {
                execl("/bin/sh", "sh", "-c", m_config.newWindowCommand.c_str(),
                      static_cast<char*>(nullptr));
            } else {
                execlp(m_config.newWindowCommand.c_str(),
                       m_config.newWindowCommand.c_str(),
                       static_cast<char*>(nullptr));
            }
            std::fprintf(stderr, "wm2: exec %s failed", m_config.newWindowCommand.c_str());
            perror(" ");
            std::exit(1);
        }
        std::exit(0);
    }

    int status;
    wait(&status);
}
```
Generalize into `spawnArgv(const std::vector<std::string>& argv)`: same double-fork/close-connection/setenv-DISPLAY structure, but call `execvp(argv[0].c_str(), argvPointers.data())` instead of the hardcoded `newWindowCommand`. Per RESEARCH.md Pitfall 2, **do not** route `.desktop`/auto-discovered `Exec=` through the `execUsingShell`/`sh -c` branch — that branch should remain reachable only for manual config entries that explicitly opt in via the existing `execUsingShell` flag. Keep the same `fprintf(stderr, "wm2: exec %s failed", ...)` + `perror(" ")` + `exit(1)` error convention.

**Stub to replace:** `WindowManager::menuLabel(int i)` (lines 803-807) is currently an unused stub (`// Stub -- Buttons.cpp will use a different approach`) — either remove it or repurpose it for category/app label lookup if the planner finds it useful; do not assume it needs to be wired up, `Buttons.cpp` already resolves labels via its own local lambda (`menuLabelFn`, `src/Buttons.cpp:124-128`).

---

### `src/main.cpp` extended (wiring, request-response startup sequencing)

**Analog:** itself — current `main()` (`src/main.cpp:5-9`)

```cpp
int main(int argc, char** argv) {
    Config config = Config::load(argc, argv);
    WindowManager manager(config);
    return 0;
}
```
Per RESEARCH.md Open Question #2 recommendation, insert the app-scan/cache-load step between `Config::load()` and `WindowManager` construction:
```cpp
int main(int argc, char** argv) {
    Config config = Config::load(argc, argv);
    std::vector<AppEntry> apps = AppCache::loadOrRescan(appCachePath(), config);
    WindowManager manager(config, apps);   // or manager.setApps(apps) post-construction
    return 0;
}
```
Exact signature/wiring (constructor param vs. setter) is planner's call; keep `main.cpp` a thin sequencing script as it is today — no logic beyond ordering these three calls.

---

### `tests/test_desktopentry.cpp`, `tests/test_appcache.cpp` (test, transform / CRUD)

**Analog:** `tests/test_config.cpp`

**Temp-file fixture pattern** (lines 14-27):
```cpp
static std::string writeTempConfig(const std::string& content) {
    static int counter = 0;
    std::string path = "/tmp/wm2-test-config-" + std::to_string(++counter) + ".cfg";
    std::ofstream out(path);
    out << content;
    out.close();
    return path;
}

static void removeTempFile(const std::string& path) {
    std::remove(path.c_str());
}
```
Copy this exact fixture-writer/cleanup pair (rename `wm2-test-config-` prefix to `wm2-test-desktopentry-`/`wm2-test-appcache-`) for writing synthetic `.desktop` files and cache JSON fixtures. Test-case naming convention: `TEST_CASE("<description>", "[desktopentry]")` mirrors `TEST_CASE("Config defaults match upstream Config.h", "[config]")` (line 32) — use tag `[desktopentry]`/`[appcache]`/`[binaryscanner]` respectively so `ctest -R desktopentry` etc. (per RESEARCH.md's validation architecture) resolves correctly.

**CMakeLists.txt registration pattern** (`CMakeLists.txt:180-188`):
```cmake
# Config parsing unit tests (pure data transformation, no X11 needed)
add_executable(test_config tests/test_config.cpp src/Config.cpp)
target_link_libraries(test_config PRIVATE
    Catch2::Catch2WithMain
)
target_include_directories(test_config PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
catch_discover_tests(test_config)
```
Add three analogous blocks for `test_desktopentry`, `test_binaryscanner`, `test_appcache` — no `X11`/`Xft` link libraries needed (matches the comment "pure data transformation, no X11 needed"), same `target_include_directories`/`catch_discover_tests` shape.

## Shared Patterns

### Warn-and-continue error handling (never `fatal()` for per-entry/per-file parse errors)
**Source:** `src/Config.cpp:95, 111-112, 124-125, 187` (all `fprintf(stderr, "wm2: warning: ...")` then `continue`/`return`, never `exit()`)
**Apply to:** `DesktopEntry`, `BinaryScanner`, `AppCache` — any malformed `.desktop` file, unreadable binary, or corrupt cache entry must be skipped with a warning, never abort the whole scan/menu build. Reserve `WindowManager::fatal()` (`src/Manager.cpp`, referenced in ARCHITECTURE.md) strictly for unrecoverable init-time conditions (display open failure, missing font) as it already is — app discovery failures are always recoverable.

### `wm2: ` message prefix convention
**Source:** project-wide convention (CLAUDE.md "Logging" section; observed throughout `src/Config.cpp`, `src/Buttons.cpp:192`)
**Apply to:** every new `fprintf(stderr, ...)` call in `DesktopEntry.cpp`, `BinaryScanner.cpp`, `AppCache.cpp` must use the `"wm2: "` / `"wm2: warning: "` prefix.

### X11-free data-class separation (Config vs WindowManager split)
**Source:** `include/Config.h` (no `<X11/*>` includes) vs `include/Manager.h`/`src/Buttons.cpp` (X11-heavy)
**Apply to:** `AppEntry`, `DesktopEntry`, `BinaryScanner`, `AppCache` must have zero `Display*`/`Window`/Xlib includes — only `src/Buttons.cpp` (menu rendering) and `src/Manager.cpp` (`spawnArgv`, process control) touch X11/process state. This is the single most important structural pattern for this phase per RESEARCH.md's "Architectural Responsibility Map."

### Double-fork process spawning
**Source:** `src/Manager.cpp:770-800` (`WindowManager::spawn()`)
**Apply to:** `spawnArgv()` — do not write a second independent process-spawning mechanism; generalize the existing one (per RESEARCH.md's "Don't Hand-Roll" table).

### XDG directory colon-split enumeration
**Source:** `src/Config.cpp:60-78` (`xdgConfigDirs()`)
**Apply to:** new `xdgDataDirs()`/`xdgApplicationsDirs()` helpers in `DesktopEntry.cpp` or a shared XDG utility — same colon-split, absolute-path-filter, fallback-default pattern.

### Catch2 test structure
**Source:** `tests/test_config.cpp` (temp-file fixtures, `TEST_CASE(..., "[tag]")`, `CMakeLists.txt:180-188` registration block)
**Apply to:** all three new test files (`test_desktopentry.cpp`, `test_binaryscanner.cpp`, `test_appcache.cpp`).

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| ELF `.dynamic`-section reader logic inside `src/BinaryScanner.cpp` | service (low-level binary parsing) | transform | No existing code in this codebase reads binary file formats (mmap + struct-cast parsing) — closest available precedent is only the XDG-dir-enumeration *style* (warn-and-continue, no X11), not the ELF parsing technique itself. RESEARCH.md's Pattern 3 and Code Examples section (man7.org elf(5) struct layout) should be used as the primary reference instead of an in-repo analog. |

## Metadata

**Analog search scope:** `src/`, `include/`, `tests/`, `CMakeLists.txt` (current C++17 codebase — `upstream-wm2/` intentionally excluded per phase instructions since it reflects the pre-modernization tree)
**Files scanned:** `src/Config.cpp`, `include/Config.h`, `src/Buttons.cpp`, `src/Manager.cpp`, `include/Manager.h`, `src/main.cpp`, `tests/test_config.cpp`, `CMakeLists.txt`
**Pattern extraction date:** 2026-07-08
