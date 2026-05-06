# Coding Conventions

**Analysis Date:** 2026-05-06

## Overview

wm2 is a 1997-era C++ codebase (pre-standard C++, targeting early g++). It uses a C++ class structure layered over C-style Xlib programming. There are no linters, formatters, or modern C++ idioms. The code is formatted with tabs and follows early-1990s C++ conventions.

## Naming Patterns

**Files:**
- Implementation files use `.C` extension (not `.cpp` or `.cc`)
- Header files use `.h` extension
- Files are named after the primary class or concept they contain: `Client.C`/`Client.h`, `Manager.C`/`Manager.h`, `Border.C`/`Border.h`
- `General.h` is the catch-all precompiled header
- `Config.h` contains compile-time configuration
- `Cursors.h` contains X11 bitmap data (cursor definitions)
- `listmacro2.h` contains generic list macros (template substitute)
- `Rotated.C`/`Rotated.h` are third-party xvertext font rotation library

**Classes:**
- PascalCase: `WindowManager`, `Client`, `Border`, `BorderRectangleList`, `Atoms`
- One class per header/implementation pair (except `Atoms` which is a static-only utility class in `General.h`)
- `BorderRectangleList` derives from a macro-generated `RectangleList` base in `Border.C:24`

**Functions/Methods:**
- camelCase: `windowToClient()`, `mapRaised()`, `installColormap()`, `fixTabHeight()`
- Multi-word names are concatenated without separators: `eventMapRequest()`, `setFrameVisibility()`
- Static helper functions in `.C` files use camelCase: `getProperty_aux()` in `Client.C:375`, `makeCursor()` in `Manager.C:211`, `nobuttons()` in `Buttons.C:95`

**Variables (member):**
- Prefix `m_` followed by camelCase: `m_activeClient`, `m_display`, `m_tabWidth`, `m_windowManager`
- Static members also use `m_` prefix: `m_initialising`, `m_signalled`, `m_tabFont`
- Declared in `private:` or `protected:` sections of headers

**Variables (local and parameter):**
- Short, descriptive: `c` for Client pointers, `e` for events, `d` for Display pointers, `w` for windows
- Loop counters: `i`, `j`, `n`
- No consistent prefix or suffix for parameters vs locals

**Macros and Constants:**
- ALL_CAPS with underscores: `CONFIG_CLICK_TO_FOCUS`, `TAB_TOP_HEIGHT`, `FRAME_WIDTH`
- `#define` constants in `Config.h`, `Border.h`, `General.h`, `Buttons.C`
- Boolean values use Xlib's `True`/`False` (capitalized), not C++ `true`/`false`

**Types:**
- `Boolean` is a typedef for `char` (defined in `General.h:33`): `typedef char Boolean;`
- Xlib types used directly: `Window`, `Display`, `Colormap`, `Cursor`, `GC`, `Atom`
- Protocol flags use `#define`: `Pdelete`, `PtakeFocus` in `Client.h:142-144`

## Code Style

**Formatting:**
- No automated formatting tool configured
- Tab indentation (appears as 4-space display width)
- Opening braces on same line as control statement (K&R/1TBS style):
  ```c++
  if (c) c->eventMapRequest(e);
  else {
      fprintf(stderr, "wm2: bad map request for window %lx\n", e->window);
  }
  ```
- Single-line bodies often placed on same line without braces:
  ```c++
  if (!m_display) fatal("can't open display");
  ```
- Short inline methods defined in headers without braces:
  ```c++
  WindowManager *windowManager() { return m_windowManager; }
  ```

**Indentation quirks:**
- Some code uses tab+space combinations for alignment
- Switch cases are indented one level from `switch`
- `private:`/`public:` access specifiers are not indented relative to class

**Line length:**
- No enforced limit; some lines exceed 80 characters
- Long function calls are split across lines with aligned arguments

**Semicolons and whitespace:**
- Trailing whitespace present in many files
- No trailing newline enforcement

**Linting:**
- No linter configured (`.clang-format`, `.clang-tidy`, `CPPLINT.cfg` absent)
- Makefile uses `-Wall` flag only (`Makefile:7`): `CFLAGS = -O2 -Wall $(INCS)`

## C++ Dialect and Idioms

**Language version:**
- Pre-standard C++ (targets g++ circa 1997)
- No namespaces, no STL, no templates, no exceptions, no RTTI
- No `const_cast`, `static_cast`, `reinterpret_cast` -- uses C-style casts throughout:
  ```c++
  m_items = (T *)realloc(m_items, (m_count + 1) * sizeof(T));
  ```
- No `nullptr`; uses `0` or `NULL` for null pointers

**Memory management:**
- Raw `new`/`delete` with no smart pointers
- `malloc()`/`free()` used alongside `new`/`delete`:
  - `new`/`delete` for class instances: `new Client(this, w)` in `Manager.C:410`, `new Border(this, w)` in `Client.C:39`
  - `malloc()`/`free()` for strings and buffers: `NewString()` macro in `General.h:34`, `malloc()` in `Client.C:580`
  - Xlib allocations freed with `XFree()`: `XFree(m_iconName)` in `Client.C:87`
- `NewString(x)` macro wraps `malloc`+`strcpy` for string duplication (`General.h:34`)

**Container pattern:**
- No STL containers
- Uses macro-based generic lists from `listmacro2.h`:
  ```c++
  // Declaration (header):
  declarePList(ClientList, Client);

  // Implementation (source):
  implementPList(ClientList, Client);
  ```
- List macros use `realloc()` for growth and `memcpy()` for element shifting

**Class design:**
- No inheritance hierarchy (except `BorderRectangleList : RectangleList` in `Border.C:24`)
- Friendship not formally declared; `Border` accesses `Client` internals through accessor delegation
- Destructor is `protected` in `Client` to prevent stack deletion (`Client.h:84-85`): comment says "cravenly submitting to gcc's warnings"
- Self-deletion pattern: `Client::release()` calls `delete this` at `Client.C:91`
- `const` correctness is inconsistent; `const` used on some parameters but not method declarations

**Static initialization:**
- Class-level static members initialized at file scope in `.C` files:
  ```c++
  int     WindowManager::m_signalled = False;    // Manager.C:18
  Boolean WindowManager::m_initialising = False;  // Manager.C:19
  ```

## Import Organization

**Order:**
1. Project's own headers (first include in each `.C` file)
2. Standard C library headers (`<string.h>`, `<stdio.h>`, etc.)
3. X11 headers (`<X11/Xlib.h>`, `<X11/Xutil.h>`, etc.)
4. X11 extension headers (`<X11/extensions/shape.h>`)
5. Third-party project headers (`"Rotated.h"`)
6. Local headers (`"Client.h"`, `"Cursors.h"`)

**Pattern in `.C` files:**
```c++
#include "Manager.h"          // own header or primary header
#include "Client.h"           // related class header
#include <string.h>           // C standard library
#include <X11/Xproto.h>       // X11 protocol
#include "Cursors.h"          // project data header
```

**Pattern in `.h` files:**
```c++
#include "General.h"          // base includes (X11, standard C)
#include "listmacro2.h"       // utility macros
```

**Header guards:**
- Use `#ifndef _NAME_H_` / `#define _NAME_H_` / `#endif` pattern
- Guard names: `_GENERAL_H_`, `_MANAGER_H_`, `_CLIENT_H_`, `_BORDER_H_`, `_CONFIG_H_`, `_LISTMACRO_H_`
- `Rotated.h` uses `_XVERTEXT_INCLUDED_`
- `Cursors.h` uses `_WM2_CURSORS_H_`

**Path aliases:**
- None; all includes use relative paths with `"quotes"`

**Forward declarations:**
- Used to break circular dependencies: `class Client;` in `Manager.h:12`, `class Client;` and `class WindowManager;` in `Border.h:9-10`

## Error Handling

**Fatal errors:**
- `WindowManager::fatal()` in `Manager.C:172-178` prints to stderr via `perror()`, then calls `exit(1)`
- Used for unrecoverable conditions: display open failure, missing font, missing shape extension
- `Client::fatal()` and `Border::fatal()` delegate to `WindowManager::fatal()`

**X11 error handler:**
- Static `WindowManager::errorHandler()` in `Manager.C:181-208` handles X11 protocol errors
- During initialization: exits on `BadAccess` (another WM running) or any error
- During runtime: logs to stderr, returns 0 (continues)
- Global `ignoreBadWindowErrors` flag (`General.h:63`) suppresses `BadWindow` errors during cleanup

**Non-fatal warnings:**
- `fprintf(stderr, "wm2: warning: ...")` for recoverable issues
- Used for bad parent windows, self-referencing transients, message send failures
- No structured warning system

**X11 error suppression pattern:**
```c++
ignoreBadWindowErrors = True;
XSync(display(), False);
ignoreBadWindowErrors = False;
```
Used in `Client.C:689-691` and `Manager.C:468-470` to suppress expected errors during window destruction.

**Signal handling:**
- `SIGTERM`, `SIGINT`, `SIGHUP` handled via `sigaction()` (wrapped by `signal()` macro in `General.h:41-48`)
- Signal handler sets static flag `m_signalled`; event loop checks flag and exits cleanly
- `sigHandler()` in `Manager.C:373-376` is minimal (sets flag only)

**Memory allocation failures:**
- `Rotated.C` uses `exit(1)` on malloc failure (lines 49, 73)
- `listmacro2.h` uses `assert()` on realloc failure (line 54)
- Main code generally does not check malloc return values

**Return value checking:**
- Xlib calls generally not checked for errors (X11 error handler deals with them asynchronously)
- `XSendEvent()` return checked in `Client.C:369-371`
- `XGrabPointer()` return checked in `Buttons.C:195`, `Buttons.C:357-359`, `Buttons.C:468-470`

## Logging

**Framework:** Direct `fprintf(stderr, ...)` and `perror()`

**Prefix convention:**
- All log messages prefixed with `"wm2: "` for identification
- Warnings use `"wm2: warning: "` prefix
- Example: `fprintf(stderr, "wm2: bad map request for window %lx\n", e->window);`

**Diagnostic output:**
- Startup banner printed to stderr in `WindowManager::WindowManager()` constructor (`Manager.C:30-73`)
- Runtime diagnostics printed to stderr for unexpected events and states
- Debug logging commented out with `//` throughout (search for `//    fprintf`): `Client.C:55`, `Client.C:64`, `Client.C:288`, `Client.C:406`

**Logging categories:**
- Startup configuration info
- Runtime warnings and errors
- Debug traces (commented out)
- Third-party code (`Rotated.C`) has its own error messages without `wm2:` prefix

## Comments

**When to Comment:**
- Inline `//` comments explain intent, gotchas, and hacks
- Block `/* */` comments used for file headers (Rotated.C/Rotated.h) and section dividers
- Section divider pattern uses fixed-width boxes:
  ```c++
  /* ---------------------------------------------------------------------- */
  ```

**Common comment patterns:**
- Acknowledgment of code quality issues: `// this is all a bit of a hack` (`Events.C:623`)
- Self-deprecating notes: `// cravenly submitting to gcc's warnings` (`Client.h:84`)
- Implementation notes: `// assumes efficient realloc()` (`listmacro2.h:8`)
- Cross-file references: `// in Buttons.C` (`Border.h:48`), `// in Events.C of all places` (`Border.h:58`)
- Attribution: `// strange code thieved from 9wm to avoid leaving zombies` (`Manager.C:534`)
- Design commentary: `// you could probably change these a certain amount before breaking the shoddy code` (`Border.h:11-13`)

**No documentation:**
- No JSDoc/TSDoc/Doxygen style documentation
- No parameter or return value documentation
- Header files serve as the interface documentation via method names and brief inline comments

## Function Design

**Size:**
- Functions range from 1 line (accessors) to ~170 lines (`WindowManager::menu()` in `Buttons.C:130-324`)
- Large functions are common; the menu handler, move, and resize operations are long inline event loops
- No refactoring pressure evident; long functions are the norm

**Parameters:**
- X11 event structs passed as pointers: `void eventButton(XButtonEvent *)`
- Boolean parameters for behavior flags: `void resize(XButtonEvent *, Boolean horizontal, Boolean vertical)`
- Default parameter values used sparingly: `Client *windowToClient(Window, Boolean create = False)` in `Manager.h:21`
- `const` applied inconsistently to pointer parameters: `const char *` in some places, `char *` in others

**Return Values:**
- `Boolean` (typedef for `char`) for success/failure and state queries: `Boolean isHidden()`, `Boolean getState(int *)`
- Raw pointers for object lookups (may return `0`/`NULL`): `Client *windowToClient(Window, Boolean)`
- `void` for operations that always succeed or handle their own errors
- Direct return of X11 types: `Window parent()`, `Display *display()`

**Inline methods:**
- Trivial accessors defined in header files:
  ```c++
  Display *display()     { return m_windowManager->display();      }
  Window parent()        { return m_border->parent();              }
  Boolean isActive()     { return (activeClient() == this);        }
  ```
- More complex methods declared in headers, defined in `.C` files

## Module Design

**Exports:**
- Each `.h` file exports one class (or set of related definitions)
- No barrel files or aggregate headers (except `General.h` which aggregates system includes and `Config.h`)
- `General.h` is the closest thing to a precompiled header: it includes all X11 headers, POSIX headers, and project-wide definitions

**Barrel Files:**
- Not used
- Each `.C` file includes only the headers it needs

**Class separation by concern:**
- `WindowManager` (`Manager.h`/`Manager.C`): Top-level WM lifecycle, event loop, client list management
- `Client` (`Client.h`/`Client.C`): Individual client window state, window management operations
- `Border` (`Border.h`/`Border.C`): Window frame decoration, shaping, rendering
- Events are split across files by interaction mode:
  - `Events.C`: Core event dispatch and property/colormap/focus events
  - `Buttons.C`: Button-driven interactions (move, resize, menu, hide, kill)
- `Rotated.C`/`Rotated.h`: Third-party font rotation (xvertext 2.0)
- `Main.C`: Entry point only (20 lines)

**Cross-file method placement:**
- `Border::coordsInHole()` declared in `Border.h:58` but implemented in `Events.C:623` (noted in header comment)
- `Border::eventButton()` declared in `Border.h:48` but implemented in `Buttons.C:615` (noted in header comment)

## Preprocessor Conventions

**`Config.h` compile-time configuration:**
- All configuration is done via `#define` constants
- Configuration is documented as requiring source edit + recompile
- Boolean config uses `True`/`False` (Xlib Boolean values)
- `CONFIG_` prefix for all config macros
- Conditional compilation for platform quirks: `#ifdef sgi` (`Manager.C:526`), `#ifdef hpux` (`Events.C:139`), `#ifndef __FreeBSD__` (`General.h:13`)

**Macro-based generics:**
- `listmacro2.h` provides `declareList`/`implementList` and `declarePList`/`implementPList` macros as a template substitute
- Token pasting used: `T##_pointer` in `listmacro2.h:10-11`
- Used for: `ClientList` (pointer list of Client objects), `RectangleList` (value list of XRectangle)

**Signal macro:**
- `General.h:41-48` redefines `signal()` as a macro wrapping `sigaction()` for portability

---

*Convention analysis: 2026-05-06*
