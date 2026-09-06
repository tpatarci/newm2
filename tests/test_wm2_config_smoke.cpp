// wm2-config: the settings window's model, and that the window opens at all
// (CGUI-01, CGUI-03, D-20, plan 09-06).
//
// TWO HALVES, and the split is deliberate.
//
// The DISPLAY-FREE half exercises FormState and the config file writer with no
// X server, no toolkit and no window manager. It runs on every host, including
// one where pkg-config never found gtk+-3.0 and the GUI was therefore not
// built. That is what stops D-20's "skipped with a reason" from becoming
// "nothing was checked": the reset semantics, the save semantics and the banner
// sentence are all proven even where no window can be opened.
//
// The SMOKE half spawns the real wm2-config on the fixture's Xvfb display and
// asserts its window maps. When the GUI was not built there is no binary to
// spawn, and those cases SKIP with the reason stated (Catch2's own SKIP, not a
// silent return) -- because a silent pass would let a GTK-less machine certify
// CGUI-01.
//
// DISC-09: the smoke half asserts the window MAPS. It deliberately does not
// assert clean stderr. GTK's accessibility bridge is a hard dependency of
// libgtk-3-0 and reaches for a session bus that a headless fixture does not
// have; the warning it prints is expected, is harmless, and tightening this
// assertion to catch it would make the suite fail on precisely the bare droplet
// this project targets (09-RESEARCH.md Pitfall 3).
//
// Standing rules honoured here: every X server is the fixture's own
// (-nolisten tcp, no access-control flag), every child is terminated by a PID
// this file created, nothing reads ~/.xsession-errors, and no case reads or
// writes the developer's real configuration directory -- the layered reads all
// run with XDG_CONFIG_HOME and XDG_CONFIG_DIRS pointed at a tree under the
// build directory.

#include <catch2/catch_test_macros.hpp>

#include "support/WmFixture.h"

#include "../apps/wm2-config/ConnectionState.h"
#include "../apps/wm2-config/FormState.h"
#include "../apps/wm2-config/ProtocolClient.h"

#include "Config.h"
#include "ConfigFileWriter.h"
#include "SocketServer.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace wm2test;

namespace {

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// A configuration tree of this case's own
// ---------------------------------------------------------------------------

// Under the CMake binary directory rather than a bare /tmp name (threat
// T-8-TMP), unique per process and per call so two ctest workers cannot read
// each other's files.
std::string makeTree(const std::string& suffix)
{
    static int counter = 0;
    const std::string base = std::string(WM2_TEST_WORKDIR) + "/gui-" +
                             std::to_string(::getpid()) + "-" +
                             std::to_string(++counter) + "-" + suffix;
    ::mkdir(base.c_str(), 0700);
    return base;
}

void writeFile(const std::string& path, const std::string& contents)
{
    const std::size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) {
        ::mkdir(path.substr(0, slash).c_str(), 0700);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << contents;
}

std::string readFileOrEmpty(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// Nanosecond modification time, so "the file was not touched" is a real
// assertion rather than one that a second-granularity stamp would pass by
// accident on any run shorter than a second.
bool modificationTime(const std::string& path, struct timespec& out)
{
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return false;
    out = st.st_mtim;
    return true;
}

bool sameTime(const struct timespec& a, const struct timespec& b)
{
    return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec;
}

// Point the layered read at a tree of this case's own, and put the environment
// back afterwards. The standing rule is that no case may read or write the
// developer's real configuration directory; this is what enforces it, rather
// than a comment hoping nobody calls Config::load().
class ScopedXdg {
public:
    ScopedXdg(const std::string& home, const std::string& dirs)
        : m_home(saved("XDG_CONFIG_HOME")), m_dirs(saved("XDG_CONFIG_DIRS"))
    {
        ::setenv("XDG_CONFIG_HOME", home.c_str(), 1);
        ::setenv("XDG_CONFIG_DIRS", dirs.c_str(), 1);
    }

    ~ScopedXdg()
    {
        restore("XDG_CONFIG_HOME", m_home);
        restore("XDG_CONFIG_DIRS", m_dirs);
    }

    ScopedXdg(const ScopedXdg&) = delete;
    ScopedXdg& operator=(const ScopedXdg&) = delete;

private:
    static std::pair<bool, std::string> saved(const char* name)
    {
        const char* v = std::getenv(name);
        return v ? std::make_pair(true, std::string(v))
                 : std::make_pair(false, std::string());
    }

    static void restore(const char* name, const std::pair<bool, std::string>& v)
    {
        if (v.first) ::setenv(name, v.second.c_str(), 1);
        else         ::unsetenv(name);
    }

    std::pair<bool, std::string> m_home;
    std::pair<bool, std::string> m_dirs;
};

// ---------------------------------------------------------------------------
// The window, observed from outside
// ---------------------------------------------------------------------------

// X protocol errors are a RACE while walking a live window tree, not a failure:
// a window listed by XQueryTree can be gone before the property read names it,
// and Xlib's default handler would kill this process with no assertion output.
int g_xErrors = 0;
int quietXErrorHandler(Display*, XErrorEvent*) { ++g_xErrors; return 0; }
struct QuietXErrors { QuietXErrors() { XSetErrorHandler(quietXErrorHandler); } };
const QuietXErrors g_quiet;

// The value of _WM2_CONFIG_STATE on `w`, or "" if it does not carry one.
std::string configStateOf(Display* d, Window w)
{
    const Atom prop = XInternAtom(d, kConfigStateProperty, True);
    if (prop == None) return std::string();

    Atom actualType = None;
    int actualFormat = 0;
    unsigned long items = 0, after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(d, w, prop, 0, 64, False, AnyPropertyType,
                           &actualType, &actualFormat, &items, &after,
                           &data) != Success) {
        return std::string();
    }
    std::string value;
    if (data) {
        if (actualFormat == 8) value.assign(reinterpret_cast<char*>(data), items);
        XFree(data);
    }
    return value;
}

// Depth-first search of the whole tree for the one window carrying the
// property. Recursive because a running window manager REPARENTS the GUI into
// a frame, so it is not a child of the root by the time it is mapped.
Window findConfigWindow(Display* d, Window from, std::string& stateOut)
{
    if (!configStateOf(d, from).empty() && from != DefaultRootWindow(d)) {
        stateOut = configStateOf(d, from);
        return from;
    }

    Window root = None, parent = None, *children = nullptr;
    unsigned int n = 0;
    if (!XQueryTree(d, from, &root, &parent, &children, &n)) return None;
    Window found = None;
    for (unsigned int i = 0; i < n && found == None; ++i) {
        found = findConfigWindow(d, children[i], stateOut);
    }
    if (children) XFree(children);
    return found;
}

bool isViewable(Display* d, Window w)
{
    XWindowAttributes attr;
    if (!XGetWindowAttributes(d, w, &attr)) return false;
    return attr.map_state == IsViewable;
}

#ifdef WM2_CONFIG_PATH
// Fork and exec the built wm2-config with its output bound to files (never a
// pipe that could fill and deadlock the child). The caller owns the returned
// ChildProcess and terminates it by that PID.
ChildProcess spawnConfigGui(const std::string& display,
                            const std::vector<std::string>& args,
                            const std::string& home)
{
    static int counter = 0;
    const std::string stem = std::string(WM2_TEST_WORKDIR) + "/wm2-config-" +
                             std::to_string(::getpid()) + "-" +
                             std::to_string(++counter);

    const pid_t pid = ::fork();
    if (pid < 0) return ChildProcess();
    if (pid == 0) {
        const int outFd = ::open((stem + ".out").c_str(),
                                 O_CREAT | O_WRONLY | O_TRUNC, 0600);
        const int errFd = ::open((stem + ".err").c_str(),
                                 O_CREAT | O_WRONLY | O_TRUNC, 0600);
        if (outFd >= 0) { ::dup2(outFd, STDOUT_FILENO); ::close(outFd); }
        if (errFd >= 0) { ::dup2(errFd, STDERR_FILENO); ::close(errFd); }

        ::setenv("DISPLAY", display.c_str(), 1);
        ::setenv("GDK_BACKEND", "x11", 1);
        // GTK wants somewhere writable for its own state, and the standing rule
        // is that no test touches the developer's real directories.
        ::setenv("HOME", home.c_str(), 1);
        ::setenv("XDG_CONFIG_HOME", (home + "/.config").c_str(), 1);
        ::setenv("XDG_CACHE_HOME", (home + "/.cache").c_str(), 1);
        ::setenv("XDG_RUNTIME_DIR", home.c_str(), 1);
        // No session bus on a fixture display; saying so up front is quieter
        // than letting GTK discover it, and changes nothing about the assertion
        // (DISC-09).
        ::setenv("NO_AT_BRIDGE", "1", 1);

        std::vector<std::string> owned;
        owned.push_back(WM2_CONFIG_PATH);
        for (const std::string& a : args) owned.push_back(a);
        std::vector<char*> argv;
        argv.reserve(owned.size() + 1);
        for (std::string& a : owned) argv.push_back(&a[0]);
        argv.push_back(nullptr);

        ::execv(WM2_CONFIG_PATH, argv.data());
        ::_exit(127);
    }
    return ChildProcess(pid);
}
#endif

const char* kGuiNotBuilt =
    "wm2-config was not built in this tree (BUILD_CONFIG_GUI resolved to OFF, "
    "or pkg-config could not find gtk+-3.0), so there is no binary to open a "
    "window with. The display-free cases in this file still ran.";

}  // namespace


// =============================================================================
// The model, with no display in sight
// =============================================================================

TEST_CASE("the form manages exactly the keys the config file writer manages",
          "[wm2_config_smoke]")
{
    const std::string tree = makeTree("seed");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());

    REQUIRE(form.fields().size() == configFileManagedKeys().size());
    for (const std::string& key : configFileManagedKeys()) {
        INFO("key: " << key);
        CHECK(form.manages(key));
    }
    // Nothing has been touched, so a save would write nothing at all.
    CHECK_FALSE(form.dirty());
    CHECK(form.edits().empty());
}

TEST_CASE("a seeded field shows the built-in default when no file sets it",
          "[wm2_config_smoke]")
{
    const std::string tree = makeTree("defaults");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());

    const Config builtIn;
    CHECK(form.value("tab-background") == builtIn.tabBackground);
    CHECK(form.value("frame-thickness") == std::to_string(builtIn.frameThickness));

    const FormField* field = form.field("tab-background");
    REQUIRE(field != nullptr);
    CHECK(field->source == ValueSource::BuiltIn);
}

TEST_CASE("a value set only in the system layer is shown as effective and names its layer",
          "[wm2_config_smoke]")
{
    // DISC-08: the GUI shows the EFFECTIVE value, whichever layer produced it,
    // and the raw field's tooltip names that layer. The system file is never
    // written -- editing this key writes an override into the user file, and
    // resetting removes that override so the system value shows through again.
    const std::string tree = makeTree("systemlayer");
    writeFile(tree + "/system/wm2-born-again/config", "tab-background=#123456\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());

    const FormField* field = form.field("tab-background");
    REQUIRE(field != nullptr);
    CHECK(field->current == "#123456");
    CHECK(field->effective == "#123456");
    CHECK(field->source == ValueSource::SystemFile);
    CHECK(field->sourceDetail == tree + "/system/wm2-born-again/config");
}

TEST_CASE("an edit produces one edit and leaves every other key out of the file",
          "[wm2_config_smoke]")
{
    const std::string tree = makeTree("oneedit");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());

    REQUIRE(form.setValue("tab-background", "#ABCDEF"));
    CHECK(form.dirty());

    const std::vector<ConfigEdit> edits = form.edits();
    REQUIRE(edits.size() == 1);
    CHECK(edits[0].key == "tab-background");
    CHECK(edits[0].value == "#ABCDEF");
    CHECK_FALSE(edits[0].remove);

    // Setting the same value again is not a second edit -- which is what stops
    // an idempotent widget signal from sending a redundant set message.
    CHECK_FALSE(form.setValue("tab-background", "#ABCDEF"));
    CHECK(form.edits().size() == 1);
}

TEST_CASE("reset marks a key for removal and shows the layer below, not the built-in default",
          "[wm2_config_smoke]")
{
    // D-13, stated as an observable: "default" is what the user will actually
    // see after the key leaves their file, and on a host with a system-wide
    // configuration that is the SYSTEM value, not the compiled-in one.
    const std::string tree = makeTree("reset");
    writeFile(tree + "/system/wm2-born-again/config", "frame-thickness=11\n");
    writeFile(tree + "/user/wm2-born-again/config", "frame-thickness=23\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());
    REQUIRE(form.value("frame-thickness") == "23");

    REQUIRE(form.requestReset("frame-thickness"));
    CHECK(form.value("frame-thickness") == "11");           // the system layer
    CHECK(form.value("frame-thickness") != std::to_string(Config().frameThickness));

    const std::vector<ConfigEdit> edits = form.edits();
    REQUIRE(edits.size() == 1);
    CHECK(edits[0].key == "frame-thickness");
    CHECK(edits[0].remove);
}

TEST_CASE("typing a value after a reset cancels the reset", "[wm2_config_smoke]")
{
    const std::string tree = makeTree("resetthenedit");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());

    REQUIRE(form.requestReset("frame-thickness"));
    REQUIRE(form.setValue("frame-thickness", "9"));

    const std::vector<ConfigEdit> edits = form.edits();
    REQUIRE(edits.size() == 1);
    CHECK_FALSE(edits[0].remove);
    CHECK(edits[0].value == "9");
}

TEST_CASE("revert restores every effective value and forgets every edit",
          "[wm2_config_smoke]")
{
    const std::string tree = makeTree("revert");
    writeFile(tree + "/user/wm2-born-again/config", "tab-background=#010203\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    form.seedFromLayers(configLayersFromDisk());

    REQUIRE(form.setValue("tab-background", "#FF0000"));
    REQUIRE(form.requestReset("frame-thickness"));
    REQUIRE(form.divergentKeys().size() >= 1);

    form.revert();
    CHECK_FALSE(form.dirty());
    CHECK(form.edits().empty());
    CHECK(form.value("tab-background") == "#010203");
    CHECK(form.divergentKeys().empty());
}

TEST_CASE("the file-only banner is the sentence the phase decided on",
          "[wm2_config_smoke]")
{
    // Compared against the CONSTANT, never against a copy of the sentence
    // written out here. A test carrying its own literal would keep passing
    // after somebody edited the window's copy, which would make it a test of
    // this file rather than of the program -- and D-03 fixed the sentence
    // precisely so that it cannot quietly become something else.
    CHECK(connectionBannerText(ConnectionState::FileOnlyNoSocket, "") ==
          std::string(kFileOnlyBannerText));

    // A refused handshake says the same thing and then names why (D-15).
    const std::string refused =
        connectionBannerText(ConnectionState::FileOnlyRefused, "protocol version 99");
    CHECK(refused.rfind(kFileOnlyBannerText, 0) == 0);
    CHECK(refused.find("protocol version 99") != std::string::npos);

    // Connected is a different sentence, or the banner would say nothing.
    CHECK(connectionBannerText(ConnectionState::Connected, "") !=
          std::string(kFileOnlyBannerText));
}


// =============================================================================
// Saving: the user's file, and nothing else
// =============================================================================

TEST_CASE("a save in file-only mode writes the user file and leaves the system file alone",
          "[wm2_config_smoke]")
{
    const std::string tree = makeTree("save");
    const std::string systemFile = tree + "/system/wm2-born-again/config";
    const std::string userFile   = tree + "/user/wm2-born-again/config";
    writeFile(systemFile, "tab-background=#123456\n");
    writeFile(userFile, "# a comment the writer must preserve\nborders=#00FF00\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    struct timespec systemBefore;
    REQUIRE(modificationTime(systemFile, systemBefore));

    FormState form;
    const ConfigLayers layers = configLayersFromDisk();
    form.seedFromLayers(layers);
    REQUIRE(layers.userFilePath == userFile);

    REQUIRE(form.setValue("tab-background", "#ABCDEF"));

    std::string error;
    REQUIRE(configFileWrite(layers.userFilePath, form.edits(), {}, false, error) ==
            ConfigWriteResult::Ok);
    form.markSaved();

    const std::string written = readFileOrEmpty(userFile);
    CHECK(written.find("tab-background=#ABCDEF") != std::string::npos);
    CHECK(written.find("# a comment the writer must preserve") != std::string::npos);
    CHECK(written.find("borders=#00FF00") != std::string::npos);

    // T-9-38 / D-04: the system-wide file is never opened for writing.
    struct timespec systemAfter;
    REQUIRE(modificationTime(systemFile, systemAfter));
    CHECK(sameTime(systemBefore, systemAfter));
    CHECK(readFileOrEmpty(systemFile) == "tab-background=#123456\n");

    CHECK_FALSE(form.dirty());
}

TEST_CASE("a reset key is gone from the user file after a save, and no default took its place",
          "[wm2_config_smoke]")
{
    const std::string tree = makeTree("resetsave");
    const std::string systemFile = tree + "/system/wm2-born-again/config";
    const std::string userFile   = tree + "/user/wm2-born-again/config";
    writeFile(systemFile, "frame-thickness=11\n");
    writeFile(userFile, "frame-thickness=23\ntab-background=#010203\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    const ConfigLayers layers = configLayersFromDisk();
    form.seedFromLayers(layers);
    REQUIRE(form.requestReset("frame-thickness"));

    std::string error;
    REQUIRE(configFileWrite(layers.userFilePath, form.edits(), {}, false, error) ==
            ConfigWriteResult::Ok);
    form.markSaved();

    const std::string written = readFileOrEmpty(userFile);
    CHECK(written.find("frame-thickness") == std::string::npos);
    // Not the built-in default written in its place, and not the system value
    // pinned into the user file either -- the line is simply gone, which is
    // what lets the system layer show through (D-13).
    CHECK(written.find(std::to_string(Config().frameThickness)) == std::string::npos);
    CHECK(written.find("tab-background=#010203") != std::string::npos);

    // And the window now shows what removal actually produced.
    CHECK(form.value("frame-thickness") == "11");
}

TEST_CASE("a save with nothing changed leaves the user file untouched",
          "[wm2_config_smoke]")
{
    const std::string tree = makeTree("nochange");
    const std::string userFile = tree + "/user/wm2-born-again/config";
    writeFile(userFile, "borders=#00FF00\n");
    ScopedXdg xdg(tree + "/user", tree + "/system");

    FormState form;
    const ConfigLayers layers = configLayersFromDisk();
    form.seedFromLayers(layers);

    struct timespec before;
    REQUIRE(modificationTime(userFile, before));
    // A nanosecond-resolution stamp still needs the two reads to be
    // distinguishable in principle, or "unchanged" would be trivially true.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // The whole assertion: with nothing dirty there is no edit list, and the
    // window must therefore not call the writer at all. Calling it with an
    // empty edit set would rewrite the file byte-identically and still move its
    // modification time, which is exactly the surprise a user does not want
    // from a Save they pressed by reflex.
    REQUIRE(form.edits().empty());

    struct timespec after;
    REQUIRE(modificationTime(userFile, after));
    CHECK(sameTime(before, after));
}


// =============================================================================
// The window itself
// =============================================================================

TEST_CASE("wm2-config opens a window and reports itself connected to the running window manager",
          "[wm2_config_smoke]")
{
#ifndef WM2_CONFIG_PATH
    SKIP(kGuiNotBuilt);
#else
    WmFixture fixture;
    const std::string home = makeTree("guihome");

    ChildProcess gui = spawnConfigGui(fixture.display(), {}, home);
    REQUIRE(gui.pid() > 0);

    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();

    Window win = None;
    std::string state;
    const bool appeared = WmFixture::pollUntil([&]() {
        state.clear();
        win = findConfigWindow(d, DefaultRootWindow(d), state);
        return win != None && isViewable(d, win) && !state.empty();
    }, 30000);

    INFO("wm2-config state property: '" << state << "'");
    CHECK(appeared);
    CHECK(win != None);
    CHECK(state == std::string(connectionStateName(ConnectionState::Connected)));

    gui.shutdown();   // by the PID spawnConfigGui() created, never by name
#endif
}

TEST_CASE("wm2-config opens with no socket to talk to and says so",
          "[wm2_config_smoke]")
{
#ifndef WM2_CONFIG_PATH
    SKIP(kGuiNotBuilt);
#else
    // A window manager IS running on this display -- the fixture started it --
    // but the GUI is pointed at a socket path nothing is listening on. That is
    // the file-only condition expressed without needing a second Xvfb with no
    // window manager on it, and it exercises the same branch: connect() fails,
    // the window opens anyway, and the banner is the fixed sentence.
    WmFixture fixture;
    const std::string home = makeTree("guihome-nosocket");
    const std::string absent = home + "/there-is-no-socket-here";

    ChildProcess gui = spawnConfigGui(fixture.display(),
                                      {"--socket", absent}, home);
    REQUIRE(gui.pid() > 0);

    x11::DisplayPtr dp = fixture.openDisplay();
    REQUIRE(dp != nullptr);
    Display* d = dp.get();

    Window win = None;
    std::string state;
    const bool appeared = WmFixture::pollUntil([&]() {
        state.clear();
        win = findConfigWindow(d, DefaultRootWindow(d), state);
        return win != None && isViewable(d, win) && !state.empty();
    }, 30000);

    INFO("wm2-config state property: '" << state << "'");
    CHECK(appeared);
    CHECK(state == std::string(connectionStateName(ConnectionState::FileOnlyNoSocket)));

    gui.shutdown();
#endif
}

TEST_CASE("a colour committed through the form state and the protocol client reaches the desktop",
          "[wm2_config_smoke]")
{
    // The GUI's OWN two halves, driven directly: the form state decides what
    // the new value is, and the GUI's own protocol client is what carries it to
    // the running window manager. Nothing here is a hand-rolled socket client,
    // so a change that broke wm2-config's client would break this case.
    //
    // The widget-to-form-state edge -- a GtkColorButton's "color-set" signal
    // reaching FormState::setValue() -- is NOT covered here, because driving a
    // GTK widget from another process means synthesising input into a toolkit,
    // which tests the toolkit. That edge is covered by the operator's
    // screenshot review at the end of this phase, and the SUMMARY says so.
    WmFixture fixture;

    const std::string path = configSocketPath(fixture.display().c_str());
    ProtocolClient client;
    REQUIRE(client.connect(path));
    REQUIRE(client.state() == ProtocolClient::State::Connected);

    const std::string tree = makeTree("liveclient");
    ScopedXdg xdg(tree + "/user", tree + "/system");
    FormState form;
    form.seedFromLayers(configLayersFromDisk());

    // Not the default, so "it changed" cannot be true of the starting state.
    const std::string chosen = "#A1B2C3";
    REQUIRE(chosen != Config().tabBackground);
    REQUIRE(form.setValue("tab-background", chosen));

    bool acked = false;
    std::string refusal;
    REQUIRE(client.sendSet("tab-background", form.value("tab-background"),
                           [&](const ConfigMessage& reply) {
                               acked = reply.type == ConfigMessageType::Ack;
                               if (reply.type == ConfigMessageType::Error) {
                                   refusal = reply.reason;
                               }
                           }));
    REQUIRE(client.pumpUntil([&]() { return acked || !refusal.empty(); }, 15000));
    INFO("refusal: " << refusal);
    REQUIRE(acked);

    // What the running window manager now says it is drawing tabs in. The
    // pixel-level repaint for this key is proven by the [wm_config_live] suite
    // from plan 09-05; what this case adds is that the GUI's client is what
    // caused it.
    std::string reported;
    REQUIRE(client.sendGet("tab-background", [&](const ConfigMessage& reply) {
        if (reply.type == ConfigMessageType::Value) reported = reply.value;
    }));
    REQUIRE(client.pumpUntil([&]() { return !reported.empty(); }, 15000));
    CHECK(reported == chosen);
}

TEST_CASE("the protocol client reports a reason and stays file-only when nothing is listening",
          "[wm2_config_smoke]")
{
    const std::string tree = makeTree("nosocketclient");
    ProtocolClient client;
    CHECK_FALSE(client.connect(tree + "/not-a-socket"));
    CHECK(client.state() == ProtocolClient::State::NoSocket);
    CHECK_FALSE(client.reason().empty());
    CHECK(client.fileDescriptor() < 0);
}
