// Degraded-capability tests against the REAL compiled wm2-born-again binary.
//
// This file is the phase's home for "the WM still works when an X extension is
// missing" coverage. It starts with the Shape fallback (XDIS-03) and grows a
// tag group per capability as later plans land; the tags, not the target name,
// are what the ctest gates select on (D-33), which is exactly why several
// unrelated tag groups can share one target here.
//
// Tags in this file:
//   [wm_noshape]       -- the WM runs correctly with Shape forced unavailable
//   [shape_invariant]  -- source-level guard: one and only one Shape call site
//
// Why an environment variable rather than a server without the extension: the X
// server refuses to turn SHAPE off, answering `Extension "SHAPE" can not be
// disabled` and listing the toggleable extensions, which omit it. D-12's hidden
// WM2_FORCE_NO_SHAPE lever is therefore the only way to exercise this path
// end-to-end. RANDR and RENDER, handled in later plans, CAN be disabled at the
// server and use that stronger mechanism instead -- three fallbacks, three
// different harness mechanisms on purpose.

#include <catch2/catch_test_macros.hpp>

#include "support/WmFixture.h"

#include "x11wrap.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>

#include <fstream>
#include <string>
#include <vector>

#ifndef WM2_SOURCE_DIR
#error "WM2_SOURCE_DIR must be defined by the build system"
#endif

using namespace wm2test;

namespace {

// The forced-fallback line src/Manager.cpp prints under the lever. Deliberately
// distinct from the genuine-absence line below, so a green test cannot be a
// false pass caused by the variable being silently ignored.
constexpr const char* kForcedLine  = "shape extension forced off";
constexpr const char* kGenuineLine = "no shape extension";

WmFixtureOptions forcedNoShape()
{
    WmFixtureOptions o;
    o.childEnv["WM2_FORCE_NO_SHAPE"] = "1";
    return o;
}

// Map a normal (non-transient, non-dock) client and wait until the WM reparents
// it into a frame. Returns the frame window, or None if it never got framed.
Window mapClientAndAwaitFrame(Display* d, Window& clientOut)
{
    Window root = DefaultRootWindow(d);
    Window w = XCreateSimpleWindow(d, root, 60, 50, 240, 180, 0,
                                   BlackPixel(d, DefaultScreen(d)),
                                   WhitePixel(d, DefaultScreen(d)));
    XMapWindow(d, w);
    XSync(d, False);
    clientOut = w;

    Window frame = None;
    const bool framed = WmFixture::pollUntil([&] {
        Window wroot = None, parent = None, *children = nullptr;
        unsigned int n = 0;
        if (!XQueryTree(d, w, &wroot, &parent, &children, &n)) return false;
        if (children) XFree(children);
        if (parent == None || parent == root) return false;
        frame = parent;
        return true;
    }, 8000);

    return framed ? frame : None;
}

// Ask the SERVER whether a window carries a bounding shape mask. This is the
// positive protocol-level assertion the plan requires: the test's own
// connection still has Shape (only the WM's view of it was forced off), so a
// truthful answer is available even in the degraded configuration.
bool queryBoundingShaped(Display* d, Window w, bool& shapedOut)
{
    Bool boundingShaped = False, clipShaped = False;
    int xbs = 0, ybs = 0, xcs = 0, ycs = 0;
    unsigned int wbs = 0, hbs = 0, wcs = 0, hcs = 0;

    Status st = XShapeQueryExtents(d, w,
                                   &boundingShaped, &xbs, &ybs, &wbs, &hbs,
                                   &clipShaped, &xcs, &ycs, &wcs, &hcs);
    if (!st) return false;
    shapedOut = (boundingShaped == True);
    return true;
}

// Read a WINDOW/32 array property (e.g. _NET_CLIENT_LIST).
bool readWindowList(Display* d, Window w, Atom prop, std::vector<Window>& out)
{
    out.clear();
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long nItems = 0, bytesAfter = 0;
    unsigned char* raw = nullptr;

    int status = XGetWindowProperty(d, w, prop, 0, 256, False, XA_WINDOW,
                                    &actualType, &actualFormat,
                                    &nItems, &bytesAfter, &raw);
    if (status != Success) return false;
    if (!raw || actualType != XA_WINDOW || actualFormat != 32) {
        if (raw) XFree(raw);
        return false;
    }
    Window* vals = reinterpret_cast<Window*>(raw);
    out.assign(vals, vals + nItems);
    XFree(raw);
    return true;
}

bool listContains(const std::vector<Window>& list, Window w)
{
    for (Window entry : list) if (entry == w) return true;
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// Case 1: degradation must not mean loss of function
// ---------------------------------------------------------------------------

TEST_CASE("With Shape forced off the WM still manages and frames a client",
          "[wm_noshape]")
{
    WmFixture fixture(forcedNoShape());

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d.get(), client);
    INFO("wm stderr: " << fixture.wmStderr());
    REQUIRE(client != None);
    REQUIRE(frame != None);          // reparented into a frame, not left on root

    Atom clientList = XInternAtom(d.get(), "_NET_CLIENT_LIST", False);
    REQUIRE(clientList != None);

    std::vector<Window> managed;
    REQUIRE(WmFixture::pollUntil([&] {
        return readWindowList(d.get(), DefaultRootWindow(d.get()), clientList, managed) &&
               listContains(managed, client);
    }, 8000));

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());

    XDestroyWindow(d.get(), client);
    XSync(d.get(), False);
}

// ---------------------------------------------------------------------------
// Case 2: the frame is genuinely unshaped, per the server -- not merely
// "nothing crashed"
// ---------------------------------------------------------------------------

TEST_CASE("With Shape forced off the server reports the frame as unshaped",
          "[wm_noshape]")
{
    WmFixture fixture(forcedNoShape());

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    // The assertion connection needs the extension even though the WM was told
    // it does not have it; without this the query below would prove nothing.
    int shapeEventBase = 0, shapeErrorBase = 0;
    REQUIRE(XShapeQueryExtension(d.get(), &shapeEventBase, &shapeErrorBase));

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d.get(), client);
    REQUIRE(frame != None);

    bool shaped = true;
    REQUIRE(queryBoundingShaped(d.get(), frame, shaped));

    INFO("frame: " << frame << "  client: " << client);
    INFO("wm stderr: " << fixture.wmStderr());
    REQUIRE_FALSE(shaped);

    REQUIRE(fixture.wmAlive());

    XDestroyWindow(d.get(), client);
    XSync(d.get(), False);
}

// ---------------------------------------------------------------------------
// Case 3: the forced path was actually taken
//
// Without this, every [wm_noshape] case above could pass green while the
// variable was being silently ignored and the WM ran fully shaped.
// ---------------------------------------------------------------------------

TEST_CASE("The forced no-Shape path announces itself distinctly in the transcript",
          "[wm_noshape]")
{
    WmFixture fixture(forcedNoShape());

    REQUIRE(WmFixture::pollUntil([&] {
        return fixture.wmStderr().find(kForcedLine) != std::string::npos;
    }, 5000));

    const std::string err = fixture.wmStderr();
    INFO("wm stderr: " << err);

    // The forced line must be distinguishable from the line a genuinely
    // Shape-less server would produce, or release evidence could not tell the
    // two situations apart.
    REQUIRE(err.find(kGenuineLine) == std::string::npos);

    REQUIRE(fixture.wmAlive());
}

// ---------------------------------------------------------------------------
// Case 4: the Shape-present control
//
// Proves the [wm_noshape] cases discriminate between the two paths rather than
// asserting something that would hold either way.
// ---------------------------------------------------------------------------

TEST_CASE("Without the lever the server reports the same frame as shaped",
          "[wm_noshape]")
{
    WmFixture fixture;          // no WM2_FORCE_NO_SHAPE in the child environment

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    int shapeEventBase = 0, shapeErrorBase = 0;
    REQUIRE(XShapeQueryExtension(d.get(), &shapeEventBase, &shapeErrorBase));

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d.get(), client);
    REQUIRE(frame != None);

    // The WM shapes the frame during MapRequest handling, so poll rather than
    // sampling once and racing it.
    bool shaped = false;
    REQUIRE(WmFixture::pollUntil([&] {
        bool s = false;
        return queryBoundingShaped(d.get(), frame, s) && s;
    }, 8000));
    REQUIRE(queryBoundingShaped(d.get(), frame, shaped));

    const std::string err = fixture.wmStderr();
    INFO("frame: " << frame << "  client: " << client);
    INFO("wm stderr: " << err);

    REQUIRE(shaped);
    // ...and this run took the ordinary path, not the forced one.
    REQUIRE(err.find(kForcedLine) == std::string::npos);

    REQUIRE(fixture.wmAlive());

    XDestroyWindow(d.get(), client);
    XSync(d.get(), False);
}

// ---------------------------------------------------------------------------
// Case 5: a degraded server must not turn shutdown into a crash
// ---------------------------------------------------------------------------

TEST_CASE("With Shape forced off SIGTERM still shuts the WM down cleanly",
          "[wm_noshape]")
{
    WmFixture fixture(forcedNoShape());

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    // Shut down with a live managed client, so teardown runs the frame-destroy
    // path rather than the trivial no-clients case.
    Window client = None;
    REQUIRE(mapClientAndAwaitFrame(d.get(), client) != None);

    REQUIRE(fixture.terminateWmCleanly());

    // Explicitly NOT the ASan exitcode sentinel and NOT a signal death.
    REQUIRE(fixture.wm().exitedNormally());
    REQUIRE(fixture.wm().exitCode() == 0);
    REQUIRE(fixture.wm().exitCode() != 42);
    REQUIRE(fixture.wm().termSignal() == 0);

    INFO("wm stderr: " << fixture.wmStderr());
    REQUIRE(fixture.asanReports().empty());
}

// ---------------------------------------------------------------------------
// The single-funnel invariant, as an assertion rather than a review promise
//
// D-11 is only worth anything if it cannot silently regrow a second, unguarded
// call site. Nothing about the runtime cases above would notice that: a new raw
// call would keep working perfectly on every server that HAS the extension.
// ---------------------------------------------------------------------------

TEST_CASE("src/Border.cpp names the Xlib rectangle-combining call exactly once",
          "[shape_invariant]")
{
    const std::string path = std::string(WM2_SOURCE_DIR) + "/src/Border.cpp";

    std::ifstream in(path);
    INFO("scanned: " << path);
    REQUIRE(in.is_open());

    const std::string token = "XShapeCombineRectangles";

    int occurrences = 0;
    int lineNo = 0;
    std::vector<int> hitLines;
    std::string line;

    while (std::getline(in, line)) {
        ++lineNo;

        // Skip comment lines. Explanatory prose naming the entry point must not
        // be able to break the guard it documents -- the exact failure mode
        // 08-01 and 08-02 each recorded against their own greppable invariants.
        const size_t firstNonSpace = line.find_first_not_of(" \t");
        if (firstNonSpace != std::string::npos &&
            line.compare(firstNonSpace, 2, "//") == 0) {
            continue;
        }

        for (size_t pos = line.find(token); pos != std::string::npos;
             pos = line.find(token, pos + token.size())) {
            ++occurrences;
            hitLines.push_back(lineNo);
        }
    }

    std::string where;
    for (int l : hitLines) where += " " + std::to_string(l);
    INFO("non-comment occurrences at line(s):" << where);
    INFO("D-11: every rectangle-combining Shape request must go through the single "
         "guarded Border::combineShape() funnel, whose early return on an absent "
         "Shape extension IS the XDIS-03 fallback. A second call site means Shape "
         "traffic now bypasses that guard and would be sent to a server that cannot "
         "answer it -- which is invisible on any server that does have the "
         "extension. Route the new call through the funnel instead.");

    REQUIRE(occurrences == 1);
}
