#pragma once

#include "x11wrap.h"
#include "Config.h"
#include "AppEntry.h"
#include "SocketServer.h"
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
#include <poll.h>
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
    // argc/argv are retained for `reload` (DISC-07, plan 09-04): re-reading the
    // configuration means re-running the WHOLE layered load -- defaults, system
    // file, user file, command line -- and the command line is the top layer.
    // Merging the file onto current state instead would let a `--frame-thickness`
    // given at startup be silently overridden by the file on the first reload.
    // Defaulted so a caller with no command line to offer still compiles; then
    // the CLI layer is simply empty.
    WindowManager(const Config& config, const std::vector<AppEntry>& apps,
                  int argc = 0, char** argv = nullptr);
    ~WindowManager();

    // The EFFECTIVE configuration -- what the window manager is drawing with
    // right now, which after a socket `set` is not what any file on disk says.
    const Config& config() const { return m_config; }

    // DISC-07: the last configuration READ FROM DISK, as a snapshot taken at
    // startup and replaced by every successful `reload`. The difference between
    // this and config() above is exactly the set of changes made over the
    // socket and not saved, which is what makes the GUI's "revert" expressible
    // as "send the saved values back" rather than as a new message type.
    const Config& savedConfig() const { return m_savedConfig; }

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

    // The NON-FATAL forms of the two allocators below, and the reason a colour
    // arriving over the socket can be refused rather than ending the process
    // (plan 09-05, threat T-9-26).
    //
    // allocateColour() calls fatal() on a name the server cannot parse, which
    // is right at startup -- a window manager with no frame colour has nothing
    // to draw -- and catastrophic for a `set`, where the correct answer is an
    // error naming the key and a desktop that keeps the colour it had. These
    // return false instead, having changed nothing.
    //
    // The live path allocates EVERY new value through these BEFORE it releases
    // a single old one. That ordering is the whole of the safety property: a
    // failure after the old value was freed would leave the window manager
    // with no usable colour, which is exactly what the prohibition forbids.
    bool tryAllocateColour(const char *name, unsigned long &out) const;
    bool tryAllocateShadeOf(const char *name, double fraction,
                            unsigned long &out) const;

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

    // WINDOWS.md ledger 8. The modal grab loops -- the root menu, releaseGrab(),
    // move, resize, the tab button and the gesture recogniser -- used to wait in
    // XMaskEvent, or sleep 50 ms between XCheckMaskEvent sweeps, and neither
    // form watched the exit flag or the self-pipe: SIGTERM delivered while a
    // button was held was honoured only when the button came up. Every such
    // loop now waits here instead. Event: *out holds a matching event.
    // Timeout: timeoutMs elapsed (never returned for timeoutMs < 0).
    // Interrupted: the exit flag is set or the self-pipe is readable; the
    // caller leaves its loop with nothing chosen and the main loop's own
    // shutdownOnSignal() takes it from there -- the pipe is NOT drained here.
    enum class ModalWait { Event, Timeout, Interrupted };
    ModalWait modalWait(long mask, XEvent *out, int timeoutMs);
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

    // DISC-07's second half. Populated from the same Config the constructor was
    // handed, replaced wholesale by a successful reload, and never touched by a
    // `set` -- because a `set` writes no file (D-01), so the saved state does
    // not change when one arrives.
    Config m_savedConfig;

    // argv, copied rather than aliased. The pointers main() was handed do live
    // for the whole process, but a copy costs a few hundred bytes once and
    // removes the question entirely.
    std::vector<std::string> m_cliArgs;

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
    // The AUTO-DISCOVERED half, kept as it arrived (plan 09-05). m_apps below
    // is this list with the effective configuration's manual entries merged
    // onto it, and the merge is re-run whenever those entries change -- which
    // cannot be done from m_apps alone, because D-08's name-match rule
    // REPLACES an auto-discovered entry rather than shadowing it.
    std::vector<AppEntry> m_autoApps;

    std::vector<AppEntry> m_apps;
    std::vector<std::pair<std::string, std::vector<AppEntry>>> m_appCategories;
    void buildAppCategories();

    // Re-run the startup merge and regroup, from m_autoApps and the effective
    // configuration's manual entries. The SAME merge the startup path uses --
    // AppCache::mergeEntries() -- rather than a second one, so a manual entry
    // set over the socket lands exactly where the identical line in a config
    // file would put it.
    void rebuildAppCategoriesFromConfig();

    // The categories the NEXT root menu will show, ';'-separated in the menu's
    // own order, for the read-only `menu-categories` key the settings window's
    // dropdown reads (plan 09-07, D-12).
    //
    // Computed from m_autoApps plus the EFFECTIVE configuration's manual
    // entries rather than read out of m_appCategories, because those two differ
    // for exactly as long as a menu is held open across a change: m_appCategories
    // is deliberately not rebuilt under a modal loop that holds a pointer into
    // it. "What the next menu will show" is the question the dropdown is
    // asking, and it is the one this answers.
    std::string menuCategoriesValue() const;

    // A root menu is open right now, and its modal loop is holding a pointer
    // INTO m_appCategories (the submenu's entry vector). Rebuilding that list
    // underneath it is the defect class plans 08-13 and 08-14 already fixed
    // once, so applyConfig() defers instead: it sets the flag below and menu()
    // rebuilds before it assembles the NEXT menu. A menu already open is left
    // undisturbed, which is also what D-08 asks for.
    bool m_menuOpen = false;
    bool m_appCategoriesStale = false;

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

    Time m_currentTime;

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

    // 08.5-12: expiry of the bounded property wait, recorded SEPARATELY from
    // having merely entered a wait. Added beside the four above and never in
    // place of any of them -- their names and stderr spellings are read by two
    // committed measurement records. Declared last so the constructor's
    // initialiser list keeps declaration order and -Wreorder stays quiet.
    unsigned long m_timestampWaitTimeouts;

    bool m_looping;
    int m_returnCode;

    // Self-pipe for signal-safe poll() wakeup (D-01)
    FdGuard m_pipeRead{-1};
    FdGuard m_pipeWrite{-1};
    static int s_pipeWriteFd;  // accessed from signal handler (static for async-signal-safety)

    // CGUI-02: the configuration socket, and the ONE descriptor set both poll
    // sites consume.
    //
    // RESEARCH Pitfall 1 is the whole reason these live here rather than as two
    // stack-local arrays. Before this phase src/Events.cpp declared
    // `struct pollfd fds[2]` TWICE -- once in nextEvent() and once in
    // modalWait() -- and every modal grab in this codebase (the root menu, move,
    // resize, the tab-button hold, the gesture recogniser) funnels through the
    // second one. A socket added to only the first produces a window manager
    // that answers when idle and appears to freeze the moment a menu is held:
    // exactly the defect class ledger 8 already fixed once for signal delivery.
    // The two arrays are now one builder called from both, and the indices are
    // named so neither site can drift from the other by a literal.
    static constexpr std::size_t kPollFdX          = 0;  // the X connection
    static constexpr std::size_t kPollFdPipe       = 1;  // the self-pipe read end
    static constexpr std::size_t kPollFdFixedCount = 2;  // socket fds start here

    ConfigSocketServer m_socketServer;

    // When this process started, for the status reply's uptime. Steady rather
    // than wall-clock, so a clock adjustment cannot make uptime run backwards.
    std::chrono::steady_clock::time_point m_startTime{};

    // The shared poll set: X connection at kPollFdX, self-pipe at kPollFdPipe,
    // the socket server's own descriptors appended after kPollFdFixedCount.
    // Rebuilt each iteration because the connection population changes.
    std::vector<struct pollfd> buildPollSet() const;

    // Clamp a poll timeout so the socket server's silence deadline can fire.
    // A deadline that expires on the passage of time alone is never reached by
    // a poll() that blocks forever.
    int clampPollTimeoutForSocket(int base) const;

    // Service whatever the socket server put in `fds`. In modalWait() this is a
    // FOURTH, SILENT case (DISC-06): it never returns Event and never returns
    // Interrupted, exactly as ServiceFocusTick is silent in nextEvent(), so no
    // existing caller of modalWait() observes any change.
    void serviceConfigSocket(const std::vector<struct pollfd>& fds);

    // The protocol policy. Owns D-15's handshake rule and D-14's field ceiling;
    // the transport in src/SocketServer.cpp owns descriptors and buffers and
    // knows nothing about what a message means.
    ConfigSocketReply handleConfigRequest(const ConfigSocketRequest& request);

    // D-14's ceiling, assembled in exactly one place so there is exactly one
    // site to review.
    ConfigMessage statusReplyMessage() const;

    // DISC-06a -- THE ONE FUNNEL THROUGH WHICH A CONFIGURATION CHANGE REACHES
    // RUNNING STATE.
    //
    // Diffs `next` against m_config field by field and re-applies only what
    // actually changed, then stores `next` whole. Every later plan in this
    // phase adds a branch HERE rather than a second application path, so
    // "what is live?" has exactly one answer and exactly one place to read it
    // -- and so the idempotency guarantee (applying the same value twice does
    // no second piece of work) holds for every setting by construction rather
    // than one setting at a time.
    // Returns false with a human-readable reason in `reasonOut`, HAVING
    // CHANGED NOTHING -- not even m_config. Plan 09-04's form returned void
    // because the one setting it applied could not fail; a colour can (the
    // config parser takes a colour verbatim and the X server is what refuses
    // it), so validation now happens before the store rather than after it.
    bool applyConfig(const Config& next, std::string& reasonOut);

    // The menu half of the palette reload, beside applyConfig() because that is
    // its only caller. Allocate-then-swap, like Border::reloadColours(); false
    // with the offending key in `keyOut`, having changed nothing.
    bool reloadMenuColours(const Config& next, std::string& keyOut);

    // The menu's half of the font reload. Load-then-close, like
    // Border::reloadTabFont(); false leaves the previous face in place. There
    // is deliberately no re-layout counterpart: menu() rebuilds and re-measures
    // the whole popup on every opening.
    bool reloadMenuFont(const std::string& pattern);

    // Serve one `set`. Validates key and value BEFORE the parser sees them --
    // see include/Config.h for why -- then applies through the very same
    // Config::applyKeyValue() the config file goes through, on a COPY, and
    // hands the result to applyConfig(). Returns false with a human-readable
    // reason in `reasonOut`, having changed nothing at all.
    bool applyConfigSet(const std::string& key, const std::string& value,
                        std::string& reasonOut);

    // Serve one `reload`: re-run the layered load, replace the saved snapshot,
    // apply the result. False with a reason on a read failure, having changed
    // nothing.
    bool reloadConfigFromDisk(std::string& reasonOut);

    // Start the socket and publish its path on the root window (DISC-03).
    void startConfigSocket();

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

    // Shutdown taken because the exit flag or the self-pipe fired. Drains the
    // pipe, reports, and stops the loop with status 0. Called from the three
    // points in nextEvent() that can observe the flag. Never called from the
    // signal handler, which must stay a flag write and a pipe write.
    void shutdownOnSignal();

    // Wakes a blocked poll() the same way a signal does, by writing the
    // self-pipe. The root menu's Exit action uses this so that choosing Exit
    // does not have to wait for the next X event to be noticed.
    void wakeEventLoop();

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

    // DISC-03 / DISC-01: the socket's filesystem path, published on the root
    // window as an XA_STRING so a client DISCOVERS it rather than
    // reconstructing it. A window manager started with an unusual
    // XDG_RUNTIME_DIR is still findable.
    static Atom wm2_configSocket;

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
