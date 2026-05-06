---
phase: 01-build-infrastructure-raii-foundation
verified: 2026-05-06T21:15:00Z
status: passed
score: 5/5 must-haves verified
overrides_applied: 0
re_verification: false
---

# Phase 1: Build Infrastructure + RAII Foundation Verification Report

**Phase Goal:** The project builds from a clean checkout on Ubuntu 22.04+ with a modern C++17 codebase and RAII-managed X11 resources, and the test harness runs on Xvfb.
**Verified:** 2026-05-06T21:15:00Z
**Status:** passed
**Re-verification:** No -- initial verification

## Goal Achievement

### Observable Truths (from ROADMAP Success Criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Running `cmake -B build && cmake --build build` produces a working binary from a clean checkout on Ubuntu 22.04 | VERIFIED | `cmake --build build` exits 0. Binary at `build/wm2-born-again` (172864 bytes). WM starts on Xvfb, prints banner, detects Shape extension, runs event loop for 3 seconds before timeout. |
| 2 | All X11 resources (Display, GC, XFontStruct, Cursor, Font, Pixmap, Colormap) are wrapped in RAII classes that release on scope exit -- no manual XFree calls in application code | VERIFIED | `include/x11wrap.h` provides 7 RAII types: DisplayPtr, GCPtr, FontStructPtr (unique_ptr+deleter), UniqueCursor, UniqueFont, UniquePixmap, UniqueColormap (thin move-only classes). Grep confirms zero XCloseDisplay/XFreeGC/XFreeCursor/XFreeFont calls outside x11wrap.h in src/ and include/. Manager.h uses x11::DisplayPtr, x11::GCPtr, x11::FontStructPtr, x11::UniqueCursor as members. |
| 3 | Code compiles with `-std=c++17` and uses std::vector, std::string, bool throughout (no char*, no custom list macros, no C-style bools) | VERIFIED | CMakeLists.txt sets `CMAKE_CXX_STANDARD 17` with `REQUIRED ON`. compile_commands.json confirms `-std=c++17`. 30 uses of `std::vector/string/unique_ptr/make_unique` found in new code. Zero instances of `Boolean`, `True`, `False` types, `#include "General.h"`, `#include "listmacro2.h"`, `delete this`, or `NewString` in src/ and include/. Only `const char*` used for static string literals (m_menuCreateLabel, m_defaultLabel, menuLabel). |
| 4 | No hardcoded X11R6 paths -- all X11 library/include paths discovered via pkg-config in CMakeLists.txt | VERIFIED | CMakeLists.txt uses `find_package(PkgConfig REQUIRED)` and `pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)`. Grep for `X11R6` and `/usr/X11` returns zero results in CMakeLists.txt, include/, and src/. |
| 5 | `ctest` runs the test suite successfully on Xvfb without a physical display, using Catch2 framework | VERIFIED | `ctest --test-dir build --output-on-failure` returns "100% tests passed, 0 tests failed out of 25". 16 RAII unit tests (null safety, move-only traits, ownership transfer) + 7 smoke tests (real X11 resource lifecycle on Xvfb) + 2 Xvfb fixture tests. Catch2 v3.14.0 via FetchContent. |

**Score:** 5/5 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `CMakeLists.txt` | Build system with pkg-config X11 discovery, FetchContent Catch2, Xvfb fixtures | VERIFIED | 97 lines. pkg_check_modules with IMPORTED_TARGET for x11, xext. FetchContent for Catch2. Xvfb start/stop fixtures. |
| `include/x11wrap.h` | RAII wrappers for 7 X11 resource types in namespace x11 | VERIFIED | 252 lines. DisplayPtr, GCPtr, FontStructPtr, UniqueCursor, UniqueFont, UniquePixmap, UniqueColormap. All with move semantics, no-copy, proper cleanup. |
| `include/Cursors.h` | Cursor bitmap data from upstream | VERIFIED | 6 cursor bitmap arrays found (cursor_bits, cursor_mask_bits, ninja_cross_bits, etc.) |
| `tests/test_raii.cpp` | RAII unit tests for construct/destruct/move/null-safety | VERIFIED | 16 tests covering all 7 RAII types: null safety, move-only type traits, ownership transfer. |
| `tests/test_smoke.cpp` | X11 + RAII integration tests on Xvfb | VERIFIED | 7 tests: display open, DisplayPtr/GCPtr/FontStructPtr/UniqueCursor/UniquePixmap lifecycle. Uses real Xvfb display. |
| `include/Manager.h` | WindowManager class with RAII members and std::vector<Client*> | VERIFIED | Contains x11::DisplayPtr, x11::GCPtr, x11::FontStructPtr, x11::UniqueCursor, std::vector<Client*>, bool types. No legacy types. |
| `include/Client.h` | Client class with std::string and std::unique_ptr<Border> | VERIFIED | std::string m_name/m_iconName/m_label, std::unique_ptr<Border> m_border, enum class Protocol, bool types. |
| `include/Border.h` | Border class with x11::GCPtr and std::string | VERIFIED | static x11::GCPtr m_drawGC, std::string m_label, constexpr dimensions. |
| `src/main.cpp` | Entry point creating WindowManager | VERIFIED | `int main(int argc, char** argv)` creates `WindowManager manager`. |
| `src/Manager.cpp` | WindowManager lifecycle (display open, atoms, screen init, event loop) | VERIFIED | 400+ lines. Constructor opens display via RAII, sets error handler, signal handlers via sigaction, interns 7 atoms, checks Shape extension, initializes screen, scans windows. |
| `src/Client.cpp` | Client construction and lifecycle | VERIFIED | 700+ lines. Uses std::make_unique<Border>, std::string for properties, no `delete this`. malloc only for X11 Colormap array (documented exception). |
| `src/Border.cpp` | Border frame creation and Shape decoration | VERIFIED | 600+ lines. Uses x11::make_gc, XReparentWindow, XShapeCombineMask/Rectangles (22 Shape calls), XRotLoadFont/DrawString. |
| `src/Buttons.cpp` | Button event dispatch and root menu | VERIFIED | 300+ lines. WindowManager::eventButton, menu with spawn/circulate/exit, Client::eventButton with move/resize. |
| `src/Events.cpp` | Event loop and event handler dispatch | VERIFIED | 323 lines. loop() with switch on all core event types (MapRequest, ConfigureRequest, UnmapNotify, DestroyNotify, ButtonPress, PropertyNotify, etc.). select()+goto pattern (Phase 2 replaces). |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `CMakeLists.txt` | `include/x11wrap.h` | target_include_directories | WIRED | `CMAKE_SOURCE_DIR/include` in target_include_directories |
| `CMakeLists.txt` | Catch2 | FetchContent | WIRED | `FetchContent_Declare(Catch2 ...)` + `FetchContent_MakeAvailable(Catch2)` |
| `tests/test_smoke.cpp` | `include/x11wrap.h` | #include | WIRED | `#include "x11wrap.h"` at line 2 |
| `src/main.cpp` | `include/Manager.h` | #include | WIRED | `#include "Manager.h"` at line 1 |
| `src/Manager.cpp` | `include/x11wrap.h` | RAII members | WIRED | Uses x11::DisplayPtr, x11::GCPtr, x11::FontStructPtr, x11::UniqueCursor (8 references) |
| `src/Client.cpp` | `include/Client.h` | implementation | WIRED | Defines Client:: methods |
| `src/Border.cpp` | `include/Border.h` | implementation | WIRED | Defines Border:: methods (2 x11:: references) |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|-------------------|--------|
| `src/Manager.cpp` | m_display | XOpenDisplay(nullptr) via RAII reset | Yes -- real X11 connection | FLOWING |
| `src/Manager.cpp` | Atoms::* | XInternAtom() calls | Yes -- real atoms interned | FLOWING |
| `src/Client.cpp` | m_name | XGetWindowProperty via getProperty() | Yes -- reads WM_NAME from X server | FLOWING |
| `src/Border.cpp` | m_label | Client name propagated via drawLabel() | Yes -- from client m_name | FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Build produces binary | `cmake --build build` | Exit 0, all targets built | PASS |
| Binary starts on Xvfb | `DISPLAY=:98 timeout 3 ./build/wm2-born-again` | Banner printed, "Shape extension available", exits after timeout (code 124) | PASS |
| Test suite passes | `ctest --test-dir build --output-on-failure` | 25/25 tests passed in 1.25s | PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| BLD-01 | Plan 01 | WM builds with CMake 3.16+ and pkg-config for X11 dependency discovery | SATISFIED | CMakeLists.txt: cmake_minimum_required(VERSION 3.16), find_package(PkgConfig REQUIRED), pkg_check_modules with IMPORTED_TARGET |
| BLD-02 | Plan 02 | C++17 standard (std::vector, std::string, bool, structured bindings) | SATISFIED | CMAKE_CXX_STANDARD 17 REQUIRED ON confirmed in compile_commands.json. std::vector/string/unique_ptr/bool used throughout. Zero legacy types. |
| BLD-03 | Plan 01 | RAII wrappers for all X11 resources (Display, Window, GC, Cursor, Font, Pixmap, Colormap) | SATISFIED | include/x11wrap.h provides 7 RAII types. All tested (16 unit + 7 smoke tests). Note: Window is an XID integer, not wrapped per design decision. |
| BLD-04 | Plan 02 | Builds and runs on Ubuntu 22.04+ with standard apt packages | SATISFIED | Build succeeds, binary runs on Xvfb on this Ubuntu system. Dependencies: libx11-dev, libxext-dev (standard apt packages). |
| BLD-05 | Plan 01 | No hardcoded X11R6 paths | SATISFIED | Zero X11R6 or /usr/X11 references in CMakeLists.txt, include/, src/. All discovery via pkg-config. |
| TEST-01 | Plan 01 | Test suite runs on Xvfb without physical display | SATISFIED | CMakeLists.txt Xvfb fixtures (start/stop), smoke tests with DISPLAY=:99, all 25 tests pass. |
| TEST-02 | Plan 01 | Catch2 test framework for unit and integration tests | SATISFIED | FetchContent declares Catch2 v3.14.0. test_raii (16 unit tests) + test_smoke (7 integration tests). |
| TEST-04 | Plan 01 | CI-ready (tests pass with cmake + ctest) | SATISFIED | `cmake --build build && ctest --test-dir build` passes all 25 tests in 1.25s. |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| `src/Events.cpp` | 162 | `goto waiting` in nextEvent() | Info | Expected -- Phase 2 replaces select()+goto with poll(). Not a defect in Phase 1 scope. |
| `src/Client.cpp` | 62,88,448,459 | malloc/free for Colormap array | Info | Documented exception for X11 Colormap* interop array. Not a string management issue. No memory leak risk (free called in destructor and before realloc). |

### Human Verification Required

None. All success criteria are verifiable programmatically and have been verified.

### Gaps Summary

No gaps found. All 5 ROADMAP success criteria verified against actual codebase evidence. All 8 requirement IDs (BLD-01 through BLD-05, TEST-01, TEST-02, TEST-04) are satisfied. Build succeeds, binary runs on Xvfb, 25 tests pass, RAII wrappers fully functional, C++17 idioms used throughout, no hardcoded paths.

---

_Verified: 2026-05-06T21:15:00Z_
_Verifier: Claude (gsd-verifier)_
