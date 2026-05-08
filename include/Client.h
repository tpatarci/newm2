#pragma once

#include "x11wrap.h"
#include <X11/Xutil.h>
#include <string>
#include <memory>
#include <vector>

class WindowManager;
class Border;

// Protocol flags (from upstream defines)
enum class Protocol : int {
    Delete    = 1,
    TakeFocus = 2
};

enum class ClientState {
    Withdrawn = 0,   // matches X11 WithdrawnState
    Normal    = 1,   // matches X11 NormalState
    Iconic    = 3    // matches X11 IconicState (NOT 2 -- X11 uses 3)
};

enum class WindowType {
    Normal,
    Dock,
    Dialog,
    Notification
};

class Client {
public:
    Client(WindowManager *wm, Window w);
    ~Client();

    // No copy, no move (X11 window identity is unique)
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // Lifecycle (replaces upstream's delete this)
    void manage(bool mapped);

    // State queries
    bool isHidden()     const { return m_state == ClientState::Iconic; }
    bool isWithdrawn()  const { return m_state == ClientState::Withdrawn; }
    bool isNormal()     const { return m_state == ClientState::Normal; }
    bool isTransient()  const { return m_transient != None; }
    bool isFixedSize()  const { return m_fixedSize; }
    bool isActive()     const;
    bool isFullscreen() const { return m_isFullscreen; }
    bool isMaximized()  const { return m_isMaximizedVert && m_isMaximizedHorz; }
    bool isDock()       const { return m_windowType == WindowType::Dock; }
    bool isNotification() const { return m_windowType == WindowType::Notification; }
    WindowType windowType() const { return m_windowType; }

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

    // EWMH state management
    void setFullscreen(bool fullscreen);
    void toggleFullscreen();
    void setMaximized(bool vert, bool horz);
    void toggleMaximized();
    void applyWmState(int action, Atom prop1, Atom prop2);
    void updateNetWmState();

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
    Client* activeClient() const;
    Display* display();

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
    void fatal(const char *m);

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

    ClientState m_state;
    int m_protocol;
    bool m_managed;
    bool m_reparenting;

    // EWMH state
    WindowType m_windowType{WindowType::Normal};
    bool m_isFullscreen{false};
    bool m_isMaximizedVert{false};
    bool m_isMaximizedHorz{false};
    int m_preFullscreenX{0}, m_preFullscreenY{0};
    int m_preFullscreenW{0}, m_preFullscreenH{0};
    int m_preMaximizedX{0}, m_preMaximizedY{0};
    int m_preMaximizedW{0}, m_preMaximizedH{0};

    std::string m_name;
    std::string m_iconName;
    std::string m_label;
    static const char* const m_defaultLabel;

    Colormap m_colormap;
    std::vector<Window> m_colormapWindows;
    std::vector<Colormap> m_windowColormaps;

    WindowManager *const m_windowManager;

    // Property access
    std::string getProperty(Atom atom);
    bool getState(int *state);
    void setState(ClientState state);
    void setState(int state);

    // Internal setup
    bool setLabel();
    void getColormaps();
    void getProtocols();
    void getTransient();
    void getWindowType();
    void decorate(bool active);

    // Gesture detection
    void detectFullscreenGesture(XButtonEvent *e);

    static bool isValidTransition(ClientState from, ClientState to);
};
