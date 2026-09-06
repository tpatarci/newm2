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
#include "ConfigFileWriter.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
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
