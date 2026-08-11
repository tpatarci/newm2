// Process-level tests against the REAL compiled wm2-born-again binary (D-07).
//
// Unlike test_client / test_ewmh, which exercise raw Xlib primitives on the
// shared :99 fixture, every case here runs a live WM child on a display the
// fixture reserved for itself (Pitfall 3 -- a running WM owns
// SubstructureRedirectMask exclusively and would reparent those tests' windows).
//
// Tag: [wm_process] -- registered as a ctest LABEL via ADD_TAGS_AS_LABELS (D-33),
// selected with `ctest -L '^wm_process$' --no-tests=error`.

#include <catch2/catch_test_macros.hpp>

#include "support/WmFixture.h"

#include "x11wrap.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <vector>

using namespace wm2test;

namespace {

constexpr int kScreenWidth = 1024;
constexpr int kScreenHeight = 768;
constexpr int kDockHeight = 40;

// Create an undecorated dock window carrying _NET_WM_WINDOW_TYPE_DOCK and a
// bottom strut. Properties are set BEFORE the map, because Client::getWindowType()
// runs during MapRequest handling (src/Client.cpp:115).
Window createDockWindow(Display* d, int strutBottom)
{
    Window root = DefaultRootWindow(d);
    Window w = XCreateSimpleWindow(d, root,
                                   0, kScreenHeight - kDockHeight,
                                   kScreenWidth, kDockHeight,
                                   0, BlackPixel(d, DefaultScreen(d)),
                                   WhitePixel(d, DefaultScreen(d)));

    Atom windowType = XInternAtom(d, "_NET_WM_WINDOW_TYPE", False);
    Atom dockType = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DOCK", False);
    XChangeProperty(d, w, windowType, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&dockType), 1);

    // _NET_WM_STRUT_PARTIAL: left, right, top, bottom, then 8 start/end values.
    Atom strutPartial = XInternAtom(d, "_NET_WM_STRUT_PARTIAL", False);
    long struts[12] = {0, 0, 0, static_cast<long>(strutBottom),
                       0, 0, 0, 0, 0, 0, 0, kScreenWidth - 1};
    XChangeProperty(d, w, strutPartial, XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(struts), 12);

    XMapWindow(d, w);
    XSync(d, False);
    return w;
}

bool workareaEquals(Display* d, Atom workarea,
                    unsigned long x, unsigned long y,
                    unsigned long w, unsigned long h)
{
    std::vector<unsigned long> vals;
    if (!readCardinals(d, DefaultRootWindow(d), workarea, vals)) return false;
    if (vals.size() < 4) return false;
    return vals[0] == x && vals[1] == y && vals[2] == w && vals[3] == h;
}

} // namespace

// ---------------------------------------------------------------------------
// Behavior 1 (the tracer): the fixture brings the real binary up on its own display
// ---------------------------------------------------------------------------

TEST_CASE("WmFixture starts the real WM and it publishes _NET_SUPPORTING_WM_CHECK",
          "[wm_process]")
{
    WmFixture fixture;

    // The fixture owns its display -- never the process DISPLAY env var.
    REQUIRE_FALSE(fixture.display().empty());
    REQUIRE(fixture.display() != ":99");

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    Atom check = XInternAtom(d.get(), "_NET_SUPPORTING_WM_CHECK", False);
    REQUIRE(check != None);

    Window root = DefaultRootWindow(d.get());

    Atom actualType = None;
    int actualFormat = 0;
    unsigned long nItems = 0, bytesAfter = 0;
    Window* data = nullptr;
    int status = XGetWindowProperty(d.get(), root, check, 0, 1, False, XA_WINDOW,
                                    &actualType, &actualFormat, &nItems, &bytesAfter,
                                    reinterpret_cast<unsigned char**>(&data));
    REQUIRE(status == Success);
    REQUIRE(nItems == 1);
    REQUIRE(data != nullptr);
    Window checkWindow = data[0];
    XFree(data);
    REQUIRE(checkWindow != None);

    // Per EWMH the check window points at itself (src/Manager.cpp:462-465).
    Window selfRef = None;
    REQUIRE(readWindowProp(d.get(), checkWindow, check, selfRef));
    REQUIRE(selfRef == checkWindow);

    REQUIRE(fixture.wmAlive());
}

// ---------------------------------------------------------------------------
// Behavior 2: a dock window's strut shrinks _NET_WORKAREA
// ---------------------------------------------------------------------------

TEST_CASE("A dock window with _NET_WM_STRUT_PARTIAL shrinks _NET_WORKAREA",
          "[wm_process]")
{
    WmFixture fixture;

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    Atom workarea = XInternAtom(d.get(), "_NET_WORKAREA", False);
    REQUIRE(workarea != None);

    // Baseline: full screen before any dock exists.
    REQUIRE(WmFixture::pollUntil([&] {
        return workareaEquals(d.get(), workarea, 0, 0, kScreenWidth, kScreenHeight);
    }, 5000));

    Window dock = createDockWindow(d.get(), kDockHeight);
    REQUIRE(dock != None);

    // The WM must notice the dock and subtract its strut.
    REQUIRE(WmFixture::pollUntil([&] {
        return workareaEquals(d.get(), workarea, 0, 0,
                              kScreenWidth, kScreenHeight - kDockHeight);
    }, 8000));

    REQUIRE(fixture.wmAlive());

    XDestroyWindow(d.get(), dock);
    XSync(d.get(), False);
}

// ---------------------------------------------------------------------------
// Behavior 3: destroying the dock restores the workarea, with no ASan report
//
// This is the proof for the eventDestroy use-after-free (SCAN-02 /
// COMPILED_CODE_BEHAVIOR_CHECKLIST.md "Current Scan Findings", threat T-8-UAF).
// Under the build/asan tree the pre-fix code reads freed memory here; the child
// would write an ASan report and exit with the sentinel exitcode 42.
// ---------------------------------------------------------------------------

TEST_CASE("Destroying a dock window restores _NET_WORKAREA with no sanitizer report",
          "[wm_process]")
{
    WmFixture fixture;

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    Atom workarea = XInternAtom(d.get(), "_NET_WORKAREA", False);
    REQUIRE(workarea != None);

    Window dock = createDockWindow(d.get(), kDockHeight);
    REQUIRE(dock != None);

    REQUIRE(WmFixture::pollUntil([&] {
        return workareaEquals(d.get(), workarea, 0, 0,
                              kScreenWidth, kScreenHeight - kDockHeight);
    }, 8000));

    // The use-after-free trigger: erase the owning unique_ptr, then recompute.
    XDestroyWindow(d.get(), dock);
    XSync(d.get(), False);

    // Workarea recomputed back to the full screen.
    REQUIRE(WmFixture::pollUntil([&] {
        return workareaEquals(d.get(), workarea, 0, 0, kScreenWidth, kScreenHeight);
    }, 8000));

    // The WM survived and is still serving requests.
    REQUIRE(fixture.wmAlive());
    Window checkWindow = None;
    Atom check = XInternAtom(d.get(), "_NET_SUPPORTING_WM_CHECK", False);
    REQUIRE(readWindowProp(d.get(), DefaultRootWindow(d.get()), check, checkWindow));
    REQUIRE(checkWindow != None);

    // Pitfall 4: a sanitizer report inside the child must fail the test rather
    // than being swallowed. Zero report files means zero findings.
    const std::vector<std::string> reports = fixture.asanReports();
    INFO("ASan log prefix: " << fixture.asanLogPrefix());
    for (const std::string& r : reports) INFO("ASan report: " << r);
    REQUIRE(reports.empty());
}

// ---------------------------------------------------------------------------
// Behavior 4: SIGTERM produces a clean exit
// ---------------------------------------------------------------------------

TEST_CASE("SIGTERM shuts the WM down cleanly with exit status 0", "[wm_process]")
{
    WmFixture fixture;
    REQUIRE(fixture.wmAlive());

    // Exercises the self-pipe signal path (src/Events.cpp:130, src/Manager.cpp:269).
    REQUIRE(fixture.terminateWmCleanly());

    // Explicitly NOT the ASan exitcode sentinel and NOT a signal death.
    REQUIRE(fixture.wm().exitedNormally());
    REQUIRE(fixture.wm().exitCode() == 0);
    REQUIRE(fixture.wm().exitCode() != 42);
    REQUIRE(fixture.wm().termSignal() == 0);

    INFO("wm stderr: " << fixture.wmStderr());
    REQUIRE(fixture.asanReports().empty());
}
