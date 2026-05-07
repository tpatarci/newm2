# Phase 3: Client Lifecycle with RAII - Pattern Map

**Mapped:** 2026-05-07
**Files analyzed:** 9
**Analogs found:** 9 / 9

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `include/Manager.h` | model (class declaration) | CRUD | `include/Manager.h` (current) | self-modify |
| `src/Manager.cpp` | controller | CRUD + request-response | `src/Manager.cpp` (current) | self-modify |
| `include/Client.h` | model (class declaration) | CRUD | `include/Client.h` (current) | self-modify |
| `src/Client.cpp` | controller | CRUD + request-response | `src/Client.cpp` (current) | self-modify |
| `include/x11wrap.h` | utility (RAII wrapper) | resource lifecycle | `include/x11wrap.h` UniqueCursor class | exact |
| `src/Events.cpp` | controller | event-driven | `src/Events.cpp` (current) | self-modify |
| `src/Border.cpp` | controller | request-response | `src/Border.cpp` (current) | self-modify |
| `src/Buttons.cpp` | controller | request-response | `src/Buttons.cpp` (current) | self-modify |
| `tests/test_client.cpp` | test | request-response | `tests/test_smoke.cpp` | role-match |

## Pattern Assignments

### `include/Manager.h` (model, CRUD -- self-modify)

**Analog:** `include/Manager.h` lines 103-106 (current vector declaration)

**Current client list pattern** (lines 103-106):
```cpp
    // Client list (D-08: std::vector replaces listmacro2.h)
    std::vector<Client*> m_clients;
    std::vector<Client*> m_hiddenClients;
    Client *m_activeClient;
```

**Target pattern** (replace raw pointer vectors with unique_ptr + add hash map):
```cpp
    // Client list (D-01: unique_ptr ownership)
    std::vector<std::unique_ptr<Client>> m_clients;
    std::vector<std::unique_ptr<Client>> m_hiddenClients;
    std::unordered_map<Window, Client*> m_windowMap;  // D-05: O(1) lookup
    Client *m_activeClient;
```

**Required includes to add** (top of file, near existing `<memory>`):
```cpp
#include <unordered_map>
```

**hide/unhide method changes** (lines 76-78, current):
```cpp
    void addToHiddenList(Client *);
    void removeFromHiddenList(Client *);
```
These become unique_ptr move operations. The signatures stay the same (raw Client* in, internal unique_ptr move).

---

### `src/Manager.cpp` (controller, CRUD -- self-modify)

**Analog:** `src/Manager.cpp` lines 386-399 (current windowToClient)

**Current lookup pattern** (lines 386-399):
```cpp
Client *WindowManager::windowToClient(Window w, bool create)
{
    if (w == 0) return nullptr;

    for (auto *c : m_clients) {
        if (c->hasWindow(w)) return c;
    }

    if (!create) return nullptr;

    Client *newC = new Client(this, w);
    m_clients.push_back(newC);
    return newC;
}
```

**Target: map-first lookup with hasWindow fallback** -- replaces linear scan with:
1. `m_windowMap.find(w)` for O(1) client window lookup
2. Linear scan of both `m_clients` and `m_hiddenClients` for border window fallback
3. `std::make_unique<Client>(this, w)` instead of raw `new`
4. Register in map: `m_windowMap[raw->window()] = raw;`

**Current hidden list management** (lines 458-475):
```cpp
void WindowManager::addToHiddenList(Client *c)
{
    for (auto *hc : m_hiddenClients) {
        if (hc == c) return;
    }
    m_hiddenClients.push_back(c);
}

void WindowManager::removeFromHiddenList(Client *c)
{
    for (size_t i = 0; i < m_hiddenClients.size(); ++i) {
        if (m_hiddenClients[i] == c) {
            m_hiddenClients.erase(m_hiddenClients.begin() + static_cast<ptrdiff_t>(i));
            return;
        }
    }
}
```

**Target: unique_ptr moves** -- hide moves from m_clients to m_hiddenClients, unhide moves back. Use `std::find_if` with lambda comparing `up.get() == c`, then `std::move(*it)` into target vector and `erase(it)` from source. Map does NOT need updating on move (raw pointer value is stable).

**Current release() cleanup** (lines 135-163):
```cpp
void WindowManager::release()
{
    // ...
    for (auto *c : m_clients) { /* ... */ }
    m_clients.clear();
    for (auto *c : unparentList) {
        c->unreparent();
        c->release();
    }
    // ...
}
```

**Target: unique_ptr iteration** -- range-for over `m_clients` uses `const auto& up` to access `up.get()`. Calls `c->unreparent()` then lets vector clear destroy unique_ptrs which triggers `~Client()`.

**Current skipInRevert** (lines 448-455):
```cpp
void WindowManager::skipInRevert(Client *c, Client *myRevert)
{
    for (auto *client : m_clients) {
        if (client != c && client->revertTo() == c) {
            client->setRevertTo(myRevert);
        }
    }
}
```

**Target: unique_ptr iteration** -- `for (const auto& client : m_clients)` with `client.get()` for pointer comparison.

---

### `include/Client.h` (model, CRUD -- self-modify)

**Analog:** `include/Client.h` lines 118-131 (current state and colormap members)

**Current state member** (line 118):
```cpp
    int m_state;
```

**Target: enum class ClientState** (add before Client class):
```cpp
enum class ClientState {
    Withdrawn = 0,   // matches X11 WithdrawnState
    Normal    = 1,   // matches X11 NormalState
    Iconic    = 3    // matches X11 IconicState
};
```

**State query methods** (lines 31-33, current):
```cpp
    bool isHidden()     const { return m_state == IconicState; }
    bool isWithdrawn()  const { return m_state == WithdrawnState; }
    bool isNormal()     const { return m_state == NormalState; }
```

**Target: enum comparisons**:
```cpp
    bool isHidden()     const { return m_state == ClientState::Iconic; }
    bool isWithdrawn()  const { return m_state == ClientState::Withdrawn; }
    bool isNormal()     const { return m_state == ClientState::Normal; }
```

**Current colormap members** (lines 128-131):
```cpp
    Colormap m_colormap;
    int m_colormapWinCount;
    Window *m_colormapWindows;
    Colormap *m_windowColormaps;
```

**Target: RAII vectors**:
```cpp
    Colormap m_colormap;
    std::vector<Window> m_colormapWindows;
    std::vector<Colormap> m_windowColormaps;
```

**setState/getState signature** (lines 137-138, current):
```cpp
    bool getState(int *state);
    void setState(int state);
```

**Target: enum-aware signatures**:
```cpp
    bool getState(int *state);           // X11 boundary, keeps int*
    void setState(ClientState state);    // internal, uses enum
    void setState(int state);            // X11 boundary overload
```

**Required includes to add** (near existing `<vector>`):
```cpp
#include <vector>   // already included via Manager.h
```

---

### `src/Client.cpp` (controller, CRUD -- self-modify)

**Analog:** `src/Client.cpp` lines 342-352 (current setState), lines 429-474 (current getColormaps), lines 66-90 (current release)

**Current setState** (lines 342-352):
```cpp
void Client::setState(int state)
{
    m_state = state;

    long data[2];
    data[0] = static_cast<long>(state);
    data[1] = static_cast<long>(None);

    XChangeProperty(display(), m_window, Atoms::wm_state, Atoms::wm_state,
                    32, PropModeReplace, reinterpret_cast<unsigned char*>(data), 2);
}
```

**Target: validated state transition with enum**:
```cpp
void Client::setState(ClientState state)
{
    if (!isValidTransition(m_state, state)) {
        std::fprintf(stderr, "wm2: warning: invalid state transition %d->%d "
                     "for window 0x%lx\n",
                     static_cast<int>(m_state), static_cast<int>(state), m_window);
    }
    m_state = state;

    long data[2];
    data[0] = static_cast<long>(state);  // enum values match X11 constants
    data[1] = static_cast<long>(None);

    XChangeProperty(display(), m_window, Atoms::wm_state, Atoms::wm_state,
                    32, PropModeReplace, reinterpret_cast<unsigned char*>(data), 2);
}

// X11 boundary overload
void Client::setState(int state)
{
    setState(static_cast<ClientState>(state));
}
```

**Current getColormaps** (lines 429-474):
```cpp
void Client::getColormaps()
{
    int n;
    Window *cw;
    // ... (reads from X11 property)
    if (m_colormapWinCount != 0) {
        XFree(m_colormapWindows);
        free(m_windowColormaps);
    }
    // ... manually manages malloc/free arrays
}
```

**Target: vector-based colormaps**:
```cpp
void Client::getColormaps()
{
    Window *cw;
    XWindowAttributes attr;

    if (!m_managed) {
        XGetWindowAttributes(display(), m_window, &attr);
        m_colormap = attr.colormap;
    }

    int n = getProperty_aux(display(), m_window, Atoms::wm_colormaps, XA_WINDOW,
                            100L, reinterpret_cast<unsigned char**>(&cw));

    if (n <= 0) {
        m_colormapWindows.clear();
        m_windowColormaps.clear();
        return;
    }

    // Copy from X11 memory into vector, then XFree
    m_colormapWindows.assign(cw, cw + n);
    XFree(cw);

    m_windowColormaps.resize(n);
    for (int i = 0; i < n; ++i) {
        if (m_colormapWindows[i] == m_window) {
            m_windowColormaps[i] = m_colormap;
        } else {
            XSelectInput(display(), m_colormapWindows[i], ColormapChangeMask);
            XGetWindowAttributes(display(), m_colormapWindows[i], &attr);
            m_windowColormaps[i] = attr.colormap;
        }
    }
}
```

**Current release/destructor** (lines 58-90):
```cpp
Client::~Client()
{
    if (m_window != None) {
        release();  // safety net if caller forgot
    }
}

void Client::release()
{
    if (m_window == None) return;
    // ... cleanup ...
    m_window = None;
}
```

**Target: destructor is the sole cleanup path**. Remove `release()`. The destructor calls unreparent (if not withdrawn), resets border, frees colormap vectors (automatic via std::vector dtor), and deactivates if active. WindowManager clears focus/map BEFORE erasing the unique_ptr.

**Constructor initializer list** (lines 19-39) changes:
- `m_state(WithdrawnState)` becomes `m_state(ClientState::Withdrawn)`
- `m_colormapWinCount(0)`, `m_colormapWindows(nullptr)`, `m_windowColormaps(nullptr)` are removed (std::vector default-constructs empty)

**eventMapRequest switch** (lines 967-989) changes:
- `case WithdrawnState:` becomes `case ClientState::Withdrawn:`
- Same for NormalState and IconicState

**eventUnmap switch** (lines 1044-1056) changes similarly.

---

### `include/x11wrap.h` (utility, resource lifecycle)

**Analog:** `include/x11wrap.h` lines 52-100 (UniqueCursor class) -- exact same pattern for ServerGrab

**UniqueCursor RAII pattern** (lines 52-100, structural template):
```cpp
class UniqueCursor {
public:
    UniqueCursor() noexcept : m_display(nullptr), m_cursor(None) {}
    UniqueCursor(Display* d, Cursor c) noexcept : m_display(d), m_cursor(c) {}

    UniqueCursor(UniqueCursor&& o) noexcept
        : m_display(o.m_display), m_cursor(o.m_cursor) {
        o.m_display = nullptr;
        o.m_cursor = None;
    }
    UniqueCursor& operator=(UniqueCursor&& o) noexcept {
        if (this != &o) {
            reset();
            m_display = o.m_display;
            m_cursor = o.m_cursor;
            o.m_display = nullptr;
            o.m_cursor = None;
        }
        return *this;
    }

    UniqueCursor(const UniqueCursor&) = delete;
    UniqueCursor& operator=(const UniqueCursor&) = delete;

    ~UniqueCursor() { reset(); }

    void reset() noexcept {
        if (m_display && m_cursor != None) {
            XFreeCursor(m_display, m_cursor);
        }
        m_display = nullptr;
        m_cursor = None;
    }
    // ...
};
```

**ServerGrab class to add** (after UniqueColormap, before `} // namespace x11`):
```cpp
class ServerGrab {
public:
    explicit ServerGrab(Display* d) noexcept : m_display(d) {
        if (m_display) XGrabServer(m_display);
    }

    ~ServerGrab() {
        if (m_display) XUngrabServer(m_display);
    }

    ServerGrab(const ServerGrab&) = delete;
    ServerGrab& operator=(const ServerGrab&) = delete;

private:
    Display* m_display;
};
```

Key differences from UniqueCursor: no move semantics needed (short-lived scope guard), no get()/release() (no resource to retrieve), constructor parameter is only Display*.

---

### `src/Border.cpp` (controller, request-response -- self-modify)

**Analog:** `src/Border.cpp` lines 714-717 (current reparent method)

**Current reparent** (lines 714-717):
```cpp
void Border::reparent()
{
    XReparentWindow(display(), m_child, m_parent, xIndent(), yIndent());
}
```

**Target: wrap with ServerGrab RAII**:
```cpp
void Border::reparent()
{
    x11::ServerGrab grab(display());
    XReparentWindow(display(), m_child, m_parent, xIndent(), yIndent());
}
```

This is the minimal change per D-08 -- XGrabServer only around XReparentWindow, not the entire manage() flow.

**Required include**: Already has access to x11wrap.h via Border.h.

---

### `src/Events.cpp` (controller, event-driven -- self-modify)

**Analog:** `src/Events.cpp` lines 236-260 (current eventDestroy)

**Current eventDestroy** (lines 236-260):
```cpp
void WindowManager::eventDestroy(XDestroyWindowEvent *e)
{
    Client *c = windowToClient(e->window);

    if (c) {
        if (m_focusChanging && c == m_focusCandidate) {
            stopConsideringFocus();
            m_focusCandidate = nullptr;
        }

        // Remove from client list
        for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
            if (*it == c) {
                m_clients.erase(it);
                break;
            }
        }

        c->release();

        ignoreBadWindowErrors = true;
        XSync(display(), false);
        ignoreBadWindowErrors = false;
    }
}
```

**Target: unique_ptr erase with map maintenance**:
1. Clear focus tracking BEFORE erase
2. Remove from m_windowMap
3. Find in either m_clients or m_hiddenClients using `std::find_if`
4. Erase the unique_ptr (triggers ~Client() destructor)
5. XSync with ignoreBadWindowErrors

The critical ordering: focus/map cleanup first, then erase (which destroys the Client). No `c->release()` call -- the destructor handles everything.

---

### `src/Buttons.cpp` (controller, request-response -- self-modify)

**Analog:** `src/Buttons.cpp` lines 33-65 (current circulate), lines 101-282 (current menu)

**Current circulate iteration** (lines 44-51):
```cpp
            for (size_t idx = 0; idx < m_clients.size(); ++idx) {
                if (m_clients[idx] == m_activeClient) {
                    i = static_cast<int>(idx);
                    break;
                }
            }
```
and (lines 54-62):
```cpp
        for (j = i + 1;
             !m_clients[j]->isNormal() || m_clients[j]->isTransient();
             ++j) {
```

**Target: unique_ptr dereference** -- `m_clients[idx]` returns `unique_ptr<Client>&`. Use `.get()` for pointer comparison with m_activeClient. Method calls (`->isNormal()`, `->isTransient()`) work unchanged via unique_ptr's `operator->`.

**Current menu hidden list iteration** (lines 108-110):
```cpp
    for (auto *hc : m_hiddenClients) {
        clients.push_back(hc);
    }
```

**Target: unique_ptr iteration**:
```cpp
    for (const auto& hc : m_hiddenClients) {
        clients.push_back(hc.get());
    }
```

---

### `tests/test_client.cpp` (test, request-response -- NEW FILE)

**Analog:** `tests/test_smoke.cpp` (existing Xvfb-based test with Catch2)

**Test pattern from test_smoke.cpp** (lines 1-12):
```cpp
#include <catch2/catch_test_macros.hpp>
#include "x11wrap.h"
#include <X11/Xlib.h>
#include <cstdlib>
#include <string>

TEST_CASE("Xvfb display is available", "[smoke]") {
    const char* display = std::getenv("DISPLAY");
    REQUIRE(display != nullptr);
}
```

**Additional analog:** `tests/test_raii.cpp` (Catch2 unit test structure):
```cpp
#include <catch2/catch_test_macros.hpp>
#include "x11wrap.h"
#include <type_traits>

TEST_CASE("DisplayDeleter handles null safely", "[raii][display]") {
    x11::DisplayDeleter deleter;
    Display* raw = nullptr;
    REQUIRE_NOTHROW(deleter(raw));
}
```

**Target test structure for test_client.cpp**:
- Uses Catch2 with `TEST_CASE` macros
- Tagged with `[client]` and sub-tags like `[client][state]`, `[client][lifecycle]`
- Requires Xvfb (DISPLAY=:99) -- CMake setup follows test_smoke.cpp pattern
- Tests: ServerGrab grab/release, enum class ClientState transitions, windowToClient O(1) lookup, unique_ptr ownership lifecycle
- Integration tests need a full WindowManager or at minimum an X11 connection to create windows

**CMakeLists.txt addition** (after existing test targets, following test_smoke pattern):
```cmake
    add_executable(test_client tests/test_client.cpp)
    target_link_libraries(test_client PRIVATE
        Catch2::Catch2WithMain
        PkgConfig::X11
    )
    target_include_directories(test_client PRIVATE ${CMAKE_SOURCE_DIR}/include)
    catch_discover_tests(test_client
        PROPERTIES
            ENVIRONMENT "DISPLAY=:99"
            FIXTURES_REQUIRED xvfb_display
    )
```

---

## Shared Patterns

### unique_ptr vector iteration
**Source:** `src/Manager.cpp` (new pattern replacing raw pointer iteration)
**Apply to:** All files that iterate over m_clients or m_hiddenClients (Manager.cpp, Events.cpp, Buttons.cpp)
```cpp
// Iteration (read-only, borrowing raw pointer):
for (const auto& up : m_clients) {
    Client* c = up.get();
    // use c as before
}

// Pointer comparison:
if (up.get() == targetRawPtr) { ... }

// Method calls work directly via unique_ptr operator->:
up->isNormal();
up->activate();
```

### RAII resource wrapper pattern
**Source:** `include/x11wrap.h` lines 52-100 (UniqueCursor)
**Apply to:** ServerGrab class (new), all future RAII wrappers
```cpp
// Pattern: constructor acquires, destructor releases, delete copy, allow move
class ClassName {
public:
    explicit ClassName(Display* d) noexcept : m_display(d) { /* acquire */ }
    ~ClassName() { /* release */ }
    ClassName(const ClassName&) = delete;
    ClassName& operator=(const ClassName&) = delete;
    // Move if ownership transfer makes sense (not for ServerGrab)
private:
    Display* m_display;
};
```

### Error/warning logging
**Source:** `src/Client.cpp` lines 274-276, `src/Manager.cpp` lines 168-171
**Apply to:** setState validation warnings, all new error paths
```cpp
std::fprintf(stderr, "wm2: warning: descriptive message with details\n");
// or for fatal:
m_windowManager->fatal("descriptive message");
```

### X11 error suppression during cleanup
**Source:** `src/Events.cpp` lines 256-258, `src/Client.cpp` lines 566-568
**Apply to:** eventDestroy (after Client destruction), unreparent (after XReparentWindow)
```cpp
ignoreBadWindowErrors = true;
XSync(display(), false);
ignoreBadWindowErrors = false;
```

### Test CMake pattern with Xvfb fixture
**Source:** `CMakeLists.txt` lines 67-96
**Apply to:** test_client target (new)
```cmake
add_executable(test_client tests/test_client.cpp)
target_link_libraries(test_client PRIVATE Catch2::Catch2WithMain PkgConfig::X11)
target_include_directories(test_client PRIVATE ${CMAKE_SOURCE_DIR}/include)
catch_discover_tests(test_client
    PROPERTIES ENVIRONMENT "DISPLAY=:99" FIXTURES_REQUIRED xvfb_display)
```

## No Analog Found

All files in this phase are modifications to existing files or follow established patterns (RAII class follows UniqueCursor, test file follows test_smoke.cpp, CMake entry follows existing test targets). No files lack an analog.

## Metadata

**Analog search scope:** `include/`, `src/`, `tests/`, `CMakeLists.txt`
**Files scanned:** 14 source + header files, 4 test files, 1 CMakeLists.txt
**Pattern extraction date:** 2026-05-07
