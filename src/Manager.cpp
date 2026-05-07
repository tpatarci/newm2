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
#include "Cursors.h"
#include "Rotated.h"

// Static member definitions
Atom Atoms::wm_state = None;
Atom Atoms::wm_changeState = None;
Atom Atoms::wm_protocols = None;
Atom Atoms::wm_delete = None;
Atom Atoms::wm_takeFocus = None;
Atom Atoms::wm_colormaps = None;
Atom Atoms::wm2_running = None;

volatile std::sig_atomic_t WindowManager::m_signalled = 0;
int  WindowManager::s_pipeWriteFd = -1;
bool WindowManager::m_initialising = false;
bool ignoreBadWindowErrors = false;

const char *const WindowManager::m_menuCreateLabel = "New";


WindowManager::WindowManager()
    : m_screenNumber(0)
    , m_root(None)
    , m_defaultColormap(None)
    , m_activeClient(nullptr)
    , m_shapeEvent(0)
    , m_currentTime(-1)
    , m_looping(false)
    , m_returnCode(0)
    , m_menuWindow(None)
    , m_menuForegroundPixel(0)
    , m_menuBackgroundPixel(0)
    , m_menuBorderPixel(0)
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
                 "  %s\n  Copying and redistribution encouraged.  No warranty.\n\n",
                 XV_COPYRIGHT);

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

    // Separate normal and iconic clients, unparent all
    std::vector<Client*> normalList;
    std::vector<Client*> unparentList;

    for (auto *c : m_clients) {
        if (c->isNormal()) normalList.push_back(c);
        else unparentList.push_back(c);
    }

    for (auto *c : normalList) {
        unparentList.push_back(c);
    }

    m_clients.clear();

    for (auto *c : unparentList) {
        c->unreparent();
        c->release();
    }

    XSetInputFocus(display(), PointerRoot, RevertToPointerRoot, timestamp(false));
    installColormap(None);

    // RAII handles: cursors (m_cursor, m_xCursor, etc.), m_menuGC, m_menuFont, m_display
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

    m_menuForegroundPixel = allocateColour("black", "menu foreground");
    m_menuBackgroundPixel = allocateColour("gray80", "menu background");
    m_menuBorderPixel     = allocateColour("black", "menu border");

    m_menuWindow = XCreateSimpleWindow(display(), m_root, 0, 0, 1, 1, 1,
                                       m_menuBorderPixel, m_menuBackgroundPixel);

    if (DoesSaveUnders(ScreenOfDisplay(display(), m_screenNumber))) {
        XSetWindowAttributes suAttr;
        suAttr.save_under = true;
        XChangeWindowAttributes(display(), m_menuWindow, CWSaveUnder, &suAttr);
    }

    // Load menu font via RAII
    m_menuFont = x11::make_font_struct(display(), "-*-lucida-medium-r-*-*-14-*-75-75-*-*-*-*");
    if (!m_menuFont) m_menuFont = x11::make_font_struct(display(), "fixed");
    if (!m_menuFont) fatal("couldn't load default menu font");

    // Create menu GC via RAII
    XGCValues values;
    values.background = m_menuBackgroundPixel;
    values.foreground = m_menuForegroundPixel ^ m_menuBackgroundPixel;
    values.function = GXxor;
    values.line_width = 0;
    values.subwindow_mode = IncludeInferiors;
    values.font = m_menuFont->fid;

    m_menuGC = x11::make_gc(display(), m_root,
        GCForeground | GCBackground | GCFunction | GCLineWidth | GCSubwindowMode | GCFont,
        &values);
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


void WindowManager::installCursor(RootCursor c)
{
    installCursorOnWindow(c, m_root);
}


void WindowManager::installCursorOnWindow(RootCursor c, Window w)
{
    XSetWindowAttributes attr;

    switch (c) {
    case RootCursor::Delete:    attr.cursor = m_xCursor;   break;
    case RootCursor::Down:      attr.cursor = m_vCursor;   break;
    case RootCursor::Right:     attr.cursor = m_hCursor;   break;
    case RootCursor::DownRight: attr.cursor = m_vhCursor;  break;
    case RootCursor::Normal:    attr.cursor = m_cursor;    break;
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

    for (auto *c : m_clients) {
        if (c->hasWindow(w)) return c;
    }

    if (!create) return nullptr;

    Client *newC = new Client(this, w);
    m_clients.push_back(newC);
    return newC;
}


void WindowManager::installColormap(Colormap cmap)
{
    if (cmap == None) {
        XInstallColormap(display(), m_defaultColormap);
    } else {
        XInstallColormap(display(), cmap);
    }
}


void WindowManager::clearFocus()
{
    static Window w = 0;
    Client *active = activeClient();

    // Focus-follows-pointer mode: just clear active client
    setActiveClient(nullptr);

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
    for (auto *client : m_clients) {
        if (client != c && client->revertTo() == c) {
            client->setRevertTo(myRevert);
        }
    }
}


void WindowManager::addToHiddenList(Client *c)
{
    for (auto *hc : m_hiddenClients) {
        if (hc == c) return;
    }
    m_hiddenClients.push_back(c);
}


void WindowManager::removeFromHiddenList(Client *c)
{
    for (size_t i = 0; i < m_hiddenClients.size(); ++i) {
        if (m_hiddenClients[i] == c) {
            m_hiddenClients.erase(m_hiddenClients.begin() + static_cast<ptrdiff_t>(i));
            return;
        }
    }
}


bool WindowManager::raiseTransients(Client *c)
{
    Client *first = nullptr;

    if (!c->isNormal()) return false;

    for (auto *client : m_clients) {
        if (client->isNormal() && client->isTransient()) {
            if (c->hasWindow(client->transientFor())) {
                if (!first) first = client;
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
                std::string envStr = "DISPLAY=";
                envStr += displayName;
                // putenv needs a mutable string that persists
                char *envBuf = new char[envStr.size() + 1];
                std::strcpy(envBuf, envStr.c_str());
                putenv(envBuf);
            }

            execlp("xterm", "xterm", static_cast<char*>(nullptr));
            std::fprintf(stderr, "wm2: exec xterm failed");
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

    // Start the auto-raise deadline (400ms from now) per D-03
    m_autoRaiseDeadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(400);
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
                m_pointerStoppedDeadline = now + milliseconds(80);
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
    return (ms > 0) ? static_cast<int>(ms) : 0;
}
