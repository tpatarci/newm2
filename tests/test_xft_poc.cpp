#include <catch2/catch_test_macros.hpp>
#include "x11wrap.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <cstdlib>
#include <cstring>

// ---------------------------------------------------------------------------
// Test 1: Xft font loads by fontconfig pattern (validates VISL-04, A3)
// ---------------------------------------------------------------------------

TEST_CASE("Xft font loads by fontconfig pattern", "[xft][poc]")
{
    // DisplayPtr ensures XCloseDisplay happens AFTER all RAII wrappers
    // that store the raw pointer are destroyed (declared first = destroyed last)
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display);

    // D-02: fallback chain Noto Sans -> DejaVu Sans -> any Sans
    auto font = x11::make_xft_font_name(display.get(), "Noto Sans,DejaVu Sans,Sans:bold:size=12");
    REQUIRE(font);
    REQUIRE(font.get() != nullptr);

    // Verify valid font metrics
    REQUIRE(font->ascent > 0);
    REQUIRE(font->descent > 0);
}

// ---------------------------------------------------------------------------
// Test 2: Rotated font loads via FcMatrix (validates D-04, A2)
// ---------------------------------------------------------------------------

TEST_CASE("Rotated font loads via FcMatrix", "[xft][poc][rotation]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display);

    auto font = x11::make_xft_font_rotated(display.get(), "Noto Sans,DejaVu Sans,Sans:bold:size=12");
    REQUIRE(font);
    REQUIRE(font.get() != nullptr);

    // Rotated Xft fonts have zero ascent/descent/height because glyph metrics
    // are in a rotated coordinate system. Use XftTextExtentsUtf8 instead.
    XGlyphInfo extents;
    const char* text = "Hello";
    XftTextExtentsUtf8(display.get(), font.get(),
                       reinterpret_cast<const FcChar8*>(text),
                       static_cast<int>(std::strlen(text)), &extents);
    REQUIRE(extents.width > 0);
    REQUIRE(extents.height > 0);
}

// ---------------------------------------------------------------------------
// Test 3: Xft text renders inside shaped window (validates A1, D-05)
// ---------------------------------------------------------------------------

TEST_CASE("Xft text renders inside shaped window", "[xft][poc][shape]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display);

    int screen = DefaultScreen(display.get());
    Window root = RootWindow(display.get(), screen);
    Visual* visual = DefaultVisual(display.get(), screen);
    Colormap colormap = DefaultColormap(display.get(), screen);

    // Create a 200x300 window (simulating a tab)
    Window window = XCreateSimpleWindow(display.get(), root, 0, 0, 200, 300, 0,
                                         BlackPixel(display.get(), screen),
                                         WhitePixel(display.get(), screen));
    REQUIRE(window != None);
    XMapWindow(display.get(), window);
    XSync(display.get(), false);

    // Check Shape extension availability
    int shape_event_base, shape_error_base;
    bool has_shape = XShapeQueryExtension(display.get(), &shape_event_base, &shape_error_base);

    if (has_shape) {
        // Apply a shaped mask (single rectangle covering full window)
        XRectangle rect;
        rect.x = 0;
        rect.y = 0;
        rect.width = 200;
        rect.height = 300;
        XShapeCombineRectangles(display.get(), window, ShapeBounding, 0, 0,
                                &rect, 1, ShapeSet, YXBanded);
        XSync(display.get(), false);
    }

    // Create XftDraw for the window
    XftDraw* raw_draw = XftDrawCreate(display.get(), window, visual, colormap);
    REQUIRE(raw_draw != nullptr);
    x11::XftDrawPtr draw(raw_draw);
    REQUIRE(draw);

    // Allocate XftColor
    x11::XftColorWrap fg_color(display.get(), visual, colormap, "black");
    REQUIRE(fg_color);

    // Load rotated font
    auto font = x11::make_xft_font_rotated(display.get(), "Noto Sans,DejaVu Sans,Sans:bold:size=12");
    REQUIRE(font);

    // Render text -- this MUST NOT crash or produce an X error
    const char* text = "Hello";
    XftDrawStringUtf8(draw.get(), fg_color.get(), font.get(),
                      10, 200,
                      reinterpret_cast<const FcChar8*>(text),
                      static_cast<int>(std::strlen(text)));
    XSync(display.get(), false);

    // Cleanup Xft resources explicitly before destroying the window
    draw.reset();
    XDestroyWindow(display.get(), window);

    // Test passes whether Shape is available or not (graceful on VNC without Shape)
}

// ---------------------------------------------------------------------------
// Test 4: XftColor allocation and cleanup (validates Pitfall 4)
// ---------------------------------------------------------------------------

TEST_CASE("XftColor allocation and cleanup", "[xft][poc][color]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display);

    int screen = DefaultScreen(display.get());
    Visual* visual = DefaultVisual(display.get(), screen);
    Colormap colormap = DefaultColormap(display.get(), screen);

    // Allocate two colors
    x11::XftColorWrap black_color(display.get(), visual, colormap, "black");
    x11::XftColorWrap gray_color(display.get(), visual, colormap, "gray80");

    REQUIRE(black_color);
    REQUIRE(gray_color);

    // Different colors should have different pixel values
    REQUIRE(black_color->pixel != gray_color->pixel);

    // Colors and display go out of scope -- RAII cleanup should not crash
    // (display destroyed last since it's declared first)
}

// ---------------------------------------------------------------------------
// Test 5: Xft text measurement with XftTextExtentsUtf8
//           (validates replacement for XRotTextWidth)
// ---------------------------------------------------------------------------

TEST_CASE("Xft text measurement with XftTextExtentsUtf8", "[xft][poc][metrics]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display);

    auto font = x11::make_xft_font_name(display.get(), "Noto Sans,DejaVu Sans,Sans:bold:size=12");
    REQUIRE(font);

    // Measure non-empty text
    const char* hello = "Hello";
    XGlyphInfo extents;
    XftTextExtentsUtf8(display.get(), font.get(),
                       reinterpret_cast<const FcChar8*>(hello),
                       static_cast<int>(std::strlen(hello)),
                       &extents);
    REQUIRE(extents.width > 0);

    // Measure shorter text -- should have smaller width
    const char* hi = "Hi";
    XGlyphInfo extents2;
    XftTextExtentsUtf8(display.get(), font.get(),
                       reinterpret_cast<const FcChar8*>(hi),
                       static_cast<int>(std::strlen(hi)),
                       &extents2);
    REQUIRE(extents2.width > 0);
    REQUIRE(extents2.width < extents.width);
}

// ---------------------------------------------------------------------------
// Test 6: UTF-8 string rendering does not crash (validates VISL-03)
// ---------------------------------------------------------------------------

TEST_CASE("UTF-8 string rendering does not crash", "[xft][poc][utf8]")
{
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display);

    int screen = DefaultScreen(display.get());
    Window root = RootWindow(display.get(), screen);
    Visual* visual = DefaultVisual(display.get(), screen);
    Colormap colormap = DefaultColormap(display.get(), screen);

    Window window = XCreateSimpleWindow(display.get(), root, 0, 0, 200, 200, 0, 0, 0);
    REQUIRE(window != None);
    XMapWindow(display.get(), window);
    XSync(display.get(), false);

    // Create XftDraw
    XftDraw* raw_draw = XftDrawCreate(display.get(), window, visual, colormap);
    REQUIRE(raw_draw != nullptr);
    x11::XftDrawPtr draw(raw_draw);

    // Load rotated font and allocate color
    auto font = x11::make_xft_font_rotated(display.get(), "Noto Sans,DejaVu Sans,Sans:bold:size=12");
    REQUIRE(font);

    x11::XftColorWrap fg_color(display.get(), visual, colormap, "black");
    REQUIRE(fg_color);

    // Render Latin-1 supplement: Cafe with e-acute (UTF-8: 0xC3 0xA9)
    const char* latin_text = "Caf\xC3\xA9";
    XftDrawStringUtf8(draw.get(), fg_color.get(), font.get(),
                      10, 100,
                      reinterpret_cast<const FcChar8*>(latin_text),
                      static_cast<int>(std::strlen(latin_text)));
    XSync(display.get(), false);

    // Render CJK: Japanese (3-byte UTF-8 per character)
    const char* cjk_text = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";
    XftDrawStringUtf8(draw.get(), fg_color.get(), font.get(),
                      10, 150,
                      reinterpret_cast<const FcChar8*>(cjk_text),
                      static_cast<int>(std::strlen(cjk_text)));
    XSync(display.get(), false);

    // Cleanup before display destruction
    draw.reset();
    XDestroyWindow(display.get(), window);
}
