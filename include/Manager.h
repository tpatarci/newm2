#pragma once

#include "x11wrap.h"
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include <vector>
#include <string>
#include <memory>

class Client;

class WindowManager {
public:
    WindowManager();
    ~WindowManager();

    void fatal(const char *message);

    // Client management
    Client* windowToClient(Window w, bool create = false);
    Client* activeClient() { return m_activeClient; }
    void setActiveClient(Client* c) { m_activeClient = c; }

    // Display accessors (raw pointers for X11 API calls)
    Display* display() { return m_display.get(); }
    Window root() { return m_root; }
    int screen() { return m_screenNumber; }

    enum class RootCursor {
        Normal, Delete, Down, Right, DownRight
    };

    void installCursor(RootCursor);
    void installCursorOnWindow(RootCursor, Window w);
    void installColormap(Colormap cmap);
    unsigned long allocateColour(const char *name, const char *fallback);

    // Focus
    void clearFocus();
    void considerFocusChange(Client *c, Window w, Time timestamp);
    void stopConsideringFocus();

    // Grab helpers
    int attemptGrab(Window, Window, int, int);
    void releaseGrab(XButtonEvent *e);

    // Exposure during grab
    void eventExposure(XExposeEvent *e);

    // Geometry display
    void showGeometry(int x, int y);
    void removeGeometry();

    bool raiseTransients(Client *c);

    void addToHiddenList(Client *);
    void removeFromHiddenList(Client *);
    void skipInRevert(Client *, Client *);

    Time timestamp(bool reset);

private:
    int loop();
    void release();
    void initialiseScreen();
    void scanInitialWindows();

    // RAII-managed X11 resources (D-05)
    // IMPORTANT: m_display declared first so it is destroyed last (D-05, Pitfall 2)
    x11::DisplayPtr m_display;

    int m_screenNumber;
    Window m_root;
    Colormap m_defaultColormap;

    // Cursors (RAII-managed)
    x11::UniqueCursor m_cursor;
    x11::UniqueCursor m_xCursor;
    x11::UniqueCursor m_vCursor;
    x11::UniqueCursor m_hCursor;
    x11::UniqueCursor m_vhCursor;

    // Client list (D-08: std::vector replaces listmacro2.h)
    std::vector<Client*> m_clients;
    std::vector<Client*> m_hiddenClients;
    Client *m_activeClient;

    int m_shapeEvent;
    int m_currentTime;

    bool m_looping;
    int m_returnCode;

    static bool m_initialising;
    static int errorHandler(Display*, XErrorEvent*);
    static void sigHandler(int);
    static int m_signalled;

    // Menu resources (RAII-managed)
    x11::GCPtr m_menuGC;
    Window m_menuWindow;
    x11::FontStructPtr m_menuFont;
    unsigned long m_menuForegroundPixel;
    unsigned long m_menuBackgroundPixel;
    unsigned long m_menuBorderPixel;

    static const char* const m_menuCreateLabel;
    const char* menuLabel(int);
    void menu(XButtonEvent *e);
    void spawn();
    void circulate(bool activeFirst);

    // Focus tracking
    bool m_focusChanging;
    Client *m_focusCandidate;
    Window m_focusCandidateWindow;
    Time m_focusTimestamp;
    bool m_focusPointerMoved;
    bool m_focusPointerNowStill;
    void checkDelaysForFocus();

    void nextEvent(XEvent *ev);

    // Event handlers
    void eventButton(XButtonEvent*);
    void eventMapRequest(XMapRequestEvent*);
    void eventConfigureRequest(XConfigureRequestEvent*);
    void eventUnmap(XUnmapEvent*);
    void eventCreate(XCreateWindowEvent*);
    void eventDestroy(XDestroyWindowEvent*);
    void eventClient(XClientMessageEvent*);
    void eventColormap(XColormapEvent*);
    void eventProperty(XPropertyEvent*);
    void eventEnter(XCrossingEvent*);
    void eventReparent(XReparentEvent*);
    void eventFocusIn(XFocusInEvent*);
};

// Atom definitions (from upstream General.h)
struct Atoms {
    static Atom wm_state;
    static Atom wm_changeState;
    static Atom wm_protocols;
    static Atom wm_delete;
    static Atom wm_takeFocus;
    static Atom wm_colormaps;
    static Atom wm2_running;
};

extern bool ignoreBadWindowErrors;
