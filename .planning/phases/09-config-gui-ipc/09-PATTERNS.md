# Phase 9: Config GUI + IPC - Pattern Map

**Mapped:** 2026-09-06
**Files analyzed:** 19 (new: 12, modified: 7 core + docs/gates)
**Analogs found:** 17 / 19

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|--------------------|------|-----------|-----------------|---------------|
| `include/ConfigProtocol.h` (NDJSON codec, message types) | utility (header-only, display-free) | transform (encode/decode) | `include/MenuPaint.h` | exact (shape: header-only, X11-free-ish logic pulled out for Catch2, no production `.cpp`) |
| `include/SocketServer.h` (pollfd-vector abstraction) | utility (header-only, display-free) | event-driven (poll-driven descriptor set) | `include/EventPump.h` + `include/TimestampWait.h` | exact (both extract a `poll()`-driven decision out of the event loop into a header a test can link without X11 or a display) |
| `src/SocketServer.cpp` (accept/SO_PEERCRED/bind/cleanup, needs libc not X11) | service | event-driven | `src/DesktopEntry.cpp` (`findOnPath`, `xdgDataHome`) | role-match (POSIX-only helper functions with no X11 dependency, compiled as its own translation unit and linked into multiple targets) |
| `include/ConfigFileWriter.h` / `src/ConfigFileWriter.cpp` (surgical edit, D-02) | utility / file-I/O | file-I/O (read-modify-write, atomic) | `src/Config.cpp` `Config::applyFile()` (reader half) — no writer analog exists | role-match for the *read* shape; **no analog for the write half** — see "No Analog Found" |
| `apps/wm2-ctl/main.cpp` (dependency-free CLI client) | controller (CLI entry point) | request-response (one round trip over the socket) | `src/Main.cpp` (entry-point-only pattern) + `tests/support/WmFixture.h`'s process-spawn idiom | role-match |
| `apps/wm2-config/main.cpp` (GTK entry point) | controller / provider (GtkApplication owner) | request-response + event-driven (GLib main loop) | none in this codebase (first GTK surface) | no analog — see below |
| `apps/wm2-config/AppearancePage.cpp/.h` | component | request-response (widget -> protocol client -> Config) | none (first GTK surface); nearest logical analog is `Config` struct's field-by-field key mapping in `Config::applyKeyValue()` | partial (field enumeration shape only) |
| `apps/wm2-config/BehaviourPage.cpp/.h` | component | request-response | same as AppearancePage | partial |
| `apps/wm2-config/MenuPage.cpp/.h` | component | CRUD (add/edit/remove rows) | `Config.cpp`'s `menu-entry-name`/`-command`/`-category` accumulator (`src/Config.cpp:204-239`) | role-match (same three-field row shape, different tier) |
| `apps/wm2-config/ProtocolClient.cpp/.h` | service (GLib-integrated socket client) | event-driven | `include/SocketServer.h`/`ConfigProtocol.h` (shared codec, consumed here instead of served) | role-match |
| `tests/test_config_protocol.cpp` | test | request-response | `tests/test_config.cpp` (Catch2, no Xvfb, direct `Config`/parser unit tests) | exact |
| `tests/test_config_writer.cpp` | test | file-I/O | `tests/test_config.cpp` (`applyFile` cases, `std::filesystem` temp-dir fixtures at lines ~271-342) + `tests/test_menupaint.cpp` (display-free header test pattern) | exact |
| `tests/test_config_live_apply.cpp` | test | event-driven (real WM process) | `tests/test_wm_focus.cpp` / `tests/test_wm_resource.cpp` (both build on `WmFixture`, start real WM, assert on live X11 state) | exact |
| `tests/test_wm2_config_smoke.cpp` | test | event-driven (GTK smoke, Xvfb) | `tests/test_wm_process.cpp` (Xvfb-based process smoke test) for the *process* shape; no GTK-specific analog exists | role-match |
| `include/Config.h` / `src/Config.cpp` (MODIFIED: add `tab-font`, `menu-font` keys) | model / config | CRUD (key=value parser) | itself — extend `applyKeyValue()`'s existing string-setting block (`src/Config.cpp:150-161`) | exact (same file, established pattern) |
| `src/Manager.cpp` (MODIFIED: socket listen fd, root-window property publish, applyConfig diff/reapply) | controller (WM lifecycle) | event-driven | `src/Manager.cpp`'s own `_NET_SUPPORTING_WM_CHECK` property-set site (~line 798-806) and `Atoms` static table (`src/Manager.cpp:22-43`) | exact (extend an established sibling pattern in the same file) |
| `src/Events.cpp` (MODIFIED: `nextEvent()`/`modalWait()` fixed `fds[2]` -> shared growable set) | controller (event loop) | event-driven | `src/Events.cpp:185-272` (`nextEvent`) and `:275-...` (`modalWait`) — modify in place | exact (same file, same functions, structural change per Research Pattern 2) |
| `src/Border.cpp` (MODIFIED: `loadTabFont()` gains a config-driven font string, live-reload path) | component | transform (font reload + relayout) | `src/Border.cpp`'s existing `loadTabFont()` | exact (same file, extend the existing method) |
| `src/Buttons.cpp` (MODIFIED: "Configure..." top-level menu entry) | controller | request-response (menu build) | `src/Buttons.cpp::menu()` (`outerLabel` lambda, `allowExit` flat top-level entry pattern, lines 231-297) | exact |
| `CMakeLists.txt` (MODIFIED: `BUILD_CONFIG_GUI` option, `wm2-ctl`/`wm2-config` targets, two install components) | config | batch (build config) | `CMakeLists.txt` `BUILD_TESTS` option (line 70) for the AUTO-option shape; `add_executable(test_config tests/test_config.cpp src/Config.cpp)` (line 779) for the "compile Config.cpp directly into a small consumer" shape | exact |
| `scripts/preflight.sh` (MODIFIED: optional `gtk+-3.0` probe) | config / script | batch | `scripts/preflight.sh:69-75` (the `for mod in x11 xext xft ...` required-module loop) | exact (same file, add an optional variant) |
| `docs/RELEASE-NOTES.md`, `COMPILED_CODE_BEHAVIOR_CHECKLIST.md` (MODIFIED) | config / docs | batch | existing key/behaviour rows for prior phases (e.g. Phase 7/8 entries) | role-match |

## Pattern Assignments

### `include/ConfigProtocol.h` (utility, transform) and `include/SocketServer.h` (utility, event-driven)

**Analog:** `include/MenuPaint.h` (codec/decision shape), `include/EventPump.h` + `include/TimestampWait.h` (poll-driven, header-only, testable-without-X11 shape)

**File-header pattern to copy verbatim in spirit** (`include/MenuPaint.h:1-14`):
```cpp
#pragma once

// The root menu's Expose-to-paint decision, lifted out of the modal loop in
// WindowManager::menu() so that a test can reach it.
//
// This header is deliberately free of any project header. test_menupaint links
// Catch2 and X11 only, with include/ on the header path and no production
// objects, so anything reachable from there must depend on nothing but Xlib and
// the C library. That constraint is the whole reason this decision lives in a
// header rather than inside the loop body.
```
For `ConfigProtocol.h`/`SocketServer.h`, adapt this to: "deliberately free of any GTK or X11 header — depends on nothing but the C++ standard library and POSIX sockets, so `wm2-ctl`, the WM and a Catch2 test can all link it with no display and no GTK."

**Enum-for-named-fallthrough pattern** (`include/MenuPaint.h:26-31`):
```cpp
enum class MenuPaintTarget {
    NoTarget,   // the event carries no window at all
    Outer,      // the outer root menu
    Submenu,    // the category flyout, and only while a category is open
    Foreign     // some other window -- see the RECORDED DEFECT note below
};
```
Use the identical shape for the protocol's message-type / decode-result enum (e.g. `ConfigMessageType { Hello, HelloAck, Get, Set, Ack, Error, Reload, Status }`) and for `SocketServer`'s per-fd dispatch decision (mirrors `EventPumpAction` below) — every branch a caller must handle should be a named enumerator, never a silent `else`-less fallthrough, per the house rule this header documents.

**Poll-driven wait/decision pattern to copy** (`include/EventPump.h:50-127`, `include/TimestampWait.h:183-242`):
```cpp
enum class EventPumpAction {
    DeliverEvent, Block, Retry, ServiceFocusTick, StopOnSignal, StopOnError
};
struct EventPumpPollResult {
    int   pollResult    = 0;
    int   pollErrno     = 0;
    short xRevents      = 0;
    short pipeRevents   = 0;
    bool  exitFlagSet   = false;
    bool  focusChanging = false;
};
inline EventPumpAction eventPumpDecide(const EventPumpPollResult &s) { ... }
```
`SocketServer.h` should expose an analogous pure function taking "everything the post-poll decision is allowed to look at" as a plain struct (listen-fd revents, each client-fd's revents, a byte-length/parse-error signal) and returning a named action (`Accept`, `ReadClient`, `CloseClient`, `Idle`) — this is exactly the shape `eventPumpDecide()` already establishes and the shape `WindowManager::nextEvent()`/`modalWait()` will call into.

**Bounded, deadline-based read pattern** (`include/TimestampWait.h:183-242`, esp. the `poll()` loop lines 206-238): use `steady_clock`, a `pollfd`, a monotonic deadline, and non-blocking `recv()` (analogous to `XCheckIfEvent`) for reading a single NDJSON line from a client socket — never a blocking `read()` on the WM side (explicit anti-pattern named in RESEARCH.md).

### `src/Events.cpp` — extending `nextEvent()`/`modalWait()` from fixed `fds[2]` to a shared descriptor set

**Analog:** the file's own two near-identical functions.

**Current fixed-size pattern (both sites, to be replaced)** (`src/Events.cpp:187-191` and `:284-288`):
```cpp
struct pollfd fds[2];
fds[0].fd = ConnectionNumber(display());
fds[0].events = POLLIN;
fds[1].fd = m_pipeRead.get();
fds[1].events = POLLIN;
```
**Dispatch pattern already established for a third case** (`src/Events.cpp:230-270`), the model for adding the socket server as a **fourth, silent case** exactly as `ServiceFocusTick` is handled today:
```cpp
EventPumpPollResult state;
state.pollResult    = r;
state.pollErrno     = pollErrno;
state.xRevents      = fds[0].revents;
state.pipeRevents   = fds[1].revents;
state.exitFlagSet   = (m_signalled != 0);
state.focusChanging = m_focusChanging;

switch (eventPumpDecide(state)) {
case EventPumpAction::StopOnError: ...
case EventPumpAction::StopOnSignal: shutdownOnSignal(); return;
case EventPumpAction::ServiceFocusTick: checkDelaysForFocus(); continue;
case EventPumpAction::DeliverEvent:
case EventPumpAction::Block:
case EventPumpAction::Retry: continue;
}
```
Both `nextEvent()` and `modalWait()` must be edited in the same commit — RESEARCH.md's Pitfall 1 names this exact file's two near-duplicate functions as the risk. `modalWait()`'s own poll loop (`src/Events.cpp:290-314`) shows the second site to change identically.

### `src/Config.h` / `src/Config.cpp` — adding `tab-font`/`menu-font` keys

**Analog:** the file's own existing string-setting block.

**Pattern to copy exactly** (`src/Config.cpp:150-161`):
```cpp
// String settings (colors)
if (key == "tab-foreground")      { tabForeground = value; return; }
if (key == "tab-background")      { tabBackground = value; return; }
...
// String settings (commands)
if (key == "new-window-command")  { newWindowCommand = value; return; }
```
Add `tab-font`/`menu-font` as new string settings in the same block, with defaults set in `include/Config.h` (`std::string tabFont = "Ubuntu,Noto Sans,DejaVu Sans,Sans:bold:size=12";` mirroring the current hardcoded literal at `src/Border.cpp:223`, and the menu equivalent mirroring `src/Manager.cpp:684-690`). No new parsing machinery needed — this is a one-line-per-key extension of the existing linear `if (key == ...)` chain, matching the project's own convention (no dispatch table, no map).

**Repeated-group accumulator pattern to model the Menu page's rows on** (`src/Config.cpp:204-239`):
```cpp
if (key == "menu-entry-name") {
    AppEntry entry;
    entry.name = value;
    entry.category = "Custom";
    entry.source = AppEntry::Source::Manual;
    manualMenuEntries.push_back(entry);
    return;
}
if (key == "menu-entry-command") {
    if (manualMenuEntries.empty()) {
        std::fprintf(stderr, "wm2: warning: menu-entry-command with no preceding menu-entry-name\n");
        return;
    }
    ...
    manualMenuEntries.back().execArgv = tokens;
    return;
}
```
`ConfigFileWriter`'s surgical rewrite of `menu-entry-*` groups (D-02) must reproduce this exact three-key, "name opens a new entry" grouping rule when it serializes the GUI's row list back to text, and `MenuPage`'s Add/Edit/Remove rows map one-to-one onto this same triple.

### `include/ConfigFileWriter.h` / `src/ConfigFileWriter.cpp` — surgical edit (D-02)

**Analog (read-side only):** `Config::applyFile()` (`src/Config.cpp:84-137`).

**Line-classification pattern to reuse for identifying "keys the GUI manages" vs. pass-through lines:**
```cpp
auto start = line.find_first_not_of(" \t");
if (start == std::string::npos) continue;  // blank line
if (line[start] == '#') continue;           // comment
...
auto eq = line.find('=');
if (eq == std::string::npos) { ...; continue; }
std::string key = line.substr(0, eq);
std::string value = line.substr(eq + 1);
trim(key); trim(value);
```
`trim()` (`src/Config.cpp:16-25`) and `toLower()` (`:27-32`) are file-local statics — the writer should either link `Config.cpp` for these or duplicate the two four-line helpers (project convention: small static helpers per translation unit, not a shared string-utils header). **No existing code writes a config file** — the writer's temp-file+rename atomic-write logic and its "replace-in-place vs. append vs. leave untouched" line classification are new engineering with no direct analog in this codebase; base the atomicity approach on POSIX `rename()` semantics (already implicitly relied on nowhere in this codebase, so this is genuinely new ground — flag it to the planner as Wave-0 design work, not a copy job).

### `apps/wm2-ctl/main.cpp` — dependency-free CLI client

**Analog:** `src/Main.cpp` (entry-point-only shape) and the CMake "compile `Config.cpp` directly into a small consumer" precedent.

**CMake target pattern to copy** (from RESEARCH.md's own Code Examples, matching `CMakeLists.txt:779`'s `test_config` shape):
```cmake
add_executable(test_config tests/test_config.cpp src/Config.cpp)
```
becomes:
```cmake
add_executable(wm2-ctl apps/wm2-ctl/main.cpp src/Config.cpp src/ConfigProtocol.cpp src/SocketServer.cpp)
target_include_directories(wm2-ctl PRIVATE ${CMAKE_SOURCE_DIR}/include)
# No X11/GTK linkage at all -- wm2-ctl is a pure socket client (D-17).
```

### `CMakeLists.txt` — `BUILD_CONFIG_GUI` AUTO option and two install components

**Analog:** `option(BUILD_TESTS "Build test suite" ON)` at `CMakeLists.txt:70`, and the `pkg_check_modules(... REQUIRED ...)` calls at lines 17-36 (for the *pattern*, inverted to non-`REQUIRED` + explicit AUTO logic).

**Pattern to copy (required-module shape, to be adapted to optional/AUTO):**
```cmake
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
```
New code follows the same `pkg_check_modules(... IMPORTED_TARGET ...)` call shape but omits `REQUIRED`, checks `GTK3_FOUND`, and gates `add_executable(wm2-config ...)` behind it — this is new CMake surface (no `install()` rules exist anywhere yet, confirmed by RESEARCH.md's own grep), so there is no direct install()-component analog in this repo; base the two-component split on CMake's standard `install(... COMPONENT wm)` / `install(... COMPONENT config-gui)` idiom (external, not house pattern, since none exists here).

### `scripts/preflight.sh` — optional `gtk+-3.0` probe

**Analog:** the existing required-module loop (`scripts/preflight.sh:69-75`):
```bash
for mod in x11 xext xft fontconfig xrandr xrender; do
    if pkg-config --exists "$mod" 2>/dev/null; then
        info "  $(printf '%-12s' "$mod") $(pkg-config --modversion "$mod")"
    else
        fail "pkg-config module missing: $mod (install the matching -dev package)"
    fi
done
```
Add a second loop for optional modules that uses `info`/a distinct non-`fail` warning path instead of `fail` on absence, matching D-18's "configure prints one clear line" requirement — e.g. `for mod in gtk+-3.0; do ... else info "  $mod not found -- wm2-config will not be built"; fi`.

### `src/Buttons.cpp` — "Configure..." top-level menu entry

**Analog:** `WindowManager::menu()`'s existing flat top-level entry pattern (`src/Buttons.cpp:231-297`, specifically the `allowExit` flag and `outerLabel` lambda).

**Pattern to copy:**
```cpp
const bool allowExit = ((e->x > mx - 3) && (e->y > my - 3));
if (allowExit) n += 1;

auto outerLabel = [&](int idx) -> const char* {
    if (idx == 0) return m_menuCreateLabel;
    if (idx < nh) return clients[idx - 1]->label().c_str();
    if (idx < nh + numCategories) return m_appCategories[idx - nh].first.c_str();
    if (allowExit && idx == n - 1) return "[Exit wm2]";
    return clients[idx - 1]->label().c_str();
};
```
A "Configure..." entry (D-11) is a `bool m_configureAvailable` computed once at WM startup via `findOnPath("wm2-config")` (see `DesktopEntry.cpp` pattern below), added as one more conditional slot in the same `n +=` / `outerLabel` lambda chain, alongside `allowExit`.

### Startup gate for "Configure..." — `findOnPath()`

**Analog:** `bool findOnPath(const std::string& bin)` (`src/DesktopEntry.cpp:53-68`):
```cpp
bool findOnPath(const std::string& bin) {
    if (bin.empty()) return false;
    if (bin.find('/') != std::string::npos) {
        return access(bin.c_str(), X_OK) == 0;
    }
    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv || pathEnv[0] == '\0') return false;
    for (const auto& dir : split(pathEnv, ':')) {
        if (dir.empty()) continue;
        std::string candidate = dir + "/" + bin;
        if (access(candidate.c_str(), X_OK) == 0) return true;
    }
    return false;
}
```
This function already exists and is reusable as-is (it is namespace-free at file scope, not `DesktopEntry::`-qualified per the grep) — the WM startup path should call it once with `"wm2-config"` to compute `m_configureAvailable`, exactly the same call shape `.desktop`'s `TryExec` handling already uses at `src/DesktopEntry.cpp:298`.

### Root-window property publish — socket path discovery

**Analog:** `_NET_SUPPORTING_WM_CHECK` property-set site and the `Atoms` static table.

**Pattern to copy** (`src/Manager.cpp:22-43` for the atom table shape; the `XChangeProperty` call cited in RESEARCH.md Pattern 4, `src/Manager.cpp:798-806`):
```cpp
Atom Atoms::net_supportingWmCheck = None;
...
XChangeProperty(display(), m_root, Atoms::net_supportingWmCheck,
                XA_WINDOW, 32, PropModeReplace,
                reinterpret_cast<unsigned char*>(&m_wmCheckWindow), 1);
```
New: add `Atom Atoms::wm2ConfigSocket = None;` to the same static table, intern it alongside the other `XInternAtom` calls in the same constructor phase, and publish the socket path immediately after the `_NET_SUPPORTING_WM_CHECK` write with the identical `XChangeProperty` call shape (substituting `XA_STRING`, format 8, and the path bytes) — this is Research Pattern 4, already fully specified.

### `tests/test_config_writer.cpp` and `tests/test_config_protocol.cpp` — Catch2, no Xvfb

**Analog:** `tests/test_config.cpp` (entire file's shape — see excerpt below) and `tests/test_menupaint.cpp` (display-free header test convention).

**Fixture/temp-file pattern to copy** (from `tests/test_config.cpp`'s `applyFile`/XDG test cases, e.g. around line 271-342 "Config load precedence"): these tests build a temp directory with `std::filesystem`, write a small config file with `std::ofstream`, call into `Config`/the parser, and assert on the resulting struct fields — `test_config_writer.cpp` should do the identical thing in reverse (write a `Config`-equivalent form-state, call `ConfigFileWriter::save()`, read the resulting file back with `std::ifstream`, and assert both on preserved untouched lines and on rewritten key lines).

**CMake registration pattern to copy exactly** (`CMakeLists.txt:779-786`):
```cmake
add_executable(test_config tests/test_config.cpp src/Config.cpp)
...
catch_discover_tests(test_config ADD_TAGS_AS_LABELS)
```
`test_config_protocol` and `test_config_writer` follow this identical `add_executable(... tests/test_X.cpp src/Y.cpp) ... catch_discover_tests(test_X ADD_TAGS_AS_LABELS)` shape, gated inside the same `if(BUILD_TESTS)` block, with no Xvfb dependency (`test_config`'s own block has none).

### `tests/test_config_live_apply.cpp` — real WM process, socket client

**Analog:** `tests/test_wm_focus.cpp` / `tests/test_wm_resource.cpp` (both consume `tests/support/WmFixture.h`).

**Fixture pattern to reuse:** `WmFixture.h` already provides `DisplayReservation` (Xvfb display lock via `flock()`), `ChildProcess` (RAII PID-based spawn/reap/shutdown, `shutdown()` at line 255), and `processCpuTicks()`/CPU-based liveness checks (lines 101-131) — all display-free helpers a socket-based live-apply test can reuse verbatim: spawn the real WM under a reserved Xvfb display exactly as the existing resource/focus tests do, then open a raw `AF_UNIX` client connection to the socket path (read from the root-window property per the new `Atoms::wm2ConfigSocket`), send a `set` message, and assert the resulting X11-visible state (frame geometry, colour pixel, font metrics) the same way `test_wm_focus.cpp` asserts on live focus state today.

## Shared Patterns

### Header-only, display-free extraction (house pattern)
**Source:** `include/MenuPaint.h`, `include/TimestampWait.h`, `include/EventPump.h`
**Apply to:** `ConfigProtocol.h`, `SocketServer.h`, `ConfigFileWriter.h`
```cpp
#pragma once
// This header is deliberately free of any project header. test_X links
// Catch2 and X11 only, with include/ on the header path and no production
// objects, so anything reachable from there must depend on nothing but
// [the C library / POSIX] and [Xlib, where genuinely needed].
```
Every new protocol/file-editor/socket-abstraction piece of logic in this phase should be `inline`, in a header, free of GTK and (for the WM-shared pieces) free of Xlib, so a Catch2 case can exercise it with no Xvfb — matching D-20's requirement that protocol and writer tests need no display.

### Named enum for every silent branch (house rule)
**Source:** `include/MenuPaint.h:19-31` (`MenuPaintTarget`, incl. `Foreign` for the previously-silent fallthrough), `include/EventPump.h:51-58` (`EventPumpAction`)
**Apply to:** `ConfigProtocol.h`'s message-type/parse-result enum, `SocketServer.h`'s per-fd dispatch decision, any WM-side "what to do with this readable socket" switch
```cpp
enum class EventPumpAction {
    DeliverEvent, Block, Retry, ServiceFocusTick, StopOnSignal, StopOnError
};
```
No `if`/`else if` chain with an unhandled tail — every outcome (including "ignore" or "not for me") gets a name, and callers `switch` over it so an unhandled case is a compile error, not a silent drop (the exact defect class ledger 8/`Foreign` closed).

### Bounded, non-blocking I/O against a monotonic deadline
**Source:** `include/TimestampWait.h:162-242` (`timestampWaitFor`), `src/Events.cpp:290-320` (`modalWait`'s poll loop)
**Apply to:** every socket read in `SocketServer.cpp`/`ProtocolClient.cpp`
```cpp
const auto waitStart = std::chrono::steady_clock::now();
...
const int pollResult = poll(&p, 1, static_cast<int>(deadlineMs - spent));
if (pollResult < 0) { if (errno == EINTR) continue; r.timedOut = true; break; }
```
Never a blocking `read()`/`recv()` on the WM side of the socket (explicit anti-pattern in RESEARCH.md); a single non-blocking `recv()` per readable notification, framed by `poll()`, exactly as the X connection is already handled.

### Linear `if (key == "...")` config-key dispatch (no map, no table)
**Source:** `src/Config.cpp:150-197` (`Config::applyKeyValue`)
**Apply to:** `tab-font`/`menu-font` key additions, and any GUI-side reverse-mapping (raw value field <-> struct field)
```cpp
if (key == "tab-foreground")      { tabForeground = value; return; }
if (key == "tab-background")      { tabBackground = value; return; }
```
The project's established convention is a flat, ordered chain of string comparisons with an early `return`, not a `std::map<std::string, ...>` dispatch table — new keys extend this chain in place.

### `wm2: warning:`-prefixed stderr diagnostics, non-fatal
**Source:** `src/Config.cpp:180-182, 192-194, 215, 234` (all `std::fprintf(stderr, "wm2: warning: ...")`, never throwing, always `return`ing early)
**Apply to:** `ConfigFileWriter`, `SocketServer` (malformed/oversized message rejection), `wm2-ctl` (bad key/value from the user)
```cpp
std::fprintf(stderr, "wm2: warning: config key '%s': invalid integer '%s'\n", key.c_str(), value.c_str());
```
Every recoverable protocol/file error is a `wm2: warning:`-prefixed stderr line and a graceful continue/return, matching project-wide convention (`CLAUDE.md` Error Handling section) — never an exception, never `exit()`, for anything short of the fatal cases `WindowManager::fatal()` already owns.

### XDG path resolution (env var, one documented fallback)
**Source:** `src/Config.cpp:51-77` (`xdgConfigHome()`, `xdgConfigDirs()`)
**Apply to:** the socket directory resolution under `$XDG_RUNTIME_DIR` (D-16)
```cpp
std::string xdgConfigHome() {
    const char* home = std::getenv("XDG_CONFIG_HOME");
    if (home && home[0] == '/') return home;
    const char* userHome = std::getenv("HOME");
    if (!userHome) userHome = "/tmp";
    return std::string(userHome) + "/.config";
}
```
The socket path resolver should read `$XDG_RUNTIME_DIR`, require it be absolute exactly as this function does for `XDG_CONFIG_HOME`, and fall back to exactly one documented alternative (`/tmp/wm2-born-again-<uid>`, per D-16) — no chasing of every possible convention, matching RESEARCH.md's "Don't Hand-Roll" table entry for XDG resolution.

## No Analog Found

Files with no close match in the codebase (planner should use RESEARCH.md patterns instead):

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| `src/ConfigFileWriter.cpp` (write half only) | file-I/O | file-I/O | Nothing in the codebase writes a config file today — `Config::applyFile()` is read-only; the atomic temp-file+rename write path is genuinely new engineering, not an extension of an existing pattern. RESEARCH.md's D-02/Security Domain sections are the design source instead. |
| `apps/wm2-config/main.cpp` (GtkApplication entry point) | controller | request-response + event-driven | This is the project's first GTK surface — no plain-C-GTK3-in-C++ static-callback code exists anywhere in this tree. RESEARCH.md's "Pattern 1: Static-callback GTK3 in C++" is the design source; the closest structural analog (`src/Main.cpp`'s "entry point only, 20 lines" shape) only covers the outermost wrapping, not the GTK lifecycle itself. |
| `apps/wm2-config/AppearancePage.cpp/.h`, `BehaviourPage.cpp/.h` (GTK widget assembly) | component | request-response | No prior GTK widget code exists; only the *field enumeration* (which keys map to which controls) has a codebase analog (`Config::applyKeyValue`'s key list). Widget wiring itself must follow RESEARCH.md's GTK3 patterns and D-10's native-chooser requirement. |
| `apps/wm2-config/wm2-config.desktop`, `apps/wm2-ctl`'s installed `.desktop` (if any) | config | batch | No `.desktop` files are authored by this project yet (only consumed, via `DesktopEntry.cpp`'s parser) — base these on the freedesktop.org Desktop Entry Specification directly, matching the `TryExec`/`Categories` fields `DesktopEntry.cpp` already knows how to read. |

## Metadata

**Analog search scope:** `include/`, `src/`, `tests/`, `tests/support/`, `scripts/`, `CMakeLists.txt` (entire repository excluding `upstream-wm2/` reference tree)
**Files scanned:** `include/Config.h`, `include/MenuPaint.h`, `include/TimestampWait.h`, `include/EventPump.h`, `src/Config.cpp`, `src/Events.cpp`, `src/Buttons.cpp`, `src/Manager.cpp` (grep pass), `src/Border.cpp` (grep pass), `src/DesktopEntry.cpp`, `tests/support/WmFixture.h`, `tests/test_config.cpp`, `scripts/preflight.sh`, `scripts/gates/build-all.sh` (grep pass), `CMakeLists.txt`
**Pattern extraction date:** 2026-09-06
