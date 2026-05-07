#pragma once

#include "x11wrap.h"
#include <X11/Xutil.h>
#include <string>

class Client;
class WindowManager;

// Frame dimensions (from upstream Config.h and Border.h)
constexpr int TAB_TOP_HEIGHT = 2;
constexpr int FRAME_WIDTH = 7;          // CONFIG_FRAME_THICKNESS
constexpr int TRANSIENT_FRAME_WIDTH = 4;

class Border {
public:
    Border(Client *client, Window child);
    ~Border();

    void map();
    void unmap();
    void lower();
    void mapRaised();
    void decorate(bool active, int w, int h);
    void reparent();
    void configure(int x, int y, int w, int h, unsigned long mask, int detail,
                   bool force = false);
    void moveTo(int x, int y);

    // Accessors
    Window parent() const { return m_parent; }
    bool hasWindow(Window w) const;

    // Delegated accessors (call into Client)
    WindowManager* windowManager();
    bool isTransient();
    bool isFixedSize();

    Display* display();
    Window root() const;

    void expose(XExposeEvent *e);
    void eventButton(XButtonEvent *e);

    int yIndent() {
        return isTransient() ? TRANSIENT_FRAME_WIDTH + 1 : FRAME_WIDTH + 1;
    }
    int xIndent() {
        return isTransient() ? TRANSIENT_FRAME_WIDTH + 1 :
            m_tabWidth + FRAME_WIDTH + 1;
    }

    bool coordsInHole(int x, int y);

private:
    void fatal(const char *m);

    std::string m_label;

    void fixTabHeight(int h);
    void drawLabel();

    void setFrameVisibility(bool, int, int);
    void setTransientFrameVisibility(bool, int, int);
    void shapeParent(int, int);
    void shapeTransientParent(int, int);
    void shapeTab(int, int);
    void resizeTab(int);
    void shapeResize();

    Client *m_client;

    Window m_parent;
    Window m_tab;
    Window m_child;
    Window m_button;
    Window m_resize;

    int m_prevW;
    int m_prevH;
    int m_tabHeight;

    // Static resources shared across all Border instances
    static int m_tabWidth;
    static XftFont *m_tabFont;         // raw pointer, managed via static refcount
    static x11::GCPtr m_drawGC;
    static unsigned long m_frameBackgroundPixel;
    static unsigned long m_buttonBackgroundPixel;
    static unsigned long m_borderPixel;
    static int m_borderCount;  // reference count for static resources

    // Static Xft colors (allocated once, shared across all Border instances)
    static XftColor m_xftForeground;
    static XftColor m_xftBackground;
    static bool m_xftColorsAllocated;  // guard for XftColor allocation

    // Per-instance XftDraw for tab label rendering (Pitfall 2)
    x11::XftDrawPtr m_tabDraw;

    // Shape extension helpers
    void allocateXftColors();
    bool shapeAvailable();

    // Rectangular fallback when Shape extension unavailable (D-07)
    void shapeParentRectangular(int w, int h);
    void shapeTabRectangular(int w, int h);
    void setFrameVisibilityRectangular(bool visible, int w, int h);
};
