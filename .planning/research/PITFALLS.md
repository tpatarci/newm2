# Pitfalls Research: wm2-born-again

**Researched:** 2026-05-06
**Confidence:** HIGH (well-documented X11 patterns, known wm2 issues)

## Critical Pitfalls

### 1. Xft + X Shape Extension Compatibility
**Risk:** HIGH | **Phase:** Border/Xft modernization

The original wm2 draws rotated bitmap text into shaped window regions. Xft uses XRender for antialiased rendering, which operates differently from core X font drawing.

**Warning signs:** Text doesn't appear in tabs, shape masks clip Xft output, visual artifacts on tab labels.

**Prevention:**
- Test Xft rendering within shaped windows early (PoC before full migration)
- Xft draws to pixmaps, then composite into shaped windows — verify this pipeline works
- May need to render Xft text to an intermediate pixmap, then use that as the shape mask source
- Keep a core X font fallback path for VNC servers without Render extension

### 2. Race Conditions During Reparenting
**Risk:** HIGH | **Phase:** Client lifecycle modernization

The reparenting sequence (unparent from root → create frame → reparent into frame) has an inherent race. The server may generate ConfigureNotify, UnmapNotify, or DestroyNotify during the transition.

**Warning signs:** Windows disappear momentarily during manage(), double-mapped windows, ghost frames.

**Prevention:**
- Use `XGrabServer()` / `XUngrabServer()` around reparenting (standard practice — see ICCCM 4.1.4)
- The original wm2 does NOT grab the server during reparenting — this is a bug
- Process all pending events after ungrab before continuing
- Set `m_reparenting` flag before grab, clear after ungrab (existing pattern, needs hardening)

### 3. Memory Management in X11 — Who Owns What
**Risk:** HIGH | **Phase:** RAII foundation

X11 has inconsistent ownership rules: `XFree()` for Xlib-allocated memory, `free()` for some strings, `delete` for C++ objects. Getting this wrong causes leaks or double-frees.

**Warning signs:** Valgrind reports XID leaks, segfaults on shutdown, growing memory over time.

**Prevention:**
- RAII wrappers with stateful deleters carrying `Display*`:
  ```cpp
  using XFontPtr = std::unique_ptr<XFontStruct, decltype([](XFontStruct* f) { /* XFreeFontInfo */ })>;
  ```
- Document ownership in comments: "WM owns" vs "X server owns" vs "Client owns"
- `XFree()` returns from `XGetAtomName`, `XGetWindowProperty` — never `free()` these
- `XAllocNamedColor` returns pixels that must be `XFreeColors()`'d on cleanup
- Audit every `XGet*` call in the original code for missing `XFree`

### 4. VNC/XRDP Compatibility Gaps
**Risk:** MEDIUM | **Phase:** All phases (continuous concern)

Not all X extensions are available on virtual X servers. VNC implementations vary widely.

**Warning signs:** WM crashes on startup with VNC, shape extension missing, fonts not found, colors fail to allocate.

**Prevention:**
- Make Shape extension optional (fallback to rectangular frames) — detect at runtime
- Make Xrandr optional (single screen assumed on VPS) — detect at runtime
- Make XRender optional (fallback to core X fonts) — detect at runtime
- Use fontconfig to find available fonts — don't hardcode font names
- Allocate colors with `XAllocNamedColor()` and handle failure gracefully (fall back to black/white)
- Test on TigerVNC, TightVNC, and xrdp during development

### 5. EWMH Implementation Mistakes
**Risk:** MEDIUM | **Phase:** EWMH compliance

Common mistakes when implementing EWMH for the first time:

**Warning signs:** GTK/Qt apps don't go fullscreen, panels show wrong window list, dialog windows get decorated.

**Prevention:**
- Set `_NET_SUPPORTING_WM_CHECK` BEFORE any other _NET_ atoms — some clients check this first
- Update `_NET_CLIENT_LIST` on EVERY map/unmap/destroy — stale lists cause panel crashes
- Handle `_NET_WM_WINDOW_TYPE` before decorating — DOCK (panels) and DIALOG types need special treatment
- Set `_NET_WM_FULLSCREEN_MONITORS` even on single-monitor setups — some apps check for it
- Always include `_NET_WM_STATE` in `_NET_SUPPORTED` before setting any states
- Test with `wmctrl -l` and `xprop` after implementation

### 6. Configuration File Security
**Risk:** MEDIUM | **Phase:** Configuration system

Config files can contain font names, colors, and command paths. A malicious config could inject commands.

**Warning signs:** Config loading executes shell commands, font names passed to system(), command paths not validated.

**Prevention:**
- Config file parser should NOT execute shell commands
- Font names go through fontconfig (not shell expansion)
- Application paths in menu config should validate binary exists before executing
- Set appropriate permissions on config directory (0700)
- Don't follow symlinks in config files without checking

### 7. Border Shape Geometry Fragility
**Risk:** HIGH | **Phase:** Border modernization

The border shaping code (Border.C) calculates precise pixel rectangles for the window frame shape. These calculations are fragile — off-by-one errors cause visual glitches.

**Warning signs:** Tab has gaps, frame has holes, resize handle misaligned, visual artifacts on resize.

**Prevention:**
- Port shape code mechanically first — don't restructure
- Add visual regression tests (screenshot comparison on Xvfb)
- Keep all original shape rectangle calculations intact during initial port
- Only optimize AFTER the port is verified to produce identical visuals
- Test with multiple window sizes and label lengths

### 8. Event Loop Modernization Gotchas
**Risk:** MEDIUM | **Phase:** Event loop

Replacing wm2's `select()` + `goto` + 50ms spin-wait with `poll()` has subtle pitfalls.

**Warning signs:** Auto-raise timing changes, focus feels sluggish, events processed out of order.

**Prevention:**
- `poll()` timeout should match auto-raise delay requirements (currently 20ms/80ms/400ms)
- Must drain all pending X events before blocking in poll() (XPending + XNextEvent loop)
- XFlush before entering poll() to ensure server has processed all requests
- Don't use `poll()` on the X connection file descriptor AND XNextEvent simultaneously — use one or the other
- Consider using `XConnectionNumber()` to get the fd for poll()

### 9. GTK Config Tool on Headless VPS
**Risk:** LOW | **Phase:** Config GUI

The GTK config tool needs to run on VPS where GTK might not be installed.

**Warning signs:** Config tool can't open display, missing GTK libraries, X forwarding issues.

**Prevention:**
- Make config GUI an optional dependency — WM works without it
- Config file is always the source of truth — GUI just edits it
- Consider a CLI config mode: `wm2-config --set font="Sans-14"`
- GTK should connect to the same display as the WM (not try to open a new one)

### 10. C++ Modernization ABI Concerns
**Risk:** LOW | **Phase:** RAII foundation

Mixing C++17 with X11's C API requires care.

**Warning signs:** Linker errors, runtime crashes in X11 callbacks, wrong function signatures.

**Prevention:**
- `extern "C"` not needed — X11 headers already handle this
- `std::string::c_str()` returns `const char*` — X11 functions taking `char*` need careful handling
- `std::vector` storage is contiguous — safe to pass `&vec[0]` to X11 functions expecting arrays
- Lambda callbacks for RAII deleters must be stateless (empty) for `unique_ptr` zero-overhead guarantee
- Signal handlers can only call async-signal-safe functions — don't use C++ standard library in signal handler
