# Technology Stack

**Analysis Date:** 2026-05-06

## Languages

**Primary:**
- C++ (pre-standard, likely C++98-era or earlier) - All source files use `.C` extension and pre-standard C++ idioms (no namespaces, no STL containers, no templates in the modern sense)

**Secondary:**
- C (system-level headers and POSIX APIs) - Used alongside C++ for system calls, X11 bindings, and low-level operations

## Runtime

**Environment:**
- X11 Window System (Xlib R4 or newer required) - Direct Xlib client, no toolkit abstraction
- POSIX-compliant Unix (uses `fork()`, `exec*()`, `select()`, `sigaction()`, `wait()`)

**Package Manager:**
- None - Hand-written `Makefile`, no package manager
- Lockfile: Not applicable

## Frameworks

**Core:**
- Xlib (X11 core protocol library) - Direct window manager protocol implementation
- X11 Shape Extension (`XShapeQueryExtension`) - Required for non-rectangular window borders (shaped windows)

**Testing:**
- None - No test framework or test files exist

**Build/Dev:**
- GNU Make (implied by `Makefile` structure) - Build orchestration
- `makedepend` - Dependency generation for Makefile

## Key Dependencies

**Critical:**
- X11 core libraries (`libX11`) - Window protocol communication, event loop, window management
- X11 extensions library (`libXext`) - Shape extension for non-rectangular window frames
- X11 toolkit (`libXt`) - Listed in link line, may not be actively used in source
- X11 miscellaneous utilities (`libXmu`) - Listed in link line
- X11 Session Management (`libSM`, `libICE`) - Listed in link line for session management
- C math library (`libm`) - Listed in link line

**Infrastructure:**
- xvertext 2.0 (bundled in `upstream-wm2/Rotated.C`, `upstream-wm2/Rotated.h`) - Rotated font rendering by Alan Richardson, used for sideways window tab labels. Third-party code included directly in the source tree.

## Configuration

**Environment:**
- Compile-time only via `#define` macros in `upstream-wm2/Config.h`
- `DISPLAY` environment variable read at runtime for X server connection
- `SHELL` environment variable read at runtime for spawning new terminal windows
- No runtime config files, no X resources, no command-line options

**Build:**
- `upstream-wm2/Makefile` - Hardcoded paths to `/usr/X11R6/lib` and `/usr/X11R6/include`
- Compiler: `gcc` for C, `g++` for C++ (configurable via `CC`/`CCC` variables)
- Flags: `-O2 -Wall` with X11 include paths

**Compile-time settings in `upstream-wm2/Config.h`:**
- Font names: `CONFIG_NICE_FONT`, `CONFIG_NICE_MENU_FONT`, `CONFIG_NASTY_FONT`
- New window command: `CONFIG_NEW_WINDOW_COMMAND` (defaults to `"xterm"`)
- Focus policy: `CONFIG_CLICK_TO_FOCUS`, `CONFIG_RAISE_ON_FOCUS`, `CONFIG_AUTO_RAISE`
- Timing: `CONFIG_AUTO_RAISE_DELAY`, `CONFIG_POINTER_STOPPED_DELAY`, `CONFIG_DESTROY_WINDOW_DELAY`
- Colors: `CONFIG_TAB_FOREGROUND`, `CONFIG_TAB_BACKGROUND`, `CONFIG_FRAME_BACKGROUND`, etc.
- Frame thickness: `CONFIG_FRAME_THICKNESS` (default 7)
- Shell execution: `CONFIG_EXEC_USING_SHELL`
- Root menu behavior: `CONFIG_EVERYTHING_ON_ROOT_MENU`

## Platform Requirements

**Development:**
- Unix system with X11 development headers
- C++ compiler (GCC `g++` assumed)
- X11 R4+ server and libraries with Shape extension support
- X font server or local fonts matching configured X font names (XLFD patterns)

**Production:**
- X11 display server with Shape extension support
- X fonts: Lucida Bold 14pt, Lucida Medium 14pt (falls back to `fixed`)
- Mouse with at least one button
- Single-screen display only (multi-screen not supported)

---

*Stack analysis: 2026-05-06*
