# Architecture Patterns: Modern X11 Window Manager

**Domain:** Lightweight floating X11 window manager (wm2 modernization)
**Researched:** 2026-05-06
**Overall confidence:** HIGH

## Recommended Architecture

A layered, single-process event-driven architecture with clear component boundaries. The WM runs one main thread with a unified event loop that multiplexes X11 events, timers, and optional IPC. No threading required -- X11 is inherently single-threaded, and a lightweight WM has no compute-heavy work.

```
+-------------------------------------------------------------+
|                      wm2-born-again                         |
+-------------------------------------------------------------+
|                                                             |
|  +------------------+    +-------------------------------+  |
|  |  Config System   |    |      AI Menu Scanner          |  |
|  |  (file + reload) |    |  (desktop entries + /bin)     |  |
|  +--------+---------+    +---------------+---------------+  |
|           |                              |                  |
|  +--------v------------------------------v---------------+  |
|  |                    Application Core                    |  |
|  |                                                        |  |
|  |  +-----------+  +---------+  +--------+  +----------+ |  |
|  |  | WindowManager | Client  |  | Border |  | RootMenu | |  |
|  |  | (controller)|  | (state) |  | (view) |  | (items)  | |  |
|  |  +-----+-----+  +----+----+  +---+----+  +-----+----+ |  |
|  |        |              |           |              |      |  |
|  +--------|--------------|-----------|--------------|------+  |
|           |              |           |              |         |
|  +--------v--------------v-----------v--------------v------+  |
|  |                 X11 Resource Layer                        |  |
|  |  (RAII wrappers: Display, Window, GC, Cursor, Pixmap)   |  |
|  +---------------------------+-------------------------------+  |
|                              |                                |
|  +---------------------------v-------------------------------+ |
|  |                 Event Loop + Timers                       | |
|  |  (poll-based, ConnectionNumber fd, timer heap)           | |
|  +---------------------------+-------------------------------+ |
|                              |                                |
+------------------------------|--------------------------------+
                               |
                    +----------v-----------+
                    |   X11 Server (Xlib)  |
                    +----------------------+
```

### Separate Process: Config GUI Tool

The configuration GUI is a separate binary that communicates with the WM via a Unix domain socket (IPC). This follows i3's proven pattern: the WM itself stays minimal and X11-only, while the GUI tool can use GTK without polluting the WM's dependency tree.

```
+-------------------+     Unix Socket      +-------------------+
|  wm2-born-again   | <=================> |  wm2-config (GTK) |
|  (WM process)     |   JSON IPC protocol  |  (separate bin)   |
+-------------------+                      +-------------------+
```

## Component Boundaries

| Component | Responsibility | Owns | Communicates With |
|-----------|---------------|------|-------------------|
| **EventLoop** | Wait on X11 fd + timers, dispatch events | Timer heap | X11ResourceLayer, WindowManager |
| **X11Display** | RAII wrapper for Display connection | `Display*` | All components needing X calls |
| **X11Window** | RAII wrapper for Window lifecycle | `Window` | Border, Client |
| **X11GC** | RAII wrapper for Graphics Context | `GC` | Border (drawing) |
| **X11Cursor** | RAII wrapper for Cursor | `Cursor` | WindowManager |
| **X11Pixmap** | RAII wrapper for Pixmap | `Pixmap` | Border |
| **WindowManager** | Central controller: focus, colormap, root menu, EWMH | Client list (vector), Config reference | Client, Border, EventLoop, RootMenu, IPC |
| **Client** | One managed X window: state, hints, protocols | Border (unique_ptr), X11 resources | WindowManager, Border |
| **Border** | Visual frame: shaped borders, tab, Xft text | X11Window, X11GC, X11Pixmap | Client (queries only) |
| **RootMenu** | X11 popup menu for launching apps, circulate, exit | Menu items | WindowManager, AppScanner |
| **Config** | Load/parse config file, provide settings access | Parsed config state | WindowManager, IPC (reload) |
| **IPC** | Unix domain socket server for external tools | Socket fd, client connections | Config, WindowManager |
| **AppScanner** | Discover installed applications for root menu | Scanned app list (cached) | RootMenu |
| **EWMH** | _NET_* atom management and compliance | Atom cache | WindowManager, Client |

### Dependency Graph (Build Order)

```
Phase 1: Foundation (no X11 dependency, testable standalone)
    X11ResourceLayer (RAII types)
    Config (file parser)
    AppScanner (filesystem scanner)
    IPC (socket protocol)

Phase 2: Core X11 (depends on Phase 1)
    EventLoop
    WindowManager (skeleton)
    Client (basic lifecycle)
    Border (basic frame)

Phase 3: Features (depends on Phase 2)
    RootMenu
    EWMH
    Full Client (all ICCCM properties)
    Full Border (shaped tabs, Xft rendering)

Phase 4: Integration (depends on Phase 3)
    IPC server in WM
    Config GUI (separate binary, uses IPC)
    AI Menu Scanner (background scan + cache)
```

## Data Flow

### X11 Event Flow (Primary Path)

```
X11 Server
    |
    v  (kernel: X11 protocol on socket fd)
EventLoop::poll()
    |  poll(ConnectionNumber(display), timer_fd, ipc_fd)
    |
    +-- X11 event ready?
    |       |
    |       v
    |   XNextEvent(display, &event)
    |       |
    |       v
    |   WindowManager::dispatch(event)
    |       |
    |       +-- MapRequest      --> Client::manage()
    |       +-- ConfigureRequest --> Client::configureRequest()
    |       +-- UnmapNotify     --> Client::unmap()
    |       +-- DestroyNotify   --> WindowManager::destroyClient()
    |       +-- ButtonPress     --> RootMenu OR Client::button()
    |       +-- MotionNotify    --> Client::motion() (move/resize drag)
    |       +-- Expose          --> Border::expose()
    |       +-- EnterNotify     --> Client::enter() (focus follows pointer)
    |       +-- FocusIn         --> Client::focusIn()
    |       +-- PropertyNotify  --> Client::propertyChange()
    |       +-- ClientMessage   --> EWMH::handleMessage()
    |       +-- ColormapNotify  --> Client::colormap()
    |
    +-- Timer expired?
    |       |
    |       v
    |   TimerQueue::dispatch()
    |       +-- auto-raise timer --> Client::raise()
    |       +-- menu scan timer  --> AppScanner::refresh()
    |
    +-- IPC socket ready?
            |
            v
        IPC::handleConnection()
            +-- GET_CONFIG   --> Config::serialize()
            +-- SET_CONFIG   --> Config::apply() --> reconfigure
            +-- GET_CLIENTS  --> WindowManager::clientList()
            +-- RELOAD       --> Config::reload()
```

### Configuration Data Flow

```
~/.config/wm2-born-again/config (text file)
    |
    v
Config::load()
    |  Parse key=value pairs
    v
Config object (in-memory)
    |
    +-- WindowManager reads: focus policy, colors, fonts, delays
    +-- Border reads: frame width, tab height, colors
    +-- RootMenu reads: menu items, custom entries
    +-- EWMH reads: supported atoms list

Hot reload path:
    SIGHUP or IPC RELOAD command
        --> Config::reload()
        --> WindowManager::reconfigure()
            --> Border::recomputeColors() on all clients
            --> Border::recomputeFont() on all clients
            --> RootMenu::rebuild()
```

### Application Scanner Data Flow

```
On startup (or timer, or inotify):
    |
    +-- Scan /usr/share/applications/*.desktop
    +-- Scan ~/.local/share/applications/*.desktop
    +-- Parse Name=, Exec=, Icon=, Categories=, NoDisplay=, Terminal=
    +-- Filter: Type=Application, NoDisplay=false
    |
    v
Cached app list (sorted by Name)
    |
    v
RootMenu::rebuild()
    |  Merge user config menu entries + scanned apps
    v
X11 popup menu (rendered on right-click)
```

## Patterns to Follow

### Pattern 1: RAII Wrappers for X11 Resources

**What:** Each X11 resource type gets a thin RAII wrapper using `unique_ptr` with custom deleters. No custom wrapper classes needed -- the `unique_ptr` + deleter approach is zero-overhead and idiomatic C++17.

**When:** Every X11 resource that has a create/destroy lifecycle.

**Example:**

```cpp
// x11_resource.h -- RAII wrappers for X11 resource types

#pragma once
#include <X11/Xlib.h>
#include <memory>

namespace wm2::x11 {

// Display connection -- the foundational resource
struct DisplayDeleter {
    void operator()(Display* d) const noexcept {
        if (d) XCloseDisplay(d);
    }
};
using DisplayPtr = std::unique_ptr<Display, DisplayDeleter>;

// Graphics Context
struct GCDeleter {
    Display* display;
    void operator()(GC gc) const noexcept {
        if (gc) XFreeGC(display, gc);
    }
};
// GCDeleter is stateful (needs display pointer) -- that's fine,
// unique_ptr supports stateful deleters without overhead.

// Window -- needs display for XDestroyWindow
struct WindowDeleter {
    Display* display;
    void operator()(Window w) const noexcept {
        if (w) XDestroyWindow(display, w);
    }
};
using WindowPtr = std::unique_ptr<Window, WindowDeleter>;

// Pixmap
struct PixmapDeleter {
    Display* display;
    void operator()(Pixmap p) const noexcept {
        if (p) XFreePixmap(display, p);
    }
};
using PixmapPtr = std::unique_ptr<Pixmap, PixmapDeleter>;

// Cursor
struct CursorDeleter {
    Display* display;
    void operator()(Cursor c) const noexcept {
        if (c) XFreeCursor(display, c);
    }
};
using CursorPtr = std::unique_ptr<Cursor, CursorDeleter>;

// Generic XFree for anything returned by Xlib that needs XFree()
struct XFreeDeleter {
    void operator()(void* p) const noexcept {
        if (p) XFree(p);
    }
};
using XFreePtr = std::unique_ptr<void, XFreeDeleter>;

} // namespace wm2::x11
```

**Key design decisions:**
- Stateful deleters (carrying `Display*`) are the right approach because the X11 API requires `Display*` for all destroy calls. The `unique_ptr` stores the deleter inline with no allocation overhead.
- `Window` is not a pointer type, so we cannot use `unique_ptr` directly with it. Instead, use a wrapper class or manage Window IDs manually with a scope guard pattern. In practice, since Border manages multiple windows with complex lifecycle (reparenting, shape changes), a dedicated `ManagedWindow` small class with destructor is cleaner than `unique_ptr` for Window specifically.
- The factory function pattern (make_xxx) avoids the `STDIN_FILENO` gotcha documented in the `unique_ptr` RAII literature.

### Pattern 2: Unified Event Loop with poll()

**What:** Replace wm2's `select()` + `goto` polling loop with a clean `poll()`-based event loop.

**When:** Always -- this is the core execution model.

**Why poll() not epoll():** For a window manager handling 1-2 file descriptors (X11 connection + optional IPC socket), `poll()` is simpler and equally fast. `epoll()` adds complexity for zero benefit at this scale. wm2's current code never handles more than ~20 windows; there is no scaling concern that warrants epoll.

**Example:**

```cpp
// event_loop.h
namespace wm2 {

class EventLoop {
public:
    using TimerCallback = std::function<void()>;

    void run();  // blocks until stop() called
    void stop();

    // Timer management for auto-raise, etc.
    void addTimer(std::chrono::milliseconds delay, TimerCallback cb);
    void cancelTimers();

private:
    void processX11Events();
    void processIPCEvents();
    void processTimers();

    Display* display_;  // non-owning, borrowed from WindowManager
    int ipc_fd_ = -1;   // IPC socket fd, -1 if not listening

    struct TimerEntry {
        std::chrono::steady_clock::time_point deadline;
        TimerCallback callback;
    };
    std::vector<TimerEntry> timers_;
    bool running_ = false;
};

} // namespace wm2
```

**Why this works:**
1. `poll()` on `ConnectionNumber(display_)` for X11 events -- blocks until data arrives
2. `poll()` on IPC socket fd (if configured) -- handles config GUI communication
3. `poll()` timeout set to nearest timer deadline -- wakes up for auto-raise, etc.
4. No busy-waiting, no 50ms spin loops, no `goto`

### Pattern 3: Client as Value Object with Managed Resources

**What:** Client objects are owned by WindowManager in a `std::vector<std::unique_ptr<Client>>`. Each Client owns its Border. No `delete this`.

**When:** All client window management.

**Example:**

```cpp
class Client {
public:
    Client(WindowManager& wm, Window w);
    ~Client();  // cleans up X11 resources, reparents back to root

    // Non-copyable, non-movable (owns X11 resources tied to window IDs)
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // ICCCM lifecycle
    void manage();
    void withdraw();
    void destroy();

    // State queries
    Window window() const { return window_; }
    bool isTransient() const { return transient_for_ != None; }
    int state() const { return state_; }

private:
    WindowManager& wm_;          // back-reference, non-owning
    Window window_;              // client window (not owned, belongs to app)
    std::unique_ptr<Border> border_;  // frame decoration (owned)
    int state_ = WithdrawnState;
    Window transient_for_ = None;
    XSizeHints size_hints_;
    // ... other ICCCM properties
};
```

**Why unique_ptr for Border:** Border has complex initialization (creates multiple X windows, sets shape, loads font). Its lifetime is strictly tied to the Client. `unique_ptr` makes ownership explicit and allows forward declaration to avoid header coupling.

### Pattern 4: Config as Parsed State Object

**What:** Config is a data object parsed from a text file, not a collection of #defines. Supports reload.

**When:** All configuration access.

**Example:**

```cpp
struct Config {
    // Focus
    bool click_to_focus = true;
    bool raise_on_focus = true;
    bool auto_raise = false;
    std::chrono::milliseconds auto_raise_delay{500};

    // Appearance
    std::string font_name = "monospace:bold:size=11";  // Xft pattern
    std::string fg_color = "#000000";
    std::string bg_color = "#cccccc";
    std::string frame_color = "#999999";
    std::string active_color = "#ffffff";
    int frame_width = 5;
    int tab_top_height = 20;

    // Menu
    std::vector<MenuItem> custom_menu_entries;
    bool scan_desktop_files = true;

    // Load from file, return parsed config or error
    static std::expected<Config, std::string> load(const std::filesystem::path& path);
};
```

**Why `std::expected`:** C++17 does not have `std::expected` (that is C++23). Use a simple Result type or `std::optional<Config>` with an error string output parameter. The key point: config parsing must not crash the WM on bad input. Return the error, log it, fall back to defaults.

### Pattern 5: IPC Protocol (for Config GUI)

**What:** Simple JSON-over-Unix-socket protocol, inspired by i3's IPC but much simpler.

**When:** Communication between config GUI tool and WM.

**Protocol:**
```
Message format: 4-byte length (network byte order) + JSON payload
Request:  {"method": "get_config"} or {"method": "set_config", "params": {...}}
Response: {"status": "ok", "data": {...}} or {"status": "error", "message": "..."}
```

**Why this design:**
- JSON is human-readable and debuggable with `socat` or `nc`
- Unix socket is local-only, no network exposure
- Length-prefixed framing avoids JSON parsing ambiguity
- No need for the binary magic-string protocol i3 uses -- this is simpler and sufficient

### Pattern 6: Application Scanner

**What:** Background scan of .desktop files and /bin to populate root menu.

**When:** On startup and optionally on timer/inotify.

**Example:**

```cpp
struct AppEntry {
    std::string name;       // from Name= field
    std::string exec;       // from Exec= field
    std::string icon;       // from Icon= field (optional)
    std::string category;   // from Categories= field
    bool terminal;          // from Terminal= field
};

class AppScanner {
public:
    // Scan standard locations, return sorted list
    std::vector<AppEntry> scan();

    // Get cached results (fast, for menu rebuild)
    const std::vector<AppEntry>& cached() const { return cache_; }

    // Cache management
    void refresh();
    void invalidate();  // force re-scan on next access

private:
    std::vector<AppEntry> cache_;
    std::filesystem::file_time_type cache_time_;
};
```

**Key decisions:**
- Scan `.desktop` files from `/usr/share/applications/`, `/usr/local/share/applications/`, and `~/.local/share/applications/` -- this is the freedesktop.org standard, far more reliable than scanning `/bin` directly.
- The "AI scan of /bin and /sbin" from the project description should supplement .desktop files, not replace them. Many binaries in /bin are not user-facing applications (e.g., `ls`, `cat`, `mount`). Use .desktop files as the primary source, then optionally add AI heuristics for binaries without .desktop entries.
- Cache results with a file timestamp. Re-scan only when mtime changes or on explicit reload.
- No background thread needed. Scan at startup, cache in memory, re-scan on timer (every 30s) or SIGHUP. For a VPS with ~100 installed apps, scan takes <50ms.

## Anti-Patterns to Avoid

### Anti-Pattern 1: Xlib in Multiple Threads

**What:** Using Xlib calls from more than one thread.
**Why bad:** Xlib is not thread-safe by default. `XInitThreads()` exists but introduces subtle races and deadlocks. No X11 WM needs threads for X11 calls.
**Instead:** Keep all X11 interaction on the main thread. Use the event loop for everything. If background work is needed (app scanning), do it synchronously during idle periods or in a forked process.

### Anti-Pattern 2: God Object WindowManager

**What:** Putting all logic in WindowManager (the current wm2 architecture).
**Why bad:** The current `Manager.C` is a monolith handling events, focus, colormap, cursor, menu, signals, and client lifecycle. 571 lines of entangled logic.
**Instead:** Decompose into focused components. WindowManager coordinates; it does not implement. Delegate to Config, EWMH, RootMenu, AppScanner, IPC. Each is independently testable.

### Anti-Pattern 3: Raw Window IDs as Lookup Keys

**What:** Linear scan through client list on every event to find which Client owns a Window.
**Why bad:** O(n) on every event. The current `windowToClient()` checks multiple windows per client.
**Instead:** Maintain `std::unordered_map<Window, Client*>` keyed by ALL managed Window IDs (client window, parent frame, tab, button, resize handle). Lookup is O(1).

```cpp
class WindowManager {
    std::vector<std::unique_ptr<Client>> clients_;
    std::unordered_map<Window, Client*> window_map_;  // all windows -> owning client

    Client* findClient(Window w) const {
        auto it = window_map_.find(w);
        return it != window_map_.end() ? it->second : nullptr;
    }
};
```

### Anti-Pattern 4: delete this

**What:** Client objects deleting themselves via `delete this` (the current wm2 pattern).
**Why bad:** After `delete this`, any access to member variables is undefined behavior. The caller may still hold a pointer.
**Instead:** WindowManager owns all Clients and handles destruction explicitly:

```cpp
void WindowManager::destroyClient(Window w) {
    auto it = std::find_if(clients_.begin(), clients_.end(),
        [w](const auto& c) { return c->window() == w; });
    if (it != clients_.end()) {
        clients_.erase(it);  // unique_ptr destructor handles cleanup
    }
}
```

### Anti-Pattern 5: Configuration via Source Code Editing

**What:** Compile-time #defines for all settings (the current wm2 approach).
**Why bad:** No per-user customization. Requires recompilation for any change.
**Instead:** Runtime config file with sensible defaults. Config.h defines only build-time constants (version string, default paths).

### Anti-Pattern 6: inotify for Config Hot-Reload

**What:** Using inotify to watch the config file for changes and auto-reload.
**Why bad:** Editors that use atomic save (write to temp file, rename) generate `IN_MOVED_TO` instead of `IN_MODIFY`. Race conditions with partial writes. Debouncing complexity.
**Instead:** SIGHUP signal for reload (standard Unix approach) + explicit IPC reload command from the config GUI. Simple, reliable, well-understood.

## Build Order (Modernization Sequence)

This is the recommended order for modernizing wm2, driven by dependency relationships. Each phase produces a buildable, testable increment.

### Phase 1: Build Infrastructure and RAII Foundation
**Rationale:** Must be able to build before doing anything else. RAII wrappers have zero dependencies and make all subsequent work safer.
- Set up CMake build system with pkg-config for X11, Xft, Xrandr
- Rename `.C` to `.cpp`
- Create `x11/resource.h` with RAII wrappers (DisplayPtr, etc.)
- Create `config.h` as a data struct (no longer just #defines)
- Port existing code to compile under C++17 with warnings enabled
- **Test:** Build succeeds. `DisplayPtr` unit tests. Config parsing unit tests.

### Phase 2: Event Loop Modernization
**Rationale:** The event loop is the heartbeat. Everything depends on it. Modernizing it first unblocks clean testing.
- Replace `select()` + `goto` with `poll()`-based EventLoop class
- Remove 50ms spin-wait polling; use proper blocking with timeout
- Add timer infrastructure (for auto-raise delay)
- **Test:** Integration test with Xvfb: start WM, verify it enters event loop, respond to SIGTERM.

### Phase 3: Client Lifecycle with RAII
**Rationale:** Client management is the core WM function. RAII for Client/Border eliminates the `delete this` and leak bugs.
- `std::vector<std::unique_ptr<Client>>` replaces macro-based list
- `std::unordered_map<Window, Client*>` replaces linear scan
- Client owns Border via `unique_ptr`
- Eliminate `delete this`; WindowManager handles destruction
- **Test:** Create/destroy 100 clients in Xvfb. Verify no X resource leaks via `xrestop`.

### Phase 4: Border + Xft Font Rendering
**Rationale:** Border is the most fragile code (746 lines of shape geometry). Modernize after Client lifecycle is solid.
- Replace xvertext + core X fonts with Xft + fontconfig
- Preserve shaped border visual identity (non-negotiable per project spec)
- RAII for XftFont, XftDraw
- **Test:** Visual comparison test: screenshot known window layouts, compare pixel patterns.

### Phase 5: Configuration System
**Rationale:** Needs to exist before features that depend on config (colors, fonts, focus policy, menu items).
- Config file parser for `~/.config/wm2-born-again/config`
- Replace all Config.h #defines with Config struct fields
- SIGHUP handler for reload
- **Test:** Unit tests for config parsing. Integration test: modify config, SIGHUP, verify changes applied.

### Phase 6: EWMH Compliance
**Rationale:** Modern apps and panels expect _NET_* hints. Without them, WM is invisible to desktop infrastructure.
- Intern and set _NET_SUPPORTED, _NET_CLIENT_LIST, _NET_ACTIVE_WINDOW, _NET_WM_STATE
- Handle _NET_WM_WINDOW_TYPE for dock windows
- Set _NET_WM_PID
- **Test:** Xvfb integration: run wm2, verify `xprop -root _NET_SUPPORTED` returns expected atoms.

### Phase 7: Root Menu + Application Scanner
**Rationale:** The menu needs config and EWMH working. App scanner is independent logic.
- AppScanner class: parse .desktop files, cache results
- RootMenu class: X11 popup menu using scanned apps + config entries
- **Test:** Verify menu appears, items match installed apps, launching works.

### Phase 8: IPC + Config GUI
**Rationale:** Needs full Config system working. GUI is a separate binary, so it's additive.
- IPC server in WM (Unix domain socket, JSON protocol)
- Separate `wm2-config` binary using GTK3
- **Test:** Start WM, start config GUI, change a color, verify WM updates.

### Phase 9: Xrandr Support
**Rationale:** Multi-monitor awareness. Can defer because VPS typically has one virtual display.
- Query RandR for output geometry
- Handle RandR events for hotplug
- Replace hardcoded `DisplayWidth(display(), 0)`
- **Test:** Xvfb with multiple screens.

## Testing Strategy

### Tier 1: Unit Tests (No X11 Required)
Test RAII wrappers, Config parsing, AppScanner, IPC protocol encoding.
- Framework: Catch2 or doctest (header-only, lightweight)
- Run: Standard `ctest`, no display needed

### Tier 2: Integration Tests (Xvfb Required)
Test X11 event handling, client lifecycle, border rendering, focus model.
- Framework: Custom test harness that starts Xvfb, launches WM, creates test windows via Xlib
- Pattern:
  ```bash
  Xvfb :99 -screen 0 1024x768x24 &
  DISPLAY=:99 ./wm2-born-again &
  DISPLAY=:99 ./test_wm2_integration
  ```
- Use `xdotool` or direct Xlib calls in test code to simulate user actions
- Verify window positions, focus state, EWMH properties via `xprop`

### Tier 3: Visual Regression Tests
Verify border rendering and shaped tabs look correct.
- Xvfb + import/xwd to capture screenshots
- Pixel comparison against reference images
- Only run on CI, not during development

### CI Integration
```yaml
# GitHub Actions / GitLab CI
test:
  script:
    - apt-get install -y xvfb xdotool libx11-dev libxft-dev libxrandr-dev
    - cmake -B build && cmake --build build
    - cd build && xvfb-run ctest --output-on-failure
```

## Scalability Considerations

| Concern | At 5 windows | At 50 windows | At 200 windows |
|---------|-------------|----------------|-----------------|
| Client lookup | Linear scan fine | unordered_map essential | unordered_map essential |
| Event dispatch | Switch statement fine | Switch statement fine | Consider handler table |
| Border redraw | No concern | Expose event coalescing helps | Expose event coalescing critical |
| Memory | <1MB | <5MB | <20MB |
| Config reload | Instant | Instant | Instant |
| App scan | <10ms | <10ms | <10ms (scan is N*apps, not N*windows) |

Realistic target: This is a lightweight floating WM for VPS use. 5-20 windows is the typical range. 200 windows is not a target scenario.

## Sources

- i3 window manager hacking guide: https://i3wm.org/docs/4.0/hacking-howto.html (HIGH confidence -- official i3 documentation)
- i3 IPC interface specification: https://i3wm.org/docs/ipc.html (HIGH confidence -- official i3 documentation)
- RAII with unique_ptr custom deleters: https://biowpn.github.io/bioweapon/2024/03/05/raii-all-the-things.html (HIGH confidence -- well-documented C++ pattern)
- RAII with C APIs using unique_ptr: https://eklitzke.org/use-unique-ptr-with-c-apis (HIGH confidence -- established pattern)
- Xlib event handling reference: https://tronche.com/gui/x/xlib/event-handling/ (HIGH confidence -- official Xlib documentation)
- X11 poll-based event loop discussion: https://handmade.network/forums/t/7481-xlib_event_loop (MEDIUM confidence -- community discussion, verified against Xlib docs)
- Desktop entries specification (ArchWiki): https://wiki.archlinux.org/title/Desktop_entries (HIGH confidence -- references freedesktop.org spec)
- wmderland C++ WM architecture: https://github.com/aesophor/wmderland (MEDIUM confidence -- real-world C++ WM, but learning project)
- XCB tutorial (X.Org): https://www.x.org/releases/X11R7.7/doc/libxcb/tutorial/index.html (HIGH confidence -- official X.Org documentation)
- Config hot-reload pitfalls (Reddit discussion by WM author): https://www.reddit.com/r/rust/comments/couwju/runtime_configuration_reloading/ (MEDIUM confidence -- anecdotal but from WM developer)
- i3 testsuite documentation: https://i3wm.org/docs/testsuite.html (HIGH confidence -- official i3 documentation)
