# Phase 6: EWMH Compliance - Research

**Researched:** 2026-05-07
**Domain:** X11 EWMH (Extended Window Manager Hints) protocol compliance
**Confidence:** HIGH

## Summary

Phase 6 adds EWMH support to wm2-born-again, enabling modern applications, panels, and taskbars to interact correctly with the window manager. The EWMH specification (freedesktop.org) defines a set of root window properties, per-client properties, and ClientMessage protocols that window managers must implement. The current codebase supports only ICCCM atoms (WM_STATE, WM_PROTOCOLS, etc.) with zero `_NET_*` support.

The implementation is pure Xlib property manipulation -- no new external dependencies are needed. All EWMH atoms are interned via `XInternAtom()`, all root window properties are set via `XChangeProperty()`, and all client-initiated requests arrive as `ClientMessage` events in the existing `eventClient()` handler. The primary work is (1) interning ~20 new atoms, (2) setting root window properties at startup, (3) extending `eventClient()` to handle `_NET_ACTIVE_WINDOW` and `_NET_WM_STATE` messages, (4) reading `_NET_WM_WINDOW_TYPE` and `_NET_WM_STRUT` from client windows, (5) adding fullscreen/maximize state to the Client class, and (6) implementing mouse-driven fullscreen/maximize toggles.

**Primary recommendation:** Extend the existing `Atoms` struct with all EWMH atoms, add fullscreen/maximize state to `Client`, extend `eventClient()` for EWMH messages, and create a dedicated `_NET_SUPPORTING_WM_CHECK` child window at startup. No new libraries needed -- this is all Xlib property manipulation following the EWMH 1.5 specification [CITED: freedesktop.org/spec/ewmh].

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** DOCK windows get no frame decoration. The WM manages their position by reading _NET_WM_STRUT/_NET_WM_STRUT_PARTIAL to determine which screen edge the dock occupies. Docks float above normal windows.
- **D-02:** NOTIFICATION windows get no frame decoration. They float above other windows. The WM honors their requested position (notification daemons position themselves).
- **D-03:** DIALOG windows get normal wm2 frame decoration with transient stacking (keep above their transient-for parent).
- **D-04:** UTILITY, SPLASH, and TOOLBAR window types are treated as NORMAL -- standard wm2 tab+border decoration.
- **D-05:** When a window goes fullscreen, the wm2 border/tab is stripped entirely and the window covers the full screen geometry including dock areas.
- **D-06:** Fullscreen is toggled via a **circular right-button mouse gesture**. This works in addition to EWMH client message requests from apps.
- **D-07:** Maximized windows keep the wm2 sideways tab+border but expand to fill the workarea (screen geometry minus dock struts).
- **D-08:** Maximize is toggled via **clicking on the border/tab area**. This works in addition to EWMH client message requests from apps.
- **D-09:** The WM reads _NET_WM_STRUT (and _NET_WM_STRUT_PARTIAL if available) from dock windows to determine reserved screen edges. _NET_WORKAREA is set to screen area minus dock struts. Normal and maximized windows respect the workarea.
- **D-10:** Phase 6 always honors _NET_ACTIVE_WINDOW client messages. Phase 8 adds focus stealing prevention.
- **D-11:** Root window properties set at startup and maintained: _NET_SUPPORTED, _NET_SUPPORTING_WM_CHECK, _NET_WM_NAME, _NET_NUMBER_OF_DESKTOPS=1, _NET_CURRENT_DESKTOP=0, _NET_WORKAREA, _NET_CLIENT_LIST, _NET_ACTIVE_WINDOW.

### Claude's Discretion
- Exact detection algorithm for circular right-button gesture (minimum radius, angular threshold, timing window)
- How to detect border/tab click for maximize toggle (which window/button combination)
- Atom interning strategy (all in Atoms struct, or separate EWMH namespace)
- Whether to read _NET_WM_STRUT or _NET_WM_STRUT_PARTIAL first (prefer PARTIAL, fall back to STRUT)
- How to handle windows that set both DOCK and another window type
- State storage for fullscreen/maximize (in Client class or separate)
- Whether to store pre-fullscreen/maximize geometry for restore
- Event processing for _NET_WM_STATE client messages (add/remove/toggle semantics per EWMH spec)

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| EWMH-01 | Set _NET_SUPPORTED listing all supported EWMH atoms | Atom interning pattern (Manager.cpp:96-103), XChangeProperty on root window |
| EWMH-02 | Set _NET_SUPPORTING_WM_CHECK for WM identification | Requires dedicated child window, self-referencing property, _NET_WM_NAME with UTF8_STRING |
| EWMH-03 | Maintain _NET_CLIENT_LIST (updated on map/unmap/destroy) | Iterate m_clients + m_hiddenClients, update on manage/hide/unhide/destroy |
| EWMH-04 | Handle _NET_ACTIVE_WINDOW (respond to client activation requests) | Extend eventClient() in Events.cpp:284, call Client::activate() |
| EWMH-05 | Handle _NET_WM_WINDOW_TYPE (DOCK, DIALOG, NOTIFICATION, NORMAL) | Read property in Client::manage(), store type, affect decoration and stacking |
| EWMH-06 | Handle _NET_WM_STATE (FULLSCREEN, MAXIMIZED_VERT/HORZ) | Extend eventClient(), add/remove/toggle semantics, Client state members |
| EWMH-07 | Set _NET_WM_NAME with UTF-8 encoding for WM name | UTF8_STRING atom type, XChangeProperty on WM check window |
| EWMH-08 | Set single-desktop atoms (_NET_NUMBER_OF_DESKTOPS=1, _NET_CURRENT_DESKTOP=0) | XChangeProperty on root window in initialiseScreen() |
| EWMH-09 | Set _NET_WORKAREA for panel/taskbar compatibility | Calculate from screen geometry minus dock struts, update when docks change |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| EWMH atom interning | API/Backend (WindowManager) | -- | Atoms are interned once in constructor, used everywhere |
| Root window property management | API/Backend (WindowManager) | -- | Root window is X11 server concept, owned by WindowManager |
| Client list tracking (_NET_CLIENT_LIST) | API/Backend (WindowManager) | -- | Client ownership is in WindowManager's vectors |
| Window type handling | API/Backend (Client) | -- | Per-client property read during manage() |
| Fullscreen/maximize state | API/Backend (Client) | -- | Per-client state stored in Client members |
| Dock strut tracking | API/Backend (WindowManager) | -- | Global screen reservation affects all clients |
| Fullscreen mouse gesture | Browser/Client (Buttons.cpp) | API/Backend (Client) | Mouse event captured in Buttons.cpp, state change in Client |
| Maximize click toggle | Browser/Client (Buttons.cpp) | API/Backend (Client) | Border/tab click in Buttons.cpp, state change in Client |
| _NET_ACTIVE_WINDOW dispatch | API/Backend (Events.cpp) | -- | ClientMessage arrives in eventClient() |
| _NET_WORKAREA calculation | API/Backend (WindowManager) | -- | Global property, depends on dock struts |
| Border strip for fullscreen | Browser/Client (Border.cpp) | API/Backend (Client) | Border manages frame visibility |

## Standard Stack

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Xlib (libX11) | system | EWMH atom interning, property setting, event handling | Already used throughout; EWMH is pure Xlib protocol [VERIFIED: codebase] |
| X11 Shape extension (libXext) | system | Border shaping for fullscreen strip | Already used in Border.cpp [VERIFIED: codebase] |

### Supporting

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| Catch2 v3 | 3.14.0 | Unit testing for atom handling, state logic | EWMH state machine tests [VERIFIED: CMakeLists.txt] |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Xlib property API | XCB property API | XCB is more modern but entire codebase is Xlib; mixing would add complexity with no benefit for property-only operations |

**Installation:**
No new packages needed. All EWMH functionality uses existing Xlib/Xext.

**Version verification:** Not applicable -- no new dependencies.

## Architecture Patterns

### System Architecture Diagram

```
Application (e.g., panel, taskbar, browser)
    |
    | ClientMessage (_NET_ACTIVE_WINDOW, _NET_WM_STATE)
    v
WindowManager::eventClient()    <-- Events.cpp:284, PRIMARY extension point
    |
    +-- _NET_ACTIVE_WINDOW --> Client::activate()
    |
    +-- _NET_WM_STATE --> Client::setFullscreen() / Client::setMaximized()
    |                       (add/remove/toggle semantics from data.l[0])
    |
    +-- _NET_CURRENT_DESKTOP --> (ignored -- single desktop only)
    |
    v
Root Window Properties (set at startup, maintained):
    _NET_SUPPORTED -----------> Atom array of all supported EWMH atoms
    _NET_SUPPORTING_WM_CHECK --> Window ID of WM check child window
    _NET_NUMBER_OF_DESKTOPS --> 1
    _NET_CURRENT_DESKTOP -----> 0
    _NET_WORKAREA -----------> [x, y, w, h] (screen - dock struts)
    _NET_CLIENT_LIST ---------> Window[] (all managed clients)
    _NET_ACTIVE_WINDOW -------> Window (focused client or None)

Application sets properties on own window:
    _NET_WM_WINDOW_TYPE ------> Read by WM during Client::manage()
    _NET_WM_STRUT_PARTIAL ----> Read by WM from dock windows
    _NET_WM_STATE ------------> Read by WM; changed via ClientMessage

Mouse Input:
    Right-button circular gesture --> Client::toggleFullscreen()
    Border/tab click -------------> Client::toggleMaximized()
```

### Recommended Project Structure

No new files needed. Modifications to existing files:

```
include/Manager.h    -- Add EWMH atoms to Atoms struct, add m_wmCheckWindow member, add helpers
src/Manager.cpp      -- Intern EWMH atoms, set root properties, create WM check window
src/Events.cpp       -- Extend eventClient() for _NET_ACTIVE_WINDOW and _NET_WM_STATE
include/Client.h     -- Add fullscreen/maximize state, window type, pre-state geometry
src/Client.cpp       -- Read _NET_WM_WINDOW_TYPE, handle state transitions, update _NET_CLIENT_LIST
src/Border.cpp       -- Fullscreen strip (hide/show frame), maximize resize
src/Buttons.cpp      -- Circular gesture detection, maximize click detection
```

### Pattern 1: EWMH Atom Interning (Same as Existing ICCCM Atoms)

**What:** All EWMH atoms follow the same intern-once pattern as existing ICCCM atoms.
**When to use:** Every EWMH atom the WM needs to read or write.

```cpp
// Source: Manager.cpp:96-103 (existing pattern)
// Extend Atoms struct in Manager.h:
struct Atoms {
    // Existing ICCCM atoms
    static Atom wm_state;
    static Atom wm_changeState;
    static Atom wm_protocols;
    static Atom wm_delete;
    static Atom wm_takeFocus;
    static Atom wm_colormaps;
    static Atom wm2_running;

    // EWMH atoms [CITED: freedesktop.org/spec/ewmh]
    static Atom net_supported;              // EWMH-01
    static Atom net_supportingWmCheck;      // EWMH-02
    static Atom net_clientList;             // EWMH-03
    static Atom net_activeWindow;           // EWMH-04
    static Atom net_wmWindowType;           // EWMH-05
    static Atom net_wmState;                // EWMH-06
    static Atom net_wmName;                 // EWMH-07
    static Atom net_wmStateFullscreen;
    static Atom net_wmStateMaximizedVert;
    static Atom net_wmStateMaximizedHorz;
    static Atom net_wmStateHidden;          // for _NET_WM_STATE on iconic windows
    static Atom net_wmWindowTypeDock;
    static Atom net_wmWindowTypeDialog;
    static Atom net_wmWindowTypeNotification;
    static Atom net_wmWindowTypeNormal;
    static Atom net_wmWindowTypeUtility;
    static Atom net_wmWindowTypeSplash;
    static Atom net_wmWindowTypeToolbar;
    static Atom net_wmStrut;                // EWMH-09
    static Atom net_wmStrutPartial;
    static Atom net_numberOfDesktops;       // EWMH-08
    static Atom net_currentDesktop;
    static Atom net_workarea;
    static Atom utf8_string;                // For _NET_WM_NAME encoding
};

// Interning in constructor (Manager.cpp), same pattern as lines 97-103:
Atoms::net_supported = XInternAtom(display(), "_NET_SUPPORTED", false);
Atoms::net_supportingWmCheck = XInternAtom(display(), "_NET_SUPPORTING_WM_CHECK", false);
// ... all other EWMH atoms
```

### Pattern 2: _NET_SUPPORTING_WM_CHECK Child Window

**What:** EWMH requires a child window whose _NET_SUPPORTING_WM_CHECK property points to itself, and whose _NET_WM_NAME contains the WM name. This allows clients to verify a WM is running.

```cpp
// Source: EWMH spec section "Root Window Properties" [CITED: freedesktop.org/spec/ewmh]
// In initialiseScreen(), after m_root is set:

// Create a child window for WM identification
m_wmCheckWindow = XCreateSimpleWindow(display(), m_root, -1, -1, 1, 1, 0, 0, 0);
XChangeProperty(display(), m_root, Atoms::net_supportingWmCheck,
                XA_WINDOW, 32, PropModeReplace,
                reinterpret_cast<unsigned char*>(&m_wmCheckWindow), 1);
XChangeProperty(display(), m_wmCheckWindow, Atoms::net_supportingWmCheck,
                XA_WINDOW, 32, PropModeReplace,
                reinterpret_cast<unsigned char*>(&m_wmCheckWindow), 1);
XChangeProperty(display(), m_wmCheckWindow, Atoms::net_wmName,
                Atoms::utf8_string, 8, PropModeReplace,
                reinterpret_cast<const unsigned char*>("wm2-born-again"), 13);
```

### Pattern 3: _NET_SUPPORTED Atom Array

**What:** Set the _NET_SUPPORTED property on the root window listing ALL EWMH atoms the WM supports.

```cpp
// Source: EWMH spec section "Root Window Properties" [CITED: freedesktop.org/spec/ewmh]
// In initialiseScreen(), after all atoms are interned:

Atom supported[] = {
    Atoms::net_supported,
    Atoms::net_supportingWmCheck,
    Atoms::net_clientList,
    Atoms::net_activeWindow,
    Atoms::net_wmWindowType,
    Atoms::net_wmState,
    Atoms::net_wmName,
    Atoms::net_numberOfDesktops,
    Atoms::net_currentDesktop,
    Atoms::net_workarea,
    Atoms::net_wmStrut,
    Atoms::net_wmStrutPartial,
    // Window type atoms
    Atoms::net_wmWindowTypeDock,
    Atoms::net_wmWindowTypeDialog,
    Atoms::net_wmWindowTypeNotification,
    Atoms::net_wmWindowTypeNormal,
    Atoms::net_wmWindowTypeUtility,
    Atoms::net_wmWindowTypeSplash,
    Atoms::net_wmWindowTypeToolbar,
    // State atoms
    Atoms::net_wmStateFullscreen,
    Atoms::net_wmStateMaximizedVert,
    Atoms::net_wmStateMaximizedHorz,
    Atoms::net_wmStateHidden,
};
XChangeProperty(display(), m_root, Atoms::net_supported,
                XA_ATOM, 32, PropModeReplace,
                reinterpret_cast<unsigned char*>(supported),
                sizeof(supported) / sizeof(Atom));
```

### Pattern 4: Extending eventClient() for EWMH ClientMessages

**What:** EWMH client messages arrive as ClientMessage events with `message_type` set to the relevant EWMH atom.
**When to use:** _NET_ACTIVE_WINDOW and _NET_WM_STATE requests from applications.

```cpp
// Source: Events.cpp:284-297 (existing), extended for EWMH
void WindowManager::eventClient(XClientMessageEvent *e)
{
    Client *c = windowToClient(e->window);

    // Existing ICCCM handler
    if (e->message_type == Atoms::wm_changeState) {
        if (c && e->format == 32 && e->data.l[0] == IconicState) {
            if (c->isNormal()) c->hide();
        }
        return;
    }

    // EWMH: _NET_ACTIVE_WINDOW [CITED: freedesktop.org/spec/ewmh]
    if (e->message_type == Atoms::net_activeWindow) {
        if (c && c->isNormal()) {
            c->activate();
        }
        return;
    }

    // EWMH: _NET_WM_STATE [CITED: freedesktop.org/spec/ewmh]
    // data.l[0] = action: 0=remove, 1=add, 2=toggle
    // data.l[1] = first property atom
    // data.l[2] = second property atom (for simultaneous changes)
    if (e->message_type == Atoms::net_wmState) {
        if (c && e->format == 32) {
            int action = static_cast<int>(e->data.l[0]);
            for (int i = 1; i <= 2; ++i) {
                Atom prop = static_cast<Atom>(e->data.l[i]);
                if (prop == None) continue;
                if (prop == Atoms::net_wmStateFullscreen) {
                    c->applyWmStateFullscreen(action);  // 0=remove, 1=add, 2=toggle
                } else if (prop == Atoms::net_wmStateMaximizedVert ||
                           prop == Atoms::net_wmStateMaximizedHorz) {
                    // Must handle both together for maximize
                    c->applyWmStateMaximized(action);
                }
            }
        }
        return;
    }

    std::fprintf(stderr, "wm2: unexpected XClientMessageEvent, type 0x%lx, "
                  "window 0x%lx\n", e->message_type, e->window);
}
```

### Pattern 5: _NET_CLIENT_LIST Maintenance

**What:** Keep _NET_CLIENT_LIST in sync with the WM's actual client list.
**When to use:** On every client addition (manage), removal (destroy), hide, and unhide.

```cpp
// Source: EWMH spec section "Root Window Properties" [CITED: freedesktop.org/spec/ewmh]
// Helper method in WindowManager:

void WindowManager::updateClientList()
{
    std::vector<Window> windows;
    for (const auto& c : m_clients) {
        windows.push_back(c->window());
    }
    for (const auto& c : m_hiddenClients) {
        windows.push_back(c->window());
    }
    XChangeProperty(display(), m_root, Atoms::net_clientList,
                    XA_WINDOW, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(windows.data()),
                    static_cast<int>(windows.size()));
}
// Call from: Client::manage(), Events.cpp eventDestroy(), addToHiddenList(), removeFromHiddenList()
```

### Anti-Patterns to Avoid

- **Forgetting to update _NET_CLIENT_LIST after hidden list operations:** `addToHiddenList()` and `removeFromHiddenList()` move clients between vectors -- _NET_CLIENT_LIST must include BOTH m_clients and m_hiddenClients per EWMH spec.
- **Setting _NET_WM_NAME with XA_STRING type instead of UTF8_STRING:** EWMH explicitly requires UTF8_STRING encoding for _NET_WM_NAME. Using XA_STRING will cause panels to display garbled text for UTF-8 names.
- **Creating the _NET_SUPPORTING_WM_CHECK window but forgetting the self-reference:** The WM check window must have _NET_SUPPORTING_WM_CHECK pointing to ITSELF, not to the root window. Missing this causes `_NET_WM_NAME` to be unreadable.
- **Treating _NET_WM_STATE add/remove/toggle as simple boolean:** The data.l[0] field defines the action: 0=remove, 1=add, 2=toggle. Ignoring this and always toggling breaks applications that explicitly request add or remove.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| UTF-8 string encoding for _NET_WM_NAME | Custom encoding detection | UTF8_STRING atom type with XChangeProperty(format=8) | X11 has well-defined UTF8_STRING type; rolling your own encoding invites byte-count errors [CITED: freedesktop.org/spec/ewmh] |
| Circular gesture detection | Complex angle math with sin/cos | Accumulate motion events, compute centroid, check angular sweep via atan2 or cross-product | Over-engineering gesture math leads to brittleness; use simple geometric heuristic (radius threshold + angular coverage) |
| Dock strut geometry calculation | Manual edge detection heuristics | _NET_WM_STRUT_PARTIAL provides explicit left/right/top/bottom reservations with per-edge x11 range | The spec gives you the exact pixel reservations; no heuristic needed [CITED: freedesktop.org/spec/ewmh] |

**Key insight:** EWMH is a well-specified protocol. Every atom, property format, and message semantics are defined in the spec. Follow the spec literally rather than inferring behavior.

## Common Pitfalls

### Pitfall 1: _NET_SUPPORTING_WM_CHECK Self-Reference

**What goes wrong:** Creating the check window but setting _NET_SUPPORTING_WM_CHECK on the root to point to the check window, without ALSO setting _NET_SUPPORTING_WM_CHECK on the check window to point to ITSELF.
**Why it happens:** The EWMH spec requires a bidirectional reference: root -> check window AND check window -> check window. The second link is easy to forget.
**How to avoid:** Set the property on BOTH windows immediately after creating the check window.
**Warning signs:** Panels like xfce4-panel or polybar cannot read the WM name; `xprop -root _NET_SUPPORTING_WM_CHECK` returns a window ID but `xprop -id <that_id> _NET_WM_NAME` returns nothing.

### Pitfall 2: UTF8_STRING Atom Not Pre-Defined

**What goes wrong:** Using `XA_STRING` as the type for `_NET_WM_NAME` instead of the `UTF8_STRING` atom.
**Why it happens:** `XA_STRING` is a pre-defined X atom, while `UTF8_STRING` must be interned with `XInternAtom()`. It is not in `<X11/Xatom.h>`.
**How to avoid:** Intern `UTF8_STRING` as part of the EWMH atom block: `Atoms::utf8_string = XInternAtom(display(), "UTF8_STRING", false);`
**Warning signs:** Window names with non-ASCII characters display incorrectly in panels.

### Pitfall 3: _NET_WM_STATE Message Semantics (Add/Remove/Toggle)

**What goes wrong:** Always toggling the state regardless of the requested action, or ignoring the second property atom in data.l[2].
**Why it happens:** Many WMs implement only toggle, but the EWMH spec defines three actions: 0=_NET_WM_STATE_REMOVE, 1=_NET_WM_STATE_ADD, 2=_NET_WM_STATE_TOGGLE. Applications expect precise control.
**How to avoid:** Read `e->data.l[0]` and dispatch to remove/add/toggle logic. Also check both `data.l[1]` and `data.l[2]` for simultaneous state changes (e.g., maximize both axes at once).
**Warning signs:** Firefox maximize/restore breaks; windows get "stuck" in fullscreen.

### Pitfall 4: Forgetting to Update _NET_CLIENT_LIST on Hide/Unhide

**What goes wrong:** _NET_CLIENT_LIST is only updated on map/destroy, but hidden (iconic) clients are moved to m_hiddenClients and back.
**Why it happens:** The existing code moves unique_ptrs between vectors in `addToHiddenList()`/`removeFromHiddenList()` without any EWMH update hook.
**How to avoid:** Call `updateClientList()` from both `addToHiddenList()` and `removeFromHiddenList()`, AND from `eventDestroy()`, AND from `Client::manage()`.
**Warning signs:** Taskbars show stale client lists; closed windows still appear.

### Pitfall 5: _NET_WORKAREA Not Updated When Docks Appear/Disappear

**What goes wrong:** _NET_WORKAREA is set once at startup and never updated.
**Why it happens:** Docks can appear and disappear at runtime. The workarea must be recalculated whenever a dock's _NET_WM_STRUT changes or a dock window is created/destroyed.
**How to avoid:** Watch for PropertyNotify events on dock windows for _NET_WM_STRUT changes. Recalculate and update _NET_WORKAREA on dock creation, destruction, and property change.
**Warning signs:** Maximized windows overlap panels; panels overlap maximized windows.

### Pitfall 6: Fullscreen Window Not Covering Dock Areas

**What goes wrong:** Fullscreen window is sized to workarea (screen minus struts) instead of full screen geometry.
**Why it happens:** It's tempting to reuse the workarea calculation for fullscreen.
**How to avoid:** Per D-05, fullscreen covers the ENTIRE screen including dock areas. Use `DisplayWidth`/`DisplayHeight` directly, not workarea.
**Warning signs:** Panel is visible over fullscreen video.

### Pitfall 7: Circular Gesture False Positives

**What goes wrong:** Normal right-click-and-drag operations (like circulate) are misinterpreted as circular gestures.
**Why it happens:** Right-button drag is already used for window circulation (circulate in Buttons.cpp:18-19). The gesture detection must distinguish circular motion from linear drag.
**How to avoid:** Require a minimum angular sweep (e.g., 270 degrees) AND a minimum radius. Discard gestures that are primarily linear. Consider using ButtonPress on a client window (not root) as the trigger, and only start gesture tracking after some minimum displacement.
**Warning signs:** Window circulation via right-click stops working; fullscreen toggles randomly.

## Code Examples

Verified patterns from the EWMH specification and reference implementations:

### Setting Single-Desktop Atoms on Root Window

```cpp
// Source: EWMH spec section "Desktop" [CITED: freedesktop.org/spec/ewmh]
// In initialiseScreen():

// _NET_NUMBER_OF_DESKTOPS = 1
long ndesktops = 1;
XChangeProperty(display(), m_root, Atoms::net_numberOfDesktops,
                XA_CARDINAL, 32, PropModeReplace,
                reinterpret_cast<unsigned char*>(&ndesktops), 1);

// _NET_CURRENT_DESKTOP = 0
long currentDesktop = 0;
XChangeProperty(display(), m_root, Atoms::net_currentDesktop,
                XA_CARDINAL, 32, PropModeReplace,
                reinterpret_cast<unsigned char*>(&currentDesktop), 1);
```

### Reading _NET_WM_WINDOW_TYPE During Client Manage

```cpp
// Source: EWMH spec section "Application Window Properties" [CITED: freedesktop.org/spec/ewmh]
// In Client::manage(), after getTransient():

Atom actualType;
int actualFormat;
unsigned long nItems, bytesAfter;
Atom *data = nullptr;
if (XGetWindowProperty(display(), m_window, Atoms::net_wmWindowType, 0, 1024,
                       false, XA_ATOM, &actualType, &actualFormat,
                       &nItems, &bytesAfter,
                       reinterpret_cast<unsigned char**>(&data)) == Success && data) {
    for (unsigned long i = 0; i < nItems; ++i) {
        if (data[i] == Atoms::net_wmWindowTypeDock) {
            m_windowType = WindowType::Dock;
            break;
        } else if (data[i] == Atoms::net_wmWindowTypeDialog) {
            m_windowType = WindowType::Dialog;
            break;
        } else if (data[i] == Atoms::net_wmWindowTypeNotification) {
            m_windowType = WindowType::Notification;
            break;
        }
        // D-04: UTILITY, SPLASH, TOOLBAR treated as NORMAL (no break, default)
    }
    XFree(data);
}
```

### Reading _NET_WM_STRUT_PARTIAL for Dock Struts

```cpp
// Source: EWMH spec section "Application Window Properties" [CITED: freedesktop.org/spec/ewmh]
// _NET_WM_STRUT_PARTIAL: 12 CARDINALs [left, right, top, bottom, left_start_y, left_end_y, ...]
// _NET_WM_STRUT: 4 CARDINALs [left, right, top, bottom]
// Prefer PARTIAL, fall back to STRUT per Claude's discretion

unsigned long nItems;
unsigned char *data = nullptr;
Atom actualType;
int actualFormat;
unsigned long bytesAfter;

// Try _NET_WM_STRUT_PARTIAL first
if (XGetWindowProperty(display(), dockWindow, Atoms::net_wmStrutPartial,
                       0, 12, false, XA_CARDINAL, &actualType, &actualFormat,
                       &nItems, &bytesAfter, &data) == Success && data && nItems >= 4) {
    long *struts = reinterpret_cast<long*>(data);
    // struts[0]=left, struts[1]=right, struts[2]=top, struts[3]=bottom
    // Use struts values to compute workarea
    XFree(data);
} else {
    // Fall back to _NET_WM_STRUT (4 values only)
    if (data) XFree(data);
    if (XGetWindowProperty(display(), dockWindow, Atoms::net_wmStrut,
                           0, 4, false, XA_CARDINAL, &actualType, &actualFormat,
                           &nItems, &bytesAfter, &data) == Success && data && nItems >= 4) {
        long *struts = reinterpret_cast<long*>(data);
        XFree(data);
    }
}
```

### Updating _NET_ACTIVE_WINDOW on Focus Change

```cpp
// Source: EWMH spec [CITED: freedesktop.org/spec/ewmh]
// In Client::activate() (Client.cpp:202-236), after setActiveClient():

void Client::activate()
{
    // ... existing activation logic ...

    windowManager()->setActiveClient(this);

    // Update _NET_ACTIVE_WINDOW on root window
    Window active = m_window;
    XChangeProperty(display(), root(), Atoms::net_activeWindow,
                    XA_WINDOW, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&active), 1);

    // ... rest of existing logic ...
}

// In WindowManager::clearFocus() (Manager.cpp:436-469):
// Set _NET_ACTIVE_WINDOW to None when clearing focus
Window noneWindow = None;
XChangeProperty(display(), m_root, Atoms::net_activeWindow,
                XA_WINDOW, 32, PropModeReplace,
                reinterpret_cast<unsigned char*>(&noneWindow), 1);
```

### Fullscreen State Toggle (Client-Side)

```cpp
// Per D-05: strip border, cover full screen
void Client::setFullscreen(bool fullscreen)
{
    if (m_isFullscreen == fullscreen) return;

    if (fullscreen) {
        // Save pre-fullscreen geometry for restore
        m_preFullscreenX = m_x;
        m_preFullscreenY = m_y;
        m_preFullscreenW = m_w;
        m_preFullscreenH = m_h;

        m_isFullscreen = true;

        // Strip border decoration
        m_border->unmap();  // hide frame windows

        // Reparent directly to root (or resize parent to cover full screen)
        int sw = DisplayWidth(display(), 0);
        int sh = DisplayHeight(display(), 0);
        XMoveResizeWindow(display(), m_window, 0, 0, sw, sh);
        XRaiseWindow(display(), m_window);
    } else {
        m_isFullscreen = false;

        // Restore border decoration
        m_border->map();
        m_border->configure(m_preFullscreenX, m_preFullscreenY,
                            m_preFullscreenW, m_preFullscreenH, 0L, Above);
        XMoveResizeWindow(display(), m_window, 0, 0,
                          m_preFullscreenW, m_preFullscreenH);
        m_x = m_preFullscreenX;
        m_y = m_preFullscreenY;
        m_w = m_preFullscreenW;
        m_h = m_preFullscreenH;
    }

    // Update _NET_WM_STATE on client window
    updateNetWmState();
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| ICCCM only (wm2 upstream) | ICCCM + EWMH 1.5 | EWMH 1.5 released 2011 | Modern apps assume EWMH; panels/taskbars break without it |
| Core X fonts for WM name | UTF8_STRING encoding | EWMH spec | WM name must be UTF-8; XA_STRING is insufficient |
| No dock awareness | _NET_WM_STRUT/_NET_WM_STRUT_PARTIAL | EWMH 1.2+ | Panels need strut reservations for proper layout |
| No fullscreen support | _NET_WM_STATE_FULLSCREEN | EWMH 1.2+ | Video players, browsers expect fullscreen protocol |

**Deprecated/outdated:**
- `_NET_WM_STRUT` (4 values): superseded by `_NET_WM_STRUT_PARTIAL` (12 values with per-edge x11 ranges), but still required for compatibility with older panels.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | The EWMH spec does not require _NET_CLOSE_WINDOW support for a compliant WM (it is optional) | Standard Stack | If required, another ClientMessage handler is needed |
| A2 | _NET_WM_WINDOW_TYPE is set before MapRequest and does not change at runtime for most applications | Architecture Patterns | If it changes, PropertyNotify handler needs to watch for it |
| A3 | Xvfb supports all EWMH atoms (they are just X atoms, no special server support needed) | Validation Architecture | If not, EWMH tests need a real X server |

## Open Questions

1. **Circular gesture vs. circulate conflict**
   - What we know: Right-button on root currently circulates (Buttons.cpp:18-19). Right-button on client does nothing currently (only Button1 is handled in Client::eventButton).
   - What's unclear: Where exactly should the circular gesture be detected? On client windows only (right-button drag on client frame)? Or on root too?
   - Recommendation: Detect circular gesture on CLIENT WINDOWS only (right-button drag on frame/border). Keep root right-click as circulate. This avoids the conflict entirely.

2. **Maximize click: which button and which window?**
   - What we know: Button1 on border windows currently dispatches to Border::eventButton() (Client.cpp:1134-1137). The border has m_tab, m_button (close), and m_resize windows.
   - What's unclear: D-08 says "click on border/tab area" but doesn't specify the button. Single-click or double-click?
   - Recommendation: Use middle-button (Button2) on the tab area for maximize toggle. Button1 on tab already has meaning (drags), and Button3 is circulate. Button2 is unused and intuitive.

3. **Should _NET_WM_WINDOW_TYPE changes be watched at runtime?**
   - What we know: Some apps (notably Qt/GTK dialogs) may change window type after mapping.
   - What's unclear: Whether wm2-born-again needs to handle this for correctness.
   - Recommendation: Watch PropertyNotify for _NET_WM_WINDOW_TYPE changes in Client::eventProperty(). Recalculate decoration/stacking on change.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Xvfb | EWMH tests | ✓ | system | -- |
| Xlib (libX11) | All EWMH operations | ✓ | system | -- |
| Xext (libXext) | Border shaping for fullscreen | ✓ | system | -- |
| Catch2 v3 | Test framework | ✓ | 3.14.0 (FetchContent) | -- |
| Xft (libXft) | Tab label rendering | ✓ | system | -- |

**Missing dependencies with no fallback:**
None -- all required dependencies are already available from prior phases.

**Missing dependencies with fallback:**
None.

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | Catch2 v3.14.0 (via FetchContent) |
| Config file | CMakeLists.txt (lines 44-173) |
| Quick run command | `ctest --test-dir build -R test_raii --output-on-failure` |
| Full suite command | `cmake --build build && cd build && ctest --output-on-failure` |

### Phase Requirements -> Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| EWMH-01 | _NET_SUPPORTED set on root with all supported atoms | smoke (Xvfb) | `ctest --test-dir build -R ewmh_supported` | Wave 0 |
| EWMH-02 | _NET_SUPPORTING_WM_CHECK self-reference + _NET_WM_NAME | smoke (Xvfb) | `ctest --test-dir build -R ewmh_check` | Wave 0 |
| EWMH-03 | _NET_CLIENT_LIST updated on map/unmap/destroy | integration | `ctest --test-dir build -R ewmh_client_list` | Wave 0 |
| EWMH-04 | _NET_ACTIVE_WINDOW message activates client | unit (mock) | `ctest --test-dir build -R ewmh_active` | Wave 0 |
| EWMH-05 | _NET_WM_WINDOW_TYPE read during manage | unit | `ctest --test-dir build -R ewmh_window_type` | Wave 0 |
| EWMH-06 | _NET_WM_STATE add/remove/toggle semantics | unit | `ctest --test-dir build -R ewmh_state` | Wave 0 |
| EWMH-07 | _NET_WM_NAME UTF-8 encoding on check window | smoke (Xvfb) | `ctest --test-dir build -R ewmh_name` | Wave 0 |
| EWMH-08 | Single-desktop atoms set correctly | smoke (Xvfb) | `ctest --test-dir build -R ewmh_desktop` | Wave 0 |
| EWMH-09 | _NET_WORKAREA calculated from dock struts | unit | `ctest --test-dir build -R ewmh_workarea` | Wave 0 |

### Sampling Rate

- **Per task commit:** `ctest --test-dir build -R <test_pattern> --output-on-failure`
- **Per wave merge:** `cmake --build build && cd build && ctest --output-on-failure`
- **Phase gate:** Full suite green before `/gsd-verify-work`

### Wave 0 Gaps

- [ ] `tests/test_ewmh.cpp` -- covers EWMH-01 through EWMH-09 (atom interning, property setting, state machine)
- [ ] CMakeLists.txt: Add `test_ewmh` executable linked against Catch2 + X11, with Xvfb fixture
- [ ] Test infrastructure: atom interning helper that creates a display connection for testing

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | No authentication in WM |
| V3 Session Management | no | No sessions |
| V4 Access Control | no | WM controls all windows; X11 server enforces access |
| V5 Input Validation | yes | Validate ClientMessage format (format=32, correct atom types) before processing |
| V6 Cryptography | no | No cryptography |

### Known Threat Patterns for EWMH/X11 Stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Malicious ClientMessage (spoofed activation) | Spoofing | Phase 6 grants all requests (D-10); Phase 8 adds focus stealing prevention |
| Client setting _NET_WM_WINDOW_TYPE after mapping | Tampering | Watch PropertyNotify for type changes; re-validate on change |
| _NET_WM_STRUT with absurd values (e.g., 100000px) | Denial of Service | Clamp strut values to screen dimensions |
| Client claiming to be DOCK to reserve screen space | Spoofing | Accept first dock per edge; warn on duplicates |

## Sources

### Primary (HIGH confidence)
- EWMH 1.5 specification (freedesktop.org/spec/ewmh) -- all atom definitions, property formats, message semantics
- Codebase analysis: Manager.cpp, Events.cpp, Client.cpp, Buttons.cpp, Border.cpp -- integration points, existing patterns

### Secondary (MEDIUM confidence)
- etwm/ewmh.c reference implementation (github.com) -- real-world C/Xlib EWMH patterns
- wm2 upstream codebase -- original ICCCM patterns that EWMH extends

### Tertiary (LOW confidence)
None.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- no new dependencies; pure Xlib
- Architecture: HIGH -- clear integration points in existing code; EWMH spec is definitive
- Pitfalls: HIGH -- well-documented pitfalls from EWMH spec and reference implementations

**Research date:** 2026-05-07
**Valid until:** 2026-06-07 (stable -- X11/EWMH spec does not change rapidly)
