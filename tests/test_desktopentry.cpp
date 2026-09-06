#include <catch2/catch_test_macros.hpp>

#include "AppEntry.h"
#include "DesktopEntry.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

// Helper: write a temp .desktop file and return its path (mirrors
// tests/test_config.cpp's writeTempConfig)
static std::string writeTempDesktopFile(const std::string& content) {
    static int counter = 0;
    std::string path = "/tmp/wm2-test-desktopentry-" + std::to_string(++counter) + ".desktop";
    std::ofstream out(path);
    out << content;
    out.close();
    return path;
}

// Helper: clean up temp file (mirrors tests/test_config.cpp's removeTempFile)
static void removeTempFile(const std::string& path) {
    std::remove(path.c_str());
}

// =============================================================================
// Test (a): a well-formed entry parses with correct execArgv and category
// =============================================================================
TEST_CASE("parseFile parses a well-formed entry", "[desktopentry]") {
    std::string path = writeTempDesktopFile(
        "[Desktop Entry]\n"
        "Name=Firefox\n"
        "Exec=firefox %u\n"
        "Icon=firefox\n"
        "Categories=Network;WebBrowser;\n"
    );

    auto result = DesktopEntry::parseFile(path);
    REQUIRE(result.has_value());
    REQUIRE(result->name == "Firefox");
    REQUIRE(result->execArgv.size() == 1);
    REQUIRE(result->execArgv[0] == "firefox");
    REQUIRE(result->icon == "firefox");
    REQUIRE(result->category == "Network");
    REQUIRE(result->source == AppEntry::Source::Desktop);

    removeTempFile(path);
}

// =============================================================================
// Test (b): NoDisplay=true excludes the entry (D-05)
// =============================================================================
TEST_CASE("parseFile excludes NoDisplay=true entries", "[desktopentry]") {
    std::string path = writeTempDesktopFile(
        "[Desktop Entry]\n"
        "Name=Hidden App\n"
        "Exec=hiddenapp\n"
        "NoDisplay=true\n"
    );

    auto result = DesktopEntry::parseFile(path);
    REQUIRE_FALSE(result.has_value());

    removeTempFile(path);
}

// =============================================================================
// Test (c): Hidden=true excludes the entry (D-05)
// =============================================================================
TEST_CASE("parseFile excludes Hidden=true entries", "[desktopentry]") {
    std::string path = writeTempDesktopFile(
        "[Desktop Entry]\n"
        "Name=Hidden App\n"
        "Exec=hiddenapp\n"
        "Hidden=true\n"
    );

    auto result = DesktopEntry::parseFile(path);
    REQUIRE_FALSE(result.has_value());

    removeTempFile(path);
}

// =============================================================================
// Test (d): NotShowIn=wm2-born-again excludes the entry (D-05)
// =============================================================================
TEST_CASE("parseFile excludes NotShowIn=wm2-born-again entries", "[desktopentry]") {
    std::string path = writeTempDesktopFile(
        "[Desktop Entry]\n"
        "Name=Excluded App\n"
        "Exec=excludedapp\n"
        "NotShowIn=wm2-born-again;\n"
    );

    auto result = DesktopEntry::parseFile(path);
    REQUIRE_FALSE(result.has_value());

    removeTempFile(path);
}

// =============================================================================
// Test (e): unresolvable TryExec excludes the entry (D-05)
// =============================================================================
TEST_CASE("parseFile excludes entries with unresolvable TryExec", "[desktopentry]") {
    std::string path = writeTempDesktopFile(
        "[Desktop Entry]\n"
        "Name=Missing Binary App\n"
        "Exec=missingbinaryapp\n"
        "TryExec=/nonexistent/binary/xyz123\n"
    );

    auto result = DesktopEntry::parseFile(path);
    REQUIRE_FALSE(result.has_value());

    removeTempFile(path);
}

// =============================================================================
// Test (f): unrecognized field code rejects the whole entry (T-7-01)
// =============================================================================
TEST_CASE("parseFile rejects unrecognized Exec field codes", "[desktopentry]") {
    std::string path = writeTempDesktopFile(
        "[Desktop Entry]\n"
        "Name=Bad Field Code App\n"
        "Exec=app %z\n"
    );

    auto result = DesktopEntry::parseFile(path);
    REQUIRE_FALSE(result.has_value());

    // Also verify directly at the parseExec level.
    auto execResult = DesktopEntry::parseExec("app %z");
    REQUIRE_FALSE(execResult.has_value());

    removeTempFile(path);
}

// =============================================================================
// Test (g): quoted Exec argument is never split/interpreted by a shell
// =============================================================================
TEST_CASE("parseExec treats a quoted argument as one literal token", "[desktopentry]") {
    auto result = DesktopEntry::parseExec(
        "app \"arg with a space and a semicolon; rm -rf ~\"");

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);
    REQUIRE((*result)[0] == "app");
    REQUIRE((*result)[1] == "arg with a space and a semicolon; rm -rf ~");
}

// =============================================================================
// Test (h1): xdgDataDirs() honors $XDG_DATA_DIRS when set
// =============================================================================
TEST_CASE("xdgDataDirs parses colon-separated XDG_DATA_DIRS", "[desktopentry][xdg]") {
    char* orig = std::getenv("XDG_DATA_DIRS");
    std::string origStr;
    if (orig) origStr = orig;

    setenv("XDG_DATA_DIRS", "/usr/local/share/custom:/usr/share/custom", 1);

    auto dirs = DesktopEntry::xdgDataDirs();
    REQUIRE(dirs.size() == 2);
    REQUIRE(dirs[0] == "/usr/local/share/custom");
    REQUIRE(dirs[1] == "/usr/share/custom");

    // Restore
    if (origStr.empty()) {
        unsetenv("XDG_DATA_DIRS");
    } else {
        setenv("XDG_DATA_DIRS", origStr.c_str(), 1);
    }
}

// =============================================================================
// Test (h2): xdgDataDirs() falls back to documented defaults when unset
// =============================================================================
TEST_CASE("xdgDataDirs defaults to /usr/local/share and /usr/share", "[desktopentry][xdg]") {
    char* orig = std::getenv("XDG_DATA_DIRS");
    std::string origStr;
    if (orig) origStr = orig;

    unsetenv("XDG_DATA_DIRS");

    auto dirs = DesktopEntry::xdgDataDirs();
    REQUIRE(dirs.size() == 2);
    REQUIRE(dirs[0] == "/usr/local/share");
    REQUIRE(dirs[1] == "/usr/share");

    // Restore
    if (origStr.empty()) {
        unsetenv("XDG_DATA_DIRS");
    } else {
        setenv("XDG_DATA_DIRS", origStr.c_str(), 1);
    }
}

// =============================================================================
// Test (h3): xdgApplicationsDirs() appends "/applications" and includes the
// XDG_DATA_HOME-derived user directory first
// =============================================================================
TEST_CASE("xdgApplicationsDirs appends applications to each data dir and prepends user dir", "[desktopentry][xdg]") {
    char* origDirs = std::getenv("XDG_DATA_DIRS");
    char* origHome = std::getenv("XDG_DATA_HOME");

    std::string origDirsStr, origHomeStr;
    if (origDirs) origDirsStr = origDirs;
    if (origHome) origHomeStr = origHome;

    setenv("XDG_DATA_DIRS", "/usr/local/share/custom", 1);
    setenv("XDG_DATA_HOME", "/custom/data/home", 1);

    auto dirs = DesktopEntry::xdgApplicationsDirs();
    REQUIRE(dirs.size() == 2);
    REQUIRE(dirs[0] == "/custom/data/home/applications");
    REQUIRE(dirs[1] == "/usr/local/share/custom/applications");

    // Restore
    if (origDirsStr.empty()) unsetenv("XDG_DATA_DIRS");
    else setenv("XDG_DATA_DIRS", origDirsStr.c_str(), 1);
    if (origHomeStr.empty()) unsetenv("XDG_DATA_HOME");
    else setenv("XDG_DATA_HOME", origHomeStr.c_str(), 1);
}

// =============================================================================
// Test: scanAll() skips missing directories without error (not asserting
// specific content since it depends on the host's real XDG data, only that
// it doesn't throw/crash)
// =============================================================================
TEST_CASE("scanAll does not throw when XDG data dirs are empty/missing", "[desktopentry]") {
    char* origDirs = std::getenv("XDG_DATA_DIRS");
    char* origHome = std::getenv("XDG_DATA_HOME");

    std::string origDirsStr, origHomeStr;
    if (origDirs) origDirsStr = origDirs;
    if (origHome) origHomeStr = origHome;

    setenv("XDG_DATA_DIRS", "/tmp/wm2-noexist-data-xyz", 1);
    setenv("XDG_DATA_HOME", "/tmp/wm2-noexist-data-home-xyz", 1);

    auto apps = DesktopEntry::scanAll();
    REQUIRE(apps.empty());

    // Restore
    if (origDirsStr.empty()) unsetenv("XDG_DATA_DIRS");
    else setenv("XDG_DATA_DIRS", origDirsStr.c_str(), 1);
    if (origHomeStr.empty()) unsetenv("XDG_DATA_HOME");
    else setenv("XDG_DATA_HOME", origHomeStr.c_str(), 1);
}

// =============================================================================
// Test: a CRLF-only blank line (bare '\r' left by std::getline splitting on
// '\n') does not crash the parser (CR-02 regression -- was UB via front() on
// an empty string, which SIGABRTs under -D_GLIBCXX_ASSERTIONS hardening)
// =============================================================================
TEST_CASE("parseFile handles a CRLF-terminated blank line without crashing", "[desktopentry]") {
    std::string path = writeTempDesktopFile(
        "[Desktop Entry]\n"
        "Name=Test\n"
        "Exec=/bin/true\n"
        "\r\n"
        "[Extra Group]\r\n"
        "Foo=bar\r\n"
    );

    auto result = DesktopEntry::parseFile(path);
    REQUIRE(result.has_value());
    REQUIRE(result->name == "Test");

    removeTempFile(path);
}

// =============================================================================
// The two .desktop files this repository ships (plan 09-08, D-11 and D-19)
//
// These cases parse the REAL files out of the source tree rather than a copy
// written here. The claim being checked is about what gets INSTALLED: an entry
// that this project's own scanner rejects would never reach the root menu, no
// matter how well-formed it looked to a human reader, and "it will show up
// under Settings" is otherwise a promise nobody can test until after somebody
// has installed the package on their machine.
//
// The stub-binary dance below is not scaffolding around the assertion -- it IS
// part of it. Both entries carry a `TryExec`, so what the parser returns
// depends on whether the named binary is installed, which is exactly the
// behaviour D-11 wants (no entry rather than a menu item that fails to launch)
// and exactly what T-9-47 relies on.
// =============================================================================

#ifndef WM2_SOURCE_DIR
#error "WM2_SOURCE_DIR must be defined for the packaging-entry cases"
#endif

namespace {

// A scratch directory under the CURRENT working directory -- which ctest sets
// to the build tree -- returned as an ABSOLUTE path.
//
// Absolute matters, and finding out why cost a red test: DesktopEntry's XDG
// helpers discard any data directory that is not absolute, so a relative
// XDG_DATA_HOME silently falls back to $HOME/.local/share and the scan reads
// the DEVELOPER'S OWN desktop files. A test that does that is both wrong and a
// standing-rule violation.
std::string makeScratchDir(const char* templateName) {
    std::string tmpl = templateName;
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    const char* made = mkdtemp(buf.data());
    REQUIRE(made != nullptr);

    char cwd[4096];
    REQUIRE(getcwd(cwd, sizeof(cwd)) != nullptr);
    return std::string(cwd) + "/" + made;
}

// A directory of stub executables, created under the CURRENT working directory
// -- which ctest sets to the build tree -- rather than at a fixed path under
// the system temporary directory, for the same reason the gate scripts stage
// there (T-9-48). Removed by the destructor.
class StubBinDir {
public:
    explicit StubBinDir(const std::vector<std::string>& names) {
        m_dir = makeScratchDir("wm2-desktopentry-stubbin-XXXXXX");
        for (const auto& n : names) {
            const std::string p = m_dir + "/" + n;
            {
                std::ofstream out(p);
                out << "#!/bin/sh\nexit 0\n";
            }
            REQUIRE(chmod(p.c_str(), 0755) == 0);
            m_files.push_back(p);
        }
    }
    ~StubBinDir() {
        for (const auto& f : m_files) std::remove(f.c_str());
        rmdir(m_dir.c_str());
    }
    StubBinDir(const StubBinDir&) = delete;
    StubBinDir& operator=(const StubBinDir&) = delete;

    const std::string& path() const { return m_dir; }

private:
    std::string              m_dir;
    std::vector<std::string> m_files;
};

// $PATH restricted to exactly one directory for the duration, restored after.
class ScopedPath {
public:
    explicit ScopedPath(const std::string& only) {
        const char* orig = std::getenv("PATH");
        m_had = (orig != nullptr);
        if (m_had) m_orig = orig;
        setenv("PATH", only.c_str(), 1);
    }
    ~ScopedPath() {
        if (m_had) {
            setenv("PATH", m_orig.c_str(), 1);
        } else {
            unsetenv("PATH");
        }
    }
    ScopedPath(const ScopedPath&) = delete;
    ScopedPath& operator=(const ScopedPath&) = delete;

private:
    std::string m_orig;
    bool        m_had = false;
};

std::string packagingFile(const char* name) {
    return std::string(WM2_SOURCE_DIR) + "/packaging/" + name;
}

// The file must be READABLE before anything is asserted about the parse. A
// packaging file that is missing and a packaging file the scanner rejects both
// come back as std::nullopt, and those are very different failures to be told
// about.
void requireReadable(const std::string& path) {
    INFO("packaging file: " << path);
    std::ifstream in(path);
    REQUIRE(in.good());
}

} // namespace

TEST_CASE("the shipped wm2-config entry parses and lands under Settings",
          "[desktopentry][packaging]") {
    const std::string path = packagingFile("wm2-config.desktop");
    requireReadable(path);

    StubBinDir bins({"wm2-config"});
    ScopedPath onlyStubs(bins.path());

    auto result = DesktopEntry::parseFile(path);
    REQUIRE(result.has_value());
    REQUIRE(result->category == "Settings");
    REQUIRE(result->execArgv.size() == 1);
    REQUIRE(result->execArgv[0] == "wm2-config");
    REQUIRE_FALSE(result->name.empty());
    REQUIRE(result->source == AppEntry::Source::Desktop);
}

TEST_CASE("the shipped wm2-config entry yields no menu entry when the binary is not installed",
          "[desktopentry][packaging]") {
    const std::string path = packagingFile("wm2-config.desktop");
    requireReadable(path);

    // A PATH containing nothing at all: the config-gui component is not
    // installed, and the entry must therefore produce no menu item rather than
    // one that fails when clicked.
    StubBinDir empty({});
    ScopedPath onlyStubs(empty.path());

    REQUIRE_FALSE(DesktopEntry::parseFile(path).has_value());
}

TEST_CASE("the shipped session entry names the window manager and survives D-05 filtering",
          "[desktopentry][packaging]") {
    const std::string path = packagingFile("wm2-born-again.desktop");
    requireReadable(path);

    StubBinDir bins({"wm2-born-again"});
    ScopedPath onlyStubs(bins.path());

    auto result = DesktopEntry::parseFile(path);
    REQUIRE(result.has_value());
    REQUIRE(result->name == "wm2-born-again");
    REQUIRE(result->execArgv.size() == 1);
    REQUIRE(result->execArgv[0] == "wm2-born-again");
}

TEST_CASE("the shipped wm2-config entry reaches the discovered-application list through the ordinary scanner",
          "[desktopentry][packaging]") {
    // The parse cases above prove the FILE is well-formed. This one proves the
    // composition D-11's second half actually depends on: the entry, sitting
    // where the config-gui component installs it (an `applications` directory
    // under an XDG data root), is picked up by the SAME scanAll() the root menu
    // calls -- no special case, no separate code path for this project's own
    // settings window.
    const std::string source = packagingFile("wm2-config.desktop");
    requireReadable(source);

    StubBinDir bins({"wm2-config"});
    ScopedPath onlyStubs(bins.path());

    // An XDG data root under the build tree, laid out the way the install rule
    // lays out a real prefix: <root>/applications/wm2-config.desktop.
    const std::string dataHome = makeScratchDir("wm2-desktopentry-xdghome-XXXXXX");
    const std::string appsDir = dataHome + "/applications";
    REQUIRE(mkdir(appsDir.c_str(), 0755) == 0);
    const std::string installed = appsDir + "/wm2-config.desktop";
    {
        std::ifstream in(source, std::ios::binary);
        std::ofstream out(installed, std::ios::binary);
        out << in.rdbuf();
    }

    char* origDirs = std::getenv("XDG_DATA_DIRS");
    char* origHome = std::getenv("XDG_DATA_HOME");
    std::string origDirsStr, origHomeStr;
    if (origDirs) origDirsStr = origDirs;
    if (origHome) origHomeStr = origHome;

    // A data-dirs list of one nonexistent directory, so the ONLY entry the scan
    // can find is the one just installed -- the assertion below is then about
    // this file and not about whatever the developer's machine happens to have.
    setenv("XDG_DATA_DIRS", "/nonexistent/wm2-packaging-test", 1);
    setenv("XDG_DATA_HOME", dataHome.c_str(), 1);

    auto apps = DesktopEntry::scanAll();

    if (origDirsStr.empty()) unsetenv("XDG_DATA_DIRS");
    else setenv("XDG_DATA_DIRS", origDirsStr.c_str(), 1);
    if (origHomeStr.empty()) unsetenv("XDG_DATA_HOME");
    else setenv("XDG_DATA_HOME", origHomeStr.c_str(), 1);

    std::remove(installed.c_str());
    rmdir(appsDir.c_str());
    rmdir(dataHome.c_str());

    REQUIRE(apps.size() == 1);
    REQUIRE(apps[0].category == "Settings");
    REQUIRE(apps[0].execArgv.size() == 1);
    REQUIRE(apps[0].execArgv[0] == "wm2-config");
    REQUIRE(apps[0].source == AppEntry::Source::Desktop);
}
