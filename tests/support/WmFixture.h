#pragma once

// WmFixture -- process-level RAII harness for the real wm2-born-again binary.
//
// D-07: WindowManager runs its event loop inside its own constructor
// (src/Manager.cpp:59-176), so it cannot be unit-tested in-process. Every
// behavioral assertion against the real WM therefore forks/execs the compiled
// binary onto a display this fixture owns exclusively.
//
// Pitfall 3 (08-RESEARCH.md): a running WM holds SubstructureRedirectMask on
// root exclusively and reparents every window created on that display. Sharing
// :99 with test_client / test_ewmh / test_smoke / test_xft_poc would corrupt
// them. Each fixture instance allocates and locks its OWN display.
//
// Pitfall 4 (08-RESEARCH.md): sanitizer state is per-process and is read from
// the environment at runtime init, which execv() rebuilds. ASAN_OPTIONS,
// LSAN_OPTIONS and UBSAN_OPTIONS are therefore setenv'd explicitly in the child
// between fork() and execv(), with log_path under the CMake binary directory
// (never a bare fixed /tmp name -- threat T-8-TMP) and a distinctive exitcode so
// a child sanitizer report becomes a test failure instead of being swallowed.
//
// No sleep()-based synchronisation anywhere: readiness is a property poll with a
// wall-clock deadline, and every child reap is deadline-bounded with a SIGKILL
// escalation.

#include "x11wrap.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef WM2_BINARY_PATH
#error "WM2_BINARY_PATH must be defined by the build system"
#endif
#ifndef WM2_LSAN_SUPPRESSIONS
#error "WM2_LSAN_SUPPRESSIONS must be defined by the build system"
#endif
#ifndef WM2_TEST_WORKDIR
#error "WM2_TEST_WORKDIR must be defined by the build system"
#endif

namespace wm2test {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

using Clock = std::chrono::steady_clock;

// Poll interval for every deadline loop. Not a synchronisation delay -- the
// loop's exit condition is always a real observation, never elapsed time.
inline constexpr int kPollIntervalMs = 20;

inline void pollSleep()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
}

inline bool pathExists(const std::string& p)
{
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}

// Total CPU time (user + system) a process has consumed, in clock ticks, read
// from /proc/<pid>/stat. Returns false if the process is gone or /proc cannot
// be read.
//
// This is the only way to assert "the WM is IDLE" as opposed to "the WM is
// still alive". A wedged event loop and a healthy blocked one are
// indistinguishable from the outside -- both answer no X requests and both keep
// their process alive -- but one of them burns a core. Two samples with a known
// wall-clock gap between them separate the two cases with no ambiguity.
//
// Field parsing starts after the LAST ')' rather than at a fixed token offset:
// the comm field is parenthesised and may itself contain spaces and
// parentheses, which is the classic way naive /proc/stat parsers go wrong.
// Between comm and utime lie eleven fields (state, ppid, pgrp, session, tty_nr,
// tpgid, flags, minflt, cminflt, majflt, cmajflt).
inline bool processCpuTicks(pid_t pid, unsigned long long& ticks)
{
    if (pid <= 0) return false;

    const std::string path = "/proc/" + std::to_string(pid) + "/stat";
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;

    std::string content;
    char buf[1024];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) content.append(buf, n);
    std::fclose(f);

    const size_t close = content.find_last_of(')');
    if (close == std::string::npos) return false;

    std::istringstream in(content.substr(close + 1));
    std::string token;
    for (int i = 0; i < 11; ++i) {
        if (!(in >> token)) return false;
    }

    unsigned long long utime = 0, stime = 0;
    if (!(in >> utime >> stime)) return false;

    ticks = utime + stime;
    return true;
}

// Move-only fd handle, same shape as FdGuard in include/Manager.h:20-33.
class FdHandle {
public:
    FdHandle() = default;
    explicit FdHandle(int fd) : m_fd(fd) {}
    ~FdHandle() { reset(); }

    FdHandle(const FdHandle&) = delete;
    FdHandle& operator=(const FdHandle&) = delete;

    FdHandle(FdHandle&& o) noexcept : m_fd(o.m_fd) { o.m_fd = -1; }
    FdHandle& operator=(FdHandle&& o) noexcept {
        if (this != &o) { reset(); m_fd = o.m_fd; o.m_fd = -1; }
        return *this;
    }

    void reset() noexcept {
        if (m_fd >= 0) ::close(m_fd);
        m_fd = -1;
    }

    int get() const noexcept { return m_fd; }
    explicit operator bool() const noexcept { return m_fd >= 0; }

private:
    int m_fd = -1;
};

// ---------------------------------------------------------------------------
// Display reservation
//
// A scan alone is NOT a reservation: two concurrent ctest workers can both see
// :137 as free and both try to claim it. The claim is an exclusive flock() on a
// per-display lock file under the CMake binary directory, held from reservation
// through Xvfb startup and for the fixture's whole lifetime.
// ---------------------------------------------------------------------------

class DisplayReservation {
public:
    DisplayReservation() = default;

    // Try to reserve display number n. Returns false if another fixture holds
    // it, or if the X server's own lock/socket for n already exists.
    bool tryReserve(int n)
    {
        if (pathExists("/tmp/.X" + std::to_string(n) + "-lock")) return false;
        if (pathExists("/tmp/.X11-unix/X" + std::to_string(n))) return false;

        const std::string lockDir = std::string(WM2_TEST_WORKDIR) + "/display-locks";
        ::mkdir(lockDir.c_str(), 0700);

        const std::string lockPath = lockDir + "/display-" + std::to_string(n) + ".lock";
        int fd = ::open(lockPath.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (fd < 0) return false;

        if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
            ::close(fd);
            return false;   // another fixture in this ctest run owns it
        }

        // Re-check after taking the lock: an external X server may have claimed
        // the display between the scan and the flock.
        if (pathExists("/tmp/.X" + std::to_string(n) + "-lock")) {
            ::close(fd);
            return false;
        }

        m_lock = FdHandle(fd);
        m_number = n;
        return true;
    }

    void release() { m_lock.reset(); m_number = -1; }

    int number() const { return m_number; }
    bool held() const { return static_cast<bool>(m_lock); }
    std::string displayString() const { return ":" + std::to_string(m_number); }

private:
    FdHandle m_lock;
    int m_number = -1;
};

// ---------------------------------------------------------------------------
// Child process handle with deadline-bounded, escalating shutdown
// ---------------------------------------------------------------------------

class ChildProcess {
public:
    ChildProcess() = default;
    explicit ChildProcess(pid_t pid) : m_pid(pid) {}

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    ChildProcess(ChildProcess&& o) noexcept
        : m_pid(o.m_pid), m_reaped(o.m_reaped), m_status(o.m_status) { o.m_pid = -1; }
    ChildProcess& operator=(ChildProcess&& o) noexcept {
        if (this != &o) {
            shutdown();
            m_pid = o.m_pid; m_reaped = o.m_reaped; m_status = o.m_status;
            o.m_pid = -1;
        }
        return *this;
    }

    ~ChildProcess() { shutdown(); }

    pid_t pid() const { return m_pid; }
    bool alive() const { return m_pid > 0 && !m_reaped; }

    // Non-blocking reap. Returns true once the child has been collected.
    bool tryReap()
    {
        if (m_pid <= 0 || m_reaped) return m_reaped;
        int status = 0;
        pid_t r = ::waitpid(m_pid, &status, WNOHANG);
        if (r == m_pid) { m_reaped = true; m_status = status; }
        else if (r < 0 && errno == ECHILD) { m_reaped = true; m_status = 0; }
        return m_reaped;
    }

    // SIGTERM, poll to deadline, escalate to SIGKILL. Never blocks forever --
    // a child that ignores SIGTERM is force-reaped before this returns.
    void shutdown(int timeoutMs = 5000)
    {
        if (m_pid <= 0 || m_reaped) return;

        ::kill(m_pid, SIGTERM);
        if (waitForExit(timeoutMs)) return;

        ::kill(m_pid, SIGKILL);
        waitForExit(2000);
        if (!m_reaped) {
            // Last resort: a blocking reap. SIGKILL is uncatchable, so this
            // cannot hang except on an unkillable (D-state) process.
            int status = 0;
            if (::waitpid(m_pid, &status, 0) == m_pid) { m_reaped = true; m_status = status; }
        }
    }

    bool waitForExit(int timeoutMs)
    {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        while (Clock::now() < deadline) {
            if (tryReap()) return true;
            pollSleep();
        }
        return tryReap();
    }

    bool reaped() const { return m_reaped; }
    int rawStatus() const { return m_status; }
    bool exitedNormally() const { return m_reaped && WIFEXITED(m_status); }
    int exitCode() const { return WIFEXITED(m_status) ? WEXITSTATUS(m_status) : -1; }
    int termSignal() const { return WIFSIGNALED(m_status) ? WTERMSIG(m_status) : 0; }

private:
    pid_t m_pid = -1;
    bool m_reaped = false;
    int m_status = 0;
};

// ---------------------------------------------------------------------------
// WmFixture
// ---------------------------------------------------------------------------

struct WmFixtureOptions {
    std::vector<std::string> wmArgs;                    // extra wm2 arguments
    std::vector<std::string> xvfbArgs;                  // e.g. {"-extension","RANDR"}
    std::map<std::string, std::string> childEnv;        // explicit child env overrides
    int readinessTimeoutMs = 10000;
    int serverTimeoutMs = 10000;

    // Opt-in (plan 08.5-09, TEST-08): let the TEST PROCESS -- and only the test
    // process and its descendants -- attach a debugger to the forked window
    // manager. Defaults false, so the ordinary suite is byte-identical to what
    // it was: no fixture that does not ask for this changes behaviour in any
    // way. See the declaration site inside spawnWm() for why the narrow form is
    // used and what it does not grant.
    bool allowTestProcessPtrace = false;
};

class WmFixture {
public:
    // Distinguishes the startup stages so a failure names its own cause instead
    // of being reported as a generic "WM not ready".
    enum class ReadyResult {
        Ready,
        NoConnection,           // could not open the fixture display at all
        NoWmCheckProperty,      // _NET_SUPPORTING_WM_CHECK never appeared on root
        WmDied,                 // the WM child exited during startup
        EventLoopNotPumping,    // property published but a mapped window never got reparented
    };

    static const char* readyStageName(ReadyResult r)
    {
        switch (r) {
        case ReadyResult::Ready:               return "ready";
        case ReadyResult::NoConnection:        return "could not open display";
        case ReadyResult::NoWmCheckProperty:   return "_NET_SUPPORTING_WM_CHECK never published";
        case ReadyResult::WmDied:              return "WM child exited during startup";
        case ReadyResult::EventLoopNotPumping: return "event loop not pumping (probe window never reparented)";
        }
        return "unknown";
    }

    explicit WmFixture(WmFixtureOptions options = {})
        : m_options(std::move(options))
    {
        // ASan slows startup substantially; give the deadlines room without
        // making them unbounded.
        start();
    }

    WmFixture(const WmFixture&) = delete;
    WmFixture& operator=(const WmFixture&) = delete;

    ~WmFixture()
    {
        // Order matters: reap the WM (and collect any sanitizer report it wrote)
        // BEFORE tearing down the X server it is talking to.
        m_wm.shutdown();
        collectStderr();
        m_asanReports = globAsanReports();
        // Drop every connection before killing the server they talk to.
        m_readyConn.reset();
        m_keepAlive.reset();
        m_xvfb.shutdown();
        m_reservation.release();
    }

    // The fixture's own display -- tests MUST use this, never getenv("DISPLAY").
    const std::string& display() const { return m_display; }

    // Open a fresh assertion connection to the fixture display.
    x11::DisplayPtr openDisplay() const
    {
        return x11::DisplayPtr(XOpenDisplay(m_display.c_str()));
    }

    bool wmAlive() { return !m_wm.tryReap(); }
    ChildProcess& wm() { return m_wm; }

    // Send SIGTERM to the WM and wait for a clean exit. Returns true only on a
    // normal exit with status 0 -- not the ASan exitcode sentinel, not a signal
    // death.
    bool terminateWmCleanly(int timeoutMs = 8000)
    {
        if (m_wm.pid() <= 0) return false;
        ::kill(m_wm.pid(), SIGTERM);
        if (!m_wm.waitForExit(timeoutMs)) return false;
        return m_wm.exitedNormally() && m_wm.exitCode() == 0;
    }

    // Captured child stderr, drained from a file (never a pipe that could fill
    // and deadlock the child).
    const std::string& wmStderr() { collectStderr(); return m_stderr; }

    std::vector<std::string> asanReports() const { return globAsanReports(); }
    const std::string& asanLogPrefix() const { return m_asanLogPrefix; }

    // Deferred item 14 (recorded by 08-12, fixed here in 08-13).
    //
    // ASan writes its report to "<log_path>.<pid>", and globAsanReports() finds
    // reports by globbing "<workdir>/asan<display>*". The display number is
    // drawn from a small pool that is reused across runs, so a report written by
    // an EARLIER run on :122 is attributed to the NEXT fixture handed :122 --
    // which then fails at `CHECK(fixture.asanReports().empty())` with a clean WM
    // and no fault of its own. Measured in 08-12: 9 of 9 cases red on a tree
    // whose source was back at its correct state, green again the moment five
    // files were deleted.
    //
    // A green tree never writes a report, so the files only accumulate when
    // someone is deliberately breaking the WM -- which is exactly what mutation
    // testing is, and mutation testing is how every defect in plans 08-11..13
    // was confirmed. Left unfixed it makes a mutation run poison every later run
    // of the same group with a failure that looks precisely like a regression in
    // the code under test.
    //
    // Exposed as a static helper rather than buried in start() so it is directly
    // testable: the fixture cannot be handed a chosen display number, but this
    // can be handed a chosen prefix.
    static void removeReportsWithPrefix(const std::string& prefix)
    {
        for (const auto& path : globWithPrefix(prefix)) {
            ::unlink(path.c_str());
        }
    }

    // Poll until `pred()` is true or the deadline expires. The exit condition is
    // always a real observation.
    template <typename Pred>
    static bool pollUntil(Pred pred, int timeoutMs)
    {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        for (;;) {
            if (pred()) return true;
            if (Clock::now() >= deadline) return false;
            pollSleep();
        }
    }

    // A LIVE diagnostics snapshot (plan 08.5-09), taken while both children are
    // still running rather than after a failed startup.
    //
    // The refresh is the whole point. diagnostics() reads the cached stderr
    // string; before this plan it did not repopulate it, because its only caller
    // was start()'s failure path, which drains first. A snapshot taken mid-run
    // would therefore have reported whatever was last drained -- which for a
    // fixture that has never failed is nothing at all.
    std::string diagnosticsSnapshot()
    {
        collectStderr();
        return diagnostics();
    }

private:
    void start()
    {
        if (!allocateDisplay()) {
            throw std::runtime_error("WmFixture: could not reserve a free X display");
        }
        if (!spawnWm()) {
            throw std::runtime_error("WmFixture: could not start wm2-born-again on " + m_display);
        }
        const ReadyResult ready = waitForWmReadyStaged();
        if (ready != ReadyResult::Ready) {
            collectStderr();
            throw std::runtime_error(
                std::string("WmFixture: WM startup failed at stage: ") +
                readyStageName(ready) + " on " + m_display + "\n" + diagnostics());
        }
    }

    // Scan upward from a base offset well clear of :0/:99, reserving with an
    // exclusive flock. If Xvfb loses a race to an external process, release and
    // advance rather than reusing the failed display.
    bool allocateDisplay()
    {
        constexpr int kBase = 120;
        constexpr int kMaxCandidates = 80;

        for (int n = kBase; n < kBase + kMaxCandidates; ++n) {
            if (!m_reservation.tryReserve(n)) continue;

            m_display = m_reservation.displayString();
            if (spawnXvfb() && waitForXServer()) return true;

            // Lost the display, or the server died on startup. Drop everything
            // and try the next candidate.
            //
            // m_keepAlive is dropped FIRST and is not optional. waitForXServer()
            // may have already opened it (it is the connection held so Xvfb
            // never sees zero clients), and it can succeed on a server that then
            // fails the rest of this branch. Leaving it set carries a live
            // DisplayPtr to a server we are about to shut down into the next
            // candidate's attempt -- a stale connection to a dead display, and
            // an fd leaked once per retry.
            m_keepAlive.reset();
            m_xvfb.shutdown();
            m_reservation.release();
            m_display.clear();
        }
        return false;
    }

    bool spawnXvfb()
    {
        // -noreset is load-bearing, not decoration. Without it Xvfb resets the
        // server the moment the last client disconnects -- and waitForXServer()
        // deliberately opens and closes a probe connection, so the WM's own
        // connection would race that reset and intermittently fail to connect.
        // The legacy :99 fixture (CMakeLists.txt) already passes it for the same
        // reason. +render matches that fixture too, so Xft behaves identically
        // here; a later plan can still disable it by appending "-extension
        // RENDER" through xvfbArgs, which is parsed after these defaults.
        std::vector<std::string> argv{
            "Xvfb", m_display, "-screen", "0", "1024x768x24",
            "-ac", "+render", "-noreset", "-nolisten", "tcp"
        };
        for (const auto& a : m_options.xvfbArgs) argv.push_back(a);

        const std::string logPath = xvfbLogPath();

        pid_t pid = ::fork();
        if (pid < 0) return false;
        if (pid == 0) {
            redirectChildOutput(logPath);
            ::setsid();
            execArgv(argv, {});
            _exit(127);         // _exit: never run atexit handlers in a forked child
        }
        m_xvfb = ChildProcess(pid);
        return true;
    }

    bool waitForXServer()
    {
        const bool up = pollUntil([this] {
            if (m_xvfb.tryReap()) return false;              // server died
            // Retain the first successful connection as a keepalive for the
            // fixture's whole lifetime, so the server never observes a
            // zero-client moment even if -noreset were ever dropped again.
            m_keepAlive = x11::DisplayPtr(XOpenDisplay(m_display.c_str()));
            return m_keepAlive != nullptr;
        }, m_options.serverTimeoutMs);

        return up && !m_xvfb.reaped();
    }

    bool spawnWm()
    {
        m_asanLogPrefix = workDir() + "/asan" + sanitizedDisplay();
        m_stderrPath    = workDir() + "/wm" + sanitizedDisplay() + ".stderr";

        // Deferred item 14: clear reports left on THIS display prefix by an
        // earlier run, before the child that would legitimately write one is
        // launched. Without this a single mutation run makes every subsequent
        // run of the same group red. See removeReportsWithPrefix() above.
        removeReportsWithPrefix(m_asanLogPrefix);

        std::vector<std::string> argv{
            WM2_BINARY_PATH,
            // Never xterm: it may not be installed. xclock is preflight-checked.
            "--new-window-command=xclock",
        };
        for (const auto& a : m_options.wmArgs) argv.push_back(a);

        std::map<std::string, std::string> env = sanitizerEnv();
        env["DISPLAY"] = m_display;
        for (const auto& kv : m_options.childEnv) env[kv.first] = kv.second;

        pid_t pid = ::fork();
        if (pid < 0) return false;
        if (pid == 0) {
            redirectChildOutput(m_stderrPath);

            // Plan 08.5-09: the hang-time diagnostic bundle's backtrace channel.
            //
            // WHY IT EXISTS: this host runs Yama in restricted mode
            // (/proc/sys/kernel/yama/ptrace_scope reads 1), under which only a
            // process's own ancestors may attach a debugger to it. A debugger
            // the test process SPAWNS is a sibling of this child, not an
            // ancestor, so without the declaration below every attach is
            // refused by the kernel and the backtrace channel can only ever
            // record an absence. Checked empirically on this host: refused
            // without the declaration, admitted with it, and the declaration
            // survives execve().
            //
            // WHY IT DEFAULTS OFF: it is an opt-in field on WmFixtureOptions, so
            // no existing fixture's child changes at all. Only a fixture that
            // deliberately asks to be observed gets a permitted tracer.
            //
            // WHY THE NARROW FORM: the request below names ONE process by pid --
            // ::getppid(), the test process. Under Yama's restricted mode the
            // declared process and its descendants become the permitted tracers,
            // which admits a debugger this test spawned and nothing else on the
            // machine. The wildcard spelling of this request would admit ANY
            // process on the host, which is a strictly wider grant than this
            // needs and is forbidden by a paired negative gate.
            if (m_options.allowTestProcessPtrace) {
                ::prctl(PR_SET_PTRACER,
                        static_cast<unsigned long>(::getppid()), 0UL, 0UL, 0UL);
            }

            ::setsid();
            execArgv(argv, env);
            _exit(127);
        }
        m_wm = ChildProcess(pid);
        return true;
    }

    std::map<std::string, std::string> sanitizerEnv() const
    {
        std::map<std::string, std::string> env;

        // Pitfall 4: log_path lives under the CMake binary directory (T-8-TMP),
        // and exitcode makes a child sanitizer report observable via waitpid.
        env["ASAN_OPTIONS"] =
            "log_path=" + m_asanLogPrefix +
            ":exitcode=42:detect_leaks=1:abort_on_error=0:handle_segv=1";

        // fast_unwind_on_malloc=0 is required, or stripped system frames never
        // appear and the libX11/libXft/libfontconfig suppressions never match.
        env["LSAN_OPTIONS"] =
            std::string("suppressions=") + WM2_LSAN_SUPPRESSIONS +
            ":fast_unwind_on_malloc=0:print_suppressions=0";

        env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=0";
        return env;
    }

    // Poll root for _NET_SUPPORTING_WM_CHECK, published by
    // WindowManager::setupEwmhProperties() (src/Manager.cpp:452-460).
    //
    // Ordering hazard (RESEARCH Pattern 1): setupEwmhProperties() runs inside
    // initialiseScreen() at src/Manager.cpp:174, BEFORE scanInitialWindows() and
    // loop() at 183-184. The property therefore appears slightly before the
    // event loop is pumping, so this additionally round-trips a request to prove
    // the loop is live.
    bool waitForWmReady()
    {
        return waitForWmReadyStaged() == ReadyResult::Ready;
    }

    ReadyResult waitForWmReadyStaged()
    {
        // XID-reuse hazard -- this connection is deliberately RETAINED for the
        // fixture's whole lifetime rather than closed when readiness is proven.
        //
        // An X connection owns a resource-id range, and closing it returns that
        // whole range to the server, which hands it straight to the next client.
        // The readiness probe below creates and destroys a window on this
        // connection; if the connection then closed, the test's own connection
        // would be given the same base and its FIRST window would come back with
        // the probe's exact id. The WM is still unwinding the probe's Client at
        // that moment, and that teardown issues XReparentWindow(<that id>, root,
        // 0, 0) for a window it believes is already gone -- BadWindow is
        // suppressed for precisely that reason (src/Events.cpp:270). Against a
        // recycled id the request SUCCEEDS, silently moving the test's brand-new
        // window to the origin. It reads as "the WM placed my window at 0,0" and
        // quietly falsifies any geometry assertion, which is exactly how it was
        // found.
        //
        // Holding the connection open makes the collision impossible rather than
        // unlikely: the test's connection is guaranteed a different resource
        // base. No timing assumption, and nothing to observe about how fast the
        // WM processes a destroy.
        m_readyConn = openDisplay();
        Display* d = m_readyConn.get();
        if (!d) return ReadyResult::NoConnection;

        Atom check = XInternAtom(d, "_NET_SUPPORTING_WM_CHECK", False);
        Window root = DefaultRootWindow(d);

        const bool published = pollUntil([&] {
            if (m_wm.tryReap()) return false;             // WM died during startup

            Atom actualType = None;
            int actualFormat = 0;
            unsigned long nItems = 0, bytesAfter = 0;
            unsigned char* raw = nullptr;
            int status = XGetWindowProperty(d, root, check, 0, 1, False, XA_WINDOW,
                                            &actualType, &actualFormat,
                                            &nItems, &bytesAfter, &raw);
            bool ok = (status == Success && raw != nullptr && nItems == 1);
            if (raw) XFree(raw);
            return ok;
        }, m_options.readinessTimeoutMs);

        if (!published) return ReadyResult::NoWmCheckProperty;
        if (m_wm.reaped()) return ReadyResult::WmDied;

        // Prove the event loop is actually pumping: a mapped window must get
        // reparented away from root by the WM. The property alone is not enough
        // -- setupEwmhProperties() runs at src/Manager.cpp:174, before loop() at
        // 184, so the property appears while the loop is still not pumping.
        if (!proveEventLoopLive(d)) return ReadyResult::EventLoopNotPumping;
        return ReadyResult::Ready;
    }

    // RETRIED ON PURPOSE, with a FRESH window each attempt.
    //
    // The probe races WindowManager::scanInitialWindows() (src/Manager.cpp),
    // which runs after _NET_SUPPORTING_WM_CHECK is published and adopts every
    // non-override-redirect child of root that exists at that instant. Adoption
    // goes through windowToClient(w, true), which constructs a Client but does
    // NOT frame it -- framing happens on MapRequest, and a window that was
    // already mapped when it was adopted will never send one. So a probe
    // created inside that window is adopted, never reparented, and waiting
    // longer cannot help: the fixture would report "event loop not pumping"
    // against a WM whose loop is running perfectly.
    //
    // That is the confirmed mechanism behind the rare startup failure recorded
    // as deferred item 10 (measured at roughly 3-5% of fixture startups here,
    // and reproduced deterministically by delaying the scan). The scan happens
    // exactly once, so a second window created after it always gets a real
    // MapRequest -- hence retry with a new window rather than a longer wait.
    bool proveEventLoopLive(Display* d)
    {
        constexpr int kAttempts = 3;
        const int perAttemptMs =
            m_options.readinessTimeoutMs / kAttempts < 1500
                ? 1500 : m_options.readinessTimeoutMs / kAttempts;

        Window root = DefaultRootWindow(d);

        for (int attempt = 0; attempt < kAttempts; ++attempt) {
            Window probe = XCreateSimpleWindow(d, root, 0, 0, 60, 40, 0,
                                               BlackPixel(d, DefaultScreen(d)),
                                               WhitePixel(d, DefaultScreen(d)));
            XMapWindow(d, probe);
            XSync(d, False);

            const bool reparented = pollUntil([&] {
                if (m_wm.tryReap()) return false;
                Window parent = None, wroot = None, *children = nullptr;
                unsigned int nChildren = 0;
                if (!XQueryTree(d, probe, &wroot, &parent, &children, &nChildren)) return false;
                if (children) XFree(children);
                return parent != None && parent != root;
            }, perAttemptMs);

            XDestroyWindow(d, probe);
            XSync(d, False);

            if (reparented) return true;
            if (m_wm.reaped()) return false;   // no retry can revive a dead WM
        }
        return false;
    }

    // --- child-side helpers (post-fork, pre-exec) --------------------------

    // Redirect stdout+stderr to a FILE, not a pipe: an undrained pipe would fill
    // its buffer and deadlock the child. The file is drained by the parent after
    // the child is reaped.
    static void redirectChildOutput(const std::string& path)
    {
        int fd = ::open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
        if (fd >= 0) {
            ::dup2(fd, STDOUT_FILENO);
            ::dup2(fd, STDERR_FILENO);
            if (fd > STDERR_FILENO) ::close(fd);
        }
    }

    static void execArgv(const std::vector<std::string>& argv,
                         const std::map<std::string, std::string>& env)
    {
        for (const auto& kv : env) {
            ::setenv(kv.first.c_str(), kv.second.c_str(), 1);
        }
        std::vector<char*> cargv;
        cargv.reserve(argv.size() + 1);
        for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
        cargv.push_back(nullptr);

        // execvp for Xvfb (PATH lookup), execv semantics for the absolute
        // WM2_BINARY_PATH -- execvp handles both.
        ::execvp(cargv[0], cargv.data());
    }

    // --- parent-side helpers ----------------------------------------------

    // Everything a failed startup needs in order to be diagnosable without a
    // rerun: which display, whether each child is still alive and how it died,
    // plus both captured logs.
    std::string diagnostics()
    {
        // 08.5-09: refresh before reading. A no-op on the existing
        // startup-failure path, which already drains before calling this.
        collectStderr();

        std::string out;
        out += "--- fixture diagnostics ---\n";
        out += "display: " + m_display + "\n";

        out += "xvfb: pid=" + std::to_string(m_xvfb.pid());
        if (m_xvfb.reaped()) {
            out += " REAPED exit=" + std::to_string(m_xvfb.exitCode()) +
                   " signal=" + std::to_string(m_xvfb.termSignal());
        } else {
            out += " alive";
        }
        out += "\n";

        out += "wm:   pid=" + std::to_string(m_wm.pid());
        if (m_wm.reaped()) {
            out += " REAPED exit=" + std::to_string(m_wm.exitCode()) +
                   " signal=" + std::to_string(m_wm.termSignal());
        } else {
            out += " alive";
        }
        out += "\n";

        out += "--- wm stderr (" + m_stderrPath + ") ---\n" + m_stderr + "\n";
        out += "--- xvfb log ---\n" + readFile(xvfbLogPath()) + "\n";
        return out;
    }

    static std::string readFile(const std::string& path)
    {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return "(file not present: " + path + ")";
        std::string out;
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
        std::fclose(f);
        return out;
    }

public:
    // Public since 08.5-09 so a diagnostic case can copy the Xvfb log into a
    // capture bundle as its own channel. Signature and body unchanged.
    std::string xvfbLogPath() const
    {
        return workDir() + "/xvfb" + sanitizedDisplay() + ".log";
    }

private:
    void collectStderr()
    {
        if (m_stderrPath.empty()) return;
        FILE* f = std::fopen(m_stderrPath.c_str(), "rb");
        if (!f) return;
        std::string out;
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
        std::fclose(f);
        m_stderr = std::move(out);
    }

    // ASan writes to "<log_path>.<pid>". Glob the directory for that prefix.
    std::vector<std::string> globAsanReports() const
    {
        return globWithPrefix(m_asanLogPrefix);
    }

public:
    // Every path in workDir() whose basename starts with `prefix`'s basename.
    // Public and static only so removeReportsWithPrefix() above -- and the
    // harness test that proves it -- can name the same glob the attribution
    // uses; a cleanup that globbed differently from the detection would be a
    // cleanup that does not clean.
    static std::vector<std::string> globWithPrefix(const std::string& prefix)
    {
        std::vector<std::string> found;
        if (prefix.empty()) return found;

        const std::string dir = workDir();
        const size_t slash = prefix.find_last_of('/');
        const std::string base = (slash == std::string::npos)
            ? prefix : prefix.substr(slash + 1);

        DIR* dp = ::opendir(dir.c_str());
        if (!dp) return found;
        while (struct dirent* e = ::readdir(dp)) {
            const std::string name = e->d_name;
            if (name.size() > base.size() && name.compare(0, base.size(), base) == 0) {
                found.push_back(dir + "/" + name);
            }
        }
        ::closedir(dp);
        return found;
    }

    static std::string workDir() { return std::string(WM2_TEST_WORKDIR); }

private:

    std::string sanitizedDisplay() const
    {
        std::string s = m_display;
        for (char& ch : s) if (ch == ':') ch = '-';
        return s;
    }

    WmFixtureOptions m_options;
    DisplayReservation m_reservation;
    std::string m_display;
    ChildProcess m_xvfb;
    ChildProcess m_wm;
    std::string m_stderrPath;
    std::string m_stderr;
    std::string m_asanLogPrefix;
    std::vector<std::string> m_asanReports;

    // Declared LAST so they are destroyed FIRST. Members are destroyed in
    // reverse declaration order, and that path is what runs when start() throws
    // out of the constructor -- the explicit ordering in ~WmFixture() never gets
    // a chance. With these declared before m_xvfb, a failed startup killed Xvfb
    // while these connections were still open, Xlib's default IO-error handler
    // called exit(1), and the whole process died before Catch2 could print the
    // staged diagnostics that exist precisely to explain such a failure. The
    // symptom was a bare "X connection to :120 broken" and no test output at all.
    x11::DisplayPtr m_keepAlive;   // held open so Xvfb never sees zero clients
    x11::DisplayPtr m_readyConn;   // retained: see waitForWmReadyStaged()
};

// ---------------------------------------------------------------------------
// Property helpers shared by the process-level tests
//
// Same six-out-param XGetWindowProperty + REQUIRE(status == Success) assertion
// shape as tests/test_ewmh.cpp.
// ---------------------------------------------------------------------------

// Read a CARDINAL/32 array property. Returns false when absent or mistyped.
inline bool readCardinals(Display* d, Window w, Atom prop,
                          std::vector<unsigned long>& out, unsigned long maxItems = 64)
{
    out.clear();
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long nItems = 0, bytesAfter = 0;
    unsigned char* raw = nullptr;

    int status = XGetWindowProperty(d, w, prop, 0, static_cast<long>(maxItems), False,
                                    XA_CARDINAL, &actualType, &actualFormat,
                                    &nItems, &bytesAfter, &raw);
    if (status != Success) return false;
    if (!raw || actualType != XA_CARDINAL || actualFormat != 32 || nItems == 0) {
        if (raw) XFree(raw);
        return false;
    }

    unsigned long* vals = reinterpret_cast<unsigned long*>(raw);
    out.assign(vals, vals + nItems);
    XFree(raw);
    return true;
}

// Read a WINDOW/32 property (single value).
inline bool readWindowProp(Display* d, Window w, Atom prop, Window& out)
{
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long nItems = 0, bytesAfter = 0;
    unsigned char* raw = nullptr;

    int status = XGetWindowProperty(d, w, prop, 0, 1, False, XA_WINDOW,
                                    &actualType, &actualFormat,
                                    &nItems, &bytesAfter, &raw);
    if (status != Success) return false;
    if (!raw || nItems != 1) {
        if (raw) XFree(raw);
        return false;
    }
    out = *reinterpret_cast<Window*>(raw);
    XFree(raw);
    return true;
}

// ---------------------------------------------------------------------------
// Resident set size in kilobytes, from /proc/<pid>/statm field 2 (resident
// pages). Chosen over VmSize because virtual size says nothing about what is
// actually held -- ASan alone reserves an enormous virtual mapping that no VPS
// ever commits.
//
// It lived in tests/test_wm_resource.cpp until plan 09-09, which needed the
// same reader for the settings window's own process. Shared rather than
// copied: the 512 MB budget is one budget, and two readers that disagreed by a
// page size would make the window manager's figure and the GUI's figure
// incomparable -- which is the one thing the release notes put them side by
// side to do.
// ---------------------------------------------------------------------------
inline bool residentKb(pid_t pid, long& out)
{
    if (pid <= 0) return false;
    const std::string path = "/proc/" + std::to_string(pid) + "/statm";
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    long total = 0, resident = 0;
    const int n = std::fscanf(f, "%ld %ld", &total, &resident);
    std::fclose(f);
    if (n != 2) return false;
    // IN-04: sysconf() answering -1 makes `-1 / 1024` zero, and every
    // memory-budget assertion built on this reader then passes vacuously
    // against a live process. A silently-zero reading is worse than a false.
    const long page = ::sysconf(_SC_PAGESIZE);
    if (page <= 0) return false;
    out = resident * (page / 1024);
    return true;
}

} // namespace wm2test
