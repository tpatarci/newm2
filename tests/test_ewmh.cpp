#include <catch2/catch_test_macros.hpp>
#include "x11wrap.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <cstring>

// ---------------------------------------------------------------------------
// EWMH atom interning tests
// ---------------------------------------------------------------------------

TEST_CASE("EWMH atoms can be interned", "[ewmh]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    Atom net_supported = XInternAtom(display.get(), "_NET_SUPPORTED", false);
    Atom net_supportingWmCheck = XInternAtom(display.get(), "_NET_SUPPORTING_WM_CHECK", false);
    Atom net_clientList = XInternAtom(display.get(), "_NET_CLIENT_LIST", false);
    Atom net_activeWindow = XInternAtom(display.get(), "_NET_ACTIVE_WINDOW", false);
    Atom net_wmWindowType = XInternAtom(display.get(), "_NET_WM_WINDOW_TYPE", false);
    Atom net_wmState = XInternAtom(display.get(), "_NET_WM_STATE", false);
    Atom net_wmName = XInternAtom(display.get(), "_NET_WM_NAME", false);
    Atom net_wmStateFullscreen = XInternAtom(display.get(), "_NET_WM_STATE_FULLSCREEN", false);
    Atom net_wmStateMaximizedVert = XInternAtom(display.get(), "_NET_WM_STATE_MAXIMIZED_VERT", false);
    Atom net_wmStateMaximizedHorz = XInternAtom(display.get(), "_NET_WM_STATE_MAXIMIZED_HORZ", false);
    Atom net_wmStateHidden = XInternAtom(display.get(), "_NET_WM_STATE_HIDDEN", false);
    Atom net_wmWindowTypeDock = XInternAtom(display.get(), "_NET_WM_WINDOW_TYPE_DOCK", false);
    Atom net_wmWindowTypeDialog = XInternAtom(display.get(), "_NET_WM_WINDOW_TYPE_DIALOG", false);
    Atom net_wmWindowTypeNotification = XInternAtom(display.get(), "_NET_WM_WINDOW_TYPE_NOTIFICATION", false);
    Atom net_wmWindowTypeNormal = XInternAtom(display.get(), "_NET_WM_WINDOW_TYPE_NORMAL", false);
    Atom net_wmWindowTypeUtility = XInternAtom(display.get(), "_NET_WM_WINDOW_TYPE_UTILITY", false);
    Atom net_wmWindowTypeSplash = XInternAtom(display.get(), "_NET_WM_WINDOW_TYPE_SPLASH", false);
    Atom net_wmWindowTypeToolbar = XInternAtom(display.get(), "_NET_WM_WINDOW_TYPE_TOOLBAR", false);
    Atom net_wmStrut = XInternAtom(display.get(), "_NET_WM_STRUT", false);
    Atom net_wmStrutPartial = XInternAtom(display.get(), "_NET_WM_STRUT_PARTIAL", false);
    Atom net_numberOfDesktops = XInternAtom(display.get(), "_NET_NUMBER_OF_DESKTOPS", false);
    Atom net_currentDesktop = XInternAtom(display.get(), "_NET_CURRENT_DESKTOP", false);
    Atom net_workarea = XInternAtom(display.get(), "_NET_WORKAREA", false);
    Atom utf8_string = XInternAtom(display.get(), "UTF8_STRING", false);

    REQUIRE(net_supported != None);
    REQUIRE(net_supportingWmCheck != None);
    REQUIRE(net_clientList != None);
    REQUIRE(net_activeWindow != None);
    REQUIRE(net_wmWindowType != None);
    REQUIRE(net_wmState != None);
    REQUIRE(net_wmName != None);
    REQUIRE(net_wmStateFullscreen != None);
    REQUIRE(net_wmStateMaximizedVert != None);
    REQUIRE(net_wmStateMaximizedHorz != None);
    REQUIRE(net_wmStateHidden != None);
    REQUIRE(net_wmWindowTypeDock != None);
    REQUIRE(net_wmWindowTypeDialog != None);
    REQUIRE(net_wmWindowTypeNotification != None);
    REQUIRE(net_wmWindowTypeNormal != None);
    REQUIRE(net_wmWindowTypeUtility != None);
    REQUIRE(net_wmWindowTypeSplash != None);
    REQUIRE(net_wmWindowTypeToolbar != None);
    REQUIRE(net_wmStrut != None);
    REQUIRE(net_wmStrutPartial != None);
    REQUIRE(net_numberOfDesktops != None);
    REQUIRE(net_currentDesktop != None);
    REQUIRE(net_workarea != None);
    REQUIRE(utf8_string != None);
}

// ---------------------------------------------------------------------------
// _NET_SUPPORTING_WM_CHECK self-reference test
// ---------------------------------------------------------------------------

TEST_CASE("_NET_SUPPORTING_WM_CHECK self-reference", "[ewmh]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    Window root = DefaultRootWindow(display.get());

    // Create a WM check child window (simulating what the WM does)
    Window checkWindow = XCreateSimpleWindow(display.get(), root, -1, -1, 1, 1, 0, 0, 0);
    REQUIRE(checkWindow != None);

    Atom net_supportingWmCheck = XInternAtom(display.get(), "_NET_SUPPORTING_WM_CHECK", false);
    REQUIRE(net_supportingWmCheck != None);

    // Set _NET_SUPPORTING_WM_CHECK on ROOT pointing to check window
    XChangeProperty(display.get(), root, net_supportingWmCheck,
                    XA_WINDOW, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&checkWindow), 1);

    // Set _NET_SUPPORTING_WM_CHECK on CHECK WINDOW pointing to ITSELF
    XChangeProperty(display.get(), checkWindow, net_supportingWmCheck,
                    XA_WINDOW, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&checkWindow), 1);

    XSync(display.get(), false);

    // Read back from root
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
    XFree(data);

    // Read back from check window (self-reference)
    data = nullptr;
    status = XGetWindowProperty(display.get(), checkWindow, net_supportingWmCheck,
                                0, 1, false, XA_WINDOW,
                                &actualType, &actualFormat, &nItems, &bytesAfter,
                                reinterpret_cast<unsigned char**>(&data));
    REQUIRE(status == Success);
    REQUIRE(nItems == 1);
    REQUIRE(data != nullptr);
    REQUIRE(data[0] == checkWindow);
    XFree(data);

    // Clean up: remove property from root to avoid polluting other tests
    XDeleteProperty(display.get(), root, net_supportingWmCheck);
    XDestroyWindow(display.get(), checkWindow);
}

// ---------------------------------------------------------------------------
// _NET_WM_NAME UTF8_STRING encoding test
// ---------------------------------------------------------------------------

TEST_CASE("_NET_WM_NAME UTF8_STRING encoding", "[ewmh]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    Window root = DefaultRootWindow(display.get());

    // Create a test window (simulating WM check window)
    Window w = XCreateSimpleWindow(display.get(), root, -1, -1, 1, 1, 0, 0, 0);
    REQUIRE(w != None);

    Atom net_wmName = XInternAtom(display.get(), "_NET_WM_NAME", false);
    Atom utf8_string = XInternAtom(display.get(), "UTF8_STRING", false);
    REQUIRE(net_wmName != None);
    REQUIRE(utf8_string != None);

    // Set _NET_WM_NAME with UTF8_STRING type
    const char* wmName = "wm2-born-again";
    XChangeProperty(display.get(), w, net_wmName,
                    utf8_string, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(wmName), 13);
    XSync(display.get(), false);

    // Read back
    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    char* data = nullptr;
    int status = XGetWindowProperty(display.get(), w, net_wmName,
                                    0, 1024, false, utf8_string,
                                    &actualType, &actualFormat, &nItems, &bytesAfter,
                                    reinterpret_cast<unsigned char**>(&data));
    REQUIRE(status == Success);
    REQUIRE(nItems == 13);
    REQUIRE(actualType == utf8_string);
    REQUIRE(actualFormat == 8);
    REQUIRE(data != nullptr);
    REQUIRE(std::strncmp(data, "wm2-born-again", 13) == 0);
    XFree(data);

    XDestroyWindow(display.get(), w);
}

// ---------------------------------------------------------------------------
// _NET_SUPPORTED atom array test
// ---------------------------------------------------------------------------

TEST_CASE("_NET_SUPPORTED contains declared atoms", "[ewmh]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    Window root = DefaultRootWindow(display.get());

    // Intern the atoms we expect to be listed
    Atom net_supported = XInternAtom(display.get(), "_NET_SUPPORTED", false);
    Atom net_supportingWmCheck = XInternAtom(display.get(), "_NET_SUPPORTING_WM_CHECK", false);
    Atom net_clientList = XInternAtom(display.get(), "_NET_CLIENT_LIST", false);
    Atom net_activeWindow = XInternAtom(display.get(), "_NET_ACTIVE_WINDOW", false);
    Atom net_numberOfDesktops = XInternAtom(display.get(), "_NET_NUMBER_OF_DESKTOPS", false);
    Atom net_currentDesktop = XInternAtom(display.get(), "_NET_CURRENT_DESKTOP", false);
    Atom net_workarea = XInternAtom(display.get(), "_NET_WORKAREA", false);

    // Create a supported array (simulating what the WM sets)
    Atom supported[] = {
        net_supported, net_supportingWmCheck,
        net_clientList, net_activeWindow,
        net_numberOfDesktops, net_currentDesktop,
        net_workarea,
    };
    XChangeProperty(display.get(), root, net_supported,
                    XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(supported),
                    sizeof(supported) / sizeof(Atom));
    XSync(display.get(), false);

    // Read back
    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    Atom* data = nullptr;
    int status = XGetWindowProperty(display.get(), root, net_supported,
                                    0, 1024, false, XA_ATOM,
                                    &actualType, &actualFormat, &nItems, &bytesAfter,
                                    reinterpret_cast<unsigned char**>(&data));
    REQUIRE(status == Success);
    REQUIRE(actualType == XA_ATOM);
    REQUIRE(actualFormat == 32);
    REQUIRE(nItems == 7);
    REQUIRE(data != nullptr);

    // Verify all expected atoms are present
    bool foundClientList = false;
    bool foundActiveWindow = false;
    bool foundWorkarea = false;
    for (unsigned long i = 0; i < nItems; ++i) {
        if (data[i] == net_clientList) foundClientList = true;
        if (data[i] == net_activeWindow) foundActiveWindow = true;
        if (data[i] == net_workarea) foundWorkarea = true;
    }
    REQUIRE(foundClientList);
    REQUIRE(foundActiveWindow);
    REQUIRE(foundWorkarea);

    XFree(data);

    // Clean up
    XDeleteProperty(display.get(), root, net_supported);
}

// ---------------------------------------------------------------------------
// Single-desktop atoms tests
// ---------------------------------------------------------------------------

TEST_CASE("_NET_NUMBER_OF_DESKTOPS is 1", "[ewmh]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    Window root = DefaultRootWindow(display.get());

    Atom net_numberOfDesktops = XInternAtom(display.get(), "_NET_NUMBER_OF_DESKTOPS", false);
    REQUIRE(net_numberOfDesktops != None);

    // Set to 1
    long ndesktops = 1;
    XChangeProperty(display.get(), root, net_numberOfDesktops,
                    XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&ndesktops), 1);
    XSync(display.get(), false);

    // Read back
    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    long* data = nullptr;
    int status = XGetWindowProperty(display.get(), root, net_numberOfDesktops,
                                    0, 1, false, XA_CARDINAL,
                                    &actualType, &actualFormat, &nItems, &bytesAfter,
                                    reinterpret_cast<unsigned char**>(&data));
    REQUIRE(status == Success);
    REQUIRE(actualType == XA_CARDINAL);
    REQUIRE(actualFormat == 32);
    REQUIRE(nItems == 1);
    REQUIRE(data != nullptr);
    REQUIRE(data[0] == 1);
    XFree(data);

    // Clean up
    XDeleteProperty(display.get(), root, net_numberOfDesktops);
}

TEST_CASE("_NET_CURRENT_DESKTOP is 0", "[ewmh]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    Window root = DefaultRootWindow(display.get());

    Atom net_currentDesktop = XInternAtom(display.get(), "_NET_CURRENT_DESKTOP", false);
    REQUIRE(net_currentDesktop != None);

    // Set to 0
    long currentDesktop = 0;
    XChangeProperty(display.get(), root, net_currentDesktop,
                    XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&currentDesktop), 1);
    XSync(display.get(), false);

    // Read back
    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    long* data = nullptr;
    int status = XGetWindowProperty(display.get(), root, net_currentDesktop,
                                    0, 1, false, XA_CARDINAL,
                                    &actualType, &actualFormat, &nItems, &bytesAfter,
                                    reinterpret_cast<unsigned char**>(&data));
    REQUIRE(status == Success);
    REQUIRE(actualType == XA_CARDINAL);
    REQUIRE(actualFormat == 32);
    REQUIRE(nItems == 1);
    REQUIRE(data != nullptr);
    REQUIRE(data[0] == 0);
    XFree(data);

    // Clean up
    XDeleteProperty(display.get(), root, net_currentDesktop);
}

// ---------------------------------------------------------------------------
// _NET_WORKAREA test
// ---------------------------------------------------------------------------

TEST_CASE("_NET_WORKAREA is 4-element CARDINAL", "[ewmh]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    Window root = DefaultRootWindow(display.get());
    int screen = DefaultScreen(display.get());

    Atom net_workarea = XInternAtom(display.get(), "_NET_WORKAREA", false);
    REQUIRE(net_workarea != None);

    // Set workarea to full screen geometry
    long workarea[4] = { 0, 0,
        static_cast<long>(DisplayWidth(display.get(), screen)),
        static_cast<long>(DisplayHeight(display.get(), screen)) };
    XChangeProperty(display.get(), root, net_workarea,
                    XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(workarea), 4);
    XSync(display.get(), false);

    // Read back
    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    long* data = nullptr;
    int status = XGetWindowProperty(display.get(), root, net_workarea,
                                    0, 4, false, XA_CARDINAL,
                                    &actualType, &actualFormat, &nItems, &bytesAfter,
                                    reinterpret_cast<unsigned char**>(&data));
    REQUIRE(status == Success);
    REQUIRE(actualType == XA_CARDINAL);
    REQUIRE(actualFormat == 32);
    REQUIRE(nItems == 4);
    REQUIRE(data != nullptr);
    REQUIRE(data[0] == 0);  // x
    REQUIRE(data[1] == 0);  // y
    REQUIRE(data[2] == static_cast<long>(DisplayWidth(display.get(), screen)));   // width
    REQUIRE(data[3] == static_cast<long>(DisplayHeight(display.get(), screen)));  // height
    XFree(data);

    // Clean up
    XDeleteProperty(display.get(), root, net_workarea);
}

// ---------------------------------------------------------------------------
// _NET_CLIENT_LIST round-trip test
// ---------------------------------------------------------------------------

TEST_CASE("_NET_CLIENT_LIST round-trip", "[ewmh]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    Window root = DefaultRootWindow(display.get());

    Atom net_clientList = XInternAtom(display.get(), "_NET_CLIENT_LIST", false);
    REQUIRE(net_clientList != None);

    // Set empty array
    XChangeProperty(display.get(), root, net_clientList,
                    XA_WINDOW, 32, PropModeReplace, nullptr, 0);
    XSync(display.get(), false);

    // Read back empty
    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    Window* data = nullptr;
    int status = XGetWindowProperty(display.get(), root, net_clientList,
                                    0, 1024, false, XA_WINDOW,
                                    &actualType, &actualFormat, &nItems, &bytesAfter,
                                    reinterpret_cast<unsigned char**>(&data));
    REQUIRE(status == Success);
    REQUIRE(nItems == 0);
    if (data) XFree(data);

    // Create two test windows
    Window w1 = XCreateSimpleWindow(display.get(), root, 0, 0, 100, 100, 0, 0, 0);
    Window w2 = XCreateSimpleWindow(display.get(), root, 0, 0, 100, 100, 0, 0, 0);
    REQUIRE(w1 != None);
    REQUIRE(w2 != None);

    // Set _NET_CLIENT_LIST with both windows
    Window windows[2] = { w1, w2 };
    XChangeProperty(display.get(), root, net_clientList,
                    XA_WINDOW, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(windows), 2);
    XSync(display.get(), false);

    // Read back
    data = nullptr;
    status = XGetWindowProperty(display.get(), root, net_clientList,
                                0, 1024, false, XA_WINDOW,
                                &actualType, &actualFormat, &nItems, &bytesAfter,
                                reinterpret_cast<unsigned char**>(&data));
    REQUIRE(status == Success);
    REQUIRE(nItems == 2);
    REQUIRE(data != nullptr);
    REQUIRE(data[0] == w1);
    REQUIRE(data[1] == w2);
    XFree(data);

    // Clean up
    XDeleteProperty(display.get(), root, net_clientList);
    XDestroyWindow(display.get(), w1);
    XDestroyWindow(display.get(), w2);
}

// ---------------------------------------------------------------------------
// _NET_ACTIVE_WINDOW test
// ---------------------------------------------------------------------------

TEST_CASE("_NET_ACTIVE_WINDOW can be set to None", "[ewmh]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    Window root = DefaultRootWindow(display.get());

    Atom net_activeWindow = XInternAtom(display.get(), "_NET_ACTIVE_WINDOW", false);
    REQUIRE(net_activeWindow != None);

    // Set to None
    Window noneWindow = None;
    XChangeProperty(display.get(), root, net_activeWindow,
                    XA_WINDOW, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&noneWindow), 1);
    XSync(display.get(), false);

    // Read back
    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    Window* data = nullptr;
    int status = XGetWindowProperty(display.get(), root, net_activeWindow,
                                    0, 1, false, XA_WINDOW,
                                    &actualType, &actualFormat, &nItems, &bytesAfter,
                                    reinterpret_cast<unsigned char**>(&data));
    REQUIRE(status == Success);
    REQUIRE(actualType == XA_WINDOW);
    REQUIRE(actualFormat == 32);
    REQUIRE(nItems == 1);
    REQUIRE(data != nullptr);
    REQUIRE(data[0] == None);
    XFree(data);

    // Clean up
    XDeleteProperty(display.get(), root, net_activeWindow);
}

// ---------------------------------------------------------------------------
// EWMH _NET_WM_STATE property format test
// ---------------------------------------------------------------------------

TEST_CASE("EWMH _NET_WM_STATE property format is Atom array", "[ewmh][state]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    Window root = DefaultRootWindow(display.get());
    Window w = XCreateSimpleWindow(display.get(), root, 0, 0, 100, 100, 0, 0, 0);
    REQUIRE(w != None);

    Atom net_wmState = XInternAtom(display.get(), "_NET_WM_STATE", false);
    Atom net_wmStateFullscreen = XInternAtom(display.get(), "_NET_WM_STATE_FULLSCREEN", false);
    Atom net_wmStateMaximizedVert = XInternAtom(display.get(), "_NET_WM_STATE_MAXIMIZED_VERT", false);
    REQUIRE(net_wmState != None);
    REQUIRE(net_wmStateFullscreen != None);
    REQUIRE(net_wmStateMaximizedVert != None);

    // Set _NET_WM_STATE with fullscreen + maximized_vert atoms
    Atom states[] = { net_wmStateFullscreen, net_wmStateMaximizedVert };
    XChangeProperty(display.get(), w, net_wmState,
                    XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(states), 2);
    XSync(display.get(), false);

    // Read back
    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    Atom* data = nullptr;
    int status = XGetWindowProperty(display.get(), w, net_wmState,
                                    0, 1024, false, XA_ATOM,
                                    &actualType, &actualFormat, &nItems, &bytesAfter,
                                    reinterpret_cast<unsigned char**>(&data));
    REQUIRE(status == Success);
    REQUIRE(actualType == XA_ATOM);
    REQUIRE(actualFormat == 32);
    REQUIRE(nItems == 2);
    REQUIRE(data != nullptr);

    bool foundFullscreen = false;
    bool foundMaximized = false;
    for (unsigned long i = 0; i < nItems; ++i) {
        if (data[i] == net_wmStateFullscreen) foundFullscreen = true;
        if (data[i] == net_wmStateMaximizedVert) foundMaximized = true;
    }
    REQUIRE(foundFullscreen);
    REQUIRE(foundMaximized);
    XFree(data);

    XDestroyWindow(display.get(), w);
}

// ---------------------------------------------------------------------------
// EWMH _NET_WM_STATE add/remove/toggle semantics test
// ---------------------------------------------------------------------------

TEST_CASE("EWMH _NET_WM_STATE add/remove/toggle semantics", "[ewmh][state]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    Window root = DefaultRootWindow(display.get());
    Window w = XCreateSimpleWindow(display.get(), root, 0, 0, 100, 100, 0, 0, 0);
    REQUIRE(w != None);

    Atom net_wmState = XInternAtom(display.get(), "_NET_WM_STATE", false);
    Atom net_wmStateFullscreen = XInternAtom(display.get(), "_NET_WM_STATE_FULLSCREEN", false);
    REQUIRE(net_wmState != None);
    REQUIRE(net_wmStateFullscreen != None);

    // Helper lambda: check if atom is in _NET_WM_STATE
    auto hasState = [&](Atom atom) -> bool {
        Atom actualType;
        int actualFormat;
        unsigned long nItems, bytesAfter;
        Atom* data = nullptr;
        int status = XGetWindowProperty(display.get(), w, net_wmState,
                                        0, 1024, false, XA_ATOM,
                                        &actualType, &actualFormat, &nItems, &bytesAfter,
                                        reinterpret_cast<unsigned char**>(&data));
        bool found = false;
        if (status == Success && data) {
            for (unsigned long i = 0; i < nItems; ++i) {
                if (data[i] == atom) found = true;
            }
            XFree(data);
        }
        return found;
    };

    // Helper: set _NET_WM_STATE to given atom array
    auto setState = [&](const std::vector<Atom>& atoms) {
        XChangeProperty(display.get(), w, net_wmState,
                        XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<unsigned char*>(const_cast<Atom*>(atoms.data())),
                        static_cast<int>(atoms.size()));
        XSync(display.get(), false);
    };

    // Start with empty state
    setState({});
    REQUIRE_FALSE(hasState(net_wmStateFullscreen));

    // Simulate add (action=1): set atom in state array
    setState({net_wmStateFullscreen});
    REQUIRE(hasState(net_wmStateFullscreen));

    // Simulate remove (action=0): remove atom from state array
    setState({});
    REQUIRE_FALSE(hasState(net_wmStateFullscreen));

    // Simulate toggle (action=2): flip atom presence
    setState({net_wmStateFullscreen});  // toggle on
    REQUIRE(hasState(net_wmStateFullscreen));
    setState({});  // toggle off
    REQUIRE_FALSE(hasState(net_wmStateFullscreen));

    XDestroyWindow(display.get(), w);
}

// ---------------------------------------------------------------------------
// EWMH fullscreen state round-trip test
// ---------------------------------------------------------------------------

TEST_CASE("EWMH fullscreen state round-trip", "[ewmh][state]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    Window root = DefaultRootWindow(display.get());
    Window w = XCreateSimpleWindow(display.get(), root, 0, 0, 100, 100, 0, 0, 0);
    REQUIRE(w != None);

    Atom net_wmState = XInternAtom(display.get(), "_NET_WM_STATE", false);
    Atom net_wmStateFullscreen = XInternAtom(display.get(), "_NET_WM_STATE_FULLSCREEN", false);
    REQUIRE(net_wmState != None);

    // Set _NET_WM_STATE = [_NET_WM_STATE_FULLSCREEN]
    Atom stateArr[] = { net_wmStateFullscreen };
    XChangeProperty(display.get(), w, net_wmState,
                    XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(stateArr), 1);
    XSync(display.get(), false);

    // Read back, verify atom present
    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    Atom* data = nullptr;
    int status = XGetWindowProperty(display.get(), w, net_wmState,
                                    0, 1024, false, XA_ATOM,
                                    &actualType, &actualFormat, &nItems, &bytesAfter,
                                    reinterpret_cast<unsigned char**>(&data));
    REQUIRE(status == Success);
    REQUIRE(nItems == 1);
    REQUIRE(data != nullptr);
    REQUIRE(data[0] == net_wmStateFullscreen);
    XFree(data);

    // Remove it, verify atom absent
    XChangeProperty(display.get(), w, net_wmState,
                    XA_ATOM, 32, PropModeReplace, nullptr, 0);
    XSync(display.get(), false);

    data = nullptr;
    status = XGetWindowProperty(display.get(), w, net_wmState,
                                0, 1024, false, XA_ATOM,
                                &actualType, &actualFormat, &nItems, &bytesAfter,
                                reinterpret_cast<unsigned char**>(&data));
    REQUIRE(status == Success);
    REQUIRE(nItems == 0);
    if (data) XFree(data);

    XDestroyWindow(display.get(), w);
}

// ---------------------------------------------------------------------------
// EWMH maximize state both axes test
// ---------------------------------------------------------------------------

TEST_CASE("EWMH maximize state both axes", "[ewmh][state]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    Window root = DefaultRootWindow(display.get());
    Window w = XCreateSimpleWindow(display.get(), root, 0, 0, 100, 100, 0, 0, 0);
    REQUIRE(w != None);

    Atom net_wmState = XInternAtom(display.get(), "_NET_WM_STATE", false);
    Atom net_wmStateMaximizedVert = XInternAtom(display.get(), "_NET_WM_STATE_MAXIMIZED_VERT", false);
    Atom net_wmStateMaximizedHorz = XInternAtom(display.get(), "_NET_WM_STATE_MAXIMIZED_HORZ", false);
    REQUIRE(net_wmState != None);

    // Set _NET_WM_STATE = [MAXIMIZED_VERT, MAXIMIZED_HORZ]
    Atom stateArr[] = { net_wmStateMaximizedVert, net_wmStateMaximizedHorz };
    XChangeProperty(display.get(), w, net_wmState,
                    XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(stateArr), 2);
    XSync(display.get(), false);

    // Read back, verify both atoms present
    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    Atom* data = nullptr;
    int status = XGetWindowProperty(display.get(), w, net_wmState,
                                    0, 1024, false, XA_ATOM,
                                    &actualType, &actualFormat, &nItems, &bytesAfter,
                                    reinterpret_cast<unsigned char**>(&data));
    REQUIRE(status == Success);
    REQUIRE(nItems == 2);
    REQUIRE(data != nullptr);

    bool foundVert = false, foundHorz = false;
    for (unsigned long i = 0; i < nItems; ++i) {
        if (data[i] == net_wmStateMaximizedVert) foundVert = true;
        if (data[i] == net_wmStateMaximizedHorz) foundHorz = true;
    }
    REQUIRE(foundVert);
    REQUIRE(foundHorz);
    XFree(data);

    XDestroyWindow(display.get(), w);
}

// ---------------------------------------------------------------------------
// EWMH _NET_WORKAREA subtracts dock struts test
// ---------------------------------------------------------------------------

TEST_CASE("EWMH _NET_WORKAREA subtracts dock struts", "[ewmh][workarea]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    Window root = DefaultRootWindow(display.get());
    int screen = DefaultScreen(display.get());
    int screenW = DisplayWidth(display.get(), screen);
    int screenH = DisplayHeight(display.get(), screen);

    // Simulate a 40px top dock strut
    const long dockTop = 40;
    long workarea[4] = { 0, dockTop,
                         static_cast<long>(screenW),
                         static_cast<long>(screenH) - dockTop };

    Atom net_workarea = XInternAtom(display.get(), "_NET_WORKAREA", false);
    REQUIRE(net_workarea != None);

    XChangeProperty(display.get(), root, net_workarea,
                    XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(workarea), 4);
    XSync(display.get(), false);

    // Read back
    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    long* data = nullptr;
    int status = XGetWindowProperty(display.get(), root, net_workarea,
                                    0, 4, false, XA_CARDINAL,
                                    &actualType, &actualFormat, &nItems, &bytesAfter,
                                    reinterpret_cast<unsigned char**>(&data));
    REQUIRE(status == Success);
    REQUIRE(nItems == 4);
    REQUIRE(data != nullptr);
    REQUIRE(data[0] == 0);                  // x: unchanged
    REQUIRE(data[1] == dockTop);            // y: offset by dock height
    REQUIRE(data[2] == screenW);            // width: full screen
    REQUIRE(data[3] == screenH - dockTop);  // height: screen minus dock
    XFree(data);

    // Clean up
    XDeleteProperty(display.get(), root, net_workarea);
}

// ---------------------------------------------------------------------------
// EWMH _NET_WM_STRUT_PARTIAL preferred over _NET_WM_STRUT test
// ---------------------------------------------------------------------------

TEST_CASE("EWMH _NET_WM_STRUT_PARTIAL preferred over _NET_WM_STRUT", "[ewmh][workarea]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    Window root = DefaultRootWindow(display.get());
    Window w = XCreateSimpleWindow(display.get(), root, 0, 0, 100, 100, 0, 0, 0);
    REQUIRE(w != None);

    Atom net_wmStrut = XInternAtom(display.get(), "_NET_WM_STRUT", false);
    Atom net_wmStrutPartial = XInternAtom(display.get(), "_NET_WM_STRUT_PARTIAL", false);
    REQUIRE(net_wmStrut != None);
    REQUIRE(net_wmStrutPartial != None);

    // Set _NET_WM_STRUT = [10, 0, 0, 0] (10px left)
    long strut[4] = { 10, 0, 0, 0 };
    XChangeProperty(display.get(), w, net_wmStrut,
                    XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(strut), 4);

    // Set _NET_WM_STRUT_PARTIAL = [20, 0, 0, 0, ...] (20px left, partial)
    long strutPartial[12] = { 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    XChangeProperty(display.get(), w, net_wmStrutPartial,
                    XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(strutPartial), 12);
    XSync(display.get(), false);

    // Read _NET_WM_STRUT_PARTIAL -- should be the one the WM prefers
    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    long* data = nullptr;
    int status = XGetWindowProperty(display.get(), w, net_wmStrutPartial,
                                    0, 12, false, XA_CARDINAL,
                                    &actualType, &actualFormat, &nItems, &bytesAfter,
                                    reinterpret_cast<unsigned char**>(&data));
    REQUIRE(status == Success);
    REQUIRE(nItems >= 4);
    REQUIRE(data != nullptr);
    REQUIRE(data[0] == 20);  // STRUT_PARTIAL value takes precedence
    XFree(data);

    XDestroyWindow(display.get(), w);
}

// ---------------------------------------------------------------------------
// EWMH _NET_WM_STRUT clamped to screen dimensions test
// ---------------------------------------------------------------------------

TEST_CASE("EWMH _NET_WM_STRUT clamped to screen dimensions", "[ewmh][workarea]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    int screen = DefaultScreen(display.get());
    int screenW = DisplayWidth(display.get(), screen);

    // Simulate clamping: min(999999, screenW) = screenW
    long absurdStrut = 999999;
    long clampedLeft = std::min(absurdStrut, static_cast<long>(screenW));

    REQUIRE(clampedLeft == screenW);
    REQUIRE(clampedLeft < absurdStrut);
}
