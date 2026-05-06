#include <catch2/catch_test_macros.hpp>
#include "x11wrap.h"
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <cstdlib>
#include <string>

TEST_CASE("Xvfb display is available", "[smoke]") {
    const char* display = std::getenv("DISPLAY");
    REQUIRE(display != nullptr);
}

TEST_CASE("Can open Xvfb display", "[smoke]") {
    Display* d = XOpenDisplay(std::getenv("DISPLAY"));
    REQUIRE(d != nullptr);
    XCloseDisplay(d);
}

TEST_CASE("DisplayPtr manages display lifecycle", "[smoke][raii]") {
    {
        x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
        REQUIRE(display != nullptr);
        REQUIRE(display.get() != nullptr);
        // Display auto-closed when display goes out of scope
    }
    // If we get here without segfault, RAII cleanup worked
    REQUIRE(true);
}

TEST_CASE("GCPtr manages GC lifecycle", "[smoke][raii]") {
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    int screen = DefaultScreen(display.get());
    Window root = RootWindow(display.get(), screen);
    XGCValues vals;
    auto gc = x11::make_gc(display.get(), root, 0, &vals);
    REQUIRE(gc != nullptr);
    REQUIRE(gc.get() != nullptr);
    // GC auto-freed when gc goes out of scope
    // Display auto-freed when display goes out of scope (declared first = destroyed last)
}

TEST_CASE("FontStructPtr manages font lifecycle", "[smoke][raii]") {
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    auto font = x11::make_font_struct(display.get(), "fixed");
    REQUIRE(font != nullptr);
    REQUIRE(font.get() != nullptr);
    // Font auto-freed when font goes out of scope
}

TEST_CASE("UniqueCursor manages cursor lifecycle", "[smoke][raii]") {
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    int screen = DefaultScreen(display.get());
    Window root = RootWindow(display.get(), screen);

    // Create a simple cursor from the built-in cursor data
    XColor fg, bg;
    fg.red = fg.green = fg.blue = 0;
    bg.red = bg.green = bg.blue = 65535;

    // Use XCreateFontCursor for simplicity (cursor_bits requires Pixmap setup)
    x11::UniqueCursor cursor(display.get(), XCreateFontCursor(display.get(), XC_left_ptr));
    REQUIRE(cursor.get() != None);
    REQUIRE(static_cast<bool>(cursor));
    // Cursor auto-freed when cursor goes out of scope
}

TEST_CASE("UniquePixmap manages pixmap lifecycle", "[smoke][raii]") {
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    int screen = DefaultScreen(display.get());
    Window root = RootWindow(display.get(), screen);
    unsigned int depth = DefaultDepth(display.get(), screen);

    Pixmap raw_pixmap = XCreatePixmap(display.get(), root, 16, 16, depth);
    REQUIRE(raw_pixmap != None);

    x11::UniquePixmap pixmap(display.get(), raw_pixmap);
    REQUIRE(pixmap.get() == raw_pixmap);
    // Pixmap auto-freed when pixmap goes out of scope
}
