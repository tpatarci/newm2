#include "Manager.h"
#include "Client.h"
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <sys/select.h>


int WindowManager::loop()
{
    XEvent ev;
    m_looping = true;

    while (m_looping) {

        nextEvent(&ev);
        m_currentTime = CurrentTime;

        if (!m_looping) break;

        switch (ev.type) {

        case ButtonPress:
            eventButton(&ev.xbutton);
            break;

        case ButtonRelease:
            break;

        case MapRequest:
            eventMapRequest(&ev.xmaprequest);
            break;

        case ConfigureRequest:
            eventConfigureRequest(&ev.xconfigurerequest);
            break;

        case UnmapNotify:
            eventUnmap(&ev.xunmap);
            break;

        case CreateNotify:
            eventCreate(&ev.xcreatewindow);
            break;

        case DestroyNotify:
            eventDestroy(&ev.xdestroywindow);
            break;

        case ClientMessage:
            eventClient(&ev.xclient);
            break;

        case ColormapNotify:
            eventColormap(&ev.xcolormap);
            break;

        case PropertyNotify:
            eventProperty(&ev.xproperty);
            break;

        case SelectionClear:
            std::fprintf(stderr, "wm2: SelectionClear (this should not happen)\n");
            break;

        case SelectionNotify:
            std::fprintf(stderr, "wm2: SelectionNotify (this should not happen)\n");
            break;

        case SelectionRequest:
            std::fprintf(stderr, "wm2: SelectionRequest (this should not happen)\n");
            break;

        case EnterNotify:
        case LeaveNotify:
            eventEnter(&ev.xcrossing);
            break;

        case ReparentNotify:
            eventReparent(&ev.xreparent);
            break;

        case FocusIn:
            eventFocusIn(&ev.xfocus);
            break;

        case Expose:
            eventExposure(&ev.xexpose);
            break;

        case MotionNotify:
            if (m_focusChanging) {
                if (!m_focusPointerMoved) m_focusPointerMoved = true;
                else m_focusPointerNowStill = false;
            }
            break;

        case FocusOut:
        case ConfigureNotify:
        case MapNotify:
        case MappingNotify:
            break;

        default:
            if (ev.type == m_shapeEvent) {
                std::fprintf(stderr, "wm2: shaped windows are not supported\n");
            } else {
                std::fprintf(stderr, "wm2: unsupported event type %d\n", ev.type);
            }
            break;
        }
    }

    release();
    return m_returnCode;
}


void WindowManager::nextEvent(XEvent *e)
{
    int fd;
    fd_set rfds;
    struct timeval t;
    int r;

    if (!m_signalled) {

    waiting:

        if (QLength(display()) > 0) {
            XNextEvent(display(), e);
            return;
        }

        fd = ConnectionNumber(display());
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        t.tv_sec = 0;
        t.tv_usec = 0;

        if (select(fd + 1, &rfds, nullptr, nullptr, &t) == 1) {
            XNextEvent(display(), e);
            return;
        }

        XFlush(display());
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        t.tv_sec = 0;
        t.tv_usec = 20000;

        if ((r = select(fd + 1, &rfds, nullptr, nullptr,
                        (m_focusChanging) ? &t : nullptr)) == 1) {
            XNextEvent(display(), e);
            return;
        }

        if (m_focusChanging) {
            checkDelaysForFocus();
        }

        if (r == 0) goto waiting;

        if (errno != EINTR || !m_signalled) {
            perror("wm2: select failed");
            m_looping = false;
        }
    }

    std::fprintf(stderr, "wm2: signal caught, exiting\n");
    m_looping = false;
    m_returnCode = 0;
}


void WindowManager::eventMapRequest(XMapRequestEvent *e)
{
    Client *c = windowToClient(e->window);

    if (c) c->eventMapRequest(e);
    else {
        std::fprintf(stderr, "wm2: bad map request for window %lx\n", e->window);
    }
}


void WindowManager::eventConfigureRequest(XConfigureRequestEvent *e)
{
    XWindowChanges wc;
    Client *c = windowToClient(e->window);

    e->value_mask &= ~CWSibling;
    if (c) c->eventConfigureRequest(e);
    else {
        wc.x = e->x;
        wc.y = e->y;
        wc.width  = e->width;
        wc.height = e->height;
        wc.border_width = 0;
        wc.sibling = None;
        wc.stack_mode = Above;
        e->value_mask &= ~CWStackMode;
        e->value_mask |= CWBorderWidth;

        XConfigureWindow(display(), e->window, e->value_mask, &wc);
    }
}


void WindowManager::eventUnmap(XUnmapEvent *e)
{
    Client *c = windowToClient(e->window);
    if (c) c->eventUnmap(e);
}


void WindowManager::eventCreate(XCreateWindowEvent *e)
{
    if (e->override_redirect) return;
    windowToClient(e->window, true);
}


void WindowManager::eventDestroy(XDestroyWindowEvent *e)
{
    Client *c = windowToClient(e->window);

    if (c) {
        if (m_focusChanging && c == m_focusCandidate) {
            m_focusChanging = false;
        }

        // Remove from client list
        for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
            if (*it == c) {
                m_clients.erase(it);
                break;
            }
        }

        c->release();

        ignoreBadWindowErrors = true;
        XSync(display(), false);
        ignoreBadWindowErrors = false;
    }
}


void WindowManager::eventClient(XClientMessageEvent *e)
{
    Client *c = windowToClient(e->window);

    if (e->message_type == Atoms::wm_changeState) {
        if (c && e->format == 32 && e->data.l[0] == IconicState) {
            if (c->isNormal()) c->hide();
            return;
        }
    }

    std::fprintf(stderr, "wm2: unexpected XClientMessageEvent, type 0x%lx, "
                  "window 0x%lx\n", e->message_type, e->window);
}


void WindowManager::eventColormap(XColormapEvent *e)
{
    Client *c = windowToClient(e->window);

    if (e->c_new) {  // renamed from "new" in modern Xlib

        if (c) c->eventColormap(e);
        else {
            for (auto *client : m_clients) {
                client->eventColormap(e);
            }
        }
    }
}


void WindowManager::eventProperty(XPropertyEvent *e)
{
    Client *c = windowToClient(e->window);
    if (c) c->eventProperty(e);
}


void WindowManager::eventReparent(XReparentEvent *e)
{
    if (e->override_redirect) return;
    (void)windowToClient(e->window, true);
}


void WindowManager::eventEnter(XCrossingEvent *e)
{
    if (e->type != EnterNotify) return;

    while (XCheckMaskEvent(display(), EnterWindowMask, reinterpret_cast<XEvent*>(e)));
    m_currentTime = e->time;

    Client *c = windowToClient(e->window);
    if (c) c->eventEnter(e);
}


void WindowManager::eventFocusIn(XFocusInEvent *e)
{
    if (e->detail != NotifyNonlinearVirtual) return;
    Client *c = windowToClient(e->window);

    if (c) c->eventFocusIn(e);
}


void WindowManager::eventExposure(XExposeEvent *e)
{
    if (e->count != 0) return;
    Client *c = windowToClient(e->window);
    if (c) c->eventExposure(e);
}
