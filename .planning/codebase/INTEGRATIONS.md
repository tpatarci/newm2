# External Integrations

**Analysis Date:** 2026-05-06

## APIs & External Services

**X11 Window Manager Protocol:**
- Xlib (X Window System protocol) - Core and only integration
  - SDK/Client: Xlib C bindings (`#include <X11/Xlib.h>`, `#include <X11/Xutil.h>`, `#include <X11/Xatom.h>`, `#include <X11/Xproto.h>`)
  - Auth: X server connection via `DISPLAY` environment variable, uses `XOpenDisplay(NULL)`
  - Location: All source files in `upstream-wm2/`

**X11 Shape Extension:**
- Non-rectangular window frame rendering
  - SDK/Client: `#include <X11/extensions/shape.h>`
  - Usage: `XShapeQueryExtension()` called at startup in `upstream-wm2/Manager.C` line 102; required to run (fatal exit if absent)
  - Extension event base stored for shaped window notifications

**xvertext Rotated Font Rendering (bundled):**
- Third-party library by Alan Richardson (1992), version 2.0
  - Source: `upstream-wm2/Rotated.C`, header: `upstream-wm2/Rotated.h`
  - Purpose: Renders text rotated 90 degrees for vertical window tab labels
  - API used: `XRotLoadFont()`, `XRotTextWidth()`, `XRotDrawString()`, `XRotUnloadFont()`
  - Consumed by: `upstream-wm2/Border.C`

## Data Storage

**Databases:**
- None

**File Storage:**
- Local filesystem only (for font access via X server)

**Caching:**
- None (rotated font glyphs cached in memory by xvertext via `XRotFontStruct`)

## Authentication & Identity

**Auth Provider:**
- X11 server authentication (handled by Xlib/Xauthority infrastructure)
  - Implementation: `XOpenDisplay(NULL)` relies on standard X auth; no custom auth code

## Monitoring & Observability

**Error Tracking:**
- Custom X11 error handler in `upstream-wm2/Manager.C` (`WindowManager::errorHandler` at line 181)
  - Detects "another window manager running" on startup (BadAccess on ChangeWindowAttributes)
  - Suppresses BadWindow errors via `ignoreBadWindowErrors` flag
  - Uses `XGetErrorText()` and `XGetErrorDatabaseText()` for human-readable messages

**Logs:**
- `fprintf(stderr, ...)` throughout all source files
  - Startup banner with copyright and focus policy info: `upstream-wm2/Manager.C` lines 30-73
  - Warning/info messages for unexpected events, client state issues: all `.C` files
  - Fatal errors via `WindowManager::fatal()` which calls `perror()` then `exit(1)`

## CI/CD & Deployment

**Hosting:**
- Local X11 desktop session - wm2 runs as a direct X client replacing any existing window manager

**CI Pipeline:**
- None

## Environment Configuration

**Required env vars:**
- `DISPLAY` - X server connection string (standard X11 env var; read by `XOpenDisplay(NULL)`)

**Optional env vars:**
- `SHELL` - Shell to use for spawning new windows (falls back to `/bin/sh` if unset)
  - Location: `upstream-wm2/Manager.C` line 78

**Secrets location:**
- None - No secrets, credentials, or API keys used

## Webhooks & Callbacks

**Incoming:**
- X11 events processed in main event loop (`upstream-wm2/Events.C` `WindowManager::loop()` at line 7):
  - `MapRequest` - New window mapping
  - `ConfigureRequest` - Window configuration changes
  - `UnmapNotify` - Window unmapping
  - `CreateNotify` - Window creation
  - `DestroyNotify` - Window destruction
  - `ClientMessage` - ICCCM client messages (WM_CHANGE_STATE)
  - `ColormapNotify` - Colormap changes
  - `PropertyNotify` - Property changes (WM_NAME, WM_ICON_NAME, WM_TRANSIENT_FOR, WM_COLORMAP_WINDOWS)
  - `EnterNotify`/`LeaveNotify` - Pointer crossing for focus-follows-mouse
  - `ReparentNotify` - Window reparenting
  - `FocusIn` - Focus gain
  - `Expose` - Window exposure/redraw
  - `MotionNotify` - Pointer motion for auto-raise delay
  - Shape extension events (logged but not fully supported)

**Outgoing:**
- X11 protocol requests (all via Xlib calls):
  - Window management: `XReparentWindow()`, `XMapWindow()`, `XUnmapWindow()`, `XMoveResizeWindow()`, `XConfigureWindow()`, `XDestroyWindow()`
  - Client messages: `XSendEvent()` for WM_PROTOCOLS (WM_DELETE_WINDOW, WM_TAKE_FOCUS) via `Client::sendMessage()` in `upstream-wm2/Client.C` line 353
  - Property changes: `XChangeProperty()` for WM_STATE via `Client::setState()` in `upstream-wm2/Client.C` line 402
  - Selection ownership: `XSetSelectionOwner()` for `_WM2_RUNNING` atom to detect duplicate instances
  - Process spawning: `fork()` + `execlp()`/`execl()` to launch new terminal windows via `WindowManager::spawn()` in `upstream-wm2/Manager.C` line 533

## ICCCM Compliance

**Supported ICCCM atoms (defined in `upstream-wm2/General.h` lines 52-61):**
- `WM_STATE` - Window state management
- `WM_CHANGE_STATE` - State change requests from clients
- `WM_PROTOCOLS` - Client protocol negotiation
- `WM_DELETE_WINDOW` - Graceful window deletion
- `WM_TAKE_FOCUS` - Focus management protocol
- `WM_COLORMAP_WINDOWS` - Colormap window list
- `_WM2_RUNNING` - Custom atom for wm2 instance detection

**Supported standard X properties:**
- `XA_WM_NAME` - Window name
- `XA_WM_ICON_NAME` - Icon name
- `XA_WM_TRANSIENT_FOR` - Transient window relationship
- `XA_WM_NORMAL_HINTS` - Window size hints (via `XGetWMNormalHints()`)
- `XA_WM_HINTS` - Window manager hints (via `XGetWMHints()`)

---

*Integration audit: 2026-05-06*
