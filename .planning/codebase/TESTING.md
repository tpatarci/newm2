# Testing Patterns

**Analysis Date:** 2026-05-06

## Overview

wm2 has **no test infrastructure whatsoever**. There are no test files, no test frameworks, no test targets in the Makefile, and no testing patterns of any kind. This is consistent with the codebase's origin as a 1997 single-developer X11 window manager, where testing was entirely manual and runtime-based.

## Test Framework

**Runner:**
- None. No test framework is present or referenced.

**Assertion Library:**
- Production code uses `assert()` from `<assert.h>` in `listmacro2.h:5,25,59` for list bounds checking
- These are runtime assertions in the shipped code, not test assertions

**Run Commands:**
- Not applicable

## Test File Organization

**Location:**
- No test files exist anywhere in the repository
- `find` for `*test*`, `*spec*`, `*Test*`, `*Spec*` patterns returns zero results

**Naming:**
- Not applicable

**Structure:**
```
upstream-wm2/
├── (no test directories or files exist)
```

## What Would Need Testing

Given the codebase's architecture, these are the areas that have no test coverage:

### Untested Units

**List macro container (`listmacro2.h`):**
- `append()`, `remove()`, `remove_all()`, `move_to_start()`, `item()`, `count()`
- Generic via macros; no type-specific tests possible without instantiating
- Runtime `assert()` provides minimal bounds checking

**Client state management (`Client.C`):**
- State transitions: `WithdrawnState` -> `NormalState` -> `IconicState` and back
- `manage()`, `hide()`, `unhide()`, `withdraw()`, `activate()`, `deactivate()`
- `gravitate()` coordinate transformation for all 9 gravity values
- `fixResizeDimensions()` size hint enforcement
- `setLabel()` label selection and truncation logic
- All require a running X11 display to test

**Border shaping and geometry (`Border.C`):**
- `shapeParent()`, `shapeTab()`, `shapeResize()` -- X11 shape extension operations
- `fixTabHeight()` -- label truncation and tab sizing
- `configure()` -- window creation and resizing
- All require X11 display with shape extension

**WindowManager event dispatch (`Events.C`):**
- Event type routing to correct handler
- `nextEvent()` -- select-based event loop with signal handling
- Focus change delay logic (`considerFocusChange()`, `checkDelaysForFocus()`)
- Requires X11 display and synthetic events

**Menu interaction (`Buttons.C`):**
- Menu layout, item selection, edge detection for "Exit wm2"
- Client hide/kill/move/resize state machines
- Requires X11 display and synthetic events

**Rotated font rendering (`Rotated.C`):**
- `XRotLoadFont()`, `XRotTextWidth()`, `XRotDrawString()`
- Third-party xvertext 2.0 code
- Requires X11 display with font server

## Barriers to Testing

### Hard Dependency on X11 Display

Every significant component in wm2 requires a live X11 display connection:

- `WindowManager` constructor (`Manager.C:27-117`) calls `XOpenDisplay()` and exits fatally if it fails
- `Client` constructor (`Client.C:11-42`) calls `XGetWindowAttributes()` on the passed window
- `Border` constructor (`Border.C:41-90`) calls `XRotLoadFont()`, `XCreateGC()`, `XAllocNamedColor()`
- Event handling functions receive X11 event structs that reference X11 resources

The `fatal()` method in `WindowManager` (`Manager.C:172-178`) calls `exit(1)` directly, making it impossible to test error paths in-process.

### No Dependency Injection

- `Client` holds `WindowManager *const m_windowManager` (`Client.h:125`) and calls its methods directly
- `Border` holds `Client *const m_client` (`Border.h:61`) and delegates through it
- No interfaces, no abstract base classes, no virtual methods that could be overridden for testing
- X11 function calls are made directly, not through a wrapper layer

### No Separation of Logic from I/O

- Business logic (coordinate calculation, state machine transitions) is interleaved with X11 calls
- Example: `Client::gravitate()` (`Client.C:455-525`) mixes pure math (gravity offset calculation) with X11 state access
- Example: `Client::fixResizeDimensions()` (`Buttons.C:437-461`) is pure logic but requires X11 context to reach

## Strategies for Adding Tests

If testing were to be introduced, these approaches would be necessary:

### Approach 1: Xvfb + Integration Tests

Use Xvfb (X Virtual Framebuffer) to provide a headless X11 display:

```bash
Xvfb :99 &
export DISPLAY=:99
# Run tests against wm2 on virtual display
```

This enables integration-level testing of the full window manager but requires:
- Starting/stopping Xvfb for each test run
- Creating test windows via Xlib from test code
- Observing wm2 behavior through X11 protocol queries

### Approach 2: X11 Abstraction Layer

Introduce an abstraction layer between wm2 and Xlib to allow mocking:

```c++
// Proposed interface
class X11Interface {
public:
    virtual Display* openDisplay(const char*) = 0;
    virtual Window createWindow(...) = 0;
    virtual void mapWindow(Window) = 0;
    // ... wrap all used X11 calls
};
```

This is a significant refactoring effort -- the codebase makes dozens of distinct X11 calls.

### Approach 3: Extract Pure Logic

Extract testable pure functions from the existing interleaved code:

- `Client::fixResizeDimensions()` in `Buttons.C:437-461` -- already nearly pure
- `Client::gravitate()` coordinate math in `Client.C:455-525` -- pure math with X11 state access
- `Border::fixTabHeight()` in `Border.C:169-207` -- string truncation logic
- `Client::setLabel()` in `Client.C:528-548` -- label selection logic

These could be extracted to free functions and tested independently.

## Testing for Modifications to This Codebase

When modifying wm2, the practical testing approach is:

1. **Compile-test first**: `make` in `upstream-wm2/` -- the `-Wall` flag in `Makefile:7` catches many issues
2. **Manual runtime test**: Run wm2 under Xvfb or a real X11 session
3. **Test pure logic in isolation**: For any extracted pure functions, add standalone test programs
4. **Validate X11 behavior**: Use tools like `xwininfo`, `xdotool`, `xev` to verify window manager behavior

## Build Verification

The only automated verification available:

```bash
cd upstream-wm2
make clean && make    # Compile with -Wall
```

The Makefile (`Makefile:16-17`):
```makefile
wm2:	$(OBJECTS)
		$(CCC) -o wm2 $(OBJECTS) $(LIBS)
```

No `test` or `check` target exists. The available targets are:
- `wm2` (default) -- build the binary
- `depend` -- regenerate dependencies
- `clean` -- remove object files
- `distclean` -- remove all generated files

## Coverage

**Requirements:** None enforced

**Actual coverage:** 0% -- no tests exist

**View Coverage:** Not applicable

## Test Types

**Unit Tests:**
- Not present
- Difficult to add due to X11 coupling and lack of dependency injection
- Best candidates for unit testing: `fixResizeDimensions()`, `gravitize()` math, `setLabel()`, `fixTabHeight()` string logic

**Integration Tests:**
- Not present
- Would require Xvfb or similar virtual display
- Could test: window creation, focus changes, move/resize, menu interaction

**E2E Tests:**
- Not present
- Would require full X11 session with window manager running
- Could use `xdotool` or similar to simulate user interactions

## Common Patterns

**Async Testing:**
- Not applicable (no async test framework)

**Error Testing:**
- Not applicable (no test framework)
- Production error paths use `fprintf(stderr, ...)` + `exit(1)`, which is hostile to automated testing

---

*Testing analysis: 2026-05-06*
