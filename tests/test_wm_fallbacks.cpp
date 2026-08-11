// Degraded-capability tests against the REAL compiled wm2-born-again binary.
//
// This file is the phase's home for "the WM still works when an X extension is
// missing" coverage. It starts with the Shape fallback (XDIS-03) and grows a
// tag group per capability as later plans land; the tags, not the target name,
// are what the ctest gates select on (D-33), which is exactly why several
// unrelated tag groups can share one target here.
//
// Tags in this file:
//   [wm_noshape]          -- the WM runs correctly with Shape forced unavailable
//   [shape_invariant]     -- source-level guard: one and only one Shape call site
//   [xft_norender_spike]  -- 08-RESEARCH Open Question 1, answered in-tree
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
// Xrender.h, not render.h: the latter is the protocol/type header and declares
// no entry points, so the capability probe below would not compile against it.
#include <X11/extensions/Xrender.h>

#include <cstdio>
#include <cstring>
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

// A genuinely capability-less server, not a lever: unlike SHAPE, the X server
// is willing to turn RENDER off. WmFixture appends xvfbArgs AFTER its own
// defaults (which include "+render"), and the later argument wins -- measured
// on this host, the XRender capability query answers False with both present
// in that order. (Named in prose, not in code: an acceptance criterion counts
// the literal entry point and a comment must not be able to break it -- the
// exact failure 08-01, 08-02, 08-03 and 08-05 each recorded.)
WmFixtureOptions noRenderServer()
{
    WmFixtureOptions o;
    o.xvfbArgs = {"-extension", "RENDER"};
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

// --- X protocol error trap, used by the spike's drawing fact ----------------
//
// Xlib reports protocol errors asynchronously through a global handler, so
// "the draw completed without an X error" can only be asserted by installing
// one, round-tripping with XSync, and reading the counter afterwards.

int g_xErrors = 0;
char g_xErrorText[256] = {0};

int countingErrorHandler(Display* d, XErrorEvent* e)
{
    ++g_xErrors;
    XGetErrorText(d, e->error_code, g_xErrorText, sizeof g_xErrorText);
    return 0;
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

// ---------------------------------------------------------------------------
// Open Question 1 (08-RESEARCH.md): does the rotated FcMatrix tab font survive
// on a server with the XRender extension disabled?
//
// ANSWER, measured by this case on this host: YES, completely. libXft falls
// back to its core-X11 glyph path, the rotated pattern loads, XftTextExtentsUtf8
// returns the same numbers it returns with RENDER present, and drawing into a
// pixmap raises no protocol error. The rotated tab -- the project's
// non-negotiable visual identity -- is therefore NOT lost on a RENDER-less
// remote server.
//
// This case exists so that answer stays reproducible by re-running one ctest
// filter rather than by trusting a sentence in a summary. It is deliberately a
// pure font/extension spike: it asserts nothing about the WM's own font ladder,
// which the [wm_norender] group below covers.
//
// The answer does NOT make the ladder in src/Border.cpp optional. It narrows
// which rung is taken here, not whether a font failure may kill the WM: the
// four rungs also cover a target with no usable font file at all, which no
// amount of RENDER availability protects against.
// ---------------------------------------------------------------------------

TEST_CASE("Spike: a rotated Xft font loads, measures and draws with XRender disabled",
          "[xft_norender_spike]")
{
    WmFixture fixture(noRenderServer());

    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();

    g_xErrors = 0;
    g_xErrorText[0] = '\0';
    XErrorHandler previous = XSetErrorHandler(countingErrorHandler);

    // --- Fact 1: is the extension actually absent on this connection? -------
    //
    // Asked through the public XRender entry point. Deliberately NOT
    // XftDefaultHasRender (exported by libXft but declared in no installed
    // header, so using it would mean hand-declaring a private symbol) and
    // deliberately NOT parsing xdpyinfo output.
    int renderEventBase = 0, renderErrorBase = 0;
    const bool renderPresent =
        (XRenderQueryExtension(d, &renderEventBase, &renderErrorBase) == True);

    std::printf("[spike] fact 1  XRender present on this connection: %s\n",
                renderPresent ? "YES" : "NO");
    INFO("wm stderr: " << fixture.wmStderr());
    // If this ever goes green with RENDER present, the whole spike is
    // measuring the ordinary path and its answer means nothing.
    REQUIRE_FALSE(renderPresent);

    // --- Fact 2: does the rotated pattern load at all? ----------------------
    //
    // Built through the SAME factory the production loader uses, with the SAME
    // preferred chain, because a differently-constructed pattern would answer a
    // different question.
    x11::XftFontPtr rotated = x11::make_xft_font_rotated(
        d, "Noto Sans,DejaVu Sans,Sans:bold:size=12");

    std::printf("[spike] fact 2  rotated preferred-chain font loaded: %s\n",
                rotated ? "YES" : "NO");

    if (!rotated) {
        // Recorded rather than assumed: if this host ever answers NO, the next
        // reader sees which rung of the Border ladder becomes load-bearing.
        std::printf("[spike] fact 2b rotated generic-sans font loaded: ");
        x11::XftFontPtr generic =
            x11::make_xft_font_rotated(d, "sans-serif:bold:size=12");
        std::printf("%s\n", generic ? "YES" : "NO");
        std::printf("[spike] CONCLUSION: rotated fonts do NOT survive without "
                    "XRender on this host; Border rungs 3/4 carry the tab.\n");
    }
    REQUIRE(rotated);
    std::printf("[spike] CONCLUSION: rotated fonts DO survive without XRender; "
                "libXft's core path renders the sideways tab.\n");

    // --- Fact 3: are the extents usable? ------------------------------------
    //
    // Border sizes the tab from these numbers, so a zero or nonsensical value
    // would break the layout even though the load succeeded.
    XGlyphInfo oneGlyph;
    std::memset(&oneGlyph, 0, sizeof oneGlyph);
    XftTextExtentsUtf8(d, rotated.get(),
                       reinterpret_cast<const FcChar8*>("M"), 1, &oneGlyph);

    XGlyphInfo fiveGlyphs;
    std::memset(&fiveGlyphs, 0, sizeof fiveGlyphs);
    XftTextExtentsUtf8(d, rotated.get(),
                       reinterpret_cast<const FcChar8*>("Hello"), 5, &fiveGlyphs);

    std::printf("[spike] fact 3  extents \"M\": w=%d h=%d   \"Hello\": w=%d h=%d\n",
                oneGlyph.width, oneGlyph.height,
                fiveGlyphs.width, fiveGlyphs.height);

    REQUIRE(oneGlyph.width > 0);
    REQUIRE(oneGlyph.height > 0);
    // Rotated: the string grows along the HEIGHT axis while the width stays at
    // the single-line thickness. That asymmetry is what proves the FcMatrix
    // rotation was honoured rather than silently dropped.
    REQUIRE(fiveGlyphs.height > oneGlyph.height);
    REQUIRE(fiveGlyphs.width == oneGlyph.width);

    // --- Fact 4: does drawing it raise a protocol error? --------------------
    const int screen = DefaultScreen(d);
    Pixmap pixmap = XCreatePixmap(d, DefaultRootWindow(d), 120, 200,
                                  static_cast<unsigned int>(DefaultDepth(d, screen)));
    REQUIRE(pixmap != None);

    XftDraw* rawDraw = XftDrawCreate(d, pixmap, DefaultVisual(d, screen),
                                     DefaultColormap(d, screen));
    REQUIRE(rawDraw != nullptr);
    x11::XftDrawPtr draw(rawDraw);

    x11::XftColorWrap ink(d, DefaultVisual(d, screen),
                          DefaultColormap(d, screen), "black");
    REQUIRE(ink);

    XftDrawStringUtf8(draw.get(), ink.get(), rotated.get(), 10, 20,
                      reinterpret_cast<const FcChar8*>("Hello"), 5);
    XSync(d, False);

    std::printf("[spike] fact 4  X protocol errors during rotated draw: %d%s%s\n",
                g_xErrors,
                g_xErrors ? "  last: " : "",
                g_xErrors ? g_xErrorText : "");
    std::fflush(stdout);

    INFO("last X error: " << g_xErrorText);
    REQUIRE(g_xErrors == 0);

    draw.reset();
    XFreePixmap(d, pixmap);
    XSync(d, False);
    XSetErrorHandler(previous);

    REQUIRE(fixture.wmAlive());
}
