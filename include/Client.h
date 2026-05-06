#pragma once

#include "x11wrap.h"
#include <X11/Xutil.h>
#include <string>
#include <memory>

class WindowManager;
class Border;

// Protocol flags (from upstream defines)
enum class Protocol : int {
    Delete    = 1,
    TakeFocus = 2
};

class Client {
public:
    Client(WindowManager *wm, Window w);
    ~Client();

    // No copy, no move (X11 window identity is unique)
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // Lifecycle (replaces upstream's delete this)
    void release();
    void manage(bool mapped);

    // State queries
    bool isHidden()     const { return m_state == IconicState; }
    bool isWithdrawn()  const { return m_state == WithdrawnState; }
    bool isNormal()     const { return m_state == NormalState; }
    bool isTransient()  const { return m_transient != None; }
    bool isFixedSize()  const { return m_fixedSize; }
    bool isActive()     const;

    // Window identity
    Window window()     const { return m_window; }
    Window transientFor() const { return m_transient; }
    bool hasWindow(Window w) const;

    // Accessors
    const std::string& name() const { return m_name; }
    const std::string& iconName() const { return m_iconName; }
    const std::string& label() const { return m_label; }
    int x() const { return m_x; }
    int y() const { return m_y; }
    int width() const { return m_w; }
    int height() const { return m_h; }

    // State changes
    void activate();
    void deactivate();
    void gravitate(bool invert);
    void installColormap();
    void unreparent();
    void withdraw(bool = true);
    void hide();
    void unhide(bool map);
    void rename();
    void kill();
    void mapRaised();
    void lower();
    void ensureVisible();

    // Interaction
    void move(XButtonEvent *e);
    void resize(XButtonEvent *e, bool horizontal, bool vertical);
    void moveOrResize(XButtonEvent *e);

    // Client messages
    void sendMessage(Atom a, long data);
    void sendConfigureNotify();

    void activateAndWarp();
    void focusIfAppropriate(bool);

    // Accessors for Border
    WindowManager* windowManager() { return m_windowManager; }
    Window parent();
    Window root();
    Client* revertTo() { return m_revert; }
    void setRevertTo(Client *c) { m_revert = c; }
    Client* activeClient();

    // Event handlers (called from WindowManager dispatch)
    void eventButton(XButtonEvent*);
    void eventMapRequest(XMapRequestEvent*);
    void eventConfigureRequest(XConfigureRequestEvent*);
    void eventUnmap(XUnmapEvent*);
    void eventColormap(XColormapEvent*);
    void eventProperty(XPropertyEvent*);
    void eventEnter(XCrossingEvent*);
    void eventFocusIn(XFocusInEvent*);
    void eventExposure(XExposeEvent*);

    void selectOnMotion(Window w, bool select);

private:
    void fatal(const char *m) { m_windowManager->fatal(m); }
    Display* display() { return m_windowManager->display(); }

    Window m_window;
    Window m_transient;
    std::unique_ptr<Border> m_border;

    Client *m_revert;

    int m_x, m_y, m_w, m_h, m_bw;

    XSizeHints m_sizeHints;
    bool m_fixedSize;
    int m_minWidth;
    int m_minHeight;
    void fixResizeDimensions(int &w, int &h, int &dw, int &dh);

    int m_state;
    int m_protocol;
    bool m_managed;
    bool m_reparenting;

    std::string m_name;
    std::string m_iconName;
    std::string m_label;
    static const char* const m_defaultLabel;

    Colormap m_colormap;
    int m_colormapWinCount;
    Window *m_colormapWindows;
    Colormap *m_windowColormaps;

    WindowManager *const m_windowManager;

    // Property access
    std::string getProperty(Atom atom);
    bool getState(int *state);
    void setState(int state);

    // Internal setup
    bool setLabel();
    void getColormaps();
    void getProtocols();
    void getTransient();
    void decorate(bool active);
};
