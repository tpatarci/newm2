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
        // CR-01/CR-03: Guard against empty client list to prevent
        // out-of-bounds access and unsigned underflow in size()-1
        if (m_clients.empty()) return;

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

            if (i < 0 || static_cast<size_t>(i) >= m_clients.size() - 1) i = -1;
        }

        // Restructured loop: check bounds BEFORE accessing m_clients[j]
        for (j = i + 1; ; ++j) {
            if (static_cast<size_t>(j) >= m_clients.size()) j = 0;
            if (j == i) return;
            if (m_clients[j]->isNormal() && !m_clients[j]->isTransient()) break;
        }

        c = m_clients[j].get();
    }

    // WR-05: Guard against null c (all clients transient/hidden)
    if (c) c->activateAndWarp();
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
        const char* label = menuLabelFn(i);
        int len = static_cast<int>(std::strlen(label));
        XGlyphInfo extents;
        XftTextExtentsUtf8(display(), m_menuFont,
            reinterpret_cast<const FcChar8*>(label), len, &extents);
        width = extents.width;
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

    // Create or rebind XftDraw for menu window
    if (!m_menuDraw) {
        m_menuDraw = x11::XftDrawPtr(XftDrawCreate(display(), m_menuWindow,
            DefaultVisual(display(), m_screenNumber),
            DefaultColormap(display(), m_screenNumber)));
    } else {
        XftDrawChange(m_menuDraw.get(), m_menuWindow);
    }

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
                // Unhighlight previous: fill with background color
                XftDrawRect(m_menuDraw.get(), m_menuBgColor.get(),
                            4, prev * entryHeight + 9,
                            maxWidth - 8, entryHeight);
            }

            if (selecting >= 0 && selecting < n) {
                // Highlight new selection: fill with highlight color
                XftDrawRect(m_menuDraw.get(), m_menuHlColor.get(),
                            4, selecting * entryHeight + 9,
                            maxWidth - 8, entryHeight);
            }
            break;

        case Expose:
            // Clear menu background
            XftDrawRect(m_menuDraw.get(), m_menuBgColor.get(), 0, 0,
                        maxWidth, totalHeight);

            // Draw border outline (4 sides, 1px each)
            XftDrawRect(m_menuDraw.get(), m_menuFgColor.get(), 2, 7,
                        maxWidth - 5, 1);         // top
            XftDrawRect(m_menuDraw.get(), m_menuFgColor.get(), 2,
                        totalHeight - 4, maxWidth - 5, 1);  // bottom
            XftDrawRect(m_menuDraw.get(), m_menuFgColor.get(), 2, 7,
                        1, totalHeight - 10);      // left
            XftDrawRect(m_menuDraw.get(), m_menuFgColor.get(),
                        maxWidth - 3, 7, 1, totalHeight - 10);  // right

            for (int i = 0; i < n; ++i) {
                const char* label = menuLabelFn(i);
                int len = static_cast<int>(std::strlen(label));
                XGlyphInfo extents;
                XftTextExtentsUtf8(display(), m_menuFont,
                    reinterpret_cast<const FcChar8*>(label), len, &extents);
                int dx = extents.width;
                int dy = i * entryHeight + m_menuFont->ascent + 10;

                if (i >= nh) {
                    // Right-aligned items (exit option)
                    XftDrawStringUtf8(m_menuDraw.get(), m_menuFgColor.get(),
                        m_menuFont, maxWidth - 8 - dx, dy,
                        reinterpret_cast<const FcChar8*>(label), len);
                } else {
                    // Left-aligned items
                    XftDrawStringUtf8(m_menuDraw.get(), m_menuFgColor.get(),
                        m_menuFont, 8, dy,
                        reinterpret_cast<const FcChar8*>(label), len);
                }
            }

            if (selecting >= 0 && selecting < n) {
                XftDrawRect(m_menuDraw.get(), m_menuHlColor.get(),
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
    int len = static_cast<int>(std::strlen(string));

    XGlyphInfo extents;
    XftTextExtentsUtf8(display(), m_menuFont,
        reinterpret_cast<const FcChar8*>(string), len, &extents);
    int width = extents.width + 8;
    int height = m_menuFont->ascent + m_menuFont->descent + 8;
    int mx = DisplayWidth(display(), m_screenNumber) - 1;
    int my = DisplayHeight(display(), m_screenNumber) - 1;

    XMoveResizeWindow(display(), m_menuWindow,
                      (mx - width) / 2, (my - height) / 2, width, height);

    // Create or rebind XftDraw for menu window
    if (!m_menuDraw) {
        m_menuDraw = x11::XftDrawPtr(XftDrawCreate(display(), m_menuWindow,
            DefaultVisual(display(), m_screenNumber),
            DefaultColormap(display(), m_screenNumber)));
    } else {
        XftDrawChange(m_menuDraw.get(), m_menuWindow);
    }

    // Clear background and draw text
    XftDrawRect(m_menuDraw.get(), m_menuBgColor.get(), 0, 0, width, height);
    XMapRaised(display(), m_menuWindow);

    XftDrawStringUtf8(m_menuDraw.get(), m_menuFgColor.get(),
        m_menuFont, 4, 4 + m_menuFont->ascent,
        reinterpret_cast<const FcChar8*>(string), len);
}


void WindowManager::removeGeometry()
{
    XUnmapWindow(display(), m_menuWindow);
}
