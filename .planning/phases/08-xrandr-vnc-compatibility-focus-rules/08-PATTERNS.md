# Phase 8: Xrandr + VNC Compatibility + Focus/Rules - Pattern Map

**Mapped:** 2026-08-11 · **Revised:** 2026-08-11 (re-synced to the 14-plan slicing)
**Files analyzed:** 27 (2 new source/header, 16 new test/script/doc, 9 modified)
**Analogs found:** 24 / 27

> Scope note: `upstream-wm2/` is reference-only and is NOT used as an analog anywhere below.
> All excerpts are from the current C++17 tree (`src/`, `include/`, `tests/`, `CMakeLists.txt`).

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `include/Rules.h` (new) | model (plain data) | transform | `include/AppEntry.h` | exact |
| `src/Rules.cpp` (new, matcher) | service | transform | `src/Config.cpp` (accumulator + hand-rolled parse) | role-match |
| `src/Config.cpp` (mod: `rule-*`, focus-stealing toggle) | config | transform | itself — `menu-entry-*` block, lines 186-226 | exact |
| `include/Config.h` (mod: rules vector, `focusStealingPrevention`, D-17 defaults) | config | transform | itself — lines 21-36 | exact |
| `include/Manager.h` (mod: `m_randrEventBase`, `screenWidth/Height()`, `lastUserInteraction()`, new Atoms) | model/header | request-response | itself — `hasShapeExtension()` line 55 / `m_shapeEvent` line 131 | exact |
| `src/Manager.cpp` (mod: RANDR query, root mask, `handleScreenGeometryChange()`, `_NET_SUPPORTED` additions) | service | event-driven | itself — Shape query lines 165-172; atoms 130-163; `_NET_SUPPORTED` 474-492 | exact |
| `src/Events.cpp` (mod: `ConfigureNotify` root case, RANDR in `default:`, `eventDestroy` UAF fix, `_NET_ACTIVE_WINDOW` arbitration) | controller | event-driven | itself — `default:` dispatch lines 115-121 | exact |
| `src/Border.cpp` (mod: 26→1 `XShapeCombineRectangles` call sites via `combineShape()`; font-load fatal → degradation ladder) | component | request-response | itself — `shapeAvailable()` line 149-152, `shapeParentRectangular()` 155-169 | exact |
| `include/Border.h` (mod: `combineShape()` decl) | component | request-response | `include/Manager.h:55` predicate style | role-match |
| `src/Client.cpp` (mod: `XGetClassHint`, rule application, `shouldFocusOnMap()`, `demandAttention()`, `ensureVisible()` accessor swap, focus-policy gating) | model | request-response | itself — `getWindowType()` 578-605, `getTransient()` 608-625, `manage()` dock path 117-128, `ensureVisible()` 840-856 | exact |
| `tests/test_rules.cpp` (new) | test | transform | `tests/test_config.cpp` | exact |
| `tests/test_wm_process.cpp` (new) | test | event-driven / process | `tests/test_ewmh.cpp` (X11 + property assertions) | role-match |
| `tests/test_wm_geometry.cpp` (new) | test | event-driven | `tests/test_ewmh.cpp` | role-match |
| `tests/test_wm_fallbacks.cpp` (new) | test | event-driven | `tests/test_ewmh.cpp` | role-match |
| `tests/test_wm_focus.cpp` (new) | test | event-driven | `tests/test_ewmh.cpp` | role-match |
| `tests/test_wm_rules.cpp` (new, 08-10) | test | event-driven / process | `tests/test_ewmh.cpp` property assertions + `tests/support/WmFixture.h` | role-match |
| `tests/test_wm_lifecycle.cpp` (new, 08-11) | test | event-driven / process | `tests/test_ewmh.cpp` + `WmFixture` | role-match |
| `tests/test_wm_state.cpp` (new, 08-12) | test | event-driven / process | `tests/test_ewmh.cpp` + `WmFixture` | role-match |
| `tests/test_wm_runtime.cpp` (new, 08-13) | test | event-driven / process | `tests/test_ewmh.cpp` + `WmFixture` | role-match |
| `tests/test_wm_resource.cpp` (new, 08-14) | test | event-driven / process | `tests/test_ewmh.cpp` + `WmFixture` | role-match |
| `scripts/capture-display-capabilities.sh` (new, 08-14) | script | batch | `scripts/preflight.sh` (created 08-01) — same shebang, `set -euo pipefail`, `wm2: ` stderr prefix, accumulate-then-report shape | role-match |
| `docs/RELEASE-NOTES.md` (new, 08-14) | doc | batch | `COMPILED_CODE_BEHAVIOR_CHECKLIST.md` — the repo's only prose signoff artifact (no `docs/` dir exists yet) | partial |
| `tests/support/WmFixture.h` (new) | test utility | process lifecycle | *(no analog — see No Analog Found)* | none |
| `tests/support/XTestDriver.h` (new) | test utility | event-driven | `include/x11wrap.h` RAII handle style | partial |
| `CMakeLists.txt` (mod: `xrandr` dep, `xtst` test-only, new test targets, extra Xvfb fixtures, preflight fixture) | config/build | batch | itself — lines 11-14, 85-102, 166-180, 183-191 | exact |
| `scripts/preflight.sh` (new) | script | batch | *(no analog — no `scripts/` dir exists)* | none |
| `scripts/analysis/*` (new) | script/config | batch | *(no analog)* | none |

## Pattern Assignments

### `include/Rules.h` (model, transform)

**Analog:** `include/AppEntry.h` (whole file, 30 lines) — the project's only "plain data struct shared between a parser and the WM" header. Copy its shape exactly: `#pragma once`, no X11 include (so `test_rules` stays display-free and links only `Config.cpp`/`Rules.cpp`), scoped `enum class` for closed vocabularies, defaulted members, and a comment stating *why* it has no X11 dependency.

```cpp
#pragma once

#include <string>
#include <vector>

// Shared data model for the application-discovery subsystem (Phase 7).
// Plain data, no X11/Xlib dependency -- mirrors include/Config.h's convention
// so it stays unit-testable without Xvfb.
struct AppEntry {
    enum class Source { Desktop, BinaryScan, Manual };
    std::string name;
    std::vector<std::string> execArgv;
    ...
    Source source = Source::Desktop;
};
```

For the window-type criterion, mirror the *fixed four-value* vocabulary already defined at `include/Client.h:24-29` (`Normal|Dock|Dialog|Notification`) — do not invent `utility`/`splash`/`toolbar` values, because `Client::getWindowType()` collapses them (`src/Client.cpp:601`).

---

### `src/Config.cpp` — `rule-*` keys (config, transform)

**Analog:** `src/Config.cpp:186-226`, the `menu-entry-*` repeated-key accumulator. This is the exact precedent D-20 names. Copy the three-part shape: group-opening key pushes a new element; every follow-on key checks `.empty()` and warns; each branch `return`s so the unknown-key warning at line 229 is not reached.

**Accumulator pattern** (lines 186-226):
```cpp
    // Manual menu entries (APPS-04): menu-entry-name= starts a new pending
    // AppEntry pushed onto manualMenuEntries; menu-entry-command= and
    // menu-entry-category= fill in manualMenuEntries.back(). This
    // accumulator pattern avoids numbered keys (menu-entry-1-name=) --
    // each menu-entry-name= opens a new "currently open entry".
    if (key == "menu-entry-name") {
        AppEntry entry;
        entry.name = value;
        entry.category = "Custom";  // D-07 default, applied at creation time
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
        return;
    }
```

**Unknown-key trap to avoid** (line 228-229) — every new `rule-*` key must be handled explicitly or each rule line warns:
```cpp
    // Unknown key -- warn but don't abort
    std::fprintf(stderr, "wm2: warning: unknown config key '%s'\n", key.c_str());
```

**Boolean + integer helpers to reuse, not re-derive** (lines 34-45):
```cpp
static int clampInt(int value, int minVal, int maxVal) { ... }
static bool parseBool(const std::string& value) {
    std::string lower = toLower(value);
    return (lower == "true" || lower == "1");
}
```

**Integer parse + warn pattern** (lines 174-184) — copy verbatim for `rule-position=X,Y` / `rule-size=WxH` component parsing (`std::stoi` in `try`, two `catch` clauses, `wm2: warning: config key '...'` message form).

**CLI surface** (lines 257-287 `longOptions[]`, 308-317 dispatch): the D-19 focus-stealing off-switch gets both `{"focus-stealing-prevention", no_argument, ...}` and `{"no-focus-stealing-prevention", no_argument, ...}` entries plus matching `std::strcmp` branches — that enable/negate pair is the established Phase 5 shape. Rules deliberately get **no** CLI flags (repeated groups do not map to getopt).

---

### `include/Config.h` (config, transform)

**Analog:** itself, lines 21-36. Add the rules vector next to `manualMenuEntries` (same "subsystem data owned by Config" slot) and the new bool in the focus block:

```cpp
    // Focus policy
    bool clickToFocus = false;
    bool raiseOnFocus = false;
    bool autoRaise    = false;
    ...
    // Manual menu entries (APPS-04)
    std::vector<AppEntry> manualMenuEntries;
```

D-17 changes `autoRaise` and `raiseOnFocus` to `true`. That **will** break `tests/test_config.cpp:51-53` (`REQUIRE(cfg.raiseOnFocus == false); REQUIRE(cfg.autoRaise == false);`) — update those assertions deliberately, as an intended change.

---

### `include/Manager.h` + `src/Manager.cpp` — RANDR capability & geometry accessor (service, event-driven)

**Analog:** the Shape capability funnel in the same two files. D-27 asks for a deliberately symmetric mirror.

**Predicate accessor** (`include/Manager.h:51-55`):
```cpp
    // Display accessors (raw pointers for X11 API calls)
    Display* display() { return m_display.get(); }
    Window root() { return m_root; }
    int screen() { return m_screenNumber; }
    bool hasShapeExtension() const { return m_shapeEvent >= 0; }
```
→ add `bool hasRandrExtension() const { return m_randrEventBase >= 0; }`, `int screenWidth() const;`, `int screenHeight() const;` in this same block.

**Sentinel member** (`include/Manager.h:131`): `int m_shapeEvent;` → add `int m_randrEventBase;` beside it (same section, same `-1`-means-absent convention).

**Capability query + warn-and-continue** (`src/Manager.cpp:165-172`):
```cpp
    // Check Shape extension -- warn but continue if missing (graceful fallback)
    int dummy;
    if (XShapeQueryExtension(display(), &m_shapeEvent, &dummy)) {
        std::fprintf(stderr, "  Shape extension available.\n");
    } else {
        std::fprintf(stderr, "wm2: warning: no shape extension, frames will be rectangular\n");
        m_shapeEvent = -1;
    }
```
→ the `XRRQueryExtension` block goes immediately after, byte-for-byte in this style (two-space-indented "available" line, `wm2: warning: ` prefix on the degraded line, `-1` sentinel). The `WM2_FORCE_NO_SHAPE=1` check (D-12) is one `std::getenv` guard added to this same `if`.

**Atom interning** (`src/Manager.cpp:139-163`) — the five new atoms (`_NET_WM_USER_TIME`, `_NET_WM_USER_TIME_WINDOW`, `_NET_WM_STATE_DEMANDS_ATTENTION`, `_NET_WM_STATE_SKIP_TASKBAR`, `_NET_WM_STATE_SKIP_PAGER`) follow this exact column-aligned form and must also be declared as `static Atom` in `struct Atoms` (`include/Manager.h:224-247`):
```cpp
    Atoms::net_wmStateHidden      = XInternAtom(display(), "_NET_WM_STATE_HIDDEN", false);
    Atoms::net_wmWindowTypeDock   = XInternAtom(display(), "_NET_WM_WINDOW_TYPE_DOCK", false);
```

**`_NET_SUPPORTED` advertisement** (`src/Manager.cpp:474-492`) — the same five atoms must be appended to this array or clients never set the properties:
```cpp
    Atom supported[] = {
        Atoms::net_supported, Atoms::net_supportingWmCheck,
        ...
        Atoms::net_workarea,
    };
    XChangeProperty(display(), m_root, Atoms::net_supported,
                    XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(supported),
                    sizeof(supported) / sizeof(Atom));
```

**Root event mask** (`src/Manager.cpp:373-379`) — add `StructureNotifyMask` here for the no-RANDR fallback (Pitfall 2):
```cpp
    XSetWindowAttributes attr;
    attr.cursor = m_cursor.get();
    attr.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
        ColormapChangeMask | ButtonPressMask | ButtonReleaseMask |
        PropertyChangeMask;
    XChangeWindowAttributes(display(), m_root, CWCursor | CWEventMask, &attr);
```

**Fatal vs. warn convention** (`src/Manager.cpp:410`, `422-424`): `fatal("...")` for unrecoverable init; `fprintf(stderr, "wm2: warning: ...")` for degradable. A missing RANDR extension is a *warning*, never fatal.

---

### `src/Events.cpp` — RANDR dispatch and event-loop changes (controller, event-driven)

**Analog:** the existing extension-event dispatch in the same `switch` (`src/Events.cpp:109-122`):
```cpp
        case FocusOut:
        case ConfigureNotify:
        case MapNotify:
        case MappingNotify:
            break;

        default:
            if (ev.type == m_shapeEvent) {
                std::fprintf(stderr, "wm2: shaped windows are not supported\n");
            } else {
                std::fprintf(stderr, "wm2: unsupported event type %d\n", ev.type);
            }
            break;
```
The RANDR branch is inserted as the first `if` in this `default:` chain, guarded by `m_randrEventBase >= 0` (mirroring how `m_shapeEvent == -1` makes the shape compare unreachable), and `ConfigureNotify` is split out of the no-op case group with a `window == m_root` test.

**Handler dispatch style** (`src/Events.cpp:189-227`): manager-level handlers look up the client then delegate — `Client *c = windowToClient(e->window); if (c) c->eventX(e); else fprintf(stderr, "wm2: ...")`. `handleScreenGeometryChange()` follows the same file's convention: a small `WindowManager::` free-standing member that iterates `m_clients` and calls into `Client`.

**Timer/poll structure to not disturb** (`src/Events.cpp:130-186`): `nextEvent()` polls `[X fd, self-pipe]` with `computePollTimeout()`. The D-15 `autoRaise=false` gate works *because* this returns `-1` when no deadline is active — do not add an unconditional timer.

---

### `src/Border.cpp` / `include/Border.h` — `combineShape()` funnel (component, request-response)

**Analog:** the existing (only) guarded helper and a representative raw call site.

**Guard predicate already present** (`src/Border.cpp:149-152`):
```cpp
bool Border::shapeAvailable()
{
    return windowManager()->hasShapeExtension();
}
```

**Typical call site to be rewritten** (`src/Border.cpp:155-169`) — note the paired `ShapeBounding` + `ShapeClip` calls; the wrapper's parameter list must cover both `destKind` values and all of `ShapeSet`/`ShapeUnion`/`ShapeSubtract`:
```cpp
void Border::shapeParentRectangular(int w, int h)
{
    // Simple rectangular frame: full width/height, no fancy shaping
    XRectangle frame;
    frame.x = 0;
    frame.y = 0;
    frame.width = w + m_tabWidth + FRAME_WIDTH + 1;
    frame.height = h + FRAME_WIDTH + 1;
    XShapeCombineRectangles(display(), m_parent, ShapeBounding,
        0, 0, &frame, 1, ShapeSet, YXBanded);

    frame.x++; frame.y++; frame.width -= 2; frame.height -= 2;
    XShapeCombineRectangles(display(), m_parent, ShapeClip,
        0, 0, &frame, 1, ShapeSet, YXBanded);
}
```

**Invariant after the refactor:** `grep -c XShapeCombineRectangles src/Border.cpp` == 1, down from **26** today `[VERIFIED: grep -c XShapeCombineRectangles src/Border.cpp → 26]`. Make that a `scripts/` guard or ctest assertion, not a review promise.

---

### `src/Client.cpp` — WM_CLASS read, rule application, focus arbitration (model, request-response)

**Analog A — X11 property read with `XFree` cleanup and a default-on-failure**: `Client::getWindowType()` (`src/Client.cpp:578-605`). `getClassHint()` copies this shape (init member to a safe default, early `return` on failure, `XFree` at the end):
```cpp
    m_windowType = WindowType::Normal;  // default

    n = getProperty_aux(display(), m_window, Atoms::net_wmWindowType, XA_ATOM,
                        1024L, reinterpret_cast<unsigned char**>(&data));
    if (n <= 0) return;
    ...
    XFree(data);
```

**Analog B — Xlib hint getter with warning path**: `Client::getTransient()` (`src/Client.cpp:608-625`) is the closest analog for `XGetClassHint()` (both are `XGet*Hint`-family calls returning a status, both need a stderr warning branch and cleanup). Note `XGetClassHint` fills an `XClassHint` whose `res_name`/`res_class` each need `XFree` — match against **both** fields per D-21.

**Analog C — the no-decorate path to reuse for the rule action** (`src/Client.cpp:117-128`):
```cpp
    // D-01/D-02: DOCK and NOTIFICATION windows get no frame decoration
    if (m_windowType == WindowType::Dock || m_windowType == WindowType::Notification) {
        XMapWindow(display(), m_window);
        setState(ClientState::Normal);
        windowManager()->updateClientList();

        // D-09: Dock windows affect workarea
        if (m_windowType == WindowType::Dock) {
            windowManager()->updateWorkarea();
        }
        return;
    }
```
The rule matcher must run **before** this block (it needs WM_CLASS/WM_NAME/type, all read at lines 108-115) so a `no-decorate` match can take the same early-return.

**Analog D — `skip-taskbar` state publication** (`src/Client.cpp:377-392`): add two bools and two `states.push_back()` lines; the property write needs no change:
```cpp
void Client::updateNetWmState()
{
    std::vector<Atom> states;
    if (m_isFullscreen) states.push_back(Atoms::net_wmStateFullscreen);
    if (m_isMaximizedVert) states.push_back(Atoms::net_wmStateMaximizedVert);
    if (m_isMaximizedHorz) states.push_back(Atoms::net_wmStateMaximizedHorz);
    if (isHidden()) states.push_back(Atoms::net_wmStateHidden);
    ...
}
```
`_NET_WM_STATE_DEMANDS_ATTENTION` (D-18) is published through this same function — do not write `_NET_WM_STATE` from anywhere else.

**Analog E — geometry clamp to swap onto the D-27 accessor** (`src/Client.cpp:840-856`). Already implements D-25 "move, not resize" and already skips fullscreen/maximized; only the two `DisplayWidth`/`DisplayHeight` reads change:
```cpp
void Client::ensureVisible()
{
    // Fullscreen and maximized windows should not be repositioned
    if (m_isFullscreen || (m_isMaximizedVert && m_isMaximizedHorz)) return;

    int mx = DisplayWidth(display(), 0) - 1;
    int my = DisplayHeight(display(), 0) - 1;
    ...
    if (m_x != px || m_y != py) m_border->moveTo(m_x, m_y);
}
```
Same swap is required at `src/Client.cpp:172` (`int dw = DisplayWidth(display(), 0), dh = ...`) plus the sites in `src/Buttons.cpp` and `src/Manager.cpp` enumerated in RESEARCH Pitfall 1.

**Analog F — the `raiseOnFocus` split point** (`src/Client.cpp:880-896`). `activate(); mapRaised();` are currently fused; D-16 gates only the second:
```cpp
void Client::focusIfAppropriate(bool ifActive)
{
    if (!m_managed || !isNormal()) return;
    if (!ifActive && isActive()) return;
    ...
    if (hasWindow(cw)) {
        activate();
        mapRaised();
        m_windowManager->stopConsideringFocus();
    }
}
```

---

### `tests/test_rules.cpp` (test, transform)

**Analog:** `tests/test_config.cpp` — the display-free parser test. Copy its temp-file helpers and its `TEST_CASE("...", "[config]")` tag/structure; use a new `[rules]` tag.

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "Config.h"
...
// Helper: write a temp config file and return its path
static std::string writeTempConfig(const std::string& content) {
    static int counter = 0;
    std::string path = "/tmp/wm2-test-config-" + std::to_string(++counter) + ".cfg";
    std::ofstream out(path);
    out << content;
    out.close();
    return path;
}

TEST_CASE("applyFile parses key=value and overrides defaults", "[config]") {
    std::string path = writeTempConfig(
        "tab-foreground = white\n"
        ...
    );
    Config cfg;
    cfg.applyFile(path);
    REQUIRE(cfg.tabForeground == "white");
}
```

---

### `tests/test_wm_process.cpp`, `test_wm_geometry.cpp`, `test_wm_fallbacks.cpp`, `test_wm_focus.cpp`, `test_wm_rules.cpp`, `test_wm_lifecycle.cpp`, `test_wm_state.cpp`, `test_wm_runtime.cpp`, `test_wm_resource.cpp` (test, event-driven)

**Analog:** `tests/test_ewmh.cpp` — the only existing test that opens a display and asserts on X properties. Copy: `x11::DisplayPtr` for the connection (never a raw `XOpenDisplay` + manual close), `XOpenDisplay(std::getenv("DISPLAY"))`, `XSync` before read-back, and the full six-out-param `XGetWindowProperty` + `REQUIRE(status == Success)` + `nItems`/`data != nullptr` triple.

**Connection + property assertion** (`tests/test_ewmh.cpp:72-110`):
```cpp
TEST_CASE("_NET_SUPPORTING_WM_CHECK self-reference", "[ewmh]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    Window root = DefaultRootWindow(display.get());
    ...
    XSync(display.get(), false);

    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    Window* data = nullptr;
    int status = XGetWindowProperty(display.get(), root, net_supportingWmCheck,
                                    0, 1, false, XA_WINDOW,
                                    &actualType, &actualFormat, &nItems, &bytesAfter,
                                    reinterpret_cast<unsigned char**>(&data));
    REQUIRE(status == Success);
    REQUIRE(nItems == 1);
    REQUIRE(data != nullptr);
    REQUIRE(data[0] == checkWindow);
```
Difference from the analog: these tests must read `getenv("DISPLAY")`-equivalent **from the fixture's own display string**, not the process env, because each `WmFixture` allocates its own display (Pitfall 3).

---

### `CMakeLists.txt` (config/build, batch)

**pkg-config dependency block** (lines 10-14) — `xrandr` is appended here and to `target_link_libraries` (36-42); `xtst` is added **only** inside `if(BUILD_TESTS)` and linked only into the process-level targets (C-6):
```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
pkg_check_modules(XEXT REQUIRED IMPORTED_TARGET xext)
pkg_check_modules(XFT REQUIRED IMPORTED_TARGET xft)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
```

**Xvfb fixture pattern to clone for the `-extension RANDR` / `-extension RENDER` servers** (lines 85-102):
```cmake
        # Xvfb fixture: start before tests, stop after
        set(XVFB_PID_FILE "${CMAKE_BINARY_DIR}/xvfb.pid")
        add_test(NAME start_xvfb
            COMMAND bash -c "Xvfb :99 -screen 0 1024x768x24 -ac +render -noreset & echo $! > ${XVFB_PID_FILE} && sleep 1"
        )
        set_tests_properties(start_xvfb PROPERTIES FIXTURES_SETUP xvfb_display)

        add_test(NAME stop_xvfb
            COMMAND bash -c "kill $(cat ${XVFB_PID_FILE}) 2>/dev/null || true"
        )
        set_tests_properties(stop_xvfb PROPERTIES FIXTURES_CLEANUP xvfb_display)

        catch_discover_tests(test_smoke
            PROPERTIES
                ENVIRONMENT "DISPLAY=:99"
                FIXTURES_REQUIRED xvfb_display
        )
```
The D-02 preflight fixture uses the same `add_test` + `FIXTURES_SETUP` idiom (setup half only, no cleanup test).

**Display-free test target pattern** (lines 183-191) — clone exactly for `test_rules`, compiling the implementation `.cpp` directly into the test binary rather than linking a library:
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

**Display-backed test target pattern** (lines 166-180, `test_ewmh`) — clone the target/link/include shape for each `test_wm_*` target, but **drop the `ENVIRONMENT "DISPLAY=:99"` and `FIXTURES_REQUIRED xvfb_display` properties**: every WM-bearing target allocates its own display through `WmFixture`. Isolation from the legacy group comes from `RESOURCE_LOCK "x_display_99"` placed on the four `:99` targets (`test_smoke`, `test_client`, `test_xft_poc`, `test_ewmh`), not from adding a fifth `DISPLAY=:99` binding. The `DISPLAY=:99` count in `CMakeLists.txt` must stay at **4** for the whole phase `[VERIFIED: grep -c 'DISPLAY=:99' CMakeLists.txt → 4; the `Xvfb :99` server-launch line is not a `DISPLAY=` assignment]`.

## Shared Patterns

### Capability funnel (applies to: Shape wrapper D-11, geometry accessor D-27, RANDR)
**Source:** `include/Manager.h:55` + `src/Manager.cpp:165-172` + `src/Border.cpp:149-152`
```cpp
// header: predicate over an int sentinel
bool hasShapeExtension() const { return m_shapeEvent >= 0; }

// ctor: query, announce, or degrade to -1
if (XShapeQueryExtension(display(), &m_shapeEvent, &dummy)) {
    std::fprintf(stderr, "  Shape extension available.\n");
} else {
    std::fprintf(stderr, "wm2: warning: no shape extension, frames will be rectangular\n");
    m_shapeEvent = -1;
}
```
Every new optional capability gets exactly this triple: member sentinel, `has*()` predicate, one warn-and-continue query in the constructor.

### Error handling
**Source:** `src/Manager.cpp:410` / `422-424` (fatal) and `src/Client.cpp:614-617` (warning)
**Apply to:** all new source files
```cpp
    if (!menuFont) fatal("couldn't load default menu font");
    ...
    std::fprintf(stderr,
                 "wm2: warning: client \"%s\" thinks it's a transient "
                 "for itself -- ignoring WM_TRANSIENT_FOR\n",
                 m_label.c_str());
```
Rule: `fatal()` only for unrecoverable *initialisation*; degraded capability and bad user input are always `wm2: warning: ` on stderr. RESEARCH flags `src/Border.cpp:49` (rotated-font `fatal()`) as a violation to fix.

**Scope fence:** `src/Border.cpp` holds **six** `fatal(` lines today `[VERIFIED: grep -c 'fatal(' src/Border.cpp → 6]` — line 49 (rotated font), line 87 (border GC allocation), lines 139 and 143 (Xft foreground/background colour allocation), and lines 206-208 (`Border::fatal()`'s own definition, declared at `include/Border.h:60`). **Only line 49 is misclassified.** The GC and colour allocations are genuine unrecoverable initialisation and stay fatal; the definition obviously stays. 08-06 Task 2 takes the count from 6 to 5, never to 0.

### RAII for X11 / POSIX handles
**Source:** `include/x11wrap.h` (`x11::DisplayPtr`, `x11::UniqueCursor`, `x11::XftFontPtr`, `x11::XftColorWrap`), plus `FdGuard` at `include/Manager.h:20-33`
**Apply to:** every new X handle in production and test code (`XTestDriver`'s connection, the fixture's fds). Note `FdGuard` is the in-repo template for a move-only, delete-copy, `get()`-accessor wrapper — copy it rather than inventing a new one.

### Config key parsing
**Source:** `src/Config.cpp:34-45` (`clampInt`, `parseBool`, `toLower`, `trim`), `:174-184` (integer + two-catch warn), `:186-226` (repeated group), `:228-229` (unknown key warn)
**Apply to:** all new `rule-*` and focus keys.

### Ownership
**Source:** `include/Manager.h:118-121`
```cpp
    // Client list (D-01: unique_ptr ownership)
    std::vector<std::unique_ptr<Client>> m_clients;
    std::vector<std::unique_ptr<Client>> m_hiddenClients;
    std::unordered_map<Window, Client*> m_windowMap;  // D-05: O(1) lookup
```
The `eventDestroy` fix must respect this: erasing from `m_clients` runs `~Client()`, so capture `const bool wasDock = c->isDock();` **before** the erase (`src/Events.cpp:272-284`).

### Decision-ID comments
**Source:** `src/Client.cpp:117` (`// D-01/D-02: DOCK and NOTIFICATION ...`), `src/Client.cpp:601`, `src/Config.cpp:186-190`, `include/Manager.h:104`
**Apply to:** every non-obvious new branch — the codebase consistently cites the phase decision ID in a comment at the decision point. New code cites `D-11`, `D-12`, `D-17` … `D-27` the same way.

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| `tests/support/WmFixture.h` | test utility | process lifecycle | Nothing in the tree forks/execs a child and manages its lifetime from a test. `WindowManager::spawn()` (`src/Manager.cpp:~840`) is the only fork/exec in the repo and is a double-fork spawn-and-forget with no `waitpid`, so it is not a usable model. Use RESEARCH Pattern 1 (Code Example, RESEARCH.md:338-363) plus the `FdGuard` (`include/Manager.h:20-33`) move-only wrapper shape. |
| `tests/support/XTestDriver.h` | test utility | event-driven | No XTEST usage exists. Partial analog only: `x11::DisplayPtr` ownership and `tests/test_ewmh.cpp`'s connection setup. Use RESEARCH Code Example 2 (RESEARCH.md:710-741). |
| `scripts/preflight.sh`, `scripts/analysis/*` | script | batch | No `scripts/` directory exists in the repo at all — greenfield. No shell-script conventions to inherit; follow `set -euo pipefail` and the `wm2: ` stderr prefix for consistency with the binary. |

## Metadata

**Analog search scope:** `src/`, `include/`, `tests/`, `CMakeLists.txt` (repository root). `upstream-wm2/` deliberately excluded per phase instruction.
**Files scanned:** 10 source, 10 headers, 11 test files, 1 CMakeLists (file listing); 9 read in detail.
**Pattern extraction date:** 2026-08-11
