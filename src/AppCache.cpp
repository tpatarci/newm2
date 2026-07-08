#include "AppCache.h"

#include "DesktopEntry.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <sys/stat.h>

namespace {

// $XDG_CONFIG_HOME (or $HOME/.config fallback), reimplemented locally
// (mirrors xdgConfigHome() in src/Config.cpp:51-58 exactly) since AppCache
// is a separate translation unit and this header does not expose Config's
// internal helper.
std::string appCacheXdgConfigHome() {
    const char* home = std::getenv("XDG_CONFIG_HOME");
    if (home && home[0] == '/') return home;

    const char* userHome = std::getenv("HOME");
    if (!userHome) userHome = "/tmp";
    return std::string(userHome) + "/.config";
}

// Creates every missing directory component of the parent directory of
// `path`. Pre-existing components are left alone (mkdir() failures other
// than "already exists" are not fatal here -- write() below will simply
// fail to open the file and warn, same as any other unwritable path).
void ensureParentDirExists(const std::string& path) {
    auto slash = path.find_last_of('/');
    if (slash == std::string::npos) return;
    std::string dir = path.substr(0, slash);
    if (dir.empty()) return;

    std::string partial;
    size_t pos = 0;
    if (dir[0] == '/') {
        partial = "/";
        pos = 1;
    }
    while (pos <= dir.size()) {
        auto next = dir.find('/', pos);
        std::string component =
            (next == std::string::npos) ? dir.substr(pos) : dir.substr(pos, next - pos);
        if (!component.empty()) {
            partial += component;
            ::mkdir(partial.c_str(), 0755);  // ignore errors (e.g. EEXIST)
            partial += "/";
        }
        if (next == std::string::npos) break;
        pos = next + 1;
    }
}

// Escapes a string for embedding inside a JSON double-quoted literal:
// backslash-escapes '"' and '\\', and \u-escapes any byte < 0x20.
std::string escapeJson(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    result += buf;
                } else {
                    result += static_cast<char>(c);
                }
        }
    }
    return result;
}

std::string sourceToString(AppEntry::Source source) {
    switch (source) {
        case AppEntry::Source::Desktop:    return "desktop";
        case AppEntry::Source::BinaryScan: return "binaryscan";
        case AppEntry::Source::Manual:     return "manual";
    }
    return "desktop";
}

// Finds the index of the character in `s` (starting at `openPos`, which
// must hold `openChar`) that closes the matching `openChar`/`closeChar`
// pair, skipping over the contents of double-quoted strings (respecting
// backslash-escaping) so that a brace/bracket inside a string value is not
// mistaken for structural JSON. Returns std::string::npos if unmatched.
size_t findMatching(const std::string& s, size_t openPos, char openChar, char closeChar) {
    int depth = 0;
    bool inString = false;
    for (size_t i = openPos; i < s.size(); ++i) {
        char c = s[i];
        if (inString) {
            if (c == '\\' && i + 1 < s.size()) {
                ++i;
                continue;
            }
            if (c == '"') inString = false;
            continue;
        }
        if (c == '"') {
            inString = true;
            continue;
        }
        if (c == openChar) {
            ++depth;
        } else if (c == closeChar) {
            --depth;
            if (depth == 0) return i;
        }
    }
    return std::string::npos;
}

// Unescapes a JSON string literal's contents (the substring strictly
// between the opening and closing double quotes has already been located
// by the caller).
std::string unescapeJsonBody(const std::string& s, size_t begin, size_t end) {
    std::string result;
    size_t p = begin;
    while (p < end) {
        if (s[p] == '\\' && p + 1 < end) {
            char next = s[p + 1];
            if (next == '"')       result += '"';
            else if (next == '\\') result += '\\';
            else                    result += next;  // best-effort for other escapes
            p += 2;
        } else {
            result += s[p];
            ++p;
        }
    }
    return result;
}

// Extracts the value of a `"key": "..."` string field from a JSON object
// substring `obj`. Returns std::nullopt if the key is absent or the value
// is not a well-formed quoted string.
std::optional<std::string> extractString(const std::string& obj, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    auto keyPos = obj.find(pattern);
    if (keyPos == std::string::npos) return std::nullopt;

    auto colon = obj.find(':', keyPos + pattern.size());
    if (colon == std::string::npos) return std::nullopt;

    size_t p = colon + 1;
    while (p < obj.size() && std::isspace(static_cast<unsigned char>(obj[p]))) ++p;
    if (p >= obj.size() || obj[p] != '"') return std::nullopt;
    ++p;

    size_t start = p;
    while (p < obj.size() && obj[p] != '"') {
        if (obj[p] == '\\' && p + 1 < obj.size()) {
            p += 2;
        } else {
            ++p;
        }
    }
    if (p >= obj.size()) return std::nullopt;  // unterminated string

    return unescapeJsonBody(obj, start, p);
}

// Extracts the value of a `"key": ["...", "...", ...]` string-array field
// from a JSON object substring `obj`. Returns std::nullopt if the key is
// absent or the value is not a well-formed array.
std::optional<std::vector<std::string>> extractStringArray(const std::string& obj,
                                                             const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    auto keyPos = obj.find(pattern);
    if (keyPos == std::string::npos) return std::nullopt;

    auto colon = obj.find(':', keyPos + pattern.size());
    if (colon == std::string::npos) return std::nullopt;

    auto arrOpen = obj.find('[', colon);
    if (arrOpen == std::string::npos) return std::nullopt;
    auto arrClose = findMatching(obj, arrOpen, '[', ']');
    if (arrClose == std::string::npos) return std::nullopt;

    std::vector<std::string> result;
    size_t p = arrOpen + 1;
    while (p < arrClose) {
        while (p < arrClose &&
               (std::isspace(static_cast<unsigned char>(obj[p])) || obj[p] == ',')) {
            ++p;
        }
        if (p >= arrClose) break;
        if (obj[p] != '"') {
            ++p;  // skip stray character rather than aborting the whole array
            continue;
        }
        ++p;
        size_t start = p;
        while (p < arrClose && obj[p] != '"') {
            if (obj[p] == '\\' && p + 1 < arrClose) {
                p += 2;
            } else {
                ++p;
            }
        }
        result.push_back(unescapeJsonBody(obj, start, p));
        if (p < arrClose) ++p;  // skip closing quote
    }
    return result;
}

// Extracts a top-level integer field's raw digit run following its colon
// (used only for "scannedAt", which is never a JSON string literal).
std::optional<long long> extractInteger(const std::string& content, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    auto keyPos = content.find(pattern);
    if (keyPos == std::string::npos) return std::nullopt;

    auto colon = content.find(':', keyPos + pattern.size());
    if (colon == std::string::npos) return std::nullopt;

    size_t p = colon + 1;
    while (p < content.size() && std::isspace(static_cast<unsigned char>(content[p]))) ++p;
    size_t start = p;
    if (p < content.size() && content[p] == '-') ++p;
    while (p < content.size() && std::isdigit(static_cast<unsigned char>(content[p]))) ++p;
    if (p == start) return std::nullopt;

    try {
        return std::stoll(content.substr(start, p - start));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace

namespace AppCache {

std::string defaultCachePath() {
    return appCacheXdgConfigHome() + "/wm2-born-again/appcache.json";
}

bool needsRescan(time_t cachedScanTime) {
    struct stat st;
    if (::stat("/usr/bin", &st) == 0 && st.st_mtime > cachedScanTime) return true;

    // Directory mtime only reflects entry additions/removals, not in-place
    // edits to an existing .desktop file's contents -- an accepted,
    // documented D-06 limitation (07-RESEARCH.md Pitfall 3): catching new
    // apps is required, catching in-place edits is not.
    for (const auto& dir : DesktopEntry::xdgApplicationsDirs()) {
        // A missing directory is not "newer" than cachedScanTime -- it is
        // simply absent, so stat() failure here is not itself a signal to
        // rescan; skip silently and keep checking the rest.
        if (::stat(dir.c_str(), &st) == 0 && st.st_mtime > cachedScanTime) return true;
    }

    return false;
}

CacheData read(const std::string& path) {
    CacheData result;

    std::ifstream in(path);
    if (!in.is_open()) return result;  // Missing/unreadable file -- skip silently.

    std::stringstream buf;
    buf << in.rdbuf();
    std::string content = buf.str();

    if (auto sat = extractInteger(content, "scannedAt")) {
        result.scannedAt = static_cast<time_t>(*sat);
    }

    auto entriesKeyPos = content.find("\"entries\"");
    if (entriesKeyPos == std::string::npos) return result;

    auto arrOpen = content.find('[', entriesKeyPos);
    if (arrOpen == std::string::npos) return result;
    auto arrClose = findMatching(content, arrOpen, '[', ']');
    if (arrClose == std::string::npos) return result;

    size_t i = arrOpen + 1;
    while (i < arrClose) {
        while (i < arrClose &&
               (std::isspace(static_cast<unsigned char>(content[i])) || content[i] == ',')) {
            ++i;
        }
        if (i >= arrClose) break;
        if (content[i] != '{') {
            ++i;  // skip stray character
            continue;
        }

        auto objClose = findMatching(content, i, '{', '}');
        if (objClose == std::string::npos || objClose > arrClose) break;  // unmatched -- give up

        std::string obj = content.substr(i, objClose - i + 1);
        i = objClose + 1;

        bool ok = true;
        AppEntry entry;

        if (auto name = extractString(obj, "name")) {
            entry.name = *name;
        } else {
            ok = false;
        }

        if (auto exec = extractStringArray(obj, "exec")) {
            entry.execArgv = *exec;
        } else {
            ok = false;
        }

        entry.icon = extractString(obj, "icon").value_or("");
        entry.category = extractString(obj, "category").value_or("Custom");

        if (auto src = extractString(obj, "source")) {
            if (*src == "desktop") {
                entry.source = AppEntry::Source::Desktop;
            } else if (*src == "binaryscan") {
                entry.source = AppEntry::Source::BinaryScan;
            } else if (*src == "manual") {
                entry.source = AppEntry::Source::Manual;
            } else {
                entry.source = AppEntry::Source::Desktop;
                std::fprintf(stderr,
                              "wm2: warning: appcache.json: unrecognized source '%s', defaulting to desktop\n",
                              src->c_str());
            }
        } else {
            entry.source = AppEntry::Source::Desktop;
        }

        if (!ok) {
            std::fprintf(stderr, "wm2: warning: appcache.json: skipping malformed entry\n");
            continue;
        }

        result.entries.push_back(entry);
    }

    return result;
}

void write(const std::string& path, const CacheData& data) {
    ensureParentDirExists(path);

    std::ofstream out(path);
    if (!out.is_open()) {
        std::fprintf(stderr, "wm2: warning: appcache.json: could not open '%s' for writing\n",
                      path.c_str());
        return;
    }

    out << "{\"scannedAt\": " << static_cast<long long>(data.scannedAt) << ", \"entries\": [";
    for (size_t i = 0; i < data.entries.size(); ++i) {
        if (i) out << ", ";
        const AppEntry& e = data.entries[i];
        out << "{\"name\": \"" << escapeJson(e.name) << "\", \"exec\": [";
        for (size_t j = 0; j < e.execArgv.size(); ++j) {
            if (j) out << ", ";
            out << "\"" << escapeJson(e.execArgv[j]) << "\"";
        }
        out << "], \"icon\": \"" << escapeJson(e.icon)
            << "\", \"category\": \"" << escapeJson(e.category)
            << "\", \"source\": \"" << sourceToString(e.source) << "\"}";
    }
    out << "]}";
}

} // namespace AppCache
