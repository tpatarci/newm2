#include "DesktopEntry.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace {

// Helper: trim leading/trailing whitespace in-place (mirrors src/Config.cpp)
void trim(std::string& s) {
    auto a = s.find_first_not_of(" \t");
    auto b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) {
        s.clear();
    } else {
        s = s.substr(a, b - a + 1);
    }
}

// Helper: convert string to lowercase (mirrors src/Config.cpp)
std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Helper: parse a boolean string value (mirrors src/Config.cpp's parseBool
// convention: case-insensitive "true"/"1")
bool parseBool(const std::string& value) {
    std::string lower = toLower(value);
    return (lower == "true" || lower == "1");
}

// Helper: split a string on a single-character delimiter
std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> result;
    std::istringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        result.push_back(item);
    }
    return result;
}

// Helper: is `bin` executable, either directly (if it contains a '/') or
// found on $PATH.
bool findOnPath(const std::string& bin) {
    if (bin.empty()) return false;

    if (bin.find('/') != std::string::npos) {
        return access(bin.c_str(), X_OK) == 0;
    }

    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv || pathEnv[0] == '\0') return false;

    for (const auto& dir : split(pathEnv, ':')) {
        if (dir.empty()) continue;
        std::string candidate = dir + "/" + bin;
        if (access(candidate.c_str(), X_OK) == 0) return true;
    }
    return false;
}

// Helper: basename of a path with a given extension stripped, if present.
std::string basenameNoExt(const std::string& path, const std::string& ext) {
    auto slash = path.find_last_of('/');
    std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
    if (base.size() > ext.size() &&
        base.compare(base.size() - ext.size(), ext.size(), ext) == 0) {
        base = base.substr(0, base.size() - ext.size());
    }
    return base;
}

// Recognized Exec= field codes per the Desktop Entry Spec. None of these are
// expanded in this phase (Phase 7 never supplies a file/url argument) --
// each is simply stripped from the token it appears in.
bool isRecognizedFieldCode(char c) {
    static const char* recognized = "fFuUdDnNickvm";
    return std::strchr(recognized, c) != nullptr;
}

// $XDG_DATA_HOME (or $HOME/.local/share fallback), mirroring
// xdgConfigHome()'s exact HOME-fallback logic in src/Config.cpp. Internal
// helper only -- not part of the public DesktopEntry:: API surface declared
// in include/DesktopEntry.h.
std::string xdgDataHome() {
    const char* home = std::getenv("XDG_DATA_HOME");
    if (home && home[0] == '/') return home;

    const char* userHome = std::getenv("HOME");
    if (!userHome) userHome = "/tmp";
    return std::string(userHome) + "/.local/share";
}

} // namespace

namespace DesktopEntry {

std::vector<std::string> xdgDataDirs() {
    const char* dirs = std::getenv("XDG_DATA_DIRS");
    std::vector<std::string> result;

    if (dirs && dirs[0] != '\0') {
        std::istringstream ss(dirs);
        std::string dir;
        while (std::getline(ss, dir, ':')) {
            if (!dir.empty() && dir[0] == '/') {
                result.push_back(dir);
            }
        }
    }

    if (result.empty()) {
        result.push_back("/usr/local/share");
        result.push_back("/usr/share");
    }
    return result;
}

std::vector<std::string> xdgApplicationsDirs() {
    std::vector<std::string> result;
    result.push_back(xdgDataHome() + "/applications");
    for (const auto& dir : xdgDataDirs()) {
        result.push_back(dir + "/applications");
    }
    return result;
}

std::optional<std::vector<std::string>> parseExec(const std::string& execValue) {
    // Step 1: tokenize respecting Desktop Entry Spec quoting.
    std::vector<std::string> rawTokens;
    size_t i = 0;
    const size_t n = execValue.size();

    while (i < n) {
        // Skip unquoted whitespace between tokens.
        while (i < n && std::isspace(static_cast<unsigned char>(execValue[i]))) ++i;
        if (i >= n) break;

        std::string token;
        if (execValue[i] == '"') {
            size_t j = i + 1;
            bool closed = false;
            while (j < n) {
                char c = execValue[j];
                if (c == '"') {
                    closed = true;
                    ++j;
                    break;
                }
                if (c == '\\' && j + 1 < n &&
                    (execValue[j + 1] == '"' || execValue[j + 1] == '`' ||
                     execValue[j + 1] == '$' || execValue[j + 1] == '\\')) {
                    token += execValue[j + 1];
                    j += 2;
                } else {
                    token += c;
                    ++j;
                }
            }
            if (!closed) {
                // Unterminated quote -- invalid Exec value.
                return std::nullopt;
            }
            i = j;
        } else {
            size_t j = i;
            while (j < n && !std::isspace(static_cast<unsigned char>(execValue[j]))) ++j;
            token = execValue.substr(i, j - i);
            i = j;
        }
        rawTokens.push_back(token);
    }

    // Step 2: strip recognized field codes from each token; unrecognized
    // field codes invalidate the whole Exec value (T-7-01).
    std::vector<std::string> argv;
    for (const auto& token : rawTokens) {
        std::string result;
        size_t p = 0;
        while (p < token.size()) {
            if (token[p] == '%') {
                if (p + 1 >= token.size()) {
                    // Trailing bare '%' -- invalid.
                    return std::nullopt;
                }
                char code = token[p + 1];
                if (code == '%') {
                    result += '%';
                    p += 2;
                    continue;
                }
                if (isRecognizedFieldCode(code)) {
                    // Recognized field code: strip entirely (not expanded).
                    p += 2;
                    continue;
                }
                // Unrecognized field code: reject the whole Exec value.
                return std::nullopt;
            }
            result += token[p];
            ++p;
        }
        if (!result.empty()) {
            argv.push_back(result);
        }
    }

    if (argv.empty()) {
        return std::nullopt;
    }
    return argv;
}

std::optional<AppEntry> parseFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return std::nullopt;

    bool inDesktopEntrySection = false;

    std::string name;
    std::string rawExec;
    std::string icon;
    std::string categoriesRaw;
    bool noDisplay = false;
    bool hidden = false;
    std::string notShowInRaw;
    std::string tryExec;

    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        ++lineNum;

        if (line.size() > 4096) {
            std::fprintf(stderr, "wm2: warning: %s:%d: line too long, skipping\n",
                         path.c_str(), lineNum);
            continue;
        }

        auto start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;  // blank line
        if (line[start] == '#') continue;           // comment

        auto end = line.find_last_not_of(" \t\r\n");
        std::string trimmedLine = line.substr(start, end - start + 1);
        if (trimmedLine.empty()) continue;  // defense in depth

        if (trimmedLine.front() == '[') {
            inDesktopEntrySection = (trimmedLine == "[Desktop Entry]");
            continue;
        }

        if (!inDesktopEntrySection) continue;

        auto eq = trimmedLine.find('=');
        if (eq == std::string::npos) {
            std::fprintf(stderr, "wm2: warning: %s:%d: missing '='\n", path.c_str(), lineNum);
            continue;
        }

        std::string key = trimmedLine.substr(0, eq);
        std::string value = trimmedLine.substr(eq + 1);
        trim(key);
        trim(value);

        if (key == "Name")            { name = value; continue; }
        if (key == "Exec")            { rawExec = value; continue; }
        if (key == "Icon")            { icon = value; continue; }
        if (key == "Categories")      { categoriesRaw = value; continue; }
        if (key == "NoDisplay")       { noDisplay = parseBool(value); continue; }
        if (key == "Hidden")          { hidden = parseBool(value); continue; }
        if (key == "NotShowIn")       { notShowInRaw = value; continue; }
        if (key == "TryExec")         { tryExec = value; continue; }
        // Unrecognized keys (OnlyShowIn, GenericName, Comment, etc.) are
        // simply ignored -- not an error, this parser only extracts the
        // fields this phase needs.
    }

    // D-05 filtering, in order.
    if (noDisplay) return std::nullopt;
    if (hidden) return std::nullopt;

    if (!notShowInRaw.empty()) {
        for (const auto& tok : split(notShowInRaw, ';')) {
            if (tok == "wm2-born-again") return std::nullopt;
        }
    }

    if (!tryExec.empty() && !findOnPath(tryExec)) {
        return std::nullopt;
    }

    auto execArgv = parseExec(rawExec);
    if (!execArgv.has_value()) {
        return std::nullopt;
    }

    AppEntry entry;
    entry.name = name.empty() ? basenameNoExt(path, ".desktop") : name;
    entry.execArgv = execArgv.value();
    entry.icon = icon;
    if (!categoriesRaw.empty()) {
        auto cats = split(categoriesRaw, ';');
        entry.category = cats.empty() || cats[0].empty() ? "Custom" : cats[0];
    } else {
        entry.category = "Custom";
    }
    entry.source = AppEntry::Source::Desktop;

    return entry;
}

std::vector<AppEntry> scanAll() {
    std::vector<AppEntry> result;

    for (const auto& dir : xdgApplicationsDirs()) {
        DIR* d = opendir(dir.c_str());
        if (!d) continue;  // Missing directory is not an error.

        struct dirent* entry;
        while ((entry = readdir(d)) != nullptr) {
            std::string name = entry->d_name;
            static const std::string suffix = ".desktop";
            if (name.size() <= suffix.size() ||
                name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) {
                continue;
            }
            auto parsed = parseFile(dir + "/" + name);
            if (parsed.has_value()) {
                result.push_back(parsed.value());
            }
        }
        closedir(d);
    }

    return result;
}

} // namespace DesktopEntry
