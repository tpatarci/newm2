# Architecture: wm2

**Mapped:** 2026-05-06

## Pattern

Single-process event loop with object-oriented decomposition. Classic X11 window manager architecture: one process owns the root window, intercepts X events via `SubstructureRedirectMask`, manages client windows by reparenting them into frame windows.

No threading, no IPC, no plugins. Synchronous Xlib calls throughout.

## Entry Points

- `Main.C:main()` — Creates `WindowManager` singleton, which runs until signal or exit
- `WindowManager::WindowManager()` — Constructor does ALL initialization: opens display, allocates atoms, sets up root window event mask, creates cursors, loads fonts/colors, scans existing windows, enters event loop
- `WindowManager::loop()` — Blocking `select()`-based event loop dispatching to `event*()` methods

## Core Classes

### WindowManager (`Manager.h`, `Manager.C`)
Central controller. Owns the display connection and root window.

**Responsibilities:**
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

**Key data:**
- `m_clients: ClientList` — all managed clients
- `m_hiddenClients: ClientList` — iconic clients
- `m_activeClient: Client*` — focused client
- `m_display: Display*` — X connection
- `m_root: Window` — root window

### Client (`Client.h`, `Client.C`)
Represents one managed X window. Created when a window appears, destroyed when it's gone.

**Responsibilities:**
- Window state management (Normal/Iconic/Withdrawn)
- Reparenting into border frame
- Move, resize, hide/unhide operations
- WM_PROTOCOLS handling (WM_DELETE_WINDOW, WM_TAKE_FOCUS)
- Property change handling (WM_NAME, WM_ICON_NAME, WM_TRANSIENT_FOR, WM_COLORMAP_WINDOWS)
- Size hints enforcement
- Colormap management per-window

**Key data:**
- `m_window: Window` — the actual client window
- `m_border: Border*` — frame decoration object
- `m_transient: Window` — transient-for hint
- `m_state: int` — Withdrawn/Normal/Iconic
- `m_sizeHints: XSizeHints` — size constraints
- `m_fixedSize: Boolean` — whether window has min==max size

### Border (`Border.h`, `Border.C`)
Decorative frame around each client window. The signature visual element of wm2 — shaped windows with sideways tabs.

**Responsibilities:**
- Frame window creation and management
- X Shape extension for shaped borders/tabs
- Drawing tab labels (rotated text via xvertext)
- Reparenting client into frame
- Resize handle
- Configure/move operations on frame

**Key windows:**
- `m_parent: Window` — frame window
- `m_tab: Window` — tab on left side
- `m_button: Window` — button at top of tab (hide/delete)
- `m_resize: Window` — resize handle at bottom-right corner

### Rotated (`Rotated.h`, `Rotated.C`)
Third-party xvertext 2.0 library by Alan Richardson. Provides 90-degree rotated bitmap text rendering using core X fonts. Used exclusively by Border for tab labels.

### listmacro2.h
Custom generic list container using preprocessor macros. Provides `declareList`/`implementList` and `declarePList`/`implementPList` (pointer variants). Uses raw `malloc`/`realloc`. Used by WindowManager for `ClientList`.

## Event Flow

```
X11 Server
    │
    ▼
WindowManager::nextEvent()     ← select()-based wait with auto-raise timeout
    │
    ▼
WindowManager::loop()          ← switch on event type
    │
    ├── eventButton()          ← root menu, or dispatch to Client
    ├── eventMapRequest()      → Client::eventMapRequest()
    ├── eventConfigureRequest()→ Client::eventConfigureRequest()
    ├── eventUnmap()           → Client::eventUnmap()
    ├── eventCreate()          → windowToClient(create=True)
    ├── eventDestroy()         → remove from list, release
    ├── eventClient()          ← WM_CHANGE_STATE only
    ├── eventColormap()        → Client::eventColormap()
    ├── eventProperty()        → Client::eventProperty()
    ├── eventEnter()           → Client::eventEnter()  ← focus follows pointer
    ├── eventReparent()        → windowToClient(create=True)
    ├── eventFocusIn()         → Client::eventFocusIn()
    ├── eventExposure()        → Client::eventExposure() → Border::expose()
    └── MotionNotify           ← auto-raise pointer tracking
```

## Window Lifecycle

```
1. Window appears on root
2. MapRequest/CreateNotify → WindowManager creates Client
3. Client::manage() → Border::reparent() wraps window in frame
4. Client state: Normal → visible, Iconic → hidden, Withdrawn → gone
5. DestroyNotify → WindowManager removes Client from list, calls release()
```

## Data Flow

```
Config.h (compile-time constants)
    ↓
General.h (atoms, types, includes)
    ↓
WindowManager ← creates and owns → Client ← owns → Border
    ↑                                      ↑
    └── uses for menu ── Rotated (tab labels)
```

## Atoms Used

Only core ICCCM atoms:
- `WM_STATE` — client state property
- `WM_CHANGE_STATE` — state change requests
- `WM_PROTOCOLS` — protocol negotiation
- `WM_DELETE_WINDOW` — graceful close
- `WM_TAKE_FOCUS` — focus handling
- `WM_COLORMAP_WINDOWS` — multi-colormap support
- `_WM2_RUNNING` — selection ownership (WM presence check)

**No EWMH atoms** — no `_NET_*` support at all.

## Build Flow

```
Makefile (hardcoded gcc/g++ flags)
    → compiles .C → .o
    → links into single `wm2` binary
    → links against: -lXext -lX11 -lXt -lXmu -lSM -lICE -lm
```

## Dependencies Between Components

- `Main.C` → `Manager.h` → `Client.h` → `Border.h` → `Rotated.h` → `General.h` → `Config.h`
- `Client` and `Border` have circular dependency (Border calls `client->isTransient()`, Client calls `border->hasWindow()`) — resolved via forward declarations and friend-like access
- `WindowManager` is passed as const pointer to all Client/Border constructors — they access global state through it
- `listmacro2.h` is used only by `Manager.h` for `ClientList`

## Key Architectural Constraints

1. **Single screen only** — hardcoded `i = 0` in `initialiseScreen()`
2. **No XCB** — Xlib only, synchronous
3. **No EWMH** — modern desktops may not interact correctly
4. **Compile-time config** — all settings in Config.h
5. **No keyboard handling** — mouse only
6. **No session management** — no XSMP/WM_SAVE_YOURSELF
7. **Shaped window support disabled** — `Events.C:104` prints "shaped windows are not supported"
