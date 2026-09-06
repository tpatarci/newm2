#pragma once

#include "x11wrap.h"
#include "Rules.h"
#include <X11/Xutil.h>
#include <X11/Xatom.h>
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
    // RULES-01 (plan 08-10): the two WM_CLASS fields, read once at manage time.
    // Empty for a window that sets no class hint, which is entirely normal.
    const std::string& resName() const { return m_resName; }
    const std::string& resClass() const { return m_resClass; }
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

    // CGUI-04 (plan 09-04): re-lay this client's decoration out for a frame
    // thickness that changed while the window manager was running. Reached only
    // from WindowManager::applyConfig(), which is the single funnel every live
    // configuration change goes through.
    void relayoutFrame();

    // CGUI-04 (plan 09-05): the same funnel's other two per-client entry
    // points. relayoutFrameForFont() re-lays the decoration out for a tab face
    // whose metrics moved the tab's thickness, and repaintForColourChange()
    // pushes a reloaded palette onto this client's frame. Both carry the SAME three skip
    // conditions relayoutFrame() carries, named individually in src/Client.cpp
    // rather than folded into one catch-all guard.
    void relayoutFrameForFont();
    void repaintForColourChange();

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

    // Deferred item 8: arm the self-reparent guard before a reparent this
    // window manager performs itself, so eventUnmap() does not mistake the
    // server's implicit unmap for a client withdrawing its own window. Only
    // arms when the window is actually mapped -- see the definition.
    void markReparenting();

    // FOCUS-01 (plan 08-08): the map-time half of focus-stealing prevention.
    //
    // shouldFocusOnMap() answers whether this newly mapped window may take the
    // input focus, by comparing the timestamp the client published against the
    // WM's last real user interaction. The activation-message path in
    // src/Events.cpp arbitrates the same question through the same shared
    // WindowManager::isUserTimeRecent() helper -- deliberately, because a
    // mitigation implemented at only one of its two entry points is not a
    // mitigation, it is a speed bump.
    //
    // demandAttention() is the visible half. A refused window is mapped and
    // fully managed, just not focused, and it advertises that it wants the user
    // via _NET_WM_STATE_DEMANDS_ATTENTION and the ICCCM urgency hint. Refusing
    // silently would trade focus stealing for lost windows.
    bool shouldFocusOnMap();
    void demandAttention();

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
    // Managed WITHOUT a frame: docks, notifications and rule-no-decorate
    // windows (issue #3 / Codex P1). parent() is root for these, so every
    // frame operation is skipped or redirected at the client window itself.
    bool m_frameless{false};
    // Only rule-no-decorate NORMAL windows take focus; docks and
    // notifications never do (a product decision recorded in issue #3).
    bool isFocusableFrameless() const;
    // Remove exactly two states from _NET_WM_STATE, keeping the rest (see .cpp).
    void stripNetWmStates(Atom a, Atom b);
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

    // FOCUS-01: set when this window was refused focus, cleared once it gets
    // the attention it asked for. Published through updateNetWmState() and
    // nowhere else -- plan 08-10 extends that same publisher with the
    // skip-taskbar/skip-pager states and must preserve this one.
    bool m_demandsAttention{false};

    // Clears the attention state and the ICCCM urgency hint together. Private
    // because the only correct trigger is activation: the EWMH says the WM
    // should unset the state once the window has had the attention it wanted.
    void clearAttentionState();

    std::string m_name;
    std::string m_iconName;
    std::string m_label;
    static const char* const m_defaultLabel;

    // RULES-01: WM_CLASS, read once when the window is taken under management.
    // Client-supplied and untrusted (threat T-8-PROP), so the copy is bounded.
    std::string m_resName;
    std::string m_resClass;

    // RULES-02 (plan 08-10): the resolved later-wins fold for THIS window, and
    // the two flags derived from it that outlive manage().
    //
    // m_ruleNoDecorate is kept distinct from isDock() on purpose: both end up on
    // the same unframed code path, but only a dock may recompute the workarea,
    // and collapsing the two would let any no-decorate rule shrink every other
    // window's usable screen.
    RuleOutcome m_ruleOutcome;
    bool m_ruleNoDecorate{false};
    bool m_skipTaskbar{false};

    Colormap m_colormap;
    std::vector<Window> m_colormapWindows;
    std::vector<Colormap> m_windowColormaps;

    WindowManager *const m_windowManager;

    // Property access.
    //
    // The type argument defaults to XA_STRING, which is what every caller but
    // one wants and what this function requested unconditionally before plan
    // 08.5-01. It has to be a parameter because _NET_WM_NAME is UTF8_STRING:
    // asking for it as XA_STRING does not fail loudly, it returns EMPTY through
    // a silent type mismatch -- which is how a title rule keyed on it would have
    // become a config key that never fires.
    std::string getProperty(Atom atom, Atom type = XA_STRING);

    // The window's title, EWMH first (D-8.5-02). See the definition for why the
    // order is not negotiable.
    std::string getWindowTitle();

    bool getState(int *state);
    void setState(ClientState state);
    void setState(int state);

    // Internal setup
    bool setLabel();
    void getColormaps();
    void getProtocols();
    void getTransient();
    void getWindowType();
    void getClassHint();
    void applyWindowRules();
    void clampGeometryToScreen();

    // Bound a frame origin so the sideways tab -- the only thing a pointer can
    // grab -- stays on screen. Unlike clampGeometryToScreen() and
    // ensureVisible(), which pull the whole window into view, this constrains
    // ONLY the handle and lets the body hang off any edge.
    void clampToKeepHandleOnScreen(int &x, int &y);

    void decorate(bool active);

    // Gesture detection
    void detectFullscreenGesture(XButtonEvent *e);

    static bool isValidTransition(ClientState from, ClientState to);
};
