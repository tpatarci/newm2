#include "Border.h"
#include "Client.h"
#include "Manager.h"
#include <X11/Xft/Xft.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

// Static member definitions (degenerate initializations -- don't change)
int FRAME_WIDTH = 7;  // Default, overwritten in constructor from config
int Border::m_tabWidth = -1;
XftFont *Border::m_tabFont = nullptr;
x11::GCPtr Border::m_drawGC;
unsigned long Border::m_frameBackgroundPixel = 0;
unsigned long Border::m_buttonBackgroundPixel = 0;
unsigned long Border::m_borderPixel = 0;
int Border::m_borderCount = 0;
XftColor Border::m_xftForeground = {};
XftColor Border::m_xftBackground = {};
bool Border::m_xftColorsAllocated = false;


Border::Border(Client *client, Window child)
    : m_client(client)
    , m_parent(root())
    , m_tab(None)
    , m_child(child)
    , m_button(None)
    , m_resize(None)
    , m_prevW(-1)
    , m_prevH(-1)
    , m_tabHeight(-1)
{
    if (m_tabFont == nullptr) {
        // Initialize FRAME_WIDTH from config (runtime, replaces constexpr)
        FRAME_WIDTH = windowManager()->config().frameThickness;

        // Load rotated tab font via FcMatrix (D-04) with fallback chain (D-02)
        x11::XftFontPtr rotatedFont = x11::make_xft_font_rotated(
            display(), "Noto Sans,DejaVu Sans,Sans:bold:size=12");

        if (!rotatedFont) {
            // Fallback to generic sans-serif
            rotatedFont = x11::make_xft_font_rotated(
                display(), "sans-serif:bold:size=12");
        }
        if (!rotatedFont) {
            windowManager()->fatal("couldn't load default rotated font, bailing out");
        }

        // Transfer ownership from RAII to raw static pointer
        // (managed via m_borderCount refcount in destructor)
        m_tabFont = rotatedFont.release();

        // Rotated Xft fonts have zero height (Plan 01 finding).
        // Use XftTextExtentsUtf8 to measure the actual glyph extent.
        XGlyphInfo extents;
        const char* sample = "M";
        XftTextExtentsUtf8(display(), m_tabFont,
            reinterpret_cast<const FcChar8*>(sample), 1, &extents);
        m_tabWidth = extents.height + 4;
        if (m_tabWidth < TAB_TOP_HEIGHT * 2 + 8) {
            m_tabWidth = TAB_TOP_HEIGHT * 2 + 8;
        }

        m_frameBackgroundPixel = windowManager()->allocateColour(windowManager()->config().frameBackground.c_str(), "frame background");
        m_buttonBackgroundPixel = windowManager()->allocateColour(windowManager()->config().buttonBackground.c_str(), "button background");
        m_borderPixel = windowManager()->allocateColour(windowManager()->config().borders.c_str(), "border");

        // Allocate Xft colors for tab label rendering
        allocateXftColors();

        // Retain m_drawGC for button fill rectangle (not text)
        XGCValues values;
        values.foreground = windowManager()->allocateColour(windowManager()->config().tabForeground.c_str(), "tab foreground");
        values.background = windowManager()->allocateColour(windowManager()->config().tabBackground.c_str(), "tab background");
        values.function = GXcopy;
        values.line_width = 0;
        values.subwindow_mode = IncludeInferiors;

        m_drawGC = x11::make_gc(display(), root(),
            GCForeground | GCBackground | GCFunction | GCLineWidth | GCSubwindowMode,
            &values);

        if (!m_drawGC) {
            windowManager()->fatal("couldn't allocate border GC");
        }
    }

    ++m_borderCount;
}


Border::~Border()
{
    if (m_parent != root()) {
        if (!m_parent) {
            std::fprintf(stderr, "wm2: zero parent in Border::~Border\n");
        } else {
            XDestroyWindow(display(), m_tab);
            XDestroyWindow(display(), m_button);
            XDestroyWindow(display(), m_parent);
            XDestroyWindow(display(), m_resize);
        }
    }

    m_tabDraw.reset();  // destroy per-instance XftDraw (Pitfall 2)

    if (--m_borderCount == 0) {
        m_drawGC.reset();

        if (m_tabFont) {
            XftFontClose(display(), m_tabFont);
            m_tabFont = nullptr;
        }

        if (m_xftColorsAllocated) {
            Display* d = display();
            Visual* visual = DefaultVisual(d, DefaultScreen(d));
            Colormap cmap = DefaultColormap(d, DefaultScreen(d));
            XftColorFree(d, visual, cmap, &m_xftForeground);
            XftColorFree(d, visual, cmap, &m_xftBackground);
            m_xftColorsAllocated = false;
        }
    }
}


void Border::allocateXftColors()
{
    if (m_xftColorsAllocated) return;

    Display* d = display();
    Visual* visual = DefaultVisual(d, DefaultScreen(d));
    Colormap cmap = DefaultColormap(d, DefaultScreen(d));

    if (!XftColorAllocName(d, visual, cmap, windowManager()->config().tabForeground.c_str(), &m_xftForeground)) {
        windowManager()->fatal("couldn't allocate Xft foreground color");
    }
    if (!XftColorAllocName(d, visual, cmap, windowManager()->config().tabBackground.c_str(), &m_xftBackground)) {
        XftColorFree(d, visual, cmap, &m_xftForeground);
        windowManager()->fatal("couldn't allocate Xft background color");
    }
    m_xftColorsAllocated = true;
}


bool Border::shapeAvailable()
{
    return windowManager()->hasShapeExtension();
}


void Border::shapeParentRectangular(int w, int h)
{
    // Simple rectangular frame: full width/height, no fancy shaping
    XRectangle frame;
    frame.x = 0;
    frame.y = 0;
    frame.width = w + m_tabWidth + FRAME_WIDTH + 1;
    frame.height = h + FRAME_WIDTH + 1;
    XShapeCombineRectangles(display(), m_parent, ShapeBounding,
        0, 0, &frame, 1, ShapeSet, YXBanded);

    frame.x++; frame.y++; frame.width -= 2; frame.height -= 2;
    XShapeCombineRectangles(display(), m_parent, ShapeClip,
        0, 0, &frame, 1, ShapeSet, YXBanded);
}


void Border::shapeTabRectangular(int w, int h)
{
    // Plain rectangular tab: no diagonal, no button cutouts
    XRectangle tabBounding;
    tabBounding.x = 0;
    tabBounding.y = 0;
    tabBounding.width = m_tabWidth + 2;
    tabBounding.height = m_tabHeight + m_tabWidth + 2;
    XShapeCombineRectangles(display(), m_tab, ShapeBounding,
        0, 0, &tabBounding, 1, ShapeSet, YXBanded);

    XRectangle tabClip;
    tabClip.x = 1;
    tabClip.y = 1;
    tabClip.width = m_tabWidth;
    tabClip.height = m_tabHeight + m_tabWidth;
    XShapeCombineRectangles(display(), m_tab, ShapeClip,
        0, 0, &tabClip, 1, ShapeSet, YXBanded);
}


void Border::setFrameVisibilityRectangular(bool visible, int w, int h)
{
    // Simple frame visibility: just show/hide resize handle
    if (!visible) return;  // In rectangular mode, frame is always visible

    if (!isFixedSize()) {
        XMapRaised(display(), m_resize);
    } else {
        XUnmapWindow(display(), m_resize);
    }
}


void Border::fatal(const char *s)
{
    windowManager()->fatal(s);
}


Display *Border::display()
{
    return m_client->display();
}


WindowManager *Border::windowManager()
{
    return m_client->windowManager();
}


Window Border::root() const
{
    return m_client->root();
}


void Border::expose(XExposeEvent *e)
{
    if (e->window != m_tab) return;
    drawLabel();
}


void Border::drawLabel()
{
    if (m_label.empty()) return;

    // Create XftDraw lazily on first use, bound to this tab window (Pitfall 2)
    if (!m_tabDraw) {
        m_tabDraw = x11::XftDrawPtr(XftDrawCreate(
            display(), m_tab,
            DefaultVisual(display(), DefaultScreen(display())),
            DefaultColormap(display(), DefaultScreen(display()))));
    }
    if (!m_tabDraw) return;

    // Clear tab background using XftDrawRect (replaces XClearWindow)
    XftDrawRect(m_tabDraw.get(), &m_xftBackground, 0, 0,
                m_tabWidth, m_tabHeight + m_tabWidth);

    // Rotated fonts have zero ascent -- use extent-based measurement for x offset
    XGlyphInfo extents;
    XftTextExtentsUtf8(display(), m_tabFont,
        reinterpret_cast<const FcChar8*>(m_label.c_str()),
        static_cast<int>(m_label.size()), &extents);

    // Draw rotated label text (UTF-8 natively via XftDrawStringUtf8)
    XftDrawStringUtf8(m_tabDraw.get(), &m_xftForeground,
                       m_tabFont,
                       2 + extents.height, m_tabHeight - 1,
                       reinterpret_cast<const FcChar8*>(m_label.c_str()),
                       static_cast<int>(m_label.size()));
}


bool Border::isTransient()
{
    return m_client->isTransient();
}


bool Border::isFixedSize()
{
    return m_client->isFixedSize();
}


bool Border::hasWindow(Window w) const
{
    return (w != root() && (w == m_parent || w == m_tab ||
                            w == m_button || w == m_resize));
}


bool Border::coordsInHole(int x, int y)
{
    return (x > 1 && x < m_tabWidth - 1 &&
            y > 1 && y < m_tabWidth - 1);
}


void Border::fixTabHeight(int maxHeight)
{
    m_tabHeight = 0x7fff;
    maxHeight -= m_tabWidth; // for diagonal

    m_label = m_client->label();

    if (!m_label.empty()) {
        XGlyphInfo extents;
        XftTextExtentsUtf8(display(), m_tabFont,
            reinterpret_cast<const FcChar8*>(m_label.c_str()),
            static_cast<int>(m_label.size()), &extents);
        m_tabHeight = extents.width + 6 + m_tabWidth;
    }

    if (m_tabHeight <= maxHeight) return;

    m_label = m_client->iconName().empty() ? std::string("incognito") : m_client->iconName();

    int len = static_cast<int>(m_label.size());
    {
        XGlyphInfo extents;
        XftTextExtentsUtf8(display(), m_tabFont,
            reinterpret_cast<const FcChar8*>(m_label.c_str()), len, &extents);
        m_tabHeight = extents.width + 6 + m_tabWidth;
    }
    if (m_tabHeight <= maxHeight) return;

    std::string newLabel = m_label;
    do {
        newLabel = newLabel.substr(0, len - 1) + "...";
        XGlyphInfo extents;
        XftTextExtentsUtf8(display(), m_tabFont,
            reinterpret_cast<const FcChar8*>(newLabel.c_str()),
            static_cast<int>(newLabel.size()), &extents);
        m_tabHeight = extents.width + 6 + m_tabWidth;
        --len;
    } while (m_tabHeight > maxHeight && len > 2);

    m_label = newLabel;

    if (m_tabHeight > maxHeight) m_tabHeight = maxHeight;
}


void Border::shapeTransientParent(int w, int h)
{
    XRectangle r;
    r.x = xIndent() - 1; r.y = yIndent() - 1;
    r.width = w + 2; r.height = h + 2;

    XShapeCombineRectangles(display(), m_parent, ShapeBounding, 0, 0,
                            &r, 1, ShapeSet, YXBanded);

    r.x = xIndent(); r.y = yIndent();
    r.width = w; r.height = h;

    XShapeCombineRectangles(display(), m_parent, ShapeClip, 0, 0,
                            &r, 1, ShapeSet, YXBanded);
}


void Border::setTransientFrameVisibility(bool visible, int w, int h)
{
    std::vector<XRectangle> rects;

    auto appendRect = [&](int x, int y, int rw, int rh) {
        XRectangle r;
        r.x = x; r.y = y; r.width = rw; r.height = rh;
        rects.push_back(r);
    };

    appendRect(0, 0, w + 1, yIndent() - 1);
    for (int i = 1; i < yIndent(); ++i) {
        appendRect(w + 1, i - 1, i + 1, 1);
    }
    appendRect(0, yIndent() - 1, xIndent() - 1, h - yIndent() + 2);
    for (int i = 1; i < yIndent(); ++i) {
        appendRect(i - 1, h, 1, i + 2);
    }

    XShapeCombineRectangles(display(), m_parent, ShapeBounding,
                            0, 0, rects.data(), static_cast<unsigned int>(rects.size()),
                            visible ? ShapeUnion : ShapeSubtract, YXSorted);

    rects.clear();

    appendRect(1, 1, w, yIndent() - 2);
    for (int i = 2; i < yIndent(); ++i) {
        appendRect(w + 1, i - 1, i, 1);
    }
    appendRect(1, yIndent() - 1, xIndent() - 2, h - yIndent() + 1);
    for (int i = 2; i < yIndent(); ++i) {
        appendRect(i - 1, h, 1, i + 1);
    }

    XShapeCombineRectangles(display(), m_parent, ShapeClip,
                            0, 0, rects.data(), static_cast<unsigned int>(rects.size()),
                            visible ? ShapeUnion : ShapeSubtract, YXSorted);
}


void Border::shapeParent(int w, int h)
{
    if (!shapeAvailable()) {
        shapeParentRectangular(w, h);
        return;
    }

    if (isTransient()) {
        shapeTransientParent(w, h);
        return;
    }

    std::vector<XRectangle> rects;

    auto appendRect = [&](int x, int y, int rw, int rh) {
        XRectangle r;
        r.x = x; r.y = y; r.width = rw; r.height = rh;
        rects.push_back(r);
    };

    // top of tab
    appendRect(0, 0, w + m_tabWidth + 1, TAB_TOP_HEIGHT + 2);
    // struts in tab, left
    appendRect(0, TAB_TOP_HEIGHT + 1,
               TAB_TOP_HEIGHT + 2, m_tabWidth - TAB_TOP_HEIGHT * 2 - 1);
    // ...and right
    appendRect(m_tabWidth - TAB_TOP_HEIGHT, TAB_TOP_HEIGHT + 1,
               TAB_TOP_HEIGHT + 2, m_tabWidth - TAB_TOP_HEIGHT * 2 - 1);

    int mainRect = static_cast<int>(rects.size());
    appendRect(xIndent() - 1, yIndent() - 1, w + 2, h + 2);

    // main tab
    appendRect(0, m_tabWidth - TAB_TOP_HEIGHT, m_tabWidth + 2,
               m_tabHeight - m_tabWidth + TAB_TOP_HEIGHT);

    // diagonal
    for (int i = 1; i < m_tabWidth - 1; ++i) {
        appendRect(i, m_tabHeight + i - 1, m_tabWidth - i + 2, 1);
    }

    XShapeCombineRectangles(display(), m_parent, ShapeBounding,
                            0, 0, rects.data(), static_cast<unsigned int>(rects.size()),
                            ShapeSet, YXSorted);

    rects[mainRect].x++;
    rects[mainRect].y++;
    rects[mainRect].width -= 2;
    rects[mainRect].height -= 2;

    XShapeCombineRectangles(display(), m_parent, ShapeClip,
                            0, 0, rects.data(), static_cast<unsigned int>(rects.size()),
                            ShapeSet, YXSorted);
}


void Border::shapeTab(int w, int h)
{
    if (!shapeAvailable()) {
        shapeTabRectangular(w, h);
        return;
    }

    if (isTransient()) return;

    std::vector<XRectangle> rects;

    auto appendRect = [&](int x, int y, int rw, int rh) {
        XRectangle r;
        r.x = x; r.y = y; r.width = rw; r.height = rh;
        rects.push_back(r);
    };

    // Bounding rectangles
    appendRect(0, 0, w + m_tabWidth + 1, TAB_TOP_HEIGHT + 2);
    appendRect(0, TAB_TOP_HEIGHT + 1, TAB_TOP_HEIGHT + 2,
               m_tabWidth - TAB_TOP_HEIGHT * 2 - 1);
    appendRect(m_tabWidth - TAB_TOP_HEIGHT, TAB_TOP_HEIGHT + 1,
               TAB_TOP_HEIGHT + 2, m_tabWidth - TAB_TOP_HEIGHT * 2 - 1);
    appendRect(0, m_tabWidth - TAB_TOP_HEIGHT, m_tabWidth + 2,
               m_tabHeight - m_tabWidth + TAB_TOP_HEIGHT);

    for (int i = 1; i < m_tabWidth - 1; ++i) {
        appendRect(i, m_tabHeight + i - 1, m_tabWidth - i + 2, 1);
    }

    XShapeCombineRectangles(display(), m_tab, ShapeBounding,
                            0, 0, rects.data(), static_cast<unsigned int>(rects.size()),
                            ShapeSet, YXSorted);

    rects.clear();

    // Clipping rectangles
    appendRect(1, 1, w + m_tabWidth - 1, TAB_TOP_HEIGHT);
    appendRect(1, TAB_TOP_HEIGHT + 1, TAB_TOP_HEIGHT,
               m_tabWidth + TAB_TOP_HEIGHT * 2 - 1);
    appendRect(m_tabWidth - TAB_TOP_HEIGHT + 1, TAB_TOP_HEIGHT + 1,
               TAB_TOP_HEIGHT, m_tabWidth + TAB_TOP_HEIGHT * 2 - 1);
    appendRect(1, m_tabWidth - TAB_TOP_HEIGHT + 1, m_tabWidth,
               m_tabHeight - m_tabWidth + TAB_TOP_HEIGHT - 1);

    for (int i = 1; i < m_tabWidth - 2; ++i) {
        appendRect(i + 1, m_tabHeight + i - 1, m_tabWidth - i, 1);
    }

    XShapeCombineRectangles(display(), m_tab, ShapeClip,
                            0, 0, rects.data(), static_cast<unsigned int>(rects.size()),
                            ShapeSet, YXSorted);
}


void Border::resizeTab(int h)
{
    if (!shapeAvailable()) return;  // No incremental resize shaping in rectangular mode

    if (isTransient()) return;

    int prevTabHeight = m_tabHeight;
    fixTabHeight(h);
    if (m_tabHeight == prevTabHeight) return;

    XWindowChanges wc;
    wc.height = m_tabHeight + 2 + m_tabWidth;
    XConfigureWindow(display(), m_tab, CWHeight, &wc);

    int shorter, longer, operation;

    if (m_tabHeight > prevTabHeight) {
        shorter = prevTabHeight;
        longer = m_tabHeight;
        operation = ShapeUnion;
    } else {
        shorter = m_tabHeight;
        longer = prevTabHeight + m_tabWidth;
        operation = ShapeSubtract;
    }

    XRectangle r;
    r.x = 0; r.y = shorter;
    r.width = m_tabWidth + 2; r.height = longer - shorter;

    XShapeCombineRectangles(display(), m_parent, ShapeBounding,
                            0, 0, &r, 1, operation, YXBanded);
    XShapeCombineRectangles(display(), m_parent, ShapeClip,
                            0, 0, &r, 1, operation, YXBanded);
    XShapeCombineRectangles(display(), m_tab, ShapeBounding,
                            0, 0, &r, 1, operation, YXBanded);

    r.x++; r.width -= 2;

    XShapeCombineRectangles(display(), m_tab, ShapeClip,
                            0, 0, &r, 1, operation, YXBanded);

    if (m_client->isActive()) {
        r.x = m_tabWidth + 1; r.y = shorter;
        r.width = FRAME_WIDTH - 1; r.height = longer - shorter;
        XShapeCombineRectangles(display(), m_parent, ShapeBounding,
                                0, 0, &r, 1, ShapeUnion, YXBanded);
    }

    std::vector<XRectangle> diagRects;
    for (int i = 1; i < m_tabWidth - 1; ++i) {
        XRectangle dr;
        dr.x = i; dr.y = m_tabHeight + i - 1;
        dr.width = m_tabWidth - i + 2; dr.height = 1;
        diagRects.push_back(dr);
    }

    XShapeCombineRectangles(display(), m_parent, ShapeBounding,
                            0, 0, diagRects.data(),
                            static_cast<unsigned int>(diagRects.size()),
                            ShapeUnion, YXBanded);
    XShapeCombineRectangles(display(), m_parent, ShapeClip,
                            0, 0, diagRects.data(),
                            static_cast<unsigned int>(diagRects.size()),
                            ShapeUnion, YXBanded);
    XShapeCombineRectangles(display(), m_tab, ShapeBounding,
                            0, 0, diagRects.data(),
                            static_cast<unsigned int>(diagRects.size()),
                            ShapeUnion, YXBanded);

    if (diagRects.size() >= 2) {
        for (size_t i = 0; i < diagRects.size() - 1; ++i) {
            diagRects[i].x++; diagRects[i].width -= 2;
        }
        XShapeCombineRectangles(display(), m_tab, ShapeClip,
                                0, 0, diagRects.data(),
                                static_cast<unsigned int>(diagRects.size() - 1),
                                ShapeUnion, YXBanded);
    }
}


void Border::shapeResize()
{
    std::vector<XRectangle> rects;

    for (int i = 0; i < FRAME_WIDTH * 2; ++i) {
        XRectangle r;
        r.x = FRAME_WIDTH * 2 - i - 1; r.y = i;
        r.width = i + 1; r.height = 1;
        rects.push_back(r);
    }

    XShapeCombineRectangles(display(), m_resize, ShapeBounding, 0, 0,
                            rects.data(), static_cast<unsigned int>(rects.size()),
                            ShapeSet, YXBanded);

    rects.clear();

    for (int i = 1; i < FRAME_WIDTH * 2; ++i) {
        XRectangle r;
        r.x = FRAME_WIDTH * 2 - i; r.y = i;
        r.width = i; r.height = 1;
        rects.push_back(r);
    }

    XShapeCombineRectangles(display(), m_resize, ShapeClip, 0, 0,
                            rects.data(), static_cast<unsigned int>(rects.size()),
                            ShapeSet, YXBanded);

    rects.clear();

    for (int i = 0; i < FRAME_WIDTH * 2 - 3; ++i) {
        XRectangle r;
        r.x = FRAME_WIDTH * 2 - i - 1; r.y = i + 3;
        r.width = 1; r.height = 1;
        rects.push_back(r);
    }

    XShapeCombineRectangles(display(), m_resize, ShapeClip, 0, 0,
                            rects.data(), static_cast<unsigned int>(rects.size()),
                            ShapeSubtract, YXBanded);

    // Install down-right cursor on resize handle
    windowManager()->installCursorOnWindow(WindowManager::RootCursor::DownRight, m_resize);
}


void Border::setFrameVisibility(bool visible, int w, int h)
{
    if (!shapeAvailable()) {
        setFrameVisibilityRectangular(visible, w, h);
        return;
    }

    std::vector<XRectangle> rects;

    auto appendRect = [&](int x, int y, int rw, int rh) {
        XRectangle r;
        r.x = x; r.y = y; r.width = rw; r.height = rh;
        rects.push_back(r);
    };

    if (isTransient()) {
        setTransientFrameVisibility(visible, w, h);
        return;
    }

    // Bounding rectangles
    appendRect(m_tabWidth + w + 1, 0, FRAME_WIDTH + 1, FRAME_WIDTH);
    appendRect(m_tabWidth + 2, TAB_TOP_HEIGHT + 2, w,
               FRAME_WIDTH - TAB_TOP_HEIGHT - 2);

    int ww = m_tabWidth - TAB_TOP_HEIGHT * 2 - 4;
    appendRect((m_tabWidth + 2 - ww) / 2, (m_tabWidth + 2 - ww) / 2, ww, ww);

    appendRect(m_tabWidth + 2, FRAME_WIDTH,
               FRAME_WIDTH - 2, m_tabHeight + m_tabWidth - FRAME_WIDTH - 2);

    // swap last two if sorted wrong
    if (rects.size() >= 2 && rects[rects.size() - 2].y > rects[rects.size() - 1].y) {
        std::swap(rects[rects.size() - 2], rects[rects.size() - 1]);
    }

    size_t finalIdx = rects.size();
    rects.push_back(rects[finalIdx - 1]);
    rects[finalIdx].x -= 1;
    rects[finalIdx].y += rects[finalIdx].height;
    rects[finalIdx].width += 1;
    rects[finalIdx].height = h - rects[finalIdx].height + 2;

    XShapeCombineRectangles(display(), m_parent, ShapeBounding,
                            0, 0, rects.data(), static_cast<unsigned int>(rects.size()),
                            visible ? ShapeUnion : ShapeSubtract, YXSorted);
    rects.clear();

    // Clip rectangles
    appendRect(m_tabWidth + w + 1, 1, FRAME_WIDTH, FRAME_WIDTH - 1);
    appendRect(m_tabWidth + 2, TAB_TOP_HEIGHT + 2, w,
               FRAME_WIDTH - TAB_TOP_HEIGHT - 2);

    ww = m_tabWidth - TAB_TOP_HEIGHT * 2 - 6;
    appendRect((m_tabWidth + 2 - ww) / 2, (m_tabWidth + 2 - ww) / 2, ww, ww);

    appendRect(m_tabWidth + 2, FRAME_WIDTH,
               FRAME_WIDTH - 2, h - FRAME_WIDTH);

    // swap last two if sorted wrong
    if (rects.size() >= 2 && rects[rects.size() - 2].y > rects[rects.size() - 1].y) {
        std::swap(rects[rects.size() - 2], rects[rects.size() - 1]);
    }

    appendRect(m_tabWidth + 2, h, FRAME_WIDTH - 2, FRAME_WIDTH + 1);

    XShapeCombineRectangles(display(), m_parent, ShapeClip,
                            0, 0, rects.data(), static_cast<unsigned int>(rects.size()),
                            visible ? ShapeUnion : ShapeSubtract, YXSorted);

    if (visible && !isFixedSize()) {
        XMapRaised(display(), m_resize);
    } else {
        XUnmapWindow(display(), m_resize);
    }
}


void Border::configure(int x, int y, int w, int h,
                       unsigned long mask, int detail,
                       bool force)
{
    if (!m_parent || m_parent == root()) {
        m_parent = XCreateSimpleWindow(display(), root(), 1, 1, 1, 1, 0,
                                       m_borderPixel, m_frameBackgroundPixel);

        m_tab = XCreateSimpleWindow(display(), m_parent, 1, 1, 1, 1, 0,
                                    m_borderPixel, m_xftBackground.pixel);

        m_button = XCreateSimpleWindow(display(), m_parent, 1, 1, 1, 1, 0,
                                       m_borderPixel, m_buttonBackgroundPixel);

        m_resize = XCreateWindow(display(), m_child, 1, 1,
                                 FRAME_WIDTH * 2, FRAME_WIDTH * 2, 0,
                                 CopyFromParent, InputOutput, CopyFromParent, 0L, nullptr);

        shapeResize();

        XSelectInput(display(), m_parent,
                     SubstructureRedirectMask | SubstructureNotifyMask |
                     ButtonPressMask | ButtonReleaseMask);

        if (!isTransient()) {
            XSelectInput(display(), m_tab,
                         ExposureMask | ButtonPressMask | ButtonReleaseMask |
                         EnterWindowMask);
        }

        XSelectInput(display(), m_button,
                     ButtonPressMask | ButtonReleaseMask);
        XSelectInput(display(), m_resize, ButtonPressMask | ButtonReleaseMask);
        mask |= CWX | CWY | CWWidth | CWHeight | CWBorderWidth;
    }

    XWindowChanges wc;
    wc.x = x - xIndent();
    wc.y = y - yIndent();
    wc.width  = w + xIndent() + 1;
    wc.height = h + yIndent() + 1;
    wc.border_width = 0;
    wc.sibling = None;
    wc.stack_mode = detail;
    XConfigureWindow(display(), m_parent, mask, &wc);

    unsigned long rmask = 0L;
    if (mask & CWWidth)  rmask |= CWX;
    if (mask & CWHeight) rmask |= CWY;
    wc.x = w - FRAME_WIDTH * 2;
    wc.y = h - FRAME_WIDTH * 2;
    XConfigureWindow(display(), m_resize, rmask, &wc);

    if (force ||
        (m_prevW < 0 || m_prevH < 0) ||
        ((mask & (CWWidth | CWHeight)) && (w != m_prevW || h != m_prevH))) {

        int prevTabHeight = m_tabHeight;
        if (isTransient()) m_tabHeight = 10;
        else fixTabHeight(h);

        shapeParent(w, h);
        setFrameVisibility(m_client->isActive(), w, h);

        if (force || w != m_prevW ||
            prevTabHeight != m_tabHeight || m_prevW < 0 || m_prevH < 0) {

            wc.x = 0;
            wc.y = 0;
            wc.width = w + xIndent();
            wc.height = m_tabHeight + 2 + m_tabWidth;
            XConfigureWindow(display(), m_tab, mask, &wc);
            shapeTab(w, h);
        }

        m_prevW = w;
        m_prevH = h;

    } else {
        resizeTab(h);
    }

    wc.x = TAB_TOP_HEIGHT + 2;
    wc.y = wc.x;
    wc.width = wc.height = m_tabWidth - TAB_TOP_HEIGHT * 2 - 4;
    XConfigureWindow(display(), m_button, mask, &wc);
}


void Border::moveTo(int x, int y)
{
    XWindowChanges wc;
    wc.x = x - xIndent();
    wc.y = y - yIndent();
    XConfigureWindow(display(), m_parent, CWX | CWY, &wc);
}


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


void Border::mapRaised()
{
    if (m_parent == root()) {
        std::fprintf(stderr, "wm2: bad parent in Border::mapRaised()\n");
    } else {
        XMapRaised(display(), m_parent);

        if (!isTransient()) {
            XMapWindow(display(), m_tab);
            XMapRaised(display(), m_button);
            if (!isFixedSize()) XMapRaised(display(), m_resize);
        }
    }
}


void Border::lower()
{
    XLowerWindow(display(), m_parent);
}


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


void Border::decorate(bool active, int w, int h)
{
    setFrameVisibility(active, w, h);
}


void Border::reparent()
{
    x11::ServerGrab grab(display());
    XReparentWindow(display(), m_child, m_parent, xIndent(), yIndent());
}


void Border::eventButton(XButtonEvent *e)
{
    if (e->window == m_parent) {
        if (!m_client->isActive()) return;
        if (isTransient()) {
            if (e->x >= xIndent() && e->y >= yIndent()) {
                return;
            } else {
                m_client->move(e);
                return;
            }
        }
        m_client->moveOrResize(e);
        return;

    } else if (e->window == m_tab) {
        m_client->move(e);
        return;
    }

    if (e->window == m_resize) {
        m_client->resize(e, true, true);
        return;
    }

    if (e->window != m_button || e->type == ButtonRelease) return;

    int menuGrabMask = ButtonPressMask | ButtonReleaseMask |
                       ButtonMotionMask | StructureNotifyMask;
    if (windowManager()->attemptGrab(m_button, None, menuGrabMask, e->time)
        != GrabSuccess) {
        return;
    }

    XEvent event;
    bool found;
    bool done = false;
    struct timeval sleepval;
    unsigned long tdiff = 0L;
    int x = e->x;
    int y = e->y;
    int action = 1;
    int buttonSize = m_tabWidth - TAB_TOP_HEIGHT * 2 - 4;

    XFillRectangle(display(), m_button, m_drawGC.get(), 0, 0, buttonSize, buttonSize);

    while (!done) {

        found = false;

        if (tdiff > static_cast<unsigned long>(windowManager()->config().destroyWindowDelay) && action == 1) {
            windowManager()->installCursor(WindowManager::RootCursor::Delete);
            action = 2;
        }

        while (XCheckMaskEvent(display(),
                ButtonPressMask | ButtonReleaseMask | ButtonMotionMask | ExposureMask,
                &event)) {
            found = true;
            if (event.type != MotionNotify) break;
        }

        if (!found) {
            sleepval.tv_sec = 0;
            sleepval.tv_usec = 50000;
            select(0, nullptr, nullptr, nullptr, &sleepval);
            tdiff += 50;
            continue;
        }

        switch (event.type) {

        default:
            std::fprintf(stderr, "wm2: unknown event type %d\n", event.type);
            break;

        case Expose:
            windowManager()->eventExposure(&event.xexpose);
            break;

        case ButtonPress:
            break;

        case ButtonRelease:
            {
                int state = event.xbutton.state & (Button1Mask | Button2Mask | Button3Mask |
                                                    Button4Mask | Button5Mask);
                if (event.xbutton.type == ButtonRelease && (state & (state - 1)) != 0) {
                    action = 0;
                }
            }
            if (x < 0 || y < 0 || x >= buttonSize || y >= buttonSize) {
                action = 0;
            }
            windowManager()->releaseGrab(&event.xbutton);
            done = true;
            break;

        case MotionNotify:
            tdiff = event.xmotion.time - e->time;
            if (tdiff > 5000L) tdiff = 5001L;

            x = event.xmotion.x;
            y = event.xmotion.y;

            if (action == 0 || action == 2) {
                if (x < 0 || y < 0 || x >= buttonSize || y >= buttonSize) {
                    windowManager()->installCursor(WindowManager::RootCursor::Normal);
                    action = 0;
                } else {
                    windowManager()->installCursor(WindowManager::RootCursor::Delete);
                    action = 2;
                }
            }
            break;
        }
    }

    XClearWindow(display(), m_button);
    windowManager()->installCursor(WindowManager::RootCursor::Normal);

    if (tdiff > 5000L) return;  // dithered too long

    if (action == 1) m_client->hide();
    else if (action == 2) m_client->kill();
}
