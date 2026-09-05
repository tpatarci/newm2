#include "Manager.h"
#include "Client.h"
#include "MenuPaint.h"
#include <cstdio>
#include <cstring>
#include <sys/time.h>
#include <algorithm>
#include <functional>
#include <iterator>
#include <vector>

#define AllButtonMask   ( Button1Mask | Button2Mask | Button3Mask \
                        | Button4Mask | Button5Mask )
#define ButtonMask      ( ButtonPressMask | ButtonReleaseMask )
#define DragMask        ( ButtonMask | ButtonMotionMask )
#define MenuMask        ( ButtonMask | ButtonMotionMask | ExposureMask )
// Pointer-grab masks are SETofPOINTEREVENT on the wire: the GrabPointer
// request carries the event mask as a CARD16, so Xlib silently drops any bit
// above bit 15. StructureNotifyMask is bit 17 and was never in effect here
// (WINDOWS.md ledger 11). Structure events on a grab window are selected with
// XSelectInput, never through the grab.
#define MenuGrabMask    ( ButtonMask | ButtonMotionMask )


void WindowManager::eventButton(XButtonEvent *e)
{
    // FOCUS-01 (plan 08-08): this is the last-user-interaction clock, and this
    // is the ONLY place that feeds it. It sits above the root-menu/client
    // dispatch so that every real button press counts, including the ones that
    // open the root menu.
    //
    // Two deliberate restrictions, both of which look like oversights if they
    // are not written down:
    //
    //  - Crossing events do NOT feed this clock. Moving the pointer over a
    //    window is not the user action the EWMH means; if crossings counted,
    //    any pop-up appearing under a drifting pointer would look
    //    user-initiated and the whole feature would be defeated.
    //  - Synthetic presses do NOT feed it either (threat T-8-FOCUS: "a clock
    //    fed only by real button presses"). XSendEvent lets any client deliver
    //    a ButtonPress to root carrying a timestamp of its choosing; a forged
    //    far-future value would make every honest client's user-time look stale
    //    and deny focus desktop-wide. Client::eventButton() already refuses
    //    send_event for the same reason.
    //
    // If keyboard handling ever lands, key presses feed this clock too -- they
    // are user interaction in exactly the sense meant here.
    if (!e->send_event) noteUserInteraction(e->time);

    if (e->window == e->root) {
        if (e->button == Button1) menu(e);
        else if (e->button == Button3) circulate(true);  // root right-click: circulate active first
        return;
    }

    Client *c = windowToClient(e->window);
    if (c) {
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

        int i = -1;

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

        // Bounded scan: every entry is examined AT MOST ONCE, starting just
        // after the active client's index and wrapping.
        //
        // The previous form (`for (j = i + 1; ; ++j)`, wrapping j to 0 and
        // exiting only on `j == i`) was unbounded whenever nothing satisfied
        // the break condition: j is always wrapped into [0, size), so it can
        // never equal -1, and i IS -1 when there is no active client. The WM
        // then froze at 100% CPU permanently -- measured at 299 ticks over 3s
        // of wall clock -- and stopped answering SIGTERM, because the signal
        // handler only sets a flag the event loop never gets back to checking.
        // A right-click on the root of a freshly started WM was enough to
        // reach it: m_clients is non-empty (the WM adopts its own menu and
        // WM-check windows) but holds nothing in Normal state.
        // deferred-items.md item 6; regression test [wm_circulate].
        //
        // Behaviour when an eligible client DOES exist is unchanged. With
        // i >= 0 the step range 1..n visits i+1 ... i+n-1 and then hits j == i
        // on the final step, which is exactly the set of entries the old loop
        // examined before its `j == i` return. With i == -1 it visits 0 ... n-1
        // once each and then stops, which is what the old loop should have done
        // and never did.
        const int n = static_cast<int>(m_clients.size());
        int found = -1;

        for (int step = 1; step <= n; ++step) {
            const int j = (i + step) % n;
            if (j == i) break;
            if (m_clients[j]->isNormal() && !m_clients[j]->isTransient()) {
                found = j;
                break;
            }
        }

        // No eligible client anywhere in the list: there is nothing to
        // circulate to, so leave the active window alone rather than spinning.
        if (found < 0) return;

        c = m_clients[found].get();
    }

    // WR-05: Guard against null c (all clients transient/hidden)
    if (c) c->activateAndWarp();
}


int WindowManager::attemptGrab(Window w, Window constrain, int mask, Time t)
{
    // `t` is a Time, not an int, and that is a correctness requirement rather
    // than a tidiness one. X server timestamps are 32-bit unsigned milliseconds
    // since server start. Routed through a signed int they turn negative after
    // ~24.8 days of server uptime, and the subsequent widening cast to Time
    // sign-extends that to a value far in the future. X ignores any grab or
    // ungrab whose time is later than the current server time, so past that
    // uptime every grab in this file would have failed silently -- on a VPS
    // droplet that is left running, which is this project's whole target.
    if (t == 0) t = timestamp(false);
    int status = XGrabPointer(display(), w, false, mask, GrabModeAsync,
                              GrabModeAsync, constrain, None, t);
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
            // Ledger 8: interruptible. On SIGTERM the grab is simply released
            // below with the press event's own time.
            if (modalWait(ButtonMask | ButtonMotionMask, &ev, -1) != ModalWait::Event) break;
            if (ev.type == MotionNotify) continue;
            e = &ev.xbutton;
            if (nobuttons(e)) break;
        }
    }

    XUngrabPointer(display(), e->time);
    m_currentTime = e->time;
}


// ===========================================================================
// The root menu, and its app-category submenu, as ONE interaction.
//
// The shape of this code is the point, so it is worth stating plainly.
//
// ONE grab, taken once when the menu opens and released once when it closes.
// ONE event loop. The submenu is a STATE of that loop, not a second loop with a
// grab of its own.
//
// The previous design did the opposite: openCategorySubmenu() ungrabbed the
// outer menu, took its own grab on m_submenuWindow, and ran a nested event loop
// until release. Because that grab used owner_events=False, every pointer event
// from then on belonged to the submenu -- so moving back over the OUTER menu
// delivered motion to the submenu's loop, which computed coordinates relative to
// itself, found the pointer outside, and highlighted nothing. Meanwhile the
// outer menu kept whatever highlight it had when you left it, frozen, because
// nothing was listening for it any more. Reported from a real XRDP session as
// "once a submenu has been activated, the main menu cannot be re-activated: it
// remains static", and reproducible on plain Xvfb. There was no way back: the
// only exit was releasing the button, which closed both popups together.
//
// The invariants this replacement holds, and why each one is here:
//
//  1. Exactly one XGrabPointer and one XUngrabPointer per menu interaction.
//     No handing the grab back and forth. Every ungrab/regrab pair is a window
//     in which another client can take the pointer and leave this code in a
//     state it has no way to detect, and the old code had two such windows on
//     the normal path.
//
//  2. Pointer position is read in ROOT coordinates (x_root/y_root) and
//     hit-tested against the rectangles this function itself chose when it
//     mapped each popup. It is never inferred from which window the grab
//     happens to be on. That is what makes "which popup is the pointer over"
//     answerable at all times, for both popups at once, from one loop.
//
//  3. The highlight model is updated on every motion event, whether or not the
//     popup has been exposed yet. Drawing is skipped until the first Expose --
//     drawing into a not-yet-viewable window is discarded by the server -- but
//     the MODEL always moves, and the Expose handler paints from the model. The
//     old code dropped motion entirely while undrawn, and a dropped motion is
//     never replayed: the pointer is already where it was put, so no further
//     event is generated and the highlight was simply wrong until the user
//     moved again.
//
//  4. Every path that fills a row rectangle redraws that row's label
//     afterwards, through one shared helper per popup. Filling paints over the
//     text; before this existed the hovered row went blank and the row you had
//     just left stayed blank.
//
//  5. The submenu is unmapped whenever the pointer selects a different outer
//     row, and the outer menu keeps its category row highlighted while the
//     pointer is inside the submenu. Leaving both popups clears the selections
//     but leaves the submenu mapped, so travelling diagonally from the category
//     row into the submenu does not dismiss it mid-journey.
// ===========================================================================
void WindowManager::menu(XButtonEvent *e)
{
    if (e->window == m_menuWindow || e->window == m_submenuWindow) return;

    std::vector<Client*> clients;
    clients.reserve(m_hiddenClients.size());
    std::transform(m_hiddenClients.begin(), m_hiddenClients.end(),
                   std::back_inserter(clients),
                   [](const std::unique_ptr<Client>& hc) { return hc.get(); });

    const int nh = static_cast<int>(clients.size()) + 1;
    const int numCategories = static_cast<int>(m_appCategories.size());
    int n = static_cast<int>(clients.size()) + 1 + numCategories;

    const int mx = screenWidth() - 1;
    const int my = screenHeight() - 1;

    const bool allowExit = ((e->x > mx - 3) && (e->y > my - 3));
    if (allowExit) n += 1;

    auto outerLabel = [&](int idx) -> const char* {
        if (idx == 0) return m_menuCreateLabel;
        if (idx < nh) return clients[idx - 1]->label().c_str();
        if (idx < nh + numCategories) return m_appCategories[idx - nh].first.c_str();
        if (allowExit && idx == n - 1) return "[Exit wm2]";
        return clients[idx - 1]->label().c_str();
    };

    const int entryHeight = m_menuFont->ascent + m_menuFont->descent + 4;

    // One width measurement, used for the outer menu and every submenu, so the
    // two can never drift apart in padding or font metrics.
    auto measureWidth = [&](const std::function<const char*(int)>& labelFn, int count) {
        int maxW = 10;
        for (int i = 0; i < count; ++i) {
            const char* label = labelFn(i);
            XGlyphInfo ext;
            XftTextExtentsUtf8(display(), m_menuFont,
                reinterpret_cast<const FcChar8*>(label),
                static_cast<int>(std::strlen(label)), &ext);
            if (static_cast<int>(ext.width) > maxW) maxW = static_cast<int>(ext.width);
        }
        return maxW + 32;
    };

    const int outerW = measureWidth(outerLabel, n);
    const int outerH = entryHeight * n + 13;

    int x = e->x - outerW / 2;
    int y = e->y - 2;
    bool warp = false;

    if (x < 0) {
        e->x -= x; x = 0; warp = true;
    } else if (x + outerW >= mx) {
        e->x -= x + outerW - mx; x = mx - outerW; warp = true;
    }

    if (y < 0) {
        e->y -= y; y = 0; warp = true;
    } else if (y + outerH >= my) {
        e->y -= y + outerH - my; y = my - outerH; warp = true;
    }

    if (warp) XWarpPointer(display(), None, root(),
                            None, None, None, None, e->x, e->y);

    // The popup's screen-space origin. Invariant 2 above: every hit test is
    // done against these, in root coordinates.
    const int outerX = x;
    const int outerY = y;

    XMoveResizeWindow(display(), m_menuWindow, outerX, outerY, outerW, outerH);
    XSelectInput(display(), m_menuWindow, MenuMask);
    XMapRaised(display(), m_menuWindow);

    if (!m_menuDraw) {
        m_menuDraw = x11::XftDrawPtr(XftDrawCreate(display(), m_menuWindow,
            DefaultVisual(display(), m_screenNumber),
            DefaultColormap(display(), m_screenNumber)));
    } else {
        XftDrawChange(m_menuDraw.get(), m_menuWindow);
    }

    // ---- interaction state -------------------------------------------------
    // Everything the loop needs to answer "where is the pointer, and what is
    // currently on screen" lives here, in one place, rather than being split
    // across two functions and two stack frames.
    int  outerSel   = -1;        // highlighted outer row, -1 for none
    bool outerDrawn = false;     // has the outer menu had its first Expose?

    int  openCat    = -1;        // index into m_appCategories, -1 = no submenu
    int  subX = 0, subY = 0, subW = 0, subH = 0, n2 = 0;
    int  subSel     = -1;
    bool subDrawn   = false;
    const std::vector<AppEntry>* subEntries = nullptr;

    // Row hit test, including the stickiness band the original had: once a row
    // is selected it keeps the selection across a few pixels of overshoot, so a
    // slightly unsteady pointer does not flicker between neighbours.
    auto rowAt = [&](int localY, int count, int current) -> int {
        const int ry = localY - 11;
        if (ry < -3) return -1;
        int r = ry / entryHeight;
        if (current >= 0 && ry >= current * entryHeight - 3 &&
            ry <= (current + 1) * entryHeight - 3) r = current;
        if (r < 0 || r >= count) return -1;
        return r;
    };

    // Invariant 4: filling a row paints over its label, so every fill is
    // followed by the matching redraw. One definition per popup, used by the
    // Expose path and the motion path alike, so a row drawn on hover cannot
    // drift out of step with the same row drawn on exposure.
    auto drawOuterRowLabel = [&](int i) {
        if (i < 0 || i >= n) return;
        const char* label = outerLabel(i);
        const int len = static_cast<int>(std::strlen(label));
        XGlyphInfo ext;
        XftTextExtentsUtf8(display(), m_menuFont,
            reinterpret_cast<const FcChar8*>(label), len, &ext);
        const int dy = i * entryHeight + m_menuFont->ascent + 10;
        // The Exit row is right-aligned; category rows also sit at idx >= nh
        // but stay left-aligned, so the test is on the Exit row specifically.
        const int dx = (allowExit && i == n - 1)
                     ? outerW - 8 - static_cast<int>(ext.width)
                     : 8;
        XftDrawStringUtf8(m_menuDraw.get(), m_menuFgColor.get(),
            m_menuFont, dx, dy, reinterpret_cast<const FcChar8*>(label), len);
    };

    auto drawSubRowLabel = [&](int i) {
        if (!subEntries || i < 0 || i >= n2) return;
        const char* label = (*subEntries)[i].name.c_str();
        const int len = static_cast<int>(std::strlen(label));
        const int dy = i * entryHeight + m_menuFont->ascent + 10;
        XftDrawStringUtf8(m_submenuDraw.get(), m_menuFgColor.get(),
            m_menuFont, 8, dy, reinterpret_cast<const FcChar8*>(label), len);
    };

    // Full repaint. Order is load-bearing: background, border, THEN the
    // highlight, THEN every label -- so the text lands on top of the highlight
    // rather than under it.
    auto paintPopup = [&](XftDraw* draw, int w, int h, int count, int sel,
                          const std::function<void(int)>& drawRow) {
        XftDrawRect(draw, m_menuBgColor.get(), 0, 0, w, h);

        XftDrawRect(draw, m_menuFgColor.get(), 2, 7, w - 5, 1);            // top
        XftDrawRect(draw, m_menuFgColor.get(), 2, h - 4, w - 5, 1);        // bottom
        XftDrawRect(draw, m_menuFgColor.get(), 2, 7, 1, h - 10);           // left
        XftDrawRect(draw, m_menuFgColor.get(), w - 3, 7, 1, h - 10);       // right

        if (sel >= 0 && sel < count) {
            XftDrawRect(draw, m_menuHlColor.get(),
                        4, sel * entryHeight + 9, w - 8, entryHeight);
        }
        for (int i = 0; i < count; ++i) drawRow(i);
    };

    auto paintOuter = [&]() {
        paintPopup(m_menuDraw.get(), outerW, outerH, n, outerSel, drawOuterRowLabel);
    };
    auto paintSub = [&]() {
        if (openCat < 0) return;
        paintPopup(m_submenuDraw.get(), subW, subH, n2, subSel, drawSubRowLabel);
    };

    // Invariant 3: the model always moves; only the drawing is conditional on
    // the popup having been exposed.
    auto setOuterSel = [&](int next) {
        if (next == outerSel) return;
        const int prev = outerSel;
        outerSel = next;
        if (!outerDrawn) return;
        if (prev >= 0 && prev < n) {
            XftDrawRect(m_menuDraw.get(), m_menuBgColor.get(),
                        4, prev * entryHeight + 9, outerW - 8, entryHeight);
            drawOuterRowLabel(prev);
        }
        if (outerSel >= 0 && outerSel < n) {
            XftDrawRect(m_menuDraw.get(), m_menuHlColor.get(),
                        4, outerSel * entryHeight + 9, outerW - 8, entryHeight);
            drawOuterRowLabel(outerSel);
        }
    };

    auto setSubSel = [&](int next) {
        if (next == subSel) return;
        const int prev = subSel;
        subSel = next;
        if (!subDrawn || openCat < 0) return;
        if (prev >= 0 && prev < n2) {
            XftDrawRect(m_submenuDraw.get(), m_menuBgColor.get(),
                        4, prev * entryHeight + 9, subW - 8, entryHeight);
            drawSubRowLabel(prev);
        }
        if (subSel >= 0 && subSel < n2) {
            XftDrawRect(m_submenuDraw.get(), m_menuHlColor.get(),
                        4, subSel * entryHeight + 9, subW - 8, entryHeight);
            drawSubRowLabel(subSel);
        }
    };

    auto closeSubmenu = [&]() {
        if (openCat < 0) return;
        XUnmapWindow(display(), m_submenuWindow);
        openCat    = -1;
        subSel     = -1;
        subDrawn   = false;
        subEntries = nullptr;
        n2         = 0;
    };

    auto openSubmenu = [&](int catIdx, int rowIndex) {
        const auto& cat = m_appCategories[catIdx];
        if (cat.second.empty()) return;   // nothing to show; leave it closed

        subEntries = &cat.second;
        n2 = static_cast<int>(subEntries->size());

        auto subLabel = [&](int i) -> const char* {
            return (*subEntries)[i].name.c_str();
        };
        subW = measureWidth(subLabel, n2);
        subH = entryHeight * n2 + 13;

        // Anchored to the right of the outer menu at the hovered row's top
        // edge, flipping to the left if it would cross the right screen edge --
        // the same edge-avoidance the outer menu applies to itself.
        subX = outerX + outerW;
        subY = outerY + rowIndex * entryHeight;
        if (subX + subW >= mx) subX = outerX - subW;
        if (subX < 0) subX = 0;
        if (subX + subW >= mx) subX = mx - subW;
        if (subY + subH >= my) subY = my - subH;
        if (subY < 0) subY = 0;

        XMoveResizeWindow(display(), m_submenuWindow, subX, subY, subW, subH);
        XSelectInput(display(), m_submenuWindow, MenuMask);
        XMapRaised(display(), m_submenuWindow);

        if (!m_submenuDraw) {
            m_submenuDraw = x11::XftDrawPtr(XftDrawCreate(display(), m_submenuWindow,
                DefaultVisual(display(), m_screenNumber),
                DefaultColormap(display(), m_screenNumber)));
        } else {
            XftDrawChange(m_submenuDraw.get(), m_submenuWindow);
        }

        openCat  = catIdx;
        subSel   = -1;
        subDrawn = false;   // wait for Expose before drawing into it
    };

    // The whole pointer policy, in one place, driven by root coordinates.
    auto pointerAt = [&](int rx, int ry) {
        const bool inSub = (openCat >= 0) &&
                           rx >= subX && rx < subX + subW &&
                           ry >= subY && ry < subY + subH;

        if (inSub) {
            // Inside the submenu the OUTER selection deliberately stays put, so
            // the category row you came from remains highlighted.
            setSubSel(rowAt(ry - subY, n2, subSel));
            return;
        }

        setSubSel(-1);

        const bool inOuter = rx >= outerX && rx < outerX + outerW &&
                             ry >= outerY && ry < outerY + outerH;
        if (!inOuter) {
            // Outside both popups: clear the outer highlight but leave the
            // submenu MAPPED, so a diagonal path from the category row into the
            // submenu does not dismiss it halfway across.
            setOuterSel(-1);
            return;
        }

        const int row = rowAt(ry - outerY, n, outerSel);
        setOuterSel(row);

        if (row >= nh && row < nh + numCategories) {
            const int cat = row - nh;
            if (cat != openCat) {
                closeSubmenu();
                openSubmenu(cat, row);
            }
        } else {
            closeSubmenu();
        }
    };

    // Invariant 1: this is the only grab, and releaseGrab() below is the only
    // ungrab, for the entire interaction.
    if (attemptGrab(m_menuWindow, None, MenuGrabMask, e->time) != GrabSuccess) {
        XUnmapWindow(display(), m_menuWindow);
        return;
    }

    bool done = false;
    int  chosenOuter = -1;
    bool haveChosenApp = false;
    AppEntry chosenApp;
    XEvent event;

    while (!done) {
        if (modalWait(MenuMask, &event, -1) != ModalWait::Event) {
            // Ledger 8: SIGTERM (or the Exit action's own wake) while the menu
            // is held. Leave with nothing chosen, cleaned up exactly as the
            // release path below cleans up; the main loop then observes the
            // flag and shuts down.
            XUngrabPointer(display(), CurrentTime);
            closeSubmenu();
            XUnmapWindow(display(), m_menuWindow);
            done = true;
            break;
        }

        switch (event.type) {

        default:
            std::fprintf(stderr, "wm2: unknown event type %d\n", event.type);
            break;

        case ButtonPress:
            break;

        case Expose:
            // The mapping lives in include/MenuPaint.h so a display-free test
            // can reach it (08.5-13 Task 1). Naming the fallthrough is what
            // made it possible to fix it: Task 3 gives Foreign a real arm
            // instead of the silence it inherited.
            //
            // The arms that paint no popup lead, so the case the tests assert
            // on is the first thing a reader meets rather than the last.
            switch (menuPaintTargetFor(event.xexpose.window, m_menuWindow,
                                       m_submenuWindow, openCat)) {
            case MenuPaintTarget::Foreign:
                // Closes ledger entry 9. This Expose belongs to some window
                // that is neither popup -- typically a managed client uncovered
                // while the menu is up. The loop selects ExposureMask, so the
                // event is genuinely delivered here; before 08.5-13 the chain
                // simply fell off its end and dropped it, and the client's
                // frame and tab label stayed blank until some later unrelated
                // Expose repainted them.
                //
                // Routing it to the ordinary handler is safe from inside the
                // grab: eventExposure() only looks the window up in the client
                // list and asks that client to repaint itself. It touches
                // neither the grab, the mask, nor this loop's termination.
                eventExposure(&event.xexpose);
                break;
            case MenuPaintTarget::NoTarget:
                // An Expose carrying no window at all. There is nothing to
                // route it to and nothing to paint.
                break;
            case MenuPaintTarget::Outer:
                outerDrawn = true;
                paintOuter();
                break;
            case MenuPaintTarget::Submenu:
                subDrawn = true;
                paintSub();
                break;
            }
            break;

        case MotionNotify:
            pointerAt(event.xmotion.x_root, event.xmotion.y_root);
            break;

        case ButtonRelease:
            if (event.xbutton.button != e->button) break;

            // Settle the selection against the release position before acting
            // on it: the release may carry a position no motion event reported.
            pointerAt(event.xbutton.x_root, event.xbutton.y_root);

            if (nobuttons(&event.xbutton)) {
                if (openCat >= 0 && subSel >= 0 && subEntries) {
                    // Copied, not referenced: closeSubmenu() below drops
                    // subEntries, and the dispatch happens after the loop.
                    chosenApp     = (*subEntries)[subSel];
                    haveChosenApp = true;
                } else {
                    chosenOuter = outerSel;
                }
            }

            releaseGrab(&event.xbutton);
            closeSubmenu();
            XUnmapWindow(display(), m_menuWindow);
            done = true;
            break;
        }
    }

    // Dispatch happens after everything is unmapped and ungrabbed, so an action
    // that itself opens windows or blocks cannot do so underneath a live grab.
    if (haveChosenApp) {
        launchApp(chosenApp);
        return;
    }

    if (chosenOuter < 0) return;

    if (allowExit && chosenOuter == n - 1) {
        m_signalled = 1;
        // Wake a blocked poll() the same way a signal does. The loop now
        // observes m_signalled, but if it is already parked in poll() with no
        // timer armed nothing would arrive to make it look.
        wakeEventLoop();
        return;
    }

    if (chosenOuter == 0) {
        spawn();
    } else if (chosenOuter < nh) {
        clients[chosenOuter - 1]->unhide(true);
    } else if (chosenOuter < nh + numCategories) {
        // A category row released with nothing chosen in its submenu: the
        // submenu opens on hover, so this means "backed out". Nothing to do.
    } else if (chosenOuter < n) {
        clients[chosenOuter - 1]->mapRaised();
        clients[chosenOuter - 1]->ensureVisible();
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
    int mx = screenWidth() - 1;
    int my = screenHeight() - 1;

    // D-34: the readout owns m_geometryWindow. It used to borrow m_menuWindow,
    // so a drag left the menu's window sized 60x31 with coordinates painted in
    // it, and the next menu() inherited that until its first Expose. Two
    // features, two windows, no shared mutable state between them.
    XMoveResizeWindow(display(), m_geometryWindow,
                      (mx - width) / 2, (my - height) / 2, width, height);

    if (!m_geometryDraw) {
        m_geometryDraw = x11::XftDrawPtr(XftDrawCreate(display(), m_geometryWindow,
            DefaultVisual(display(), m_screenNumber),
            DefaultColormap(display(), m_screenNumber)));
    } else {
        XftDrawChange(m_geometryDraw.get(), m_geometryWindow);
    }

    // Clear background and draw text
    XftDrawRect(m_geometryDraw.get(), m_menuBgColor.get(), 0, 0, width, height);
    XMapRaised(display(), m_geometryWindow);

    XftDrawStringUtf8(m_geometryDraw.get(), m_menuFgColor.get(),
        m_menuFont, 4, 4 + m_menuFont->ascent,
        reinterpret_cast<const FcChar8*>(string), len);
}


void WindowManager::removeGeometry()
{
    XUnmapWindow(display(), m_geometryWindow);
}
