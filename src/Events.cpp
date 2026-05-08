#include "Manager.h"
#include "Client.h"
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <poll.h>
#include <chrono>
#include <unistd.h>


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
                if (!m_focusPointerMoved) {
                    m_focusPointerMoved = true;
                    // First motion event: start the pointer-stopped timer
                    m_pointerStoppedDeadline =
                        std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(m_config.pointerStoppedDelay);
                    m_pointerStoppedDeadlineActive = true;
                } else {
                    m_focusPointerNowStill = false;
                }
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
    struct pollfd fds[2];
    fds[0].fd = ConnectionNumber(display());
    fds[0].events = POLLIN;
    fds[1].fd = m_pipeRead.get();
    fds[1].events = POLLIN;

    while (m_looping) {

        // Check Xlib's internal queue first (Xlib may have buffered events)
        if (QLength(display()) > 0) {
            XNextEvent(display(), e);
            return;
        }

        // Flush pending X output before blocking
        XFlush(display());

        int timeout = computePollTimeout();

        int r = poll(fds, 2, timeout);

        if (r < 0) {
            if (errno == EINTR) continue;  // signal interrupted, re-check m_looping
            std::perror("wm2: poll failed");
            m_looping = false;
            m_returnCode = 1;
            return;
        }

        // Signal pipe readable? (signal handler wrote a byte)
        if (fds[1].revents & POLLIN) {
            // Drain pipe (handler may have written multiple bytes)
            char buf[32];
            while (read(m_pipeRead.get(), buf, sizeof(buf)) > 0) { /* drain */ }
            std::fprintf(stderr, "wm2: signal caught, exiting\n");
            m_looping = false;
            m_returnCode = 0;
            return;
        }

        // Timer expired (r == 0 means timeout, no fd ready)
        if (r == 0) {
            if (m_focusChanging) {
                checkDelaysForFocus();
            }
            continue;  // re-check X11 queue, then poll again
        }

        // X11 fd readable
        if (fds[0].revents & POLLIN) {
            XNextEvent(display(), e);
            return;
        }
    }
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
        // STEP 1: Clear focus tracking BEFORE destroying the Client (Pitfall 1)
        if (m_focusChanging && c == m_focusCandidate) {
            stopConsideringFocus();
            m_focusCandidate = nullptr;
        }

        // STEP 2: Clear active client if this is it
        if (m_activeClient == c) {
            setActiveClient(nullptr);
        }

        // STEP 3: Remove from map first
        m_windowMap.erase(c->window());

        // CR-02: Suppress BadWindow errors BEFORE the erase triggers ~Client(),
        // which calls unreparent() -> XReparentWindow/XConfigureWindow on the
        // already-destroyed X11 window
        ignoreBadWindowErrors = true;

        // STEP 4: Find and erase from whichever vector owns the unique_ptr
        auto removeFrom = [c](auto& vec) -> bool {
            auto it = std::find_if(vec.begin(), vec.end(),
                [c](const auto& up) { return up.get() == c; });
            if (it != vec.end()) {
                vec.erase(it);
                return true;
            }
            return false;
        };

        if (!removeFrom(m_clients)) {
            removeFrom(m_hiddenClients);
        }
        // ~Client() runs here -- destructor handles unreparent, colormap cleanup
        // (BadWindow errors are now suppressed)

        // Update _NET_CLIENT_LIST after client removal
        updateClientList();

        // EWMH: Recalculate workarea if dock was destroyed
        if (c->isDock()) {
            updateWorkarea();
        }

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

    // EWMH: _NET_ACTIVE_WINDOW (per D-10, always grant)
    if (e->message_type == Atoms::net_activeWindow) {
        if (c && c->isNormal()) {
            c->activate();
        }
        return;
    }

    // EWMH: _NET_WM_STATE (Pitfall 3: honor add/remove/toggle semantics)
    if (e->message_type == Atoms::net_wmState) {
        if (c && e->format == 32) {
            int action = static_cast<int>(e->data.l[0]);  // 0=remove, 1=add, 2=toggle
            Atom prop1 = static_cast<Atom>(e->data.l[1]);
            Atom prop2 = static_cast<Atom>(e->data.l[2]);
            c->applyWmState(action, prop1, prop2);
        }
        return;
    }

    // Reduce noise: only warn for truly unexpected messages (not EWMH we intentionally ignore)
    if (e->message_type != Atoms::net_currentDesktop) {
        std::fprintf(stderr, "wm2: unexpected XClientMessageEvent, type 0x%lx, "
                      "window 0x%lx\n", e->message_type, e->window);
    }
}


void WindowManager::eventColormap(XColormapEvent *e)
{
    Client *c = windowToClient(e->window);

    if (e->c_new) {  // renamed from "new" in modern Xlib

        if (c) c->eventColormap(e);
        else {
            for (const auto& client : m_clients) {
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
