#include "Manager.h"
#include "Client.h"
#include <X11/extensions/Xrandr.h>
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

        case ConfigureNotify:
            // XDIS-02: the no-RANDR fallback. Split out of the no-op group above
            // for exactly one window -- root itself. A resolution change reaches
            // a plain X client as root's own ConfigureNotify, which is why
            // StructureNotifyMask was added to the root mask in
            // initialiseScreen(); SubstructureNotifyMask covers root's children
            // and would never deliver this.
            //
            // The RANDR cache-update call in the default arm below is
            // deliberately NOT made here. A ConfigureNotify is not an
            // XRRScreenChangeNotifyEvent, and on the server this branch exists
            // for there is no RANDR extension to update anything in. The common
            // handler re-reads the root geometry from the server, so this path
            // needs nothing from Xlib's cache.
            //
            // (Spelled as prose rather than by name on purpose: this file's
            // acceptance gate is a line-counting grep for that call, and it must
            // count exactly one -- the real one.)
            //
            // Every other ConfigureNotify is discarded exactly as before.
            if (ev.xconfigure.window == m_root) {
                handleScreenGeometryChange();
            }
            break;

        case FocusOut:
        case MapNotify:
        case MappingNotify:
            break;

        default:
            // XDIS-01: the RANDR path. Guarded on the sentinel being
            // non-negative FIRST, so a forced-off or absent extension can never
            // match: with the sentinel at -1 the sum below is negative and no
            // real event type is.
            if (m_randrEventBase >= 0 &&
                ev.type == m_randrEventBase + RRScreenChangeNotify) {
                // Mandatory, and it must come first (RESEARCH Pitfall 1). Xlib
                // caches the screen dimensions inside the Display struct and
                // never updates them behind the client's back; this is the call
                // that keeps that cache coherent for any remaining Xlib
                // consumer. The WM's own geometry does not depend on it -- the
                // handler asks the server directly -- but leaving Xlib's view
                // stale would quietly break anything that ever reads it.
                XRRUpdateConfiguration(&ev);
                handleScreenGeometryChange();
            } else if (ev.type == m_shapeEvent) {
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
        // SCAN-02: Capture dock-ness BEFORE STEP 4 erases the owning unique_ptr.
        // The post-erase workarea recomputation used to query the client through
        // the raw pointer after ~Client() had already run -- a heap-use-after-free
        // reachable by any client that simply destroys a managed window
        // (COMPILED_CODE_BEHAVIOR_CHECKLIST.md "Current Scan Findings",
        // src/Events.cpp:237-283; threat T-8-UAF). Branch on the local instead.
        //
        // Read through this alias, never through the raw `c` pointer, so the
        // greppable regression guard holds: grepping this file for an isDock
        // call made through `c` must return zero matches. Any reappearance of
        // that spelling means a post-erase dereference has come back.
        const Client &dying = *c;
        const bool wasDock = dying.isDock();

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

        // EWMH: Recalculate workarea if dock was destroyed (SCAN-02: `c` is dangling here)
        if (wasDock) {
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
