#pragma once

#include <X11/Xlib.h>
#include <memory>

namespace x11 {

// ============================================================
// Pointer-type resources: unique_ptr + custom deleters
// ============================================================

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

inline FontStructPtr make_font_struct(Display* d, const char* name) {
    XFontStruct* fs = XLoadQueryFont(d, name);
    FontStructPtr p{ fs };
    if (p) p.get_deleter().display = d;
    return p;
}

// ============================================================
// XID-type resources: thin RAII wrapper classes
// ============================================================

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

    Cursor release() noexcept {
        Cursor c = m_cursor;
        m_display = nullptr;
        m_cursor = None;
        return c;
    }

    Cursor get() const noexcept { return m_cursor; }
    operator Cursor() const noexcept { return m_cursor; }
    explicit operator bool() const noexcept { return m_cursor != None; }

private:
    Display* m_display;
    Cursor m_cursor;
};

class UniqueFont {
public:
    UniqueFont() noexcept : m_display(nullptr), m_font(None) {}
    UniqueFont(Display* d, Font f) noexcept : m_display(d), m_font(f) {}

    UniqueFont(UniqueFont&& o) noexcept
        : m_display(o.m_display), m_font(o.m_font) {
        o.m_display = nullptr;
        o.m_font = None;
    }
    UniqueFont& operator=(UniqueFont&& o) noexcept {
        if (this != &o) {
            reset();
            m_display = o.m_display;
            m_font = o.m_font;
            o.m_display = nullptr;
            o.m_font = None;
        }
        return *this;
    }

    UniqueFont(const UniqueFont&) = delete;
    UniqueFont& operator=(const UniqueFont&) = delete;

    ~UniqueFont() { reset(); }

    void reset() noexcept {
        if (m_display && m_font != None) {
            XUnloadFont(m_display, m_font);
        }
        m_display = nullptr;
        m_font = None;
    }

    Font release() noexcept {
        Font f = m_font;
        m_display = nullptr;
        m_font = None;
        return f;
    }

    Font get() const noexcept { return m_font; }
    operator Font() const noexcept { return m_font; }
    explicit operator bool() const noexcept { return m_font != None; }

private:
    Display* m_display;
    Font m_font;
};

class UniquePixmap {
public:
    UniquePixmap() noexcept : m_display(nullptr), m_pixmap(None) {}
    UniquePixmap(Display* d, Pixmap p) noexcept : m_display(d), m_pixmap(p) {}

    UniquePixmap(UniquePixmap&& o) noexcept
        : m_display(o.m_display), m_pixmap(o.m_pixmap) {
        o.m_display = nullptr;
        o.m_pixmap = None;
    }
    UniquePixmap& operator=(UniquePixmap&& o) noexcept {
        if (this != &o) {
            reset();
            m_display = o.m_display;
            m_pixmap = o.m_pixmap;
            o.m_display = nullptr;
            o.m_pixmap = None;
        }
        return *this;
    }

    UniquePixmap(const UniquePixmap&) = delete;
    UniquePixmap& operator=(const UniquePixmap&) = delete;

    ~UniquePixmap() { reset(); }

    void reset() noexcept {
        if (m_display && m_pixmap != None) {
            XFreePixmap(m_display, m_pixmap);
        }
        m_display = nullptr;
        m_pixmap = None;
    }

    Pixmap release() noexcept {
        Pixmap p = m_pixmap;
        m_display = nullptr;
        m_pixmap = None;
        return p;
    }

    Pixmap get() const noexcept { return m_pixmap; }
    operator Pixmap() const noexcept { return m_pixmap; }
    explicit operator bool() const noexcept { return m_pixmap != None; }

private:
    Display* m_display;
    Pixmap m_pixmap;
};

class UniqueColormap {
public:
    UniqueColormap() noexcept : m_display(nullptr), m_colormap(None) {}
    UniqueColormap(Display* d, Colormap c) noexcept : m_display(d), m_colormap(c) {}

    UniqueColormap(UniqueColormap&& o) noexcept
        : m_display(o.m_display), m_colormap(o.m_colormap) {
        o.m_display = nullptr;
        o.m_colormap = None;
    }
    UniqueColormap& operator=(UniqueColormap&& o) noexcept {
        if (this != &o) {
            reset();
            m_display = o.m_display;
            m_colormap = o.m_colormap;
            o.m_display = nullptr;
            o.m_colormap = None;
        }
        return *this;
    }

    UniqueColormap(const UniqueColormap&) = delete;
    UniqueColormap& operator=(const UniqueColormap&) = delete;

    ~UniqueColormap() { reset(); }

    void reset() noexcept {
        if (m_display && m_colormap != None) {
            XFreeColormap(m_display, m_colormap);
        }
        m_display = nullptr;
        m_colormap = None;
    }

    Colormap release() noexcept {
        Colormap c = m_colormap;
        m_display = nullptr;
        m_colormap = None;
        return c;
    }

    Colormap get() const noexcept { return m_colormap; }
    operator Colormap() const noexcept { return m_colormap; }
    explicit operator bool() const noexcept { return m_colormap != None; }

private:
    Display* m_display;
    Colormap m_colormap;
};

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

} // namespace x11
