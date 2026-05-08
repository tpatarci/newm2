#pragma once

#include "x11wrap.h"
#include "Config.h"
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/shape.h>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <csignal>
#include <chrono>
#include <unistd.h>

// RAII wrapper for POSIX file descriptors (not X11 resources -- those are in x11wrap.h)
struct FdGuard {
    int fd = -1;
    FdGuard() = default;
    explicit FdGuard(int f) : fd(f) {}
    ~FdGuard() { if (fd >= 0) ::close(fd); }
    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;
    FdGuard(FdGuard&& o) noexcept : fd(o.fd) { o.fd = -1; }
    FdGuard& operator=(FdGuard&& o) noexcept {
        if (this != &o) { if (fd >= 0) ::close(fd); fd = o.fd; o.fd = -1; }
        return *this;
    }
    int get() const { return fd; }
};

class Client;

class WindowManager {
public:
    WindowManager(const Config& config);
    ~WindowManager();

    const Config& config() const { return m_config; }

    void fatal(const char *message);

    // Client management
    Client* windowToClient(Window w, bool create = false);
    Client* activeClient() { return m_activeClient; }
    void setActiveClient(Client* c) { m_activeClient = c; }

    // Display accessors (raw pointers for X11 API calls)
    Display* display() { return m_display.get(); }
    Window root() { return m_root; }
    int screen() { return m_screenNumber; }
    bool hasShapeExtension() const { return m_shapeEvent >= 0; }

    enum class RootCursor {
        Normal, Delete, Down, Right, DownRight
    };

    void installCursor(RootCursor);
    void installCursorOnWindow(RootCursor, Window w);
    void installColormap(Colormap cmap);
    unsigned long allocateColour(const char *name, const char *fallback);

    // Focus
    void clearFocus();
    void considerFocusChange(Client *c, Window w, Time timestamp);
    void stopConsideringFocus();

    // Grab helpers
    int attemptGrab(Window, Window, int, int);
    void releaseGrab(XButtonEvent *e);

    // Exposure during grab
    void eventExposure(XExposeEvent *e);

    // Geometry display
    void showGeometry(int x, int y);
    void removeGeometry();

    // EWMH helpers (called from Client)
    void updateClientList();
    void updateActiveWindow(Window w);
    void updateWorkarea();

    bool raiseTransients(Client *c);

    void addToHiddenList(Client *);
    void removeFromHiddenList(Client *);
    void skipInRevert(Client *, Client *);

    Time timestamp(bool reset);

private:
    int loop();
    void release();
    void initialiseScreen();
    void scanInitialWindows();

    Config m_config;

    // RAII-managed X11 resources (D-05)
    // IMPORTANT: m_display declared first so it is destroyed last (D-05, Pitfall 2)
    x11::DisplayPtr m_display;

    int m_screenNumber;
    Window m_root;
    Colormap m_defaultColormap;

    // Cursors (RAII-managed)
    x11::UniqueCursor m_cursor;
    x11::UniqueCursor m_xCursor;
    x11::UniqueCursor m_vCursor;
    x11::UniqueCursor m_hCursor;
    x11::UniqueCursor m_vhCursor;

    // Client list (D-01: unique_ptr ownership)
    std::vector<std::unique_ptr<Client>> m_clients;
    std::vector<std::unique_ptr<Client>> m_hiddenClients;
    std::unordered_map<Window, Client*> m_windowMap;  // D-05: O(1) lookup
    Client *m_activeClient;

    int m_shapeEvent;
    int m_currentTime;

    bool m_looping;
    int m_returnCode;

    // Self-pipe for signal-safe poll() wakeup (D-01)
    FdGuard m_pipeRead{-1};
    FdGuard m_pipeWrite{-1};
    static int s_pipeWriteFd;  // accessed from signal handler (static for async-signal-safety)

    static bool m_initialising;
    static int errorHandler(Display*, XErrorEvent*);
    static void sigHandler(int);
    static volatile std::sig_atomic_t m_signalled;

    // Menu resources (RAII-managed)
    Window m_menuWindow;
    XftFont* m_menuFont;                  // raw pointer, freed in release()
    x11::XftDrawPtr m_menuDraw;           // XftDraw bound to m_menuWindow
    x11::XftColorWrap m_menuFgColor;      // "black" foreground
    x11::XftColorWrap m_menuBgColor;      // "gray80" background
    x11::XftColorWrap m_menuHlColor;      // "gray60" highlight (replaces XOR)
    unsigned long m_menuBorderPixel;      // for XCreateSimpleWindow border

    // EWMH WM check window (per EWMH spec, child of root)
    Window m_wmCheckWindow;

    static const char* const m_menuCreateLabel;
    const char* menuLabel(int);
    void menu(XButtonEvent *e);
    void spawn();
    void circulate(bool activeFirst);

    // EWMH setup (called internally)
    void setupEwmhProperties();

    // Focus tracking
    bool m_focusChanging;
    Client *m_focusCandidate;
    Window m_focusCandidateWindow;
    Time m_focusTimestamp;
    bool m_focusPointerMoved;
    bool m_focusPointerNowStill;

    // Timer deadlines for auto-raise focus tracking (D-02, D-03)
    std::chrono::steady_clock::time_point m_pointerStoppedDeadline{};
    std::chrono::steady_clock::time_point m_autoRaiseDeadline{};
    bool m_pointerStoppedDeadlineActive = false;
    bool m_autoRaiseDeadlineActive = false;

    // Compute poll() timeout from active timer deadlines
    int computePollTimeout() const;

    void checkDelaysForFocus();

    void nextEvent(XEvent *ev);

    // Event handlers
    void eventButton(XButtonEvent*);
    void eventMapRequest(XMapRequestEvent*);
    void eventConfigureRequest(XConfigureRequestEvent*);
    void eventUnmap(XUnmapEvent*);
    void eventCreate(XCreateWindowEvent*);
    void eventDestroy(XDestroyWindowEvent*);
    void eventClient(XClientMessageEvent*);
    void eventColormap(XColormapEvent*);
    void eventProperty(XPropertyEvent*);
    void eventEnter(XCrossingEvent*);
    void eventReparent(XReparentEvent*);
    void eventFocusIn(XFocusInEvent*);
};

// Atom definitions (from upstream General.h)
struct Atoms {
    static Atom wm_state;
    static Atom wm_changeState;
    static Atom wm_protocols;
    static Atom wm_delete;
    static Atom wm_takeFocus;
    static Atom wm_colormaps;
    static Atom wm2_running;

    // EWMH atoms
    static Atom net_supported;
    static Atom net_supportingWmCheck;
    static Atom net_clientList;
    static Atom net_activeWindow;
    static Atom net_wmWindowType;
    static Atom net_wmState;
    static Atom net_wmName;
    static Atom net_wmStateFullscreen;
    static Atom net_wmStateMaximizedVert;
    static Atom net_wmStateMaximizedHorz;
    static Atom net_wmStateHidden;
    static Atom net_wmWindowTypeDock;
    static Atom net_wmWindowTypeDialog;
    static Atom net_wmWindowTypeNotification;
    static Atom net_wmWindowTypeNormal;
    static Atom net_wmWindowTypeUtility;
    static Atom net_wmWindowTypeSplash;
    static Atom net_wmWindowTypeToolbar;
    static Atom net_wmStrut;
    static Atom net_wmStrutPartial;
    static Atom net_numberOfDesktops;
    static Atom net_currentDesktop;
    static Atom net_workarea;
    static Atom utf8_string;
};

extern bool ignoreBadWindowErrors;
