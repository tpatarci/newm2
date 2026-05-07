#include <catch2/catch_test_macros.hpp>
#include "x11wrap.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <cstdlib>
#include <cstring>
#include <type_traits>

// ClientState enum is in Client.h, which needs WindowManager -- include just the enum
// definition by including the header that declares it.
#include "Client.h"

// ---------------------------------------------------------------------------
// Test Group 1: ServerGrab RAII
// ---------------------------------------------------------------------------

TEST_CASE("ServerGrab grabs and releases X server", "[client][servergrab]")
{
    Display* d = XOpenDisplay(std::getenv("DISPLAY"));
    REQUIRE(d != nullptr);

    // Grab the server -- all other X clients are frozen
    {
        x11::ServerGrab grab(d);
        // Server is grabbed. Verify we can still make X calls (we ARE the grabber).
        Window root = DefaultRootWindow(d);
        REQUIRE(root != None);

        // Create a window while grabbed -- should work
        Window w = XCreateSimpleWindow(d, root, 0, 0, 100, 100, 0, 0, 0);
        REQUIRE(w != None);
        XDestroyWindow(d, w);
    }
    // Server is ungrabbed when ServerGrab goes out of scope

    XCloseDisplay(d);
}

TEST_CASE("ServerGrab handles null display safely", "[client][servergrab]")
{
    // Should not crash with null display
    x11::ServerGrab grab(nullptr);
    // If we reach here without crashing, the null-safety works
    REQUIRE(true);
}

TEST_CASE("ServerGrab is not copyable", "[client][servergrab]")
{
    REQUIRE(!std::is_copy_constructible_v<x11::ServerGrab>);
    REQUIRE(!std::is_copy_assignable_v<x11::ServerGrab>);
}

// ---------------------------------------------------------------------------
// Test Group 2: ClientState enum correctness
// ---------------------------------------------------------------------------

TEST_CASE("ClientState enum values match X11 constants", "[client][state]")
{
    // These must match X11 WM_STATE constants exactly (Pitfall 5)
    REQUIRE(static_cast<int>(ClientState::Withdrawn) == WithdrawnState);
    REQUIRE(static_cast<int>(ClientState::Normal)    == NormalState);
    REQUIRE(static_cast<int>(ClientState::Iconic)    == IconicState);

    // Verify specific values: Withdrawn=0, Normal=1, Iconic=3 (NOT 2)
    REQUIRE(static_cast<int>(ClientState::Withdrawn) == 0);
    REQUIRE(static_cast<int>(ClientState::Normal)    == 1);
    REQUIRE(static_cast<int>(ClientState::Iconic)    == 3);
}

TEST_CASE("ClientState is a scoped enum", "[client][state]")
{
    REQUIRE(std::is_enum_v<ClientState>);
    REQUIRE(!std::is_convertible_v<ClientState, int>);
}

// ---------------------------------------------------------------------------
// Test Group 3: Window lifecycle operations on Xvfb
// ---------------------------------------------------------------------------

TEST_CASE("Window creation and destruction on Xvfb", "[client][lifecycle]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    Window root = DefaultRootWindow(display.get());

    // Create a top-level window
    Window w = XCreateSimpleWindow(display.get(), root, 10, 10, 200, 150, 1,
                                   BlackPixel(display.get(), 0),
                                   WhitePixel(display.get(), 0));
    REQUIRE(w != None);

    // Map it
    XMapWindow(display.get(), w);
    XSync(display.get(), false);

    // Verify we can get window attributes
    XWindowAttributes attr;
    REQUIRE(XGetWindowAttributes(display.get(), w, &attr) != 0);
    REQUIRE(attr.width == 200);
    REQUIRE(attr.height == 150);

    // Unmap it
    XUnmapWindow(display.get(), w);
    XSync(display.get(), false);

    // Destroy it
    XDestroyWindow(display.get(), w);
    XSync(display.get(), false);
}

TEST_CASE("Window move and resize operations", "[client][lifecycle]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    Window root = DefaultRootWindow(display.get());

    Window w = XCreateSimpleWindow(display.get(), root, 0, 0, 100, 100, 0, 0, 0);
    REQUIRE(w != None);

    // Move
    XMoveWindow(display.get(), w, 50, 75);
    XSync(display.get(), false);

    XWindowAttributes attr;
    XGetWindowAttributes(display.get(), w, &attr);
    REQUIRE(attr.x == 50);
    REQUIRE(attr.y == 75);

    // Resize
    XResizeWindow(display.get(), w, 300, 200);
    XSync(display.get(), false);

    XGetWindowAttributes(display.get(), w, &attr);
    REQUIRE(attr.width == 300);
    REQUIRE(attr.height == 200);

    // Move + resize
    XMoveResizeWindow(display.get(), w, 10, 20, 150, 120);
    XSync(display.get(), false);

    XGetWindowAttributes(display.get(), w, &attr);
    REQUIRE(attr.x == 10);
    REQUIRE(attr.y == 20);
    REQUIRE(attr.width == 150);
    REQUIRE(attr.height == 120);

    XDestroyWindow(display.get(), w);
}

TEST_CASE("Window reparenting on Xvfb", "[client][lifecycle]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    Window root = DefaultRootWindow(display.get());

    // Create parent window
    Window parent = XCreateSimpleWindow(display.get(), root, 0, 0, 400, 400, 0, 0, 0);
    REQUIRE(parent != None);

    // Create child window
    Window child = XCreateSimpleWindow(display.get(), root, 10, 10, 100, 100, 0, 0, 0);
    REQUIRE(child != None);

    // Reparent with ServerGrab (matches Border::reparent pattern)
    {
        x11::ServerGrab grab(display.get());
        XReparentWindow(display.get(), child, parent, 5, 5);
    }
    XSync(display.get(), false);

    // Verify child is now child of parent
    Window actual_root, actual_parent;
    Window* children;
    unsigned int nchildren;
    XQueryTree(display.get(), child, &actual_root, &actual_parent, &children, &nchildren);
    REQUIRE(actual_parent == parent);
    if (children) XFree(children);

    XDestroyWindow(display.get(), child);
    XDestroyWindow(display.get(), parent);
}

TEST_CASE("WM_STATE property round-trip with ClientState values", "[client][state]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    Window root = DefaultRootWindow(display.get());
    Window w = XCreateSimpleWindow(display.get(), root, 0, 0, 100, 100, 0, 0, 0);
    REQUIRE(w != None);

    Atom wm_state = XInternAtom(display.get(), "WM_STATE", false);
    REQUIRE(wm_state != None);

    // Write NormalState (1) via ClientState enum value
    long data[2];
    data[0] = static_cast<long>(ClientState::Normal);  // = 1
    data[1] = static_cast<long>(None);
    XChangeProperty(display.get(), w, wm_state, wm_state, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(data), 2);
    XSync(display.get(), false);

    // Read back
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

TEST_CASE("Colormap window property read and free", "[client][colormap]")
{
    // Verify X11 property read + XFree pattern used by getColormaps
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    Window root = DefaultRootWindow(display.get());
    Window w = XCreateSimpleWindow(display.get(), root, 0, 0, 100, 100, 0, 0, 0);
    REQUIRE(w != None);

    // Write a WM_COLORMAP_WINDOWS property with a window list
    Atom wm_colormaps = XInternAtom(display.get(), "WM_COLORMAP_WINDOWS", false);
    Window windows[2] = { w, root };
    XChangeProperty(display.get(), w, wm_colormaps, XA_WINDOW, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(windows), 2);
    XSync(display.get(), false);

    // Read back using the same pattern as getProperty_aux
    Window* cw = nullptr;
    Atom realType;
    int format;
    unsigned long n, extra;
    int status = XGetWindowProperty(display.get(), w, wm_colormaps, 0, 100, false,
                                    XA_WINDOW, &realType, &format, &n, &extra,
                                    reinterpret_cast<unsigned char**>(&cw));
    REQUIRE(status == Success);
    REQUIRE(n == 2);
    REQUIRE(cw != nullptr);
    REQUIRE(cw[0] == w);
    REQUIRE(cw[1] == root);

    // CRITICAL: XFree the X11-allocated memory (not our vector)
    XFree(cw);

    XDestroyWindow(display.get(), w);
}

TEST_CASE("Multiple windows managed simultaneously", "[client][lifecycle]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    Window root = DefaultRootWindow(display.get());
    constexpr int count = 5;

    // Create multiple windows (simulating multiple clients)
    Window windows[count];
    for (int i = 0; i < count; ++i) {
        windows[i] = XCreateSimpleWindow(display.get(), root, i * 50, i * 50,
                                         100, 100, 0, 0, 0);
        REQUIRE(windows[i] != None);
        XMapWindow(display.get(), windows[i]);
    }
    XSync(display.get(), false);

    // Verify all exist
    for (int i = 0; i < count; ++i) {
        XWindowAttributes attr;
        REQUIRE(XGetWindowAttributes(display.get(), windows[i], &attr) != 0);
        REQUIRE(attr.map_state == IsViewable);
    }

    // Unmap one (simulate hide)
    XUnmapWindow(display.get(), windows[2]);
    XSync(display.get(), false);

    XWindowAttributes attr;
    XGetWindowAttributes(display.get(), windows[2], &attr);
    REQUIRE(attr.map_state == IsUnmapped);

    // Destroy all
    for (int i = 0; i < count; ++i) {
        XDestroyWindow(display.get(), windows[i]);
    }
    XSync(display.get(), false);
}
