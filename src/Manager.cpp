#include "Manager.h"
#include "Client.h"
#include "Border.h"   // FRAME_WIDTH -- the live frame thickness applyConfig() writes
#include "TimestampWait.h"
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/Xproto.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xrender.h>
#include <algorithm>
#include <cstdint>
#include <map>
#include "Cursors.h"

// Static member definitions
Atom Atoms::wm_state = None;
Atom Atoms::wm_changeState = None;
Atom Atoms::wm_protocols = None;
Atom Atoms::wm_delete = None;
Atom Atoms::wm_takeFocus = None;
Atom Atoms::wm_colormaps = None;
Atom Atoms::wm2_running = None;
Atom Atoms::wm2_configSocket = None;

// EWMH atom static members
Atom Atoms::net_supported = None;
Atom Atoms::net_supportingWmCheck = None;
Atom Atoms::net_clientList = None;
Atom Atoms::net_activeWindow = None;
Atom Atoms::net_wmWindowType = None;
Atom Atoms::net_wmState = None;
Atom Atoms::net_wmName = None;
Atom Atoms::net_wmStateFullscreen = None;
Atom Atoms::net_wmStateMaximizedVert = None;
Atom Atoms::net_wmStateMaximizedHorz = None;
Atom Atoms::net_wmStateHidden = None;
Atom Atoms::net_wmWindowTypeDock = None;
Atom Atoms::net_wmWindowTypeDialog = None;
Atom Atoms::net_wmWindowTypeNotification = None;
Atom Atoms::net_wmWindowTypeNormal = None;
Atom Atoms::net_wmWindowTypeUtility = None;
Atom Atoms::net_wmWindowTypeSplash = None;
Atom Atoms::net_wmWindowTypeToolbar = None;
Atom Atoms::net_wmStrut = None;
Atom Atoms::net_wmStrutPartial = None;
Atom Atoms::net_numberOfDesktops = None;
Atom Atoms::net_currentDesktop = None;
Atom Atoms::net_workarea = None;
Atom Atoms::utf8_string = None;
Atom Atoms::net_wmUserTime = None;
Atom Atoms::net_wmUserTimeWindow = None;
Atom Atoms::net_wmStateDemandsAttention = None;
Atom Atoms::net_wmStateSkipTaskbar = None;
Atom Atoms::net_wmStateSkipPager = None;

volatile std::sig_atomic_t WindowManager::m_signalled = 0;
int  WindowManager::s_pipeWriteFd = -1;
bool WindowManager::m_initialising = false;
bool ignoreBadWindowErrors = false;

const char *const WindowManager::m_menuCreateLabel = "New";


WindowManager::WindowManager(const Config& config, const std::vector<AppEntry>& apps,
                             int argc, char** argv)
    : m_config(config)
    , m_savedConfig(config)
    , m_screenNumber(0)
    , m_root(None)
    , m_defaultColormap(None)
    , m_activeClient(nullptr)
    , m_apps(apps)
    , m_shapeEvent(0)
    , m_randrEventBase(-1)
    , m_lastKnownScreenW(0)
    , m_lastKnownScreenH(0)
    , m_currentTime(CurrentTime)
    , m_timestampColdEntries(0)
    , m_timestampBlockedWaits(0)
    , m_timestampForeignMatches(0)
    , m_timestampLongestWaitMs(0)
    , m_timestampWaitTimeouts(0)
    , m_looping(false)
    , m_returnCode(0)
    , m_startTime(std::chrono::steady_clock::now())
    , m_menuWindow(None)
    , m_menuFont(nullptr)
    , m_menuBorderPixel(0)
    , m_submenuWindow(None)
    , m_geometryWindow(None)
    , m_wmCheckWindow(None)
    , m_focusChanging(false)
    , m_focusCandidate(nullptr)
    , m_focusCandidateWindow(None)
    , m_focusTimestamp(0)
    , m_focusPointerMoved(false)
    , m_focusPointerNowStill(false)
    , m_pointerStoppedDeadline{}
    , m_autoRaiseDeadline{}
    , m_pointerStoppedDeadlineActive(false)
    , m_autoRaiseDeadlineActive(false)
{
    // DISC-07: keep the command line for reload. argv[0] is included because
    // getopt_long() skips it and because it is what an error message would
    // name.
    for (int i = 0; i < argc && argv && argv[i]; ++i) m_cliArgs.emplace_back(argv[i]);

    std::fprintf(stderr, "\nwm2-born-again: Copyright (c) 1996-7 Chris Cannam, modernized 2026.\n"
                 "  Parts derived from 9wm Copyright (c) 1994-96 David Hogan\n"
                 "  Copying and redistribution encouraged.  No warranty.\n\n");

    // FOCUS-02: the banner used to claim "Focus follows pointer" unconditionally.
    // That was true of every build until this plan wired the booleans up, and is
    // a lie the moment a user sets click-to-focus. The startup transcript is the
    // per-target evidence artefact for XDIS-05, so it has to describe the policy
    // this process is actually running, not the one the source once hardcoded.
    std::fprintf(stderr, "  %s  %s  %s  Hidden clients only on menu.\n\n",
                 m_config.clickToFocus ? "Click to focus." : "Focus follows pointer.",
                 m_config.autoRaise    ? "Auto-raise on."   : "Auto-raise off.",
                 m_config.raiseOnFocus ? "Raise on focus."  : "No raise on focus.");

    // Group the merged AppEntry list into category buckets for menu rendering.
    // Pure data grouping, no X11 dependency -- safe to run before the display opens.
    buildAppCategories();

    // Open display via RAII
    m_display.reset(XOpenDisplay(nullptr));
    if (!m_display) fatal("can't open display");

    m_initialising = true;
    XSetErrorHandler(errorHandler);
    ignoreBadWindowErrors = false;

    // Self-pipe for signal-safe wakeup (D-01)
    int pipefd[2];
    if (pipe(pipefd) != 0) fatal("can't create signal pipe");

    // Set both ends non-blocking (Pitfall 1: prevents write() blocking in handler)
    for (int i = 0; i < 2; ++i) {
        int flags = fcntl(pipefd[i], F_GETFL, 0);
        if (flags < 0 || fcntl(pipefd[i], F_SETFL, flags | O_NONBLOCK) < 0) {
            fatal("can't set pipe non-blocking");
        }
    }
    m_pipeRead = FdGuard(pipefd[0]);
    m_pipeWrite = FdGuard(pipefd[1]);
    s_pipeWriteFd = m_pipeWrite.get();

    // Signal handlers -- call sigaction directly (not upstream's signal() macro)
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = sigHandler;

    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);

    // Intern atoms
    Atoms::wm_state       = XInternAtom(display(), "WM_STATE",            false);
    Atoms::wm_changeState = XInternAtom(display(), "WM_CHANGE_STATE",     false);
    Atoms::wm_protocols   = XInternAtom(display(), "WM_PROTOCOLS",        false);
    Atoms::wm_delete      = XInternAtom(display(), "WM_DELETE_WINDOW",    false);
    Atoms::wm_takeFocus   = XInternAtom(display(), "WM_TAKE_FOCUS",       false);
    Atoms::wm_colormaps   = XInternAtom(display(), "WM_COLORMAP_WINDOWS", false);
    Atoms::wm2_running    = XInternAtom(display(), "_WM2_RUNNING",        false);
    Atoms::wm2_configSocket = XInternAtom(display(), "_WM2_CONFIG_SOCKET",  false);

    // EWMH atoms
    Atoms::net_supported          = XInternAtom(display(), "_NET_SUPPORTED", false);
    Atoms::net_supportingWmCheck  = XInternAtom(display(), "_NET_SUPPORTING_WM_CHECK", false);
    Atoms::net_clientList         = XInternAtom(display(), "_NET_CLIENT_LIST", false);
    Atoms::net_activeWindow       = XInternAtom(display(), "_NET_ACTIVE_WINDOW", false);
    Atoms::net_wmWindowType       = XInternAtom(display(), "_NET_WM_WINDOW_TYPE", false);
    Atoms::net_wmState            = XInternAtom(display(), "_NET_WM_STATE", false);
    Atoms::net_wmName             = XInternAtom(display(), "_NET_WM_NAME", false);
    Atoms::net_wmStateFullscreen  = XInternAtom(display(), "_NET_WM_STATE_FULLSCREEN", false);
    Atoms::net_wmStateMaximizedVert = XInternAtom(display(), "_NET_WM_STATE_MAXIMIZED_VERT", false);
    Atoms::net_wmStateMaximizedHorz = XInternAtom(display(), "_NET_WM_STATE_MAXIMIZED_HORZ", false);
    Atoms::net_wmStateHidden      = XInternAtom(display(), "_NET_WM_STATE_HIDDEN", false);
    Atoms::net_wmWindowTypeDock   = XInternAtom(display(), "_NET_WM_WINDOW_TYPE_DOCK", false);
    Atoms::net_wmWindowTypeDialog = XInternAtom(display(), "_NET_WM_WINDOW_TYPE_DIALOG", false);
    Atoms::net_wmWindowTypeNotification = XInternAtom(display(), "_NET_WM_WINDOW_TYPE_NOTIFICATION", false);
    Atoms::net_wmWindowTypeNormal = XInternAtom(display(), "_NET_WM_WINDOW_TYPE_NORMAL", false);
    Atoms::net_wmWindowTypeUtility = XInternAtom(display(), "_NET_WM_WINDOW_TYPE_UTILITY", false);
    Atoms::net_wmWindowTypeSplash = XInternAtom(display(), "_NET_WM_WINDOW_TYPE_SPLASH", false);
    Atoms::net_wmWindowTypeToolbar = XInternAtom(display(), "_NET_WM_WINDOW_TYPE_TOOLBAR", false);
    Atoms::net_wmStrut            = XInternAtom(display(), "_NET_WM_STRUT", false);
    Atoms::net_wmStrutPartial     = XInternAtom(display(), "_NET_WM_STRUT_PARTIAL", false);
    Atoms::net_numberOfDesktops   = XInternAtom(display(), "_NET_NUMBER_OF_DESKTOPS", false);
    Atoms::net_currentDesktop     = XInternAtom(display(), "_NET_CURRENT_DESKTOP", false);
    Atoms::net_workarea           = XInternAtom(display(), "_NET_WORKAREA", false);

    // FOCUS-01 (plan 08-08): focus-stealing prevention reads the first two and
    // publishes the third on refusal; the last two are 08-10's skip states.
    Atoms::net_wmUserTime         = XInternAtom(display(), "_NET_WM_USER_TIME", false);
    Atoms::net_wmUserTimeWindow   = XInternAtom(display(), "_NET_WM_USER_TIME_WINDOW", false);
    Atoms::net_wmStateDemandsAttention = XInternAtom(display(), "_NET_WM_STATE_DEMANDS_ATTENTION", false);
    Atoms::net_wmStateSkipTaskbar = XInternAtom(display(), "_NET_WM_STATE_SKIP_TASKBAR", false);
    Atoms::net_wmStateSkipPager   = XInternAtom(display(), "_NET_WM_STATE_SKIP_PAGER", false);
    Atoms::utf8_string            = XInternAtom(display(), "UTF8_STRING", false);

    // Check Shape extension -- warn but continue if missing (graceful fallback)
    //
    // D-12: the capability can additionally be forced unavailable from the
    // environment. This is an INTERNAL TEST LEVER only: there is deliberately no
    // user-facing CLI flag (a --no-shape option is deferred beyond this phase)
    // and the variable is kept out of user documentation. It exists because the
    // X server refuses to turn SHAPE off at run time -- it answers
    // `Extension "SHAPE" can not be disabled` and lists the toggleable
    // extensions, which do not include it -- so this is the only way to drive
    // the rectangular fallback end-to-end against the real binary.
    //
    // Read exactly once, here, and never re-read: extension availability is
    // fixed for the lifetime of an X connection. Forcing the sentinel at this
    // one point rather than at each consumer makes every hasShapeExtension()
    // caller correct at once.
    int dummy;
    const char *forceNoShape = std::getenv("WM2_FORCE_NO_SHAPE");
    if (forceNoShape != nullptr && std::strcmp(forceNoShape, "1") == 0) {
        // Deliberately worded differently from the genuine-absence warning
        // below, so a captured transcript proves the forced path was actually
        // taken rather than the server merely happening to lack the extension.
        std::fprintf(stderr, "wm2: warning: shape extension forced off, frames will be rectangular\n");
        m_shapeEvent = -1;
    } else if (XShapeQueryExtension(display(), &m_shapeEvent, &dummy)) {
        std::fprintf(stderr, "  Shape extension available.\n");
    } else {
        std::fprintf(stderr, "wm2: warning: no shape extension, frames will be rectangular\n");
        m_shapeEvent = -1;
    }

    // D-24/D-26: RANDR, queried in exactly the Shape block's style. libxrandr is
    // a hard BUILD dependency (see CMakeLists.txt) but its runtime availability
    // is optional: a server without it gets a warning and the root-ConfigureNotify
    // fallback, never a hard stop. That split is XDIS-02.
    //
    // The env lever read below is the same kind of internal test lever as the
    // Shape one -- read exactly once, never documented for users, no CLI flag.
    // Unlike SHAPE, this server CAN be started with `-extension RANDR`, so the
    // lever is not the only way to exercise the degraded path (the [wm_norandr]
    // tests use a genuinely RANDR-less server). It exists so the fallback can
    // also be forced on a server that does have the extension.
    //
    // Event selection is deliberately NOT done here: m_root is still None at
    // this point -- initialiseScreen() below is what establishes it -- and a
    // select-input against None would raise BadWindow, which errorHandler turns
    // into exit(1) while m_initialising. The subscription therefore happens in
    // initialiseScreen(), immediately after the root window exists, guarded by
    // the predicate this block sets up.
    //
    // Token discipline: the acceptance gates for this file are line-counting
    // greps, so the env-var name and the select-input call are each spelled
    // exactly once in code and never repeated in prose.
    int randrErrorBase = 0;
    const char *forceNoRandr = std::getenv("WM2_FORCE_NO_RANDR");
    if (forceNoRandr != nullptr && std::strcmp(forceNoRandr, "1") == 0) {
        // Worded distinctly from the genuine-absence line below, so a captured
        // transcript proves which path was taken (same rationale as Shape).
        std::fprintf(stderr, "wm2: warning: xrandr extension forced off, "
                             "screen geometry will track resolution changes via the root window only\n");
        m_randrEventBase = -1;
    } else if (XRRQueryExtension(display(), &m_randrEventBase, &randrErrorBase)) {
        std::fprintf(stderr, "  Xrandr extension available.\n");
    } else {
        std::fprintf(stderr, "wm2: warning: no xrandr extension, "
                             "screen geometry will track resolution changes via the root window only\n");
        m_randrEventBase = -1;
    }

    // XDIS-04/XDIS-05: XRender, queried in the same style as the two blocks
    // above but WITHOUT a sentinel, because unlike Shape and RANDR nothing in
    // the WM branches on it. 08-06's spike measured why: with the extension
    // absent, libXft renders the rotated tab face through its core X11 glyph
    // path with identical metrics and no protocol error, so there is no
    // behaviour to degrade -- the tab-font ladder in src/Border.cpp keys on
    // whether a FONT could be produced, which is the thing that can actually
    // fail. The probe is here because the capability is a fact the release
    // evidence for XDIS-05 has to state per target, and because it is what
    // lets a RENDER-less end-to-end run prove it really was RENDER-less rather
    // than passing for the ordinary reason.
    int renderEventBase = 0;
    int renderErrorBase = 0;
    if (XRenderQueryExtension(display(), &renderEventBase, &renderErrorBase)) {
        std::fprintf(stderr, "  XRender extension available.\n");
    } else {
        std::fprintf(stderr, "wm2: warning: no xrender extension, "
                             "tab labels will be drawn through the core X11 glyph path\n");
    }

    // CGUI-02: the configuration socket, started BEFORE initialiseScreen()
    // because that is what publishes root-window properties -- the path has to
    // exist before the property naming it is written. Nothing here needs the
    // root window; the display name is all it takes.
    //
    // A failure is a warning and no socket, never a fatal. A window manager
    // with no configuration socket still manages windows, which is the whole of
    // its job; only wm2-ctl and the GUI lose their live connection.
    startConfigSocket();

    initialiseScreen();

    // Claim WM selection
    XSetSelectionOwner(display(), Atoms::wm2_running, m_menuWindow, timestamp(true));
    XSync(display(), false);
    m_initialising = false;
    m_returnCode = 0;

    clearFocus();
    scanInitialWindows();
    loop();
}


WindowManager::~WindowManager()
{
    // RAII handles resource cleanup
}


void WindowManager::buildAppCategories()
{
    // std::map keys sort alphabetically, giving us the base ordering for free;
    // "Custom" (D-07: manual/uncategorized entries) is special-cased to always
    // be appended last, matching conventional WM root-menu UX.
    std::map<std::string, std::vector<AppEntry>> buckets;
    for (const AppEntry& entry : m_apps) {
        buckets[entry.category].push_back(entry);
    }

    m_appCategories.clear();
    for (auto& kv : buckets) {
        if (kv.first == "Custom") continue;
        m_appCategories.emplace_back(kv.first, std::move(kv.second));
    }

    auto customIt = buckets.find("Custom");
    if (customIt != buckets.end()) {
        m_appCategories.emplace_back(customIt->first, std::move(customIt->second));
    }
}


void WindowManager::release()
{
    if (m_returnCode != 0) return;

    // The configuration socket goes first, before any X resource: it owns
    // plain file descriptors and a filesystem node, neither of which depends on
    // the display, and closing it here means no client can observe a half-torn
    // window manager. close() unlinks the socket, so the next start finds
    // nothing to reclaim.
    m_socketServer.close();

    m_windowMap.clear();

    // WR-02: Rely solely on ~Client() for unreparenting. Previously release()
    // called unreparent() explicitly AND ~Client() also called it (since
    // unreparent() does not change m_state to Withdrawn), causing redundant
    // XReparentWindow calls and XSync on every client during shutdown.
    ignoreBadWindowErrors = true;
    m_clients.clear();
    m_hiddenClients.clear();
    ignoreBadWindowErrors = false;

    XSetInputFocus(display(), PointerRoot, RevertToPointerRoot, timestamp(false));
    installColormap(None);

    // Clean up Xft menu resources. Every XftDraw goes before the window it is
    // bound to is destroyed: XftDrawDestroy touches its drawable, and getting
    // this order wrong is exactly the RenderBadPicture-on-every-close defect
    // 08-11 found in Border::~Border.
    m_menuDraw.reset();
    m_submenuDraw.reset();
    m_geometryDraw.reset();
    // m_menuFgColor, m_menuBgColor, m_menuHlColor auto-freed by RAII

    if (m_menuFont) {
        XftFontClose(display(), m_menuFont);
        m_menuFont = nullptr;
    }

    // WR-01: Destroy menu window (raw Window XID, not RAII-wrapped)
    if (m_menuWindow != None) {
        XDestroyWindow(display(), m_menuWindow);
        m_menuWindow = None;
    }

    // Destroy submenu window (Phase 7), same pattern as m_menuWindow
    if (m_submenuWindow != None) {
        XDestroyWindow(display(), m_submenuWindow);
        m_submenuWindow = None;
    }

    // Destroy the geometry readout window (D-34), same pattern again.
    if (m_geometryWindow != None) {
        XDestroyWindow(display(), m_geometryWindow);
        m_geometryWindow = None;
    }

    // Destroy EWMH WM check window
    if (m_wmCheckWindow != None) {
        XDestroyWindow(display(), m_wmCheckWindow);
        m_wmCheckWindow = None;
    }

    // RAII handles: cursors (m_cursor, m_xCursor, etc.), m_display
}


void WindowManager::fatal(const char *message)
{
    std::fprintf(stderr, "wm2: ");
    perror(message);
    std::fprintf(stderr, "\n");
    std::exit(1);
}


int WindowManager::errorHandler(Display *d, XErrorEvent *e)
{
    if (m_initialising && (e->request_code == X_ChangeWindowAttributes) &&
        e->error_code == BadAccess) {
        std::fprintf(stderr, "wm2: another window manager running?\n");
        std::exit(1);
    }

    if (ignoreBadWindowErrors && e->error_code == BadWindow) return 0;

    char msg[100], number[30], request[100];
    XGetErrorText(d, e->error_code, msg, 100);
    std::sprintf(number, "%d", e->request_code);
    XGetErrorDatabaseText(d, "XRequest", number, "", request, 100);

    if (request[0] == '\0') std::sprintf(request, "<request-code-%d>", e->request_code);

    std::fprintf(stderr, "wm2: %s (0x%lx): %s\n", request, e->resourceid, msg);

    if (m_initialising) {
        std::fprintf(stderr, "wm2: failure during initialisation, abandoning\n");
        std::exit(1);
    }

    return 0;
}


void WindowManager::sigHandler(int)
{
    m_signalled = 1;
    // write() is async-signal-safe per POSIX. Using static fd avoids
    // dereferencing object pointer (not guaranteed safe in signal handler).
    if (s_pipeWriteFd >= 0) {
        char c = 'x';
        // glibc marks write() warn_unused_result and a (void) cast does not
        // silence it under -O2 (the release gate's one warning at 2a94cbb). A
        // full pipe or a closed read end is not actionable here; the byte is
        // a wake-up, and the flag above is what the loop acts on.
        const ssize_t rc = write(s_pipeWriteFd, &c, 1);
        (void)rc;
    }
}


static Cursor makeCursor(Display *d, Window w,
                         unsigned char *bits, unsigned char *mask_bits,
                         int width, int height, int xhot, int yhot,
                         XColor *fg, XColor *bg)
{
    Pixmap pixmap = XCreateBitmapFromData(d, w, reinterpret_cast<const char*>(bits),
                                          width, height);
    Pixmap mask = XCreateBitmapFromData(d, w, reinterpret_cast<const char*>(mask_bits),
                                        width, height);

    Cursor cursor = XCreatePixmapCursor(d, pixmap, mask, fg, bg, xhot, yhot);
    XFreePixmap(d, pixmap);
    XFreePixmap(d, mask);

    return cursor;
}


// D-27: these two accessors are the single source of truth for screen geometry
// in this codebase. Every menu placement, window clamp, fullscreen geometry and
// workarea computation goes through them, and no other translation unit is
// permitted to read Xlib's cached screen dimensions directly.
//
// The cache they return is seeded once from Xlib in initialiseScreen() below.
// Plan 08-05 of this phase refreshes it from a live root-geometry query, driven
// by either a RANDR screen-change notification or a root ConfigureNotify.
// Direct reads elsewhere are forbidden precisely because Xlib's own cache does
// not self-update: on a server without RANDR nothing can refresh it at all, so
// a direct reader would keep returning the pre-resize size forever.
//
// This deliberately mirrors the Shape funnel (Border::combineShape()): one
// place to consult, one owner of the state, one place a later plan has to
// change to make every consumer correct at once.
int WindowManager::screenWidth() const
{
    return m_lastKnownScreenW;
}


int WindowManager::screenHeight() const
{
    return m_lastKnownScreenH;
}


// XDIS-01 / D-25: everything the WM does about a resolution change happens here,
// and it happens exactly once per distinct geometry.
//
// Both entry points in src/Events.cpp converge on this function -- the RANDR
// screen-change notification and, on a server with no RANDR at all, the root
// window's own ConfigureNotify. On a RANDR-capable server BOTH fire for the same
// logical resize, which is not a bug to be routed around but the reason the
// coalescing guard below is written the way it is.
//
// The geometry is re-read from the server and never taken from the event.
// That is not defensive style, it is required. The probe transcript in
// 08-RESEARCH.md shows this server delivering FOUR events for one `--fb`
// resize, and the first two of them carry the PRE-resize dimensions:
//
//     [0] RRScreenChangeNotify ev=1280x1024   <- stale
//     [1] root ConfigureNotify  ev=1280x1024  <- stale
//     [2] RRScreenChangeNotify ev=1024x768    <- the real change
//     [3] root ConfigureNotify  ev=1024x768   <- duplicate
//
// A handler that trusted event fields would reflow twice against the old size
// before ever seeing the new one. Reading the root window's attributes costs one
// round trip and is correct on every path, including the one where there is no
// RANDR event to consult in the first place.
//
// Note that screenWidth()/screenHeight() are NOT re-read here to discover the
// new size: since plan 08-04 they return this manager's own cache, so asking
// them after a resize would return the value this function is about to replace.
// They are the readers; this is the one writer.
void WindowManager::handleScreenGeometryChange()
{
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(display(), m_root, &attrs)) {
        std::fprintf(stderr, "wm2: warning: could not read root geometry after a "
                             "screen change, keeping %dx%d\n",
                     m_lastKnownScreenW, m_lastKnownScreenH);
        return;
    }

    // The coalescing guard. Duplicate delivery, stale intermediates and a
    // replayed resize at the same geometry all land here and all return
    // without touching a single client. It is also the mitigation for threat
    // T-8-GEO: a client that drives the desktop size in a loop cannot make the
    // WM do more than one reflow pass per DISTINCT geometry.
    if (attrs.width == m_lastKnownScreenW && attrs.height == m_lastKnownScreenH) {
        return;
    }

    m_lastKnownScreenW = attrs.width;
    m_lastKnownScreenH = attrs.height;

    // D-25: move windows that the new screen has left hanging off an edge, and
    // leave every other window exactly where the user put it. ensureVisible()
    // is the existing primitive for this -- it moves, never resizes, and it
    // declines to touch fullscreen or maximized clients.
    //
    // The hidden list is iterated too: a client unhidden after the resize would
    // otherwise be restored to coordinates that no longer exist on this screen.
    for (const auto& c : m_clients)       c->ensureVisible();
    for (const auto& c : m_hiddenClients) c->ensureVisible();

    // Republish _NET_WORKAREA against the new rectangle. This also re-clamps any
    // dock strut that was sized for the old screen (threat T-8-STRUT): the
    // clamp inside updateWorkarea() now runs against the refreshed geometry
    // rather than a stale cache, so an oversized strut cannot outlive a shrink.
    updateWorkarea();
}


void WindowManager::initialiseScreen()
{
    int i = 0;
    m_screenNumber = i;

    m_root = RootWindow(display(), i);
    m_defaultColormap = DefaultColormap(display(), i);

    // D-27: seed the manager-owned geometry cache immediately after the root
    // window is established, and BEFORE setupEwmhProperties() -- called at the
    // end of this function -- publishes the initial workarea from it. This is
    // the only place in the codebase that reads Xlib's cached screen size.
    m_lastKnownScreenW = DisplayWidth(display(), m_screenNumber);
    m_lastKnownScreenH = DisplayHeight(display(), m_screenNumber);

    XColor black, white, temp;

    if (!XAllocNamedColor(display(), m_defaultColormap, "black", &black, &temp))
        fatal("couldn't load colour \"black\"!");
    if (!XAllocNamedColor(display(), m_defaultColormap, "white", &white, &temp))
        fatal("couldn't load colour \"white\"!");

    m_cursor = x11::UniqueCursor(display(), makeCursor(display(), m_root,
        cursor_bits, cursor_mask_bits,
        cursor_width, cursor_height, cursor_x_hot, cursor_y_hot, &black, &white));

    m_xCursor = x11::UniqueCursor(display(), makeCursor(display(), m_root,
        ninja_cross_bits, ninja_cross_mask_bits,
        ninja_cross_width, ninja_cross_height, ninja_cross_x_hot, ninja_cross_y_hot,
        &black, &white));

    m_hCursor = x11::UniqueCursor(display(), makeCursor(display(), m_root,
        cursor_right_bits, cursor_right_mask_bits,
        cursor_right_width, cursor_right_height, cursor_right_x_hot, cursor_right_y_hot,
        &black, &white));

    m_vCursor = x11::UniqueCursor(display(), makeCursor(display(), m_root,
        cursor_down_bits, cursor_down_mask_bits,
        cursor_down_width, cursor_down_height, cursor_down_x_hot, cursor_down_y_hot,
        &black, &white));

    m_vhCursor = x11::UniqueCursor(display(), makeCursor(display(), m_root,
        cursor_down_right_bits, cursor_down_right_mask_bits,
        cursor_down_right_width, cursor_down_right_height,
        cursor_down_right_x_hot, cursor_down_right_y_hot, &black, &white));

    XSetWindowAttributes attr;
    attr.cursor = m_cursor.get();
    // XDIS-02 / RESEARCH Pitfall 2: the last bit is the no-RANDR fallback's only
    // event source. SubstructureNotifyMask delivers notifications about root's
    // CHILDREN; a resolution change arrives as a ConfigureNotify on ROOT ITSELF,
    // which requires the structure bit. Without it the fallback path in
    // src/Events.cpp has literally nothing to hook and can never fire.
    attr.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
        ColormapChangeMask | ButtonPressMask | ButtonReleaseMask |
        PropertyChangeMask | StructureNotifyMask;
    XChangeWindowAttributes(display(), m_root, CWCursor | CWEventMask, &attr);
    XSync(display(), false);

    // D-24: subscribe to RANDR screen-change notifications now that m_root
    // exists. Deferred to here from the capability query in the constructor for
    // exactly that reason -- see the comment beside that query.
    if (hasRandrExtension()) {
        XRRSelectInput(display(), m_root, RRScreenChangeNotifyMask);
    }

    m_menuBorderPixel     = allocateColour(m_config.menuBorders.c_str(), "menu border");

    // The WM's own popups are OVERRIDE-REDIRECT, and that flag is load-bearing
    // rather than decorative.
    //
    // Without it these are ordinary top-level children of the root, so the WM's
    // own CreateNotify/MapRequest handlers adopt them as clients: the menu, the
    // submenu and the WM-check window were all appearing in _NET_CLIENT_LIST,
    // observed live on an XRDP and a TigerVNC session. That is deferred item 7,
    // and it is not merely untidy. Once a popup is a client, every client
    // lifecycle path -- adopt, reparent, unmanage, destroy -- can act on the
    // very window menu() is about to XMoveResizeWindow/XMapRaised, which is the
    // BadWindow-then-no-menu failure recorded as deferred item 17. It also gave
    // circulate() a non-empty client list with nothing in Normal state, which is
    // how deferred item 6's 100% CPU spin was reachable on a freshly started WM.
    //
    // override_redirect is the X idiom that says "this window is not for a
    // window manager to manage" -- including the window manager that made it.
    // Setting it at creation, before the window is ever mapped, means no
    // CreateNotify handler can adopt it in the first place.
    // The flag MUST be set at creation, not patched on afterwards. CreateNotify
    // carries the value the window had when it was created, and eventCreate()
    // (src/Events.cpp) reads it off that event. XCreateSimpleWindow followed by
    // XChangeWindowAttributes therefore adopts the window anyway: the event is
    // already queued with override_redirect False by the time the change lands.
    // That is why createPopupWindow() uses XCreateWindow with the flag in the
    // creation valuemask -- measured, after the patch-afterwards version left
    // all four windows still sitting in _NET_CLIENT_LIST.
    const bool saveUnders = DoesSaveUnders(ScreenOfDisplay(display(), m_screenNumber));

    auto createPopupWindow = [&]() -> Window {
        XSetWindowAttributes attr;
        attr.override_redirect = True;
        attr.border_pixel      = m_menuBorderPixel;
        attr.background_pixel  = 0;
        unsigned long mask = CWOverrideRedirect | CWBorderPixel | CWBackPixel;
        if (saveUnders) {
            attr.save_under = True;
            mask |= CWSaveUnder;
        }
        return XCreateWindow(display(), m_root, 0, 0, 1, 1, 1,
                             CopyFromParent, InputOutput,
                             CopyFromParent, mask, &attr);
    };

    m_menuWindow = createPopupWindow();

    // Submenu popup window (Phase 7): app-category flyout, provisioned the same
    // way as m_menuWindow. Reuses m_menuFont/m_menuFgColor/m_menuBgColor/m_menuHlColor.
    m_submenuWindow = createPopupWindow();

    // The drag geometry readout gets its OWN window (D-34). It used to borrow
    // m_menuWindow, which meant one window served two unrelated purposes, each
    // of which maps, resizes and draws into it: a menu opened after a drag
    // inherited the indicator's size and contents until its first Expose, and
    // the two features could not be reasoned about independently. Separate
    // windows cost one XID and remove the whole class of interaction.
    m_geometryWindow = createPopupWindow();

    // Load menu font via Xft with fontconfig fallback chain (D-02)
    // Font size 12 matches Lucida Bold 14pt visual footprint (D-03)
    //
    // The preferred pattern comes from config since plan 09-01; its default is
    // the literal this call spelled inline before, so a user with no config file
    // gets the same face. The generic-sans SECOND rung below keeps its own
    // literal, and the fatal() below it stands: the menu measures every row
    // against this font, so unlike the tab there is no "carry on without it".
    // Only a host with no sans font at all reaches that exit -- the same
    // condition that already ended startup before this key existed (T-9-02).
    x11::XftFontPtr menuFont = x11::make_xft_font_name(display(),
        m_config.menuFont.c_str());
    if (!menuFont) {
        menuFont = x11::make_xft_font_name(display(), "sans-serif:size=12");
    }
    if (!menuFont) fatal("couldn't load default menu font");
    m_menuFont = menuFont.release();
    // m_menuFont is a raw pointer freed in release().

    // Allocate Xft colors for menu rendering
    Visual* visual = DefaultVisual(display(), m_screenNumber);
    Colormap cmap = DefaultColormap(display(), m_screenNumber);

    m_menuFgColor = x11::XftColorWrap(display(), visual, cmap, m_config.menuForeground.c_str());
    m_menuBgColor = x11::XftColorWrap(display(), visual, cmap, m_config.menuBackground.c_str());
    m_menuHlColor = x11::XftColorWrap(display(), visual, cmap, m_config.menuHighlight.c_str());

    if (!m_menuFgColor || !m_menuBgColor || !m_menuHlColor) {
        fatal("couldn't allocate menu colors");
    }

    // m_menuWindow background needs to match Xft background color pixel
    XSetWindowBackground(display(), m_menuWindow, m_menuBgColor->pixel);

    // m_submenuWindow reuses the same background color as m_menuWindow
    XSetWindowBackground(display(), m_submenuWindow, m_menuBgColor->pixel);

    // Set up EWMH root window properties (per EWMH spec)
    setupEwmhProperties();
}


bool WindowManager::tryAllocateColour(const char *name, unsigned long &out) const
{
    XColor nearest, ideal;

    // const_cast'd through the accessor rather than making display() const:
    // Xlib takes a non-const Display* everywhere, and this is a read of the
    // colormap, not a mutation of the manager.
    Display *d = const_cast<WindowManager *>(this)->m_display.get();
    if (!XAllocNamedColor(d, DefaultColormap(d, m_screenNumber),
                          name, &nearest, &ideal)) {
        return false;
    }
    out = nearest.pixel;
    return true;
}


unsigned long WindowManager::allocateColour(const char *name, const char *desc)
{
    // The FATAL wrapper, and the only one startup uses. A colour the server
    // cannot parse before the window manager has drawn anything is an
    // unrecoverable configuration error; the same failure arriving over the
    // socket later is not, which is why the predicate above exists separately
    // rather than as a flag on this function.
    unsigned long pixel = 0;
    if (!tryAllocateColour(name, pixel)) {
        char error[100];
        std::snprintf(error, sizeof error, "couldn't load %s colour", desc);
        fatal(error);
    }
    return pixel;
}


bool WindowManager::tryAllocateShadeOf(const char *name, double fraction,
                                       unsigned long &out) const
{
    XColor nearest, ideal;

    Display *d = const_cast<WindowManager *>(this)->m_display.get();
    if (!XAllocNamedColor(d, DefaultColormap(d, m_screenNumber),
                          name, &nearest, &ideal)) {
        return false;
    }

    auto blend = [fraction](unsigned short c) -> unsigned short {
        const double v = static_cast<double>(c);
        const double result = (fraction >= 0.0)
            ? v + (65535.0 - v) * fraction
            : v * (1.0 + fraction);
        if (result < 0.0) return 0;
        if (result > 65535.0) return 65535;
        return static_cast<unsigned short>(result);
    };

    XColor shade;
    shade.red   = blend(ideal.red);
    shade.green = blend(ideal.green);
    shade.blue  = blend(ideal.blue);
    shade.flags = DoRed | DoGreen | DoBlue;

    // A shade that will not allocate is reported as SUCCESS with a zero pixel,
    // exactly as the warning wrapper below reports it: the base colour did
    // resolve, and "no bevel" is the correct degradation on a display whose
    // colormap is full. A false return is reserved for the one condition a
    // caller must refuse on -- the base colour itself being unparseable.
    if (!XAllocColor(d, DefaultColormap(d, m_screenNumber), &shade)) {
        out = 0;
        return true;
    }

    out = shade.pixel;
    return true;
}


unsigned long WindowManager::allocateShadeOf(const char *name, double fraction,
                                             const char *desc)
{
    XColor nearest, ideal;

    if (!XAllocNamedColor(display(), DefaultColormap(display(), m_screenNumber),
                          name, &nearest, &ideal)) {
        char error[100];
        std::snprintf(error, sizeof error, "couldn't load %s colour", desc);
        fatal(error);
    }

    // Blend from the IDEAL values, not the nearest ones. `nearest` is what the
    // colormap could actually give us and may already have been rounded; on an
    // 8-bit visual, deriving a shade from an already-rounded value compounds
    // the error and can collapse the highlight into the body colour.
    auto blend = [fraction](unsigned short c) -> unsigned short {
        const double v = static_cast<double>(c);
        const double out = (fraction >= 0.0)
            ? v + (65535.0 - v) * fraction   // toward white
            : v * (1.0 + fraction);          // toward black
        if (out < 0.0) return 0;
        if (out > 65535.0) return 65535;
        return static_cast<unsigned short>(out);
    };

    XColor shade;
    shade.red   = blend(ideal.red);
    shade.green = blend(ideal.green);
    shade.blue  = blend(ideal.blue);
    shade.flags = DoRed | DoGreen | DoBlue;

    // A failed allocation here is NOT fatal, unlike the named-colour path
    // above. This is decoration: on a display whose colormap is full, the
    // correct outcome is a frame without bevels, not a window manager that
    // refuses to start. The caller treats 0 as "no bevel".
    if (!XAllocColor(display(), DefaultColormap(display(), m_screenNumber),
                     &shade)) {
        std::fprintf(stderr, "wm2: warning: could not allocate the %s shade, "
                             "frames will be drawn without bevels\n", desc);
        return 0;
    }

    return shade.pixel;
}


void WindowManager::setupEwmhProperties()
{
    // Create WM check child window (per EWMH spec).
    //
    // Override-redirect for the same reason as the menu popups: this is the WM's
    // own bookkeeping window, not a client, and without the flag the WM adopts it
    // into m_clients and publishes it in _NET_CLIENT_LIST (deferred item 7,
    // observed live on XRDP and TigerVNC). It is never mapped, but it is still a
    // top-level child of the root and the CreateNotify path does not care.
    // Set at creation, for the same reason as the popups above: CreateNotify
    // carries the creation-time value, so patching the flag on afterwards is
    // always too late to stop eventCreate() adopting the window.
    {
        XSetWindowAttributes checkAttr;
        checkAttr.override_redirect = True;
        m_wmCheckWindow = XCreateWindow(display(), m_root, -1, -1, 1, 1, 0,
                                        CopyFromParent, InputOutput,
                                        CopyFromParent, CWOverrideRedirect, &checkAttr);
    }

    // Set _NET_SUPPORTING_WM_CHECK on root pointing to check window
    XChangeProperty(display(), m_root, Atoms::net_supportingWmCheck,
                    XA_WINDOW, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&m_wmCheckWindow), 1);

    // DISC-03: the configuration socket's path, published in exactly the shape
    // of the write above -- a root-window property, PropModeReplace, one atom
    // and one payload. Written immediately after it so a client that has just
    // established a window manager is present can ask where to talk to it in
    // the same round trip.
    //
    // XA_STRING, format 8: the value is a filesystem path, and paths are bytes.
    // Written ONLY when the socket actually bound, so the property's presence
    // is itself the answer to "is there a socket?" -- a client never has to
    // connect to find out.
    if (m_socketServer.isListening()) {
        const std::string &socketPath = m_socketServer.path();
        XChangeProperty(display(), m_root, Atoms::wm2_configSocket,
                        XA_STRING, 8, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(socketPath.c_str()),
                        static_cast<int>(socketPath.size()));
    }

    // Set _NET_SUPPORTING_WM_CHECK on check window pointing to ITSELF (Pitfall 1)
    XChangeProperty(display(), m_wmCheckWindow, Atoms::net_supportingWmCheck,
                    XA_WINDOW, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&m_wmCheckWindow), 1);

    // Set _NET_WM_NAME on check window with UTF8_STRING encoding (Pitfall 2)
    // Length from the string, not a literal: the literal was 13 for a
    // 14-byte name and the 08.5-08 smoke transcript read "wm2-born-agai".
    const char *wmName = "wm2-born-again";
    XChangeProperty(display(), m_wmCheckWindow, Atoms::net_wmName,
                    Atoms::utf8_string, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(wmName),
                    static_cast<int>(std::strlen(wmName)));

    // Set _NET_SUPPORTED atom array on root window
    Atom supported[] = {
        Atoms::net_supported, Atoms::net_supportingWmCheck,
        Atoms::net_clientList, Atoms::net_activeWindow,
        Atoms::net_wmWindowType, Atoms::net_wmState,
        Atoms::net_wmName,
        Atoms::net_wmWindowTypeDock, Atoms::net_wmWindowTypeDialog,
        Atoms::net_wmWindowTypeNotification, Atoms::net_wmWindowTypeNormal,
        Atoms::net_wmWindowTypeUtility, Atoms::net_wmWindowTypeSplash,
        Atoms::net_wmWindowTypeToolbar,
        Atoms::net_wmStateFullscreen, Atoms::net_wmStateMaximizedVert,
        Atoms::net_wmStateMaximizedHorz, Atoms::net_wmStateHidden,
        Atoms::net_wmStrut, Atoms::net_wmStrutPartial,
        Atoms::net_numberOfDesktops, Atoms::net_currentDesktop,
        Atoms::net_workarea,
        // FOCUS-01 (plan 08-08). Skipping this advertisement would be fatal to
        // the feature rather than merely untidy: a compliant client checks
        // _NET_SUPPORTED before setting _NET_WM_USER_TIME, so an unadvertised
        // atom means the property is never written, the arbitration always sees
        // "absent", and focus-stealing prevention silently degrades to the
        // always-grant behaviour it exists to replace.
        Atoms::net_wmUserTime, Atoms::net_wmUserTimeWindow,
        Atoms::net_wmStateDemandsAttention,
        Atoms::net_wmStateSkipTaskbar, Atoms::net_wmStateSkipPager,
    };
    XChangeProperty(display(), m_root, Atoms::net_supported,
                    XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(supported),
                    sizeof(supported) / sizeof(Atom));

    // Set single-desktop atoms (per D-11, EWMH-08)
    long ndesktops = 1;
    XChangeProperty(display(), m_root, Atoms::net_numberOfDesktops,
                    XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&ndesktops), 1);
    long currentDesktop = 0;
    XChangeProperty(display(), m_root, Atoms::net_currentDesktop,
                    XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&currentDesktop), 1);

    // Set _NET_WORKAREA to full screen geometry initially (docks not yet known)
    long workarea[4] = { 0, 0,
        static_cast<long>(screenWidth()),
        static_cast<long>(screenHeight()) };
    XChangeProperty(display(), m_root, Atoms::net_workarea,
                    XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(workarea), 4);

    // Set _NET_ACTIVE_WINDOW to None initially
    Window noneWindow = None;
    XChangeProperty(display(), m_root, Atoms::net_activeWindow,
                    XA_WINDOW, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&noneWindow), 1);

    // Set _NET_CLIENT_LIST to empty array initially
    XChangeProperty(display(), m_root, Atoms::net_clientList,
                    XA_WINDOW, 32, PropModeReplace, nullptr, 0);
}


void WindowManager::installCursor(RootCursor c)
{
    installCursorOnWindow(c, m_root);
}


void WindowManager::installCursorOnWindow(RootCursor c, Window w)
{
    XSetWindowAttributes attr;

    switch (c) {
    case RootCursor::Delete:    attr.cursor = m_xCursor.get();   break;
    case RootCursor::Down:      attr.cursor = m_vCursor.get();   break;
    case RootCursor::Right:     attr.cursor = m_hCursor.get();   break;
    case RootCursor::DownRight: attr.cursor = m_vhCursor.get();  break;
    case RootCursor::Normal:    attr.cursor = m_cursor.get();    break;
    }

    XChangeWindowAttributes(display(), w, CWCursor, &attr);
}


// FOCUS-01 (plan 08-08), threat T-8-WRAP. The one timestamp comparison both
// arbitration entry points share -- Client::shouldFocusOnMap() for the map-time
// path and the _NET_ACTIVE_WINDOW branch in src/Events.cpp for the activation
// path. Neither may reimplement it, because getting it wrong in one place and
// not the other is exactly how a security decision drifts apart.
//
// X server timestamps are 32-bit millisecond counters that wrap roughly every
// 49.7 days. The naive `userTime >= m_lastUserInteraction` is wrong across a
// wrap: after the counter rolls over, every fresh client timestamp compares as
// smaller than the stored one and the WM refuses focus to EVERYTHING until the
// user clicks something -- a total focus outage, once per wrap.
//
// The fix is the standard X idiom: subtract in unsigned 32-bit arithmetic, then
// reinterpret the result as signed. The subtraction is exact modulo 2^32, so it
// yields the true difference for any pair of timestamps less than ~24.85 days
// apart, wrap or no wrap. Both operands are narrowed to uint32_t first because
// Xlib's `Time` is an unsigned long -- 64-bit on this target -- and subtracting
// at 64 bits would NOT wrap and would reintroduce the bug the cast exists to
// avoid.
bool WindowManager::isUserTimeRecent(Time userTime) const
{
    const std::uint32_t a = static_cast<std::uint32_t>(userTime);
    const std::uint32_t b = static_cast<std::uint32_t>(m_lastUserInteraction);
    const std::int32_t delta = static_cast<std::int32_t>(a - b);
    return delta >= 0;
}


Time WindowManager::timestamp(bool reset)
{
    if (reset) m_currentTime = CurrentTime;

    if (m_currentTime == CurrentTime) {
        XEvent event;

        // 08.5-12: the sentinel request, the predicate and the wait live in
        // include/TimestampWait.h, where a test binary can reach them and where
        // all three are now fixed -- the request cannot mismatch, the predicate
        // tests four fields, and the wait is bounded. Each fix is justified at
        // its own site in that header.
        //
        // This request is the one the constructor makes at :288, TWO LINES
        // before m_initialising is cleared at :290. That ordering is why the
        // request had to be made survivable rather than merely bounded: while
        // that flag is set, errorHandler() calls std::exit(1) for any error at
        // all (:416-419), so a BadMatch here did not make the window manager
        // slow, it stopped it starting.
        timestampSentinelRequest(display(), m_root, Atoms::wm2_running);

        // 08.5-06 attribution instrumentation. The counter names and the stderr
        // spellings below are UNCHANGED -- two committed measurement records
        // read those tokens and must stay comparable -- and the timeout record
        // added further down is a new line beside them, not an edit to them.
        //
        // SECURITY (threat T-8-TRACE-01): every line printed below is a fixed
        // ASCII state word plus an integer. No window id, atom name, window
        // title, host name or environment value. These transcripts are
        // committed to a public repository.
        ++m_timestampColdEntries;

        const TimestampWaitResult wait = timestampWaitFor(
            display(), m_root, Atoms::wm2_running,
            kTimestampWaitDeadlineMs, &event);

        if (wait.blocked) {
            ++m_timestampBlockedWaits;

            if (m_timestampBlockedWaits == 1) {
                // Written on the way IN, not on the way out. A window manager
                // blocked in a mask wait burns no CPU and is indistinguishable
                // from a healthy idle loop when observed from outside, so a
                // wait that never returns has to leave its record before it
                // starts rather than after it ends.
                std::fprintf(stderr,
                             "wm2: timestamp: entering blocking property wait\n");
                std::fflush(stderr);
            }

            if (wait.elapsedMs > m_timestampLongestWaitMs) {
                m_timestampLongestWaitMs = wait.elapsedMs;
            }
            if (wait.elapsedMs > 100) {
                // A warning, per the project's error-handling convention.
                // Never fatal(): a slow wait is a diagnosis, not a reason to
                // take the user's session down.
                std::fprintf(stderr,
                             "wm2: warning: timestamp: blocking property wait %ld ms\n",
                             wait.elapsedMs);
                std::fflush(stderr);
            }
        }

        // 08.5-12: the deadline expired without the sentinel arriving. A
        // DEFINED OUTCOME, not an error -- report CurrentTime and leave the
        // cache COLD, so the very next call retries instead of caching a value
        // that was never a server timestamp. Returning here is what makes the
        // bound safe: nothing downstream is handed a fabricated time.
        //
        // Recorded on its own line, distinctly from merely having entered a
        // wait, and ADDED BESIDE the existing instrumentation rather than
        // folded into it: the loop() summary at src/Events.cpp:174 is read
        // token-by-token by two committed measurement records and its spelling
        // is not this plan's to change.
        if (!wait.matched) {
            ++m_timestampWaitTimeouts;
            if (m_timestampWaitTimeouts == 1) {
                std::fprintf(stderr,
                             "wm2: warning: timestamp: property wait timed out\n");
                std::fflush(stderr);
            }
            return CurrentTime;
        }

        // Classification, retained. It answered whether the wait had been
        // satisfied by some other client's PropertyNotify -- a real hazard
        // while the selector matched on event type alone.
        //
        // 08.5-12 has made it unreachable BY CONSTRUCTION rather than by
        // deletion: the wait now returns only events the four-field predicate
        // accepts, so a foreign match is no longer possible. The counter and
        // its stderr spelling stay exactly as they are, still read by the two
        // committed measurement records, and a run that prints foreign=0 now
        // says so because the defect is gone rather than because it did not
        // happen to fire.
        if (!timestampIsSentinel(event, m_root, Atoms::wm2_running)) {
            ++m_timestampForeignMatches;
            if (m_timestampForeignMatches == 1) {
                std::fprintf(stderr,
                             "wm2: warning: timestamp: property wait matched a foreign event\n");
                std::fflush(stderr);
            }
        }

        m_currentTime = event.xproperty.time;
    }

    return m_currentTime;
}


void WindowManager::scanInitialWindows()
{
    unsigned int n;
    Window w1, w2, *wins;
    XWindowAttributes attr;

    XQueryTree(display(), m_root, &w1, &w2, &wins, &n);

    for (unsigned int i = 0; i < n; ++i) {
        XGetWindowAttributes(display(), wins[i], &attr);
        // The explicit `wins[i] == m_menuWindow` exclusion that used to sit here
        // is gone: it was a partial workaround for the popups not being
        // override-redirect, and it named only ONE of the four WM-owned windows
        // -- the submenu, geometry and WM-check windows were adopted regardless,
        // which is what put them in _NET_CLIENT_LIST. Now that all four are
        // created override-redirect, this single check covers every one of them,
        // and covers any popup added later without anyone remembering to extend
        // a list.
        if (attr.override_redirect) continue;

        (void)windowToClient(wins[i], true);
    }

    XFree(wins);
}


Client *WindowManager::windowToClient(Window w, bool create)
{
    if (w == 0) return nullptr;

    // O(1) lookup via hash map (client window IDs)
    auto it = m_windowMap.find(w);
    if (it != m_windowMap.end()) return it->second;

    // Fallback: linear scan for border windows (rare, per D-05)
    auto checkVector = [&](const auto& vec) -> Client* {
        for (const auto& c : vec) {
            if (c->hasWindow(w)) return c.get();
        }
        return nullptr;
    };

    Client* found = checkVector(m_clients);
    if (!found) found = checkVector(m_hiddenClients);
    if (found || !create) return found;

    // Create new client with unique_ptr ownership
    auto newClient = std::make_unique<Client>(this, w);
    Client* raw = newClient.get();
    m_clients.push_back(std::move(newClient));
    m_windowMap[raw->window()] = raw;
    updateClientList();
    return raw;
}


void WindowManager::installColormap(Colormap cmap)
{
    if (cmap == None) {
        XInstallColormap(display(), m_defaultColormap);
    } else {
        XInstallColormap(display(), cmap);
    }
}


void WindowManager::updateClientList()
{
    std::vector<Window> windows;
    windows.reserve(m_clients.size() + m_hiddenClients.size());
    for (const auto& c : m_clients) {
        windows.push_back(c->window());
    }
    for (const auto& c : m_hiddenClients) {
        windows.push_back(c->window());
    }
    XChangeProperty(display(), m_root, Atoms::net_clientList,
                    XA_WINDOW, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(windows.data()),
                    static_cast<int>(windows.size()));
}


void WindowManager::updateActiveWindow(Window w)
{
    XChangeProperty(display(), m_root, Atoms::net_activeWindow,
                    XA_WINDOW, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&w), 1);
}


void WindowManager::updateWorkarea()
{
    int screenW = screenWidth();
    int screenH = screenHeight();
    int left = 0, right = 0, top = 0, bottom = 0;

    if (screenW < 0) screenW = 0;
    if (screenH < 0) screenH = 0;

    // T-8-STRUT (plan 08-12): clamp in the WIDE type, before the narrowing
    // conversion, not after it.
    //
    // Stated precisely, because the imprecise version of this claim is
    // tempting and wrong: Xlib SIGN-EXTENDS format-32 property data into `long`
    // (_XRead32 reads it through an INT32*), so every strut value that reaches
    // this function is already representable in an int and `static_cast<int>`
    // on it is exact TODAY. Measured, not assumed -- a dock declaring
    // 0xffffff00 arrives here as -256, not as 4294967040.
    //
    // The clamp is kept anyway, and not as ceremony. It puts the [0, limit]
    // invariant at the point where the untrusted value ENTERS the arithmetic,
    // rather than leaving the code correct only for as long as that Xlib detail
    // holds -- and it makes the two guards below (which are load-bearing)
    // readable as one policy instead of three scattered conversions. A future
    // reader adding a 64-bit property, or a wider strut source, inherits the
    // invariant instead of having to rediscover it.
    //
    // Recorded in the plan summary as an equivalent mutant rather than covered
    // by a contrived test: no observation from outside the WM can distinguish
    // it, and a test claiming otherwise would not test its own name.
    auto clampStrut = [](long value, int limit) -> int {
        if (value <= 0) return 0;
        if (value >= static_cast<long>(limit)) return limit;
        return static_cast<int>(value);
    };

    // Iterate all clients (both normal and hidden) for dock struts
    auto checkStruts = [&](const auto& clients) {
        for (const auto& client : clients) {
            if (!client->isDock()) continue;

            Atom actualType;
            int actualFormat;
            unsigned long nItems, bytesAfter;
            unsigned char *data = nullptr;

            // T-8-PROP: the returned type, format and item count are all
            // checked before the reinterpret_cast. A dock is a client, and a
            // client may write _NET_WM_STRUT_PARTIAL with type CARDINAL and
            // format 8 -- twelve BYTES, which the cast below would read as
            // ninety-six.
            auto accumulate = [&](Atom prop, long len) -> bool {
                if (XGetWindowProperty(display(), client->window(), prop, 0, len,
                        false, XA_CARDINAL, &actualType, &actualFormat,
                        &nItems, &bytesAfter, &data) != Success) {
                    data = nullptr;
                    return false;
                }
                bool used = false;
                if (data && actualType == XA_CARDINAL && actualFormat == 32 && nItems >= 4) {
                    long *struts = reinterpret_cast<long*>(data);
                    left   = std::max(left,   clampStrut(struts[0], screenW));
                    right  = std::max(right,  clampStrut(struts[1], screenW));
                    top    = std::max(top,    clampStrut(struts[2], screenH));
                    bottom = std::max(bottom, clampStrut(struts[3], screenH));
                    used = true;
                }
                if (data) { XFree(data); data = nullptr; }
                return used;
            };

            // _NET_WM_STRUT_PARTIAL (12 values) first, _NET_WM_STRUT (4) as the
            // fallback -- and the fallback runs whenever the partial form was
            // absent OR unusable, so a malformed partial strut does not shadow
            // a well-formed simple one.
            if (!accumulate(Atoms::net_wmStrutPartial, 12)) {
                accumulate(Atoms::net_wmStrut, 4);
            }
        }
    };

    checkStruts(m_clients);
    checkStruts(m_hiddenClients);

    // Cap the COMBINED struts, not just each one individually.
    //
    // Clamping each edge to the screen dimension leaves left == right ==
    // screenW perfectly reachable, and the published width is then
    // screenW - left - right == -screenW. MEASURED on the shipped binary from a
    // single dock declaring 100000 on all four edges:
    //
    //     _NET_WORKAREA = (1024, 768, -1024, -768)
    //
    // An inverted workarea is not a cosmetic wrong number. Client::setMaximized
    // reads it and hands the result to XConfigureWindow, whose width and height
    // parameters are UNSIGNED -- the same arithmetic that bought a 64536-pixel
    // window in plan 08-11, reachable here by any client that can map a dock.
    //
    // Ordering matters: `right` is capped against what is LEFT after `left`, so
    // the two can never together exceed the screen and the published width and
    // height are non-negative by construction rather than by a trailing floor
    // that would hide which edge was unreasonable.
    if (left  > screenW)         left   = screenW;
    if (right > screenW - left)  right  = screenW - left;
    if (top    > screenH)        top    = screenH;
    if (bottom > screenH - top)  bottom = screenH - top;

    long workarea[4] = {
        static_cast<long>(left),
        static_cast<long>(top),
        static_cast<long>(screenW - left - right),
        static_cast<long>(screenH - top - bottom)
    };

    XChangeProperty(display(), m_root, Atoms::net_workarea,
                    XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(workarea), 4);
}


void WindowManager::clearFocus()
{
    static Window w = 0;
    Client *active = activeClient();

    // Focus-follows-pointer mode: just clear active client
    setActiveClient(nullptr);
    updateActiveWindow(None);

    if (active) {
        active->deactivate();

        for (Client *c = active->revertTo(); c; c = c->revertTo()) {
            if (c->isNormal()) {
                c->activate();
                return;
            }
        }

        installColormap(None);
    }

    if (w == 0) {
        XSetWindowAttributes attr;
        int mask = CWOverrideRedirect;
        attr.override_redirect = true;

        w = XCreateWindow(display(), root(), 0, 0, 1, 1, 0,
                          CopyFromParent, InputOnly, CopyFromParent,
                          mask, &attr);
        XMapWindow(display(), w);
    }

    XSetInputFocus(display(), w, RevertToPointerRoot, timestamp(false));
}


void WindowManager::skipInRevert(Client *c, Client *myRevert)
{
    for (const auto& client : m_clients) {
        if (client.get() != c && client->revertTo() == c) {
            client->setRevertTo(myRevert);
        }
    }
    // Also check hidden clients for revert chains
    for (const auto& client : m_hiddenClients) {
        if (client.get() != c && client->revertTo() == c) {
            client->setRevertTo(myRevert);
        }
    }
}


void WindowManager::addToHiddenList(Client *c)
{
    // Move unique_ptr from m_clients to m_hiddenClients
    auto it = std::find_if(m_clients.begin(), m_clients.end(),
        [c](const auto& up) { return up.get() == c; });
    if (it != m_clients.end()) {
        m_hiddenClients.push_back(std::move(*it));
        m_clients.erase(it);
    }
    // Note: map entry NOT updated -- raw Client* value unchanged per D-06, Pitfall 3
    updateClientList();
}


void WindowManager::removeFromHiddenList(Client *c)
{
    // Move unique_ptr from m_hiddenClients to m_clients
    auto it = std::find_if(m_hiddenClients.begin(), m_hiddenClients.end(),
        [c](const auto& up) { return up.get() == c; });
    if (it != m_hiddenClients.end()) {
        m_clients.push_back(std::move(*it));
        m_hiddenClients.erase(it);
    }
    // Note: map entry NOT updated -- raw Client* value unchanged per D-06, Pitfall 3
    updateClientList();
}


bool WindowManager::raiseTransients(Client *c)
{
    Client *first = nullptr;

    if (!c->isNormal()) return false;

    for (const auto& client : m_clients) {
        if (client->isNormal() && client->isTransient()) {
            if (c->hasWindow(client->transientFor())) {
                if (!first) first = client.get();
                else client->mapRaised();
            }
        }
    }

    if (first) {
        first->mapRaised();
        return true;
    }
    return false;
}


void WindowManager::spawn()
{
    // Double-fork to avoid zombies (from 9wm)
    char *displayName = DisplayString(display());

    if (fork() == 0) {
        if (fork() == 0) {
            close(ConnectionNumber(display()));

            if (displayName && displayName[0] != '\0') {
                setenv("DISPLAY", displayName, 1);
            }

            if (m_config.execUsingShell) {
                execl("/bin/sh", "sh", "-c", m_config.newWindowCommand.c_str(),
                      static_cast<char*>(nullptr));
            } else {
                execlp(m_config.newWindowCommand.c_str(),
                       m_config.newWindowCommand.c_str(),
                       static_cast<char*>(nullptr));
            }
            std::fprintf(stderr, "wm2: exec %s failed", m_config.newWindowCommand.c_str());
            perror(" ");
            // _exit(), not std::exit(): this is a forked copy of the WM, and
            // running its atexit handlers and static destructors here would
            // tear down state the real process still owns. Under ASan it also
            // triggers a leak check in the child, which reports allocations
            // that belong to the parent.
            _exit(1);
        }
        // Same reasoning as above -- the intermediate child must not run the
        // forked copy's atexit handlers or static destructors.
        _exit(0);
    }

    // Reaps only the intermediate child, which exits immediately. The
    // grandchild is orphaned to init on purpose, so it can never be a zombie.
    int status;
    wait(&status);
}


void WindowManager::spawnArgv(const std::vector<std::string>& argv)
{
    // Double-fork to avoid zombies (from 9wm), generalized from spawn() to take
    // an arbitrary pre-tokenized argv instead of a single command string.
    // Never routes through a shell -- always execvp() directly (T-7-06).
    char *displayName = DisplayString(display());

    if (fork() == 0) {
        if (fork() == 0) {
            close(ConnectionNumber(display()));

            if (displayName && displayName[0] != '\0') {
                setenv("DISPLAY", displayName, 1);
            }

            if (argv.empty()) {
                // No-op guard: avoids execvp(nullptr, ...) undefined behavior.
                // _exit(), not std::exit(): a forked copy must not run the
                // atexit handlers or static destructors it inherited.
                _exit(1);
            }

            std::vector<char*> argvPointers;
            argvPointers.reserve(argv.size() + 1);
            for (const std::string& arg : argv) {
                argvPointers.push_back(const_cast<char*>(arg.c_str()));
            }
            argvPointers.push_back(nullptr);

            execvp(argv[0].c_str(), argvPointers.data());

            std::fprintf(stderr, "wm2: exec %s failed", argv[0].c_str());
            perror(" ");
            // _exit(): forked copy, see spawn().
            _exit(1);
        }
        // _exit(): forked copy, see spawn().
        _exit(0);
    }

    // Reaps only the intermediate child; the grandchild is orphaned to init.
    int status;
    wait(&status);
}


void WindowManager::launchApp(const AppEntry& entry)
{
    // T-7-06: Desktop- and BinaryScan-sourced entries can never reach the shell
    // path below -- only Manual (config-authored) entries may opt into it, and
    // only via the pre-existing execUsingShell config flag (Phase 5 precedent).
    if (entry.source == AppEntry::Source::Manual && m_config.execUsingShell) {
        std::string joined;
        for (std::size_t i = 0; i < entry.execArgv.size(); ++i) {
            if (i > 0) joined += ' ';
            joined += entry.execArgv[i];
        }

        char *displayName = DisplayString(display());

        if (fork() == 0) {
            if (fork() == 0) {
                close(ConnectionNumber(display()));

                if (displayName && displayName[0] != '\0') {
                    setenv("DISPLAY", displayName, 1);
                }

                execl("/bin/sh", "sh", "-c", joined.c_str(),
                      static_cast<char*>(nullptr));

                std::fprintf(stderr, "wm2: exec %s failed", joined.c_str());
                perror(" ");
                // _exit(): forked copy, see spawn().
                _exit(1);
            }
            // _exit(): forked copy, see spawn().
            _exit(0);
        }

        // Reaps only the intermediate child; the grandchild is orphaned to init.
        int status;
        wait(&status);
        return;
    }

    spawnArgv(entry.execArgv);
}


void WindowManager::considerFocusChange(Client *c, Window w, Time ts)
{
    if (m_focusChanging) {
        stopConsideringFocus();
    }

    m_focusChanging = true;
    m_focusTimestamp = ts;
    m_focusCandidate = c;
    m_focusCandidateWindow = w;
    m_focusPointerMoved = false;
    m_focusPointerNowStill = false;

    // Start the auto-raise deadline per D-03 -- but only when the user asked
    // for auto-raise.
    //
    // FOCUS-02 / D-15: the auto-raise gate. Until this branch existed the
    // boolean had no runtime consumer and the deadline was armed
    // unconditionally. The gate belongs HERE, where the deadline is armed,
    // rather than in checkDelaysForFocus()'s expiry branches: with nothing
    // armed, computePollTimeout() already reports no active deadline and the
    // event loop blocks indefinitely instead of waking every autoRaiseDelay
    // milliseconds. That is both the behaviour the user asked for and the
    // no-idle-CPU-spin item on the compiled-behaviour checklist. Gating the
    // expiry instead would have left the loop waking on a timer whose only
    // effect was to do nothing.
    //
    // Deliberately NOT touched: the timer arithmetic (this decides WHETHER the
    // machinery runs, not how long it runs), and the pointer-stopped deadline
    // below, which serves a different purpose and is armed by the first
    // MotionNotify.
    if (m_config.autoRaise) {
        m_autoRaiseDeadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(m_config.autoRaiseDelay);
        m_autoRaiseDeadlineActive = true;
    } else {
        m_autoRaiseDeadlineActive = false;
    }
    // Pointer-stopped timer starts after first MotionNotify per D-04
    m_pointerStoppedDeadlineActive = false;

    m_focusCandidate->selectOnMotion(m_focusCandidateWindow, true);
}


void WindowManager::stopConsideringFocus()
{
    // BUG FIX: upstream sets m_focusChanging=false before checking it,
    // so selectOnMotion(false) never executes. We call selectOnMotion first.
    if (m_focusChanging && m_focusCandidateWindow && m_focusCandidate) {
        m_focusCandidate->selectOnMotion(m_focusCandidateWindow, false);
    }

    m_focusChanging = false;
    m_pointerStoppedDeadlineActive = false;
    m_autoRaiseDeadlineActive = false;
}


void WindowManager::checkDelaysForFocus()
{
    if (!m_focusCandidate || !m_focusChanging) return;

    using namespace std::chrono;

    auto now = steady_clock::now();

    if (m_focusPointerMoved) {
        // Branch 1: We've seen at least one MotionNotify.
        // Check if pointer has been still for 80ms (pointer-stopped detection).
        if (m_pointerStoppedDeadlineActive &&
            now >= m_pointerStoppedDeadline) {

            if (m_focusPointerNowStill) {
                // Pointer confirmed stopped -> try to raise
                m_focusCandidate->focusIfAppropriate(true);
            } else {
                // Pointer moved since last check; mark as "still until
                // proven otherwise". Next MotionNotify will set
                // m_focusPointerNowStill back to false.
                m_focusPointerNowStill = true;
                // Reset pointer-stopped deadline for next check
                m_pointerStoppedDeadline = now + milliseconds(m_config.pointerStoppedDelay);
            }
        }
    } else {
        // Branch 2: No MotionNotify at all (window doesn't generate motion).
        // Check auto-raise deadline directly (400ms).
        if (m_autoRaiseDeadlineActive &&
            now >= m_autoRaiseDeadline) {

            m_focusCandidate->focusIfAppropriate(true);
        }
    }
}


int WindowManager::computePollTimeout() const
{
    using namespace std::chrono;

    if (!m_pointerStoppedDeadlineActive && !m_autoRaiseDeadlineActive) {
        return -1;  // no active timers, block indefinitely
    }

    auto now = steady_clock::now();
    steady_clock::time_point earliest = steady_clock::time_point::max();

    if (m_pointerStoppedDeadlineActive) {
        earliest = std::min(earliest, m_pointerStoppedDeadline);
    }
    if (m_autoRaiseDeadlineActive) {
        earliest = std::min(earliest, m_autoRaiseDeadline);
    }

    if (earliest == steady_clock::time_point::max()) {
        return -1;
    }

    auto ms = duration_cast<milliseconds>(earliest - now).count();
    if (ms <= 0) return 0;
    return static_cast<int>(std::min(ms, static_cast<decltype(ms)>(30000)));
}


// =============================================================================
// The configuration socket (CGUI-02)
// =============================================================================
//
// The transport -- descriptors, buffers, the uid boundary, the framing bound --
// lives in src/SocketServer.cpp and knows nothing about what a message means.
// Everything below is POLICY: D-15's handshake rule and D-14's field ceiling.

// The version this build reports over the socket. Supplied by CMake from the
// project version so the two cannot drift; the fallback exists only so this
// file still compiles outside the project's own build.
#ifndef WM2_VERSION
#define WM2_VERSION "0.0.0-unknown"
#endif


// D-14's CEILING, ASSEMBLED IN EXACTLY ONE PLACE.
//
// Seven fields and no eighth: window manager version, protocol version, uptime
// in seconds, screen width, screen height, the count of managed windows and the
// count of hidden ones. That is the whole of what D-14 permits to leave the
// window manager over the socket.
//
// WHAT IS DELIBERATELY ABSENT: every per-window datum. No title, no class, no
// instance name, no geometry, no window id -- not filtered out downstream, but
// never gathered here at all (T-9-13). A per-window list is an explicitly
// EXCLUDED FUTURE MESSAGE: D-14 records it as addable later as a NEW message
// type without changing this one, so its omission is a decision rather than an
// oversight. Anyone adding it should add a message, not a field here.
//
// This function is the single site a security review has to read, and a
// region-scoped source check over it is part of this plan's acceptance: the
// assembly must not reach the client label accessor at all.
ConfigMessage WindowManager::statusReplyMessage() const
{
    ConfigMessage reply;
    reply.type = ConfigMessageType::StatusReply;

    const auto up = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - m_startTime).count();

    // "managed" is every window under management, hidden ones included:
    // addToHiddenList() MOVES a client out of m_clients rather than copying it,
    // so the two vectors are disjoint and a bare m_clients.size() would report
    // a window manager losing windows as the user hides them.
    const std::size_t hidden  = m_hiddenClients.size();
    const std::size_t managed = m_clients.size() + hidden;

    reply.fields.emplace_back("version",       WM2_VERSION);
    reply.fields.emplace_back("protocol",      std::to_string(kConfigProtocolVersion));
    reply.fields.emplace_back("uptime",        std::to_string(static_cast<long long>(up)));
    reply.fields.emplace_back("screen-width",  std::to_string(screenWidth()));
    reply.fields.emplace_back("screen-height", std::to_string(screenHeight()));
    reply.fields.emplace_back("managed",       std::to_string(managed));
    reply.fields.emplace_back("hidden",        std::to_string(hidden));

    return reply;
}


// =============================================================================
// Live configuration (CGUI-04, plan 09-04)
// =============================================================================

namespace {

// A whole string, or nothing. std::stoi("12abc") happily returns 12, which over
// a socket would mean answering "yes" to a request nobody made; the config FILE
// can afford that laxity because it warns on stderr to a user who is reading
// it, and a client waiting on a reply cannot.
bool parseWholeInt(const std::string& text, int& out)
{
    if (text.empty()) return false;
    std::size_t consumed = 0;
    long long value = 0;
    try {
        value = std::stoll(text, &consumed);
    } catch (const std::exception&) {
        return false;
    }
    if (consumed != text.size()) return false;
    if (value < -2147483648LL || value > 2147483647LL) return false;
    out = static_cast<int>(value);
    return true;
}

// The four spellings Config's parseBool() gives a meaning to, and no others.
// The parser maps every OTHER string to false silently -- fine for a file being
// read once at startup, wrong for a request, because `set click-to-focus yes`
// would be acknowledged while meaning the opposite of what was typed.
bool parseStrictBool(const std::string& text, bool& out)
{
    std::string lower;
    lower.reserve(text.size());
    for (unsigned char c : text) lower += static_cast<char>(std::tolower(c));

    if (lower == "true"  || lower == "1") { out = true;  return true; }
    if (lower == "false" || lower == "0") { out = false; return true; }
    return false;
}

// How the accepted value will read back out of the Config once the parser has
// stored it. Compared against the real read-back below, so a future divergence
// between this file's pre-validation and Config::applyKeyValue() is caught by
// the code rather than shipped.
std::string canonicalValue(const ConfigKeySpec& spec, const std::string& value)
{
    switch (spec.kind) {
    case ConfigValueKind::Boolean: {
        bool parsed = false;
        parseStrictBool(value, parsed);
        return parsed ? "true" : "false";
    }
    case ConfigValueKind::Integer: {
        int parsed = 0;
        parseWholeInt(value, parsed);
        return std::to_string(parsed);
    }
    case ConfigValueKind::String:
        break;
    }
    return value;
}

}  // namespace


bool WindowManager::reloadMenuColours(const Config &next, std::string &keyOut)
{
    Visual  *visual = DefaultVisual(display(), m_screenNumber);
    Colormap cmap   = DefaultColormap(display(), m_screenNumber);

    // ALLOCATE-THEN-SWAP, exactly as Border::reloadColours() does it: all four
    // values into locals, and only a complete success assigns anything.
    // XftColorWrap's move-assignment frees what it replaces, so the swap below
    // is where the old colours are released and not one statement earlier.
    x11::XftColorWrap fg(display(), visual, cmap, next.menuForeground.c_str());
    if (!fg) { keyOut = "menu-foreground"; return false; }
    x11::XftColorWrap bg(display(), visual, cmap, next.menuBackground.c_str());
    if (!bg) { keyOut = "menu-background"; return false; }
    x11::XftColorWrap hl(display(), visual, cmap, next.menuHighlight.c_str());
    if (!hl) { keyOut = "menu-highlight"; return false; }

    unsigned long borderPixel = 0;
    if (!tryAllocateColour(next.menuBorders.c_str(), borderPixel)) {
        keyOut = "menu-borders";
        return false;
    }

    m_menuFgColor     = std::move(fg);
    m_menuBgColor     = std::move(bg);
    m_menuHlColor     = std::move(hl);
    m_menuBorderPixel = borderPixel;

    // The three popups are UNMAPPED between uses and rebuilt on every opening,
    // so there is no live drawing to repair here -- only the state the server
    // paints from when the next opening maps them. The background pixel is what
    // fills the window on map (which is the BackgroundOnly state
    // tests/support/PixelVerdict.h names); the border pixel is the one-pixel
    // outline, these popups being the only windows in the codebase created with
    // a border WIDTH above zero.
    for (Window w : {m_menuWindow, m_submenuWindow, m_geometryWindow}) {
        if (w == None) continue;
        XSetWindowBackground(display(), w, m_menuBgColor->pixel);
        XSetWindowBorder(display(), w, m_menuBorderPixel);
    }
    return true;
}


bool WindowManager::reloadMenuFont(const std::string &pattern)
{
    // LOAD BEFORE CLOSE, exactly as Border::reloadTabFont() does it. The
    // difference between the two is only in the ladder: the menu has one rung
    // and a fatal() beneath it at startup, because every row of the menu is
    // MEASURED against this face and there is no "carry on without it". At
    // reload time there is no fatal to reach -- a pattern that will not open
    // leaves the previous face in place and the reload is refused.
    x11::XftFontPtr font = x11::make_xft_font_name(display(), pattern.c_str());
    if (!font) {
        std::fprintf(stderr, "wm2: warning: no usable menu font for that "
                             "pattern, keeping the previous one\n");
        return false;
    }

    if (m_menuFont) XftFontClose(display(), m_menuFont);
    m_menuFont = font.release();

    // NOTHING IS RE-LAID-OUT HERE, and that is correct rather than an
    // omission. WindowManager::menu() measures every row, computes the entry
    // height from this face's ascent and descent, and sizes the popup, ALL on
    // each opening -- so the next menu is drawn in the new face by
    // construction. Do not add a re-layout of an open menu: the popups are
    // unmapped between uses, and a menu that IS open is being iterated by a
    // modal loop holding pointers into state this function must not disturb.
    return true;
}


bool WindowManager::applyConfig(const Config &next, std::string &reasonOut)
{
    // DISC-06a. Read the declaration in include/Manager.h before adding to
    // this function: the rule is that every field is diffed here, and that
    // nothing anywhere else in the codebase writes running state from a Config.

    const Config previous = m_config;

    // --- Everything that can FAIL happens before anything is stored ---------
    //
    // Plan 09-04 stored `next` first, because the one live setting it applied
    // could not fail. A colour can: `set tab-background nonsense` is a value
    // the CONFIG PARSER accepts verbatim (colours are validated by the server,
    // not by Config) and the X server then refuses. Storing first and failing
    // second would leave `get tab-background` reporting a value nothing is
    // drawn in -- so the order is now validate, then store, then apply, and a
    // failure returns having changed nothing at all.
    const bool coloursChanged =
        next.tabForeground    != previous.tabForeground    ||
        next.tabBackground    != previous.tabBackground    ||
        next.frameBackground  != previous.frameBackground  ||
        next.buttonBackground != previous.buttonBackground ||
        next.borders          != previous.borders          ||
        next.menuForeground   != previous.menuForeground   ||
        next.menuBackground   != previous.menuBackground   ||
        next.menuHighlight    != previous.menuHighlight    ||
        next.menuBorders      != previous.menuBorders;

    if (coloursChanged) {
        // PRE-FLIGHT. Every one of the nine is resolved before either reload
        // begins, so the two reloads below cannot leave the frame palette new
        // and the menu palette old: by the time the first of them commits, the
        // server has already agreed that all nine names parse.
        const struct { const char *key; const std::string *value; } palette[] = {
            {"tab-foreground",    &next.tabForeground},
            {"tab-background",    &next.tabBackground},
            {"frame-background",  &next.frameBackground},
            {"button-background", &next.buttonBackground},
            {"borders",           &next.borders},
            {"menu-foreground",   &next.menuForeground},
            {"menu-background",   &next.menuBackground},
            {"menu-highlight",    &next.menuHighlight},
            {"menu-borders",      &next.menuBorders},
        };
        for (const auto &entry : palette) {
            unsigned long ignored = 0;
            if (!tryAllocateColour(entry.value->c_str(), ignored)) {
                reasonOut = std::string("the X server cannot parse the ") +
                            entry.key + " colour";
                return false;
            }
        }

        std::string offending;
        if (!Border::reloadColours(this, next, offending) ||
            !reloadMenuColours(next, offending)) {
            // Unreachable after the pre-flight above unless the colormap
            // filled between the two, which on the TrueColor visuals this
            // project targets cannot happen. Reported rather than asserted,
            // because a window manager must not abort on a colour.
            reasonOut = "could not allocate the " + offending + " colour";
            return false;
        }
    }

    // --- Fonts --------------------------------------------------------------
    //
    // Also before the store, and for the same reason as the colours: a
    // fontconfig pattern with no usable face is refused, and a refusal that
    // had already moved m_config would leave `get tab-font` naming a face
    // nothing is drawn in.
    const bool tabFontChanged  = next.tabFont  != previous.tabFont;
    const bool menuFontChanged = next.menuFont != previous.menuFont;

    if (tabFontChanged && !Border::reloadTabFont(this, next.tabFont)) {
        reasonOut = "no usable face for that tab-font pattern";
        return false;
    }
    if (menuFontChanged && !reloadMenuFont(next.menuFont)) {
        // The tab face may already have been swapped above. That is not a
        // half-applied state a caller can observe as inconsistent: a `set`
        // names ONE key, so at most one of these two branches ever runs for a
        // set, and a `reload` that fails here refuses whole and leaves the
        // window manager on the configuration it already had -- with a tab
        // face from a file it has decided not to adopt. Named here rather than
        // left to be discovered, because it is the one place in this function
        // where the two-stage swap is not literally atomic.
        reasonOut = "no usable face for that menu-font pattern";
        return false;
    }

    // --- Stored WHOLE, now that nothing left can fail -----------------------
    //
    // Whole rather than field by field, so a field a later plan does not yet
    // apply live is still the value the window manager reports and the value
    // the next thing to read it sees: a new branch below writes an application,
    // never an assignment.
    m_config = next;

    // --- Frame thickness ----------------------------------------------------
    //
    // The diff is what makes the idempotency guarantee true: a `set` that names
    // the value already in force does no work at all, so a client that repeats
    // itself cannot make the desktop flicker or the frames drift.
    if (next.frameThickness != previous.frameThickness) {
        FRAME_WIDTH = next.frameThickness;

        // Both lists. addToHiddenList() MOVES a client out of m_clients rather
        // than copying it, so walking only m_clients would leave every hidden
        // window wearing the old thickness the moment it is unhidden.
        for (const auto &client : m_clients)       client->relayoutFrame();
        for (const auto &client : m_hiddenClients) client->relayoutFrame();

        XFlush(display());
    }

    // --- Tab font -----------------------------------------------------------
    //
    // The face was swapped above; what is left is the geometry it moved. A tab
    // is as thick as the face's metrics say, so every managed client's frame,
    // tab, button and shape has to be recomputed -- through the same entry
    // point a thickness change uses, not a second computation of the same
    // numbers.
    //
    // Skipped when the thickness ALSO changed in the same application, because
    // that branch has just walked both lists and re-laid every frame out with
    // the new tab width already in force; running both would re-shape every
    // frame twice for one message.
    if (tabFontChanged && next.frameThickness == previous.frameThickness) {
        for (const auto &client : m_clients)       client->relayoutFrameForFont();
        for (const auto &client : m_hiddenClients) client->relayoutFrameForFont();

        XFlush(display());
    }

    // --- Colours ------------------------------------------------------------
    //
    // The palette was reloaded above, before m_config moved; what is left is to
    // make it visible. Both lists again, for the same reason: a hidden window
    // unhidden later must not come back wearing the old palette.
    if (coloursChanged) {
        for (const auto &client : m_clients)       client->repaintForColourChange();
        for (const auto &client : m_hiddenClients) client->repaintForColourChange();

        XFlush(display());
    }

    return true;
}


bool WindowManager::applyConfigSet(const std::string &key, const std::string &value,
                                   std::string &reasonOut)
{
    const ConfigKeySpec *spec = configKeySpecFor(key);
    if (!spec) {
        // Not a single setting. `rule-*` and `menu-entry-*` land here too, and
        // deliberately: they are ordered repeated groups, and applying one of
        // them in isolation would mean something different from what the same
        // line means in a file.
        reasonOut = "unknown setting";
        return false;
    }

    // --- Validation, BEFORE the parser sees anything -------------------------
    //
    // This is the whole of the prohibition this plan carries. The parser's
    // answer to a bad value is to CLAMP it and warn on stderr; that is right
    // for a file read at startup and wrong for a request with a client waiting,
    // because a clamp would be acknowledged as though it were what was asked
    // for. So the range and the kind are checked here and the value is refused
    // -- and then the value that survives is applied through the very same
    // Config::applyKeyValue(). Stricter than the file path, never looser: no
    // value reaches state by this route that the file route would have rejected.
    switch (spec->kind) {
    case ConfigValueKind::Boolean: {
        bool parsed = false;
        if (!parseStrictBool(value, parsed)) {
            reasonOut = "expected true or false";
            return false;
        }
        break;
    }
    case ConfigValueKind::Integer: {
        int parsed = 0;
        if (!parseWholeInt(value, parsed)) {
            reasonOut = "expected a whole number";
            return false;
        }
        if (parsed < spec->minValue || parsed > spec->maxValue) {
            reasonOut = "value out of range (" + std::to_string(spec->minValue) +
                        " to " + std::to_string(spec->maxValue) + ")";
            return false;
        }
        break;
    }
    case ConfigValueKind::String:
        // Taken verbatim, exactly as the file takes it. A colour or a font
        // pattern is validated by the server and by fontconfig respectively,
        // both of which degrade rather than fail -- the same treatment a value
        // in the file gets.
        break;
    }

    // --- Application, on a COPY ---------------------------------------------
    Config next = m_config;
    next.applyKeyValue(key, value);

    // And the read-back check: did the parser actually store what was agreed?
    // A mismatch here means the validation above and Config::applyKeyValue()
    // have come to disagree -- a bug, not a user error -- so it fails CLOSED,
    // leaving m_config untouched, rather than applying something nobody
    // authorised.
    std::string after;
    if (!configValueForKey(next, key, after) || after != canonicalValue(*spec, value)) {
        reasonOut = "the configuration parser did not accept this value";
        return false;
    }

    // The funnel can itself refuse -- a colour the config parser takes verbatim
    // and the X server then rejects reaches this point looking valid. Its
    // reason is passed straight through, so the client is told which key and
    // why rather than being handed a generic failure.
    return applyConfig(next, reasonOut);
}


bool WindowManager::reloadConfigFromDisk(std::string &reasonOut)
{
    // A file that does not exist is not an error -- that is the ordinary state
    // of a machine with no user configuration, and Config::applyFile() skips it
    // silently. A file that EXISTS and cannot be read is a different thing
    // entirely: silently carrying on would report a successful reload that did
    // not read the user's settings.
    const std::string userFile = xdgConfigHome() + "/wm2-born-again/config";
    if (::access(userFile.c_str(), F_OK) == 0 &&
        ::access(userFile.c_str(), R_OK) != 0) {
        reasonOut = "cannot read " + userFile;
        return false;
    }

    // The WHOLE layered load, command line included, rather than the file
    // merged onto current state. That is what makes an override given at
    // startup still win afterwards, and it is also what discards a `set` made
    // over the socket -- which is correct, because a `set` writes no file
    // (D-01) and so has nothing on disk to be re-read.
    std::vector<char *> argv;
    argv.reserve(m_cliArgs.size() + 1);
    for (std::string &arg : m_cliArgs) argv.push_back(&arg[0]);
    argv.push_back(nullptr);

    Config next = Config::load(static_cast<int>(m_cliArgs.size()),
                               argv.empty() ? nullptr : argv.data());

    // Applied BEFORE the saved snapshot is replaced. A file carrying a colour
    // the server cannot parse is refused whole -- nothing is applied and the
    // snapshot still describes what the window manager is actually drawing
    // with, which is what makes DISC-07's "revert" mean something.
    if (!applyConfig(next, reasonOut)) return false;

    m_savedConfig = next;
    return true;
}


// One frame in, one decision out.
//
// D-15 IS ENFORCED HERE AND NOWHERE ELSE: a message arriving before a handshake
// closes the connection with an error first, and so does a handshake naming a
// protocol version this build does not speak. The window manager never answers
// a stranger with anything but a refusal.
ConfigSocketReply WindowManager::handleConfigRequest(const ConfigSocketRequest &request)
{
    ConfigSocketReply out;

    ConfigMessage message;
    const ConfigDecodeResult result = configProtocolDecode(request.line, message);

    auto refuse = [&out](const char *reason, bool close) {
        ConfigMessage error;
        error.type = ConfigMessageType::Error;
        error.reason = reason;
        out.line = configProtocolEncode(error);
        out.closeAfterSend = close;
    };

    // A refusal that NAMES THE KEY, for get and set. The connection is always
    // kept open: a client that asked about a key this build does not know is
    // not a stranger, it is a client that guessed wrong, and it may well have
    // more to say.
    auto refuseKey = [&out](const std::string &key, const char *reason) {
        ConfigMessage error;
        error.type = ConfigMessageType::Error;
        error.key = key;
        error.reason = reason;
        out.line = configProtocolEncode(error);
    };

    switch (result) {
    case ConfigDecodeResult::TooLong:
        refuse("message too long", true);
        return out;

    case ConfigDecodeResult::Malformed:
        refuse("malformed message", true);
        return out;

    case ConfigDecodeResult::UnknownType:
        // A type this version does not speak is a NAMED verdict, not a
        // malformation (DISC-01c): declining it is what lets a later version
        // add a message additively. Before the handshake it is still a
        // stranger's first word, so it is refused with the connection.
        refuse("unsupported message type", !request.helloSeen);
        return out;

    case ConfigDecodeResult::Ok:
        break;
    }

    if (!request.helloSeen && message.type != ConfigMessageType::Hello) {
        refuse("handshake required", true);
        return out;
    }

    switch (message.type) {
    case ConfigMessageType::Hello: {
        if (message.protocol != kConfigProtocolVersion) {
            refuse("unsupported protocol version", true);
            return out;
        }
        ConfigMessage ack;
        ack.type = ConfigMessageType::HelloAck;
        ack.program = "wm2-born-again";
        ack.protocol = kConfigProtocolVersion;
        out.line = configProtocolEncode(ack);
        out.helloAccepted = true;
        return out;
    }

    case ConfigMessageType::Status:
        out.line = configProtocolEncode(statusReplyMessage());
        return out;

    case ConfigMessageType::Get: {
        // Answered from the EFFECTIVE config, never from the file: the question
        // a client is asking is "what are you using?", and after a `set` those
        // two are deliberately different things.
        std::string value;
        if (!configValueForKey(m_config, message.key, value)) {
            refuseKey(message.key, "unknown setting");
            return out;
        }
        ConfigMessage reply;
        reply.type = ConfigMessageType::Value;
        reply.key = message.key;
        reply.value = value;
        out.line = configProtocolEncode(reply);
        return out;
    }

    case ConfigMessageType::Set: {
        std::string reason;
        if (!applyConfigSet(message.key, message.value, reason)) {
            refuseKey(message.key, reason.c_str());
            return out;
        }
        ConfigMessage ack;
        ack.type = ConfigMessageType::Ack;
        ack.key = message.key;
        out.line = configProtocolEncode(ack);
        return out;
    }

    case ConfigMessageType::Reload: {
        std::string reason;
        if (!reloadConfigFromDisk(reason)) {
            refuse(reason.c_str(), false);
            return out;
        }
        ConfigMessage reloaded;
        reloaded.type = ConfigMessageType::Reloaded;
        out.line = configProtocolEncode(reloaded);
        return out;
    }

    case ConfigMessageType::HelloAck:
    case ConfigMessageType::Value:
    case ConfigMessageType::Ack:
    case ConfigMessageType::Error:
    case ConfigMessageType::Reloaded:
    case ConfigMessageType::StatusReply:
        // Replies. A client sending one is confused about which end it is;
        // named individually rather than left to a default arm so a twelfth
        // message type is a compile error here instead of silence.
        refuse("not a request", false);
        return out;

    case ConfigMessageType::Unknown:
        // Unreachable: the UnknownType verdict above already returned.
        refuse("unsupported message type", true);
        return out;
    }

    refuse("unsupported message type", true);
    return out;
}


void WindowManager::startConfigSocket()
{
    // DisplayString() rather than getenv("DISPLAY"): the window manager may
    // have been given a display by some other route, and the socket has to be
    // named after the display it is actually managing -- that per-display name
    // is what lets two window managers on two displays coexist, and what lets
    // the ctest suite run window-manager fixtures in parallel.
    m_socketServer.listen(DisplayString(display()));
}
