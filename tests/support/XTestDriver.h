#pragma once

// XTestDriver -- synthesises real pointer input against the running WM (D-08).
//
// Why XTEST and not the alternatives:
//   - xdotool would add a runtime tool dependency and timing flakiness.
//   - raw client-side event injection is useless here: Client::eventButton bails
//     out on `e->send_event` (src/Client.cpp:1377), so an injected synthetic
//     event would never reach the activation path it is supposed to prove.
//
// Pitfall 10 (08-RESEARCH.md): XTEST grab-imperviousness is a property OF THE
// CALLING CONNECTION. The WM takes pointer grabs during menu(), move and resize;
// unless grab control is enabled on the SAME connection that emits the fake
// events, every synthesised event is swallowed. This driver therefore owns a
// dedicated Display connection, separate from every assertion connection, and
// enables grab control on it at construction.
//
// NOTE: the acceptance gates here are line-counting greps -- the grab-control
// call must appear exactly once, and the client-side injection API not at all.
// Keep both literal spellings out of comment prose.
//
// The fake-event `delay` argument also blocks the calling connection for its
// whole duration, so every call here passes CurrentTime (0) and sequences with
// an explicit XFlush plus property polling. Long-press interactions (e.g. the
// destroy-window-delay tab button) are expressed by callers as
// press() -> wall-clock wait -> release(), never via the delay parameter.

#include "x11wrap.h"

#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>

#include <stdexcept>
#include <string>

namespace wm2test {

class XTestDriver {
public:
    // displayName must be the fixture's own display string, never getenv("DISPLAY").
    explicit XTestDriver(const std::string& displayName)
        : m_dpy(XOpenDisplay(displayName.c_str()))
    {
        if (!m_dpy) {
            throw std::runtime_error("XTestDriver: cannot open display " + displayName);
        }

        int eventBase = 0, errorBase = 0, major = 0, minor = 0;
        if (!XTestQueryExtension(m_dpy.get(), &eventBase, &errorBase, &major, &minor)) {
            throw std::runtime_error("XTestDriver: XTEST extension unavailable on " + displayName);
        }

        // Must be this connection (Pitfall 10), before any fake event.
        XTestGrabControl(m_dpy.get(), True);
        XFlush(m_dpy.get());
    }

    XTestDriver(const XTestDriver&) = delete;
    XTestDriver& operator=(const XTestDriver&) = delete;

    void moveTo(int x, int y)
    {
        XTestFakeMotionEvent(m_dpy.get(), 0, x, y, CurrentTime);
        XFlush(m_dpy.get());
    }

    void press(unsigned int button)
    {
        XTestFakeButtonEvent(m_dpy.get(), button, True, CurrentTime);
        XFlush(m_dpy.get());
    }

    void release(unsigned int button)
    {
        XTestFakeButtonEvent(m_dpy.get(), button, False, CurrentTime);
        XFlush(m_dpy.get());
    }

    void click(unsigned int button)
    {
        press(button);
        release(button);
    }

    Display* display() const { return m_dpy.get(); }

private:
    x11::DisplayPtr m_dpy;
};

} // namespace wm2test
