#pragma once

#include "x11wrap.h"
#include "Config.h"
#include "AppEntry.h"
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/shape.h>
#include <vector>
#include <string>
#include <utility>
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
    WindowManager(const Config& config, const std::vector<AppEntry>& apps);
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

    // D-27: the single source of truth for screen geometry. Defined in
    // src/Manager.cpp, where the comment beside the definitions explains why no
    // other translation unit may read Xlib's cached screen dimensions directly.
    // Placed beside the capability predicate above deliberately: both are
    // funnels, and the phase's later work hangs off each of them.
    int screenWidth() const;
    int screenHeight() const;

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

    // Application discovery (Phase 7): merged AppEntry list from Desktop/BinaryScan/Manual
    // sources, and the same entries grouped into category buckets (alphabetical,
    // "Custom" always last) ready for menu rendering.
    std::vector<AppEntry> m_apps;
    std::vector<std::pair<std::string, std::vector<AppEntry>>> m_appCategories;
    void buildAppCategories();

    // Capability sentinel convention (Phase 8). Every optional X extension this
    // WM depends on is represented by the SAME triple:
    //
    //   1. an int member holding the extension's event base, set once in the
    //      constructor, where any value BELOW ZERO means "capability absent";
    //   2. a has*Extension() predicate over that member (see above), which is
    //      the only thing the rest of the code is allowed to ask; and
    //   3. exactly one call funnel per extension that returns without touching
    //      the X connection when the predicate is false.
    //
    // The negative sentinel is load-bearing beyond the predicate: the default
    // branch of the event dispatch in src/Events.cpp compares the incoming
    // event type against these members, and a real event type is never
    // negative, so a forced-off capability can never be matched by accident.
    //
    // Later plans in this phase add further capability members alongside this
    // one; they follow the same triple rather than inventing a new shape.
    int m_shapeEvent;

    // D-27: manager-owned screen-geometry cache. Seeded from Xlib in
    // initialiseScreen() before anything can publish or consume geometry, and
    // read only through the two public accessors declared above -- which are
    // the only readers of these members anywhere in the codebase. A later plan
    // in this phase refreshes them from a live root-geometry query after a
    // resolution change; nothing else may write them.
    int m_lastKnownScreenW;
    int m_lastKnownScreenH;

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

    // Submenu popup window (Phase 7): app-category flyout, reuses the main
    // menu's font/colors (m_menuFont/m_menuFgColor/m_menuBgColor/m_menuHlColor).
    Window m_submenuWindow;
    x11::XftDrawPtr m_submenuDraw;        // XftDraw bound to m_submenuWindow

    // EWMH WM check window (per EWMH spec, child of root)
    Window m_wmCheckWindow;

    static const char* const m_menuCreateLabel;
    void menu(XButtonEvent *e);
    void openCategorySubmenu(const std::pair<std::string, std::vector<AppEntry>>& category,
                              XButtonEvent* e, int outerX, int outerY,
                              int outerMaxWidth, int rowIndex);
    void spawn();
    void spawnArgv(const std::vector<std::string>& argv);
    void launchApp(const AppEntry& entry);
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
