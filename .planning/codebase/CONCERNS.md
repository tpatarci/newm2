# Codebase Concerns

**Analysis Date:** 2026-05-06

## Tech Debt

### Pre-Standard C++ Patterns (1997 era)

- Issue: The entire codebase uses pre-standard C++ idioms that will not compile with modern compilers without significant adaptation.
- Files: All `.C` and `.h` files in `upstream-wm2/`
- Impact: Code cannot be built with C++11 or later compilers. The Makefile uses `g++` with only `-O2 -Wall`, no standard version flag.
- Fix approach: Modernize incrementally -- add `-std=c++98` initially to establish a baseline, then port to C++11+.

**Specific pre-standard issues:**

1. **Macro-based generic list** (`upstream-wm2/listmacro2.h`): Uses `declareList`/`implementList` macros to generate container classes instead of templates. Each "instantiation" creates a new class via macro expansion. Uses raw `malloc`/`realloc`/`memcpy` internally.

2. **`Boolean` typedef as `char`** (`upstream-wm2/General.h:33`): `typedef char Boolean;` instead of `bool`. Uses `True`/`False` instead of `true`/`false`.

3. **`#define signal(...)` macro** (`upstream-wm2/General.h:41-48`): Redefines the `signal()` function as a macro that calls `sigaction`. This interferes with namespace cleanliness and is fragile.

4. **`NewString` macro** (`upstream-wm2/General.h:34`): `#define NewString(x) (strcpy((char *)malloc(strlen(x)+1),(x)))` -- combines allocation and copy in one expression. No NULL check on malloc return value; crash on out-of-memory.

5. **C-style casts everywhere**: `(char *)malloc(...)`, `(int *) 0`, `(unsigned char **)`, `(XEvent *)` etc. should be C++ style casts.

6. **`wait((int *) 0)`** (`upstream-wm2/Manager.C:569`): Uses obsolete `union wait`-era calling convention. Modern code should use `waitpid()` with `int status`.

### Hardcoded X11R6 Library Paths

- Issue: The Makefile hardcodes paths to `/usr/X11R6/lib` and `/usr/X11R6/include`, which do not exist on modern Linux distributions (X11 libs are now in `/usr/lib/x86_64-linux-gnu/` or similar).
- Files: `upstream-wm2/Makefile:2-3`
- Impact: Build fails immediately on any modern system without manual path correction.
- Fix approach: Use `pkg-config --libs x11 xext xmu` and `pkg-config --cflags x11` in the Makefile, or use `/usr/lib` and `/usr/include/X11` as fallback paths.

### Configuration by Source Editing

- Issue: All configuration (fonts, colors, focus policy, delays) is done via `#define` macros in `upstream-wm2/Config.h`. There is no runtime configuration file, no X resource support, and no command-line options.
- Files: `upstream-wm2/Config.h`
- Impact: Any user-visible change requires recompilation. No per-user customization possible.
- Fix approach: Add X resource or config file support. Parse a `~/.wm2rc` file at startup.

### No `.C` File Extension Recognition

- Issue: Source files use `.C` extension which some build systems and tools do not recognize as C++.
- Files: All implementation files in `upstream-wm2/`
- Impact: IDEs, `ctags`, and other tooling may not identify these as C++ files.
- Fix approach: Rename to `.cpp` or `.cc` during modernization.

## Known Bugs

### Dead Code in `stopConsideringFocus`

- Symptoms: The focus-deselection code in `stopConsideringFocus()` never executes its cleanup branch.
- Files: `upstream-wm2/Events.C:233-241`
- Trigger: Any focus change when `CONFIG_AUTO_RAISE` is enabled.
- Workaround: None; the effect is that `PointerMotionMask` is never removed from windows after auto-raise focus changes.
- Detail: Line 237 sets `m_focusChanging = False`, then line 238 checks `if (m_focusChanging && ...)` which is always `False`. The condition should have been checked before the assignment, or the order should be swapped.

### `circulate()` Can Walk Off End of Client List

- Symptoms: Potential array bounds violation when cycling through clients.
- Files: `upstream-wm2/Buttons.C:36-64`
- Trigger: Invoked via right-click (Button3) when all clients are transient or not in NormalState.
- Workaround: In practice, `j` wraps via `j = -1` and the `j == i` guard catches it, but the loop at line 53-58 accesses `m_clients.item(j)` *before* checking if `j` is in range.
- Detail: The `for` loop increments `j` from `i+1`, and the body accesses `m_clients.item(j)` before the `if (j >= m_clients.count() - 1) j = -1` check on the next iteration. If `j` starts at `m_clients.count()-1`, the first access is valid but the loop condition `j == i` may never trigger if `i == -1`, causing infinite loop potential.

### `setLabel()` Always Returns True

- Symptoms: Window borders are redrawn even when the label has not changed, causing unnecessary X server traffic.
- Files: `upstream-wm2/Client.C:528-548`
- Trigger: Any `WM_NAME` or `WM_ICON_NAME` property change.
- Workaround: None.
- Detail: Line 547 shows `} else return True;//False;// dammit!` -- the original developer clearly intended to return `False` when the label is unchanged, but was forced to return `True` (likely due to a bug it triggered elsewhere). This means `rename()` is called on every property change regardless of whether the name actually changed.

### Screen Index Hardcoded to Zero

- Symptoms: Multi-monitor or multi-screen setups are completely unsupported.
- Files: `upstream-wm2/Client.C:201`, `upstream-wm2/Client.C:720-721`, `upstream-wm2/Manager.C:232-233`
- Trigger: Running on any system with more than one X screen.
- Workaround: None. README explicitly states "wm2 does NOT support multi-screen displays."
- Detail: `DisplayWidth(display(), 0)` and `DisplayHeight(display(), 0)` hardcode screen 0. `initialiseScreen()` also hardcodes `int i = 0` at line 232.

## Security Considerations

### Buffer Overflow: `sprintf` into Fixed-Size Buffers

- Risk: Multiple `sprintf` calls write into stack buffers without bounds checking. Malicious or unexpected X server responses could overflow these buffers.
- Files:
  - `upstream-wm2/Buttons.C:330`: `char string[20]; sprintf(string, "%d %d\n", x, y);` -- coordinates can be large integers (e.g., > 5 digits each), exceeding 20 bytes.
  - `upstream-wm2/Manager.C:192-198`: `char msg[100], number[30], request[100];` with `sprintf` calls in the error handler.
  - `upstream-wm2/Manager.C:323-324`: `char error[100]; sprintf(error, "couldn't load %s colour", desc);` -- if `desc` is long, overflow.
  - `upstream-wm2/Manager.C:549`: `sprintf(pstring, "DISPLAY=%s", displayName);` -- `DISPLAY` strings can be arbitrarily long.
- Current mitigation: None.
- Recommendations: Replace all `sprintf` with `snprintf`, or use `std::string` concatenation.

### Buffer Overflow: `showGeometry` Coordinate String

- Risk: The geometry display string buffer is only 20 bytes but must hold two decimal integers plus space, newline, and null terminator.
- Files: `upstream-wm2/Buttons.C:329-330`
- Current mitigation: None.
- Recommendations: Use `snprintf(string, sizeof(string), "%d %d\n", x, y)` at minimum.

### Unsafe `NewString` Macro

- Risk: The `NewString` macro in `upstream-wm2/General.h:34` calls `malloc` then `strcpy` in a single expression. If `malloc` returns `NULL`, `strcpy` will crash with a NULL destination pointer.
- Files: `upstream-wm2/General.h:34`, used in `upstream-wm2/Client.C:38`, `upstream-wm2/Client.C:538`, `upstream-wm2/Client.C:544`, `upstream-wm2/Border.C:175`, `upstream-wm2/Manager.C:79`
- Current mitigation: None.
- Recommendations: Replace with a function that checks the `malloc` return value.

### `malloc` Return Values Not Checked

- Risk: Numerous `malloc`/`realloc`/`calloc` calls throughout the codebase do not check for `NULL` return.
- Files: `upstream-wm2/Border.C:192`, `upstream-wm2/Client.C:580`, `upstream-wm2/Manager.C:548`, `upstream-wm2/listmacro2.h:50-52` (only `assert`, which is compiled out with `-DNDEBUG`)
- Current mitigation: `listmacro2.h:54` uses `assert(m_items)` which is removed in release builds.
- Recommendations: Add explicit NULL checks after all allocations. Replace `assert(m_items)` with a proper error-handling path.

### Double-Free Potential in `Border::fixTabHeight`

- Risk: `m_label` is freed on line 174, then freed again on line 184 if the first label does not fit. If the `NewString` on line 175 returns the same pointer (impossible in practice with malloc, but logically risky pattern), this would be a double-free.
- Files: `upstream-wm2/Border.C:174-184`
- Current mitigation: `malloc` will return a new pointer each time. However, the pattern is fragile.
- Recommendations: Set `m_label = NULL` immediately after each `free(m_label)`.

### `delete this` in `Client::release()`

- Risk: `Client::release()` calls `delete this` at line 91. After `delete this`, the method accesses `this->m_window` implicitly through the still-executing function. The object's memory may already be reused.
- Files: `upstream-wm2/Client.C:51-92`
- Current mitigation: The `delete this` is the last statement in the method (line 91), so no further member access occurs after it. However, this is undefined behavior in C++.
- Recommendations: Refactor so that the caller (WindowManager) deletes the Client object, not the object itself.

## Performance Bottlenecks

### O(n) Client Lookup on Every Event

- Problem: `windowToClient()` performs a linear scan of all clients on every X event, checking `hasWindow()` for each. `hasWindow()` itself checks multiple windows.
- Files: `upstream-wm2/Manager.C:397-414`
- Cause: Linear search through `m_clients` list. No hash table or map.
- Impact: With many clients, every event (motion, expose, property change) triggers O(n) lookups.
- Improvement path: Replace with `std::unordered_map<Window, Client*>` keyed by all managed Window IDs.

### `realloc` on Every List Append/Remove

- Problem: The macro-based list in `listmacro2.h` calls `realloc` on every `append` and every `remove`, growing or shrinking by exactly one element.
- Files: `upstream-wm2/listmacro2.h:49-63`
- Cause: No amortized growth strategy. The list allocates exactly `count * sizeof(T)` bytes.
- Impact: Adding/removing N clients causes N `realloc` calls and N `memcpy` operations to shift elements.
- Improvement path: Replace with `std::vector<Client*>` which uses geometric growth.

### Spin-Wait Loops with `select` Sleep

- Problem: The move, resize, and button event loops use `select(0,0,0,0,&sleepval)` with 50ms timeout as a polling mechanism when no events are available.
- Files: `upstream-wm2/Buttons.C:381-383`, `upstream-wm2/Buttons.C:508-510`, `upstream-wm2/Buttons.C:676-678`
- Cause: The loops drain all pending events with `XCheckMaskEvent`, then sleep 50ms, then poll again.
- Impact: Wastes CPU cycles during interactive operations. 50ms polling adds input latency.
- Improvement path: Use `XMaskEvent` (blocking) instead of `XCheckMaskEvent` + `select` sleep, or use `XPending` with proper blocking.

### Rotated Font Bitmap Creation

- Problem: `XRotLoadFont` creates individual X Pixmaps for each of 95 printable characters, doing pixel-level bitmap rotation in C.
- Files: `upstream-wm2/Rotated.C:124-337`
- Cause: The xvertext 2.0 library pre-renders every character at the desired rotation angle.
- Impact: Slow startup (95 XCreatePixmap + XGetSubImage + XPutImage calls). Memory overhead of 95 pixmaps per font.
- Improvement path: Use Xft or modern font rendering with XRender extension instead of bitmap rotation.

## Fragile Areas

### Border Shape Management

- Files: `upstream-wm2/Border.C` (746 lines), `upstream-wm2/Border.h`
- Why fragile: The shaped window borders use manual XShape rectangle lists with hardcoded offsets derived from `TAB_TOP_HEIGHT`, `FRAME_WIDTH`, and `m_tabWidth`. The comment on `upstream-wm2/Border.h:12-13` explicitly warns: "You could probably change these a certain amount before breaking the shoddy code in the rest of this file."
- Safe modification: Do not change `TAB_TOP_HEIGHT`, `FRAME_WIDTH`, or `TRANSIENT_FRAME_WIDTH` without thorough testing. The rectangle lists in `shapeParent()`, `shapeTab()`, `setFrameVisibility()`, and `resizeTab()` all depend on precise geometric relationships.
- Test coverage: None. No automated tests exist.

### Event Dispatch Switch Statement

- Files: `upstream-wm2/Events.C:6-115` (`WindowManager::loop()`)
- Why fragile: Large switch statement handles all X events. Adding a new event type requires modifying this switch. Shaped window events are explicitly not supported (line 104-105 prints an error and ignores them).
- Safe modification: Add new `case` labels at the end. Do not modify existing case ordering.
- Test coverage: None.

### Client Lifecycle Management

- Files: `upstream-wm2/Client.C`, `upstream-wm2/Manager.C`
- Why fragile: Client objects are allocated with `new` but deleted via `Client::release()` calling `delete this`. The WindowManager maintains raw pointers to Client objects in a list. If `release()` is called twice, `m_window == None` check prints a warning but does not prevent further damage.
- Safe modification: Ensure `release()` is only ever called once per Client. Consider using `std::unique_ptr` or a similar ownership mechanism.
- Test coverage: None.

### goto in Event Loop

- Files: `upstream-wm2/Events.C:127-174`
- Why fragile: `nextEvent()` uses `goto waiting` to implement a polling loop with `select()`. The control flow is hard to reason about, especially with signal handling (`m_signalled`) interleaved.
- Safe modification: Rewrite as a structured `while` loop.
- Test coverage: None.

## Scaling Limits

### Single-Screen Only

- Current capacity: Exactly one X screen (index 0).
- Limit: `initialiseScreen()` hardcodes `int i = 0` for screen selection. All `DisplayWidth`/`DisplayHeight` calls use screen 0. No XRandR/Xinerama awareness.
- Scaling path: Implement proper multi-screen support using `XScreenCount()`, `ScreenCount()` macro, and XRandR for dynamic monitor configuration.

### No EWMH/ICCCM Compliance Beyond Basics

- Current capacity: Handles only `WM_STATE`, `WM_PROTOCOLS` (DELETE_WINDOW, TAKE_FOCUS), `WM_COLORMAP_WINDOWS`, `WM_TRANSIENT_FOR`, `WM_NAME`, `WM_ICON_NAME`, `WM_HINTS`, `WM_NORMAL_HINTS`.
- Limit: No `_NET_ACTIVE_WINDOW`, `_NET_CLIENT_LIST`, `_NET_CURRENT_DESKTOP`, `_NET_WM_STATE`, `_NET_SUPPORTED`, or any other EWMH hints. Pager and taskbar applications cannot interact with wm2. No `_NET_WM_WINDOW_TYPE` support.
- Scaling path: Implement EWMH spec for `_NET_SUPPORTED`, `_NET_CLIENT_LIST`, `_NET_ACTIVE_WINDOW`, and `_NET_WM_STATE` at minimum.

### No Keyboard Shortcuts

- Current capacity: All window management is mouse-only.
- Limit: No key bindings at all. No keyboard-driven window switching, movement, or resizing.
- Scaling path: Add key grab infrastructure and configurable key bindings.

## Dependencies at Risk

### XShape Extension Required

- Risk: wm2 requires the X Shape extension and exits immediately if it is unavailable. While still universally available, it is a legacy extension.
- Impact: wm2 will not start on X servers without Shape support.
- Migration plan: The shaped window borders could potentially be reimplemented using XRender with ARGB visuals, though this would be a significant rewrite.

### Hardcoded Font Names

- Risk: `Config.h` hardcodes XLFD font names (`-*-lucida-bold-r-*-*-14-*-75-75-*-*-*-*`). These fonts may not exist on all systems.
- Impact: Falls back to `CONFIG_NASTY_FONT` ("fixed"), which may have poor aesthetics. If "fixed" is also missing, wm2 exits.
- Migration plan: Use Xft/fontconfig with font pattern matching (e.g., "Lucida:sBold:size=14") instead of XLFD names.

### Xlib (not XCB)

- Risk: The entire codebase uses Xlib, which is the older X11 C binding. XCB is the modern replacement.
- Impact: Xlib is still maintained but deprecated in favor of XCB. Some newer X extensions are XCB-only.
- Migration plan: A full XCB port would be a rewrite. More practically, continue using Xlib but be aware of its limitations.

### `libXt` and `libXmu` Linking

- Risk: The Makefile links against `-lXt -lXmu -lSM -lICE` but the source code does not appear to use any Xt or Xmu functions.
- Impact: Unnecessary dependencies. May cause build failures if these libraries are not installed.
- Migration plan: Remove `-lXt -lXmu -lSM -lICE` from `LIBS` in the Makefile and verify the build still works.

## Missing Critical Features

### No Configuration File Support

- Problem: All settings require editing `Config.h` and recompiling.
- Blocks: Per-user customization, runtime configuration changes, distribution packaging with sane defaults.

### No EWMH Compliance

- Problem: No `_NET_*` atoms are interned or handled.
- Blocks: Integration with pagers, taskbars, session managers, and compositors. Modern desktop environments will not recognize wm2.

### No Multi-Monitor Support

- Problem: Hardcoded screen index 0. No XRandR awareness.
- Blocks: Correct behavior on multi-monitor setups. Windows may be positioned off-screen.

### No Shaped Window Support

- Problem: Shaped client windows are explicitly not supported. `Events.C:105` prints "wm2: shaped windows are not supported" when shape events arrive.
- Blocks: Applications using non-rectangular windows (e.g., some dock apps, media players with custom shapes).

### No Key Binding Support

- Problem: No keyboard event handling for window management.
- Blocks: Keyboard-driven workflows. Accessibility.

### No Session Management

- Problem: No XSMP/SM_CLIENT_ID handling. No `_NET_WM_PID`.
- Blocks: Session save/restore across X session restarts.

### No X Resource Management

- Problem: No `XrmInitialize()` or resource file parsing.
- Blocks: X-resource-based configuration (standard X11 configuration mechanism).

## Test Coverage Gaps

### No Tests Exist At All

- What's not tested: Everything. There are zero test files in the codebase.
- Files: N/A -- no test infrastructure exists.
- Risk: Any change to the codebase has unknown impact. Refactoring is extremely risky without being able to verify behavior.
- Priority: High -- at minimum, add integration tests that spin up a virtual X server (Xvfb), launch wm2, create test windows, and verify frame geometry, focus behavior, and client lifecycle.

### Specifically Untested Critical Paths

- **Client lifecycle (create/manage/withdraw/destroy)**: No verification that X resources are properly cleaned up.
- **Border shape computation**: The complex geometric calculations in `Border.C` have no validation. Off-by-one errors in rectangle lists could cause visual corruption.
- **Focus model interactions**: The interplay between `CONFIG_CLICK_TO_FOCUS`, `CONFIG_RAISE_ON_FOCUS`, and `CONFIG_AUTO_RAISE` has subtle timing dependencies that could regress.
- **Memory management**: No checking for leaks after client creation/destruction cycles. The mix of `malloc`/`free`, `new`/`delete`, and Xlib allocation (`XFree`) is error-prone.

---

*Concerns audit: 2026-05-06*
