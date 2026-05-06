#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "x11wrap.h"
#include <type_traits>

// ============================================================
// DisplayDeleter tests
// ============================================================

TEST_CASE("DisplayDeleter handles null safely", "[raii][display]") {
    x11::DisplayDeleter deleter;
    Display* raw = nullptr;
    REQUIRE_NOTHROW(deleter(raw));
}

// ============================================================
// GCDeleter tests
// ============================================================

TEST_CASE("GCDeleter default state has null display", "[raii][gc]") {
    x11::GCDeleter deleter;
    REQUIRE(deleter.display == nullptr);
}

TEST_CASE("GCDeleter handles null GC safely", "[raii][gc]") {
    x11::GCDeleter deleter;
    GC null_gc = nullptr;
    REQUIRE_NOTHROW(deleter(null_gc));
}

TEST_CASE("GCDeleter handles null display + null GC safely", "[raii][gc]") {
    x11::GCDeleter deleter;
    deleter.display = nullptr;
    GC null_gc = nullptr;
    REQUIRE_NOTHROW(deleter(null_gc));
}

// ============================================================
// FontStructDeleter tests
// ============================================================

TEST_CASE("FontStructDeleter handles null safely", "[raii][font]") {
    x11::FontStructDeleter deleter;
    REQUIRE(deleter.display == nullptr);
    XFontStruct* null_fs = nullptr;
    REQUIRE_NOTHROW(deleter(null_fs));
}

// ============================================================
// UniqueCursor tests
// ============================================================

TEST_CASE("UniqueCursor default constructor is None", "[raii][cursor]") {
    x11::UniqueCursor c;
    REQUIRE(c.get() == None);
    REQUIRE_FALSE(static_cast<bool>(c));
}

TEST_CASE("UniqueCursor is move-only", "[raii][cursor]") {
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<x11::UniqueCursor>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<x11::UniqueCursor>);
    STATIC_REQUIRE(std::is_move_constructible_v<x11::UniqueCursor>);
    STATIC_REQUIRE(std::is_nothrow_move_constructible_v<x11::UniqueCursor>);
    STATIC_REQUIRE(std::is_move_assignable_v<x11::UniqueCursor>);
}

TEST_CASE("UniqueCursor move transfers ownership", "[raii][cursor]") {
    // Use a fake display pointer (non-null) and a fake cursor ID
    Display* fake_display = reinterpret_cast<Display*>(0x1);
    Cursor fake_cursor = 42;

    x11::UniqueCursor a(fake_display, fake_cursor);
    REQUIRE(a.get() == fake_cursor);

    x11::UniqueCursor b(std::move(a));
    REQUIRE(b.get() == fake_cursor);
    REQUIRE(a.get() == None);  // moved-from

    // Reset b without freeing (fake display, would crash if XFreeCursor called)
    // Use release() instead to avoid calling XFreeCursor with fake display
    [[maybe_unused]] Cursor released = b.release();
    REQUIRE(b.get() == None);
}

TEST_CASE("UniqueCursor release disassociates without freeing", "[raii][cursor]") {
    Display* fake_display = reinterpret_cast<Display*>(0x1);
    x11::UniqueCursor c(fake_display, 100);
    Cursor raw = c.release();
    REQUIRE(raw == 100);
    REQUIRE(c.get() == None);
    REQUIRE_FALSE(static_cast<bool>(c));
}

// ============================================================
// UniqueFont tests
// ============================================================

TEST_CASE("UniqueFont default constructor is None", "[raii][font]") {
    x11::UniqueFont f;
    REQUIRE(f.get() == None);
    REQUIRE_FALSE(static_cast<bool>(f));
}

TEST_CASE("UniqueFont is move-only", "[raii][font]") {
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<x11::UniqueFont>);
    STATIC_REQUIRE(std::is_move_constructible_v<x11::UniqueFont>);
}

// ============================================================
// UniquePixmap tests
// ============================================================

TEST_CASE("UniquePixmap default constructor is None", "[raii][pixmap]") {
    x11::UniquePixmap p;
    REQUIRE(p.get() == None);
    REQUIRE_FALSE(static_cast<bool>(p));
}

TEST_CASE("UniquePixmap is move-only", "[raii][pixmap]") {
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<x11::UniquePixmap>);
    STATIC_REQUIRE(std::is_move_constructible_v<x11::UniquePixmap>);
}

// ============================================================
// UniqueColormap tests
// ============================================================

TEST_CASE("UniqueColormap default constructor is None", "[raii][colormap]") {
    x11::UniqueColormap c;
    REQUIRE(c.get() == None);
    REQUIRE_FALSE(static_cast<bool>(c));
}

TEST_CASE("UniqueColormap is move-only", "[raii][colormap]") {
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<x11::UniqueColormap>);
    STATIC_REQUIRE(std::is_move_constructible_v<x11::UniqueColormap>);
}

// ============================================================
// DisplayPtr tests
// ============================================================

TEST_CASE("DisplayPtr is move-only", "[raii][display]") {
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<x11::DisplayPtr>);
    STATIC_REQUIRE(std::is_move_constructible_v<x11::DisplayPtr>);
}
