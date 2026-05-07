#pragma once

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <fontconfig/fontconfig.h>
#include <memory>
#include <cstring>

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
    explicit operator Cursor() const noexcept { return m_cursor; }
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

// ============================================================
// Xft resources: RAII wrappers for font rendering
// ============================================================

// XftFontPtr -- unique_ptr + custom deleter (follows GCPtr pattern)
struct XftFontDeleter {
    Display* display = nullptr;
    void operator()(XftFont* font) const noexcept {
        if (display && font) XftFontClose(display, font);
    }
};
using XftFontPtr = std::unique_ptr<XftFont, XftFontDeleter>;

// Load font by fontconfig name pattern (e.g., "Noto Sans:bold:size=12")
// Used for non-rotated text (menu labels)
inline XftFontPtr make_xft_font_name(Display* d, const char* name) {
    XftFont* f = XftFontOpenName(d, DefaultScreen(d), name);
    XftFontPtr p{f};
    if (p) p.get_deleter().display = d;
    return p;
}

// Load font with 90-degree clockwise rotation via FcMatrix (D-04)
// Used for rotated text (border tab labels)
inline XftFontPtr make_xft_font_rotated(Display* d, const char* pattern) {
    FcPattern* pat = FcNameParse(reinterpret_cast<const FcChar8*>(pattern));
    if (!pat) return XftFontPtr{nullptr};

    FcConfigSubstitute(nullptr, pat, FcMatchPattern);
    XftDefaultSubstitute(d, DefaultScreen(d), pat);

    // Apply 90-degree clockwise rotation: cos(90)=0, sin(90)=1 (D-04, Pitfall 6)
    FcMatrix mat;
    FcMatrixInit(&mat);
    FcMatrixRotate(&mat, 0.0, 1.0);
    FcPatternAddMatrix(pat, FC_MATRIX, &mat);

    FcResult result;
    FcPattern* match = FcFontMatch(nullptr, pat, &result);
    FcPatternDestroy(pat);
    if (!match) return XftFontPtr{nullptr};

    XftFont* font = XftFontOpenPattern(d, match);
    // Pitfall 3: XftFontOpenPattern takes ownership of match on success only
    if (!font) {
        FcPatternDestroy(match);
        return XftFontPtr{nullptr};
    }

    XftFontPtr p{font};
    p.get_deleter().display = d;
    return p;
}

// XftDrawPtr -- thin move-only class (follows UniqueCursor pattern)
class XftDrawPtr {
public:
    XftDrawPtr() noexcept : m_draw(nullptr) {}
    explicit XftDrawPtr(XftDraw* d) noexcept : m_draw(d) {}

    XftDrawPtr(XftDrawPtr&& o) noexcept : m_draw(o.m_draw) { o.m_draw = nullptr; }
    XftDrawPtr& operator=(XftDrawPtr&& o) noexcept {
        if (this != &o) { reset(); m_draw = o.m_draw; o.m_draw = nullptr; }
        return *this;
    }

    XftDrawPtr(const XftDrawPtr&) = delete;
    XftDrawPtr& operator=(const XftDrawPtr&) = delete;

    ~XftDrawPtr() { reset(); }

    void reset() noexcept {
        if (m_draw) XftDrawDestroy(m_draw);
        m_draw = nullptr;
    }

    XftDraw* get() const noexcept { return m_draw; }
    explicit operator bool() const noexcept { return m_draw != nullptr; }

private:
    XftDraw* m_draw;
};

// XftColorWrap -- stores Display*/Visual*/Colormap for cleanup per Pitfall 4
class XftColorWrap {
public:
    XftColorWrap() noexcept : m_display(nullptr), m_visual(nullptr), m_cmap(None) {
        std::memset(&m_color, 0, sizeof(m_color));
    }

    XftColorWrap(Display* d, Visual* v, Colormap c, const char* name) noexcept
        : m_display(d), m_visual(v), m_cmap(c) {
        if (!XftColorAllocName(d, v, c, name, &m_color)) {
            m_display = nullptr; // mark as invalid
        }
    }

    XftColorWrap(XftColorWrap&& o) noexcept
        : m_display(o.m_display), m_visual(o.m_visual), m_cmap(o.m_cmap), m_color(o.m_color) {
        o.m_display = nullptr;
    }
    XftColorWrap& operator=(XftColorWrap&& o) noexcept {
        if (this != &o) {
            reset();
            m_display = o.m_display; m_visual = o.m_visual;
            m_cmap = o.m_cmap; m_color = o.m_color; o.m_display = nullptr;
        }
        return *this;
    }

    XftColorWrap(const XftColorWrap&) = delete;
    XftColorWrap& operator=(const XftColorWrap&) = delete;

    ~XftColorWrap() { reset(); }

    void reset() noexcept {
        if (m_display && m_visual && m_cmap != None) {
            XftColorFree(m_display, m_visual, m_cmap, &m_color);
        }
        m_display = nullptr; m_visual = nullptr; m_cmap = None;
    }

    XftColor* operator->() noexcept { return &m_color; }
    XftColor* get() noexcept { return &m_color; }
    const XftColor* get() const noexcept { return &m_color; }
    explicit operator bool() const noexcept { return m_display != nullptr; }

private:
    Display* m_display;
    Visual* m_visual;
    Colormap m_cmap;
    XftColor m_color;
};

} // namespace x11
