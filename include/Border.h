#pragma once

#include "x11wrap.h"
#include <X11/Xutil.h>
#include <string>
#include <vector>

class Client;
class WindowManager;

// Frame dimensions (from upstream Config.h and Border.h)
constexpr int TAB_TOP_HEIGHT = 2;
extern int FRAME_WIDTH;                 // CONFIG_FRAME_THICKNESS (runtime from Config)
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

    // Re-lay this frame out IN PLACE after FRAME_WIDTH changed (CGUI-04, plan
    // 09-04). Every window the frame is made of -- the frame itself, the tab,
    // the button and the resize handle -- has geometry computed from the frame
    // thickness, and two of them (the resize handle's size and its shape) are
    // set only at creation, so configure() alone would leave a corner grabber
    // sized for the old thickness.
    //
    // Deliberately NOT a destroy-and-rebuild: rebuilding would reparent the
    // client, which flashes and loses stacking order. The client window keeps
    // its size and its position ON SCREEN; only the decoration around it moves.
    void relayoutForFrameThickness(int x, int y, int w, int h);

    // Fullscreen support
    void stripForFullscreen();
    void restoreFromFullscreen(int x, int y, int w, int h);

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

    // The button paints a small square but ANSWERS across the whole top square
    // of the tab. The two sizes are deliberately different and must not be
    // conflated: the painted one is the window's look, which is settled, and the
    // hit one is how hard it is to aim at, which was 8x8 -- 64 square pixels,
    // with every near-miss landing on the tab (starting a drag) or in the shaped
    // hole (falling through to the root window). See Border::configure().
    static int buttonDrawInset() { return TAB_TOP_HEIGHT + 2; }
    static int buttonDrawSize()  { return m_tabWidth - TAB_TOP_HEIGHT * 2 - 4; }
    static int buttonHitSize()   { return m_tabWidth; }

private:
    void fatal(const char *m);

    // XDIS-04: which rung of the tab-font degradation ladder this process
    // landed on. Established once, at the first Border construction, and never
    // revisited -- font availability is fixed for the lifetime of the X
    // connection, exactly like the extension sentinels in include/Manager.h.
    //
    // Rung 1 is the normal path and the only one that prints nothing; every
    // other rung announces itself on stderr so a release-evidence transcript
    // records the degradation instead of leaving it to be inferred from a
    // screenshot.
    enum class TabFontRung {
        RotatedPreferred,   // 1 -- sideways labels from the preferred chain
        RotatedGeneric,     // 2 -- sideways labels from the generic sans chain
        Unrotated,          // 3 -- horizontal labels, truncated to the tab width
        NoFont              // 4 -- no label at all; frames are still drawn (None is an Xlib macro)
    };

    std::string m_label;

    void loadTabFont();
    void fixTabHeight(int h);
    void drawLabel(bool active);
    void drawLabelHorizontal();

    // Predicates over the rung, in the same shape as the capability predicates
    // in include/Manager.h: consumers branch on these, never on the enumerator.
    static bool hasTabFont()    { return m_tabFont != nullptr; }
    static bool tabFontRotated() {
        return m_tabFont != nullptr &&
               (m_tabFontRung == TabFontRung::RotatedPreferred ||
                m_tabFontRung == TabFontRung::RotatedGeneric);
    }

    void setFrameVisibility(bool, int, int);
    void setTransientFrameVisibility(bool, int, int);
    void shapeParent(int, int);
    void shapeTransientParent(int, int);
    void shapeTab(int, int);
    void resizeTab(int);
    void shapeResize();

    // The tab-button press loop. Takes the press position in BUTTON-window
    // coordinates, because it is entered from two places: a press on the button
    // itself, and a press on the tab that landed inside the button's target
    // square but outside the small square the button paints.
    void runButtonPress(XButtonEvent *e, int startX, int startY);

    // D-11: the one and only entry point through which this class is permitted
    // to issue a rectangle-combining request to the X Shape extension. Mirrors
    // the Xlib call's parameter list minus the Display*, and takes the rectangle
    // array as pointer-to-const so callers may pass const data. Definition in
    // src/Border.cpp guards on the capability and returns without touching the
    // connection when it is absent.
    void combineShape(Window dest, int destKind, int xOff, int yOff,
                      const XRectangle *rects, int nRects,
                      int op, int ordering);

    // The YXSorted form of the above (plan 08-13).
    //
    // A YXSorted request is a PROMISE to the server that the rectangles arrive
    // sorted by y origin and then by x origin. The server validates the promise
    // and rejects the whole request with BadMatch when it is broken -- so a
    // mis-ordered list does not merely render slowly, it leaves the window
    // UNSHAPED, and WindowManager::errorHandler() logs the rejection and carries
    // on. Every list below is assembled in an order that depends on FRAME_WIDTH
    // and on the measured tab width, neither of which is fixed, so the promise
    // held only for a narrow band of configured frame thicknesses.
    //
    // Takes the vector BY VALUE on purpose: shapeParent() submits one list
    // twice, mutating a remembered INDEX in between, and sorting the caller's
    // own vector would invalidate that index.
    void combineShapeSorted(Window dest, int destKind, int xOff, int yOff,
                            std::vector<XRectangle> rects, int op);

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
    static TabFontRung m_tabFontRung;

    // The statics below used to be initialised on "m_tabFont is still null",
    // which stops working the moment a null font becomes a legitimate outcome
    // (rung 4): every subsequent Border would re-run the whole block and leak a
    // GC per frame. The guard is therefore explicit rather than inferred.
    static bool m_staticsInitialised;
    static x11::GCPtr m_drawGC;

    // The 1 px raised bevel (plan 08.5-02), drawn on the ACTIVE window only.
    //
    // ONE PIXEL, NEVER TWO. At this window manager's scale -- a 7 px frame and
    // a ~22 px tab -- a 2 px bevel is Windows 95. A single pixel inside the
    // existing 1 px black outline puts four values across about four pixels:
    // highlight, body, shadow, black. That is the machined-metal read, and it
    // is the one NeXTSTEP used, which is where wm2's look comes from.
    //
    // Active-only is a deliberate extension of the WM's existing idiom rather
    // than a new one: activity already means "the frame appears"
    // (setFrameVisibility), so now the active window also LIFTS. It costs no
    // config, no per-state colour, and no second tab background.
    //
    // Either GC may be null when the colormap is full. Both draw sites check;
    // no bevel is the correct degradation, not a fatal error.
    static x11::GCPtr m_bevelLightGC;
    static x11::GCPtr m_bevelShadowGC;
    void drawBevel(bool active);
    void drawButtonBevel(bool active);
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
