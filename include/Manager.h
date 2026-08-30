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
    bool hasRandrExtension() const { return m_randrEventBase >= 0; }

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

    // A shade of `name`: blended `fraction` of the way toward white when
    // positive, toward black when negative. Used for the 1 px bevel highlight
    // and shadow (plan 08.5-02).
    //
    // DERIVED RATHER THAN CONFIGURED, and that is the point. Hardcoded bevel
    // colours would be correct for exactly one palette -- set a dark tab
    // background and a fixed near-white highlight stops reading as a highlight
    // and starts reading as a defect. Deriving them means every palette the
    // user can configure gets bevels that belong to it, with no extra keys to
    // set and no way to set them inconsistently.
    unsigned long allocateShadeOf(const char *name, double fraction,
                                  const char *desc);

    // Focus
    void clearFocus();
    void considerFocusChange(Client *c, Window w, Time timestamp);
    void stopConsideringFocus();

    // FOCUS-01 (plan 08-08): the last-user-interaction clock, and the single
    // timestamp comparison both arbitration entry points share.
    //
    // The clock is fed ONLY from real button presses (see the recording site in
    // src/Buttons.cpp). It is deliberately NOT fed from pointer crossing events:
    // moving the pointer across a window is not the kind of interaction the
    // EWMH means by a user action, and feeding it from crossings would make
    // every pop-up that appears under the pointer look user-initiated, which
    // defeats the whole feature.
    Time lastUserInteraction() const { return m_lastUserInteraction; }
    void noteUserInteraction(Time t) { m_lastUserInteraction = t; }

    // True when `userTime` is at least as recent as the last user interaction.
    // Both arbitration sites (Client::shouldFocusOnMap() and the activation
    // client-message branch in src/Events.cpp) call THIS rather than comparing
    // timestamps themselves: X timestamps are 32-bit milliseconds and wrap
    // roughly every 49.7 days, so a bare `a < b` produces a total focus outage
    // once per wrap. Defined in src/Manager.cpp.
    bool isUserTimeRecent(Time userTime) const;

    // Grab helpers
    int attemptGrab(Window, Window, int, Time);
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

    // D-24/D-26: RANDR, following the same triple. Below zero means the server
    // has no RANDR extension (or WM2_FORCE_NO_RANDR forced it off), in which
    // case the WM still tracks resolution changes -- through the root window's
    // own ConfigureNotify, which is why StructureNotifyMask is in the root mask.
    // The negative sentinel matters more here than for Shape: the dispatch in
    // src/Events.cpp compares against this member PLUS the screen-change-notify
    // offset, and with a zero sentinel that sum would be a small non-negative
    // number that a real core event type could collide with.
    //
    // (The member name is spelled once below and nowhere in this comment: the
    // acceptance gates for this file are line-counting greps, and prose that
    // repeats the token defeats the guard it is describing.)
    int m_randrEventBase;

    // D-27: manager-owned screen-geometry cache. Seeded from Xlib in
    // initialiseScreen() before anything can publish or consume geometry, and
    // read only through the two public accessors declared above -- which are
    // the only readers of these members anywhere in the codebase. A later plan
    // in this phase refreshes them from a live root-geometry query after a
    // resolution change; nothing else may write them.
    int m_lastKnownScreenW;
    int m_lastKnownScreenH;

    int m_currentTime;

    // 08.5-06: cold-cache property-wait self-report for timestamp().
    //
    // src/Events.cpp invalidates m_currentTime on every loop iteration, so any
    // timestamp(false) reached by a handler that did not itself record an event
    // time falls into the cold-cache branch and takes a property wait on the
    // window manager's only thread. These four counters are what let a run in
    // which that branch was never entered be told apart from a run in which it
    // entered and blocked -- a distinction the loop() tail reports on exit, and
    // the reason a REFUTATION of the cold-cache hypothesis is checkable rather
    // than merely assertable.
    //
    // They are diagnostics only: nothing in the window manager reads them to
    // decide anything, and the bound that would use them belongs to 08.5-07.
    // Declaration order here is the order the constructor's initialiser list
    // must use, or -Wreorder fires.
    unsigned long m_timestampColdEntries;
    unsigned long m_timestampBlockedWaits;
    unsigned long m_timestampForeignMatches;
    long m_timestampLongestWaitMs;

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

    // Drag geometry readout (D-34). Its own window, not a second job for
    // m_menuWindow: the indicator and the menu map, resize and draw
    // independently, and sharing one XID meant a menu opened after a drag
    // inherited the indicator's geometry until its first Expose.
    Window m_geometryWindow;
    x11::XftDrawPtr m_geometryDraw;       // XftDraw bound to m_geometryWindow

    // EWMH WM check window (per EWMH spec, child of root)
    Window m_wmCheckWindow;

    static const char* const m_menuCreateLabel;
    // menu() owns the WHOLE root-menu interaction, submenu included: one grab,
    // one event loop, the submenu a state of that loop. openCategorySubmenu()
    // is deliberately gone rather than merely unused -- it was a second nested
    // loop with a second grab, and splitting one pointer interaction across two
    // of each is what made the outer menu unreachable once a submenu opened.
    void menu(XButtonEvent *e);
    void spawn();
    void spawnArgv(const std::vector<std::string>& argv);
    void launchApp(const AppEntry& entry);
    void circulate(bool activeFirst);

    // EWMH setup (called internally)
    void setupEwmhProperties();

    // FOCUS-01: when the user last actually did something. Initialised to the
    // X "current time" sentinel (CurrentTime == 0), which is older than any real
    // server timestamp -- so before the user has touched anything, every window
    // that publishes a user-time is granted focus. That is the intended
    // behaviour for a freshly started session, not an accident.
    Time m_lastUserInteraction = CurrentTime;

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

    // XDIS-01 / D-25: the single settling point for a resolution change, shared
    // by the RANDR notification and the root-ConfigureNotify fallback. Both
    // entry points live in src/Events.cpp; the definition is in src/Manager.cpp
    // beside the geometry cache it owns.
    void handleScreenGeometryChange();

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

    // FOCUS-01 (plan 08-08). The first three drive focus-stealing prevention:
    // a client publishes the timestamp of the user action that caused it to map
    // a window, optionally on a proxy window so it can update the value without
    // generating property events on the toplevel, and a refused window is marked
    // as demanding attention rather than being silently denied. The last two are
    // the skip-taskbar/skip-pager states that plan 08-10's rule actions need;
    // they are interned here so the _NET_SUPPORTED array is written once.
    //
    // Advertising these in _NET_SUPPORTED is not optional decoration. A
    // compliant client consults that array before deciding whether to set a
    // property, so omitting them means clients never write the values the
    // arbitration reads, and the feature silently degrades to always-grant.
    static Atom net_wmUserTime;
    static Atom net_wmUserTimeWindow;
    static Atom net_wmStateDemandsAttention;
    static Atom net_wmStateSkipTaskbar;
    static Atom net_wmStateSkipPager;
};

extern bool ignoreBadWindowErrors;
