#include "Client.h"
#include "Manager.h"
#include "Border.h"
#include <X11/Xutil.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <poll.h>

const char *const Client::m_defaultLabel = "incognito";


void Client::fatal(const char *m) { m_windowManager->fatal(m); }
Display* Client::display() { return m_windowManager->display(); }
Client* Client::activeClient() const { return m_windowManager->activeClient(); }


Client::Client(WindowManager *wm, Window w)
    : m_window(w)
    , m_transient(None)
    , m_revert(nullptr)
    , m_x(0)
    , m_y(0)
    , m_w(1)
    , m_h(1)
    , m_bw(0)
    , m_fixedSize(false)
    , m_minWidth(50)
    , m_minHeight(50)
    , m_state(WithdrawnState)
    , m_protocol(0)
    , m_managed(false)
    , m_reparenting(false)
    , m_colormap(None)
    , m_colormapWinCount(0)
    , m_colormapWindows(nullptr)
    , m_windowColormaps(nullptr)
    , m_windowManager(wm)
{
    XWindowAttributes attr;
    XGetWindowAttributes(display(), m_window, &attr);

    m_x = attr.x;
    m_y = attr.y;
    m_w = attr.width;
    m_h = attr.height;
    m_bw = attr.border_width;
    m_sizeHints.flags = 0L;

    m_label = m_defaultLabel;
    m_border = std::make_unique<Border>(this, w);

    if (attr.map_state == IsViewable) manage(true);
}


Client::~Client()
{
    if (m_window != None) {
        release();  // safety net if caller forgot
    }
}


void Client::release()
{
    // Guard against double-release
    if (m_window == None) return;

    windowManager()->skipInRevert(this, m_revert);

    if (isHidden()) unhide(false);

    // Border auto-destroyed by unique_ptr
    m_border.reset();
    m_window = None;

    if (isActive()) {
        windowManager()->setActiveClient(nullptr);
    }

    if (m_colormapWinCount > 0) {
        XFree(m_colormapWindows);
        free(m_windowColormaps);
        m_colormapWinCount = 0;
        m_colormapWindows = nullptr;
        m_windowColormaps = nullptr;
    }
}


bool Client::isActive() const
{
    return activeClient() == this;
}


bool Client::hasWindow(Window w) const
{
    return (m_window == w) || (m_border && m_border->hasWindow(w));
}


Window Client::parent()
{
    return m_border ? m_border->parent() : m_window;
}


Window Client::root()
{
    return m_windowManager->root();
}


void Client::manage(bool mapped)
{
    bool shouldHide, reshape;
    Display *d = display();
    long mSize;
    int state;

    XSelectInput(d, m_window, ColormapChangeMask | EnterWindowMask |
                 PropertyChangeMask | FocusChangeMask);

    m_iconName = getProperty(XA_WM_ICON_NAME);
    m_name = getProperty(XA_WM_NAME);
    setLabel();

    getColormaps();
    getProtocols();
    getTransient();

    XWMHints *hints = XGetWMHints(d, m_window);

    if (!getState(&state)) {
        state = hints ? hints->initial_state : NormalState;
    }

    shouldHide = (state == IconicState);
    if (hints) XFree(hints);

    if (XGetWMNormalHints(d, m_window, &m_sizeHints, &mSize) == 0 ||
        m_sizeHints.flags == 0) {
        m_sizeHints.flags = PSize;
    }

    m_fixedSize = false;
    if ((m_sizeHints.flags & (PMinSize | PMaxSize)) == (PMinSize | PMaxSize) &&
        (m_sizeHints.min_width == m_sizeHints.max_width &&
         m_sizeHints.min_height == m_sizeHints.max_height)) {
        m_fixedSize = true;
    }

    reshape = !mapped;

    if (m_fixedSize) {
        if ((m_sizeHints.flags & USPosition)) reshape = false;
        if ((m_sizeHints.flags & PPosition) && shouldHide) reshape = false;
        if (m_transient != None) reshape = false;
    }

    if ((m_sizeHints.flags & PBaseSize)) {
        m_minWidth  = m_sizeHints.base_width;
        m_minHeight = m_sizeHints.base_height;
    } else if ((m_sizeHints.flags & PMinSize)) {
        m_minWidth  = m_sizeHints.min_width;
        m_minHeight = m_sizeHints.min_height;
    } else {
        m_minWidth = m_minHeight = 50;
    }

    // Act
    gravitate(false);

    int dw = DisplayWidth(display(), 0), dh = DisplayHeight(display(), 0);

    if (m_w < m_minWidth) {
        m_w = m_minWidth; m_fixedSize = false; reshape = true;
    }
    if (m_h < m_minHeight) {
        m_h = m_minHeight; m_fixedSize = false; reshape = true;
    }

    if (m_w > dw - 8) m_w = dw - 8;
    if (m_h > dh - 8) m_h = dh - 8;

    if (m_x > dw - m_border->xIndent()) m_x = dw - m_border->xIndent();
    if (m_y > dh - m_border->yIndent()) m_y = dh - m_border->yIndent();
    if (m_x < m_border->xIndent()) m_x = m_border->xIndent();
    if (m_y < m_border->yIndent()) m_y = m_border->yIndent();

    m_border->configure(m_x, m_y, m_w, m_h, 0L, Above);

    if (mapped) m_reparenting = true;
    if (reshape && !m_fixedSize) XResizeWindow(d, m_window, m_w, m_h);
    XSetWindowBorderWidth(d, m_window, 0);

    m_border->reparent();

    XAddToSaveSet(d, m_window);
    m_managed = true;

    if (shouldHide) {
        hide();
    } else {
        XMapWindow(d, m_window);
        m_border->map();
        setState(NormalState);

        // Focus follows pointer -- don't auto-activate on manage
        deactivate();
    }

    if (activeClient() && !isActive()) {
        activeClient()->installColormap();
    }
}


void Client::activate()
{
    if (parent() == root()) {
        std::fprintf(stderr, "wm2: warning: bad parent in Client::activate\n");
        return;
    }

    if (!m_managed || isHidden() || isWithdrawn()) return;

    if (isActive()) {
        decorate(true);
        return;
    }

    if (activeClient()) {
        activeClient()->deactivate();
    }

    XUngrabButton(display(), AnyButton, AnyModifier, parent());

    XSetInputFocus(display(), m_window, RevertToPointerRoot,
                   windowManager()->timestamp(false));

    if (m_protocol & static_cast<int>(Protocol::TakeFocus)) {
        sendMessage(Atoms::wm_protocols, Atoms::wm_takeFocus);
    }

    windowManager()->skipInRevert(this, m_revert);
    m_revert = activeClient();
    while (m_revert && !m_revert->isNormal()) m_revert = m_revert->revertTo();

    windowManager()->setActiveClient(this);
    decorate(true);
    installColormap();
}


void Client::deactivate()
{
    if (parent() == root()) {
        std::fprintf(stderr, "wm2: warning: bad parent in Client::deactivate\n");
        return;
    }

    XGrabButton(display(), AnyButton, AnyModifier, parent(), false,
                ButtonPressMask | ButtonReleaseMask,
                GrabModeAsync, GrabModeSync, None, None);

    decorate(false);
}


void Client::sendMessage(Atom a, long l)
{
    XEvent ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = m_window;
    ev.xclient.message_type = a;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = l;
    ev.xclient.data.l[1] = static_cast<long>(windowManager()->timestamp(false));
    long mask = 0L;
    int status = XSendEvent(display(), m_window, false, mask, &ev);

    if (status == 0) {
        std::fprintf(stderr, "wm2: warning: Client::sendMessage failed\n");
    }
}


void Client::sendConfigureNotify()
{
    XConfigureEvent ce;
    ce.type   = ConfigureNotify;
    ce.event  = m_window;
    ce.window = m_window;
    ce.x = m_x;
    ce.y = m_y;
    ce.width  = m_w;
    ce.height = m_h;
    ce.border_width = m_bw;
    ce.above = None;
    ce.override_redirect = 0;

    XSendEvent(display(), m_window, false, StructureNotifyMask,
               reinterpret_cast<XEvent*>(&ce));
}


static int getProperty_aux(Display *d, Window w, Atom a, Atom type, long len,
                           unsigned char **p)
{
    Atom realType;
    int format;
    unsigned long n, extra;
    int status;

    status = XGetWindowProperty(d, w, a, 0L, len, false, type, &realType,
                                &format, &n, &extra, p);

    if (status != Success || *p == 0) return -1;
    if (n == 0) XFree(*p);

    return static_cast<int>(n);
}


std::string Client::getProperty(Atom a)
{
    unsigned char *p = nullptr;
    if (getProperty_aux(display(), m_window, a, XA_STRING, 100L, &p) <= 0) {
        return {};
    }
    std::string result(reinterpret_cast<char*>(p));
    XFree(p);
    return result;
}


void Client::setState(int state)
{
    m_state = state;

    long data[2];
    data[0] = static_cast<long>(state);
    data[1] = static_cast<long>(None);

    XChangeProperty(display(), m_window, Atoms::wm_state, Atoms::wm_state,
                    32, PropModeReplace, reinterpret_cast<unsigned char*>(data), 2);
}


bool Client::getState(int *state)
{
    long *p = nullptr;

    if (getProperty_aux(display(), m_window, Atoms::wm_state, Atoms::wm_state,
                        2L, reinterpret_cast<unsigned char**>(&p)) <= 0) {
        return false;
    }

    *state = static_cast<int>(*p);
    XFree(p);
    return true;
}


void Client::getProtocols()
{
    long n;
    Atom *p;

    m_protocol = 0;
    if ((n = getProperty_aux(display(), m_window, Atoms::wm_protocols, XA_ATOM,
                             20L, reinterpret_cast<unsigned char**>(&p))) <= 0) {
        return;
    }

    for (int i = 0; i < n; ++i) {
        if (p[i] == Atoms::wm_delete) {
            m_protocol |= static_cast<int>(Protocol::Delete);
        } else if (p[i] == Atoms::wm_takeFocus) {
            m_protocol |= static_cast<int>(Protocol::TakeFocus);
        }
    }

    XFree(p);
}


void Client::getTransient()
{
    Window t = None;

    if (XGetTransientForHint(display(), m_window, &t) != 0) {
        if (windowManager()->windowToClient(t) == this) {
            std::fprintf(stderr,
                         "wm2: warning: client \"%s\" thinks it's a transient "
                         "for itself -- ignoring WM_TRANSIENT_FOR\n",
                         m_label.c_str());
            m_transient = None;
        } else {
            m_transient = t;
        }
    } else {
        m_transient = None;
    }
}


bool Client::setLabel()
{
    const char *newLabel;

    if (!m_name.empty()) newLabel = m_name.c_str();
    else if (!m_iconName.empty()) newLabel = m_iconName.c_str();
    else newLabel = m_defaultLabel;

    if (m_label != newLabel) {
        m_label = newLabel;
    }

    return true;
}


void Client::getColormaps()
{
    int n;
    Window *cw;
    XWindowAttributes attr;

    if (!m_managed) {
        XGetWindowAttributes(display(), m_window, &attr);
        m_colormap = attr.colormap;
    }

    n = getProperty_aux(display(), m_window, Atoms::wm_colormaps, XA_WINDOW,
                        100L, reinterpret_cast<unsigned char**>(&cw));

    if (m_colormapWinCount != 0) {
        XFree(m_colormapWindows);
        free(m_windowColormaps);
    }

    if (n <= 0) {
        m_colormapWinCount = 0;
        return;
    }

    m_colormapWinCount = n;
    m_colormapWindows = cw;

    m_windowColormaps = static_cast<Colormap*>(malloc(n * sizeof(Colormap)));

    if (!m_windowColormaps) {
        m_colormapWinCount = 0;
        XFree(m_colormapWindows);
        m_colormapWindows = nullptr;
        return;
    }

    for (int i = 0; i < n; ++i) {
        if (cw[i] == m_window) {
            m_windowColormaps[i] = m_colormap;
        } else {
            XSelectInput(display(), cw[i], ColormapChangeMask);
            XGetWindowAttributes(display(), cw[i], &attr);
            m_windowColormaps[i] = attr.colormap;
        }
    }
}


void Client::installColormap()
{
    Client *cc = nullptr;

    if (m_colormapWinCount != 0) {
        int found = 0;
        for (int i = m_colormapWinCount - 1; i >= 0; --i) {
            windowManager()->installColormap(m_windowColormaps[i]);
            if (m_colormapWindows[i] == m_window) ++found;
        }
        if (found == 0) {
            windowManager()->installColormap(m_colormap);
        }
    } else if (m_transient != None &&
               (cc = windowManager()->windowToClient(m_transient))) {
        cc->installColormap();
    } else {
        windowManager()->installColormap(m_colormap);
    }
}


void Client::gravitate(bool invert)
{
    int gravity = NorthWestGravity;
    int w = 0, h = 0, xdelta, ydelta;

    if (m_sizeHints.flags & PWinGravity) gravity = m_sizeHints.win_gravity;

    xdelta = m_bw - m_border->xIndent();
    ydelta = m_bw - m_border->yIndent();

    switch (gravity) {
    case NorthWestGravity: break;
    case NorthGravity:     w = xdelta; break;
    case NorthEastGravity:  w = xdelta + m_bw - 1; break;
    case WestGravity:       h = ydelta; break;
    case CenterGravity:
    case StaticGravity:     w = xdelta; h = ydelta; break;
    case EastGravity:       w = xdelta + m_bw - 1; h = ydelta; break;
    case SouthWestGravity:  h = ydelta + m_bw - 1; break;
    case SouthGravity:      w = xdelta; h = ydelta + m_bw - 1; break;
    case SouthEastGravity:  w = xdelta + m_bw - 1; h = ydelta + m_bw - 1; break;
    default:
        std::fprintf(stderr, "wm2: bad window gravity %d for window 0x%lx\n",
                     gravity, m_window);
        return;
    }

    w += m_border->xIndent();
    h += m_border->yIndent();

    if (invert) { w = -w; h = -h; }

    m_x += w;
    m_y += h;
}


void Client::unreparent()
{
    XWindowChanges wc;

    if (!isWithdrawn()) {
        gravitate(true);
        XReparentWindow(display(), m_window, root(), m_x, m_y);
    }

    wc.border_width = m_bw;
    XConfigureWindow(display(), m_window, CWBorderWidth, &wc);

    XSync(display(), true);
}


void Client::withdraw(bool changeState)
{
    m_border->unmap();

    gravitate(true);
    XReparentWindow(display(), m_window, root(), m_x, m_y);

    gravitate(false);

    if (changeState) {
        XRemoveFromSaveSet(display(), m_window);
        setState(WithdrawnState);
    }

    ignoreBadWindowErrors = true;
    XSync(display(), false);
    ignoreBadWindowErrors = false;
}


void Client::hide()
{
    if (isHidden()) {
        std::fprintf(stderr, "wm2: Client already hidden in Client::hide\n");
        return;
    }

    m_border->unmap();
    XUnmapWindow(display(), m_window);

    if (isActive()) windowManager()->clearFocus();

    setState(IconicState);
    windowManager()->addToHiddenList(this);
}


void Client::unhide(bool map)
{
    if (!isHidden()) {
        std::fprintf(stderr, "wm2: Client not hidden in Client::unhide\n");
        return;
    }

    windowManager()->removeFromHiddenList(this);

    if (map) {
        setState(NormalState);
        XMapWindow(display(), m_window);
        mapRaised();
    }
}


void Client::rename()
{
    m_border->configure(0, 0, m_w, m_h, CWWidth | CWHeight, Above);
}


void Client::mapRaised()
{
    m_border->mapRaised();
    windowManager()->raiseTransients(this);
}


void Client::kill()
{
    if (m_protocol & static_cast<int>(Protocol::Delete)) {
        sendMessage(Atoms::wm_protocols, Atoms::wm_delete);
    } else {
        XKillClient(display(), m_window);
    }
}


void Client::lower()
{
    m_border->lower();
}


void Client::ensureVisible()
{
    int mx = DisplayWidth(display(), 0) - 1;
    int my = DisplayHeight(display(), 0) - 1;
    int px = m_x;
    int py = m_y;

    if (m_x + m_w > mx) m_x = mx - m_w;
    if (m_y + m_h > my) m_y = my - m_h;
    if (m_x < 0) m_x = 0;
    if (m_y < 0) m_y = 0;

    if (m_x != px || m_y != py) m_border->moveTo(m_x, m_y);
}


void Client::decorate(bool active)
{
    m_border->decorate(active, m_w, m_h);
}


void Client::selectOnMotion(Window w, bool select)
{
    if (!w || w == root()) return;

    if (w == m_window || (m_border && m_border->hasWindow(w))) {
        XSelectInput(display(), m_window,
                     ColormapChangeMask | EnterWindowMask |
                     PropertyChangeMask | FocusChangeMask |
                     (select ? PointerMotionMask : 0L));
    } else {
        XSelectInput(display(), w, select ? PointerMotionMask : 0L);
    }
}


void Client::focusIfAppropriate(bool ifActive)
{
    if (!m_managed || !isNormal()) return;
    if (!ifActive && isActive()) return;

    Window rw, cw;
    int rx, ry, cx, cy;
    unsigned int k;

    XQueryPointer(display(), root(), &rw, &cw, &rx, &ry, &cx, &cy, &k);

    if (hasWindow(cw)) {
        activate();
        mapRaised();
        m_windowManager->stopConsideringFocus();
    }
}


void Client::activateAndWarp()
{
    mapRaised();
    ensureVisible();
    XWarpPointer(display(), None, parent(), 0, 0, 0, 0,
                 m_border->xIndent() / 2, m_border->xIndent() + 8);
    activate();
}


void Client::move(XButtonEvent *e)
{
    int x = -1, y = -1, xoff, yoff;
    bool done = false;

    if (windowManager()->attemptGrab(root(), None,
            ButtonPressMask | ButtonReleaseMask | ButtonMotionMask,
            e->time) != GrabSuccess) {
        return;
    }

    xoff = m_border->xIndent() - e->x;
    yoff = m_border->yIndent() - e->y;

    XEvent event;
    bool found;
    bool doSomething = false;

    while (!done) {

        found = false;

        while (XCheckMaskEvent(display(),
                ButtonPressMask | ButtonReleaseMask | ButtonMotionMask | ExposureMask,
                &event)) {
            found = true;
            if (event.type != MotionNotify) break;
        }

        if (!found) {
            poll(nullptr, 0, 50);
            continue;
        }

        switch (event.type) {

        default:
            std::fprintf(stderr, "wm2: unknown event type %d\n", event.type);
            break;

        case Expose:
            windowManager()->eventExposure(&event.xexpose);
            break;

        case ButtonPress:
            XUngrabPointer(display(), event.xbutton.time);
            doSomething = false;
            done = true;
            break;

        case ButtonRelease:
            {
                x = event.xbutton.x; y = event.xbutton.y;

                // Check all buttons released
                int state = event.xbutton.state & (Button1Mask | Button2Mask | Button3Mask |
                                                    Button4Mask | Button5Mask);
                if (event.xbutton.type == ButtonRelease && (state & (state - 1)) != 0) {
                    doSomething = false;
                }

                windowManager()->releaseGrab(&event.xbutton);
                done = true;
            }
            break;

        case MotionNotify:
            x = event.xbutton.x; y = event.xbutton.y;
            if (x + xoff != m_x || y + yoff != m_y) {
                windowManager()->showGeometry(x + xoff, y + yoff);
                m_border->moveTo(x + xoff, y + yoff);
                doSomething = true;
            }
            break;
        }
    }

    windowManager()->removeGeometry();

    if (x >= 0 && doSomething) {
        m_x = x + xoff;
        m_y = y + yoff;
    }

    m_border->moveTo(m_x, m_y);
    sendConfigureNotify();
}


void Client::resize(XButtonEvent *e, bool horizontal, bool vertical)
{
    if (isFixedSize()) return;

    int dragMask = ButtonPressMask | ButtonReleaseMask | ButtonMotionMask;

    if (windowManager()->attemptGrab(root(), None, dragMask, e->time) != GrabSuccess) {
        return;
    }

    if (vertical && horizontal)
        windowManager()->installCursor(WindowManager::RootCursor::DownRight);
    else if (vertical)
        windowManager()->installCursor(WindowManager::RootCursor::Down);
    else
        windowManager()->installCursor(WindowManager::RootCursor::Right);

    Window dummy;
    XTranslateCoordinates(display(), e->window, parent(),
                          e->x, e->y, &e->x, &e->y, &dummy);

    int x = e->x;
    int y = e->y;
    int w = m_w, h = m_h;
    int dw, dh;

    XEvent event;
    bool found;
    bool doSomething = false;
    bool done = false;

    while (!done) {

        found = false;

        while (XCheckMaskEvent(display(), dragMask | ExposureMask, &event)) {
            found = true;
            if (event.type != MotionNotify) break;
        }

        if (!found) {
            poll(nullptr, 0, 50);
            continue;
        }

        switch (event.type) {

        default:
            std::fprintf(stderr, "wm2: unknown event type %d\n", event.type);
            break;

        case Expose:
            windowManager()->eventExposure(&event.xexpose);
            break;

        case ButtonPress:
            XUngrabPointer(display(), event.xbutton.time);
            done = true;
            break;

        case ButtonRelease:
            {
                x = event.xbutton.x; y = event.xbutton.y;

                int state = event.xbutton.state & (Button1Mask | Button2Mask | Button3Mask |
                                                    Button4Mask | Button5Mask);
                if (event.xbutton.type == ButtonRelease && (state & (state - 1)) != 0) {
                    x = -1;
                }
                windowManager()->releaseGrab(&event.xbutton);
                done = true;
            }
            break;

        case MotionNotify:
            x = event.xbutton.x; y = event.xbutton.y;

            if (vertical && horizontal) {
                int prevH = h; h = y - m_y;
                int prevW = w; w = x - m_x;
                fixResizeDimensions(w, h, dw, dh);
                if (h != prevH || w != prevW) {
                    m_border->configure(m_x, m_y, w, h, CWWidth | CWHeight, 0);
                    windowManager()->showGeometry(dw, dh);
                    doSomething = true;
                }
            } else if (vertical) {
                int prevH = h; h = y - m_y;
                fixResizeDimensions(w, h, dw, dh);
                if (h != prevH) {
                    m_border->configure(m_x, m_y, w, h, CWHeight, 0);
                    windowManager()->showGeometry(dw, dh);
                    doSomething = true;
                }
            } else {
                int prevW = w; w = x - m_x;
                fixResizeDimensions(w, h, dw, dh);
                if (w != prevW) {
                    m_border->configure(m_x, m_y, w, h, CWWidth, 0);
                    windowManager()->showGeometry(dw, dh);
                    doSomething = true;
                }
            }
            break;
        }
    }

    if (doSomething) {
        windowManager()->removeGeometry();

        if (vertical && horizontal) {
            m_w = x - m_x; m_h = y - m_y;
            fixResizeDimensions(m_w, m_h, dw, dh);
            m_border->configure(m_x, m_y, m_w, m_h, CWWidth | CWHeight, 0, true);
        } else if (vertical) {
            m_h = y - m_y;
            fixResizeDimensions(m_w, m_h, dw, dh);
            m_border->configure(m_x, m_y, m_w, m_h, CWHeight, 0, true);
        } else {
            m_w = x - m_x;
            fixResizeDimensions(m_w, m_h, dw, dh);
            m_border->configure(m_x, m_y, m_w, m_h, CWWidth, 0, true);
        }

        XMoveResizeWindow(display(), m_window,
                          m_border->xIndent(), m_border->yIndent(), m_w, m_h);
        sendConfigureNotify();
    }

    windowManager()->installCursor(WindowManager::RootCursor::Normal);
}


void Client::moveOrResize(XButtonEvent *e)
{
    if (e->x < m_border->xIndent() && e->y > m_h) {
        resize(e, false, true);
    } else if (e->y < m_border->yIndent() &&
               e->x > m_w + m_border->xIndent() - m_border->yIndent()) {
        resize(e, true, false);
    } else {
        move(e);
    }
}


void Client::fixResizeDimensions(int &w, int &h, int &dw, int &dh)
{
    if (w < 50) w = 50;
    if (h < 50) h = 50;

    if (m_sizeHints.flags & PResizeInc) {
        w = m_minWidth  + (((w - m_minWidth) / m_sizeHints.width_inc) *
                           m_sizeHints.width_inc);
        h = m_minHeight + (((h - m_minHeight) / m_sizeHints.height_inc) *
                           m_sizeHints.height_inc);

        dw = (w - m_minWidth) / m_sizeHints.width_inc;
        dh = (h - m_minHeight) / m_sizeHints.height_inc;
    } else {
        dw = w; dh = h;
    }

    if (m_sizeHints.flags & PMaxSize) {
        if (w > m_sizeHints.max_width)  w = m_sizeHints.max_width;
        if (h > m_sizeHints.max_height) h = m_sizeHints.max_height;
    }

    if (w < m_minWidth)  w = m_minWidth;
    if (h < m_minHeight) h = m_minHeight;
}


// Event handlers called from WindowManager dispatch

void Client::eventMapRequest(XMapRequestEvent *)
{
    switch (m_state) {
    case WithdrawnState:
        if (parent() == root()) {
            manage(false);
            return;
        }
        m_border->reparent();
        XAddToSaveSet(display(), m_window);
        XMapWindow(display(), m_window);
        mapRaised();
        setState(NormalState);
        break;

    case NormalState:
        XMapWindow(display(), m_window);
        mapRaised();
        break;

    case IconicState:
        unhide(true);
        break;
    }
}


void Client::eventConfigureRequest(XConfigureRequestEvent *e)
{
    XWindowChanges wc;
    bool doRaise = false;

    e->value_mask &= ~CWSibling;
    gravitate(true);

    if (e->value_mask & CWX)      m_x = e->x;
    if (e->value_mask & CWY)      m_y = e->y;
    if (e->value_mask & CWWidth)  m_w = e->width;
    if (e->value_mask & CWHeight) m_h = e->height;
    if (e->value_mask & CWBorderWidth) m_bw = e->border_width;

    gravitate(false);

    if (e->value_mask & CWStackMode) {
        if (e->detail == Above) doRaise = true;
        e->value_mask &= ~CWStackMode;
    }

    if (parent() != root() && m_window == e->window) {
        m_border->configure(m_x, m_y, m_w, m_h, e->value_mask, e->detail);
        sendConfigureNotify();
    }

    if (m_managed) {
        wc.x = m_border->xIndent();
        wc.y = m_border->yIndent();
    } else {
        wc.x = e->x;
        wc.y = e->y;
    }

    wc.width = e->width;
    wc.height = e->height;
    wc.border_width = 0;
    wc.sibling = None;
    wc.stack_mode = Above;
    e->value_mask &= ~CWStackMode;
    e->value_mask |= CWBorderWidth;

    XConfigureWindow(display(), e->window, e->value_mask, &wc);

    if (doRaise && parent() != root()) {
        mapRaised();
    }
}


void Client::eventUnmap(XUnmapEvent *e)
{
    switch (m_state) {
    case IconicState:
        if (e->send_event) {
            unhide(false);
            withdraw();
        }
        break;

    case NormalState:
        if (isActive()) windowManager()->clearFocus();
        if (!m_reparenting) withdraw();
        break;
    }

    m_reparenting = false;
}


void Client::eventColormap(XColormapEvent *e)
{
    if (e->window == m_window || e->window == parent()) {
        m_colormap = e->colormap;
        if (isActive()) installColormap();
    } else {
        for (int i = 0; i < m_colormapWinCount; ++i) {
            if (m_colormapWindows[i] == e->window) {
                m_windowColormaps[i] = e->colormap;
                if (isActive()) installColormap();
                return;
            }
        }
    }
}


void Client::eventProperty(XPropertyEvent *e)
{
    Atom a = e->atom;
    bool shouldDelete = (e->state == PropertyDelete);

    switch (a) {
    case XA_WM_ICON_NAME:
        if (shouldDelete) m_iconName.clear();
        else m_iconName = getProperty(a);
        if (setLabel()) rename();
        return;

    case XA_WM_NAME:
        if (shouldDelete) m_name.clear();
        else m_name = getProperty(a);
        if (setLabel()) rename();
        return;

    case XA_WM_TRANSIENT_FOR:
        getTransient();
        return;
    }

    if (a == Atoms::wm_colormaps) {
        getColormaps();
        if (isActive()) installColormap();
    }
}


void Client::eventEnter(XCrossingEvent *e)
{
    if (e->type != EnterNotify) return;

    // Start auto-raise focus tracking (replaces immediate activate for
    // focus-follows-pointer; the auto-raise timer will activate after delay)
    windowManager()->considerFocusChange(this, m_window, e->time);
}


void Client::eventFocusIn(XFocusInEvent *e)
{
    if (m_window == e->window && !isActive()) {
        activate();
        mapRaised();
    }
}


void Client::eventExposure(XExposeEvent *e)
{
    if (m_border && m_border->hasWindow(e->window)) {
        m_border->expose(e);
    }
}


void Client::eventButton(XButtonEvent *e)
{
    if (e->type != ButtonPress) return;

    mapRaised();

    if (e->button == Button1) {
        if (m_border->hasWindow(e->window)) {
            m_border->eventButton(e);
        }
    }

    if (!isNormal() || isActive() || e->send_event) return;
    activate();
}
