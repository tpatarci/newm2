#include "Manager.h"
#include "Client.h"
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
#include <algorithm>
#include "Cursors.h"

// Static member definitions
Atom Atoms::wm_state = None;
Atom Atoms::wm_changeState = None;
Atom Atoms::wm_protocols = None;
Atom Atoms::wm_delete = None;
Atom Atoms::wm_takeFocus = None;
Atom Atoms::wm_colormaps = None;
Atom Atoms::wm2_running = None;

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

volatile std::sig_atomic_t WindowManager::m_signalled = 0;
int  WindowManager::s_pipeWriteFd = -1;
bool WindowManager::m_initialising = false;
bool ignoreBadWindowErrors = false;

const char *const WindowManager::m_menuCreateLabel = "New";


WindowManager::WindowManager(const Config& config)
    : m_config(config)
    , m_screenNumber(0)
    , m_root(None)
    , m_defaultColormap(None)
    , m_activeClient(nullptr)
    , m_shapeEvent(0)
    , m_currentTime(-1)
    , m_looping(false)
    , m_returnCode(0)
    , m_menuWindow(None)
    , m_menuFont(nullptr)
    , m_menuBorderPixel(0)
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
    std::fprintf(stderr, "\nwm2-born-again: Copyright (c) 1996-7 Chris Cannam, modernized 2026.\n"
                 "  Parts derived from 9wm Copyright (c) 1994-96 David Hogan\n"
                 "  Copying and redistribution encouraged.  No warranty.\n\n");

    std::fprintf(stderr, "  Focus follows pointer.  Hidden clients only on menu.\n\n");

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
    Atoms::utf8_string            = XInternAtom(display(), "UTF8_STRING", false);

    // Check Shape extension -- warn but continue if missing (graceful fallback)
    int dummy;
    if (XShapeQueryExtension(display(), &m_shapeEvent, &dummy)) {
        std::fprintf(stderr, "  Shape extension available.\n");
    } else {
        std::fprintf(stderr, "wm2: warning: no shape extension, frames will be rectangular\n");
        m_shapeEvent = -1;
    }

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


void WindowManager::release()
{
    if (m_returnCode != 0) return;

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

    // Clean up Xft menu resources
    m_menuDraw.reset();
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
        (void)write(s_pipeWriteFd, &c, 1);
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


void WindowManager::initialiseScreen()
{
    int i = 0;
    m_screenNumber = i;

    m_root = RootWindow(display(), i);
    m_defaultColormap = DefaultColormap(display(), i);

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
    attr.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
        ColormapChangeMask | ButtonPressMask | ButtonReleaseMask |
        PropertyChangeMask;
    XChangeWindowAttributes(display(), m_root, CWCursor | CWEventMask, &attr);
    XSync(display(), false);

    m_menuBorderPixel     = allocateColour(m_config.menuBorders.c_str(), "menu border");

    m_menuWindow = XCreateSimpleWindow(display(), m_root, 0, 0, 1, 1, 1,
                                       m_menuBorderPixel, 0);

    if (DoesSaveUnders(ScreenOfDisplay(display(), m_screenNumber))) {
        XSetWindowAttributes suAttr;
        suAttr.save_under = true;
        XChangeWindowAttributes(display(), m_menuWindow, CWSaveUnder, &suAttr);
    }

    // Load menu font via Xft with fontconfig fallback chain (D-02)
    // Font size 12 matches Lucida Bold 14pt visual footprint (D-03)
    x11::XftFontPtr menuFont = x11::make_xft_font_name(display(),
        "Noto Sans,DejaVu Sans,Sans:size=12");
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

    // Set up EWMH root window properties (per EWMH spec)
    setupEwmhProperties();
}


unsigned long WindowManager::allocateColour(const char *name, const char *desc)
{
    XColor nearest, ideal;

    if (!XAllocNamedColor(display(), DefaultColormap(display(), m_screenNumber),
                          name, &nearest, &ideal)) {
        char error[100];
        std::sprintf(error, "couldn't load %s colour", desc);
        fatal(error);
    }

    return nearest.pixel;
}


void WindowManager::setupEwmhProperties()
{
    // Create WM check child window (per EWMH spec)
    m_wmCheckWindow = XCreateSimpleWindow(display(), m_root, -1, -1, 1, 1, 0, 0, 0);

    // Set _NET_SUPPORTING_WM_CHECK on root pointing to check window
    XChangeProperty(display(), m_root, Atoms::net_supportingWmCheck,
                    XA_WINDOW, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&m_wmCheckWindow), 1);

    // Set _NET_SUPPORTING_WM_CHECK on check window pointing to ITSELF (Pitfall 1)
    XChangeProperty(display(), m_wmCheckWindow, Atoms::net_supportingWmCheck,
                    XA_WINDOW, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&m_wmCheckWindow), 1);

    // Set _NET_WM_NAME on check window with UTF8_STRING encoding (Pitfall 2)
    const char *wmName = "wm2-born-again";
    XChangeProperty(display(), m_wmCheckWindow, Atoms::net_wmName,
                    Atoms::utf8_string, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(wmName), 13);

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
        static_cast<long>(DisplayWidth(display(), m_screenNumber)),
        static_cast<long>(DisplayHeight(display(), m_screenNumber)) };
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


Time WindowManager::timestamp(bool reset)
{
    if (reset) m_currentTime = CurrentTime;

    if (m_currentTime == CurrentTime) {
        XEvent event;
        XChangeProperty(display(), m_root, Atoms::wm2_running,
                        Atoms::wm2_running, 8, PropModeAppend,
                        reinterpret_cast<unsigned char*>(const_cast<char*>("")), 0);
        XMaskEvent(display(), PropertyChangeMask, &event);
        m_currentTime = event.xproperty.time;
    }

    return static_cast<Time>(m_currentTime);
}


void WindowManager::scanInitialWindows()
{
    unsigned int n;
    Window w1, w2, *wins;
    XWindowAttributes attr;

    XQueryTree(display(), m_root, &w1, &w2, &wins, &n);

    for (unsigned int i = 0; i < n; ++i) {
        XGetWindowAttributes(display(), wins[i], &attr);
        if (attr.override_redirect || wins[i] == m_menuWindow) continue;

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
    int screenW = DisplayWidth(display(), m_screenNumber);
    int screenH = DisplayHeight(display(), m_screenNumber);
    int left = 0, right = 0, top = 0, bottom = 0;

    // Iterate all clients (both normal and hidden) for dock struts
    auto checkStruts = [&](const auto& clients) {
        for (const auto& client : clients) {
            if (!client->isDock()) continue;

            Atom actualType;
            int actualFormat;
            unsigned long nItems, bytesAfter;
            unsigned char *data = nullptr;

            // Try _NET_WM_STRUT_PARTIAL first (12 values)
            bool found = false;
            if (XGetWindowProperty(display(), client->window(),
                    Atoms::net_wmStrutPartial, 0, 12, false, XA_CARDINAL,
                    &actualType, &actualFormat, &nItems, &bytesAfter, &data) == Success
                && data && nItems >= 4) {
                long *struts = reinterpret_cast<long*>(data);
                left   = std::max(left,   static_cast<int>(struts[0]));
                right  = std::max(right,  static_cast<int>(struts[1]));
                top    = std::max(top,    static_cast<int>(struts[2]));
                bottom = std::max(bottom, static_cast<int>(struts[3]));
                found = true;
            }
            if (data) { XFree(data); data = nullptr; }

            // Fallback to _NET_WM_STRUT (4 values)
            if (!found) {
                if (XGetWindowProperty(display(), client->window(),
                        Atoms::net_wmStrut, 0, 4, false, XA_CARDINAL,
                        &actualType, &actualFormat, &nItems, &bytesAfter, &data) == Success
                    && data && nItems >= 4) {
                    long *struts = reinterpret_cast<long*>(data);
                    left   = std::max(left,   static_cast<int>(struts[0]));
                    right  = std::max(right,  static_cast<int>(struts[1]));
                    top    = std::max(top,    static_cast<int>(struts[2]));
                    bottom = std::max(bottom, static_cast<int>(struts[3]));
                }
                if (data) { XFree(data); data = nullptr; }
            }
        }
    };

    checkStruts(m_clients);
    checkStruts(m_hiddenClients);

    // Clamp strut values to screen dimensions (security: prevent absurd values)
    left   = std::min(left,   screenW);
    right  = std::min(right,  screenW);
    top    = std::min(top,    screenH);
    bottom = std::min(bottom, screenH);

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
            std::exit(1);
        }
        std::exit(0);
    }

    int status;
    wait(&status);
}


const char* WindowManager::menuLabel(int i)
{
    // Stub -- Buttons.cpp will use a different approach
    return m_menuCreateLabel;
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

    // Start the auto-raise deadline per D-03
    m_autoRaiseDeadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(m_config.autoRaiseDelay);
    m_autoRaiseDeadlineActive = true;
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
