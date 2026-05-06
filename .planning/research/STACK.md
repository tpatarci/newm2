# Stack Research: wm2-born-again

**Researched:** 2026-05-06
**Confidence:** HIGH (well-established ecosystem, stable specs)

## Recommended Stack

### Language: C++17

| Choice | Rationale |
|--------|-----------|
| **C++17** | Provides `std::optional`, `std::variant`, structured bindings, `if constexpr`, `std::string_view`, filesystem library. Widely supported on Ubuntu 22.04+ (gcc 11+). Not C++20 — avoids modules/concepts complexity for a project this size. |

### X11 Libraries

| Library | Version | Purpose | Rationale |
|---------|---------|---------|-----------|
| **Xlib** | libx11-dev | Core X11 protocol | Keep Xlib (not XCB) — wm2's existing Xlib code is well-understood, XCB's async model adds complexity for no benefit in a single-connection WM |
| **Xext (Shape)** | libxext-dev | Shaped window borders | Required for wm2's distinctive tab shapes. Keep as hard dependency. |
| **Xft** | libxft-dev | Font rendering | Replaces core X fonts + xvertext. Antialiasing, UTF-8, fontconfig integration. |
| **fontconfig** | libfontconfig-dev | Font discovery | Paired with Xft. Replaces hardcoded XLFD font names. |
| **Xrandr** | libxrandr-dev | Display configuration | Modern replacement for Xinerama. Handle gracefully when unavailable (VNC). |
| **Xfixes** | libxfixes-dev | Cursor visibility | Useful for hiding cursor on idle, cursor naming. Lightweight. |

**Do NOT use:**
- XCB — lower-level, more complex, wm2 doesn't need async
- Xinerama — deprecated, replaced by Xrandr
- Xt/Xmu/SM/ICE — upstream links these but doesn't use them (CONCERNS.md flags this)

### Build System

| Tool | Rationale |
|------|-----------|
| **CMake 3.16+** | Standard for C++ projects, well-supported by IDEs and package managers. Minimum version 3.16 for FetchContent improvements. |
| **pkg-config** | For discovering X11 library paths/flags. Replaces hardcoded `/usr/X11R6/` paths. |

CMakeLists.txt structure:
```
cmake_minimum_required(VERSION 3.16)
project(wm2-born-again LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(X11 REQUIRED x11)
pkg_check_modules(XFT REQUIRED xft)
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
pkg_check_modules(XRANDR REQUIRED xrandr)
pkg_check_modules(XEXT REQUIRED xext)
pkg_check_modules(XFIXES REQUIRED xfixes)
```

### Configuration File Format

| Choice | Rationale |
|--------|-----------|
| **Key=value with sections** | No external parser dependency. Simple to parse, simple to generate from GUI. Example: `font = "Sans-12"` on one line, `[colors]` section header on another. |

**Do NOT use:**
- TOML — requires tomlplusplus or similar dependency
- YAML — overkill, requires libyaml
- JSON — poor ergonomics for hand-editing config files
- INI — same as key=value but implies specific escaping rules

### Configuration GUI Toolkit

| Choice | Rationale |
|--------|-----------|
| **GTK 3** | Available on Ubuntu 22.04 (3.24). Lightweight enough for VPS. Well-documented. Runs via X forwarding over SSH. GTK4 would require more dependencies and doesn't add value for a config dialog. |

**Architecture:** Separate binary (`wm2-config`) communicating with WM via Unix domain socket (JSON protocol). Keeps GTK out of the WM's dependency tree — WM stays Xlib-only.

**Do NOT use:**
- Qt — too heavy for a VPS config tool
- GTK4 — more dependencies, not needed for simple dialogs
- FLTK — less maintained, smaller ecosystem
- Motif/Xaw — ancient, poor accessibility

### Testing

| Tool | Purpose | Rationale |
|------|---------|-----------|
| **Xvfb** | Headless X server for testing | Standard approach for X11 WM testing. CI can run full WM tests without physical display. |
| **Catch2** | C++ test framework | Header-only option, modern assertions, good test discovery. Lightweight. |
| **cmake + ctest** | Test runner | Built into CMake, integrates with CI. |

### AI Menu Discovery

| Component | Choice | Rationale |
|-----------|--------|-----------|
| **Primary source** | XDG .desktop files | Freedesktop.org standard. Provides name, executable, categories, terminal flag. Located at `$XDG_DATA_DIRS/applications/`. |
| **Secondary source** | Binary scan with AI heuristics | For minimal VPS installs that lack .desktop files. Scan `/usr/bin/`, use heuristics to identify GUI applications. |
| **Cache** | JSON file in config dir | Avoid re-scanning on every startup. Cache at `~/.config/wm2-born-again/appcache.json`. |

### Dependency Summary (apt packages)

```
Build: cmake g++ pkg-config libx11-dev libxft-dev libfontconfig-dev libxrandr-dev libxext-dev libxfixes-dev libgtk-3-dev
Runtime: libx11-6 libxft2 libfontconfig1 libxrandr2 libxext6 libxfixes3 libgtk-3-0
```

Minimal runtime footprint — suitable for 512MB VPS.
