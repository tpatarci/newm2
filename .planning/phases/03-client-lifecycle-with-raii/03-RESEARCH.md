# Phase 3: Client Lifecycle with RAII - Research

**Researched:** 2026-05-07
**Domain:** X11 window manager client lifecycle, RAII ownership, ICCCM compliance
**Confidence:** HIGH

## Summary

Phase 3 replaces the raw-pointer client management with a proper RAII ownership model using `std::unique_ptr<Client>`, adds O(1) window lookup via `std::unordered_map`, wraps XGrabServer in an RAII guard for ICCCM-compliant atomic reparenting, introduces a type-safe `enum class ClientState` with validation, replaces raw `malloc`/`free` colormap arrays with `std::vector`, and adds automated tests for core WM operations.

The current codebase already has significant groundwork: `std::vector<Client*>` replaced listmacro2.h (CLNT-03 done), `std::unique_ptr<Border>` already owns the border object inside Client, and `FdGuard` in Manager.h demonstrates the RAII pattern that ServerGrab will follow. The `release()` method still calls `delete this` (inherited from upstream), which this phase eliminates.

**Primary recommendation:** The phase is primarily a mechanical refactoring of client ownership. The most complex aspect is ensuring all code paths that currently hold raw `Client*` (event handlers, focus tracking, revert chains, circulate, menu) remain safe under the unique_ptr regime. Since the WM is single-threaded, raw `Client*` borrowing is safe as long as the unique_ptr is not erased while the borrowed pointer is in use -- which the event loop's synchronous dispatch guarantees.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** `std::vector<std::unique_ptr<Client>>` for m_clients and m_hiddenClients. WindowManager exclusively owns all Client objects. No shared_ptr, no manual new/delete.
- **D-02:** Moving a Client between visible and hidden lists transfers the unique_ptr (hide: move from m_clients to m_hiddenClients; unhide: move back). Zero duplication, single ownership at all times.
- **D-03:** Client cleanup stays in the destructor (unreparent, colormap free, deactivate). No separate release() method -- the dtor is the single cleanup path. WindowManager triggers destruction by erasing the unique_ptr from the vector.
- **D-04:** Raw `Client*` for all borrowing (event handlers, focus tracking, revert chains). No observer_ptr wrapper -- standard non-owning pointer pattern. Borrowers never outlive the unique_ptr owner because the event loop is single-threaded.
- **D-05:** `std::unordered_map<Window, Client*>` keyed by client window ID only (m_window). Border window lookups fall back to linear scan of vector checking `hasWindow()`. Border events are rare -- fallback is acceptable.
- **D-06:** Map stores raw `Client*` (non-owning). Updated when unique_ptrs move between m_clients and m_hiddenClients, and on client creation/destruction. The map is a lookup accelerator, not an owner.
- **D-07:** `windowToClient()` checks map first (O(1) for client windows). On miss, falls back to linear scan through both vectors checking `hasWindow()` (for border window events).
- **D-08:** Tight XGrabServer/XUngrabServer scope -- only around the XReparentWindow call in Border::reparent(). Not the entire manage() flow. Minimal X server stall.
- **D-09:** RAII `ServerGrab` class in x11wrap.h alongside existing RAII types. Constructor calls XGrabServer, destructor calls XUngrabServer. Exception-safe, no chance of forgotten ungrab.
- **D-10:** `enum class ClientState { Withdrawn, Normal, Iconic }` replaces raw int state. All internal code uses the enum. Conversion to/from X11 int values only at the X11 boundary (WM_STATE property reads/writes).
- **D-11:** State transitions validated in setState(). Invalid transitions log a warning via `fprintf(stderr)` and apply anyway. Matches upstream wm2's permissive approach -- warnings catch logic bugs without crashing the WM.
- **D-12:** Valid transitions: Withdrawn->Normal, Normal->Iconic, Iconic->Normal, Normal->Withdrawn, Iconic->Withdrawn. Redundant transitions (Normal->Normal) are logged but accepted.
- **D-13:** Replace raw `malloc`/`free` colormap arrays (`m_colormapWindows`, `m_windowColormaps`) with `std::vector<Window>` and `std::vector<Colormap>`. Consistent with C++17 RAII pattern established in Phase 1.

### Claude's Discretion
- Exact structure of the RAII ServerGrab class (constructor params, placement in x11wrap.h)
- Whether m_hiddenClients should also be `vector<unique_ptr<Client>>` or clients should stay in m_clients with state checking (move-based approach is preferred per D-02)
- Test structure and naming conventions for Phase 3 tests
- Implementation details of unordered_map integration with existing event dispatch

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| CLNT-01 | ICCCM-compliant reparenting using XGrabServer/XUngrabServer | ServerGrab RAII class (D-09), tight scope around XReparentWindow only (D-08). Border::reparent() is the single call site. |
| CLNT-02 | Replace `delete this` with proper lifecycle management via WindowManager | unique_ptr ownership (D-01), destructor-only cleanup (D-03), WindowManager::eventDestroy erases from vector to trigger destruction. |
| CLNT-03 | Replace custom listmacro2.h with std::vector<Client*> | Already done -- current code uses `std::vector<Client*>`. This requirement is satisfied but the upgrade to `std::vector<std::unique_ptr<Client>>` is in scope via D-01. |
| CLNT-04 | O(1) client lookup by window ID (hash map instead of linear scan) | std::unordered_map<Window, Client*> (D-05, D-06), map-first lookup with hasWindow() fallback (D-07). |
| CLNT-05 | Client state transitions are correct (Withdrawn -> Normal -> Iconic -> Withdrawn) | enum class ClientState (D-10), validation in setState() (D-11), valid transition set (D-12). |
| TEST-03 | Core WM operations tested: window map, move, resize, hide/unhide, delete | New test file test_client.cpp with Xvfb fixture, testing lifecycle operations against X11 windows. |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Client ownership/lifecycle | API / Backend (WindowManager) | -- | WindowManager exclusively owns Client objects; it is the single point of creation and destruction |
| O(1) window lookup | API / Backend (WindowManager) | -- | Hash map lives in WindowManager, accelerates event dispatch |
| ICCCM reparenting atomicity | API / Backend (Border) | -- | Border::reparent() performs the XReparentWindow call; ServerGrab wraps it |
| State machine validation | API / Backend (Client) | -- | Client tracks its own state; setState validates transitions |
| Colormap resource management | API / Backend (Client) | -- | Client owns colormap arrays; RAII via std::vector |
| Lifecycle testing | Test Infrastructure | -- | test_client.cpp on Xvfb verifies all operations |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| std::unique_ptr | C++17 | Client ownership | Zero-overhead RAII, move semantics for list transfers, custom deleter support already proven in Phase 1 |
| std::unordered_map | C++17 | O(1) Window->Client lookup | Hash map with Window (unsigned long) as key; X11 Window IDs are stable for the lifetime of the window |
| std::vector | C++17 | Colormap arrays, client lists | Already used throughout codebase; geometric growth, RAII cleanup |
| Catch2 | 3.14.0 | Test framework | Already configured in CMake, Xvfb fixtures established in Phase 1 |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| Xvfb | 21.1.11 | Headless X server for testing | TEST-03 lifecycle tests need a real X server for window creation/destruction |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| std::unordered_map<Window, Client*> | std::unordered_map<Window, size_t> (index into vector) | Index invalidation on vector erase is dangerous; raw Client* is simpler and safe in single-threaded context |
| Separate map for border windows | Single map + hasWindow() fallback | Border events are rare; adding 4 more map entries per client wastes memory for little gain [ASSUMED] |

**Installation:**
No new packages needed. All types are in the C++17 standard library already linked.

**Version verification:**
```
g++ 13.3.0 (supports C++17 fully)
cmake 3.28.3
Catch2 v3.14.0 (fetched via FetchContent in CMakeLists.txt)
Xvfb 21.1.11 (X.Org)
```

## Architecture Patterns

### System Architecture Diagram

```
X11 Events --> WindowManager::eventDispatch
                    |
                    v
            windowToClient(w)
              |          |
              v          v
        unordered_map   linear scan
        (O(1) client    (hasWindow()
         window IDs)     for border wins)
              |          |
              v          v
           Client* raw pointer (borrowed)
              |
              v
    Client event handlers
    (eventMapRequest, eventUnmap, etc.)
              |
              v
    Client::setState(ClientState)
    (validated transition, warning on invalid)

WindowManager owns:
  m_clients:  vector<unique_ptr<Client>>  ---+
  m_hiddenClients: vector<unique_ptr<Client>> |--> all Client objects
  m_windowMap: unordered_map<Window, Client*> ---+--> lookup accelerator

Client owns:
  m_border: unique_ptr<Border>
  m_colormapWindows: vector<Window>      (D-13)
  m_windowColormaps: vector<Colormap>    (D-13)

Border::reparent():
  {
    ServerGrab grab(display());  // RAII: XGrabServer
    XReparentWindow(...);
  }  // ~ServerGrab: XUngrabServer
```

### Recommended Project Structure
```
include/
  x11wrap.h          # ServerGrab class added here
  Client.h           # enum class ClientState, vector<Colormap> members
  Manager.h          # unique_ptr<Client>, unordered_map members
src/
  Client.cpp         # setState validation, destructor as sole cleanup, vector colormap
  Manager.cpp        # windowToClient with map, unique_ptr lifecycle, eventDestroy rewrite
  Events.cpp         # eventDestroy uses unique_ptr erase, map maintenance
  Border.cpp         # ServerGrab around reparent()
  Buttons.cpp        # circulate/menu iterate unique_ptr vectors
tests/
  test_client.cpp    # NEW: TEST-03 lifecycle tests (map, hide, unhide, delete)
```

### Pattern 1: unique_ptr Ownership with Raw Pointer Borrowing
**What:** WindowManager owns Client objects via `std::unique_ptr<Client>`. All other code borrows raw `Client*`.
**When to use:** Throughout the event dispatch and client interaction code.
**Example:**
```cpp
// In Manager.h:
std::vector<std::unique_ptr<Client>> m_clients;
std::unordered_map<Window, Client*> m_windowMap;

// Getting a borrowed pointer:
Client* c = m_windowMap[w];  // raw non-owning pointer

// Transferring ownership (hide/unhide):
auto it = std::find_if(m_clients.begin(), m_clients.end(),
    [c](const auto& up) { return up.get() == c; });
if (it != m_clients.end()) {
    m_hiddenClients.push_back(std::move(*it));
    m_clients.erase(it);
}

// Destroying a client:
auto it = std::find_if(m_clients.begin(), m_clients.end(),
    [c](const auto& up) { return up.get() == c; });
if (it != m_clients.end()) {
    m_windowMap.erase(c->window());
    m_clients.erase(it);  // unique_ptr destructor runs ~Client()
}
```

### Pattern 2: RAII ServerGrab
**What:** Scoped guard that grabs the X server on construction and ungrabs on destruction.
**When to use:** Only around XReparentWindow calls for ICCCM-compliant atomic reparenting.
**Example:**
```cpp
// In x11wrap.h:
class ServerGrab {
public:
    explicit ServerGrab(Display* d) : m_display(d) {
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

// In Border::reparent():
void Border::reparent() {
    ServerGrab grab(display());  // grabs server
    XReparentWindow(display(), m_child, m_parent, xIndent(), yIndent());
}  // ~ServerGrab ungrabs server -- even if XReparentWindow throws
```

### Pattern 3: Type-Safe State Machine
**What:** `enum class ClientState` with validation in setState, warning on invalid transitions.
**When to use:** Every state change in Client.
**Example:**
```cpp
enum class ClientState { Withdrawn, Normal, Iconic };

// In Client.cpp:
static bool isValidTransition(ClientState from, ClientState to) {
    if (from == to) return true;  // redundant but accepted
    switch (from) {
    case ClientState::Withdrawn: return to == ClientState::Normal;
    case ClientState::Normal:    return to == ClientState::Iconic ||
                                         to == ClientState::Withdrawn;
    case ClientState::Iconic:    return to == ClientState::Normal ||
                                         to == ClientState::Withdrawn;
    }
    return false;
}

void Client::setState(ClientState state) {
    if (!isValidTransition(m_state, state)) {
        std::fprintf(stderr, "wm2: warning: invalid state transition %d->%d for window 0x%lx\n",
                     static_cast<int>(m_state), static_cast<int>(state), m_window);
    }
    m_state = state;
    // X11 boundary: write WM_STATE property with int value
    long data[2] = { static_cast<long>(state), static_cast<long>(None) };
    XChangeProperty(display(), m_window, Atoms::wm_state, Atoms::wm_state,
                    32, PropModeReplace, reinterpret_cast<unsigned char*>(data), 2);
}
```

### Anti-Patterns to Avoid
- **Storing unique_ptr::get() in the map without updating on move:** When a unique_ptr moves between m_clients and m_hiddenClients, the raw pointer value does not change, so the map entry remains valid. But the map must be updated when a Client is destroyed (erased from the map).
- **Using std::remove_if with unique_ptr vectors:** `std::remove_if` moves elements but does not erase them, meaning unique_ptrs get moved to the end but not destroyed. Always use erase-remove idiom: `vec.erase(std::remove_if(...), vec.end())`.
- **Accessing Client members after erasing from vector:** Once the unique_ptr is erased from the vector, `~Client()` runs immediately. The borrowed `Client*` is dangling. All cleanup must happen in the destructor, not after the erase point.
- **Calling setState before X11 property write:** The state must be set and the WM_STATE property written atomically. The setState method does both.
- **Grabbing the server for the entire manage() flow:** D-08 explicitly limits XGrabServer to only around XReparentWindow. The server grab stalls all X clients -- extending it to the entire manage() would freeze the display during font loading, color allocation, etc.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Window lookup | Custom hash table | std::unordered_map<Window, Client*> | Standard library, well-tested, O(1) amortized |
| Client memory management | manual new/delete, release() calling delete this | std::unique_ptr<Client> | Automatic cleanup, no double-free, exception-safe |
| Colormap arrays | malloc/free with manual count tracking | std::vector<Window>, std::vector<Colormap> | Automatic memory management, bounds-safe, no manual free |
| Server grab/ungrab pairing | Manual XGrabServer/XUngrabServer calls | RAII ServerGrab class | Exception-safe, no forgotten ungrab, follows FdGuard pattern |
| State validation | Manual int comparisons scattered in code | enum class + transition table in setState | Type-safe, centralized, self-documenting |

**Key insight:** Every custom memory management pattern in this phase (raw new/delete, malloc/free arrays, manual resource pairing) has a direct C++17 standard library replacement. The complexity is in the transition, not the target.

## Common Pitfalls

### Pitfall 1: unique_ptr erase order -- dangling pointer in event handlers
**What goes wrong:** `eventDestroy` erases the unique_ptr from the vector, destroying the Client. But if focus tracking (m_activeClient, m_focusCandidate, m_revert chain) still holds a raw pointer to that Client, it becomes dangling.
**Why it happens:** The current code calls `c->release()` then removes from the vector. With unique_ptr, erasing from the vector IS the destruction.
**How to avoid:** In `eventDestroy`, clear focus-related pointers BEFORE erasing the unique_ptr. The existing code already does `stopConsideringFocus()` and `setActiveClient(nullptr)` in release() -- these must happen before the erase, not in the destructor.
**Warning signs:** Use-after-free crashes in event handlers after a window is destroyed.

### Pitfall 2: circulate() uses integer index into m_clients
**What goes wrong:** `Buttons.cpp:circulate()` uses `m_clients[j]` with wrap-around indexing. With `unique_ptr<Client>`, `m_clients[j]` returns `unique_ptr<Client>&`, which is fine for reading. But the index-based access assumes stable ordering -- if the vector is modified during iteration, indices become invalid.
**Why it happens:** circulate() was written for raw pointer vectors where indices are always valid.
**How to avoid:** circulate() only reads, never modifies the vector during its loop. It calls `activateAndWarp()` after the loop exits, which modifies focus but not the vector. This is safe as-is.
**Warning signs:** Out-of-bounds access or skipping clients during circulate.

### Pitfall 3: Map not updated on hide/unhide transfers
**What goes wrong:** When a Client is hidden (unique_ptr moved from m_clients to m_hiddenClients), the map entry still points to the same Client object (raw pointer value unchanged). But if the map is keyed only by m_window, and the Client is not destroyed, the map entry remains valid.
**Why it happens:** Forgetting that the map is an accelerator for lookups, not a record of which vector owns the Client.
**How to avoid:** D-06 says map is updated "when unique_ptrs move between m_clients and m_hiddenClients." But since the raw Client* pointer value does not change during a move, the map entry does NOT need updating for hide/unhide. It only needs updating for creation (add entry) and destruction (remove entry).
**Warning signs:** windowToClient() returning stale pointers or missing entries.

### Pitfall 4: Colormap array lifetime mismatch with X11 property reads
**What goes wrong:** `getColormaps()` currently frees old arrays then allocates new ones. With `std::vector`, old vectors are replaced by assignment, which is safe. But the function reads from `cw` (XFree'd pointer from getProperty_aux) into the new vector -- this order must be preserved.
**Why it happens:** The XGetWindowProperty returns data that must be XFree'd. The current code stores the XFree-managed pointer directly as m_colormapWindows.
**How to avoid:** With `std::vector<Window>`, copy data from the X11 property into the vector, then XFree the X11 pointer. Do NOT store the X11-allocated pointer in the vector.
**Warning signs:** Double-free or use-after-free in getColormaps().

### Pitfall 5: setState int values do not match X11 WM_STATE constants
**What goes wrong:** X11 defines `WithdrawnState=0`, `NormalState=1`, `IconicState=3`. If the enum values don't match, the WM_STATE property will be written with wrong values.
**Why it happens:** C++ enum class values default to 0,1,2,... but X11 uses 0,1,3.
**How to avoid:** Explicitly assign enum values to match X11: `enum class ClientState { Withdrawn=0, Normal=1, Iconic=3 }`. This eliminates any conversion at the X11 boundary.
**Warning signs:** Clients not appearing or appearing in wrong state after WM restart.

### Pitfall 6: ~Client() calling unreparent() triggers ReparentNotify event
**What goes wrong:** The Client destructor calls unreparent(), which calls XReparentWindow. This generates a ReparentNotify event. If the event loop processes this event after the Client is destroyed, it will try to look up a non-existent Client.
**Why it happens:** X11 events are asynchronous. By the time ReparentNotify arrives, the Client object is gone.
**How to avoid:** The current `windowToClient()` handles this gracefully -- it returns nullptr for unknown windows, and `eventReparent()` in Events.cpp calls `windowToClient(e->window, true)` which creates a new Client. With the map, the lookup for the old m_window will miss (map entry erased before destruction), and a new Client will be created. This matches upstream behavior where `eventReparent` is a no-op for already-managed windows.
**Warning signs:** Spurious Client creation after destruction, or crashes in ReparentNotify handling.

## Code Examples

### Creating a Client with unique_ptr + map registration
```cpp
// In Manager.cpp windowToClient():
Client* WindowManager::windowToClient(Window w, bool create)
{
    if (w == 0) return nullptr;

    // O(1) lookup via hash map
    auto it = m_windowMap.find(w);
    if (it != m_windowMap.end()) return it->second;

    // Fallback: linear scan for border windows (rare)
    auto checkVector = [&](const auto& vec) -> Client* {
        for (const auto& c : vec) {
            if (c->hasWindow(w)) return c.get();
        }
        return nullptr;
    };

    Client* found = checkVector(m_clients);
    if (!found) found = checkVector(m_hiddenClients);
    if (found || !create) return found;

    // Create new client
    auto newClient = std::make_unique<Client>(this, w);
    Client* raw = newClient.get();
    m_clients.push_back(std::move(newClient));
    m_windowMap[raw->window()] = raw;
    return raw;
}
```

### Destroying a Client (eventDestroy)
```cpp
// In Events.cpp:
void WindowManager::eventDestroy(XDestroyWindowEvent *e)
{
    Client *c = windowToClient(e->window);
    if (!c) return;

    // Clear focus tracking BEFORE destroying the Client
    if (m_focusChanging && c == m_focusCandidate) {
        stopConsideringFocus();
        m_focusCandidate = nullptr;
    }

    // Remove from map first
    m_windowMap.erase(c->window());

    // Find and erase from whichever vector owns it
    auto removeFrom = [c](auto& vec) {
        auto it = std::find_if(vec.begin(), vec.end(),
            [c](const auto& up) { return up.get() == c; });
        if (it != vec.end()) {
            vec.erase(it);
            return true;
        }
        return false;
    };

    if (!removeFrom(m_clients)) {
        removeFrom(m_hiddenClients);
    }
    // ~Client() runs here -- destructor handles unreparent, colormap cleanup

    ignoreBadWindowErrors = true;
    XSync(display(), false);
    ignoreBadWindowErrors = false;
}
```

### Colormap array modernization (D-13)
```cpp
// In Client.h -- replace:
//   int m_colormapWinCount;
//   Window *m_colormapWindows;
//   Colormap *m_windowColormaps;
// With:
//   std::vector<Window> m_colormapWindows;
//   std::vector<Colormap> m_windowColormaps;

// In Client.cpp getColormaps():
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

    // Copy from X11-allocated memory into our vectors
    m_colormapWindows.assign(cw, cw + n);
    XFree(cw);  // Free X11-allocated memory

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

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `delete this` in Client::release() | unique_ptr erase triggers ~Client() | Phase 3 | Ownership is clear, no undefined behavior |
| `std::vector<Client*>` raw pointers | `std::vector<unique_ptr<Client>>` | Phase 3 | RAII ownership, automatic cleanup |
| O(n) linear scan for every event | O(1) hash map + O(n) border fallback | Phase 3 | Performance improvement for many-client scenarios |
| `int m_state` with X11 constants | `enum class ClientState` with validation | Phase 3 | Type safety, catch logic bugs at transition points |
| `malloc`/`free` colormap arrays | `std::vector<Window/Colormap>` | Phase 3 | RAII, bounds safety, no manual free |
| No XGrabServer around reparent | RAII ServerGrab around XReparentWindow | Phase 3 | ICCCM-compliant atomic reparenting, no forgotten ungrab |

**Deprecated/outdated:**
- `Client::release()` method: Replaced by ~Client() destructor as the sole cleanup path
- `m_reparenting` flag: Still needed to distinguish reparenting-triggered UnmapNotify from client-initiated UnmapNotify

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Border events are rare enough that linear scan fallback for border window lookups is acceptable | O(1) Lookup | Minor performance issue; fixable by adding border windows to map later |
| A2 | enum class ClientState values Withdrawn=0, Normal=1, Iconic=3 match X11 WM_STATE constants | State Machine | WM_STATE property writes incorrect values; clients misbehave |

**Both assumptions can be verified by code inspection.** A2 is verified: X11 defines these values in `<X11/Xutil.h>` as `WithdrawnState=0`, `NormalState=1`, `IconicState=3`. A1 is a design tradeoff explicitly chosen in CONTEXT.md D-05.

## Open Questions (RESOLVED)

1. **Should ~Client() call unreparent()?**
   - What we know: The current code has separate `release()` and `unreparent()` methods. `release()` does cleanup (skipInRevert, unhide, deactivate, free colormap arrays). `unreparent()` does XReparentWindow back to root and restores border width.
   - What's unclear: Whether the destructor should call unreparent() directly or whether WindowManager should call it before erasing the unique_ptr.
   - RESOLVED: Yes, the destructor calls unreparent() if not already withdrawn per D-03. WindowManager calls specific pre-destruction operations (map removal, focus clearing) before erasing. The ~Client() destructor calls unreparent() (if not already withdrawn), frees colormaps, and deactivates.

2. **Should the `m_reparenting` flag be kept?**
   - What we know: `m_reparenting` is set in `manage()` to distinguish reparenting-triggered UnmapNotify from client-initiated UnmapNotify. With ServerGrab around the reparent, the UnmapNotify is still generated.
   - What's unclear: Whether ServerGrab changes the UnmapNotify timing or ordering.
   - RESOLVED: Yes, keep `m_reparenting`. ServerGrab prevents other clients from interfering but does not suppress UnmapNotify events generated by XReparentWindow.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| g++ (C++17) | Build | Yes | 13.3.0 | -- |
| CMake 3.16+ | Build | Yes | 3.28.3 | -- |
| libx11-dev | X11 client | Yes | 2:1.8.7 | -- |
| libxext-dev | Shape extension | Yes | 2:1.3.4 | -- |
| Xvfb | TEST-03 | Yes | 21.1.11 | -- |
| Catch2 | Tests | Yes | 3.14.0 (FetchContent) | -- |

**Missing dependencies with no fallback:**
None -- all dependencies are available.

**Missing dependencies with fallback:**
None.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 v3.14.0 |
| Config file | CMakeLists.txt (no standalone config file) |
| Quick run command | `cd build && cmake --build . --target test_client && DISPLAY=:99 ./test_client` |
| Full suite command | `cd build && cmake --build . && ctest --output-on-failure` |

### Phase Requirements to Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| CLNT-01 | ServerGrab grabs and releases X server | unit | `./test_client "ServerGrab"` | Wave 0 |
| CLNT-01 | Reparenting with ServerGrab is atomic | integration | `./test_client "Reparent with grab"` | Wave 0 |
| CLNT-02 | unique_ptr ownership destroys Client on erase | unit | `./test_client "Client ownership"` | Wave 0 |
| CLNT-04 | windowToClient returns Client* for known window in O(1) | unit | `./test_client "O(1) lookup"` | Wave 0 |
| CLNT-04 | windowToClient falls back for border windows | unit | `./test_client "Border window fallback"` | Wave 0 |
| CLNT-05 | Valid state transitions accepted | unit | `./test_client "Valid state transition"` | Wave 0 |
| CLNT-05 | Invalid state transitions logged but applied | unit | `./test_client "Invalid state warning"` | Wave 0 |
| TEST-03 | Window map lifecycle (create, map, unmap, destroy) | integration | `./test_client "Window lifecycle"` | Wave 0 |
| TEST-03 | Window hide/unhide preserves Client identity | integration | `./test_client "Hide unhide"` | Wave 0 |
| TEST-03 | Window deletion cleans up all resources | integration | `./test_client "Delete cleanup"` | Wave 0 |

### Sampling Rate
- **Per task commit:** `cd build && cmake --build . --target test_client && ./test_client`
- **Per wave merge:** `cd build && cmake --build . && ctest --output-on-failure`
- **Phase gate:** Full suite green before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `tests/test_client.cpp` -- covers CLNT-01 through CLNT-05 and TEST-03
- [ ] CMakeLists.txt entry for test_client target with Xvfb fixture
- [ ] Test helper utilities: create simple X11 window on Xvfb for lifecycle testing

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | N/A -- local X11 window manager, no auth |
| V3 Session Management | no | N/A -- no sessions |
| V4 Access Control | no | N/A -- single-user local WM |
| V5 Input Validation | yes | enum class ClientState validates state transitions; setState logs warnings |
| V6 Cryptography | no | N/A -- no crypto |

### Known Threat Patterns for X11 WM Stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| X11 client sending malformed WM_STATE | Tampering | setState validates transitions, applies anyway with warning |
| X11 client destroying window during reparent | Denial of Service | ServerGrab prevents interference; ignoreBadWindowErrors suppresses cleanup errors |
| Raw pointer to destroyed Client (use-after-free) | Tampering | unique_ptr ownership ensures single destruction point; event loop is single-threaded |

## Sources

### Primary (HIGH confidence)
- Source code: `include/Client.h`, `include/Manager.h`, `include/x11wrap.h`, `src/Client.cpp`, `src/Manager.cpp`, `src/Events.cpp`, `src/Buttons.cpp`, `src/Border.cpp` -- all read and verified in this session
- CONTEXT.md decisions D-01 through D-13 -- verified against source code
- CMakeLists.txt -- verified test infrastructure

### Secondary (MEDIUM confidence)
- [jichu4n.com - How X Window Managers Work, Part III](https://jichu4n.com/posts/how-x-window-managers-work-and-how-to-write-one-part-iii/) -- XGrabServer best practice for reparenting, ICCCM event handling during reparent
- [tronche.com - XReparentWindow](https://tronche.com/gui/x/xlib/window-and-session-manager/XReparentWindow.html) -- reparenting generates ReparentNotify, UnmapNotify, MapNotify events
- [Xlib Programming Manual Chapter 16](http://tqd1.physik.uni-freiburg.de/library/SGI_bookshelves/SGI_Developer/books/XLib_PG/sgi_html/ch16.html) -- save-set mechanism, reparenting protocol

### Tertiary (LOW confidence)
- None -- all findings verified against source code or official documentation

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - all types are C++17 standard library, already in use in the project
- Architecture: HIGH - ownership model is straightforward, single-threaded event loop guarantees pointer safety
- Pitfalls: HIGH - all pitfalls identified by reading current source code and comparing against the target design
- ICCCM compliance: MEDIUM - ServerGrab pattern is well-documented but edge cases during reparenting of already-mapped windows need testing

**Research date:** 2026-05-07
**Valid until:** 2026-06-07 (stable domain -- C++17 and X11 do not change rapidly)
