<!-- GSD:project-start source:PROJECT.md -->
## Project

**wm2-born-again**

A modernized resurrection of wm2, Chris Cannam's minimalist X11 window manager from 1997, adapted for use on VPS droplets (DigitalOcean-style) accessed via VNC/RDP. Retains wm2's distinctive sideways-tab visual identity while updating the internals for modern Linux, adding runtime configuration, and introducing AI-powered application discovery for the root menu. Also serves as a showcase project for AI-assisted software development.

**Core Value:** A lightweight, visually distinctive window manager that works well on resource-constrained VPS instances via remote desktop — simple enough for non-programmers to configure, reliable enough for daily use.

### Constraints

- **Tech stack**: C++17, X11/Xlib, Xft, Xrandr, fontconfig — must remain lightweight for VPS use
- **Build system**: CMake with pkg-config — standard for modern Linux packaging
- **Target OS**: Ubuntu 22.04+ (primary), should build on any modern Linux with X11
- **License**: MIT (matching upstream wm2)
- **Performance**: Must run comfortably in 512MB RAM VPS with VNC server
- **Remote desktop**: Must work over VNC/XRDP without requiring special server configuration
- **Visual identity**: Preserve wm2's classic sideways-tab look — this is non-negotiable
<!-- GSD:project-end -->

<!-- GSD:stack-start source:codebase/STACK.md -->
## Technology Stack

## Languages
- C++ (pre-standard, likely C++98-era or earlier) - All source files use `.C` extension and pre-standard C++ idioms (no namespaces, no STL containers, no templates in the modern sense)
- C (system-level headers and POSIX APIs) - Used alongside C++ for system calls, X11 bindings, and low-level operations
## Runtime
- X11 Window System (Xlib R4 or newer required) - Direct Xlib client, no toolkit abstraction
- POSIX-compliant Unix (uses `fork()`, `exec*()`, `select()`, `sigaction()`, `wait()`)
- None - Hand-written `Makefile`, no package manager
- Lockfile: Not applicable
## Frameworks
- Xlib (X11 core protocol library) - Direct window manager protocol implementation
- X11 Shape Extension (`XShapeQueryExtension`) - Required for non-rectangular window borders (shaped windows)
- None - No test framework or test files exist
- GNU Make (implied by `Makefile` structure) - Build orchestration
- `makedepend` - Dependency generation for Makefile
## Key Dependencies
- X11 core libraries (`libX11`) - Window protocol communication, event loop, window management
- X11 extensions library (`libXext`) - Shape extension for non-rectangular window frames
- X11 toolkit (`libXt`) - Listed in link line, may not be actively used in source
- X11 miscellaneous utilities (`libXmu`) - Listed in link line
- X11 Session Management (`libSM`, `libICE`) - Listed in link line for session management
- C math library (`libm`) - Listed in link line
- xvertext 2.0 (bundled in `upstream-wm2/Rotated.C`, `upstream-wm2/Rotated.h`) - Rotated font rendering by Alan Richardson, used for sideways window tab labels. Third-party code included directly in the source tree.
## Configuration
- Compile-time only via `#define` macros in `upstream-wm2/Config.h`
- `DISPLAY` environment variable read at runtime for X server connection
- `SHELL` environment variable read at runtime for spawning new terminal windows
- No runtime config files, no X resources, no command-line options
- `upstream-wm2/Makefile` - Hardcoded paths to `/usr/X11R6/lib` and `/usr/X11R6/include`
- Compiler: `gcc` for C, `g++` for C++ (configurable via `CC`/`CCC` variables)
- Flags: `-O2 -Wall` with X11 include paths
- Font names: `CONFIG_NICE_FONT`, `CONFIG_NICE_MENU_FONT`, `CONFIG_NASTY_FONT`
- New window command: `CONFIG_NEW_WINDOW_COMMAND` (defaults to `"xterm"`)
- Focus policy: `CONFIG_CLICK_TO_FOCUS`, `CONFIG_RAISE_ON_FOCUS`, `CONFIG_AUTO_RAISE`
- Timing: `CONFIG_AUTO_RAISE_DELAY`, `CONFIG_POINTER_STOPPED_DELAY`, `CONFIG_DESTROY_WINDOW_DELAY`
- Colors: `CONFIG_TAB_FOREGROUND`, `CONFIG_TAB_BACKGROUND`, `CONFIG_FRAME_BACKGROUND`, etc.
- Frame thickness: `CONFIG_FRAME_THICKNESS` (default 7)
- Shell execution: `CONFIG_EXEC_USING_SHELL`
- Root menu behavior: `CONFIG_EVERYTHING_ON_ROOT_MENU`
## Platform Requirements
- Unix system with X11 development headers
- C++ compiler (GCC `g++` assumed)
- X11 R4+ server and libraries with Shape extension support
- X font server or local fonts matching configured X font names (XLFD patterns)
- X11 display server with Shape extension support
- X fonts: Lucida Bold 14pt, Lucida Medium 14pt (falls back to `fixed`)
- Mouse with at least one button
- Single-screen display only (multi-screen not supported)
<!-- GSD:stack-end -->

<!-- GSD:conventions-start source:CONVENTIONS.md -->
## Conventions

## Overview
## Naming Patterns
- Implementation files use `.C` extension (not `.cpp` or `.cc`)
- Header files use `.h` extension
- Files are named after the primary class or concept they contain: `Client.C`/`Client.h`, `Manager.C`/`Manager.h`, `Border.C`/`Border.h`
- `General.h` is the catch-all precompiled header
- `Config.h` contains compile-time configuration
- `Cursors.h` contains X11 bitmap data (cursor definitions)
- `listmacro2.h` contains generic list macros (template substitute)
- `Rotated.C`/`Rotated.h` are third-party xvertext font rotation library
- PascalCase: `WindowManager`, `Client`, `Border`, `BorderRectangleList`, `Atoms`
- One class per header/implementation pair (except `Atoms` which is a static-only utility class in `General.h`)
- `BorderRectangleList` derives from a macro-generated `RectangleList` base in `Border.C:24`
- camelCase: `windowToClient()`, `mapRaised()`, `installColormap()`, `fixTabHeight()`
- Multi-word names are concatenated without separators: `eventMapRequest()`, `setFrameVisibility()`
- Static helper functions in `.C` files use camelCase: `getProperty_aux()` in `Client.C:375`, `makeCursor()` in `Manager.C:211`, `nobuttons()` in `Buttons.C:95`
- Prefix `m_` followed by camelCase: `m_activeClient`, `m_display`, `m_tabWidth`, `m_windowManager`
- Static members also use `m_` prefix: `m_initialising`, `m_signalled`, `m_tabFont`
- Declared in `private:` or `protected:` sections of headers
- Short, descriptive: `c` for Client pointers, `e` for events, `d` for Display pointers, `w` for windows
- Loop counters: `i`, `j`, `n`
- No consistent prefix or suffix for parameters vs locals
- ALL_CAPS with underscores: `CONFIG_CLICK_TO_FOCUS`, `TAB_TOP_HEIGHT`, `FRAME_WIDTH`
- `#define` constants in `Config.h`, `Border.h`, `General.h`, `Buttons.C`
- Boolean values use Xlib's `True`/`False` (capitalized), not C++ `true`/`false`
- `Boolean` is a typedef for `char` (defined in `General.h:33`): `typedef char Boolean;`
- Xlib types used directly: `Window`, `Display`, `Colormap`, `Cursor`, `GC`, `Atom`
- Protocol flags use `#define`: `Pdelete`, `PtakeFocus` in `Client.h:142-144`
## Code Style
- No automated formatting tool configured
- Tab indentation (appears as 4-space display width)
- Opening braces on same line as control statement (K&R/1TBS style):
- Single-line bodies often placed on same line without braces:
- Short inline methods defined in headers without braces:
- Some code uses tab+space combinations for alignment
- Switch cases are indented one level from `switch`
- `private:`/`public:` access specifiers are not indented relative to class
- No enforced limit; some lines exceed 80 characters
- Long function calls are split across lines with aligned arguments
- Trailing whitespace present in many files
- No trailing newline enforcement
- No linter configured (`.clang-format`, `.clang-tidy`, `CPPLINT.cfg` absent)
- Makefile uses `-Wall` flag only (`Makefile:7`): `CFLAGS = -O2 -Wall $(INCS)`
## C++ Dialect and Idioms
- Pre-standard C++ (targets g++ circa 1997)
- No namespaces, no STL, no templates, no exceptions, no RTTI
- No `const_cast`, `static_cast`, `reinterpret_cast` -- uses C-style casts throughout:
- No `nullptr`; uses `0` or `NULL` for null pointers
- Raw `new`/`delete` with no smart pointers
- `malloc()`/`free()` used alongside `new`/`delete`:
- `NewString(x)` macro wraps `malloc`+`strcpy` for string duplication (`General.h:34`)
- No STL containers
- Uses macro-based generic lists from `listmacro2.h`:
- List macros use `realloc()` for growth and `memcpy()` for element shifting
- No inheritance hierarchy (except `BorderRectangleList : RectangleList` in `Border.C:24`)
- Friendship not formally declared; `Border` accesses `Client` internals through accessor delegation
- Destructor is `protected` in `Client` to prevent stack deletion (`Client.h:84-85`): comment says "cravenly submitting to gcc's warnings"
- Self-deletion pattern: `Client::release()` calls `delete this` at `Client.C:91`
- `const` correctness is inconsistent; `const` used on some parameters but not method declarations
- Class-level static members initialized at file scope in `.C` files:
## Import Organization
#include "Manager.h"          // own header or primary header
#include "Client.h"           // related class header
#include <string.h>           // C standard library
#include <X11/Xproto.h>       // X11 protocol
#include "Cursors.h"          // project data header
#include "General.h"          // base includes (X11, standard C)
#include "listmacro2.h"       // utility macros
- Use `#ifndef _NAME_H_` / `#define _NAME_H_` / `#endif` pattern
- Guard names: `_GENERAL_H_`, `_MANAGER_H_`, `_CLIENT_H_`, `_BORDER_H_`, `_CONFIG_H_`, `_LISTMACRO_H_`
- `Rotated.h` uses `_XVERTEXT_INCLUDED_`
- `Cursors.h` uses `_WM2_CURSORS_H_`
- None; all includes use relative paths with `"quotes"`
- Used to break circular dependencies: `class Client;` in `Manager.h:12`, `class Client;` and `class WindowManager;` in `Border.h:9-10`
## Error Handling
- `WindowManager::fatal()` in `Manager.C:172-178` prints to stderr via `perror()`, then calls `exit(1)`
- Used for unrecoverable conditions: display open failure, missing font, missing shape extension
- `Client::fatal()` and `Border::fatal()` delegate to `WindowManager::fatal()`
- Static `WindowManager::errorHandler()` in `Manager.C:181-208` handles X11 protocol errors
- During initialization: exits on `BadAccess` (another WM running) or any error
- During runtime: logs to stderr, returns 0 (continues)
- Global `ignoreBadWindowErrors` flag (`General.h:63`) suppresses `BadWindow` errors during cleanup
- `fprintf(stderr, "wm2: warning: ...")` for recoverable issues
- Used for bad parent windows, self-referencing transients, message send failures
- No structured warning system
- `SIGTERM`, `SIGINT`, `SIGHUP` handled via `sigaction()` (wrapped by `signal()` macro in `General.h:41-48`)
- Signal handler sets static flag `m_signalled`; event loop checks flag and exits cleanly
- `sigHandler()` in `Manager.C:373-376` is minimal (sets flag only)
- `Rotated.C` uses `exit(1)` on malloc failure (lines 49, 73)
- `listmacro2.h` uses `assert()` on realloc failure (line 54)
- Main code generally does not check malloc return values
- Xlib calls generally not checked for errors (X11 error handler deals with them asynchronously)
- `XSendEvent()` return checked in `Client.C:369-371`
- `XGrabPointer()` return checked in `Buttons.C:195`, `Buttons.C:357-359`, `Buttons.C:468-470`
## Logging
- All log messages prefixed with `"wm2: "` for identification
- Warnings use `"wm2: warning: "` prefix
- Example: `fprintf(stderr, "wm2: bad map request for window %lx\n", e->window);`
- Startup banner printed to stderr in `WindowManager::WindowManager()` constructor (`Manager.C:30-73`)
- Runtime diagnostics printed to stderr for unexpected events and states
- Debug logging commented out with `//` throughout (search for `//    fprintf`): `Client.C:55`, `Client.C:64`, `Client.C:288`, `Client.C:406`
- Startup configuration info
- Runtime warnings and errors
- Debug traces (commented out)
- Third-party code (`Rotated.C`) has its own error messages without `wm2:` prefix
## Comments
- Inline `//` comments explain intent, gotchas, and hacks
- Block `/* */` comments used for file headers (Rotated.C/Rotated.h) and section dividers
- Section divider pattern uses fixed-width boxes:
- Acknowledgment of code quality issues: `// this is all a bit of a hack` (`Events.C:623`)
- Self-deprecating notes: `// cravenly submitting to gcc's warnings` (`Client.h:84`)
- Implementation notes: `// assumes efficient realloc()` (`listmacro2.h:8`)
- Cross-file references: `// in Buttons.C` (`Border.h:48`), `// in Events.C of all places` (`Border.h:58`)
- Attribution: `// strange code thieved from 9wm to avoid leaving zombies` (`Manager.C:534`)
- Design commentary: `// you could probably change these a certain amount before breaking the shoddy code` (`Border.h:11-13`)
- No JSDoc/TSDoc/Doxygen style documentation
- No parameter or return value documentation
- Header files serve as the interface documentation via method names and brief inline comments
## Function Design
- Functions range from 1 line (accessors) to ~170 lines (`WindowManager::menu()` in `Buttons.C:130-324`)
- Large functions are common; the menu handler, move, and resize operations are long inline event loops
- No refactoring pressure evident; long functions are the norm
- X11 event structs passed as pointers: `void eventButton(XButtonEvent *)`
- Boolean parameters for behavior flags: `void resize(XButtonEvent *, Boolean horizontal, Boolean vertical)`
- Default parameter values used sparingly: `Client *windowToClient(Window, Boolean create = False)` in `Manager.h:21`
- `const` applied inconsistently to pointer parameters: `const char *` in some places, `char *` in others
- `Boolean` (typedef for `char`) for success/failure and state queries: `Boolean isHidden()`, `Boolean getState(int *)`
- Raw pointers for object lookups (may return `0`/`NULL`): `Client *windowToClient(Window, Boolean)`
- `void` for operations that always succeed or handle their own errors
- Direct return of X11 types: `Window parent()`, `Display *display()`
- Trivial accessors defined in header files:
- More complex methods declared in headers, defined in `.C` files
## Module Design
- Each `.h` file exports one class (or set of related definitions)
- No barrel files or aggregate headers (except `General.h` which aggregates system includes and `Config.h`)
- `General.h` is the closest thing to a precompiled header: it includes all X11 headers, POSIX headers, and project-wide definitions
- Not used
- Each `.C` file includes only the headers it needs
- `WindowManager` (`Manager.h`/`Manager.C`): Top-level WM lifecycle, event loop, client list management
- `Client` (`Client.h`/`Client.C`): Individual client window state, window management operations
- `Border` (`Border.h`/`Border.C`): Window frame decoration, shaping, rendering
- Events are split across files by interaction mode:
- `Rotated.C`/`Rotated.h`: Third-party font rotation (xvertext 2.0)
- `Main.C`: Entry point only (20 lines)
- `Border::coordsInHole()` declared in `Border.h:58` but implemented in `Events.C:623` (noted in header comment)
- `Border::eventButton()` declared in `Border.h:48` but implemented in `Buttons.C:615` (noted in header comment)
## Preprocessor Conventions
- All configuration is done via `#define` constants
- Configuration is documented as requiring source edit + recompile
- Boolean config uses `True`/`False` (Xlib Boolean values)
- `CONFIG_` prefix for all config macros
- Conditional compilation for platform quirks: `#ifdef sgi` (`Manager.C:526`), `#ifdef hpux` (`Events.C:139`), `#ifndef __FreeBSD__` (`General.h:13`)
- `listmacro2.h` provides `declareList`/`implementList` and `declarePList`/`implementPList` macros as a template substitute
- Token pasting used: `T##_pointer` in `listmacro2.h:10-11`
- Used for: `ClientList` (pointer list of Client objects), `RectangleList` (value list of XRectangle)
- `General.h:41-48` redefines `signal()` as a macro wrapping `sigaction()` for portability
<!-- GSD:conventions-end -->

<!-- GSD:architecture-start source:ARCHITECTURE.md -->
## Architecture

## Pattern
## Entry Points
- `Main.C:main()` — Creates `WindowManager` singleton, which runs until signal or exit
- `WindowManager::WindowManager()` — Constructor does ALL initialization: opens display, allocates atoms, sets up root window event mask, creates cursors, loads fonts/colors, scans existing windows, enters event loop
- `WindowManager::loop()` — Blocking `select()`-based event loop dispatching to `event*()` methods
## Core Classes
### WindowManager (`Manager.h`, `Manager.C`)
- X connection lifecycle (open/close display)
- Event loop (`loop()`, `nextEvent()`)
- Event dispatch to handler methods
- Client list management (`m_clients`, `m_hiddenClients`)
- Root menu (spawn new windows, circulate, exit)
- Focus management (click-to-focus, raise-on-focus, auto-raise)
- Cursor management
- Colormap installation
- Color allocation
- Timestamp management (synthetic via property change)
- Signal handling (SIGTERM, SIGINT, SIGHUP)
- `m_clients: ClientList` — all managed clients
- `m_hiddenClients: ClientList` — iconic clients
- `m_activeClient: Client*` — focused client
- `m_display: Display*` — X connection
- `m_root: Window` — root window
### Client (`Client.h`, `Client.C`)
- Window state management (Normal/Iconic/Withdrawn)
- Reparenting into border frame
- Move, resize, hide/unhide operations
- WM_PROTOCOLS handling (WM_DELETE_WINDOW, WM_TAKE_FOCUS)
- Property change handling (WM_NAME, WM_ICON_NAME, WM_TRANSIENT_FOR, WM_COLORMAP_WINDOWS)
- Size hints enforcement
- Colormap management per-window
- `m_window: Window` — the actual client window
- `m_border: Border*` — frame decoration object
- `m_transient: Window` — transient-for hint
- `m_state: int` — Withdrawn/Normal/Iconic
- `m_sizeHints: XSizeHints` — size constraints
- `m_fixedSize: Boolean` — whether window has min==max size
### Border (`Border.h`, `Border.C`)
- Frame window creation and management
- X Shape extension for shaped borders/tabs
- Drawing tab labels (rotated text via xvertext)
- Reparenting client into frame
- Resize handle
- Configure/move operations on frame
- `m_parent: Window` — frame window
- `m_tab: Window` — tab on left side
- `m_button: Window` — button at top of tab (hide/delete)
- `m_resize: Window` — resize handle at bottom-right corner
### Rotated (`Rotated.h`, `Rotated.C`)
### listmacro2.h
## Event Flow
```
```
## Window Lifecycle
```
```
## Data Flow
```
```
## Atoms Used
- `WM_STATE` — client state property
- `WM_CHANGE_STATE` — state change requests
- `WM_PROTOCOLS` — protocol negotiation
- `WM_DELETE_WINDOW` — graceful close
- `WM_TAKE_FOCUS` — focus handling
- `WM_COLORMAP_WINDOWS` — multi-colormap support
- `_WM2_RUNNING` — selection ownership (WM presence check)
## Build Flow
```
```
## Dependencies Between Components
- `Main.C` → `Manager.h` → `Client.h` → `Border.h` → `Rotated.h` → `General.h` → `Config.h`
- `Client` and `Border` have circular dependency (Border calls `client->isTransient()`, Client calls `border->hasWindow()`) — resolved via forward declarations and friend-like access
- `WindowManager` is passed as const pointer to all Client/Border constructors — they access global state through it
- `listmacro2.h` is used only by `Manager.h` for `ClientList`
## Key Architectural Constraints
<!-- GSD:architecture-end -->

<!-- GSD:skills-start source:skills/ -->
## Project Skills

No project skills found. Add skills to any of: `.claude/skills/`, `.agents/skills/`, `.cursor/skills/`, `.github/skills/`, or `.codex/skills/` with a `SKILL.md` index file.
<!-- GSD:skills-end -->

<!-- GSD:workflow-start source:GSD defaults -->
## GSD Workflow Enforcement

Before using Edit, Write, or other file-changing tools, start work through a GSD command so planning artifacts and execution context stay in sync.

Use these entry points:
- `/gsd-quick` for small fixes, doc updates, and ad-hoc tasks
- `/gsd-debug` for investigation and bug fixing
- `/gsd-execute-phase` for planned phase work

Do not make direct repo edits outside a GSD workflow unless the user explicitly asks to bypass it.
<!-- GSD:workflow-end -->



<!-- GSD:profile-start -->
## Developer Profile

> Profile not yet configured. Run `/gsd-profile-user` to generate your developer profile.
> This section is managed by `generate-claude-profile` -- do not edit manually.
<!-- GSD:profile-end -->
