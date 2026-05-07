---
phase: 05-configuration-system
reviewed: 2026-05-07T00:00:00Z
depth: standard
files_reviewed: 10
files_reviewed_list:
  - CMakeLists.txt
  - include/Border.h
  - include/Config.h
  - include/Manager.h
  - src/Border.cpp
  - src/Config.cpp
  - src/Events.cpp
  - src/Manager.cpp
  - src/main.cpp
  - tests/test_config.cpp
findings:
  critical: 2
  warning: 6
  info: 4
  total: 12
status: issues_found
---

# Phase 05: Code Review Report

**Reviewed:** 2026-05-07
**Depth:** standard
**Files Reviewed:** 10
**Status:** issues_found

## Summary

Reviewed all 10 source files for the configuration system phase and related core changes. The configuration system itself (Config.h, Config.cpp, test_config.cpp) is well-implemented with solid XDG path resolution, layered config loading, and comprehensive test coverage (35 tests). However, the review uncovered two critical bugs and several warnings in the broader codebase touched by this phase.

The critical issues are: (1) a global mutable variable `FRAME_WIDTH` that is set only once in the first Border constructor but never re-synchronized if a second WindowManager were ever instantiated, creating a latent global-state corruption risk; and (2) the `Config::applyCliArgs` method uses global `optind`/`optarg` state which makes it unsafe for repeated calls and causes test interference if tests are not carefully isolated.

## Critical Issues

### CR-01: FRAME_WIDTH global mutable state is set only on first Border construction, never updated for subsequent WindowManager instances

**File:** `src/Border.cpp:11,37`
**Issue:** `FRAME_WIDTH` is a global `extern int` variable (declared in `Border.h:12`) initialized to 7 and then overwritten from config at `Border.cpp:37` inside the `if (m_tabFont == nullptr)` guard. This guard runs only once (the static `m_tabFont` check), meaning the global `FRAME_WIDTH` is set from the first Border's config and never updated again. If a second WindowManager were created with a different `frameThickness`, all Border instances would still use the first WindowManager's thickness value. More immediately, every other translation unit that reads `FRAME_WIDTH` at static-init time or after the first Border gets a stale or inconsistent value. The `yIndent()` and `xIndent()` inline methods in `Border.h:45-51` read `FRAME_WIDTH` at call time, which is correct only as long as the global stays in sync with the config -- but there is no mechanism to re-sync it.

**Fix:** Remove the global `FRAME_WIDTH` entirely. Store `frameThickness` per-Border (or per-WindowManager) and access it through `windowManager()->config().frameThickness` everywhere it is needed. Alternatively, if the global must remain for compatibility, set it in the WindowManager constructor (not in Border) and document the singleton constraint.

### CR-02: Config::applyCliArgs pollutes global getopt state (optind/optarg), making repeated calls unsafe and causing test fragility

**File:** `src/Config.cpp:248`
**Issue:** The function resets `optind = 1` at the start but does not reset `opterr` or handle `optopt`. The POSIX `getopt_long` function uses global state (`optind`, `optarg`, `optopt`, `opterr`) that persists across calls. While the `optind = 1` reset handles the normal case, this design means:
1. If `Config::applyCliArgs` is called twice in the same process (which the tests do), the second call's getopt state depends on cleanup from the first call. The `optind = 1` reset is not guaranteed to be sufficient on all platforms -- glibc requires `optind = 0` to trigger full reinitialization of internal static variables.
2. The `Config::load()` flow calls `applyCliArgs` exactly once, which is the production path. But the test file calls `applyCliArgs` directly on multiple Config objects in the same process (Tests 20-35), which relies on implementation-specific getopt reinitialization behavior.

**Fix:** Use `optind = 0` instead of `optind = 1` to trigger full glibc reinitialization. Additionally, consider setting `opterr = 0` before parsing and handling error messages manually, to prevent getopt from printing to stderr during tests (currently unknown options cause `exit(2)` which is also handled via fork in the test, but this is fragile).

## Warnings

### WR-01: Border static resources (GC, colors, font) are allocated using the first Client's Display/Screen but never verified against subsequent clients

**File:** `src/Border.cpp:35-89`
**Issue:** The static initialization block guarded by `if (m_tabFont == nullptr)` allocates `m_frameBackgroundPixel`, `m_buttonBackgroundPixel`, `m_borderPixel`, `m_drawGC`, `m_xftForeground`, and `m_xftBackground` using the first Border's `display()` and `root()`. These are shared across all Border instances. While this is correct for a single-display window manager, there is no assertion or comment documenting that `display()` must always return the same value. If the static resources were ever cleaned up (e.g., on last Border destruction) and a new Border was created afterward, the resources would be re-allocated using whatever display the new Border has, but the old display may have been closed.

**Fix:** Add a comment block documenting the singleton-display assumption. Consider adding an assertion in the constructor: `assert(m_tabFont == nullptr || display() == /* previously used display */)`.

### WR-02: Border destructor calls XDestroyWindow on windows that may have already been destroyed

**File:** `src/Border.cpp:95-106`
**Issue:** In `WindowManager::eventDestroy` (Events.cpp:259), `ignoreBadWindowErrors` is set to `true` before the Client's unique_ptr is erased. When the Client destructor runs, it may call `unreparent()` which destroys/reparents X11 windows. Then the Border destructor also calls `XDestroyWindow` on `m_tab`, `m_button`, `m_parent`, and `m_resize`. If the X11 windows were already destroyed (by the client window being destroyed), these calls generate `BadWindow` errors. The `ignoreBadWindowErrors` flag suppresses these, but this is fragile -- if `ignoreBadWindowErrors` is ever not set when a Border is destroyed after its client window is gone, the error handler will fire. The pattern relies on a global boolean flag for correctness.

**Fix:** Set the Border's window handles to `None` after destruction, or check whether the child window still exists before destroying parent windows. Consider using `XDestroyWindow` only on `m_parent` (since destroying the parent automatically destroys child windows in X11).

### WR-03: Config file values for colors are not validated -- invalid X11 color names will cause fatal() at runtime

**File:** `src/Config.cpp:137-146`
**Issue:** The `applyKeyValue` method accepts any string for color settings (tab-foreground, tab-background, etc.) without validation. An invalid color name like `tab-foreground = not-a-color` will be stored in the Config struct and later passed to `XAllocNamedColor` or `XftColorAllocName` in `Border.cpp` or `Manager.cpp`, which calls `fatal()` and terminates the window manager. A typo in a config file becomes a hard crash with no indication of which line caused it.

**Fix:** Consider adding a validation step in `applyKeyValue` that attempts to parse the color (or at least checks for empty strings). Alternatively, catch the error in Border/Manager initialization and print a message identifying the offending config key, then fall back to the default rather than calling `fatal()`.

### WR-04: Test file uses std::system() with unsanitized directory paths (command injection risk in test context)

**File:** `tests/test_config.cpp:272-273,306-307`
**Issue:** The precedence test (Test 10) uses `std::system(("mkdir -p " + sysSubdir).c_str())` and `std::system(("rm -rf " + sysDir).c_str())`. While the directory names are generated from `std::rand()`, the paths are not shell-escaped. If the temporary directory path contained shell metacharacters (unlikely with `std::rand()` but possible if `HOME` or `TMPDIR` contained them), this could execute arbitrary commands. This is a test-only concern but represents a poor pattern.

**Fix:** Use `mkdir()` syscall or `std::filesystem::create_directories()` instead of `std::system()`. Use `std::filesystem::remove_all()` instead of `rm -rf` via `std::system()`.

### WR-05: Test file uses std::rand() for temp directory names without seeding -- predictable names

**File:** `tests/test_config.cpp:266`
**Issue:** `std::rand()` is used without calling `std::srand()` first, so the sequence is implementation-defined but deterministic. In parallel test execution, two test processes could generate the same "random" directory name and interfere with each other. The test also uses `/tmp/` directly instead of respecting `TMPDIR` or using `mkdtemp()`.

**Fix:** Use `mkdtemp()` or `std::filesystem::create_unique_directory()` for temporary directory creation.

### WR-06: Config::applyFile silently ignores I/O errors other than file-not-found

**File:** `src/Config.cpp:86`
**Issue:** The check `if (!file.is_open()) return;` treats all open failures identically -- whether the file doesn't exist (expected, should skip silently) or exists but is unreadable due to permissions (should warn the user). A user who has a config file with wrong permissions will get no indication that their config is being ignored.

**Fix:** Check if the file exists first (e.g., with `std::ifstream` combined with `errno` or `access()`) and warn on permission errors while still silently skipping non-existent files.

## Info

### IN-01: Static counter in writeTempConfig produces predictable filenames

**File:** `tests/test_config.cpp:16-17`
**Issue:** The `writeTempFile` helper uses a `static int counter` that increments across all test cases. If a test crashes without cleanup, subsequent test runs will use different counter values and stale temp files accumulate in `/tmp/`. This is a minor test hygiene issue.

**Fix:** Use `mkstemp()` or `std::filesystem::temp_directory_path()` for truly unique temporary files, or use a test fixture that cleans up on failure.

### IN-02: Config struct is exposed as a plain struct with public members -- no encapsulation

**File:** `include/Config.h:6-44`
**Issue:** All 18 config fields are public members of `Config`. This is intentional for simplicity (no getters/setters for a plain data struct), but it means any code can modify the config after `Config::load()` returns. The `WindowManager` stores a copy (`m_config`) so this is not a runtime risk, but it reduces the ability to add validation in the future.

**Fix:** This is acceptable for a small config struct. If validation requirements grow, consider making fields private with accessor methods.

### IN-03: FRAME_WIDTH extern global vs. Config::frameThickness -- redundant storage

**File:** `include/Border.h:12` and `src/Border.cpp:11`
**Issue:** `FRAME_WIDTH` is a global `int` that duplicates `Config::frameThickness`. It exists because the upstream code used a `#define` constant. The modernized code stores the value in `Config` but still copies it to the global for backward compatibility with code that reads `FRAME_WIDTH` directly. This creates two sources of truth for the same value.

**Fix:** Replace all uses of `FRAME_WIDTH` with `windowManager()->config().frameThickness` (or pass the config reference through to where it is needed) and remove the global.

### IN-04: Events.cpp and Manager.cpp contain hardcoded startup message about focus policy

**File:** `src/Manager.cpp:61`
**Issue:** The startup message `"Focus follows pointer. Hidden clients only on menu.\n"` is hardcoded and does not reflect the actual configured focus policy (which can now be `click-to-focus` via the new configuration system). This message will be incorrect if the user configures `click-to-focus = true`.

**Fix:** Update the startup message to reflect the actual configured focus policy, or print the active configuration on startup.

---

_Reviewed: 2026-05-07_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
