// Resource-budget tests against the REAL compiled wm2-born-again binary
// (plan 08-14, D-32, TEST-08).
//
// PROJECT.md constrains this window manager to "run comfortably in 512MB RAM
// VPS with VNC server". That sentence has been a number in a document for the
// whole project. This file turns it into a gate the suite enforces: a change
// that makes the WM hold ten times as much memory with twenty windows open, or
// that turns its event loop into a poll, fails here rather than on a user's
// droplet.
//
// TWO ASSERTIONS, both from the checklist's "Remote Desktop And VPS Checklist"
// low-resource line and its "No steady CPU spin while idle" line:
//
//   1. RESIDENT MEMORY with twenty simple clients mapped, against a FIXED named
//      budget.
//   2. IDLE CPU over a thirty-second window with those twenty clients mapped and
//      no input at all, under one percent of wall-clock time. This is the
//      observable consequence of the event loop BLOCKING in poll() rather than
//      spinning -- the same property deferred item 6 (circulate() at 100% CPU)
//      violated, and the property a "fix" that merely relocated a busy-wait
//      would still violate.
//
// ---------------------------------------------------------------------------
// THE BUDGET IS NOT DERIVED FROM THE RUN THAT IS JUDGED BY IT
// ---------------------------------------------------------------------------
//
// A threshold computed from the same measurement it is judging asserts nothing:
// it passes by construction, whatever the program does. So this file uses a
// deliberate TWO-STAGE workflow, and the two stages are in different places:
//
//   Stage 1, CALIBRATION -- the case tagged [.][wm_resource_calibration] below.
//   It is HIDDEN (the leading `.`), so Catch2 does not list it and
//   catch_discover_tests therefore never registers it with ctest. It can only
//   be run by asking for it by name:
//
//       ./build/debug/test_wm_resource '[wm_resource_calibration]' -s
//       ./build/asan/test_wm_resource  '[wm_resource_calibration]' -s
//
//   It measures and PRINTS the zero/one/five/twenty-client figures and asserts
//   nothing about them. Its output is committed under the phase evidence
//   directory.
//
//   Stage 2, JUDGEMENT -- the cases tagged [wm_resource_budget]. They compare
//   against the constants below, which a human wrote after reading stage 1's
//   output. They never recompute a threshold.
//
// Being hidden is the mechanism, not a convenience: it is what makes it
// STRUCTURALLY impossible for the judged run to have produced its own bar.
//
// If the host or the build mode changes materially, re-run stage 1, commit the
// new transcript, and change the constant DELIBERATELY. Widening the budget
// because a run exceeded it is the one thing this file exists to prevent.
//
// ---------------------------------------------------------------------------
// Methodology inherited from 08-04 .. 08-13, none of it optional
// ---------------------------------------------------------------------------
//
//   NO sleep()-BASED SYNCHRONISATION for observation. Every wait whose exit
//   condition is a real observation is a deadline-bounded poll. The thirty-second
//   idle window is the sole deliberate wall-clock interval, because the elapsed
//   time IS the thing under test -- and during it the driver issues NO X traffic
//   whatever, since any request would be work the WM is then charged for.
//
//   NON-EVENTS ARE PROVEN BY SETTLING FIRST (deferred item 9). Every read that
//   follows a batch of mapping runs settleWm() -- fifteen SPACED nudges -- first.
//
//   DEFERRED ITEM 7 IS ACCOUNTED FOR, not worked around: the WM adopts its own
//   menu, submenu and EWMH check windows as clients, so _NET_CLIENT_LIST always
//   carries entries this file did not create. Every list assertion here filters
//   to the windows the case owns.

#include <catch2/catch_test_macros.hpp>

#include "support/WmFixture.h"

#include "x11wrap.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace wm2test;

namespace {

// A parking spot for the pointer clear of every window this file maps. Set
// BEFORE anything is mapped: the pointer starts at the screen centre, and a
// window mapped underneath it would generate an EnterNotify -- which is real
// work for the WM and would be charged to an idle sample that is supposed to
// measure a WM with nothing to do.
constexpr int kParkX = 5;
constexpr int kParkY = 5;

// The client counts the measurement ladder steps through. Twenty is the figure
// the D-32 budget is stated against; the intermediate steps exist so a reader of
// the calibration transcript can see whether cost is per-client or one-off.
constexpr int kStep1  = 1;
constexpr int kStep5  = 5;
constexpr int kStep20 = 20;

// The idle sampling window. The plan's floor is thirty seconds; a longer window
// makes the one-percent bar finer-grained (at 100 Hz, 30 s of wall clock allows
// 30 ticks and no fewer, so a shorter window would quantise the bar coarsely).
constexpr int kIdleWindowSeconds = 30;

// ---------------------------------------------------------------------------
// THE FIXED BUDGETS
//
// Written down by a human after reading the stage-1 calibration transcript
// committed at .planning/phases/08-xrandr-vnc-compatibility-focus-rules/
// evidence/resource-budget/, and deliberately NOT derived from any measurement
// the judged cases take.
//
// CALIBRATION PROVENANCE -- kept next to the numbers so it cannot drift away
// from them:
//
//   date:   2026-08-29
//   host:   tomislav-HP-Z440-Workstation, Ubuntu 22.04.5 LTS, Linux 6.8.0-124
//   server: Xvfb 1024x768x24, one WmFixture instance per measurement
//   binary: build/{debug,asan}/wm2-born-again at commit 0e56fc8
//
// MEASURED (resident set size, /proc/<pid>/statm, ladder of 0/1/5/20 clients):
//
//   debug tree     11724 -> 11724 -> 11748 -> 11840 kB
//   asan  tree     34736 -> 34744 -> 34916 -> 35300 kB
//
// The shape of that ladder is worth reading before arguing with the numbers:
// almost all of it is STARTUP -- the Xft font, the colours, the application
// cache -- and a managed window costs roughly SIX KILOBYTES. Twenty windows add
// under 1% to the figure. So this budget is overwhelmingly a bound on what the
// WM holds at rest, and only marginally a bound on per-window cost. A future
// regression in per-window cost would have to be very large to breach it; a
// regression in startup cost would breach it immediately. Both are worth
// catching, but a reader should not mistake this for a tight per-client bound.
//
// HOW THE BUDGET WAS CHOSEN, so a future reader can argue with it rather than
// guess at it:
//
//   The number that matters to the project is the 512 MB VPS. A window manager
//   is one process among a VNC server, a desktop's worth of applications and the
//   OS, so a defensible ceiling for the WM with a working set of twenty windows
//   is a low single-digit share of that box. 24 MB is 4.7% of 512 MB, and the
//   debug tree measures less than half of it.
//
//   The budget is stated as an ABSOLUTE resident figure, not as growth from a
//   baseline, precisely because the ladder above shows growth is the small term.
//   A growth budget here would pass a WM that held 300 MB at startup.
//
//   The sanitizer tree gets its own, larger constant rather than one loose
//   number that would be meaningless in the debug tree: every allocation gains
//   redzones, freed memory sits in a quarantine, and the shadow map is charged
//   to RSS. That build is a diagnostic tool and never ships, so its budget
//   exists only to catch a REGRESSION in that tree, not to certify a VPS.
//
// If a run exceeds these, that is a FINDING. It is not a budget to raise.
// ---------------------------------------------------------------------------

constexpr long kRssBudgetKbDebug = 24576;    // 24 MB -- 4.7% of the 512 MB target
constexpr long kRssBudgetKbAsan  = 98304;    // 96 MB -- diagnostic tree only

#if defined(__SANITIZE_ADDRESS__)
constexpr long kRssBudgetKb = kRssBudgetKbAsan;
constexpr const char* kBudgetTree = "asan";
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
constexpr long kRssBudgetKb = kRssBudgetKbAsan;
constexpr const char* kBudgetTree = "asan";
#  else
constexpr long kRssBudgetKb = kRssBudgetKbDebug;
constexpr const char* kBudgetTree = "debug";
#  endif
#else
constexpr long kRssBudgetKb = kRssBudgetKbDebug;
constexpr const char* kBudgetTree = "debug";
#endif

// The idle-CPU bar, as a percentage of wall clock. One percent, per the plan.
// It is the SAME number in both trees on purpose: a blocked poll() costs the
// same nothing whether or not the binary is instrumented, so a sanitizer-
// specific allowance here would be an allowance for a busy-wait.
constexpr double kIdleCpuPercentBudget = 1.0;

// ---------------------------------------------------------------------------
// Settling (deferred item 9)
// ---------------------------------------------------------------------------

// Wake the WM's event loop so it flushes its X output buffer. The nudge is
// override-redirect, so WindowManager::eventCreate returns immediately for it
// and it can never be managed or perturb a measurement.
void pumpWm(Display* d)
{
    XSetWindowAttributes attr;
    attr.override_redirect = True;
    Window nudge = XCreateWindow(d, DefaultRootWindow(d), -20, -20, 1, 1, 0,
                                 CopyFromParent, InputOnly, CopyFromParent,
                                 CWOverrideRedirect, &attr);
    XSync(d, False);
    XDestroyWindow(d, nudge);
    XSync(d, False);
}

// Fifteen nudges at 20 ms intervals -- the shape tests/test_wm_focus.cpp arrived
// at after a TIGHT loop of the same length still read a stale value.
void settleWm(Display* d)
{
    for (int i = 0; i < 15; ++i) {
        pumpWm(d);
        pollSleep();
    }
}

// ---------------------------------------------------------------------------
// Server observation
// ---------------------------------------------------------------------------

Window parentOf(Display* d, Window w)
{
    Window wroot = None, parent = None, *children = nullptr;
    unsigned int n = 0;
    if (!XQueryTree(d, w, &wroot, &parent, &children, &n)) return None;
    if (children) XFree(children);
    return parent;
}

// _NET_CLIENT_LIST, in the order the WM published it.
std::vector<Window> clientList(Display* d)
{
    static Atom prop = None;
    if (prop == None) prop = XInternAtom(d, "_NET_CLIENT_LIST", False);

    std::vector<Window> out;
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long nItems = 0, bytesAfter = 0;
    unsigned char* raw = nullptr;

    if (XGetWindowProperty(d, DefaultRootWindow(d), prop, 0, 1024, False, XA_WINDOW,
                           &actualType, &actualFormat, &nItems, &bytesAfter,
                           &raw) != Success) {
        return out;
    }
    if (raw && actualType == XA_WINDOW && actualFormat == 32) {
        Window* vals = reinterpret_cast<Window*>(raw);
        out.assign(vals, vals + nItems);
    }
    if (raw) XFree(raw);
    return out;
}

// True once every window in `wins` is published in _NET_CLIENT_LIST. Reads the
// whole list once per poll rather than querying each window.
bool allListed(Display* d, const std::vector<Window>& wins)
{
    const std::vector<Window> l = clientList(d);
    for (Window w : wins) {
        if (std::find(l.begin(), l.end(), w) == l.end()) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Process measurement
// ---------------------------------------------------------------------------

// Resident set size in kilobytes, from /proc/<pid>/statm field 2 (resident
// pages). Chosen over VmSize because virtual size says nothing about what is
// actually held -- ASan alone reserves an enormous virtual mapping that no VPS
// ever commits.
bool residentKb(pid_t pid, long& out)
{
    if (pid <= 0) return false;
    const std::string path = "/proc/" + std::to_string(pid) + "/statm";
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    long total = 0, resident = 0;
    const int n = std::fscanf(f, "%ld %ld", &total, &resident);
    std::fclose(f);
    if (n != 2) return false;
    out = resident * (::sysconf(_SC_PAGESIZE) / 1024);
    return true;
}

// ---------------------------------------------------------------------------
// The measurement ladder
// ---------------------------------------------------------------------------

Window createClient(Display* d, int x, int y, int w, int h, const char* name)
{
    Window root = DefaultRootWindow(d);
    Window win = XCreateSimpleWindow(d, root, x, y,
                                     static_cast<unsigned>(w), static_cast<unsigned>(h), 0,
                                     BlackPixel(d, DefaultScreen(d)),
                                     WhitePixel(d, DefaultScreen(d)));
    if (name) XStoreName(d, win, name);
    XSync(d, False);
    return win;
}

// Map `count` further simple clients and wait until the WM has framed and
// published every one of them, so the measurement that follows is taken against
// a WM that has finished the work rather than one that is still mid-batch.
// Returns false if any of them was never framed.
bool mapClients(Display* d, int count, std::vector<Window>& owned)
{
    std::vector<Window> batch;
    batch.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const int n = static_cast<int>(owned.size()) + i;
        const int x = 20 + (n % 8) * 110;
        const int y = 40 + (n / 8) * 130;
        Window win = createClient(d, x, y, 80, 60, "budget");
        XMapWindow(d, win);
        batch.push_back(win);
    }
    XSync(d, False);

    const bool framed = WmFixture::pollUntil([&] {
        pumpWm(d);
        for (Window w : batch) {
            const Window p = parentOf(d, w);
            if (p == None || p == DefaultRootWindow(d)) return false;
        }
        return true;
    }, 30000);

    const bool listedAll = WmFixture::pollUntil([&] {
        pumpWm(d);
        return allListed(d, batch);
    }, 30000);

    owned.insert(owned.end(), batch.begin(), batch.end());
    settleWm(d);
    return framed && listedAll;
}

// One rung of the ladder: how much the WM is holding with `owned.size()` clients
// mapped and framed. `-1` means the read failed.
long measureAt(WmFixture& fixture, Display* d, std::vector<Window>& owned, int addCount)
{
    if (addCount > 0 && !mapClients(d, addCount, owned)) return -1;
    settleWm(d);
    long kb = 0;
    if (!residentKb(fixture.wm().pid(), kb)) return -1;
    return kb;
}

struct Ladder {
    long rss0 = -1;
    long rss1 = -1;
    long rss5 = -1;
    long rss20 = -1;
};

// Runs the whole ladder against one fixture and leaves twenty clients mapped.
Ladder runLadder(WmFixture& fixture, Display* d, std::vector<Window>& owned)
{
    Ladder l;
    l.rss0  = measureAt(fixture, d, owned, 0);
    l.rss1  = measureAt(fixture, d, owned, kStep1);
    l.rss5  = measureAt(fixture, d, owned, kStep5 - kStep1);
    l.rss20 = measureAt(fixture, d, owned, kStep20 - kStep5);
    return l;
}

void reportLadder(const char* tree, const Ladder& l)
{
    // Printed with std::printf rather than only through INFO so the figures are
    // in the transcript of a PASSING run too -- Catch2 discards INFO on success,
    // and a budget whose measurements are only visible on failure cannot be
    // calibrated from.
    std::printf("\n"
                "[wm2 resource] tree=%s\n"
                "[wm2 resource]   resident set size, twenty-client ladder\n"
                "[wm2 resource]     0 clients   %ld kB\n"
                "[wm2 resource]     1 client    %ld kB\n"
                "[wm2 resource]     5 clients   %ld kB\n"
                "[wm2 resource]    20 clients   %ld kB\n",
                tree, l.rss0, l.rss1, l.rss5, l.rss20);
    std::fflush(stdout);
}

// Accumulated CPU time of the WM over a quiet window, expressed as a percentage
// of the wall clock the window actually took.
//
// NOTHING in here touches the X connection. A single request would be work the
// WM is then legitimately charged for, and the sample would measure the driver
// rather than the window manager.
struct IdleSample {
    bool ok = false;
    unsigned long long ticksBefore = 0;
    unsigned long long ticksAfter = 0;
    double wallSeconds = 0.0;
    double cpuSeconds = 0.0;
    double percent = 0.0;
    long ticksPerSecond = 0;
};

IdleSample sampleIdleCpu(pid_t pid, int windowSeconds)
{
    IdleSample s;
    s.ticksPerSecond = ::sysconf(_SC_CLK_TCK);
    if (s.ticksPerSecond <= 0) return s;

    if (!processCpuTicks(pid, s.ticksBefore)) return s;
    const auto start = Clock::now();

    // A plain blocking sleep is correct here and a polled loop would not be:
    // the point of the window is that NOTHING happens in it.
    std::this_thread::sleep_for(std::chrono::seconds(windowSeconds));

    const auto end = Clock::now();
    if (!processCpuTicks(pid, s.ticksAfter)) return s;

    s.wallSeconds = std::chrono::duration<double>(end - start).count();
    s.cpuSeconds  = static_cast<double>(s.ticksAfter - s.ticksBefore) /
                    static_cast<double>(s.ticksPerSecond);
    s.percent     = (s.wallSeconds > 0.0) ? (s.cpuSeconds / s.wallSeconds) * 100.0 : 0.0;
    s.ok = true;
    return s;
}

void reportIdle(const char* what, const IdleSample& s)
{
    std::printf("[wm2 resource]   idle CPU (%s)\n"
                "[wm2 resource]     window        %.2f s wall clock, no X traffic\n"
                "[wm2 resource]     CPU ticks     %llu -> %llu (%llu, at %ld Hz)\n"
                "[wm2 resource]     CPU time      %.3f s\n"
                "[wm2 resource]     as %% of wall  %.4f %%   (budget %.2f %%)\n",
                what, s.wallSeconds,
                s.ticksBefore, s.ticksAfter, s.ticksAfter - s.ticksBefore, s.ticksPerSecond,
                s.cpuSeconds, s.percent, kIdleCpuPercentBudget);
    std::fflush(stdout);
}

}  // namespace


// ===========================================================================
// Stage 1 -- CALIBRATION. Hidden (`[.]`), so it is not listed by Catch2 and
// therefore never registered with ctest. Run it BY NAME:
//
//     ./build/debug/test_wm_resource '[wm_resource_calibration]' -s
//
// It measures and prints. It asserts nothing about the figures, which is the
// whole point: the numbers a human then writes into kRssBudgetKb* above must
// not have been produced by a run that was itself being judged.
// ===========================================================================

TEST_CASE("CALIBRATION: resident memory ladder and idle CPU, measured and printed, "
          "never judged", "[.][wm_resource_calibration]")
{
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();

    XWarpPointer(d, None, DefaultRootWindow(d), 0, 0, 0, 0, kParkX, kParkY);
    XSync(d, False);
    settleWm(d);

    std::vector<Window> owned;
    const Ladder l = runLadder(fixture, d, owned);
    reportLadder(kBudgetTree, l);

    REQUIRE(l.rss20 > 0);
    std::printf("[wm2 resource]   (calibration -- no budget applied)\n");

    const IdleSample idle = sampleIdleCpu(fixture.wm().pid(), kIdleWindowSeconds);
    REQUIRE(idle.ok);
    reportIdle("default config, 20 clients", idle);

    // The WM must still be alive and still be a window manager afterwards --
    // a process that died mid-window would report a flattering zero.
    CHECK(fixture.wmAlive());
}


// ===========================================================================
// Stage 2 -- JUDGEMENT. These are the registered [wm_resource_budget] cases.
// ===========================================================================

TEST_CASE("Resident memory with twenty clients mapped stays inside the fixed, "
          "pre-committed budget", "[wm_resource_budget]")
{
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();

    XWarpPointer(d, None, DefaultRootWindow(d), 0, 0, 0, 0, kParkX, kParkY);
    XSync(d, False);
    settleWm(d);

    std::vector<Window> owned;
    const Ladder l = runLadder(fixture, d, owned);
    reportLadder(kBudgetTree, l);

    // The whole ladder is recorded, but only the twenty-client figure is judged
    // -- that is the working set the 512 MB constraint is stated against.
    std::printf("[wm2 resource]   FIXED budget  %ld kB  (tree=%s, calibrated 2026-08-29 "
                "on tomislav-HP-Z440-Workstation)\n"
                "[wm2 resource]   RESULT        %ld kB\n",
                kRssBudgetKb, kBudgetTree, l.rss20);
    std::fflush(stdout);

    REQUIRE(l.rss0 > 0);
    REQUIRE(l.rss1 > 0);
    REQUIRE(l.rss5 > 0);
    REQUIRE(l.rss20 > 0);

    // Every one of the twenty is really mapped and really managed. Without this
    // the memory assertion could pass because the clients never arrived --
    // deferred item 7 means the list is never empty, so its filtered form is
    // what carries the claim.
    const std::vector<Window> published = clientList(d);
    int mine = 0;
    for (Window w : owned) {
        if (std::find(published.begin(), published.end(), w) != published.end()) ++mine;
    }
    INFO("clients this case owns that the WM published: " << mine << " of " << owned.size());
    REQUIRE(owned.size() == static_cast<size_t>(kStep20));
    CHECK(mine == kStep20);

    INFO("tree " << kBudgetTree
         << ": twenty-client resident set " << l.rss20
         << " kB, fixed budget " << kRssBudgetKb << " kB");
    CHECK(l.rss20 < kRssBudgetKb);
}

TEST_CASE("Idle CPU with twenty clients mapped and no input stays under one percent "
          "of wall clock", "[wm_resource_budget]")
{
    WmFixture fixture;
    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();

    XWarpPointer(d, None, DefaultRootWindow(d), 0, 0, 0, 0, kParkX, kParkY);
    XSync(d, False);
    settleWm(d);

    std::vector<Window> owned;
    REQUIRE(mapClients(d, kStep20, owned));
    REQUIRE(owned.size() == static_cast<size_t>(kStep20));

    // A positive control for the measurement itself: the WM has just done real
    // work framing twenty windows, so it must have accrued SOME CPU. A sampler
    // that reported zero for a busy process would make the idle assertion below
    // pass for the wrong reason.
    unsigned long long afterWork = 0;
    REQUIRE(processCpuTicks(fixture.wm().pid(), afterWork));
    INFO("CPU ticks accrued while framing twenty windows: " << afterWork);
    CHECK(afterWork > 0);

    const IdleSample idle = sampleIdleCpu(fixture.wm().pid(), kIdleWindowSeconds);
    REQUIRE(idle.ok);
    reportIdle("default config, 20 clients", idle);

    CHECK(idle.wallSeconds >= static_cast<double>(kIdleWindowSeconds));
    CHECK(fixture.wmAlive());

    INFO("idle CPU " << idle.percent << " % of wall clock over "
         << idle.wallSeconds << " s, budget " << kIdleCpuPercentBudget << " %");
    CHECK(idle.percent < kIdleCpuPercentBudget);
}

TEST_CASE("Idle CPU stays under one percent with auto-raise disabled",
          "[wm_resource_budget]")
{
    // The auto-raise-off configuration gated in plan 08-07. It is measured
    // separately because turning the timer OFF is the configuration in which a
    // busy-wait would be least excusable and most easily overlooked: with
    // auto-raise on, a periodic wake-up is at least explicable.
    WmFixtureOptions options;
    options.wmArgs = {"--no-auto-raise"};
    WmFixture fixture(options);

    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();

    XWarpPointer(d, None, DefaultRootWindow(d), 0, 0, 0, 0, kParkX, kParkY);
    XSync(d, False);
    settleWm(d);

    std::vector<Window> owned;
    REQUIRE(mapClients(d, kStep20, owned));
    REQUIRE(owned.size() == static_cast<size_t>(kStep20));

    const IdleSample idle = sampleIdleCpu(fixture.wm().pid(), kIdleWindowSeconds);
    REQUIRE(idle.ok);
    reportIdle("--no-auto-raise, 20 clients", idle);

    CHECK(fixture.wmAlive());

    INFO("idle CPU (auto-raise off) " << idle.percent << " % of wall clock over "
         << idle.wallSeconds << " s, budget " << kIdleCpuPercentBudget << " %");
    CHECK(idle.percent < kIdleCpuPercentBudget);
}
