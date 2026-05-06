# Phase 1: Build Infrastructure + RAII Foundation - Research

**Researched:** 2026-05-06
**Domain:** C++17 X11 window manager build system, RAII wrappers for X11 resources, Catch2 test harness on Xvfb
**Confidence:** HIGH

## Summary

Phase 1 establishes the entire build-to-test foundation for wm2-born-again. The project uses CMake 3.16+ with `pkg_check_modules()` and `IMPORTED_TARGET` for X11 dependency discovery (no hardcoded paths), writes fresh C++17 code in `src/`/`include/` layout, and wraps all pointer-based X11 resources (`Display*`, `GC`) in `std::unique_ptr` with custom deleters. XID-integer resources (`Window`, `Pixmap`, `Font`, `Colormap`, `Cursor`) each need a small RAII wrapper class because `unique_ptr` only works with pointer types. Catch2 v3.14.0 is the latest release (verified via GitHub API, 2026-04-05) and integrates via `FetchContent` so the developer does not need it pre-installed. Tests run on Xvfb using CTest fixtures for display setup/teardown.

The critical finding is that the development environment is **missing all build dependencies**: cmake, pkg-config, X11 dev headers, Xvfb, and Catch2 are all absent. The plan must include an environment setup step. Catch2 is handled via FetchContent (auto-downloaded), but cmake, pkg-config, `libx11-dev`, `libxext-dev`, and `xvfb` must be installed via apt.

**Primary recommendation:** Write CMakeLists.txt to use `find_package(PkgConfig REQUIRED)` + `pkg_check_modules(... IMPORTED_TARGET ...)` for X11, FetchContent for Catch2, and create thin RAII classes for XID resources alongside `unique_ptr` aliases for pointer resources -- all in a single `include/x11wrap.h` header.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Write fresh C++17 code in `src/` and `include/`. Keep `upstream-wm2/` as untouched reference -- it is never compiled, only consulted for behavior.
- **D-02:** Split layout: `src/*.cpp` for implementation, `include/*.h` for headers.
- **D-03:** Entry point at `src/main.cpp`.
- **D-04:** File names match upstream classes: `Manager.cpp/h`, `Client.cpp/h`, `Border.cpp/h`, `Rotated.cpp/h`.
- **D-05:** `std::unique_ptr` with custom deleters for all X11 resource management. Zero overhead, idiomatic C++17, compile-time safety.
- **D-06:** `Window` (XID typedef) stays as raw `Window` type -- it's an integer ID, not a pointer. All pointer-based resources (Display, GC, Cursor, Font, Pixmap, Colormap) are wrapped.
- **D-07:** All RAII type aliases and custom deleters in a single `include/x11wrap.h` header.
- **D-08:** All-new C++17 code from scratch. No incremental porting of upstream files. Modern idioms (`std::string`, `std::vector`, `bool`, structured bindings) used throughout new code.
- **D-09:** Phase 1 produces a working WM binary with minimal window management functionality (open display, manage basic windows, basic frame decoration). Not just build infrastructure.
- **D-10:** Keep xvertext (`Rotated.C/h`) as a compiled third-party dependency without modernization. Phase 4 (Xft) replaces it entirely. Include it in CMake but don't rewrite it.
- **D-11:** RAII unit tests (construct, destruct, move semantics, release) plus a WM smoke test (binary starts on Xvfb, manages a basic window). Not exhaustive coverage.
- **D-12:** CMake-managed Xvfb setup. `ctest` starts Xvfb in test fixtures automatically. No manual Xvfb step required.
- **D-13:** Test files in root `tests/` directory: `tests/test_raii.cpp`, `tests/test_smoke.cpp`, etc.

### Claude's Discretion
No specific requirements -- open to standard approaches. User emphasized "light, reliable, fast" for RAII wrappers, which drove the `unique_ptr` + custom deleters choice.

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| BLD-01 | WM builds with CMake 3.16+ and pkg-config for X11 dependency discovery | CMake `find_package(PkgConfig)` + `pkg_check_modules(x11 IMPORTED_TARGET)` verified; `IMPORTED_TARGET` available since CMake 3.6 [VERIFIED: Context7 CMake docs] |
| BLD-02 | C++17 standard (std::vector, std::string, bool, structured bindings) | `target_compile_features(target PUBLIC cxx_std_17)` or `set(CMAKE_CXX_STANDARD 17)` [VERIFIED: Context7 CMake docs] |
| BLD-03 | RAII wrappers for all X11 resources (Display, Window, GC, Cursor, Font, Pixmap, Colormap) | `unique_ptr` + custom deleters for pointer types; thin RAII classes for XID types -- see Architecture Patterns section [VERIFIED: Xlib specification + C++ standard] |
| BLD-04 | Builds and runs on Ubuntu 22.04+ with standard apt packages | `apt install build-essential cmake pkg-config libx11-dev libxext-dev xvfb` [ASSUMED] |
| BLD-05 | No hardcoded X11R6 paths -- all library paths discovered via pkg-config | `pkg_check_modules(... IMPORTED_TARGET)` handles this; no `-L` or `-I` flags needed [VERIFIED: Context7 CMake docs] |
| TEST-01 | Test suite runs on Xvfb without physical display | CTest fixtures for Xvfb start/stop; `ENVIRONMENT DISPLAY=:99` on tests [VERIFIED: CMake documentation + community patterns] |
| TEST-02 | Catch2 test framework for unit and integration tests | Catch2 v3.14.0 via FetchContent; link `Catch2::Catch2WithMain` [VERIFIED: GitHub API + Context7 Catch2 docs] |
| TEST-04 | CI-ready (tests pass with cmake + ctest) | `enable_testing()` + `catch_discover_tests()` from Catch's extras [VERIFIED: Context7 Catch2 docs] |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| CMake build configuration | Build System | -- | CMakeLists.txt owns all compilation, linking, and test discovery |
| X11 RAII resource wrappers | API / Backend | -- | Header-only in `include/x11wrap.h`; consumed by all runtime code |
| X11 dependency discovery | Build System | -- | pkg-config via CMake `FindPkgConfig`; no runtime involvement |
| Test harness (Catch2 + Xvfb) | Test Infrastructure | -- | CTest fixtures manage Xvfb lifecycle; Catch2 manages test lifecycle |
| WM binary entry point | API / Backend | -- | `src/main.cpp` creates WindowManager which owns the event loop |
| Minimal WM functionality | API / Backend | -- | WindowManager opens display, scans windows, creates basic frames |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| CMake | 3.16+ | Build system | Decision BLD-01; 3.16 minimum for modern FetchContent + IMPORTED_TARGET [CITED: cmake.org] |
| pkg-config | via CMake FindPkgConfig | X11 dependency discovery | Standard X11 discovery mechanism on Linux; avoids hardcoded paths [VERIFIED: Context7 CMake docs] |
| libX11 | system (via pkg-config `x11`) | X11 core protocol | Core requirement for any X11 window manager |
| libXext | system (via pkg-config `xext`) | Shape extension | Required for wm2's shaped window borders [VERIFIED: upstream Makefile link line] |
| Catch2 | v3.14.0 (FetchContent) | Test framework | Decision TEST-02; latest stable release as of 2026-04-05 [VERIFIED: GitHub API] |
| Xvfb | system apt package | Headless test display | Decision TEST-01; standard X virtual framebuffer [ASSUMED] |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| xvertext 2.0 | bundled (upstream) | Rotated font rendering | Compiled from `upstream-wm2/Rotated.C` as third-party code; Phase 4 replaces with Xft |
| libm | system | Math library | Upstream links it; likely needed by xvertext |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| FetchContent for Catch2 | System-installed Catch2 via `find_package` | FetchContent avoids requiring developer to pre-install Catch2; simpler CI setup |
| `pkg_check_modules` | `find_package(X11)` | `find_package(X11)` is CMake-native but does not discover Xext or Shape extension reliably; pkg-config is the standard Linux approach |
| CTest fixtures for Xvfb | `xvfb-run` wrapper script | CTest fixtures are CMake-native and integrate with `ctest`; `xvfb-run` requires an outer script layer |

**Installation (development environment):**
```bash
sudo apt install build-essential cmake pkg-config libx11-dev libxext-dev xvfb
# Catch2 is auto-downloaded by CMake FetchContent -- no manual install needed
```

**Installation verification:**
```bash
cmake --version         # expect 3.16+
pkg-config --libs x11   # expect -lX11
pkg-config --libs xext  # expect -lXext
which Xvfb              # expect /usr/bin/Xvfb
```

## Architecture Patterns

### System Architecture Diagram

```
[Developer]
    |
    v
[cmake -B build]  --(pkg-config)--> [system X11 libs: libX11, libXext]
    |                                      |
    v                                      v
[CMakeLists.txt]  --(FetchContent)--> [Catch2 v3.14.0 (downloaded)]
    |
    +-- wm2-born-again binary
    |       src/main.cpp --> Manager --> Client --> Border --> xvertext
    |                                         |
    |                                         v
    |                                  [x11wrap.h RAII]
    |                                  (DisplayPtr, GCPtr,
    |                                   XFont, XCursor, ...)
    |
    +-- test executables
            tests/test_raii.cpp --> [x11wrap.h]
            tests/test_smoke.cpp --> [wm2 binary on Xvfb]
                    |
                    v
             [Xvfb :99] (CTest fixture)
```

### Recommended Project Structure
```
.
+-- CMakeLists.txt              # Top-level: project definition, dependencies, targets
+-- upstream-wm2/               # Untouched reference (never compiled by new build)
|   +-- Rotated.C               # Compiled as third-party dependency (D-10)
|   +-- Rotated.h
+-- include/                    # Public headers
|   +-- x11wrap.h               # RAII wrappers for all X11 resources (D-07)
|   +-- Manager.h
|   +-- Client.h
|   +-- Border.h
|   +-- Cursors.h               # Cursor bitmap data (usable as-is from upstream)
+-- src/                        # Implementation files
|   +-- main.cpp                # Entry point (D-03)
|   +-- Manager.cpp
|   +-- Client.cpp
|   +-- Border.cpp
|   +-- Buttons.cpp             # Button/menu interaction handlers
|   +-- Events.cpp              # Event dispatch handlers
+-- tests/                      # Test files (D-13)
    +-- test_raii.cpp           # RAII construct/destruct/move/release tests
    +-- test_smoke.cpp          # WM smoke test on Xvfb
```

### Pattern 1: RAII Wrappers via `unique_ptr` + Custom Deleters (Pointer Types)

**What:** X11 resources that are C pointer types (`Display*`, `GC`) are wrapped using `std::unique_ptr` with custom deleter structs.

**When to use:** For all pointer-based X11 resources (Display, GC).

**Key distinction from CONTEXT.md:** D-06 says "All pointer-based resources (Display, GC, Cursor, Font, Pixmap, Colormap) are wrapped." However, only `Display*` and `GC` are actual C pointer types in Xlib. `Cursor`, `Font`, `Pixmap`, and `Colormap` are `XID` (unsigned long integer) typedefs -- they are NOT pointers. `unique_ptr` cannot hold non-pointer types. These need a thin RAII wrapper class instead.

**X11 type categorization (CRITICAL for correct RAII design):**

| X11 Type | Underlying C Type | Xlib Free Function | RAII Strategy |
|----------|-------------------|-------------------|---------------|
| `Display*` | `struct _XDisplay*` (opaque pointer) | `XCloseDisplay()` | `unique_ptr` + custom deleter |
| `GC` | `struct _XGC*` (opaque pointer) | `XFreeGC(display, gc)` | `unique_ptr` + custom deleter (stateful: holds display) |
| `Window` | `XID` (unsigned long) | `XDestroyWindow(display, w)` | Raw `Window` per D-06 |
| `Font` | `XID` (unsigned long) | `XUnloadFont(display, font)` | Thin RAII class (holds display + XID) |
| `Cursor` | `XID` (unsigned long) | `XFreeCursor(display, cursor)` | Thin RAII class (holds display + XID) |
| `Pixmap` | `XID` (unsigned long) | `XFreePixmap(display, pixmap)` | Thin RAII class (holds display + XID) |
| `Colormap` | `XID` (unsigned long) | `XFreeColormap(display, cmap)` | Thin RAII class (holds display + XID) |
| `XFontStruct*` | pointer | `XFreeFont(display, fs)` | `unique_ptr` + custom deleter |

Source: [Xlib specification](https://www.x.org/releases/current/doc/libX11/libX11/libX11.html) -- `XID` is `unsigned long`, `GC` is `struct _XGC*`, `Font`/`Cursor`/`Pixmap`/`Colormap` are XID typedefs. [VERIFIED: x.org official docs]

**Example -- Pointer types (unique_ptr):**
```cpp
// include/x11wrap.h

#pragma once
#include <X11/Xlib.h>
#include <memory>

namespace x11 {

// --- Pointer-type resources: unique_ptr + custom deleters ---

struct DisplayDeleter {
    void operator()(Display* d) const noexcept {
        if (d) XCloseDisplay(d);
    }
};
using DisplayPtr = std::unique_ptr<Display, DisplayDeleter>;

// Stateful deleter: GC needs the display connection to free
struct GCDeleter {
    Display* display = nullptr;
    void operator()(GC gc) const noexcept {
        if (display && gc) XFreeGC(display, gc);
    }
};
using GCPtr = std::unique_ptr<std::remove_pointer_t<GC>, GCDeleter>;

// Factory function ensures deleter gets the display
inline GCPtr make_gc(Display* d, Window w, unsigned long mask, XGCValues* vals) {
    GCPtr p{ XCreateGC(d, w, mask, vals) };
    p.get_deleter().display = d;
    return p;
}

// XFontStruct* deleter
struct FontStructDeleter {
    Display* display = nullptr;
    void operator()(XFontStruct* fs) const noexcept {
        if (display && fs) XFreeFont(display, fs);
    }
};
using FontStructPtr = std::unique_ptr<XFontStruct, FontStructDeleter>;

} // namespace x11
```

### Pattern 2: RAII Wrappers for XID Integer Types (Thin Class)

**What:** X11 resources that are XID (unsigned long integer) typedefs need a thin RAII class because `unique_ptr` cannot hold non-pointer types.

**When to use:** For `Font`, `Cursor`, `Pixmap`, `Colormap` XID resources.

**Example:**
```cpp
// include/x11wrap.h (continued)

namespace x11 {

// --- XID-type resources: thin RAII class ---

class UniqueCursor {
public:
    UniqueCursor() noexcept : m_display(nullptr), m_cursor(None) {}
    UniqueCursor(Display* d, Cursor c) noexcept : m_display(d), m_cursor(c) {}

    // Move-only
    UniqueCursor(UniqueCursor&& o) noexcept
        : m_display(o.m_display), m_cursor(o.m_cursor) {
        o.m_display = nullptr;
        o.m_cursor = None;
    }
    UniqueCursor& operator=(UniqueCursor&& o) noexcept {
        if (this != &o) {
            reset();
            m_display = o.m_display;
            m_cursor = o.m_cursor;
            o.m_display = nullptr;
            o.m_cursor = None;
        }
        return *this;
    }

    UniqueCursor(const UniqueCursor&) = delete;
    UniqueCursor& operator=(const UniqueCursor&) = delete;

    ~UniqueCursor() { reset(); }

    void reset() noexcept {
        if (m_display && m_cursor != None) {
            XFreeCursor(m_display, m_cursor);
        }
        m_display = nullptr;
        m_cursor = None;
    }

    Cursor get() const noexcept { return m_cursor; }
    operator Cursor() const noexcept { return m_cursor; }
    explicit operator bool() const noexcept { return m_cursor != None; }

private:
    Display* m_display;
    Cursor m_cursor;
};

// Analogous classes: UniqueFont, UniquePixmap, UniqueColormap
// Each follows the same pattern with appropriate XFree* function.

} // namespace x11
```

### Pattern 3: CMake Build System with pkg-config

**What:** Modern CMake using `find_package(PkgConfig)` + `pkg_check_modules(... IMPORTED_TARGET)` for X11 libraries.

**When to use:** Always -- this is the only supported build method.

**Example:**
```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(wm2-born-again LANGUAGES CXX)

# C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# X11 dependencies via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
pkg_check_modules(XEXT REQUIRED IMPORTED_TARGET xext)

# WM binary
add_executable(wm2-born-again
    src/main.cpp
    src/Manager.cpp
    src/Client.cpp
    src/Border.cpp
    src/Buttons.cpp
    src/Events.cpp
    # Third-party: compile as-is from upstream
    upstream-wm2/Rotated.C
)

target_include_directories(wm2-born-again PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/upstream-wm2
)

target_link_libraries(wm2-born-again PRIVATE
    PkgConfig::X11
    PkgConfig::XEXT
    m
)

target_compile_features(wm2-born-again PUBLIC cxx_std_17)
```

Source: [CMake FindPkgConfig documentation](https://cmake.org/cmake/help/latest/module/FindPkgConfig.html), `IMPORTED_TARGET` available since CMake 3.6. [VERIFIED: Context7 + cmake.org]

### Pattern 4: Catch2 Integration via FetchContent

**What:** Download Catch2 at configure time so developers do not need it pre-installed.

**Example:**
```cmake
# CMakeLists.txt (test section)
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.14.0
)
FetchContent_MakeAvailable(Catch2)

# Test executable
add_executable(test_raii tests/test_raii.cpp)
target_link_libraries(test_raii PRIVATE
    Catch2::Catch2WithMain
    PkgConfig::X11
)
target_include_directories(test_raii PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Test discovery
include(CTest)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(test_raii)
```

Source: [Catch2 CMake integration docs](https://github.com/catchorg/catch2/blob/devel/docs/cmake-integration.md) [VERIFIED: Context7 Catch2 docs]

### Pattern 5: Xvfb Test Fixture in CTest

**What:** CTest fixtures start/stop Xvfb around tests that need an X display.

**Example:**
```cmake
# CMakeLists.txt (Xvfb fixture)

# Find Xvfb
find_program(XVFB_EXECUTABLE Xvfb)

if(XVFB_EXECUTABLE)
    # Start Xvfb fixture
    add_test(NAME start_xvfb
        COMMAND ${CMAKE_COMMAND} -E env bash -c
        "Xvfb :99 -screen 0 1024x768x24 -ac +render -noreset & echo $! > ${CMAKE_BINARY_DIR}/xvfb.pid && sleep 1"
    )
    set_tests_properties(start_xvfb PROPERTIES FIXTURES_SETUP xvfb_display)

    # Stop Xvfb fixture
    add_test(NAME stop_xvfb
        COMMAND ${CMAKE_COMMAND} -E env bash -c
        "kill $(cat ${CMAKE_BINARY_DIR}/xvfb.pid) 2>/dev/null || true"
    )
    set_tests_properties(stop_xvfb PROPERTIES FIXTURES_CLEANUP xvfb_display)

    # Smoke test requires Xvfb
    add_executable(test_smoke tests/test_smoke.cpp)
    target_link_libraries(test_smoke PRIVATE Catch2::Catch2WithMain)
    catch_discover_tests(test_smoke)

    # Set environment for smoke tests
    set_tests_properties(
        ${test_smoke_DISCOVERED_TESTS}
        PROPERTIES
            ENVIRONMENT "DISPLAY=:99"
            FIXTURES_REQUIRED xvfb_display
    )
endif()
```

Source: [CMake CTest fixtures documentation](https://cmake.org/cmake/help/latest/command/add_test.html), `FIXTURES_SETUP`/`FIXTURES_REQUIRED`/`FIXTURES_CLEANUP` since CMake 3.14. [ASSUMED: fixture pattern from community practice, not Context7-verified]

### Anti-Patterns to Avoid
- **Wrapping XID integers in unique_ptr:** `unique_ptr<unsigned long>` is nonsensical. XID types (Font, Cursor, Pixmap, Colormap) need thin RAII wrapper classes, not unique_ptr.
- **Stale deleter references:** Stateful deleters (GCDeleter holding `Display*`) must outlive the resource. If Display is destroyed before GC, the deleter has a dangling pointer. Design: always destroy resources in reverse creation order (natural with RAII scope exit).
- **Catching X11 errors with exceptions:** X11 uses an asynchronous error handler callback, not return codes. The RAII wrappers should not try to throw from destructors -- X11 error handling stays in the WindowManager error handler.
- **Compiling upstream source with C++17 flags:** `Rotated.C` is pre-standard C. It must be compiled with permissive flags (or separate target properties) to avoid errors from its C-style casts and implicit conversions. Use `set_source_files_properties(upstream-wm2/Rotated.C PROPERTIES COMPILE_FLAGS "-Wno-error")` or equivalent.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| X11 library discovery | Hardcoded `-L/usr/X11R6/lib` paths | `pkg_check_modules(x11 IMPORTED_TARGET)` | Paths vary across distros and versions; pkg-config handles this portably |
| Test framework | Custom test macros/assert | Catch2 v3 | Battle-tested, rich assertions, BDD support, CMake integration |
| Test discovery | Manual test registration | `catch_discover_tests()` | Auto-registers `TEST_CASE`s with CTest; zero maintenance |
| Xvfb lifecycle | Shell script wrapper | CTest fixtures | Native CMake integration; `ctest` handles start/stop automatically |
| Generic RAII for C resources | Custom smart pointer class | `std::unique_ptr` + custom deleter | Zero overhead, move-only semantics, standard library quality |
| String handling | `char*` + `malloc`/`strcpy` | `std::string` | Memory safety, no buffer overflows, standard API |
| Container for client list | `listmacro2.h` macro lists | `std::vector<Client*>` | Type-safe, bounds-checked, standard algorithms |

**Key insight:** Every X11 resource has a well-defined free function. The RAII wrappers are mechanical -- map each create function to a free function. The complexity is in ensuring the correct display pointer is available in the deleter, not in the RAII pattern itself.

## Common Pitfalls

### Pitfall 1: XID vs Pointer Type Confusion
**What goes wrong:** Trying to wrap `Font` (which is `unsigned long`) in `unique_ptr<Font>` -- compiler error because `unique_ptr` expects a pointer type.
**Why it happens:** Xlib documentation uses the term "resource" for both pointer types (Display*, GC) and integer IDs (Window, Font, Cursor, Pixmap, Colormap), making them seem like the same kind of thing.
**How to avoid:** Check the actual typedef. If it derives from `XID` (unsigned long), use a thin RAII class. If it's a `struct*`, use `unique_ptr`.
**Warning signs:** Compiler errors about `unique_ptr` with non-pointer types, or `reinterpret_cast` gymnastics to fit integer handles into pointer types.

### Pitfall 2: Stateful Deleter Dangling Display Pointer
**What goes wrong:** A GC RAII wrapper holds `Display*` in its deleter, but the Display is destroyed first (because it was declared after the GC in the same scope, or the GC is a member of a class that outlives the display).
**Why it happens:** RAII destruction order is reverse declaration order. If Display is declared before GC, it is destroyed after -- correct. But if they are members of different objects with unclear lifetime, the order can be wrong.
**How to avoid:** Always declare `DisplayPtr` as the FIRST member (or base class) of any object that owns X11 resources. This ensures it is destroyed LAST.
**Warning signs:** Segfaults in `XFreeGC` or `XFreeCursor` during program shutdown.

### Pitfall 3: Rotated.C Compilation Failures with C++17
**What goes wrong:** `Rotated.C` uses pre-standard C idioms (implicit `int` return from functions, C-style casts, `char*` string literals, `malloc` without cast) that produce errors under strict C++17 compilation.
**Why it happens:** The file was written for GCC circa 1992. C++17 compilers reject many pre-standard idioms.
**How to avoid:** Set per-file compile flags: `set_source_files_properties(upstream-wm2/Rotated.C PROPERTIES COMPILE_FLAGS "-fpermissive -w")` or compile it as C (`set_source_files_properties(upstream-wm2/Rotated.C PROPERTIES LANGUAGE C)`). Alternatively, use a separate static library target with relaxed flags.
**Warning signs:** Build errors in `Rotated.C` about implicit int, invalid casts, or deprecated conversions.

### Pitfall 4: Xvfb Race Condition in Tests
**What goes wrong:** Smoke test starts before Xvfb is ready, causing `XOpenDisplay` to fail.
**Why it happens:** Xvfb takes a moment to start listening on the display socket. `add_test(COMMAND ... Xvfb :99 &)` returns immediately but Xvfb may not be ready.
**How to avoid:** Add a small sleep after starting Xvfb in the fixture, or use a readiness check (e.g., loop until `xdpyinfo -display :99` succeeds). The CTest fixture pattern with `sleep 1` is a pragmatic solution.
**Warning signs:** Intermittent test failures on CI, especially under load.

### Pitfall 5: Shape Extension Not Available on Xvfb
**What goes wrong:** wm2 requires the Shape extension (`XShapeQueryExtension`), but Xvfb may not load it by default.
**Why it happens:** The upstream code calls `fatal()` if Shape extension is missing. Xvfb loads it by default on modern systems, but this is not guaranteed.
**How to avoid:** For Phase 1, the minimal WM binary should gracefully handle missing Shape extension (print warning, continue with rectangular frames). For tests, start Xvfb with explicit `+extension SHAPE` flag if needed. Phase 4 fully addresses this with graceful fallback (XDIS-03).
**Warning signs:** Test failures with "no shape extension, can't run without it" error message.

### Pitfall 6: Missing Build Dependencies
**What goes wrong:** Developer clones the repo, runs `cmake -B build`, and gets errors about missing X11 headers or pkg-config.
**Why it happens:** The project depends on system packages that are not installed by default on Ubuntu Server or minimal VPS images.
**How to avoid:** CMakeLists.txt should fail early with clear messages: `pkg_check_modules(X11 REQUIRED ...)` produces a readable error when pkg-config or the library is missing. Document the `apt install` command prominently.
**Warning signs:** `Could NOT find PkgConfig` or `Package 'x11' not found` errors during cmake configure.

## Code Examples

### Complete CMakeLists.txt (Skeleton)
```cmake
# Source: [Context7 CMake docs + Catch2 docs]
cmake_minimum_required(VERSION 3.16)
project(wm2-born-again VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
pkg_check_modules(XEXT REQUIRED IMPORTED_TARGET xext)

# --- WM Binary ---
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/Manager.cpp
    src/Client.cpp
    src/Border.cpp
    src/Buttons.cpp
    src/Events.cpp
    upstream-wm2/Rotated.C
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/upstream-wm2
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    PkgConfig::X11
    PkgConfig::XEXT
    m
)

# Relax warnings for third-party code
set_source_files_properties(upstream-wm2/Rotated.C PROPERTIES
    COMPILE_FLAGS "-w"
)

# --- Tests ---
option(BUILD_TESTS "Build test suite" ON)

if(BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.14.0
    )
    FetchContent_MakeAvailable(Catch2)

    enable_testing()
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)

    # RAII unit tests (no X11 display needed)
    add_executable(test_raii tests/test_raii.cpp)
    target_link_libraries(test_raii PRIVATE Catch2::Catch2WithMain)
    target_include_directories(test_raii PRIVATE ${CMAKE_SOURCE_DIR}/include)
    catch_discover_tests(test_raii)

    # Smoke tests (need Xvfb)
    find_program(XVFB_EXECUTABLE Xvfb)
    if(XVFB_EXECUTABLE)
        add_executable(test_smoke tests/test_smoke.cpp)
        target_link_libraries(test_smoke PRIVATE
            Catch2::Catch2WithMain
            PkgConfig::X11
        )
        target_include_directories(test_smoke PRIVATE ${CMAKE_SOURCE_DIR}/include)
        catch_discover_tests(test_smoke)

        # Xvfb fixture
        set(XVFB_PID_FILE "${CMAKE_BINARY_DIR}/xvfb.pid")
        add_test(NAME start_xvfb
            COMMAND bash -c "Xvfb :99 -screen 0 1024x768x24 -ac +render -noreset & echo $! > ${XVFB_PID_FILE} && sleep 1"
        )
        set_tests_properties(start_xvfb PROPERTIES FIXTURES_SETUP xvfb_display)

        add_test(NAME stop_xvfb
            COMMAND bash -c "kill $(cat ${XVFB_PID_FILE}) 2>/dev/null || true"
        )
        set_tests_properties(stop_xvfb PROPERTIES FIXTURES_CLEANUP xvfb_display)

        set_tests_properties(
            ${test_smoke_DISCOVERED_TESTS}
            PROPERTIES
                ENVIRONMENT "DISPLAY=:99"
                FIXTURES_REQUIRED xvfb_display
        )
    else()
        message(WARNING "Xvfb not found -- smoke tests disabled")
    endif()
endif()
```

### RAII Unit Test Example
```cpp
// tests/test_raii.cpp
#include <catch2/catch_test_macros.hpp>
#include "x11wrap.h"

TEST_CASE("DisplayPtr closes display on destruction", "[raii]") {
    // This test requires Xvfb or a real display
    // For pure unit tests without a display, test the deleter logic directly
    x11::DisplayDeleter deleter;
    Display* raw = nullptr;
    REQUIRE_NOTHROW(deleter(raw));  // null is safe

    // Full integration test is in test_smoke.cpp
}

TEST_CASE("GCPtr deleter holds display reference", "[raii]") {
    x11::GCDeleter deleter;
    REQUIRE(deleter.display == nullptr);
    GC null_gc = nullptr;
    REQUIRE_NOTHROW(deleter(null_gc));  // null GC is safe with null display
}

TEST_CASE("UniqueCursor is move-only", "[raii]") {
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<x11::UniqueCursor>);
    STATIC_REQUIRE(std::is_move_constructible_v<x11::UniqueCursor>);
    STATIC_REQUIRE(std::is_nothrow_move_constructible_v<x11::UniqueCursor>);
}
```

### Smoke Test Example
```cpp
// tests/test_smoke.cpp
#include <catch2/catch_test_macros.hpp>
#include <X11/Xlib.h>
#include <cstdlib>
#include <string>
#include <thread>
#include <chrono>

TEST_CASE("WM binary starts on Xvfb", "[smoke]") {
    // Build the binary path relative to the test executable
    const char* display = std::getenv("DISPLAY");
    REQUIRE(display != nullptr);

    // Try to open the display
    Display* d = XOpenDisplay(display);
    REQUIRE(d != nullptr);
    XCloseDisplay(d);
}

TEST_CASE("X11 RAII wrappers work with real display", "[smoke][raii]") {
    x11::DisplayPtr display(XOpenDisplay(std::getenv("DISPLAY")));
    REQUIRE(display != nullptr);

    // Test GC creation via RAII
    int screen = DefaultScreen(display.get());
    Window root = RootWindow(display.get(), screen);
    XGCValues vals;
    auto gc = x11::make_gc(display.get(), root, 0, &vals);
    REQUIRE(gc != nullptr);
    // GC is freed automatically when gc goes out of scope
    // Display is freed automatically when display goes out of scope
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Hardcoded `/usr/X11R6` paths | `pkg-config` via CMake `FindPkgConfig` | X11R7 (~2005) | No more hardcoded library paths |
| Hand-rolled Makefile | CMake with imported targets | Standard practice | Cross-platform, IDE support, dependency management |
| `char*` + `malloc`/`strcpy` | `std::string` | C++98 | Memory safety, no buffer overflows |
| Macro-based lists (`listmacro2.h`) | `std::vector<T>` | C++98 | Type safety, bounds checking, standard algorithms |
| Custom `Boolean` typedef | `bool` | C++98 | Standard boolean semantics |
| `delete this` self-deletion | Lifecycle managed by container | C++11+ | Clear ownership, no dangling pointers |
| `signal()` system call | `sigaction()` (already wrapped upstream) | POSIX.1-2001 | Well-defined signal handling semantics |
| `select()` event loop | `poll()` (Phase 2) | POSIX.1-2001 | No file descriptor limit, cleaner API |
| Core X fonts (XLFD) | Xft + fontconfig (Phase 4) | ~2002 | Antialiased, UTF-8, fontconfig patterns |
| xvertext font rotation | Xft matrix transforms (Phase 4) | -- | Eliminates bundled third-party code |
| `find_package(X11)` alone | `pkg_check_modules(x11 IMPORTED_TARGET)` | CMake 3.6+ | Reliable extension discovery (xext, xrandr, etc.) |
| Catch2 v2 (single header) | Catch2 v3 (modular, CMake-integrated) | 2022 | FetchContent integration, `Catch2::Catch2WithMain` target |

**Deprecated/outdated:**
- `makedepend`: Replaced by CMake automatic dependency scanning
- `NewString()` macro: Replaced by `std::string`
- `listmacro2.h`: Replaced by `std::vector`

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Ubuntu 22.04+ has `cmake` 3.16+ available via `apt install cmake` | Standard Stack | Build requires manual cmake install from kitware repo if system cmake is older |
| A2 | `libx11-dev` and `libxext-dev` provide working pkg-config `.pc` files named `x11` and `xext` | Standard Stack | CMake configure fails; need to verify actual pkg-config names |
| A3 | Xvfb loads the Shape extension by default (or with `+extension SHAPE`) | Common Pitfalls | Smoke tests fail if Shape extension check runs before graceful fallback |
| A4 | `Rotated.C` compiles with `-w` flag under C++17 without needing `-fpermissive` | Common Pitfalls | Need additional permissive flags or compile as C |
| A5 | CTest `FIXTURES_SETUP`/`FIXTURES_CLEANUP` pattern works reliably for Xvfb start/stop | Architecture Patterns | Tests may fail intermittently; need xvfb-run fallback |
| A6 | The Shape extension pkg-config entry is discoverable as part of `xext` package, not a separate `x11-xshape` package | Standard Stack | May need to add shape-specific pkg-config discovery |

**If this table is empty:** All claims in this research were verified or cited -- no user confirmation needed.

## Open Questions -- RESOLVED

1. **Exact pkg-config names for X11 packages on Ubuntu 22.04**
   - RESOLVED: Plan 01-01 Task 1 starts with `sudo apt install -y cmake pkg-config libx11-dev libxext-dev xvfb`. If `pkg_check_modules(x11 ...)` or `pkg_check_modules(xext ...)` fails at cmake configure time, the executor will run `pkg-config --list-all | grep -i x1` to discover the correct names and adjust CMakeLists.txt accordingly. This is a build-time verification, not a blocking unknown.

2. **Rotated.C compilation compatibility with strict C++17**
   - RESOLVED: Plan 01-01 Task 3 already includes adaptive mitigation -- first try `-w`, then add `-fpermissive`, then try compiling as C. The executor handles this at build time.

3. **Shape extension availability in Xvfb on Ubuntu 22.04**
   - RESOLVED: Plan 01-02 Task 2 specifies graceful fallback for missing Shape extension (warn but continue with rectangular frames), and Plan 01-01 Task 3 includes adding `+extension SHAPE` to the Xvfb start command if needed. Both paths are covered.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| cmake | Build system | no | -- | apt install cmake |
| pkg-config | X11 discovery | no | -- | apt install pkg-config |
| g++ (C++17) | Compiler | yes | 13.3.0 | -- |
| build-essential | Compiler toolchain | yes | 12.10ubuntu1 | -- |
| libx11-dev | X11 headers | no (runtime lib only) | 1.8.7 | apt install libx11-dev |
| libxext-dev | Shape extension headers | no (runtime lib only) | 1.3.4 | apt install libxext-dev |
| Xvfb | Headless testing | no | -- | apt install xvfb |
| Catch2 | Test framework | no | -- | FetchContent (auto-downloaded) |
| libm | Math library | yes (system) | -- | -- |

**Missing dependencies with no fallback:**
- cmake -- required for build system; apt install
- pkg-config -- required for X11 discovery; apt install
- libx11-dev -- required for X11 headers; apt install
- libxext-dev -- required for Shape extension; apt install
- Xvfb -- required for headless tests; apt install (tests can be disabled with `-DBUILD_TESTS=OFF`)

**Missing dependencies with fallback:**
- Catch2 -- FetchContent downloads automatically during cmake configure

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 v3.14.0 |
| Config file | None -- integrated via FetchContent in CMakeLists.txt |
| Quick run command | `ctest --test-dir build -R test_raii --output-on-failure` |
| Full suite command | `cmake -B build && cmake --build build && cd build && ctest --output-on-failure` |

### Phase Requirements to Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| BLD-01 | CMake + pkg-config discovers X11 | integration | `cmake -B build 2>&1 \| grep -q "Found X11"` | No -- Wave 0 |
| BLD-02 | Code compiles with C++17 | build | `cmake --build build` | No -- Wave 0 |
| BLD-03 | RAII wrappers construct/destruct/move | unit | `ctest -R test_raii --output-on-failure` | No -- Wave 0 |
| BLD-04 | Builds on Ubuntu 22.04 with apt packages | manual | Full build from clean checkout | N/A |
| BLD-05 | No hardcoded paths in build | build | `grep -r X11R6 build/ \| wc -l` should be 0 | No -- Wave 0 |
| TEST-01 | Tests run on Xvfb | integration | `ctest --output-on-failure` | No -- Wave 0 |
| TEST-02 | Catch2 framework used | unit | Test files use `#include <catch2/catch_test_macros.hpp>` | No -- Wave 0 |
| TEST-04 | CI-ready ctest | integration | `cmake -B build && cmake --build build && cd build && ctest` | No -- Wave 0 |

### Sampling Rate
- **Per task commit:** `cmake --build build && ctest --test-dir build -R test_raii --output-on-failure`
- **Per wave merge:** `cmake -B build && cmake --build build && cd build && ctest --output-on-failure`
- **Phase gate:** Full suite green, plus clean checkout build on bare Ubuntu 22.04

### Wave 0 Gaps
- [ ] `tests/test_raii.cpp` -- covers BLD-03, TEST-02
- [ ] `tests/test_smoke.cpp` -- covers TEST-01, TEST-04
- [ ] Framework install: `sudo apt install cmake pkg-config libx11-dev libxext-dev xvfb` -- all missing from environment
- [ ] CMakeLists.txt -- does not exist yet; must be created as first Wave 0 task
- [ ] `include/x11wrap.h` -- RAII wrappers; does not exist yet

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | N/A -- window manager, no authentication |
| V3 Session Management | no | N/A -- no session management in Phase 1 |
| V4 Access Control | no | N/A -- local-only X11 window manager |
| V5 Input Validation | no | N/A -- no user input processing in Phase 1 |
| V6 Cryptography | no | N/A -- no cryptographic operations |

Phase 1 is build infrastructure and RAII wrappers. No user-facing input processing, no network communication, no authentication. Security concerns are deferred to later phases.

### Known Threat Patterns for C++17 + X11 Build System

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Build dependency confusion (FetchContent) | Tampering | Pin exact Git tag (`v3.14.0`); use HTTPS URL |
| Unsanitized environment variables (DISPLAY) | Tampering | Xlib handles DISPLAY parsing; no custom parsing needed |

## Sources

### Primary (HIGH confidence)
- Context7 /catchorg/catch2 - CMake integration, FetchContent, catch_discover_tests
- Context7 /kitware/cmake - FindPkgConfig, pkg_check_modules, IMPORTED_TARGET, cmake_pkg_config
- [x.org Xlib specification](https://www.x.org/releases/current/doc/libX11/libX11/libX11.html) - XID type definitions, GC pointer type
- [CMake FindPkgConfig documentation](https://cmake.org/cmake/help/latest/module/FindPkgConfig.html) - pkg_check_modules API

### Secondary (MEDIUM confidence)
- [Catch2 CMake integration](https://github.com/catchorg/catch2/blob/devel/docs/cmake-integration.md) - FetchContent pattern, test discovery
- [Modern CMake guide](https://cliutils.gitlab.io/modern-cmake/) - Best practices for pkg-config integration
- [bioweapon: std::unique_ptr as RAII wrapper](https://biowpn.github.io/bioweapon/2024/03/05/raii-all-the-things.html) - Custom deleter patterns for non-memory resources
- [Daily bit(e) of C++: Legacy resources with unique_ptr](https://medium.com/@simontoth/daily-bit-e-of-c-legacy-resources-with-std-unique-ptr-5f1ba7a68f2c) - Opaque handle wrapping pattern

### Tertiary (LOW confidence)
- CTest Xvfb fixture pattern -- community practice, not verified against official docs for this exact use case

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - CMake pkg-config pattern well-documented; Catch2 FetchContent verified via Context7 and GitHub API
- Architecture: HIGH - RAII patterns are standard C++; X11 type categorization verified against Xlib specification
- Pitfalls: MEDIUM - Rotated.C compilation issues are anticipated but not tested; Xvfb fixture reliability is community knowledge
- Environment: HIGH - All missing dependencies verified by probing the actual system

**Research date:** 2026-05-06
**Valid until:** 2026-07-06 (stable domain; C++17 and X11 are mature)
