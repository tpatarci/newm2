// The surgical config-file writer (D-02, D-13), exercised with no display, no
// X server, no GTK and no window manager running -- D-20's file-editor half.
//
// Every preservation case here asserts on the EXACT BYTES of the resulting
// file, never on a re-parse. A re-parse would pass just as happily against a
// writer that dropped every comment and blank line in the file, which is the
// precise failure D-02 forbids. Byte equality is the only assertion that can
// tell the difference.

#include <catch2/catch_test_macros.hpp>

#include "AppEntry.h"
#include "Config.h"
#include "ConfigFileWriter.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

namespace {

// A private directory per case, under the build tree's own temp area rather
// than a bare fixed /tmp name (threat T-8-TMP, established in 08-01).
class TempDir {
public:
    TempDir() {
        static int counter = 0;
        std::error_code ec;
        m_path = std::filesystem::temp_directory_path(ec) /
                 ("wm2-config-writer-" + std::to_string(::getpid()) + "-" +
                  std::to_string(++counter));
        std::filesystem::remove_all(m_path, ec);
        std::filesystem::create_directories(m_path, ec);
    }

    ~TempDir() {
        std::error_code ec;
        // Restore write permission first: one case deliberately drops it.
        std::filesystem::permissions(m_path, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::add, ec);
        std::filesystem::remove_all(m_path, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return m_path; }
    std::string file(const std::string& name) const { return (m_path / name).string(); }

    // Every entry directly inside the directory, sorted -- so a case can assert
    // that a save created nothing beyond the file it was handed.
    std::vector<std::string> entries() const {
        std::vector<std::string> names;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(m_path, ec)) {
            names.push_back(e.path().filename().string());
        }
        std::sort(names.begin(), names.end());
        return names;
    }

private:
    std::filesystem::path m_path;
};

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

ConfigEdit set(const std::string& key, const std::string& value) {
    ConfigEdit e;
    e.key = key;
    e.value = value;
    return e;
}

ConfigEdit removeKey(const std::string& key) {
    ConfigEdit e;
    e.key = key;
    e.remove = true;
    return e;
}

// configFileWrite with the menu-entry half switched off, which is what every
// case that is not about menu entries wants.
ConfigWriteResult save(const std::string& path, const std::vector<ConfigEdit>& edits,
                       std::string& errorOut) {
    return configFileWrite(path, edits, {}, false, errorOut);
}

AppEntry entry(const std::string& name, const std::vector<std::string>& argv,
               const std::string& category) {
    AppEntry e;
    e.name = name;
    e.execArgv = argv;
    e.category = category;
    e.source = AppEntry::Source::Manual;
    return e;
}

// The section comment the writer generates when it has to append. Asserted as
// a LITERAL rather than against a constant the implementation exports: this
// string ends up in the user's own file, so a silent change to it should fail
// a test rather than quietly appear in everyone's config.
const char* const kSectionComment = "# Added by wm2-config\n";

}  // namespace

// =============================================================================
// The boundary of what the writer owns
// =============================================================================

TEST_CASE("The managed key list is exactly the twenty-one keys the GUI owns",
          "[config_writer]") {
    const std::vector<std::string>& keys = configFileManagedKeys();

    // An explicit count, so a later plan that adds a Config field has to touch
    // this number rather than silently shipping a setting the GUI cannot save.
    REQUIRE(keys.size() == 21u);

    const std::vector<std::string> expected = {
        "tab-foreground", "tab-background", "frame-background", "button-background",
        "borders", "menu-foreground", "menu-background", "menu-highlight", "menu-borders",
        "tab-font", "menu-font",
        "click-to-focus", "raise-on-focus", "auto-raise", "focus-stealing-prevention",
        "auto-raise-delay", "pointer-stopped-delay", "destroy-window-delay",
        "frame-thickness", "new-window-command", "exec-using-shell"};
    REQUIRE(keys == expected);

    for (const std::string& k : expected) {
        INFO("key: " << k);
        REQUIRE(configFileKeyIsManaged(k));
    }
}

// The rules editor is a Deferred Idea in 09-CONTEXT.md. The writer leaving
// rule lines alone is exactly what lets a later phase add that editor with no
// file-format work, so this boundary is asserted rather than assumed.
TEST_CASE("No rule-* or menu-entry-* key is managed", "[config_writer]") {
    for (const std::string& k : configFileManagedKeys()) {
        INFO("key: " << k);
        CHECK(k.rfind("rule-", 0) != 0);
        CHECK(k.rfind("menu-entry-", 0) != 0);
    }
    CHECK_FALSE(configFileKeyIsManaged("rule-match-class"));
    CHECK_FALSE(configFileKeyIsManaged("rule-action-position"));
    CHECK_FALSE(configFileKeyIsManaged("menu-entry-name"));
    CHECK_FALSE(configFileKeyIsManaged("something-nobody-has-invented-yet"));
    // Case-sensitive, because Config::applyKeyValue() compares keys verbatim
    // and never lowercases them.
    CHECK_FALSE(configFileKeyIsManaged("Tab-Foreground"));
}

// =============================================================================
// Preservation (D-02) -- asserted byte for byte
// =============================================================================

TEST_CASE("A comment, a blank line, an unknown key and a rule group survive a save byte for byte",
          "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");

    const std::string original =
        "# My window manager, my rules.\n"
        "\n"
        "tab-background = #C8CACC\n"
        "\n"
        "# something this version has never heard of\n"
        "future-setting = 42\n"
        "\n"
        "rule-match-class = Firefox\n"
        "rule-action-position = 100,100\n"
        "rule-match-class = Xterm\n"
        "rule-action-desktop = 2\n"
        "\n"
        "   indented-unknown   =   spaced out   \n"
        "a line with no equals sign at all\n";
    writeFile(path, original);

    std::string error;
    REQUIRE(save(path, {set("tab-background", "#101010")}, error) == ConfigWriteResult::Ok);
    REQUIRE(error.empty());

    const std::string expected =
        "# My window manager, my rules.\n"
        "\n"
        "tab-background = #101010\n"
        "\n"
        "# something this version has never heard of\n"
        "future-setting = 42\n"
        "\n"
        "rule-match-class = Firefox\n"
        "rule-action-position = 100,100\n"
        "rule-match-class = Xterm\n"
        "rule-action-desktop = 2\n"
        "\n"
        "   indented-unknown   =   spaced out   \n"
        "a line with no equals sign at all\n";
    REQUIRE(readFile(path) == expected);
}

TEST_CASE("A managed key is replaced in place, keeping its position and its spacing",
          "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");

    // Three different spacing conventions, all of which Config::applyFile()
    // accepts, and all of which the user chose on purpose.
    writeFile(path,
              "frame-thickness=7\n"
              "  auto-raise-delay  =  400\n"
              "borders\t=\t#000000\n");

    std::string error;
    REQUIRE(save(path,
                 {set("frame-thickness", "9"), set("auto-raise-delay", "250"),
                  set("borders", "#FFFFFF")},
                 error) == ConfigWriteResult::Ok);

    REQUIRE(readFile(path) ==
            "frame-thickness=9\n"
            "  auto-raise-delay  =  250\n"
            "borders\t=\t#FFFFFF\n");
}

TEST_CASE("A managed key the file does not contain is appended under one section comment",
          "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");

    writeFile(path,
              "# existing\n"
              "tab-background = #C8CACC\n");

    std::string error;
    REQUIRE(save(path, {set("frame-thickness", "9")}, error) == ConfigWriteResult::Ok);

    const std::string afterFirst = readFile(path);
    REQUIRE(afterFirst ==
            std::string("# existing\n"
                        "tab-background = #C8CACC\n"
                        "\n") +
                kSectionComment + "frame-thickness = 9\n");

    // A second save appends a second key but NOT a second section comment.
    REQUIRE(save(path, {set("frame-thickness", "9"), set("menu-font", "Sans:size=14")}, error) ==
            ConfigWriteResult::Ok);

    const std::string afterSecond = readFile(path);
    REQUIRE(afterSecond ==
            std::string("# existing\n"
                        "tab-background = #C8CACC\n"
                        "\n") +
                kSectionComment + "frame-thickness = 9\nmenu-font = Sans:size=14\n");

    std::size_t occurrences = 0;
    for (std::size_t p = afterSecond.find(kSectionComment); p != std::string::npos;
         p = afterSecond.find(kSectionComment, p + 1)) {
        ++occurrences;
    }
    REQUIRE(occurrences == 1u);
}

TEST_CASE("A file that does not end in a newline gains one before an appended key",
          "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");
    writeFile(path, "tab-background = #C8CACC");  // no trailing newline

    std::string error;
    REQUIRE(save(path, {set("frame-thickness", "9")}, error) == ConfigWriteResult::Ok);

    REQUIRE(readFile(path) == std::string("tab-background = #C8CACC\n\n") + kSectionComment +
                                  "frame-thickness = 9\n");
}

TEST_CASE("A save against a file that does not exist yet creates it", "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");

    std::string error;
    REQUIRE(save(path, {set("frame-thickness", "9")}, error) == ConfigWriteResult::Ok);
    REQUIRE(readFile(path) == std::string(kSectionComment) + "frame-thickness = 9\n");
}

TEST_CASE("A missing parent directory is created first", "[config_writer]") {
    TempDir dir;
    const std::string path = (dir.path() / "wm2-born-again" / "config").string();
    REQUIRE_FALSE(std::filesystem::exists(path));

    std::string error;
    REQUIRE(save(path, {set("frame-thickness", "9")}, error) == ConfigWriteResult::Ok);
    REQUIRE(std::filesystem::exists(path));
    REQUIRE(readFile(path) == std::string(kSectionComment) + "frame-thickness = 9\n");
}

// =============================================================================
// D-13 -- removing means removing, not defaulting
// =============================================================================

TEST_CASE("A removal deletes the line and never writes the built-in default", "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");

    writeFile(path,
              "# keep me\n"
              "frame-thickness = 15\n"
              "tab-background = #C8CACC\n");

    std::string error;
    REQUIRE(save(path, {removeKey("frame-thickness")}, error) == ConfigWriteResult::Ok);

    const std::string after = readFile(path);
    REQUIRE(after ==
            "# keep me\n"
            "tab-background = #C8CACC\n");

    // The key is GONE, not reset to 7. Writing the built-in default would
    // permanently mask a value set in the system-wide layer, which is exactly
    // what "reset to default" must not do.
    REQUIRE(after.find("frame-thickness") == std::string::npos);
    REQUIRE(after.find("7") == std::string::npos);
}

TEST_CASE("Removing a key the file does not contain is a no-op, not an append",
          "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");
    const std::string original = "# nothing to remove here\ntab-background = #C8CACC\n";
    writeFile(path, original);

    std::string error;
    REQUIRE(save(path, {removeKey("frame-thickness")}, error) == ConfigWriteResult::Ok);
    REQUIRE(readFile(path) == original);
}

TEST_CASE("A removal wins over a value left in the same edit", "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");
    writeFile(path, "frame-thickness = 15\n");

    ConfigEdit e;
    e.key = "frame-thickness";
    e.value = "9";  // stale form state the caller forgot to clear
    e.remove = true;

    std::string error;
    REQUIRE(save(path, {e}, error) == ConfigWriteResult::Ok);
    REQUIRE(readFile(path).empty());
}

TEST_CASE("A duplicated managed key collapses to the one line the parser will read",
          "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");

    // Config::applyFile() is later-wins, so replacing only the first line would
    // leave the second one overriding the value the user just chose.
    writeFile(path,
              "frame-thickness = 7\n"
              "# a note in between\n"
              "frame-thickness = 15\n");

    std::string error;
    REQUIRE(save(path, {set("frame-thickness", "9")}, error) == ConfigWriteResult::Ok);

    REQUIRE(readFile(path) ==
            "frame-thickness = 9\n"
            "# a note in between\n");
}

// =============================================================================
// The menu-entry block
// =============================================================================

TEST_CASE("Menu entries are rewritten as one block at the first existing menu-entry line",
          "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");

    writeFile(path,
              "# top\n"
              "tab-background = #C8CACC\n"
              "menu-entry-name = Old Thing\n"
              "menu-entry-command = oldthing\n"
              "menu-entry-category = Custom\n"
              "# a comment AFTER the entries\n"
              "rule-match-class = Firefox\n");

    const std::vector<AppEntry> entries = {
        entry("Terminal", {"xterm", "-ls"}, "System"),
        entry("Editor", {"vim"}, "Custom"),
    };

    std::string error;
    REQUIRE(configFileWrite(path, {}, entries, true, error) == ConfigWriteResult::Ok);

    REQUIRE(readFile(path) ==
            "# top\n"
            "tab-background = #C8CACC\n"
            "menu-entry-name = Terminal\n"
            "menu-entry-command = xterm -ls\n"
            "menu-entry-category = System\n"
            "menu-entry-name = Editor\n"
            "menu-entry-command = vim\n"
            "menu-entry-category = Custom\n"
            "# a comment AFTER the entries\n"
            "rule-match-class = Firefox\n");
}

TEST_CASE("Menu entries with no existing block are appended", "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");
    writeFile(path, "tab-background = #C8CACC\n");

    std::string error;
    REQUIRE(configFileWrite(path, {}, {entry("Terminal", {"xterm"}, "System")}, true, error) ==
            ConfigWriteResult::Ok);

    REQUIRE(readFile(path) ==
            "tab-background = #C8CACC\n"
            "menu-entry-name = Terminal\n"
            "menu-entry-command = xterm\n"
            "menu-entry-category = System\n");
}

TEST_CASE("An empty menu-entry list removes the block and leaves everything else",
          "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");
    writeFile(path,
              "# top\n"
              "menu-entry-name = Old Thing\n"
              "menu-entry-command = oldthing\n"
              "rule-match-class = Firefox\n");

    std::string error;
    REQUIRE(configFileWrite(path, {}, {}, true, error) == ConfigWriteResult::Ok);

    REQUIRE(readFile(path) ==
            "# top\n"
            "rule-match-class = Firefox\n");
}

TEST_CASE("With rewriteMenuEntries false the existing menu-entry lines pass through untouched",
          "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");
    const std::string original =
        "menu-entry-name = Old Thing\n"
        "menu-entry-command = oldthing\n"
        "menu-entry-category = Custom\n";
    writeFile(path, original);

    std::string error;
    // A non-empty list that must be ignored, because the flag is false.
    REQUIRE(configFileWrite(path, {}, {entry("New", {"new"}, "Custom")}, false, error) ==
            ConfigWriteResult::Ok);
    REQUIRE(readFile(path) == original);
}

// =============================================================================
// Idempotency (the edge the probe raised for this phase)
// =============================================================================

TEST_CASE("Running the same edit set twice produces byte-identical output", "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");

    const std::string original =
        "# a user's file, with feelings\n"
        "\n"
        "tab-background=#C8CACC\n"
        "menu-entry-name = Old\n"
        "menu-entry-command = old\n"
        "unknown-key = untouched\n"
        "rule-match-class = Firefox\n"
        "rule-action-desktop = 2\n";
    writeFile(path, original);

    const std::vector<ConfigEdit> edits = {
        set("tab-background", "#101010"),
        set("frame-thickness", "9"),
        removeKey("auto-raise"),
    };
    const std::vector<AppEntry> entries = {entry("Terminal", {"xterm", "-ls"}, "System")};

    std::string error;
    REQUIRE(configFileWrite(path, edits, entries, true, error) == ConfigWriteResult::Ok);
    const std::string first = readFile(path);

    REQUIRE(configFileWrite(path, edits, entries, true, error) == ConfigWriteResult::Ok);
    const std::string second = readFile(path);

    REQUIRE(second == first);

    // And a third, because an off-by-one in the append path can take two rounds
    // to show itself.
    REQUIRE(configFileWrite(path, edits, entries, true, error) == ConfigWriteResult::Ok);
    REQUIRE(readFile(path) == first);
}

// =============================================================================
// Atomicity and containment (T-9-07, T-9-08)
// =============================================================================

TEST_CASE("A save leaves no file anywhere but the target and its own directory",
          "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");
    writeFile(path, "tab-background = #C8CACC\n");

    std::string error;
    REQUIRE(save(path, {set("frame-thickness", "9"), set("menu-font", "Sans:size=14")}, error) ==
            ConfigWriteResult::Ok);

    // Exactly one entry: the target. No temporary left behind, no backup file,
    // nothing beside it.
    const std::vector<std::string> after = dir.entries();
    REQUIRE(after.size() == 1u);
    REQUIRE(after[0] == "config");
}

// The temporary MUST be created in the target's own directory -- a
// cross-directory temporary would make the final rename non-atomic, which is
// the entire reason the temporary exists.
//
// This case proves it by removing write permission from the target's directory
// and nowhere else. If the writer put its temporary in /tmp or in the current
// working directory the save would succeed; because it puts it beside the
// target, it cannot even create it, and the original file is left exactly as it
// was -- which is also T-9-07's assertion.
TEST_CASE("A temporary that cannot be created in the target's own directory fails the save "
          "and leaves the original untouched",
          "[config_writer]") {
    if (::geteuid() == 0) {
        SKIP("running as root: directory permissions are not enforced, so this case cannot "
             "distinguish a temporary beside the target from one anywhere else");
    }

    TempDir dir;
    const std::string path = dir.file("config");
    const std::string original =
        "# precious\n"
        "tab-background = #C8CACC\n"
        "rule-match-class = Firefox\n";
    writeFile(path, original);

    std::error_code ec;
    std::filesystem::permissions(dir.path(),
                                 std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::remove, ec);
    REQUIRE_FALSE(ec);

    std::string error;
    const ConfigWriteResult result = save(path, {set("tab-background", "#101010")}, error);

    std::filesystem::permissions(dir.path(), std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add, ec);

    REQUIRE(result == ConfigWriteResult::TempFailed);
    REQUIRE_FALSE(error.empty());

    // The file is byte-for-byte what it was. There is no partial state.
    REQUIRE(readFile(path) == original);
    const std::vector<std::string> after = dir.entries();
    REQUIRE(after.size() == 1u);
    REQUIRE(after[0] == "config");
}

TEST_CASE("A successful save preserves the target's existing permissions", "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");
    writeFile(path, "tab-background = #C8CACC\n");

    std::error_code ec;
    std::filesystem::permissions(path,
                                 std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write |
                                     std::filesystem::perms::group_read,
                                 std::filesystem::perm_options::replace, ec);
    REQUIRE_FALSE(ec);

    std::string error;
    REQUIRE(save(path, {set("tab-background", "#101010")}, error) == ConfigWriteResult::Ok);

    struct stat st {};
    REQUIRE(::stat(path.c_str(), &st) == 0);
    REQUIRE((st.st_mode & 07777) == 0640u);
}

// =============================================================================
// Refusals -- fail-closed, and before any file is touched
// =============================================================================

TEST_CASE("An edit naming an unmanaged key is refused and the file is not touched",
          "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");
    const std::string original = "rule-match-class = Firefox\ntab-background = #C8CACC\n";
    writeFile(path, original);

    std::string error;
    // The prohibition this enforces: the writer must never rewrite a line it
    // does not manage. A caller asking it to is a bug worth surfacing.
    REQUIRE(save(path, {set("rule-match-class", "Chromium")}, error) ==
            ConfigWriteResult::InvalidEdit);
    REQUIRE_FALSE(error.empty());
    REQUIRE(readFile(path) == original);

    REQUIRE(save(path, {set("menu-entry-name", "Sneaky")}, error) ==
            ConfigWriteResult::InvalidEdit);
    REQUIRE(readFile(path) == original);

    REQUIRE(save(path, {set("not-a-key-at-all", "x")}, error) == ConfigWriteResult::InvalidEdit);
    REQUIRE(readFile(path) == original);

    // And a refusal anywhere in the set refuses the WHOLE set: a valid edit
    // beside an invalid one must not land on its own.
    REQUIRE(save(path, {set("tab-background", "#101010"), set("rule-action-desktop", "2")},
                 error) == ConfigWriteResult::InvalidEdit);
    REQUIRE(readFile(path) == original);
}

TEST_CASE("A value carrying a newline is refused rather than injected as an extra line",
          "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");
    const std::string original = "tab-background = #C8CACC\n";
    writeFile(path, original);

    std::string error;
    REQUIRE(save(path, {set("new-window-command", "xterm\nrule-match-class = Anything")},
                 error) == ConfigWriteResult::InvalidEdit);
    REQUIRE(readFile(path) == original);

    REQUIRE(save(path, {set("new-window-command", "xterm\rmore")}, error) ==
            ConfigWriteResult::InvalidEdit);
    REQUIRE(readFile(path) == original);
}

TEST_CASE("A value longer than the parser will read back is refused", "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");
    writeFile(path, "tab-background = #C8CACC\n");

    std::string error;
    // Config::applyFile() drops values over 256 bytes with a warning, so
    // writing one would produce a line the window manager never reads.
    REQUIRE(save(path, {set("tab-font", std::string(257, 'a'))}, error) ==
            ConfigWriteResult::InvalidEdit);
    REQUIRE(save(path, {set("tab-font", std::string(256, 'a'))}, error) == ConfigWriteResult::Ok);
}

TEST_CASE("A menu entry carrying a newline is refused", "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");
    const std::string original = "tab-background = #C8CACC\n";
    writeFile(path, original);

    std::string error;
    REQUIRE(configFileWrite(path, {}, {entry("Bad\nName", {"x"}, "Custom")}, true, error) ==
            ConfigWriteResult::InvalidEdit);
    REQUIRE(readFile(path) == original);

    REQUIRE(configFileWrite(path, {}, {entry("Fine", {"x\ny"}, "Custom")}, true, error) ==
            ConfigWriteResult::InvalidEdit);
    REQUIRE(readFile(path) == original);
}

TEST_CASE("Two edits naming the same key are refused rather than half-honoured",
          "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");
    const std::string original = "frame-thickness = 7\n";
    writeFile(path, original);

    std::string error;
    // Honouring both would append a second frame-thickness line, which is the
    // "two lines for one key, later wins" state the duplicate collapse exists
    // to prevent -- so the writer refuses the set instead of producing it.
    REQUIRE(save(path, {set("frame-thickness", "9"), set("frame-thickness", "11")}, error) ==
            ConfigWriteResult::InvalidEdit);
    REQUIRE_FALSE(error.empty());
    REQUIRE(readFile(path) == original);
}

TEST_CASE("A file with no trailing newline keeps that shape when nothing lands at its end",
          "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");
    writeFile(path, "frame-thickness=7\ntab-background = #C8CACC");  // no trailing newline

    std::string error;
    REQUIRE(save(path, {set("frame-thickness", "9")}, error) == ConfigWriteResult::Ok);

    // The last line was not touched, so the file still ends without a newline.
    REQUIRE(readFile(path) == "frame-thickness=9\ntab-background = #C8CACC");
}

// =============================================================================
// The writer and the parser are inverses (Task 3)
//
// Everything above proves the writer produces the bytes it meant to. That is
// only half of what matters: a key the GUI can write and the window manager
// never reads back is a setting that appears to work and does nothing, which
// is exactly the dead-config defect plan 08-07 found in the focus booleans.
// These cases close the loop through Config::applyFile().
//
// `rule-*` keys are deliberately absent from every table here, because they
// are deliberately absent from configFileManagedKeys(). The rules editor is a
// Deferred Idea in 09-CONTEXT.md; the writer leaving rule lines untouched is
// what lets a later phase add it with no file-format work at all.
// =============================================================================

namespace {

// A value for each managed key that is DISTINCT FROM ITS BUILT-IN DEFAULT.
// Distinctness is the whole point: a value equal to the default would make
// every assertion below pass whether the file was read or ignored.
const std::vector<std::pair<std::string, std::string>>& distinctiveValues() {
    static const std::vector<std::pair<std::string, std::string>> table = {
        {"tab-foreground",            "#010203"},
        {"tab-background",            "#040506"},
        {"frame-background",          "#070809"},
        {"button-background",         "#0A0B0C"},
        {"borders",                   "#0D0E0F"},
        {"menu-foreground",           "#101112"},
        {"menu-background",           "#131415"},
        {"menu-highlight",            "#161718"},
        {"menu-borders",              "#191A1B"},
        {"tab-font",                  "Distinctive Tab Face:bold:size=19"},
        {"menu-font",                 "Distinctive Menu Face:size=21"},
        {"click-to-focus",            "true"},   // default false
        {"raise-on-focus",            "false"},  // default true
        {"auto-raise",                "false"},  // default true
        {"focus-stealing-prevention", "false"},  // default true
        {"auto-raise-delay",          "1234"},   // default 400
        {"pointer-stopped-delay",     "1235"},   // default 80
        {"destroy-window-delay",      "1236"},   // default 400
        {"frame-thickness",           "13"},     // default 7
        {"new-window-command",        "distinctive-terminal"},
        {"exec-using-shell",          "true"},   // default false
    };
    return table;
}

// The value of one managed key as it stands in a Config, rendered as the string
// the file would carry. This is the ONLY place that maps a key name to a Config
// member, so a key the GUI manages but this function cannot resolve is a
// compile-time-visible gap rather than a silent one.
std::string fieldOf(const Config& c, const std::string& key) {
    if (key == "tab-foreground")            return c.tabForeground;
    if (key == "tab-background")            return c.tabBackground;
    if (key == "frame-background")          return c.frameBackground;
    if (key == "button-background")         return c.buttonBackground;
    if (key == "borders")                   return c.borders;
    if (key == "menu-foreground")           return c.menuForeground;
    if (key == "menu-background")           return c.menuBackground;
    if (key == "menu-highlight")            return c.menuHighlight;
    if (key == "menu-borders")              return c.menuBorders;
    if (key == "tab-font")                  return c.tabFont;
    if (key == "menu-font")                 return c.menuFont;
    if (key == "click-to-focus")            return c.clickToFocus ? "true" : "false";
    if (key == "raise-on-focus")            return c.raiseOnFocus ? "true" : "false";
    if (key == "auto-raise")                return c.autoRaise ? "true" : "false";
    if (key == "focus-stealing-prevention") return c.focusStealingPrevention ? "true" : "false";
    if (key == "auto-raise-delay")          return std::to_string(c.autoRaiseDelay);
    if (key == "pointer-stopped-delay")     return std::to_string(c.pointerStoppedDelay);
    if (key == "destroy-window-delay")      return std::to_string(c.destroyWindowDelay);
    if (key == "frame-thickness")           return std::to_string(c.frameThickness);
    if (key == "new-window-command")        return c.newWindowCommand;
    if (key == "exec-using-shell")          return c.execUsingShell ? "true" : "false";
    return "<no such managed key>";
}

// Every managed field at once, so a case can assert that applying one key
// changed that field AND left the other twenty alone.
std::vector<std::string> snapshot(const Config& c) {
    std::vector<std::string> out;
    for (const std::string& k : configFileManagedKeys()) out.push_back(fieldOf(c, k));
    return out;
}

}  // namespace

TEST_CASE("The distinctive-value table covers exactly the managed key set", "[config_writer]") {
    const std::vector<std::string>& keys = configFileManagedKeys();
    REQUIRE(distinctiveValues().size() == keys.size());

    for (std::size_t i = 0; i < keys.size(); ++i) {
        INFO("position " << i);
        REQUIRE(distinctiveValues()[i].first == keys[i]);
    }

    // Every table value really is distinct from the built-in default, or the
    // round trip below would pass against a writer that produced nothing.
    const Config defaults;
    for (const auto& row : distinctiveValues()) {
        INFO("key: " << row.first);
        REQUIRE(fieldOf(defaults, row.first) != row.second);
    }
}

TEST_CASE("Every managed key survives a write and a read back through Config::applyFile",
          "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");

    std::vector<ConfigEdit> edits;
    for (const auto& row : distinctiveValues()) edits.push_back(set(row.first, row.second));

    std::string error;
    REQUIRE(configFileWrite(path, edits, {}, false, error) == ConfigWriteResult::Ok);
    REQUIRE(error.empty());

    Config loaded;
    loaded.applyFile(path);

    for (const auto& row : distinctiveValues()) {
        INFO("key: " << row.first << " (wrote '" << row.second << "')");
        REQUIRE(fieldOf(loaded, row.first) == row.second);
    }
}

TEST_CASE("Menu entries survive a write and a read back through Config::applyFile",
          "[config_writer]") {
    TempDir dir;
    const std::string path = dir.file("config");

    const std::vector<AppEntry> entries = {
        entry("Terminal", {"xterm", "-ls", "-sb"}, "System"),
        entry("Editor", {"vim"}, "Custom"),
    };

    std::string error;
    REQUIRE(configFileWrite(path, {}, entries, true, error) == ConfigWriteResult::Ok);

    Config loaded;
    loaded.applyFile(path);

    REQUIRE(loaded.manualMenuEntries.size() == 2u);
    CHECK(loaded.manualMenuEntries[0].name == "Terminal");
    CHECK(loaded.manualMenuEntries[0].execArgv == std::vector<std::string>{"xterm", "-ls", "-sb"});
    CHECK(loaded.manualMenuEntries[0].category == "System");
    CHECK(loaded.manualMenuEntries[1].name == "Editor");
    CHECK(loaded.manualMenuEntries[1].execArgv == std::vector<std::string>{"vim"});
    CHECK(loaded.manualMenuEntries[1].category == "Custom");
}

// The drift guard. A managed key the parser ignores is a key the GUI can write
// and the window manager will never read -- it would look like a working
// control that does nothing at all. This case is what catches that.
TEST_CASE("Every managed key reaches a Config field through Config::applyKeyValue",
          "[config_writer]") {
    const std::vector<std::string>& keys = configFileManagedKeys();

    for (std::size_t i = 0; i < keys.size(); ++i) {
        const std::string& key = keys[i];
        const std::string& value = distinctiveValues()[i].second;
        INFO("key: " << key << " = " << value);

        Config before;
        const std::vector<std::string> was = snapshot(before);

        Config after;
        after.applyKeyValue(key, value);
        const std::vector<std::string> now = snapshot(after);

        // The key's own field changed...
        REQUIRE(now[i] != was[i]);
        REQUIRE(now[i] == value);

        // ...and nothing else did. A key that moved the wrong field would pass
        // the assertion above on its own.
        for (std::size_t j = 0; j < keys.size(); ++j) {
            if (j == i) continue;
            INFO("collateral change in: " << keys[j]);
            REQUIRE(now[j] == was[j]);
        }
    }
}

// The mirror of the guard above, and the reason rule-* stays out of the managed
// set: a rule key applied through the parser must NOT touch any managed field.
TEST_CASE("A rule-* key touches no managed field", "[config_writer]") {
    Config before;
    const std::vector<std::string> was = snapshot(before);

    Config after;
    after.applyKeyValue("rule-match-class", "Firefox");
    after.applyKeyValue("rule-action-desktop", "2");

    REQUIRE(snapshot(after) == was);
    REQUIRE(after.rules.size() == 1u);  // it did reach the rules, just not a setting
}


// ---------------------------------------------------------------------------
// A target that could not be READ is never treated as a target that is ABSENT
// (WR-01)
// ---------------------------------------------------------------------------

TEST_CASE("a target whose existence cannot be determined is a read failure",
          "[config_writer][refusal]") {
    TempDir dir;

    // std::filesystem::exists() returns FALSE and sets its error_code on
    // ELOOP, ENAMETOOLONG, EACCES on a path component and EIO. Reading that
    // false as "there is no file here" starts from an empty line vector, emits
    // only the edits, and renames over whatever was really there -- silent
    // data loss, which is the one thing the surgical writer exists to prevent.
    //
    // ENAMETOOLONG is the deterministic way to reach it from a test: the
    // directory is perfectly good, so nothing upstream refuses first, and the
    // error is in the existence check itself.
    const std::string tooLong = dir.file(std::string(300, 'x'));

    std::string error;
    const ConfigWriteResult r = save(tooLong, {set("frame-thickness", "9")}, error);

    INFO("result " << static_cast<int>(r) << ", error '" << error << "'");
    CHECK(r == ConfigWriteResult::ReadFailed);
    CHECK(error.find("could not read") != std::string::npos);

    // And nothing was created on the way to the refusal.
    CHECK(dir.entries().empty());
}

TEST_CASE("an absent target is still an empty starting point, not a failure",
          "[config_writer][refusal]") {
    // The other side of the check above: "could not tell" must become a
    // refusal without "there is genuinely no file" becoming one too.
    TempDir dir;
    const std::string path = dir.file("config");

    std::string error;
    CHECK(save(path, {set("frame-thickness", "9")}, error) == ConfigWriteResult::Ok);
    CHECK(error.empty());
    CHECK(readFile(path).find("frame-thickness = 9") != std::string::npos);
}


// ---------------------------------------------------------------------------
// Two concurrent savers do not lose each other's edits (WR-02)
// ---------------------------------------------------------------------------

TEST_CASE("a save waits for a concurrent saver rather than overwriting it",
          "[config_writer][concurrency]") {
    TempDir dir;
    const std::string path = dir.file("config");
    writeFile(path, "# hand written\nframe-thickness = 7\n");

    // A child stands in for the second wm2-config window: it takes the same
    // lock a save takes, writes the file, and only then lets go. The pipe
    // handshake is what makes the case deterministic rather than a race the
    // test hopes to win -- the parent's save does not begin until the child
    // holds the lock.
    int ready[2] = { -1, -1 };
    REQUIRE(::pipe(ready) == 0);

    const pid_t child = ::fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        ::close(ready[0]);
        const int fd = ::open(dir.path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd < 0) ::_exit(1);
        if (::flock(fd, LOCK_EX) != 0) ::_exit(1);

        char go = 'g';
        ssize_t ignored = ::write(ready[1], &go, 1);
        (void)ignored;
        ::close(ready[1]);

        // Long enough that a save which ignored the lock would certainly have
        // finished and renamed before this line runs.
        ::usleep(400 * 1000);
        writeFile(path, "# hand written\nframe-thickness = 7\nmenu-font = Sans:size=9\n");
        ::flock(fd, LOCK_UN);
        ::close(fd);
        ::_exit(0);
    }

    ::close(ready[1]);
    char go = 0;
    REQUIRE(::read(ready[0], &go, 1) == 1);
    ::close(ready[0]);

    std::string error;
    const ConfigWriteResult r = save(path, {set("tab-font", "Sans:bold:size=13")}, error);

    int status = 0;
    ::waitpid(child, &status, 0);

    const std::string content = readFile(path);
    INFO("result " << static_cast<int>(r) << ", error '" << error << "'");
    INFO("file:\n" << content);

    CHECK(r == ConfigWriteResult::Ok);

    // BOTH edits survive. Without the lock the save read the file before the
    // child wrote it, so the child's line is absent from the output that was
    // renamed over it -- a lost update with no diagnostic, which is exactly
    // what "the last one to press Save wins" hides.
    CHECK(content.find("menu-font = Sans:size=9") != std::string::npos);
    CHECK(content.find("tab-font = Sans:bold:size=13") != std::string::npos);
    // And the pass-through content is still pass-through.
    CHECK(content.find("# hand written") != std::string::npos);
    CHECK(content.find("frame-thickness = 7") != std::string::npos);
}
TEST_CASE("a save refuses rather than waiting for ever on a held lock",
          "[config_writer][concurrency]") {
    // THE OTHER HALF OF THE LOCK (W-02). The case above proves the lock
    // SERIALISES; this one proves it is BOUNDED. configFileWrite() runs from a
    // GTK button handler on the thread that draws the settings window, so a
    // lock held by anything at all -- another wm2-config mid-fsync, a backup
    // tool, a dotfile syncer, a shell running `flock ~/.config/... -c ...` --
    // must not be able to freeze that window with no repaint and no way out.
    //
    // The refusal the user sees is worth more than the wait: `Busy` carries a
    // sentence naming the situation, and the window's existing "Could not
    // save: " path shows it.
    TempDir dir;
    const std::string path = dir.file("config");
    writeFile(path, "# hand written\nframe-thickness = 7\n");

    // TWO pipes. `ready` is the handshake that makes the case deterministic
    // rather than a race the test hopes to win: the parent's save does not
    // begin until the child holds the lock. `hold` is what the child BLOCKS ON,
    // so it releases the lock and exits the moment the parent closes it -- or
    // the moment the parent dies for any reason at all, including a harness
    // timeout on a run where the bound is missing. A child that slept on a
    // timer, or waited to be killed, would be left holding the lock for ever
    // by exactly the run that proves the bug.
    int ready[2] = { -1, -1 };
    int hold[2]  = { -1, -1 };
    REQUIRE(::pipe(ready) == 0);
    REQUIRE(::pipe(hold) == 0);

    const pid_t child = ::fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        ::close(ready[0]);
        ::close(hold[1]);
        const int fd = ::open(dir.path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd < 0) ::_exit(1);
        if (::flock(fd, LOCK_EX) != 0) ::_exit(1);

        char go = 'g';
        ssize_t ignored = ::write(ready[1], &go, 1);
        (void)ignored;
        ::close(ready[1]);

        // Held until the parent lets go of the other end. No timer, so the
        // hold cannot expire mid-measurement.
        char stop = 0;
        while (::read(hold[0], &stop, 1) < 0 && errno == EINTR) { }
        ::_exit(0);
    }

    ::close(ready[1]);
    ::close(hold[0]);
    char go = 0;
    REQUIRE(::read(ready[0], &go, 1) == 1);
    ::close(ready[0]);

    const auto started = std::chrono::steady_clock::now();
    std::string error;
    const ConfigWriteResult r = save(path, {set("tab-font", "Sans:bold:size=13")}, error);
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - started).count();

    // Release the hold and reap. Closing the pipe is what the child is waiting
    // on, so nothing here has to signal anything.
    ::close(hold[1]);
    int status = 0;
    ::waitpid(child, &status, 0);

    INFO("result " << static_cast<int>(r) << ", error '" << error << "', "
         << elapsedMs << " ms");

    // REFUSED, not hung. Without the bound this call never returns until the
    // holder lets go, which for a stuck holder is never.
    CHECK(r == ConfigWriteResult::Busy);
    CHECK(error.find("another program") != std::string::npos);

    // And it refused ON the deadline rather than instantly: an unconditional
    // LOCK_NB with no retry would refuse a lock a healthy saver was about to
    // release, which is a different defect with the same result code. Three
    // times the deadline as the upper bound, so a loaded host cannot flake it.
    CHECK(elapsedMs >= kConfigFileLockWaitMs / 2);
    CHECK(elapsedMs < kConfigFileLockWaitMs * 3);

    // Nothing was read, nothing was written, nothing was left behind.
    CHECK(readFile(path) == "# hand written\nframe-thickness = 7\n");
    const std::vector<std::string> left = dir.entries();
    CHECK(left.size() == 1);
    CHECK(left.front() == "config");
}


// ---------------------------------------------------------------------------
// A value the file format cannot carry is refused where it is typed (WR-03)
// ---------------------------------------------------------------------------

TEST_CASE("a menu entry containing a ';' is refused by the writer too",
          "[config_writer][refusal]") {
    // I-06. WR-15 refuses ';' in MenuEntryDraft::complete(), which is the
    // dialog. It is the ONLY guard: an entry that reaches the writer by any
    // other route still put one in the file, and the route that exists today is
    // a hand-edited config file. `menu-entry-name = Mail; News` is read by the
    // window manager, returned by `get menu-entries`, and split by
    // parseMenuEntriesValue into `menu-entry-name=Mail` and ` News` -- the
    // second has no '=', so the parse fails and the user is told. A name of the
    // form `Foo;menu-entry-category=Bar` parses SUCCESSFULLY into something the
    // user did not write, which is the case worth closing.
    TempDir dir;
    const std::string path = dir.file("config");
    const std::string before = "frame-thickness = 7\n";
    writeFile(path, before);

    const std::vector<AppEntry> refused = {
        entry("Foo;menu-entry-category=Bar", {"thunderbird"}, "Network"),
        entry("Mail", {"sh", "-c", "thunderbird;true"}, "Network"),
        entry("Mail", {"thunderbird"}, "Net;work"),
    };

    for (const AppEntry& e : refused) {
        std::string error;
        INFO("name '" << e.name << "' category '" << e.category << "'");
        CHECK(configFileWrite(path, {}, {e}, true, error) ==
              ConfigWriteResult::InvalidEdit);
        CHECK(error.find("';'") != std::string::npos);
        CHECK(readFile(path) == before);
    }
}


TEST_CASE("a menu entry name or category with an outer space is refused",
          "[config_writer][refusal]") {
    // W-03. WR-03's guard covers ConfigEdit::value, and menu entries reach the
    // same file by a different route: appendMenuEntry writes
    // `menu-entry-name = <name>`, and Config::applyFile() trims what it reads
    // back. A name of " Mail" is written and returns as "Mail" -- the running
    // window manager holds one string and the file says another, which is
    // WR-03's own sentence reached through the menu-entry keys.
    TempDir dir;
    const std::string path = dir.file("config");
    const std::string before = "frame-thickness = 7\n";
    writeFile(path, before);

    const std::vector<AppEntry> refused = {
        entry(" Mail",   {"thunderbird"}, "Network"),
        entry("Mail ",   {"thunderbird"}, "Network"),
        entry("\tMail",  {"thunderbird"}, "Network"),
        entry("Mail",    {"thunderbird"}, " Network"),
        entry("Mail",    {"thunderbird"}, "Network\t"),
    };

    for (const AppEntry& e : refused) {
        std::string error;
        INFO("name '" << e.name << "' category '" << e.category << "'");
        CHECK(configFileWrite(path, {}, {e}, true, error) ==
              ConfigWriteResult::InvalidEdit);
        CHECK(error.find("space") != std::string::npos);
        // Refused BEFORE anything is opened, so the file is untouched.
        CHECK(readFile(path) == before);
    }

    // The command is deliberately NOT refused: it is tokenised on whitespace by
    // Config::applyKeyValue() on every route into the window manager, so an
    // outer space in it is not carried by either representation and round-trips
    // exactly. Refusing it would tell the user something untrue about the file.
    std::string error;
    CHECK(configFileWrite(path, {}, {entry("Mail", {"thunderbird"}, "Network")},
                          true, error) == ConfigWriteResult::Ok);
    CHECK(readFile(path).find("menu-entry-name = Mail") != std::string::npos);
}


TEST_CASE("a value with leading or trailing whitespace is refused",
          "[config_writer][refusal]") {
    TempDir dir;
    const std::string path = dir.file("config");
    writeFile(path, "frame-thickness = 7\n");

    // Config::applyFile() trims what it reads, so a value written with an
    // outer space comes back without it. The window manager would hold one
    // string and the file another, and `get` would disagree with the file the
    // moment anything reloaded.
    for (const std::string& value : { std::string(" xterm"),
                                      std::string("xterm "),
                                      std::string("\txterm"),
                                      std::string("xterm\t"),
                                      std::string("   ") }) {
        std::string error;
        INFO("value '" << value << "'");
        CHECK(save(path, {set("new-window-command", value)}, error) ==
              ConfigWriteResult::InvalidEdit);
        CHECK(error.find("begins or ends with a space") != std::string::npos);
    }

    // Refused BEFORE anything is opened: the file is byte-for-byte what it was.
    CHECK(readFile(path) == "frame-thickness = 7\n");

    // And an inner space is untouched -- it round-trips perfectly, and
    // refusing it would break every multi-word command.
    std::string error;
    CHECK(save(path, {set("new-window-command", "xterm -ls")}, error) ==
          ConfigWriteResult::Ok);
    CHECK(readFile(path).find("new-window-command = xterm -ls") != std::string::npos);

    // The round trip the refusal exists to protect: what the writer accepted
    // is what the parser reads back, through the parser itself.
    Config parsed;
    parsed.applyKeyValue("new-window-command", "xterm -ls");
    CHECK(parsed.newWindowCommand == "xterm -ls");
}


// ---------------------------------------------------------------------------
// A symlinked target is refused rather than silently replaced (WR-05)
// ---------------------------------------------------------------------------

TEST_CASE("saving over a symlinked config file is refused, and the link survives",
          "[config_writer][refusal]") {
    TempDir dir;

    // The arrangement this protects: the real file lives in a dotfiles
    // repository and the configuration path is a link into it.
    const std::string real = dir.file("dotfiles-config");
    const std::string link = dir.file("config");
    writeFile(real, "# from the dotfiles repository\nframe-thickness = 7\n");
    REQUIRE(::symlink(real.c_str(), link.c_str()) == 0);

    std::string error;
    const ConfigWriteResult r = save(link, {set("frame-thickness", "11")}, error);

    INFO("result " << static_cast<int>(r) << ", error '" << error << "'");
    CHECK(r == ConfigWriteResult::WriteFailed);
    CHECK(error.find("symbolic link") != std::string::npos);

    // The link is still a link -- rename() would have replaced it with a
    // regular file, and every later edit would have gone somewhere the
    // repository could not see.
    struct stat lst {};
    REQUIRE(::lstat(link.c_str(), &lst) == 0);
    CHECK(S_ISLNK(lst.st_mode));

    // And the file it points at is untouched.
    CHECK(readFile(real) == "# from the dotfiles repository\nframe-thickness = 7\n");
}
