# Phase 6: EWMH Compliance - Pattern Map

**Mapped:** 2026-05-07
**Files analyzed:** 9 (7 modified, 1 modified header, 1 new test)
**Analogs found:** 9 / 9

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `include/Manager.h` | config/model | state | `include/Manager.h` (itself) | exact |
| `src/Manager.cpp` | controller | request-response | `src/Manager.cpp` (itself) | exact |
| `src/Events.cpp` | controller | event-driven | `src/Events.cpp` (itself) | exact |
| `include/Client.h` | model | state | `include/Client.h` (itself) | exact |
| `src/Client.cpp` | service | CRUD/state | `src/Client.cpp` (itself) | exact |
| `include/Border.h` | model | state | `include/Border.h` (itself) | exact |
| `src/Border.cpp` | component | request-response | `src/Border.cpp` (itself) | exact |
| `src/Buttons.cpp` | controller | event-driven | `src/Buttons.cpp` (itself) | exact |
| `tests/test_ewmh.cpp` | test | request-response | `tests/test_client.cpp` | role-match |

## Pattern Assignments

### `include/Manager.h` (config/model, state)

**Analog:** `include/Manager.h` (extending existing file)

**Atom struct pattern** (lines 184-195):
```cpp
// Atom definitions (from upstream General.h)
struct Atoms {
    static Atom wm_state;
    static Atom wm_changeState;
    static Atom wm_protocols;
    static Atom wm_delete;
    static Atom wm_takeFocus;
    static Atom wm_colormaps;
    static Atom wm2_running;
};

extern bool ignoreBadWindowErrors;
```
**Extension:** Add ~22 EWMH static Atom members to the existing `Atoms` struct. Follow the naming convention `net_*` (camelCase for multi-word: `net_supportingWmCheck`, `net_wmStateFullscreen`).

**New member pattern** (near line 101-103 area):
```cpp
Window m_root;
Colormap m_defaultColormap;
```
**Extension:** Add `Window m_wmCheckWindow{None};` member for the EWMH WM check child window. Add helper method declarations: `void updateClientList();`, `void updateActiveWindow(Window w);`, `void updateWorkarea();`.

---

### `src/Manager.cpp` (controller, request-response)

**Analog:** `src/Manager.cpp` (extending existing file)

**Atom interning pattern** (lines 96-103):
```cpp
// Intern atoms
Atoms::wm_state       = XInternAtom(display(), "WM_STATE",            false);
Atoms::wm_changeState = XInternAtom(display(), "WM_CHANGE_STATE",     false);
Atoms::wm_protocols   = XInternAtom(display(), "WM_PROTOCOLS",        false);
Atoms::wm_delete      = XInternAtom(display(), "WM_DELETE_WINDOW",    false);
Atoms::wm_takeFocus   = XInternAtom(display(), "WM_TAKE_FOCUS",       false);
Atoms::wm_colormaps   = XInternAtom(display(), "WM_COLORMAP_WINDOWS", false);
Atoms::wm2_running    = XInternAtom(display(), "_WM2_RUNNING",        false);
```
**Extension:** Add EWMH atom interning immediately after line 103, following the same pattern. Intern `_NET_SUPPORTED`, `_NET_SUPPORTING_WM_CHECK`, `_NET_CLIENT_LIST`, `_NET_ACTIVE_WINDOW`, `_NET_WM_WINDOW_TYPE`, `_NET_WM_STATE`, `_NET_WM_NAME`, `_NET_WM_STATE_FULLSCREEN`, `_NET_WM_STATE_MAXIMIZED_VERT`, `_NET_WM_STATE_MAXIMIZED_HORZ`, `_NET_WM_STATE_HIDDEN`, `_NET_WM_WINDOW_TYPE_DOCK`, `_NET_WM_WINDOW_TYPE_DIALOG`, `_NET_WM_WINDOW_TYPE_NOTIFICATION`, `_NET_WM_WINDOW_TYPE_NORMAL`, `_NET_WM_WINDOW_TYPE_UTILITY`, `_NET_WM_WINDOW_TYPE_SPLASH`, `_NET_WM_WINDOW_TYPE_TOOLBAR`, `_NET_WM_STRUT`, `_NET_WM_STRUT_PARTIAL`, `_NET_NUMBER_OF_DESKTOPS`, `_NET_CURRENT_DESKTOP`, `_NET_WORKAREA`, `UTF8_STRING`.

**Static member definitions pattern** (lines 17-23):
```cpp
Atom Atoms::wm_state = None;
Atom Atoms::wm_changeState = None;
// ... etc
```
**Extension:** Add static member definitions for all new EWMH atoms immediately after line 23, following the same `Atom Atoms::net_xxx = None;` pattern.

**Root window property setting pattern** (lines 366-369, in `timestamp()`):
```cpp
XChangeProperty(display(), m_root, Atoms::wm2_running,
                Atoms::wm2_running, 8, PropModeAppend,
                reinterpret_cast<unsigned char*>(const_cast<char*>("")), 0);
```
**Extension:** In `initialiseScreen()`, after the menu window is set up (after line 320), add EWMH root window property setup: create WM check window, set `_NET_SUPPORTING_WM_CHECK` on both root and check window, set `_NET_WM_NAME` on check window, set `_NET_SUPPORTED` atom array, set `_NET_NUMBER_OF_DESKTOPS=1`, `_NET_CURRENT_DESKTOP=0`, `_NET_WORKAREA`.

**Client list management pattern** (lines 488-511):
```cpp
void WindowManager::addToHiddenList(Client *c)
{
    auto it = std::find_if(m_clients.begin(), m_clients.end(),
        [c](const auto& up) { return up.get() == c; });
    if (it != m_clients.end()) {
        m_hiddenClients.push_back(std::move(*it));
        m_clients.erase(it);
    }
}
```
**Extension:** Add `updateClientList()` helper that iterates `m_clients` and `m_hiddenClients`, collects `window()` from each, and calls `XChangeProperty` with `XA_WINDOW` on root for `_NET_CLIENT_LIST`. Call this from `addToHiddenList()`, `removeFromHiddenList()`, `eventDestroy()`, and wherever clients are added.

**Cleanup pattern** (lines 134-168, in `release()`):
```cpp
// RAII handles resource cleanup
// ...
if (m_menuWindow != None) {
    XDestroyWindow(display(), m_menuWindow);
    m_menuWindow = None;
}
```
**Extension:** Add cleanup for `m_wmCheckWindow` in `release()` following the same pattern: destroy the window, set to None.

---

### `src/Events.cpp` (controller, event-driven)

**Analog:** `src/Events.cpp` (extending existing file)

**ClientMessage dispatch pattern** (lines 284-297):
```cpp
void WindowManager::eventClient(XClientMessageEvent *e)
{
    Client *c = windowToClient(e->window);

    if (e->message_type == Atoms::wm_changeState) {
        if (c && e->format == 32 && e->data.l[0] == IconicState) {
            if (c->isNormal()) c->hide();
        }
        return;
    }

    std::fprintf(stderr, "wm2: unexpected XClientMessageEvent, type 0x%lx, "
                  "window 0x%lx\n", e->message_type, e->window);
}
```
**Extension:** Add `_NET_ACTIVE_WINDOW` and `_NET_WM_STATE` handlers BEFORE the final fprintf. Follow the same pattern: check `e->message_type == Atoms::net_activeWindow`, validate `e->format == 32`, call `c->activate()` for active window, or dispatch to `c->applyWmState(action)` for state changes with add/remove/toggle semantics from `e->data.l[0]`. Keep the existing `wm_changeState` handler. Remove or reduce the fallback fprintf since EWMH messages are expected.

**Property event dispatch pattern** (lines 316-320):
```cpp
void WindowManager::eventProperty(XPropertyEvent *e)
{
    Client *c = windowToClient(e->window);
    if (c) c->eventProperty(e);
}
```
**Extension:** No change needed in Events.cpp for property handling -- `_NET_WM_WINDOW_TYPE` and `_NET_WM_STRUT` property changes are handled inside `Client::eventProperty()`.

---

### `include/Client.h` (model, state)

**Analog:** `include/Client.h` (extending existing file)

**Enum pattern** (lines 18-22):
```cpp
enum class ClientState {
    Withdrawn = 0,   // matches X11 WithdrawnState
    Normal    = 1,   // matches X11 NormalState
    Iconic    = 3    // matches X11 IconicState (NOT 2 -- X11 uses 3)
};
```
**Extension:** Add a new `enum class WindowType { Normal, Dock, Dialog, Notification };` following the same scoped enum pattern. Add near line 23.

**State member pattern** (lines 124-127):
```cpp
ClientState m_state;
int m_protocol;
bool m_managed;
bool m_reparenting;
```
**Extension:** Add new members: `WindowType m_windowType{WindowType::Normal};`, `bool m_isFullscreen{false};`, `bool m_isMaximizedVert{false};`, `bool m_isMaximizedHorz{false};`, `int m_preFullscreenX{0}`, `int m_preFullscreenY{0}`, `int m_preFullscreenW{0}`, `int m_preFullscreenH{0}`, `int m_preMaximizedX{0}`, `int m_preMaximizedY{0}`, `int m_preMaximizedW{0}`, `int m_preMaximizedH{0}`.

**Accessor pattern** (lines 37-41):
```cpp
bool isHidden()     const { return m_state == ClientState::Iconic; }
bool isWithdrawn()  const { return m_state == ClientState::Withdrawn; }
bool isNormal()     const { return m_state == ClientState::Normal; }
bool isTransient()  const { return m_transient != None; }
bool isFixedSize()  const { return m_fixedSize; }
```
**Extension:** Add `bool isFullscreen() const { return m_isFullscreen; }`, `bool isMaximized() const { return m_isMaximizedVert && m_isMaximizedHorz; }`, `bool isDock() const { return m_windowType == WindowType::Dock; }`, `bool isNotification() const { return m_windowType == WindowType::Notification; }`, `WindowType windowType() const { return m_windowType; }`.

**Method declaration pattern** (lines 78-80):
```cpp
// Interaction
void move(XButtonEvent *e);
void resize(XButtonEvent *e, bool horizontal, bool vertical);
```
**Extension:** Add EWMH methods: `void setFullscreen(bool fullscreen);`, `void toggleFullscreen();`, `void setMaximized(bool vert, bool horz);`, `void toggleMaximized();`, `void applyWmState(int action);`, `void getWindowType();`, `void updateNetWmState();`, `void activateEwmh();`.

---

### `src/Client.cpp` (service, CRUD/state)

**Analog:** `src/Client.cpp` (extending existing file)

**Property read pattern -- XGetWindowProperty** (lines 292-307, `getProperty_aux`):
```cpp
static int getProperty_aux(Display *d, Window w, Atom a, Atom type, long len,
                           unsigned char **p)
{
    Atom realType;
    int format;
    unsigned long n, extra;
    int status;

    status = XGetWindowProperty(d, w, a, 0L, len, false, type, &realType,
                                &format, &n, &extra, p);

    if (status != Success || *p == 0) return -1;
    if (n == 0) XFree(*p);

    return static_cast<int>(n);
}
```
**Extension:** Reuse `getProperty_aux` for reading `_NET_WM_WINDOW_TYPE` (use `XA_ATOM` type) and `_NET_WM_STRUT_PARTIAL`/`_NET_WM_STRUT` (use `XA_CARDINAL` type). No new helper needed -- the existing one handles the pattern.

**Property read during manage pattern** (lines 111-113, in `manage()`):
```cpp
getColormaps();
getProtocols();
getTransient();
```
**Extension:** Add `getWindowType();` call after `getTransient();` in `manage()`. The method reads `_NET_WM_WINDOW_TYPE` from the client window and sets `m_windowType`.

**Protocol enumeration pattern** (lines 374-394, `getProtocols()`):
```cpp
void Client::getProtocols()
{
    long n;
    Atom *p;

    m_protocol = 0;
    if ((n = getProperty_aux(display(), m_window, Atoms::wm_protocols, XA_ATOM,
                             20L, reinterpret_cast<unsigned char**>(&p))) <= 0) {
        return;
    }

    for (int i = 0; i < n; ++i) {
        if (p[i] == Atoms::wm_delete) {
            m_protocol |= static_cast<int>(Protocol::Delete);
        } else if (p[i] == Atoms::wm_takeFocus) {
            m_protocol |= static_cast<int>(Protocol::TakeFocus);
        }
    }

    XFree(p);
}
```
**Extension:** `getWindowType()` follows this exact pattern: call `getProperty_aux` with `Atoms::net_wmWindowType` and `XA_ATOM`, iterate returned atoms, compare against `Atoms::net_wmWindowTypeDock`, `net_wmWindowTypeDialog`, `net_wmWindowTypeNotification`, break on first match. Use `XFree(p)` at the end.

**State setting pattern** (lines 336-356, `setState()`):
```cpp
void Client::setState(ClientState state)
{
    // ...
    m_state = state;

    long data[2];
    data[0] = static_cast<long>(state);
    data[1] = static_cast<long>(None);

    XChangeProperty(display(), m_window, Atoms::wm_state, Atoms::wm_state,
                    32, PropModeReplace, reinterpret_cast<unsigned char*>(data), 2);
}
```
**Extension:** `updateNetWmState()` follows this pattern: build an array of Atom values representing the current `_NET_WM_STATE` (fullscreen, maximized_vert, maximized_horz, hidden), call `XChangeProperty` with `XA_ATOM` type on the client window for `_NET_WM_STATE`. Call this from `setFullscreen()`, `setMaximized()`, `hide()`, `unhide()`.

**Window operations pattern** (lines 202-236, `activate()`):
```cpp
void Client::activate()
{
    if (parent() == root()) {
        std::fprintf(stderr, "wm2: warning: bad parent in Client::activate\n");
        return;
    }

    if (!m_managed || isHidden() || isWithdrawn()) return;

    // ... focus, raise, colormap, decorate ...

    windowManager()->setActiveClient(this);
    decorate(true);
    installColormap();
}
```
**Extension:** Add `_NET_ACTIVE_WINDOW` update at the end of `activate()`: `XChangeProperty(display(), root(), Atoms::net_activeWindow, XA_WINDOW, 32, PropModeReplace, ...)`. Also update in `clearFocus()` in Manager.cpp to set `_NET_ACTIVE_WINDOW` to `None`.

**Property change handler pattern** (lines 1071-1098, `eventProperty()`):
```cpp
void Client::eventProperty(XPropertyEvent *e)
{
    Atom a = e->atom;
    bool shouldDelete = (e->state == PropertyDelete);

    switch (a) {
    case XA_WM_ICON_NAME:
        if (shouldDelete) m_iconName.clear();
        else m_iconName = getProperty(a);
        if (setLabel()) rename();
        return;

    case XA_WM_NAME:
        if (shouldDelete) m_name.clear();
        else m_name = getProperty(a);
        if (setLabel()) rename();
        return;

    case XA_WM_TRANSIENT_FOR:
        getTransient();
        return;
    }

    if (a == Atoms::wm_colormaps) {
        getColormaps();
        if (isActive()) installColormap();
    }
}
```
**Extension:** Add `if (a == Atoms::net_wmWindowType) { getWindowType(); return; }` after the `wm_colormaps` check. Also add `if (a == Atoms::net_wmStrut || a == Atoms::net_wmStrutPartial) { windowManager()->updateWorkarea(); return; }` for dock strut changes.

**Geometry bounds checking pattern** (lines 157-172, in `manage()`):
```cpp
int dw = DisplayWidth(display(), 0), dh = DisplayHeight(display(), 0);

if (m_w < m_minWidth) { m_w = m_minWidth; m_fixedSize = false; reshape = true; }
if (m_h < m_minHeight) { m_h = m_minHeight; m_fixedSize = false; reshape = true; }
if (m_w > dw - 8) m_w = dw - 8;
if (m_h > dh - 8) m_h = dh - 8;
```
**Extension:** `setFullscreen()` and `setMaximized()` use `DisplayWidth`/`DisplayHeight` for screen geometry. Fullscreen uses raw screen dimensions (per D-05, covers dock areas). Maximized uses workarea dimensions (screen minus dock struts, per D-07).

---

### `include/Border.h` (model, state)

**Analog:** `include/Border.h` (extending existing file)

**Method declaration pattern** (lines 20-23):
```cpp
void map();
void unmap();
void lower();
void mapRaised();
```
**Extension:** Add `void stripForFullscreen();` and `void restoreFromFullscreen(int x, int y, int w, int h);` for fullscreen border manipulation. Add `void resizeToWorkarea(int x, int y, int w, int h);` for maximize.

**Accessor pattern** (lines 45-51):
```cpp
int yIndent() {
    return isTransient() ? TRANSIENT_FRAME_WIDTH + 1 : FRAME_WIDTH + 1;
}
int xIndent() {
    return isTransient() ? TRANSIENT_FRAME_WIDTH + 1 :
        m_tabWidth + FRAME_WIDTH + 1;
}
```
**Extension:** No changes to existing accessors needed. The new fullscreen/maximize methods use these accessors for coordinate math.

---

### `src/Border.cpp` (component, request-response)

**Analog:** `src/Border.cpp` (extending existing file)

**Window unmap pattern** (lines 850-862, `unmap()`):
```cpp
void Border::unmap()
{
    if (m_parent == root()) {
        std::fprintf(stderr, "wm2: bad parent in Border::unmap()\n");
    } else {
        XUnmapWindow(display(), m_parent);

        if (!isTransient()) {
            XUnmapWindow(display(), m_tab);
            XUnmapWindow(display(), m_button);
        }
    }
}
```
**Extension:** `stripForFullscreen()` follows this pattern: unmap parent, tab, button, and resize handle. Then reparent the child window directly to root. `restoreFromFullscreen()` reverses: reparent back to parent, then `map()`.

**Window map pattern** (lines 812-825, `map()`):
```cpp
void Border::map()
{
    if (m_parent == root()) {
        std::fprintf(stderr, "wm2: bad parent in Border::map()\n");
    } else {
        XMapWindow(display(), m_parent);

        if (!isTransient()) {
            XMapWindow(display(), m_tab);
            XMapWindow(display(), m_button);
            if (!isFixedSize()) XMapWindow(display(), m_resize);
        }
    }
}
```
**Extension:** `restoreFromFullscreen()` calls `configure()` with pre-fullscreen geometry, then `map()`.

**Configure pattern** (lines 714-800, `configure()`):
```cpp
void Border::configure(int x, int y, int w, int h,
                       unsigned long mask, int detail,
                       bool force)
{
    // ... creates windows if needed, configures geometry ...
    XWindowChanges wc;
    wc.x = x - xIndent();
    wc.y = y - yIndent();
    wc.width  = w + xIndent() + 1;
    wc.height = h + yIndent() + 1;
    // ...
    XConfigureWindow(display(), m_parent, mask, &wc);
```
**Extension:** `resizeToWorkarea()` for maximized windows calls `configure()` with workarea geometry. The tab+border remains visible (per D-07).

**Reparent pattern** (lines 871-875):
```cpp
void Border::reparent()
{
    x11::ServerGrab grab(display());
    XReparentWindow(display(), m_child, m_parent, xIndent(), yIndent());
}
```
**Extension:** Fullscreen restore uses the same `reparent()` call to move the child back into the border frame.

---

### `src/Buttons.cpp` (controller, event-driven)

**Analog:** `src/Buttons.cpp` (extending existing file)

**Button dispatch pattern** (lines 16-29, `eventButton()`):
```cpp
void WindowManager::eventButton(XButtonEvent *e)
{
    if (e->button == Button3) {
        circulate(e->window == e->root);
        return;
    }

    Client *c = windowToClient(e->window);

    if (e->window == e->root) {
        if (e->button == Button1) menu(e);
    } else if (c) {
        c->eventButton(e);
    }
}
```
**Extension:** The maximize toggle (D-08) is detected in `Border::eventButton()` (or `Client::eventButton()`) when a specific button (Button2 recommended per open question O-02) is clicked on the tab or frame area. The fullscreen circular gesture (D-06) is detected during right-button drag on client windows -- add motion tracking in a new gesture detection block.

**Event loop with grab pattern** (lines 106-316, `menu()`):
```cpp
if (attemptGrab(m_menuWindow, None, MenuGrabMask, e->time) != GrabSuccess) {
    XUnmapWindow(display(), m_menuWindow);
    return;
}

bool done = false;
XEvent event;

while (!done) {
    XMaskEvent(display(), MenuMask, &event);
    switch (event.type) {
    // ...
    case MotionNotify:
        // track pointer position
        break;
    case ButtonRelease:
        // finalize
        done = true;
        break;
    }
}
```
**Extension:** The circular gesture detection follows this event loop with grab pattern: grab pointer on right-button press on a client window, track `MotionNotify` events, compute angular sweep around centroid, and if threshold exceeded, toggle fullscreen on release. Use `attemptGrab()` at start and `releaseGrab()` at end.

**Motion tracking pattern** (lines 224-252, menu MotionNotify handler):
```cpp
case MotionNotify:
    if (!drawn) break;
    x = event.xbutton.x;
    y = event.xbutton.y;
    prev = selecting;
    selecting = y / entryHeight;
    // ... position tracking ...
    break;
```
**Extension:** The gesture detection tracks motion positions in a similar way: accumulate (x, y) positions, compute centroid of all points, measure angular sweep using cross-product/atan2 of successive motion vectors relative to centroid.

---

### `tests/test_ewmh.cpp` (test, request-response)

**Analog:** `tests/test_client.cpp`

**Imports and setup pattern** (lines 1-9):
```cpp
#include <catch2/catch_test_macros.hpp>
#include "x11wrap.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <cstdlib>
#include <cstring>
#include <type_traits>
```

**Display connection pattern** (used throughout):
```cpp
x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
REQUIRE(display != nullptr);
Window root = DefaultRootWindow(display.get());
```

**Atom interning and property test pattern** (lines 187-222):
```cpp
TEST_CASE("WM_STATE property round-trip with ClientState values", "[client][state]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);
    Window root = DefaultRootWindow(display.get());
    Window w = XCreateSimpleWindow(display.get(), root, 0, 0, 100, 100, 0, 0, 0);
    REQUIRE(w != None);

    Atom wm_state = XInternAtom(display.get(), "WM_STATE", false);
    REQUIRE(wm_state != None);

    // Write property
    long data[2];
    data[0] = static_cast<long>(ClientState::Normal);
    data[1] = static_cast<long>(None);
    XChangeProperty(display.get(), w, wm_state, wm_state, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(data), 2);
    XSync(display.get(), false);

    // Read back and verify
    Atom type;
    int format;
    unsigned long n, extra;
    long* p = nullptr;
    int status = XGetWindowProperty(display.get(), w, wm_state, 0, 2, false,
                                    wm_state, &type, &format, &n, &extra,
                                    reinterpret_cast<unsigned char**>(&p));
    REQUIRE(status == Success);
    REQUIRE(n == 2);
    REQUIRE(p != nullptr);
    REQUIRE(p[0] == static_cast<long>(ClientState::Normal));
    XFree(p);

    XDestroyWindow(display.get(), w);
}
```
**Extension:** EWMH tests follow this exact pattern: intern EWMH atoms, set properties on root/client windows, read them back with `XGetWindowProperty`, verify values, `XFree` results. Test tags should be `[ewmh]`.

**CMake registration pattern** (CMakeLists.txt lines 128-142):
```cmake
if(XVFB_EXECUTABLE)
    add_executable(test_client tests/test_client.cpp)
    target_link_libraries(test_client PRIVATE
        Catch2::Catch2WithMain
        PkgConfig::X11
    )
    target_include_directories(test_client PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${XFT_INCLUDE_DIRS}
    )
    catch_discover_tests(test_client
        PROPERTIES
            ENVIRONMENT "DISPLAY=:99"
            FIXTURES_REQUIRED xvfb_display
    )
endif()
```
**Extension:** Add `test_ewmh` executable following this exact pattern. Needs Xvfb because it tests X11 atom interning and property manipulation. Link against `Catch2::Catch2WithMain` and `PkgConfig::X11`. Include `${CMAKE_SOURCE_DIR}/include`.

## Shared Patterns

### Atom Interning
**Source:** `src/Manager.cpp` lines 96-103
**Apply to:** `src/Manager.cpp` (add ~22 new EWMH atoms)
```cpp
// Pattern: XInternAtom(display(), "ATOM_NAME", false);
// All atoms are interned once in constructor, stored as static members of Atoms struct
Atoms::net_supported = XInternAtom(display(), "_NET_SUPPORTED", false);
Atoms::net_supportingWmCheck = XInternAtom(display(), "_NET_SUPPORTING_WM_CHECK", false);
// ... etc
```

### XChangeProperty on Root Window
**Source:** `src/Manager.cpp` lines 366-369, `src/Client.cpp` lines 348-351
**Apply to:** `src/Manager.cpp` (EWMH root properties), `src/Client.cpp` (per-client EWMH state)
```cpp
// Pattern for setting root window properties:
XChangeProperty(display(), m_root, <atom>, <type>, 32, PropModeReplace,
                reinterpret_cast<unsigned char*>(<data>), <count>);
// Types: XA_ATOM for atom arrays, XA_WINDOW for window IDs, XA_CARDINAL for integers
// For UTF8_STRING: type=Atoms::utf8_string, format=8 (not 32)
```

### XGetWindowProperty for Reading Client Properties
**Source:** `src/Client.cpp` lines 292-307, 374-394
**Apply to:** `src/Client.cpp` (reading _NET_WM_WINDOW_TYPE, _NET_WM_STRUT)
```cpp
// Pattern: use getProperty_aux() helper
Atom *data = nullptr;
int n = getProperty_aux(display(), m_window, <atom>, <type>, <maxLen>,
                        reinterpret_cast<unsigned char**>(&data));
if (n > 0) {
    for (int i = 0; i < n; ++i) {
        if (data[i] == <targetAtom>) { /* match */ break; }
    }
    XFree(data);
}
```

### EWMH ClientMessage Dispatch
**Source:** `src/Events.cpp` lines 284-297
**Apply to:** `src/Events.cpp` (extend eventClient)
```cpp
// Pattern: check message_type, validate format, dispatch to client method
if (e->message_type == Atoms::net_activeWindow) {
    if (c && c->isNormal()) c->activate();
    return;
}
if (e->message_type == Atoms::net_wmState) {
    if (c && e->format == 32) {
        int action = static_cast<int>(e->data.l[0]); // 0=remove, 1=add, 2=toggle
        Atom prop1 = static_cast<Atom>(e->data.l[1]);
        Atom prop2 = static_cast<Atom>(e->data.l[2]);
        // dispatch based on prop atoms and action
    }
    return;
}
```

### Test Fixture (Xvfb)
**Source:** `CMakeLists.txt` lines 70-102, 128-142
**Apply to:** `CMakeLists.txt` (add test_ewmh)
```cmake
# Requires Xvfb fixture, same DISPLAY=:99, same FIXTURES_REQUIRED xvfb_display
# Links: Catch2::Catch2WithMain + PkgConfig::X11
```

### Error Handling
**Source:** `src/Manager.cpp` lines 171-177, `src/Client.cpp` lines 14, 206
**Apply to:** All new code
```cpp
// Fatal errors: windowManager()->fatal("message") -- exits with perror
// Warnings: std::fprintf(stderr, "wm2: warning: ...\n") -- continues
// No exceptions (C++17 but no exception usage in the codebase)
```

### Dock/Notification Window Handling
**Source:** `src/Client.cpp` lines 97-199 (manage method), `src/Border.cpp` lines 24-92 (constructor)
**Apply to:** `src/Client.cpp` (getWindowType, manage extension for DOCK/NOTIFICATION)
```cpp
// Pattern for DOCK windows: skip border decoration entirely (do not call m_border->configure
// or m_border->reparent), position at requested coordinates, keep above normal windows.
// Pattern for NOTIFICATION: same as DOCK -- no border, honor requested position, float above.
// Both need: do NOT call m_border->reparent(), do NOT call m_border->map().
// They stay as direct children of the root window.
```

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| (none) | | | All files have exact analogs (they are modifications to existing files) |

**Note:** The circular right-button gesture for fullscreen toggle (D-06) has no direct analog in the current codebase. The closest pattern is the button event loop with grab in `Border::eventButton()` (lines 878-1003) and the `menu()` event loop (lines 106-316). The planner should combine the grab+event-loop pattern from those with geometry math (centroid, angular sweep) for gesture detection. The RESEARCH.md open question O-01 recommends detecting on client windows only (not root) to avoid conflicting with circulate.

**Note:** The maximize toggle click (D-08) extends `Border::eventButton()`. Currently Button1 on tab dispatches to `m_client->move(e)` (line 894), and Button1 on parent dispatches to `m_client->moveOrResize(e)` (line 890-891). The new behavior adds a button check (likely Button2 per RESEARCH.md open question O-02) on the tab window that calls `m_client->toggleMaximized()` instead of move.

## Metadata

**Analog search scope:** `include/`, `src/`, `tests/`, `CMakeLists.txt`
**Files scanned:** 15 source files + 7 test files + 1 build file
**Pattern extraction date:** 2026-05-07
