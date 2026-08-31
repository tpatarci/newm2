// Targeted reproducer for the post-readiness reparent sequence (plan 08.5-09,
// TEST-08).
//
// This file is a DIAGNOSTIC INSTRUMENT THAT LIVES IN THE SUITE. Its cases are
// cheap by default -- they cost one fixture and one mapped client -- and are
// widened into a real measurement only by an explicit measurement run that sets
// the budget knobs in the environment. Nothing here certifies the window
// manager, and no case in this file may redden because the window manager
// wedged: the wedge is the OBSERVATION this instrument exists to make, not a
// failure. A client that never frames is recorded, never asserted.
//
// Tags in this file:
//   [wm_repro]  -- the hang-time capture path, and the budgeted sampler built
//                  on top of it
//
// Why an isolated reproducer rather than more full-suite sampling: plan 08.5-06
// spent an eight-run budget on the full debug gate and the one red run it bought
// carried no window-manager stderr, because that case's annotation guard sits
// below the assertion that failed. The causal chain the record verified is
// narrow enough to isolate directly -- the fixture's readiness probe returns as
// soon as its own probe window is reparented, and the window manager then
// proceeds reparent -> activate -> timestamp, where it can wedge on an unbounded
// blocking wait. So construction can legitimately return "ready" at the exact
// moment the window manager is about to block, and the NEXT window's map request
// is never processed. That is the sequence this file maps one client into.
//
// Why the diagnostic threshold is a NEW knob rather than a widened one: the
// reparent deadline below is a hard-coded constant with no environment override
// of any spelling, and the threshold is refused if it is configured at or above
// it. The threshold sits strictly INSIDE the deadline so a capture happens while
// the window manager is still wedged, rather than after the poll has given up --
// which is the after-the-fact reading this whole plan exists to replace.

#include <catch2/catch_test_macros.hpp>

#include "support/WmFixture.h"

#include "x11wrap.h"
#include <X11/Xlib.h>

#include <sys/stat.h>
#include <sys/wait.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

#ifndef WM2_SOURCE_DIR
#error "WM2_SOURCE_DIR must be defined by the build system"
#endif

using namespace wm2test;

namespace {

// The reparent poll deadline. Declared ONCE, matching the independent copy in
// tests/test_wm_fallbacks.cpp, and deliberately carrying NO environment
// override of any kind -- see the file header. Every other reference to it in
// this file is by name, including the refusal message below, which interpolates
// the constant rather than spelling the number.
constexpr int kReparentDeadlineMs = 8000;

// The diagnostic threshold: how long a post-readiness reparent poll may run
// before a capture is taken. Strictly inside the deadline above.
constexpr int kDefaultThresholdMs = 500;

// The five channels a bundle must be readable in. Fewer means a channel is
// silently missing, which is the whole defect class this file exists to remove.
const char* const kChannelNames[] = {
    "wm-alive", "wm-stderr", "xvfb-log", "wchan", "backtrace"
};
constexpr int kChannelCount = 5;

// The ONLY environment knob this task reads, and it is read through a string
// literal rather than a variable so the file's whole environment surface is
// visible to a static reading.
//
// Refusing the EQUAL case is deliberate, not defensive: a threshold equal to the
// deadline collapses the two constants into one reading, and the capture would
// fire, if at all, exactly when the poll gives up.
int reproThresholdMs()
{
    static const int resolved = [] {
        const char* raw = std::getenv("WM2_REPRO_THRESHOLD_MS");
        if (raw == nullptr || raw[0] == '\0') return kDefaultThresholdMs;

        char* end = nullptr;
        errno = 0;
        const long offered = std::strtol(raw, &end, 10);
        const bool parsed = (end != raw && end != nullptr && *end == '\0' && errno == 0);

        if (!parsed || offered <= 0 || offered >= static_cast<long>(kReparentDeadlineMs)) {
            std::fprintf(stderr,
                         "wm2-repro: refused WM2_REPRO_THRESHOLD_MS=%s -- must be a positive "
                         "integer strictly below the %d ms reparent deadline; using %d ms\n",
                         raw, kReparentDeadlineMs, kDefaultThresholdMs);
            std::fflush(stderr);
            return kDefaultThresholdMs;
        }
        return static_cast<int>(offered);
    }();
    return resolved;
}

// Every budget knob below is validated the same way: parsed as an integer, a
// non-positive or absurd value refused with one prefixed message, then the
// default used. The already-fetched value is passed in so that every
// environment read in this file is a call on a STRING LITERAL -- a getenv
// reached through a variable would put the knob set beyond a static reading,
// and the closed enumeration of that set is what makes the no-widening claim
// checkable rather than promised.
int validatedKnob(const char* name, const char* raw, int fallback, long ceiling)
{
    if (raw == nullptr || raw[0] == '\0') return fallback;

    char* end = nullptr;
    errno = 0;
    const long offered = std::strtol(raw, &end, 10);
    const bool parsed = (end != raw && end != nullptr && *end == '\0' && errno == 0);

    if (!parsed || offered <= 0 || offered > ceiling) {
        std::fprintf(stderr,
                     "wm2-repro: refused %s=%s -- must be a positive integer no greater "
                     "than %ld; using %d\n",
                     name, raw, ceiling, fallback);
        std::fflush(stderr);
        return fallback;
    }
    return static_cast<int>(offered);
}

// The closed knob set. Eight members, no more: the threshold above plus these
// seven. The reparent deadline is in neither list and has no spelling in
// either -- it is a constexpr literal declared exactly once.
int sFixtures()
{
    static const int v = validatedKnob("WM2_REPRO_S_FIXTURES",
                                       std::getenv("WM2_REPRO_S_FIXTURES"), 2, 100000);
    return v;
}

int rFixtures()
{
    static const int v = validatedKnob("WM2_REPRO_R_FIXTURES",
                                       std::getenv("WM2_REPRO_R_FIXTURES"), 1, 10000);
    return v;
}

int rMaps()
{
    static const int v = validatedKnob("WM2_REPRO_R_MAPS",
                                       std::getenv("WM2_REPRO_R_MAPS"), 5, 100000);
    return v;
}

int sWallMs()
{
    static const int v = validatedKnob("WM2_REPRO_S_WALL_MS",
                                       std::getenv("WM2_REPRO_S_WALL_MS"), 20000, 7200000);
    return v;
}

int rWallMs()
{
    static const int v = validatedKnob("WM2_REPRO_R_WALL_MS",
                                       std::getenv("WM2_REPRO_R_WALL_MS"), 20000, 7200000);
    return v;
}

int bundleCap()
{
    static const int v = validatedKnob("WM2_REPRO_BUNDLE_CAP",
                                       std::getenv("WM2_REPRO_BUNDLE_CAP"), 20, 10000);
    return v;
}

// --- small filesystem helpers ----------------------------------------------

bool mkdirp(const std::string& path)
{
    if (path.empty() || path[0] != '/') return false;

    std::string acc = "/";
    size_t i = 1;
    while (i <= path.size()) {
        size_t slash = path.find('/', i);
        if (slash == std::string::npos) slash = path.size();
        acc += path.substr(i, slash - i);
        if (::mkdir(acc.c_str(), 0700) != 0 && errno != EEXIST) return false;
        acc += "/";
        i = slash + 1;
    }
    return true;
}

bool writeFile(const std::string& path, const std::string& content)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    if (!content.empty()) std::fwrite(content.data(), 1, content.size(), f);
    std::fclose(f);
    return true;
}

std::string readFileOrEmpty(const std::string& path)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return std::string();
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

void appendLine(const std::string& path, const std::string& line)
{
    FILE* f = std::fopen(path.c_str(), "ab");
    if (!f) return;
    std::fprintf(f, "%s\n", line.c_str());
    std::fclose(f);
}

std::string sanitizeDisplay(const std::string& display)
{
    std::string s = display;
    for (char& ch : s) {
        if (ch == ':' || ch == '.' || ch == '/') ch = '-';
    }
    return s;
}

// Where trip bundles are written. Validated on the same terms as every other
// knob: an absolute path with no parent-directory component, or the default
// under the build tree. There is no fixed temporary-directory name anywhere in
// this file (threat T-8-TMP).
std::string outDirRoot()
{
    static const std::string resolved = [] {
        const std::string fallback = WmFixture::workDir() + "/repro-bundles";
        const char* raw = std::getenv("WM2_REPRO_OUT_DIR");
        if (raw == nullptr || raw[0] == '\0') return fallback;

        const std::string offered(raw);
        if (offered[0] != '/' || offered.find("..") != std::string::npos) {
            std::fprintf(stderr,
                         "wm2-repro: refused WM2_REPRO_OUT_DIR=%s -- must be an absolute "
                         "path with no parent-directory component; using the build tree\n",
                         raw);
            std::fflush(stderr);
            return fallback;
        }
        return offered;
    }();
    return resolved;
}

long elapsedMsSince(const Clock::time_point& start)
{
    return static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - start).count());
}

// Every line of `text` that begins "<channel>: ".
std::vector<std::string> channelLines(const std::string& text, const std::string& channel)
{
    const std::string prefix = channel + ": ";
    std::vector<std::string> found;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        const std::string line = text.substr(pos, eol - pos);
        if (line.compare(0, prefix.size(), prefix) == 0) found.push_back(line);
        if (eol == text.size()) break;
        pos = eol + 1;
    }
    return found;
}

// A well-formed verdict is PRESENT with content, or ABSENT with a stated
// reason. An empty remainder on either is not a verdict.
bool verdictWellFormed(const std::string& line, const std::string& channel)
{
    const std::string body = line.substr(channel.size() + 2);
    if (body.compare(0, 8, "PRESENT ") == 0) return body.size() > 8;
    if (body.compare(0, 7, "ABSENT ") == 0) return body.size() > 7;
    return false;
}

// The number of channels carrying EXACTLY ONE well-formed verdict line. Five is
// the only acceptable reading: fewer means a channel is silently missing, more
// means one was written twice and the bundle's readings are ambiguous.
int wellFormedChannelCount(const std::string& capText)
{
    int good = 0;
    for (int i = 0; i < kChannelCount; ++i) {
        const std::string channel = kChannelNames[i];
        const std::vector<std::string> lines = channelLines(capText, channel);
        if (lines.size() == 1 && verdictWellFormed(lines[0], channel)) ++good;
    }
    return good;
}

std::string procStateOf(const std::string& capText)
{
    const std::vector<std::string> lines = channelLines(capText, "proc-state");
    if (lines.size() != 1) return std::string();
    return lines[0].substr(std::strlen("proc-state: "));
}

// --- the capture helper -----------------------------------------------------
//
// Writes fixture.txt, the first three channel lines of capability.txt,
// wm-stderr.txt and xvfb.log, then invokes the capture tool, which appends the
// wchan, backtrace and proc-state readings. Any channel the tool did not reach
// is written here as an ABSENT verdict with a stated reason, so a channel is
// never silently missing from a bundle.

struct CaptureOutcome {
    std::string dir;
    bool wrote = false;
    int scriptStatus = -1;
};

CaptureOutcome captureBundle(WmFixture& fixture,
                             const std::string& dir,
                             const std::string& mode,
                             const std::string& reason,
                             long elapsedMs,
                             bool framed)
{
    CaptureOutcome out;
    out.dir = dir;
    if (!mkdirp(dir)) return out;

    const long wmPid = static_cast<long>(fixture.wm().pid());
    const bool alive = fixture.wmAlive();

    std::string fx;
    fx += "display: " + fixture.display() + "\n";
    fx += "wm-pid: " + std::to_string(wmPid) + "\n";
    fx += "mode: " + mode + "\n";
    fx += "reason: " + reason + "\n";
    fx += "threshold-ms: " + std::to_string(reproThresholdMs()) + "\n";
    fx += "elapsed-ms: " + std::to_string(elapsedMs) + "\n";
    fx += "framed: " + std::string(framed ? "yes" : "no") + "\n";
    writeFile(dir + "/fixture.txt", fx);

    std::string capText;

    capText += std::string("wm-alive: ") +
        (alive ? ("PRESENT pid=" + std::to_string(wmPid))
               : std::string("ABSENT window manager already reaped")) + "\n";

    const std::string err = fixture.wmStderr();
    writeFile(dir + "/wm-stderr.txt", err);
    capText += std::string("wm-stderr: ") +
        (err.empty() ? std::string("ABSENT no bytes captured from the child")
                     : ("PRESENT bytes=" + std::to_string(err.size()))) + "\n";

    const std::string xvfb = readFileOrEmpty(fixture.xvfbLogPath());
    writeFile(dir + "/xvfb.log", xvfb);
    capText += std::string("xvfb-log: ") +
        (xvfb.empty() ? std::string("ABSENT log file empty or not present")
                      : ("PRESENT bytes=" + std::to_string(xvfb.size()))) + "\n";

    // Truncating, not appending: a stale capability.txt left on this display by
    // an earlier run would otherwise double every channel it names.
    const std::string cap = dir + "/capability.txt";
    writeFile(cap, capText);

    const std::string script =
        std::string(WM2_SOURCE_DIR) + "/scripts/diag/wm-stack-capture.sh";
    const std::string cmd =
        "bash '" + script + "' " + std::to_string(wmPid) + " '" + dir + "'" +
        " >> '" + dir + "/capture-tool.log' 2>&1";
    out.scriptStatus = std::system(cmd.c_str());

    const int scriptExit =
        (out.scriptStatus != -1 && WIFEXITED(out.scriptStatus))
            ? WEXITSTATUS(out.scriptStatus) : -1;

    std::string why;
    if (!alive) {
        why = "window manager already reaped, no process to read";
    } else if (scriptExit == 3) {
        why = "capture tool refused the pid on its exe or display anchor";
    } else {
        why = "capture tool did not record this channel, exit=" +
              std::to_string(scriptExit);
    }

    const std::string after = readFileOrEmpty(cap);
    if (channelLines(after, "wchan").empty()) {
        appendLine(cap, "wchan: ABSENT " + why);
    }
    if (channelLines(after, "backtrace").empty()) {
        appendLine(cap, "backtrace: ABSENT " + why);
    }
    if (channelLines(after, "proc-state").empty()) {
        appendLine(cap, "proc-state: ?");
    }

    out.wrote = true;
    return out;
}

// Map one client and poll for its frame against the hard deadline. Returns the
// frame window, or None if it never got framed -- which is recorded, never
// asserted.
Window mapClientAndPoll(Display* d, Window& clientOut, long& elapsedMsOut, bool& framedOut)
{
    Window root = DefaultRootWindow(d);
    Window w = XCreateSimpleWindow(d, root, 60, 50, 240, 180, 0,
                                   BlackPixel(d, DefaultScreen(d)),
                                   WhitePixel(d, DefaultScreen(d)));
    XMapWindow(d, w);
    XSync(d, False);
    clientOut = w;

    Window frame = None;
    const auto started = Clock::now();
    framedOut = WmFixture::pollUntil([&] {
        Window wroot = None, parent = None, *children = nullptr;
        unsigned int n = 0;
        if (!XQueryTree(d, w, &wroot, &parent, &children, &n)) return false;
        if (children) XFree(children);
        if (parent == None || parent == root) return false;
        frame = parent;
        return true;
    }, kReparentDeadlineMs);

    elapsedMsOut = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - started).count());

    return framedOut ? frame : None;
}

// --- the budgeted sampler ---------------------------------------------------

struct ModeCounters {
    long fixtures = 0;
    long startFailures = 0;
    long maps = 0;
    long trips = 0;
    long allocated = 0;
    long bundles = 0;
    long capped = 0;
    long unframed = 0;
    long wallMs = 0;
    long bundlesWithFiveVerdicts = 0;
};

// The transcript's counter tokens are part of this measurement's contract,
// because the readout-integrity gate parses them.
//
// EVERY key carries its mode letter and there is deliberately NO mode-agnostic
// spelling of any of them. A pooled scalar read with `tail -1` reports whichever
// mode printed last and hides the other, which fails in both directions: one
// mode's trips vanish behind the other mode's zero, or an honest single-mode
// reproduction is reddened by the other mode's zero. Hiding one mode behind the
// other is the exact false reading this measurement exists to eliminate.
//
// Printed UNCONDITIONALLY, including for a mode that observed nothing, so a
// missing token is a defect rather than a value of zero.
//
// `startfail-<m>` is recorded beyond the contract's seven mandatory keys: a
// fixture whose window manager wedged during its OWN startup never reaches a
// post-readiness map, and a run that lost fixtures that way must not read as a
// run that sampled cleanly.
void printModeSummary(const std::string& mode, const ModeCounters& c)
{
    const char* m = mode.c_str();
    std::printf("wm2-repro: mode-%s fixtures-%s=%ld startfail-%s=%ld maps-%s=%ld "
                "trips-%s=%ld bundles-%s=%ld capped-%s=%ld unframed-%s=%ld "
                "wallms-%s=%ld\n",
                m, m, c.fixtures, m, c.startFailures, m, c.maps, m, c.trips,
                m, c.bundles, m, c.capped, m, c.unframed, m, c.wallMs);
    std::fflush(stdout);
}

// The shared poll: the hard reparent deadline, with the diagnostic threshold
// checked inside it.
//
// The comparison is `>=` and the capture is LATCHED per poll, so an elapsed
// reading landing exactly on the threshold trips exactly once -- not zero times
// and not twice. The bundle index is allocated from a per-mode MONOTONIC
// counter starting at 1, never from a timestamp and never from a directory
// listing, so two trips can never be handed the same directory name.
//
// After a trip the poll CONTINUES to the deadline, so whether the client
// eventually framed is recorded rather than lost. That elapsed figure is a
// liveness record, NOT a stall measurement: a debugger attach suspends the
// observed process for the duration of the backtrace, so everything after a
// capture is perturbed by the capture.
bool pollForFrameWithCapture(WmFixture& fixture, Display* d, Window w,
                             const std::string& mode, ModeCounters& counters)
{
    Window root = DefaultRootWindow(d);
    const auto started = Clock::now();
    const long thresholdMs = static_cast<long>(reproThresholdMs());
    const long cap = static_cast<long>(bundleCap());
    bool latched = false;

    for (;;) {
        Window wroot = None, parent = None, *children = nullptr;
        unsigned int n = 0;
        if (XQueryTree(d, w, &wroot, &parent, &children, &n)) {
            if (children) XFree(children);
            if (parent != None && parent != root) return true;
        }

        const long elapsed = elapsedMsSince(started);

        if (!latched && elapsed >= thresholdMs) {
            latched = true;
            ++counters.trips;

            if (counters.allocated < cap) {
                const long index = ++counters.allocated;
                const std::string dir =
                    outDirRoot() + "/trip-" + mode + "-" + std::to_string(index);
                const CaptureOutcome oc = captureBundle(
                    fixture, dir, mode,
                    "diagnostic threshold reached during a post-readiness reparent poll",
                    elapsed, false);
                if (oc.wrote) {
                    ++counters.bundles;
                    const std::string capText =
                        readFileOrEmpty(dir + "/capability.txt");
                    if (wellFormedChannelCount(capText) == kChannelCount) {
                        ++counters.bundlesWithFiveVerdicts;
                    }
                }
            } else {
                // Declined because the per-mode cap was already reached. The
                // identity trips == bundles + capped therefore holds by
                // construction, which is what makes a DROPPED write visible in
                // the transcript rather than silently absorbed.
                ++counters.capped;
            }
        }

        if (elapsed >= static_cast<long>(kReparentDeadlineMs)) return false;
        pollSleep();
    }
}

Window createAndMap(Display* d)
{
    Window root = DefaultRootWindow(d);
    Window w = XCreateSimpleWindow(d, root, 60, 50, 240, 180, 0,
                                   BlackPixel(d, DefaultScreen(d)),
                                   WhitePixel(d, DefaultScreen(d)));
    XMapWindow(d, w);
    XSync(d, False);
    return w;
}

// Mode S -- the startup-adjacent shape. A fresh fixture, exactly ONE client
// mapped after it reports ready, then teardown. This is the shape both case #80
// and the before-half's case #93 exhibited, and it is the more faithful
// reproduction of the two.
ModeCounters runModeS()
{
    ModeCounters c;
    const auto started = Clock::now();
    const long budgetMs = static_cast<long>(sWallMs());
    const int fixtures = sFixtures();

    for (int i = 0; i < fixtures; ++i) {
        if (elapsedMsSince(started) >= budgetMs) break;

        try {
            WmFixtureOptions options;
            options.allowTestProcessPtrace = true;
            WmFixture fixture(options);
            ++c.fixtures;

            x11::DisplayPtr conn = fixture.openDisplay();
            if (!conn) continue;
            Display* d = conn.get();

            Window w = createAndMap(d);
            ++c.maps;
            if (!pollForFrameWithCapture(fixture, d, w, "s", c)) ++c.unframed;
            XDestroyWindow(d, w);
            XSync(d, False);
        } catch (const std::exception&) {
            // A fixture that never reached "ready" is RECORDED, never asserted.
            // A window manager wedged during its own startup is precisely the
            // observation this instrument exists to make.
            ++c.startFailures;
        }
    }

    c.wallMs = elapsedMsSince(started);
    return c;
}

// Mode R -- the repeated-map shape. One fixture, many clients mapped, awaited
// and destroyed in sequence inside it. Every mapped client traverses
// reparent -> activate -> timestamp, and the cached time is invalidated on
// every loop iteration, so every one of those activations takes the cold-cache
// path. Mode R therefore samples the same branch at a fraction of Mode S's
// per-sample cost, and running both is itself informative: trips in R and not
// in S per unit of sampling means the mechanism is per-activation; trips only in
// S means it is startup-adjacent. The two modes' counts are recorded separately
// and are never pooled into one rate.
ModeCounters runModeR()
{
    ModeCounters c;
    const auto started = Clock::now();
    const long budgetMs = static_cast<long>(rWallMs());
    const int fixtures = rFixtures();
    const int mapsPerFixture = rMaps();

    for (int i = 0; i < fixtures; ++i) {
        if (elapsedMsSince(started) >= budgetMs) break;

        try {
            WmFixtureOptions options;
            options.allowTestProcessPtrace = true;
            WmFixture fixture(options);
            ++c.fixtures;

            x11::DisplayPtr conn = fixture.openDisplay();
            if (!conn) continue;
            Display* d = conn.get();

            for (int j = 0; j < mapsPerFixture; ++j) {
                if (elapsedMsSince(started) >= budgetMs) break;
                if (!fixture.wmAlive()) break;

                Window w = createAndMap(d);
                ++c.maps;
                if (!pollForFrameWithCapture(fixture, d, w, "r", c)) ++c.unframed;
                XDestroyWindow(d, w);
                XSync(d, False);
            }
        } catch (const std::exception&) {
            ++c.startFailures;
        }
    }

    c.wallMs = elapsedMsSince(started);
    return c;
}

} // namespace

TEST_CASE("The hang-time capture path yields a five-channel bundle from a healthy window manager",
          "[wm_repro]")
{
    WmFixtureOptions options;
    options.allowTestProcessPtrace = true;
    WmFixture fixture(options);

    x11::DisplayPtr conn = fixture.openDisplay();
    REQUIRE(conn != nullptr);
    Display* d = conn.get();

    Window client = None;
    long elapsedMs = 0;
    bool framed = false;
    const Window frame = mapClientAndPoll(d, client, elapsedMs, framed);
    static_cast<void>(frame);

    // The capture is FORCED, regardless of whether the client framed. That is
    // what makes this case a deterministic exercise of the whole capture path
    // rather than a wait for luck -- and because the window manager is healthy
    // at this moment, the bundle it produces is the CALIBRATION reading: the
    // record of what this host's backtrace looks like when the event loop is
    // behaving. Without it, a later backtrace in the event loop's own poll would
    // be an argument from absence rather than a comparison.
    const std::string dir =
        WmFixture::workDir() + "/repro-control-" + sanitizeDisplay(fixture.display());
    const CaptureOutcome outcome =
        captureBundle(fixture, dir, "control",
                      "calibration: forced capture against a healthy window manager",
                      elapsedMs, framed);

    // BIND FIRST, THEN ANNOTATE, THEN ASSERT -- and do not "tidy" this by moving
    // the annotations back down beside their siblings. A Catch2 annotation is
    // evaluated when the annotation statement executes and annotates only
    // assertions that FOLLOW it, which is exactly why plan 08.5-06's one red run
    // arrived unreadable: its guard sat six lines below the assertion that
    // failed. Every value the assertions below depend on is bound here, before
    // the first annotation.
    const bool bundleExists = pathExists(dir);
    const std::string capText = readFileOrEmpty(dir + "/capability.txt");
    const int channelsWithOneVerdict = wellFormedChannelCount(capText);
    const std::string procState = procStateOf(capText);
    const bool stillAlive = fixture.wmAlive();

    INFO("bundle dir: " << dir);
    INFO("elapsed ms: " << elapsedMs << ", framed: " << (framed ? "yes" : "no"));
    INFO("capture tool status: " << outcome.scriptStatus);
    INFO("capability.txt:\n" << capText);
    INFO("capture-tool.log:\n" << readFileOrEmpty(dir + "/capture-tool.log"));
    INFO("wm stderr: " << fixture.wmStderr());

    REQUIRE(outcome.wrote);
    REQUIRE(bundleExists);

    // Counts channels, deliberately: a bundle that merely EXISTS proves nothing.
    REQUIRE(channelsWithOneVerdict == kChannelCount);

    // The backtrace channel is deliberately NOT asserted PRESENT. A host without
    // a debugger, or one whose kernel refuses the attach, must degrade to a
    // recorded absence rather than a red suite. That this host produced real
    // frames is a host-specific fact and is gated over the committed calibration
    // bundle instead, which is where a host-specific fact belongs.

    REQUIRE(stillAlive);
    REQUIRE_FALSE(procState.empty());
    REQUIRE(procState != "T");
    REQUIRE(procState != "t");

    if (client != None) {
        XDestroyWindow(d, client);
        XSync(d, False);
    }
}

TEST_CASE("The post-readiness reparent is sampled to a stated budget in two modes",
          "[wm_repro]")
{
    const ModeCounters s = runModeS();
    printModeSummary("s", s);

    const ModeCounters r = runModeR();
    printModeSummary("r", r);

    // BIND FIRST, THEN ANNOTATE, THEN ASSERT. See the calibration case above for
    // why the ordering is not stylistic: an annotation covers only assertions
    // that follow it.
    const long totalMaps = s.maps + r.maps;
    const long totalBundles = s.bundles + r.bundles;
    const long totalFiveVerdict = s.bundlesWithFiveVerdicts + r.bundlesWithFiveVerdicts;

    // The readout-integrity reading, per mode and never pooled: a trip that
    // produced no bundle is exactly the unreadable red that ended plan 08.5-06.
    const bool sReadable = (s.trips == 0) || (s.bundles >= 1);
    const bool rReadable = (r.trips == 0) || (r.bundles >= 1);

    INFO("mode s: fixtures-s=" << s.fixtures << " startfail-s=" << s.startFailures
         << " maps-s=" << s.maps << " trips-s=" << s.trips
         << " bundles-s=" << s.bundles << " capped-s=" << s.capped
         << " unframed-s=" << s.unframed << " wallms-s=" << s.wallMs);
    INFO("mode r: fixtures-r=" << r.fixtures << " startfail-r=" << r.startFailures
         << " maps-r=" << r.maps << " trips-r=" << r.trips
         << " bundles-r=" << r.bundles << " capped-r=" << r.capped
         << " unframed-r=" << r.unframed << " wallms-r=" << r.wallMs);
    INFO("bundle root: " << outDirRoot());

    // 1. The anti-vacuity guard. A sampler that sampled nothing proves nothing,
    //    and every plan from 08-07 through 08-14 found a test that did not test
    //    its own name.
    REQUIRE(totalMaps >= 1);

    // 2. The readout-integrity assertion, and the defect class this whole file
    //    exists to close. A client that never framed is RECORDED, not asserted --
    //    the wedge is the observation, not the failure -- but a trip that
    //    produced no bundle is a failure of the instrument itself.
    REQUIRE(sReadable);
    REQUIRE(rReadable);

    // 3. Every bundle written is readable in all five channels, which includes
    //    the window manager being recorded alive or recorded dead at the capture
    //    (the wm-alive channel's own verdict).
    REQUIRE(totalFiveVerdict == totalBundles);
}
