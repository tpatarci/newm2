# Phase 1: Build Infrastructure + RAII Foundation - Pattern Map

**Mapped:** 2026-05-06
**Files analyzed:** 14 (all new -- greenfield project)
**Analogs found:** 8 / 14 (upstream reference code, not compiled)

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `CMakeLists.txt` | config | build | `upstream-wm2/Makefile` | structural (link targets) |
| `include/x11wrap.h` | utility | request-response | `upstream-wm2/General.h` | conceptual (X11 types only) |
| `include/Manager.h` | model | request-response | `upstream-wm2/Manager.h` | exact (class skeleton) |
| `include/Client.h` | model | request-response | `upstream-wm2/Client.h` | exact (class skeleton) |
| `include/Border.h` | model | request-response | `upstream-wm2/Border.h` | exact (class skeleton) |
| `include/Cursors.h` | utility | N/A (static data) | `upstream-wm2/Cursors.h` | exact (copy as-is) |
| `src/main.cpp` | controller | request-response | `upstream-wm2/Main.C` | exact (entry point) |
| `src/Manager.cpp` | controller | event-driven | `upstream-wm2/Manager.C` | exact (core lifecycle) |
| `src/Client.cpp` | service | CRUD | `upstream-wm2/Client.C` | exact (window lifecycle) |
| `src/Border.cpp` | component | request-response | `upstream-wm2/Border.C` | exact (frame rendering) |
| `src/Buttons.cpp` | controller | event-driven | `upstream-wm2/Buttons.C` | exact (button handlers) |
| `src/Events.cpp` | controller | event-driven | `upstream-wm2/Events.C` | exact (event dispatch) |
| `tests/test_raii.cpp` | test | unit | (no upstream analog -- new) | no-match |
| `tests/test_smoke.cpp` | test | integration | (no upstream analog -- new) | no-match |

## Pattern Assignments

### `CMakeLists.txt` (config, build)

**Analog:** `upstream-wm2/Makefile` (structural reference only -- link targets, source files)

**Link targets from upstream Makefile** (lines 2, 8):
```
LIBS = -L/usr/X11R6/lib -lXext -lX11 -lXt -lXmu -lSM -lICE -lm
OBJECTS = Border.o Buttons.o Client.o Events.o Main.o Manager.o Rotated.o
```

**CMake pattern** (from RESEARCH.md, verified against Context7 CMake docs):
```cmake
cmake_minimum_required(VERSION 3.16)
project(wm2-born-again VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# X11 dependencies via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
pkg_check_modules(XEXT REQUIRED IMPORTED_TARGET xext)

# WM binary
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/Manager.cpp
    src/Client.cpp
    src/Border.cpp
    src/Buttons.cpp
    src/Events.cpp
    upstream-wm2/Rotated.C
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/upstream-wm2
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    PkgConfig::X11
    PkgConfig::XEXT
    m
)

# Relax warnings for third-party code
set_source_files_properties(upstream-wm2/Rotated.C PROPERTIES
    COMPILE_FLAGS "-w"
)
```

**Test harness pattern** (Catch2 + Xvfb from RESEARCH.md):
```cmake
option(BUILD_TESTS "Build test suite" ON)

if(BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.14.0
    )
    FetchContent_MakeAvailable(Catch2)

    enable_testing()
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)

    # RAII unit tests
    add_executable(test_raii tests/test_raii.cpp)
    target_link_libraries(test_raii PRIVATE Catch2::Catch2WithMain)
    target_include_directories(test_raii PRIVATE ${CMAKE_SOURCE_DIR}/include)
    catch_discover_tests(test_raii)

    # Smoke tests (need Xvfb)
    find_program(XVFB_EXECUTABLE Xvfb)
    if(XVFB_EXECUTABLE)
        add_executable(test_smoke tests/test_smoke.cpp)
        target_link_libraries(test_smoke PRIVATE
            Catch2::Catch2WithMain
            PkgConfig::X11
        )
        target_include_directories(test_smoke PRIVATE ${CMAKE_SOURCE_DIR}/include)
        catch_discover_tests(test_smoke)

        set(XVFB_PID_FILE "${CMAKE_BINARY_DIR}/xvfb.pid")
        add_test(NAME start_xvfb
            COMMAND bash -c "Xvfb :99 -screen 0 1024x768x24 -ac +render -noreset & echo $! > ${XVFB_PID_FILE} && sleep 1"
        )
        set_tests_properties(start_xvfb PROPERTIES FIXTURES_SETUP xvfb_display)

        add_test(NAME stop_xvfb
            COMMAND bash -c "kill $(cat ${XVFB_PID_FILE}) 2>/dev/null || true"
        )
        set_tests_properties(stop_xvfb PROPERTIES FIXTURES_CLEANUP xvfb_display)

        set_tests_properties(
            ${test_smoke_DISCOVERED_TESTS}
            PROPERTIES
                ENVIRONMENT "DISPLAY=:99"
                FIXTURES_REQUIRED xvfb_display
        )
    else()
        message(WARNING "Xvfb not found -- smoke tests disabled")
    endif()
endif()
```

**Key differences from upstream Makefile:**
- No hardcoded `/usr/X11R6` paths (BLD-05)
- pkg-config discovers X11 libs instead of manual `-l` flags
- Drops `-lXt -lXmu -lSM -lICE` (CONCERNS.md notes they appear unused)
- Adds Catch2 test targets
- Compiles `upstream-wm2/Rotated.C` with `-w` for third-party tolerance

---

### `include/x11wrap.h` (utility, request-response)

**Analog:** `upstream-wm2/General.h` (X11 type imports and utility definitions)

**No direct code analog exists** -- this is a new concept (RAII wrappers). The upstream uses raw X11 resources with manual cleanup in destructors. The pattern comes from RESEARCH.md verified against Xlib specification and C++ standard.

**X11 type imports from General.h** (lines 22-29):
```cpp
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xos.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
```

**Manual cleanup pattern that RAII replaces** -- `WindowManager::release()` in `Manager.C` lines 159-168:
```cpp
XFreeCursor(m_display, m_cursor);
XFreeCursor(m_display, m_xCursor);
XFreeCursor(m_display, m_vCursor);
XFreeCursor(m_display, m_hCursor);
XFreeCursor(m_display, m_vhCursor);

XFreeFont(m_display, m_menuFont);
XFreeGC(m_display, m_menuGC);

XCloseDisplay(m_display);
```

**Border destructor cleanup** in `Border.C` lines 93-112:
```cpp
Border::~Border()
{
    if (m_parent != root()) {
        if (!m_parent) fprintf(stderr,"wm2: zero parent in Border::~Border\n");
        else {
            XDestroyWindow(display(), m_tab);
            XDestroyWindow(display(), m_button);
            XDestroyWindow(display(), m_parent);
            XDestroyWindow(display(), m_resize);
        }
    }
    if (m_label) free(m_label);
    if (--borderCounter == 0) {
        XFreeGC(display(), m_drawGC);
    }
}
```

**New RAII wrapper code** (from RESEARCH.md, verified against Xlib spec):

Pointer types -- `unique_ptr` + custom deleters:
```cpp
#pragma once
#include <X11/Xlib.h>
#include <memory>

namespace x11 {

struct DisplayDeleter {
    void operator()(Display* d) const noexcept {
        if (d) XCloseDisplay(d);
    }
};
using DisplayPtr = std::unique_ptr<Display, DisplayDeleter>;

struct GCDeleter {
    Display* display = nullptr;
    void operator()(GC gc) const noexcept {
        if (display && gc) XFreeGC(display, gc);
    }
};
using GCPtr = std::unique_ptr<std::remove_pointer_t<GC>, GCDeleter>;

inline GCPtr make_gc(Display* d, Window w, unsigned long mask, XGCValues* vals) {
    GCPtr p{ XCreateGC(d, w, mask, vals) };
    p.get_deleter().display = d;
    return p;
}

struct FontStructDeleter {
    Display* display = nullptr;
    void operator()(XFontStruct* fs) const noexcept {
        if (display && fs) XFreeFont(display, fs);
    }
};
using FontStructPtr = std::unique_ptr<XFontStruct, FontStructDeleter>;

} // namespace x11
```

XID types -- thin RAII class (from RESEARCH.md):
```cpp
namespace x11 {

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

    Cursor get() const noexcept { return m_cursor; }
    operator Cursor() const noexcept { return m_cursor; }
    explicit operator bool() const noexcept { return m_cursor != None; }

private:
    Display* m_display;
    Cursor m_cursor;
};

// Same pattern for UniqueFont, UniquePixmap, UniqueColormap
// with appropriate XFree* function.

} // namespace x11
```

**CRITICAL distinction (from RESEARCH.md):** Only `Display*`, `GC` (which is `struct _XGC*`), and `XFontStruct*` are actual pointer types. `Font`, `Cursor`, `Pixmap`, `Colormap` are all `XID` (`unsigned long`) -- they need thin RAII classes, not `unique_ptr`.

---

### `include/Manager.h` (model, request-response)

**Analog:** `upstream-wm2/Manager.h`

**Header guard pattern:**
```cpp
#ifndef _MANAGER_H_
#define _MANAGER_H_
```

**Class skeleton from upstream** (Manager.h lines 12-128):
```cpp
class WindowManager {
public:
    WindowManager();
    ~WindowManager();

    void fatal(const char *);

    Client *windowToClient(Window, Boolean create = False);
    Client *activeClient() { return m_activeClient; }
    // ... accessors ...

    Display *display() { return m_display; }
    Window root() { return m_root; }

private:
    int loop();
    void release();

    Display *m_display;
    int m_screenNumber;
    Window m_root;
    Colormap m_defaultColormap;
    Cursor m_cursor;
    // ... more members ...

    ClientList m_clients;
    ClientList m_hiddenClients;
    Client *m_activeClient;
    // ... more members ...
};
```

**Modernization changes for new code:**
- `Boolean` becomes `bool`
- `ClientList` (macro-based) becomes `std::vector<Client*>`
- Raw `Cursor`, `Colormap` become RAII wrappers from `x11wrap.h`
- `Display*` becomes `x11::DisplayPtr`
- `GC` becomes `x11::GCPtr`
- `XFontStruct*` becomes `x11::FontStructPtr`
- `char*` becomes `std::string`
- Add `#include <vector>`, `#include <string>`, `#include "x11wrap.h"`
- Forward declare `class Client;`
- Use `#pragma once` or modern header guards

---

### `include/Client.h` (model, request-response)

**Analog:** `upstream-wm2/Client.h`

**Header guard pattern:**
```cpp
#ifndef _CLIENT_H_
#define _CLIENT_H_
```

**Class skeleton from upstream** (Client.h lines 10-145):
```cpp
class Client {
public:
    Client(WindowManager *const, Window);
    void release();

    void activate();
    void deactivate();
    void gravitate(Boolean invert);
    // ... more methods ...

    Boolean hasWindow(Window w) {
        return ((m_window == w) || m_border->hasWindow(w));
    }

    Boolean isHidden()     { return (m_state == IconicState);    }
    Boolean isWithdrawn()  { return (m_state == WithdrawnState); }
    Boolean isNormal()     { return (m_state == NormalState);    }
    Boolean isTransient()  { return (m_transient != None);       }

private:
    Window m_window;
    Window m_transient;
    Border *m_border;
    Client *m_revert;
    int m_x, m_y, m_w, m_h, m_bw;
    XSizeHints m_sizeHints;
    Boolean m_fixedSize;
    int m_state;
    int m_protocol;
    char *m_name;
    char *m_iconName;
    const char *m_label;
    Colormap m_colormap;
    WindowManager *const m_windowManager;
    // ... more members ...
};
```

**Modernization changes:**
- `Boolean` -> `bool`
- `Border *m_border` -> `std::unique_ptr<Border>` (or raw with explicit ownership)
- `char *m_name` -> `std::string`
- `const char *m_label` -> `std::string_view` or `const std::string&`
- Forward declare `class Border;`, `class WindowManager;`
- Protocol constants: `enum class Protocol { Delete = 1, TakeFocus = 2 }`

---

### `include/Border.h` (model, request-response)

**Analog:** `upstream-wm2/Border.h`

**Class skeleton from upstream** (Border.h lines 20-97):
```cpp
class Border {
public:
    Border(Client *const, Window child);
    ~Border();

    void map();
    void unmap();
    void decorate(Boolean active, int w, int h);
    void reparent();
    void configure(int x, int y, int w, int h, unsigned long mask, int detail,
                   Boolean force = False);
    void moveTo(int x, int y);

    Window parent() { return m_parent; }
    Boolean hasWindow(Window w) {
        return (w != root() && (w == m_parent || w == m_tab ||
                                w == m_button || w == m_resize));
    }

private:
    Client *m_client;
    Window m_parent;
    Window m_tab;
    Window m_child;
    Window m_button;
    Window m_resize;
    char *m_label;

    static int m_tabWidth;
    static XRotFontStruct *m_tabFont;
    static GC m_drawGC;
    // ... pixel members ...
};
```

**Modernization changes:**
- `Boolean` -> `bool`
- `Window` stays raw (D-06)
- `GC m_drawGC` becomes `x11::GCPtr` (static, needs careful init)
- `char *m_label` -> `std::string`
- Forward declarations: `class Client;`, `class WindowManager;`

---

### `include/Cursors.h` (utility, N/A -- static data)

**Analog:** `upstream-wm2/Cursors.h`

**Can be used as-is** -- it contains only `#define` constants and `static unsigned char` arrays for cursor bitmap data. No C++ modernization needed. Copy from upstream unchanged.

**Structure** (Cursors.h lines 1-85):
```cpp
#ifndef _WM2_CURSORS_H_
#define _WM2_CURSORS_H_

#define cursor_width 16
#define cursor_height 16
static unsigned char cursor_bits[] = { /* ... */ };
// ... more cursor definitions ...
#endif
```

---

### `src/main.cpp` (controller, request-response)

**Analog:** `upstream-wm2/Main.C`

**Upstream entry point** (Main.C lines 1-20):
```cpp
#include "Manager.h"
#include "Client.h"
#include "Border.h"

int main(int argc, char **argv)
{
    int i;

    if (argc > 1) {
        for (i = strlen(argv[0])-1; i > 0 && argv[0][i] != '/'; --i);
        fprintf(stderr, "usage: %s\n", argv[0] + (i > 0) + i);
        exit(2);
    }

    WindowManager manager;
    return 0;
}
```

**Modernized pattern:**
```cpp
#include "Manager.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc > 1) {
        std::fprintf(stderr, "usage: %s\n", argv[0]);
        return 2;
    }

    WindowManager manager;
    return 0;
}
```

**Key changes:**
- `<cstdio>`, `<cstring>`, `<cstdlib>` instead of C headers
- Use `std::fprintf` or `std::cerr`
- Simplified usage message (upstream's basename extraction is unnecessary complexity for Phase 1)
- WindowManager constructor still drives everything (singleton pattern preserved)

---

### `src/Manager.cpp` (controller, event-driven)

**Analog:** `upstream-wm2/Manager.C`

**Constructor pattern** (Manager.C lines 27-117):
```cpp
WindowManager::WindowManager() :
    m_menuGC(0), m_menuWindow(0), m_menuFont(0), m_focusChanging(False)
{
    // Banner
    fprintf(stderr, "\nwm2: Copyright (c) 1996-7 Chris Cannam. ...\n");

    // Open display
    m_display = XOpenDisplay(NULL);
    if (!m_display) fatal("can't open display");

    // Setup error handler
    XSetErrorHandler(errorHandler);

    // Signal handling
    signal(SIGTERM, sigHandler);
    signal(SIGINT,  sigHandler);
    signal(SIGHUP,  sigHandler);

    // Atom initialization
    Atoms::wm_state      = XInternAtom(m_display, "WM_STATE", False);
    // ... more atoms ...

    // Shape extension check
    if (!XShapeQueryExtension(m_display, &m_shapeEvent, &dummy))
        fatal("no shape extension, can't run without it");

    // Screen init, window scan, event loop
    initialiseScreen();
    scanInitialWindows();
    loop();
}
```

**Event loop pattern** (Manager.C, `loop()` in Events.C lines 6-81):
```cpp
int WindowManager::loop()
{
    XEvent ev;
    m_looping = True;

    while (m_looping) {
        nextEvent(&ev);
        if (!m_looping) break;

        switch (ev.type) {
        case ButtonPress:   eventButton(&ev.xbutton);      break;
        case MapRequest:    eventMapRequest(&ev.xmaprequest); break;
        case ConfigureRequest: eventConfigureRequest(&ev.xconfigurerequest); break;
        case UnmapNotify:   eventUnmap(&ev.xunmap);        break;
        case DestroyNotify: eventDestroy(&ev.xdestroywindow); break;
        case PropertyNotify: eventProperty(&ev.xproperty); break;
        case EnterNotify:
        case LeaveNotify:   eventEnter(&ev.xcrossing);     break;
        // ... more cases ...
        }
    }
    return m_returnCode;
}
```

**Error handling pattern** (Manager.C lines 172-208):
```cpp
void WindowManager::fatal(const char *message)
{
    fprintf(stderr, "wm2: ");
    perror(message);
    fprintf(stderr, "\n");
    exit(1);
}

int WindowManager::errorHandler(Display *d, XErrorEvent *e)
{
    if (m_initialising && (e->request_code == X_ChangeWindowAttributes) &&
        e->error_code == BadAccess) {
        fprintf(stderr, "wm2: another window manager running?\n");
        exit(1);
    }

    if (ignoreBadWindowErrors == True && e->error_code == BadWindow) return 0;

    char msg[100], number[30], request[100];
    XGetErrorText(d, e->error_code, msg, 100);
    // ... logging ...
    return 0;
}
```

**Resource cleanup in release()** (Manager.C lines 126-168):
```cpp
void WindowManager::release()
{
    // Unparent all clients
    // Free cursors, fonts, GCs
    // Close display
    XFreeCursor(m_display, m_cursor);
    // ... more cleanup ...
    XCloseDisplay(m_display);
}
```

**Modernization changes:**
- `m_display` becomes `x11::DisplayPtr` (auto-cleanup)
- Cursors become `x11::UniqueCursor` (auto-cleanup)
- GC becomes `x11::GCPtr` (auto-cleanup)
- `ClientList` becomes `std::vector<std::unique_ptr<Client>>` or `std::vector<Client*>`
- `fatal()` may throw or call `std::exit(1)` -- same behavior
- Error handler stays as static/Xlib callback (X11 uses async error handling, not exceptions)
- Signal handling: use `sigaction()` directly instead of upstream's `signal()` macro wrapper
- `m_signalled` flag pattern preserved (set in handler, checked in loop)

---

### `src/Client.cpp` (service, CRUD)

**Analog:** `upstream-wm2/Client.C`

**Constructor pattern** (Client.C lines 11-42):
```cpp
Client::Client(WindowManager *const wm, Window w) :
    m_window(w),
    m_transient(None),
    m_revert(0),
    m_fixedSize(False),
    m_state(WithdrawnState),
    m_managed(False),
    m_reparenting(False),
    m_windowManager(wm)
{
    XWindowAttributes attr;
    XGetWindowAttributes(display(), m_window, &attr);

    m_x = attr.x;
    m_y = attr.y;
    m_w = attr.width;
    m_h = attr.height;
    m_bw = attr.border_width;
    m_name = m_iconName = 0;
    m_sizeHints.flags = 0L;
    m_label = NewString(m_defaultLabel);
    m_border = new Border(this, w);

    if (attr.map_state == IsViewable) manage(True);
}
```

**Self-deletion pattern** (Client.C lines 51-92):
```cpp
void Client::release()
{
    // cleanup ...
    delete m_border;
    m_window = None;
    // ... more cleanup ...
    delete this;  // <-- must be replaced in modern code
}
```

**Modernization changes:**
- Replace `new Border` with `std::make_unique<Border>`
- Replace `delete this` with external ownership via WindowManager's container
- Replace `NewString()` macro with `std::string`
- Replace `malloc`/`free` with RAII
- Use `std::vector` for colormap arrays
- Keep X11 window lifecycle: MapRequest -> reparent -> manage -> unmap/destroy

---

### `src/Border.cpp` (component, request-response)

**Analog:** `upstream-wm2/Border.C`

**Constructor + static initialization** (Border.C lines 7-90):
```cpp
int Border::m_tabWidth = -1;
XRotFontStruct *Border::m_tabFont = 0;
GC Border::m_drawGC = 0;

Border::Border(Client *const c, Window child) :
    m_client(c), m_parent(0), m_tab(0),
    m_child(child), m_button(0), m_resize(0), m_label(0),
    m_prevW(-1), m_prevH(-1), m_tabHeight(-1)
{
    // First border initializes static resources (font, GC, colors)
    // Window creation for frame elements
    ++borderCounter;
}
```

**Destructor with window cleanup** (Border.C lines 93-112):
```cpp
Border::~Border()
{
    if (m_parent != root()) {
        XDestroyWindow(display(), m_tab);
        XDestroyWindow(display(), m_button);
        XDestroyWindow(display(), m_parent);
        XDestroyWindow(display(), m_resize);
    }
    if (m_label) free(m_label);
    if (--borderCounter == 0) {
        XFreeGC(display(), m_drawGC);
    }
}
```

**Modernization changes:**
- `Window` stays raw per D-06 (still call `XDestroyWindow` in destructor)
- `GC m_drawGC` becomes `x11::GCPtr` for auto-cleanup
- `char *m_label` -> `std::string`
- Static initialization stays (first Border loads font, creates GC)
- Shape extension usage preserved (XShapeCombineRectangles etc.)

---

### `src/Buttons.cpp` (controller, event-driven)

**Analog:** `upstream-wm2/Buttons.C`

**Event dispatch pattern** (Buttons.C lines 1-31):
```cpp
#include "Manager.h"
#include "Client.h"

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

**Modernization changes:**
- Same dispatch pattern, modernized types
- Button event handling is complex with pointer grabs -- keep the grab/ungrab pattern
- Menu rendering uses raw X11 drawing calls (preserved until Phase 4)

---

### `src/Events.cpp` (controller, event-driven)

**Analog:** `upstream-wm2/Events.C`

**Event loop pattern** (Events.C lines 6-81 -- the `loop()` and dispatch):
```cpp
int WindowManager::loop()
{
    XEvent ev;
    m_looping = True;
    while (m_looping) {
        nextEvent(&ev);
        switch (ev.type) {
        case ButtonPress:    eventButton(&ev.xbutton);           break;
        case MapRequest:     eventMapRequest(&ev.xmaprequest);   break;
        case ConfigureRequest: eventConfigureRequest(&ev.xconfigurerequest); break;
        case UnmapNotify:    eventUnmap(&ev.xunmap);             break;
        case CreateNotify:   eventCreate(&ev.xcreatewindow);     break;
        case DestroyNotify:  eventDestroy(&ev.xdestroywindow);   break;
        case ClientMessage:  eventClient(&ev.xclient);           break;
        case ColormapNotify: eventColormap(&ev.xcolormap);       break;
        case PropertyNotify: eventProperty(&ev.xproperty);       break;
        case EnterNotify:
        case LeaveNotify:    eventEnter(&ev.xcrossing);          break;
        case ReparentNotify: eventReparent(&ev.xreparent);       break;
        case FocusIn:        eventFocusIn(&ev.xfocus);           break;
        case Expose:         eventExposure(&ev.xexpose);         break;
        }
    }
    return m_returnCode;
}
```

**Modernization changes:**
- Same event dispatch structure preserved
- Uses `m_signalled` flag check (set by signal handler)
- `nextEvent()` uses `select()` -- will modernize to `poll()` in Phase 2 but keep `select()` for Phase 1
- All event handler signatures keep their X11 event struct pointers

---

### `tests/test_raii.cpp` (test, unit)

**No upstream analog.** Pattern from RESEARCH.md.

**Test structure** (from RESEARCH.md):
```cpp
#include <catch2/catch_test_macros.hpp>
#include "x11wrap.h"

TEST_CASE("DisplayDeleter handles null safely", "[raii]") {
    x11::DisplayDeleter deleter;
    Display* raw = nullptr;
    REQUIRE_NOTHROW(deleter(raw));
}

TEST_CASE("UniqueCursor is move-only", "[raii]") {
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<x11::UniqueCursor>);
    STATIC_REQUIRE(std::is_move_constructible_v<x11::UniqueCursor>);
}
```

---

### `tests/test_smoke.cpp` (test, integration)

**No upstream analog.** Pattern from RESEARCH.md.

**Test structure** (from RESEARCH.md):
```cpp
#include <catch2/catch_test_macros.hpp>
#include <X11/Xlib.h>
#include <cstdlib>

TEST_CASE("X11 RAII wrappers work with real display", "[smoke][raii]") {
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    int screen = DefaultScreen(display.get());
    Window root = RootWindow(display.get(), screen);
    XGCValues vals;
    auto gc = x11::make_gc(display.get(), root, 0, &vals);
    REQUIRE(gc != nullptr);
}
```

---

## Shared Patterns

### X11 Resource Lifecycle (RAII)
**Source:** `upstream-wm2/Manager.C` lines 126-168 (manual cleanup), `upstream-wm2/Border.C` lines 93-112 (manual cleanup)
**Apply to:** All files that own X11 resources (Manager.cpp, Client.cpp, Border.cpp)
**Modern pattern:** Replace manual `XFree*` calls in destructors with automatic cleanup via `x11wrap.h` RAII types. Destruction order matters: Display must outlive all resources created from it (declare `DisplayPtr` first in member list).

### Error Handling
**Source:** `upstream-wm2/Manager.C` lines 172-208
**Apply to:** Manager.cpp (global error handler and fatal function)
```cpp
void WindowManager::fatal(const char *message) {
    fprintf(stderr, "wm2: ");
    perror(message);
    fprintf(stderr, "\n");
    exit(1);
}
```
**Note:** X11 uses async error handler callbacks, not exceptions. Do NOT throw from RAII destructors. Keep `fatal()` as `std::exit(1)` and keep the static `errorHandler()` callback pattern.

### Member Variable Naming
**Source:** `upstream-wm2/Manager.h`, `upstream-wm2/Client.h`, `upstream-wm2/Border.h`
**Apply to:** All new headers and implementation files
```cpp
// m_ prefix + camelCase -- retained from upstream (D-09 preserves consistency)
Display* m_display;
Client* m_activeClient;
int m_screenNumber;
```

### Header Include Order
**Source:** `upstream-wm2/General.h` lines 1-50, all `.C` files
**Apply to:** All new source and header files
```cpp
// 1. Corresponding header (in .cpp files)
#include "Manager.h"
// 2. Related project headers
#include "Client.h"
#include "Border.h"
// 3. C++ standard library
#include <vector>
#include <string>
// 4. X11 headers
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
// 5. System headers
#include <unistd.h>
```

### Forward Declarations for Circular Dependencies
**Source:** `upstream-wm2/Manager.h` line 9, `upstream-wm2/Border.h` lines 9-10
**Apply to:** Manager.h, Client.h, Border.h
```cpp
// Manager.h
class Client;

// Border.h
class Client;
class WindowManager;
```

### Singleton WindowManager Pattern
**Source:** `upstream-wm2/Main.C` lines 17-18
**Apply to:** src/main.cpp
```cpp
// One instance, owns everything, runs event loop in constructor
WindowManager manager;
return 0;  // loop() exits, destructor cleans up
```

### Logging Prefix
**Source:** `upstream-wm2/Manager.C` line 174, throughout all files
**Apply to:** All stderr output
```cpp
fprintf(stderr, "wm2: ...");         // messages
fprintf(stderr, "wm2: warning: ..."); // warnings
```

## No Analog Found

Files with no close match in the codebase (planner should use RESEARCH.md patterns instead):

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| `CMakeLists.txt` | config | build | No CMake build system exists; upstream uses hand-rolled Makefile |
| `include/x11wrap.h` | utility | request-response | No RAII wrappers exist; upstream uses raw X11 resources with manual cleanup |
| `tests/test_raii.cpp` | test | unit | No test files exist in upstream |
| `tests/test_smoke.cpp` | test | integration | No test files or Xvfb integration exists in upstream |

## Metadata

**Analog search scope:** `upstream-wm2/` (19 files), project root (CLAUDE.md, .planning/)
**Files scanned:** 19 upstream source files + planning documents
**Pattern extraction date:** 2026-05-06
