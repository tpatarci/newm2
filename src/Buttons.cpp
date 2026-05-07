#include "Manager.h"
#include "Client.h"
#include <cstdio>
#include <cstring>
#include <sys/time.h>
#include <vector>

#define AllButtonMask   ( Button1Mask | Button2Mask | Button3Mask \
                        | Button4Mask | Button5Mask )
#define ButtonMask      ( ButtonPressMask | ButtonReleaseMask )
#define DragMask        ( ButtonMask | ButtonMotionMask )
#define MenuMask        ( ButtonMask | ButtonMotionMask | ExposureMask )
#define MenuGrabMask    ( ButtonMask | ButtonMotionMask | StructureNotifyMask )


void WindowManager::eventButton(XButtonEvent *e)
{
    if (e->button == Button3) {
        circulate(e->window == e->root);
        return;
    }

    Client *c = windowToClient(e->window);

    if (e->window == e->root) {
        if (e->button == Button1) menu(e);
    } else if (c) {
        c->eventButton(e);
    }
}


void WindowManager::circulate(bool activeFirst)
{
    Client *c = nullptr;
    if (activeFirst) c = m_activeClient;

    if (!c) {
        int i = -1, j;

        if (!m_activeClient) {
            i = -1;
        } else {
            for (size_t idx = 0; idx < m_clients.size(); ++idx) {
                if (m_clients[idx].get() == m_activeClient) {
                    i = static_cast<int>(idx);
                    break;
                }
            }

            if (static_cast<size_t>(i) >= m_clients.size() - 1) i = -1;
        }

        for (j = i + 1;
             !m_clients[j]->isNormal() || m_clients[j]->isTransient();
             ++j) {
            if (static_cast<size_t>(j) >= m_clients.size() - 1) j = -1;
            if (j == i) return;
        }

        c = m_clients[j].get();
    }

    c->activateAndWarp();
}


int WindowManager::attemptGrab(Window w, Window constrain, int mask, int t)
{
    if (t == 0) t = static_cast<int>(timestamp(false));
    int status = XGrabPointer(display(), w, false, mask, GrabModeAsync,
                              GrabModeAsync, constrain, None, static_cast<Time>(t));
    return status;
}


static int nobuttons(XButtonEvent *e)
{
    int state = e->state & AllButtonMask;
    return (e->type == ButtonRelease) && (state & (state - 1)) == 0;
}


void WindowManager::releaseGrab(XButtonEvent *e)
{
    XEvent ev;
    if (!nobuttons(e)) {
        for (;;) {
            XMaskEvent(display(), ButtonMask | ButtonMotionMask, &ev);
            if (ev.type == MotionNotify) continue;
            e = &ev.xbutton;
            if (nobuttons(e)) break;
        }
    }

    XUngrabPointer(display(), e->time);
    m_currentTime = e->time;
}


void WindowManager::menu(XButtonEvent *e)
{
    if (e->window == m_menuWindow) return;

    std::vector<Client*> clients;
    bool allowExit = false;

    for (const auto& hc : m_hiddenClients) {
        clients.push_back(hc.get());
    }
    int nh = static_cast<int>(clients.size()) + 1;

    int n = static_cast<int>(clients.size()) + 1;

    int mx = DisplayWidth(display(), m_screenNumber) - 1;
    int my = DisplayHeight(display(), m_screenNumber) - 1;

    allowExit = ((e->x > mx - 3) && (e->y > my - 3));
    if (allowExit) n += 1;

    auto menuLabelFn = [&](int idx) -> const char* {
        if (idx == 0) return m_menuCreateLabel;
        if (allowExit && idx > static_cast<int>(clients.size())) return "[Exit wm2]";
        return clients[idx - 1]->label().c_str();
    };

    int width, maxWidth = 10;
    for (int i = 0; i < n; ++i) {
        width = XTextWidth(m_menuFont.get(), menuLabelFn(i),
                           static_cast<int>(std::strlen(menuLabelFn(i))));
        if (width > maxWidth) maxWidth = width;
    }
    maxWidth += 32;

    int selecting = -1, prev = -1;
    int entryHeight = m_menuFont->ascent + m_menuFont->descent + 4;
    int totalHeight = entryHeight * n + 13;
    int x = e->x - maxWidth / 2;
    int y = e->y - 2;
    bool warp = false;

    if (x < 0) {
        e->x -= x; x = 0; warp = true;
    } else if (x + maxWidth >= mx) {
        e->x -= x + maxWidth - mx; x = mx - maxWidth; warp = true;
    }

    if (y < 0) {
        e->y -= y; y = 0; warp = true;
    } else if (y + totalHeight >= my) {
        e->y -= y + totalHeight - my; y = my - totalHeight; warp = true;
    }

    if (warp) XWarpPointer(display(), None, root(),
                            None, None, None, None, e->x, e->y);

    XMoveResizeWindow(display(), m_menuWindow, x, y, maxWidth, totalHeight);
    XSelectInput(display(), m_menuWindow, MenuMask);
    XMapRaised(display(), m_menuWindow);

    if (attemptGrab(m_menuWindow, None, MenuGrabMask, e->time) != GrabSuccess) {
        XUnmapWindow(display(), m_menuWindow);
        return;
    }

    bool done = false;
    bool drawn = false;
    XEvent event;

    while (!done) {
        XMaskEvent(display(), MenuMask, &event);

        switch (event.type) {

        default:
            std::fprintf(stderr, "wm2: unknown event type %d\n", event.type);
            break;

        case ButtonPress:
            break;

        case ButtonRelease:
            {
                int sel = -1;
                if (drawn) {
                    if (event.xbutton.button != e->button) break;
                    x = event.xbutton.x;
                    y = event.xbutton.y - 11;
                    sel = y / entryHeight;

                    if (selecting >= 0 && y >= selecting * entryHeight - 3 &&
                        y <= (selecting + 1) * entryHeight - 3) sel = selecting;

                    if (x < 0 || x > maxWidth || y < -3) sel = -1;
                    else if (sel < 0 || sel >= n) sel = -1;
                }

                if (!nobuttons(&event.xbutton)) sel = -1;
                releaseGrab(&event.xbutton);
                XUnmapWindow(display(), m_menuWindow);
                selecting = sel;
                done = true;
            }
            break;

        case MotionNotify:
            if (!drawn) break;
            x = event.xbutton.x;
            y = event.xbutton.y - 11;
            prev = selecting;
            selecting = y / entryHeight;

            if (prev >= 0 && y >= prev * entryHeight - 3 &&
                y <= (prev + 1) * entryHeight - 3) selecting = prev;

            if (x < 0 || x > maxWidth || y < -3) selecting = -1;
            else if (selecting < 0 || selecting > n) selecting = -1;

            if (selecting == prev) break;

            if (prev >= 0 && prev < n) {
                XFillRectangle(display(), m_menuWindow, m_menuGC.get(),
                               4, prev * entryHeight + 9,
                               maxWidth - 8, entryHeight);
            }

            if (selecting >= 0 && selecting < n) {
                XFillRectangle(display(), m_menuWindow, m_menuGC.get(),
                               4, selecting * entryHeight + 9,
                               maxWidth - 8, entryHeight);
            }
            break;

        case Expose:
            XClearWindow(display(), m_menuWindow);

            XDrawRectangle(display(), m_menuWindow, m_menuGC.get(), 2, 7,
                           maxWidth - 5, totalHeight - 10);

            for (int i = 0; i < n; ++i) {
                int dx = XTextWidth(m_menuFont.get(), menuLabelFn(i),
                                    static_cast<int>(std::strlen(menuLabelFn(i))));
                int dy = i * entryHeight + m_menuFont->ascent + 10;

                if (i >= nh) {
                    XDrawString(display(), m_menuWindow, m_menuGC.get(),
                                maxWidth - 8 - dx, dy,
                                menuLabelFn(i),
                                static_cast<int>(std::strlen(menuLabelFn(i))));
                } else {
                    XDrawString(display(), m_menuWindow, m_menuGC.get(), 8, dy,
                                menuLabelFn(i),
                                static_cast<int>(std::strlen(menuLabelFn(i))));
                }
            }

            if (selecting >= 0 && selecting < n) {
                XFillRectangle(display(), m_menuWindow, m_menuGC.get(),
                               4, selecting * entryHeight + 9,
                               maxWidth - 8, entryHeight);
            }

            drawn = true;
        }
    }

    if (selecting == n - 1 && allowExit) {
        m_signalled = 1;
        return;
    }

    if (selecting >= 0) {
        if (selecting == 0) {
            spawn();
        } else if (selecting < nh) {
            clients[selecting - 1]->unhide(true);
        } else if (selecting < n) {
            clients[selecting - 1]->mapRaised();
            clients[selecting - 1]->ensureVisible();
        }
    }
}


void WindowManager::showGeometry(int x, int y)
{
    char string[20];
    std::sprintf(string, "%d %d\n", x, y);
    int width = XTextWidth(m_menuFont.get(), string,
                           static_cast<int>(std::strlen(string))) + 8;
    int height = m_menuFont->ascent + m_menuFont->descent + 8;
    int mx = DisplayWidth(display(), m_screenNumber) - 1;
    int my = DisplayHeight(display(), m_screenNumber) - 1;

    XMoveResizeWindow(display(), m_menuWindow,
                      (mx - width) / 2, (my - height) / 2, width, height);
    XClearWindow(display(), m_menuWindow);
    XMapRaised(display(), m_menuWindow);

    XDrawString(display(), m_menuWindow, m_menuGC.get(), 4,
                4 + m_menuFont->ascent, string,
                static_cast<int>(std::strlen(string)));
}


void WindowManager::removeGeometry()
{
    XUnmapWindow(display(), m_menuWindow);
}
