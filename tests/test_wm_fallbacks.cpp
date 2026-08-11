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
//   [wm_norender]         -- the WM runs correctly with XRender absent, and on
//                            every rung of the tab-font degradation ladder
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

// --- the XDIS-04 transcript vocabulary -------------------------------------
//
// The XRender capability line, and the four rungs of the tab-font ladder. Rung
// 1 is silent by design (it is the healthy path), so it has no marker here --
// its signature is the ABSENCE of every string below.
constexpr const char* kNoRenderLine   = "no xrender extension";
constexpr const char* kRenderLine     = "XRender extension available";
constexpr const char* kRung2Line      = "preferred rotated tab font unavailable";
constexpr const char* kRung3Line      = "no rotated tab font on this display";
constexpr const char* kRung4Line      = "no usable tab font on this display";
// Worded distinctly from the rung lines, so a transcript proves the lever was
// honoured rather than the host merely happening to lack a font.
constexpr const char* kForcedRotatedOff = "rotated tab font forced off";
constexpr const char* kForcedFontOff    = "tab font forced off";

// True when the transcript names any rung of the ladder, i.e. when the WM did
// NOT get the rotated tab font it prefers.
bool namesAnyLadderRung(const std::string& err)
{
    return err.find(kRung2Line) != std::string::npos ||
           err.find(kRung3Line) != std::string::npos ||
           err.find(kRung4Line) != std::string::npos;
}

WmFixtureOptions forcedNoRotatedTabFont()
{
    WmFixtureOptions o;
    o.childEnv["WM2_FORCE_NO_ROTATED_TAB_FONT"] = "1";
    return o;
}

WmFixtureOptions forcedNoTabFont()
{
    WmFixtureOptions o;
    o.childEnv["WM2_FORCE_NO_TAB_FONT"] = "1";
    return o;
}

// A frame that exists but has collapsed to nothing would satisfy "the client
// was reparented" while being useless to a user, so the frame's own rectangle
// is read from the server rather than inferred.
bool frameSize(Display* d, Window frame, unsigned int& w, unsigned int& h)
{
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(d, frame, &attrs)) return false;
    w = static_cast<unsigned int>(attrs.width);
    h = static_cast<unsigned int>(attrs.height);
    return true;
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

// ===========================================================================
// XDIS-04 end to end: the WM itself on a RENDER-less server, and on every rung
// of the tab-font ladder.
//
// Read the split before reading the cases. The spike above measured that a
// RENDER-less server does NOT cost the rotated font, so the RENDER-less runs
// legitimately take rung 1 and produce no ladder warning at all -- and the
// cases below assert exactly that rather than looking for a degradation that
// does not happen. The discriminator that keeps them from being false greens
// is therefore the WM's own XRender capability line, which the control case
// pins in the opposite direction.
//
// Rungs 3 and 4 are then exercised on their own terms with the two levers,
// because no server option can make fontconfig fail to resolve a font. Without
// those two cases the ladder's lower rungs would be untested code claiming to
// be a fallback -- which is the failure this whole plan exists to remove.
// ===========================================================================

// ---------------------------------------------------------------------------
// Case 1: the assertion that would have failed before the ladder existed
// ---------------------------------------------------------------------------

TEST_CASE("On a server without XRender the WM starts, manages a client and frames it",
          "[wm_norender]")
{
    // Reaching this line at all is half the assertion: WmFixture's constructor
    // throws unless the WM published _NET_SUPPORTING_WM_CHECK *and* proved its
    // event loop is pumping. A WM that took the old exit path on the font load
    // would never get that far.
    WmFixture fixture(noRenderServer());

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d.get(), client);
    INFO("wm stderr: " << fixture.wmStderr());
    REQUIRE(client != None);
    REQUIRE(frame != None);

    Atom clientList = XInternAtom(d.get(), "_NET_CLIENT_LIST", False);
    REQUIRE(clientList != None);

    std::vector<Window> managed;
    REQUIRE(WmFixture::pollUntil([&] {
        return readWindowList(d.get(), DefaultRootWindow(d.get()), clientList, managed) &&
               listContains(managed, client);
    }, 8000));

    // A labelless tab would be acceptable; a collapsed frame would not.
    unsigned int fw = 0, fh = 0;
    REQUIRE(frameSize(d.get(), frame, fw, fh));
    INFO("frame " << frame << " is " << fw << "x" << fh);
    REQUIRE(fw > 0);
    REQUIRE(fh > 0);

    REQUIRE(fixture.wmAlive());
    REQUIRE(fixture.asanReports().empty());

    XDestroyWindow(d.get(), client);
    XSync(d.get(), False);
}

// ---------------------------------------------------------------------------
// Case 2: the run really was RENDER-less, and it cost nothing
//
// Without the first assertion every [wm_norender] case could pass green on a
// fully RENDER-capable server. The second is the spike's finding restated as a
// property of the shipped binary: no rung of the ladder is entered, so the
// sideways tab is intact.
// ---------------------------------------------------------------------------

TEST_CASE("A RENDER-less server is recorded in the transcript and costs no font rung",
          "[wm_norender]")
{
    WmFixture fixture(noRenderServer());

    REQUIRE(WmFixture::pollUntil([&] {
        return fixture.wmStderr().find(kNoRenderLine) != std::string::npos;
    }, 5000));

    const std::string err = fixture.wmStderr();
    INFO("wm stderr: " << err);

    REQUIRE(err.find(kRenderLine) == std::string::npos);

    // The measured answer to 08-RESEARCH Open Question 1, asserted rather than
    // asserted-about: libXft's core path produces the rotated face, so the WM
    // stays on rung 1 and says nothing about its tab font. If this ever goes
    // red, the ladder started carrying real weight on a RENDER-less server and
    // the release notes need to say so.
    REQUIRE_FALSE(namesAnyLadderRung(err));

    REQUIRE(fixture.wmAlive());
}

// ---------------------------------------------------------------------------
// Case 3: a degraded server must not turn shutdown into a crash
// ---------------------------------------------------------------------------

TEST_CASE("On a server without XRender SIGTERM still shuts the WM down cleanly",
          "[wm_norender]")
{
    WmFixture fixture(noRenderServer());

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    Window client = None;
    REQUIRE(mapClientAndAwaitFrame(d.get(), client) != None);

    REQUIRE(fixture.terminateWmCleanly());

    // Explicitly NOT the exit path a font failure used to take, NOT the ASan
    // exitcode sentinel, and NOT a signal death.
    REQUIRE(fixture.wm().exitedNormally());
    REQUIRE(fixture.wm().exitCode() == 0);
    REQUIRE(fixture.wm().exitCode() != 1);
    REQUIRE(fixture.wm().exitCode() != 42);
    REQUIRE(fixture.wm().termSignal() == 0);

    INFO("wm stderr: " << fixture.wmStderr());
    REQUIRE(fixture.asanReports().empty());
}

// ---------------------------------------------------------------------------
// Case 4: the XRender-present control
//
// Proves the two cases above discriminate between the two servers rather than
// asserting something that holds either way.
// ---------------------------------------------------------------------------

TEST_CASE("Without the disable the same WM records XRender as available",
          "[wm_norender]")
{
    WmFixture fixture;          // the fixture's default server keeps RENDER

    REQUIRE(WmFixture::pollUntil([&] {
        return fixture.wmStderr().find(kRenderLine) != std::string::npos;
    }, 5000));

    const std::string err = fixture.wmStderr();
    INFO("wm stderr: " << err);

    REQUIRE(err.find(kNoRenderLine) == std::string::npos);
    REQUIRE_FALSE(namesAnyLadderRung(err));

    REQUIRE(fixture.wmAlive());
}

// ---------------------------------------------------------------------------
// Case 5: rung 3 -- an unrotated face, reached with the lever
// ---------------------------------------------------------------------------

TEST_CASE("With the rotated tab font unavailable the WM degrades to horizontal labels",
          "[wm_norender]")
{
    WmFixture fixture(forcedNoRotatedTabFont());

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d.get(), client);
    INFO("wm stderr: " << fixture.wmStderr());
    REQUIRE(frame != None);

    unsigned int fw = 0, fh = 0;
    REQUIRE(frameSize(d.get(), frame, fw, fh));
    REQUIRE(fw > 0);
    REQUIRE(fh > 0);

    const std::string err = fixture.wmStderr();
    INFO("wm stderr: " << err);

    // Exactly one rung announced, and it is rung 3.
    REQUIRE(err.find(kRung3Line) != std::string::npos);
    REQUIRE(err.find(kRung4Line) == std::string::npos);
    REQUIRE(err.find(kRung2Line) == std::string::npos);
    // ...and the transcript says the rung was forced rather than discovered.
    REQUIRE(err.find(kForcedRotatedOff) != std::string::npos);

    REQUIRE(fixture.terminateWmCleanly());
    REQUIRE(fixture.asanReports().empty());
}

// ---------------------------------------------------------------------------
// Case 6: rung 4 -- no font at all
//
// The rung the plan's truth statement is really about: frames are still drawn,
// windows are still manageable, one warning is emitted, and the WM lives.
// ---------------------------------------------------------------------------

TEST_CASE("With no tab font at all the WM still frames clients and exits cleanly",
          "[wm_norender]")
{
    WmFixture fixture(forcedNoTabFont());

    x11::DisplayPtr d = fixture.openDisplay();
    REQUIRE(d != nullptr);

    Window client = None;
    Window frame = mapClientAndAwaitFrame(d.get(), client);
    INFO("wm stderr: " << fixture.wmStderr());
    REQUIRE(client != None);
    REQUIRE(frame != None);

    Atom clientList = XInternAtom(d.get(), "_NET_CLIENT_LIST", False);
    std::vector<Window> managed;
    REQUIRE(WmFixture::pollUntil([&] {
        return readWindowList(d.get(), DefaultRootWindow(d.get()), clientList, managed) &&
               listContains(managed, client);
    }, 8000));

    unsigned int fw = 0, fh = 0;
    REQUIRE(frameSize(d.get(), frame, fw, fh));
    INFO("frame " << frame << " is " << fw << "x" << fh);
    REQUIRE(fw > 0);
    REQUIRE(fh > 0);

    const std::string err = fixture.wmStderr();
    INFO("wm stderr: " << err);

    REQUIRE(err.find(kRung4Line) != std::string::npos);
    REQUIRE(err.find(kRung3Line) == std::string::npos);
    REQUIRE(err.find(kRung2Line) == std::string::npos);
    REQUIRE(err.find(kForcedFontOff) != std::string::npos);

    REQUIRE(fixture.terminateWmCleanly());
    REQUIRE(fixture.wm().exitCode() == 0);
    REQUIRE(fixture.asanReports().empty());
}
