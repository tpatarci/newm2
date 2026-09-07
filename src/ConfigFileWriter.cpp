// The surgical config-file writer (D-02, D-13, D-04).
//
// Read, classify, emit, rename. Nothing here serialises a Config: the file is
// read as lines, the lines this writer owns are rewritten, and every other line
// is copied through byte for byte. See include/ConfigFileWriter.h for why.
//
// No X11, no Xft, no GTK, no GLib -- the same constraint the header states,
// enforced here by including nothing but the standard library and POSIX.

#include "ConfigFileWriter.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <sys/file.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

// =============================================================================
// File-local helpers
//
// trim() is duplicated from src/Config.cpp rather than hoisted into a shared
// string header, following this project's per-translation-unit convention for
// small static helpers. The duplication is deliberate and it is load-bearing:
// the writer must classify a line EXACTLY as Config::applyFile() does, so the
// two must be the same four lines, and a divergence between them is a bug in
// this file rather than a shared-header refactor away.
// =============================================================================

namespace {

// Byte-for-byte the helper in src/Config.cpp.
void trim(std::string& s) {
    auto a = s.find_first_not_of(" \t");
    auto b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) {
        s.clear();
    } else {
        s = s.substr(a, b - a + 1);
    }
}

// The one comment line the writer may generate. Appended keys go under it so a
// user can see at a glance which lines the GUI added; a second save finds it
// already present and does not add another.
const char* const kSectionComment = "# Added by wm2-config";

// The longest value Config::applyFile() will read back (src/Config.cpp,
// "Reject values > 256 chars"). Writing a longer one would put a line in the
// file that the window manager silently drops.
const std::size_t kMaxValueBytes = kConfigFileMaxValueBytes;

// A key/value line, split the way Config::applyFile() splits it.
struct LineSplit {
    bool isPair = false;   // false for blank, comment, and no-'=' lines
    std::string key;       // trimmed
    std::size_t eqPos = 0; // index of the '=' in the original line
};

LineSplit splitLine(const std::string& line) {
    LineSplit out;

    const auto start = line.find_first_not_of(" \t");
    if (start == std::string::npos) return out;  // blank
    if (line[start] == '#') return out;          // comment

    const auto eq = line.find('=');
    if (eq == std::string::npos) return out;     // no '=' -- the parser warns, we preserve

    out.isPair = true;
    out.eqPos = eq;
    out.key = line.substr(0, eq);
    trim(out.key);
    return out;
}

bool isMenuEntryKey(const std::string& key) {
    return key == "menu-entry-name" || key == "menu-entry-command" ||
           key == "menu-entry-category";
}

// Rebuilds a key/value line with a new value, keeping everything to the left of
// the value exactly as the user wrote it: the indentation, the key's own
// spelling, the '=', and whatever whitespace followed it. A file written with
// `key=value` stays that way and one written with `  key  =  value` stays that
// way, which is both a courtesy and what makes a second identical save produce
// byte-identical output.
std::string replaceValue(const std::string& line, std::size_t eqPos, const std::string& value) {
    std::string out = line.substr(0, eqPos + 1);

    std::size_t p = eqPos + 1;
    while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) {
        out += line[p];
        ++p;
    }

    out += value;

    // A file with CRLF endings keeps them: the '\r' is part of the line as the
    // splitter saw it, and dropping it would rewrite a line convention the user
    // did not ask to change.
    if (!line.empty() && line.back() == '\r') out += '\r';

    return out;
}

// Every reason an edit is refused, as a sentence for the user. Returns true
// when the whole set is acceptable. Checked BEFORE any file is opened, so a
// refusal never leaves a half-written file behind.
bool editsAreAcceptable(const std::vector<ConfigEdit>& edits, std::string& errorOut) {
    for (std::size_t i = 0; i < edits.size(); ++i) {
        for (std::size_t j = i + 1; j < edits.size(); ++j) {
            if (edits[i].key != edits[j].key) continue;
            // Two edits for one key is a caller bug with a silent outcome: the
            // second would be appended as a duplicate line, which is precisely
            // the "two lines for one key, later wins" state the collapse below
            // exists to prevent. Refused rather than half-honoured.
            errorOut = "'" + edits[i].key + "' appears more than once in the edit set";
            return false;
        }
    }

    for (const ConfigEdit& e : edits) {
        if (!configFileKeyIsManaged(e.key)) {
            errorOut = "'" + e.key + "' is not a key the configuration editor manages";
            return false;
        }
        if (e.remove) continue;  // the value is ignored entirely
        if (e.value.find('\n') != std::string::npos ||
            e.value.find('\r') != std::string::npos) {
            errorOut = "the value for '" + e.key + "' contains a line break";
            return false;
        }
        if (e.value.size() > kMaxValueBytes) {
            errorOut = "the value for '" + e.key + "' is longer than " +
                       std::to_string(kMaxValueBytes) + " characters and would not be read back";
            return false;
        }
        // A value the FILE FORMAT CANNOT CARRY, refused in the writer's own
        // vocabulary rather than lost silently (WR-03). Config::applyFile()
        // trims the value it reads, so " xterm" is written as
        // `new-window-command =  xterm` and read back as "xterm": the running
        // window manager holds one string, the file says another, and `get`
        // disagrees with the file the moment anything reloads. A value that is
        // entirely whitespace becomes the empty string. Refusing is the only
        // outcome that does not quietly change what the user typed.
        if (!e.value.empty() &&
            (e.value.front() == ' ' || e.value.front() == '\t' ||
             e.value.back()  == ' ' || e.value.back()  == '\t')) {
            errorOut = "the value for '" + e.key + "' begins or ends with a space, "
                       "which the configuration file cannot preserve";
            return false;
        }
    }
    return true;
}

bool menuEntriesAreAcceptable(const std::vector<AppEntry>& entries, std::string& errorOut) {
    for (const AppEntry& e : entries) {
        std::string command;
        for (std::size_t i = 0; i < e.execArgv.size(); ++i) {
            if (i != 0) command += ' ';
            command += e.execArgv[i];
        }
        const std::string* parts[3] = {&e.name, &command, &e.category};
        const char* names[3] = {"name", "command", "category"};
        for (int i = 0; i < 3; ++i) {
            if (parts[i]->find('\n') != std::string::npos ||
                parts[i]->find('\r') != std::string::npos) {
                errorOut = std::string("the menu entry ") + names[i] + " contains a line break";
                return false;
            }
            if (parts[i]->size() > kMaxValueBytes) {
                errorOut = std::string("the menu entry ") + names[i] + " is longer than " +
                           std::to_string(kMaxValueBytes) + " characters";
                return false;
            }
            // WR-15'S RULE, IN THE WRITER AS WELL AS THE DIALOG (I-06).
            // MenuEntryDraft::complete() was the only guard against ';', so an
            // entry reaching the writer by any other route still put one in
            // the file -- and the route that exists today is a hand-edited
            // config file, read back by the window manager and returned by
            // `get menu-entries`. A name of the form
            // `Foo;menu-entry-category=Bar` then parses SUCCESSFULLY into
            // something the user did not write. The wire grammar separates
            // records with ';' and has no escape, so the character simply
            // cannot be carried; refusing it is fail-closed and cheap.
            if (parts[i]->find(';') != std::string::npos) {
                errorOut = std::string("the menu entry ") + names[i] +
                           " contains a ';', which is what separates entries "
                           "when the list is sent to the window manager";
                return false;
            }
            // WR-03'S RULE, REACHED THROUGH THE MENU-ENTRY KEYS (W-03).
            // appendMenuEntry writes `menu-entry-name = <name>` and
            // Config::applyFile() trims what it reads back, so a name of
            // " Mail" is written and returns as "Mail": the running window
            // manager holds one string, the file says another, and the wire
            // path -- which does NOT trim -- disagrees with both. Refused in
            // the same words editsAreAcceptable() uses, because it is the same
            // rule.
            //
            // The COMMAND is exempt. It is tokenised on whitespace by
            // Config::applyKeyValue() on every route into the window manager,
            // so an outer space in it is carried by neither representation and
            // round-trips exactly; refusing it would state something about the
            // file format that is not true of that field.
            if (i != 1 && !parts[i]->empty() &&
                (parts[i]->front() == ' ' || parts[i]->front() == '\t' ||
                 parts[i]->back()  == ' ' || parts[i]->back()  == '\t')) {
                errorOut = std::string("the menu entry ") + names[i] +
                           " begins or ends with a space, which the configuration "
                           "file cannot preserve";
                return false;
            }
        }
    }
    return true;
}

// The three-key group the parser's accumulator requires, in the order it
// requires: the name key opens the entry, then command, then category.
//
// The command is the argv joined with single spaces, which is the exact inverse
// of the whitespace tokenisation Config::applyKeyValue() performs on
// menu-entry-command. An argument containing a space cannot survive that round
// trip -- but it cannot be expressed in the file format at all, so this loses
// nothing the format could have carried.
void appendMenuEntry(const AppEntry& e, std::vector<std::string>& out) {
    out.push_back("menu-entry-name = " + e.name);

    std::string command;
    for (std::size_t i = 0; i < e.execArgv.size(); ++i) {
        if (i != 0) command += ' ';
        command += e.execArgv[i];
    }
    out.push_back("menu-entry-command = " + command);

    out.push_back("menu-entry-category = " + e.category);
}

// An exclusive advisory lock over the whole read-modify-write cycle (WR-02).
//
// configFileWrite() is read, classify, emit, rename. rename() gives atomicity
// of the FILE; it gives none at all to the cycle. Two savers -- two wm2-config
// windows, the program being deliberately G_APPLICATION_NON_UNIQUE, or one
// window and a hand edit -- interleave as A reads, B reads, A renames, B
// renames, and B's output was computed from the pre-A file. A's edits are gone
// with no diagnostic, which "the last one to press Save wins" understates.
//
// THE DIRECTORY, NOT A LOCK FILE. The target may not exist yet, and even when
// it does the rename replaces the inode, so a lock taken on the file would be
// held on the wrong one by the time it mattered. Locking the parent directory
// has neither problem, creates no litter beside the user's configuration, and
// costs nothing in contention: the directory holds one configuration file.
//
// BEST EFFORT, NEVER FATAL, BUT NEVER UNBOUNDED EITHER (W-02). A filesystem
// that will not lock at all (some NFS mounts) leaves the save exactly as
// unserialised as it was before this existed, which is strictly no worse. A
// filesystem that WOULD lock and a holder that will not let go are a different
// thing: the wait is bounded at kConfigFileLockWaitMs and the save is refused
// with ConfigWriteResult::Busy rather than freezing the GTK main thread until
// the holder decides otherwise.
//
// THE WAIT IS A POLL, NOT A BLOCKING flock. LOCK_EX alone has no deadline and
// is interruptible: a signal delivered during it returns EINTR, and treating
// that as "this filesystem cannot lock" would let the save proceed entirely
// unserialised -- the lost update WR-02 exists to prevent, produced by a
// SIGCHLD. LOCK_NB in a loop makes EINTR a retry rather than a verdict and
// makes the deadline expressible at all.
//
// The two failures are told apart because they mean different things to the
// caller: `unsupported()` is "there is no lock here, carry on unserialised",
// `busy()` is "somebody else holds it, refuse and say so".
class DirectoryLock {
public:
    explicit DirectoryLock(const std::filesystem::path& directory) {
        m_fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (m_fd < 0) return;   // no lock to be had; unsupported, not busy

        const auto until = std::chrono::steady_clock::now() +
                           std::chrono::milliseconds(kConfigFileLockWaitMs);
        for (;;) {
            if (::flock(m_fd, LOCK_EX | LOCK_NB) == 0) return;   // held
            if (errno == EINTR) continue;                        // not a refusal
            if (errno != EWOULDBLOCK && errno != EAGAIN) break;   // cannot lock here
            if (std::chrono::steady_clock::now() >= until) {
                m_busy = true;
                break;
            }
            ::usleep(20 * 1000);
        }

        ::close(m_fd);
        m_fd = -1;
    }

    ~DirectoryLock() {
        if (m_fd < 0) return;
        (void)::flock(m_fd, LOCK_UN);
        ::close(m_fd);      // released by the close alone; the unlock is explicit
    }

    DirectoryLock(const DirectoryLock&) = delete;
    DirectoryLock& operator=(const DirectoryLock&) = delete;

    // Somebody else held the lock for the whole of the deadline. The save must
    // be refused: it is the only outcome that neither hangs the window nor
    // overwrites the other saver's work.
    bool busy() const { return m_busy; }

private:
    int  m_fd = -1;
    bool m_busy = false;
};

// Reads the file into lines. `exists` distinguishes "no such file" (which is an
// empty starting point, not an error) from a real read failure.
bool readLines(const std::string& path, std::vector<std::string>& lines,
               bool& endedWithNewline, bool& exists) {
    lines.clear();
    endedWithNewline = true;

    // "COULD NOT TELL" IS NOT "IS NOT THERE" (WR-01). exists() returns false
    // AND sets its error_code on ELOOP, ENAMETOOLONG, EACCES on a path
    // component and EIO. Reading that false as an absent file starts the save
    // from an empty line vector, emits only the edits, and renames over
    // whatever was really at the path -- silent data loss, which is the one
    // thing this writer exists to prevent. The is_open() branch below already
    // gets this right; the existence check has to agree with it.
    std::error_code ec;
    exists = std::filesystem::exists(path, ec);
    if (ec) {
        errno = ec.value();
        exists = false;
        return false;          // ReadFailed -- "could not read X", never "X is absent"
    }
    if (!exists) return true;

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;

    std::ostringstream ss;
    ss << in.rdbuf();
    if (in.bad()) return false;
    const std::string content = ss.str();

    if (content.empty()) return true;

    std::size_t start = 0;
    for (;;) {
        const std::size_t nl = content.find('\n', start);
        if (nl == std::string::npos) {
            lines.push_back(content.substr(start));
            endedWithNewline = false;
            break;
        }
        lines.push_back(content.substr(start, nl - start));
        start = nl + 1;
        if (start == content.size()) break;
    }
    return true;
}

}  // namespace

// =============================================================================
// The managed key set
// =============================================================================

const std::vector<std::string>& configFileManagedKeys() {
    // Grouped the way include/Config.h groups the fields they map to, so the
    // two can be read side by side. The count is asserted in a
    // [config_writer] case: adding a Config field means touching that number.
    static const std::vector<std::string> keys = {
        // Colours (nine)
        "tab-foreground",
        "tab-background",
        "frame-background",
        "button-background",
        "borders",
        "menu-foreground",
        "menu-background",
        "menu-highlight",
        "menu-borders",
        // Fonts (two, from 09-01)
        "tab-font",
        "menu-font",
        // Focus policy (three booleans plus focus-stealing prevention)
        "click-to-focus",
        "raise-on-focus",
        "auto-raise",
        "focus-stealing-prevention",
        // Timing (three)
        "auto-raise-delay",
        "pointer-stopped-delay",
        "destroy-window-delay",
        // Frame, commands
        "frame-thickness",
        "new-window-command",
        "exec-using-shell",
    };
    return keys;
}

bool configFileKeyIsManaged(const std::string& key) {
    for (const std::string& k : configFileManagedKeys()) {
        if (k == key) return true;
    }
    return false;
}

std::vector<std::string> configFileKeysIn(const std::string& path) {
    std::vector<std::string> keys;

    std::vector<std::string> lines;
    bool endedWithNewline = true;
    bool exists = false;
    // A read that FAILED and a file that is not THERE both answer "this file
    // sets nothing", and deliberately: the caller is asking which keys the user
    // file overrides, and a file it cannot read overrides nothing it can act
    // on. Distinguishing the two here would put a second error path into a
    // question that has no second answer.
    if (!readLines(path, lines, endedWithNewline, exists) || !exists) return keys;

    for (const std::string& line : lines) {
        // Config::applyFile()'s own first guard, before anything is split.
        if (line.size() > 4096) continue;
        const LineSplit split = splitLine(line);
        if (!split.isPair) continue;
        keys.push_back(split.key);
    }
    return keys;
}

// =============================================================================
// configFileWrite
// =============================================================================

ConfigWriteResult configFileWrite(const std::string& path,
                                  const std::vector<ConfigEdit>& edits,
                                  const std::vector<AppEntry>& menuEntries,
                                  bool rewriteMenuEntries,
                                  std::string& errorOut) {
    errorOut.clear();

    // --- Refusals, before anything is opened -------------------------------
    if (!editsAreAcceptable(edits, errorOut)) return ConfigWriteResult::InvalidEdit;
    if (rewriteMenuEntries && !menuEntriesAreAcceptable(menuEntries, errorOut)) {
        return ConfigWriteResult::InvalidEdit;
    }

    // --- The parent directory ----------------------------------------------
    std::error_code ec;
    const std::filesystem::path target(path);
    const std::filesystem::path directory =
        target.has_parent_path() ? target.parent_path() : std::filesystem::path(".");

    // "COULD NOT TELL" IS NOT "IS NOT THERE" (I-03, the same shape WR-01 fixed
    // in readLines()). exists() returns false AND sets its error_code on
    // ELOOP, ENAMETOOLONG, EACCES on a path component and EIO. Reading that
    // false as an absent directory sends this straight into
    // create_directories, which then fails with a message about creating a
    // directory that may well already exist -- the two spellings of the same
    // call, thirty lines apart in one file, disagreeing about whether a failed
    // existence check is a failure.
    const bool directoryExists = std::filesystem::exists(directory, ec);
    if (ec) {
        errorOut = "could not examine the directory '" + directory.string() +
                   "': " + ec.message();
        return ConfigWriteResult::DirectoryFailed;
    }
    if (!directoryExists) {
        std::filesystem::create_directories(directory, ec);
        if (ec) {
            errorOut = "could not create the directory '" + directory.string() +
                       "': " + ec.message();
            return ConfigWriteResult::DirectoryFailed;
        }
    }

    // --- The lock, held from before the read until after the rename ---------
    //
    // Declared here so it covers every return below: the read the output is
    // computed from and the rename that publishes it are one critical section,
    // or a concurrent saver's edits are lost (WR-02).
    const DirectoryLock lock(directory);
    if (lock.busy()) {
        errorOut = "another program is writing '" + path + "' at the moment. "
                   "Nothing was changed; try saving again in a moment.";
        return ConfigWriteResult::Busy;
    }

    // --- A symlinked target is REFUSED, and said so (WR-05) -----------------
    //
    // BELOW THE LOCK (I-04). The check used to run above it, which made it
    // advisory: another saver could in principle have replaced the target
    // between this lstat and the lock being taken. No saver this project
    // ships creates symlinks, so nothing could actually race it -- but
    // moving the check inside the critical section costs nothing and
    // removes the question entirely.
    //
    // rename() replaces the LINK, not what it points at. A user who keeps
    // ~/.config/wm2-born-again/config as a symlink into a dotfiles repository
    // -- a common arrangement -- lost the link on the first Save, and every
    // later edit went to the real file while the repository copy silently
    // stopped being the source of truth.
    //
    // NOT writing through the symlink is the security-correct choice and is
    // kept; the defect was doing it silently. Refusing and naming the
    // situation lets the user edit the file the link points at, which is what
    // they meant.
    {
        struct stat lst {};
        if (::lstat(path.c_str(), &lst) == 0 && S_ISLNK(lst.st_mode)) {
            errorOut = "'" + path + "' is a symbolic link. Saving would replace "
                       "the link with a regular file; edit the file it points "
                       "at instead.";
            return ConfigWriteResult::WriteFailed;
        }
    }

    // --- Read ---------------------------------------------------------------
    std::vector<std::string> lines;
    bool endedWithNewline = true;
    bool targetExists = false;
    if (!readLines(path, lines, endedWithNewline, targetExists)) {
        errorOut = "could not read '" + path + "': " + std::strerror(errno);
        return ConfigWriteResult::ReadFailed;
    }

    // --- Classify and emit --------------------------------------------------
    std::vector<std::string> out;
    out.reserve(lines.size() + edits.size() + menuEntries.size() * 3 + 2);

    std::vector<bool> seen(edits.size(), false);
    bool sawSectionComment = false;
    bool sawMenuEntry = false;
    std::size_t menuInsertPos = 0;

    for (const std::string& line : lines) {
        std::string bare = line;
        trim(bare);
        if (bare == kSectionComment) sawSectionComment = true;

        const LineSplit split = splitLine(line);

        if (!split.isPair) {
            out.push_back(line);
            continue;
        }

        if (rewriteMenuEntries && isMenuEntryKey(split.key)) {
            // Dropped here; the whole block is re-emitted at the position of
            // the FIRST one, so the entries keep the place in the file the user
            // put them in.
            if (!sawMenuEntry) {
                sawMenuEntry = true;
                menuInsertPos = out.size();
            }
            continue;
        }

        // Which edit, if any, owns this line.
        std::size_t which = edits.size();
        for (std::size_t i = 0; i < edits.size(); ++i) {
            if (edits[i].key == split.key) { which = i; break; }
        }

        if (which == edits.size()) {
            out.push_back(line);  // not ours: byte for byte
            continue;
        }

        if (edits[which].remove) {
            seen[which] = true;
            continue;  // D-13: the line goes, the built-in default is NOT written
        }

        if (seen[which]) {
            // A duplicate of a key already rewritten above. Config::applyFile()
            // is later-wins, so leaving this line would let it override the
            // value just written -- the save would appear to do nothing. It is
            // a managed key, so collapsing it is within what this writer owns.
            continue;
        }

        seen[which] = true;
        out.push_back(replaceValue(line, split.eqPos, edits[which].value));
    }

    // --- The menu-entry block ----------------------------------------------
    if (rewriteMenuEntries) {
        std::vector<std::string> block;
        for (const AppEntry& e : menuEntries) appendMenuEntry(e, block);

        if (!block.empty()) {
            const std::size_t at = sawMenuEntry ? menuInsertPos : out.size();
            out.insert(out.begin() + static_cast<std::ptrdiff_t>(at), block.begin(), block.end());
        }
    }

    // --- Keys the file did not have ----------------------------------------
    std::vector<std::string> appended;
    for (std::size_t i = 0; i < edits.size(); ++i) {
        if (seen[i] || edits[i].remove) continue;
        appended.push_back(edits[i].key + " = " + edits[i].value);
    }

    if (!appended.empty()) {
        if (!sawSectionComment) {
            // A blank line only when there is something to separate from.
            if (!out.empty()) out.push_back(std::string());
            out.push_back(kSectionComment);
        }
        for (const std::string& line : appended) out.push_back(line);
    }

    // --- Serialise ----------------------------------------------------------
    std::string content;
    for (const std::string& line : out) {
        content += line;
        content += '\n';
    }
    // A file that did not end in a newline keeps that shape, but only while its
    // last line is still literally the line it was -- if the save changed,
    // removed or appended past the end, the file ends in a newline like every
    // other line in it. The predicate is the last line itself rather than a
    // count, because a count also matches "one line removed, one appended".
    if (!endedWithNewline && targetExists && !content.empty() && !out.empty() &&
        !lines.empty() && out.back() == lines.back()) {
        content.erase(content.size() - 1);
    }

    // --- Write to a temporary IN THE TARGET'S OWN DIRECTORY -----------------
    //
    // Beside the target and nowhere else: rename() is only atomic within one
    // filesystem, and a temporary in /tmp could be on a different one. That is
    // the entire reason the temporary exists.
    std::string tmpPath = (directory / ("." + target.filename().string() + ".wm2-XXXXXX")).string();
    std::vector<char> tmpBuf(tmpPath.begin(), tmpPath.end());
    tmpBuf.push_back('\0');

    const int fd = ::mkstemp(tmpBuf.data());
    if (fd < 0) {
        errorOut = "could not create a temporary file beside '" + path + "': " +
                   std::strerror(errno);
        return ConfigWriteResult::TempFailed;
    }
    tmpPath.assign(tmpBuf.data());

    // The new file inherits the target's permissions when there is a target;
    // otherwise it keeps mkstemp's 0600, which is the conservative choice for a
    // file only its owner needs to read (and matches D-16's uid boundary in
    // spirit).
    if (targetExists) {
        struct stat st {};
        if (::stat(path.c_str(), &st) == 0) {
            // IN-07: 0777, not 07777. A configuration file should never
            // carry setuid, setgid or the sticky bit; if one somehow does,
            // copying it onto the replacement propagates it rather than
            // dropping it.
            (void)::fchmod(fd, st.st_mode & 0777);
        }
    }

    // THE ERRNO IS CAPTURED AT EACH FAILURE SITE, not once at the end (WR-04).
    // A single read after the last call reports whatever errno happened to
    // hold, which for a failing close() after a SUCCESSFUL fsync is commonly 0
    // -- producing "could not write '...': Success", a save the user is told
    // failed with no cause given. close() is exactly where deferred write
    // errors surface on NFS and on some journalling filesystems, so it is the
    // case that matters most.
    std::size_t written = 0;
    bool writeOk = true;
    int failErrno = 0;
    while (written < content.size()) {
        const ssize_t n = ::write(fd, content.data() + written, content.size() - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            writeOk = false;
            failErrno = errno;
            break;
        }
        written += static_cast<std::size_t>(n);
    }

    // fsync BEFORE the rename: a rename over a file whose contents are still
    // only in the page cache can survive a crash as an empty file, which is
    // exactly the corruption the temporary exists to prevent.
    if (writeOk && ::fsync(fd) != 0) { writeOk = false; failErrno = errno; }

    if (::close(fd) != 0 && writeOk) { writeOk = false; failErrno = errno; }

    if (!writeOk) {
        errorOut = "could not write '" + tmpPath + "': " + std::strerror(failErrno);
        (void)::unlink(tmpPath.c_str());
        return ConfigWriteResult::WriteFailed;
    }

    if (::rename(tmpPath.c_str(), path.c_str()) != 0) {
        errorOut = "could not replace '" + path + "': " + std::strerror(errno);
        (void)::unlink(tmpPath.c_str());
        return ConfigWriteResult::RenameFailed;
    }

    // Best effort, and deliberately not a failure: the rename has already
    // happened, so the caller's edit is in the file either way. Syncing the
    // directory only decides whether that survives a power loss in the next few
    // seconds, and reporting a failed save for it would be a lie.
    const int dirFd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY);
    if (dirFd >= 0) {
        (void)::fsync(dirFd);
        (void)::close(dirFd);
    }

    return ConfigWriteResult::Ok;
}
