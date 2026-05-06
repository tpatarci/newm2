# Structure: wm2

**Mapped:** 2026-05-06

## Directory Layout

```
wm2-born-again/
├── upstream-wm2/          # Original 1997 source code (reference only)
│   ├── Main.C             # Entry point (20 lines)
│   ├── Manager.h          # WindowManager class declaration
│   ├── Manager.C          # WindowManager implementation (573 lines)
│   ├── Client.h           # Client class declaration
│   ├── Client.C           # Client implementation (~500 lines)
│   ├── Border.h           # Border class declaration
│   ├── Border.C           # Border implementation (~500 lines)
│   ├── Buttons.C          # Button event handling + root menu (~400 lines)
│   ├── Events.C           # Event loop + event handlers (~664 lines)
│   ├── Rotated.h          # xvertext font rotation declarations
│   ├── Rotated.C          # xvertext font rotation implementation (~500 lines)
│   ├── General.h          # Shared includes, types, atom declarations
│   ├── Config.h           # Compile-time configuration constants
│   ├── Cursors.h          # XBM cursor bitmap data
│   ├── listmacro2.h       # Generic list container macros
│   ├── Makefile           # Build configuration
│   ├── README             # Documentation
│   └── COPYING            # MIT license
└── .planning/             # GSD project planning (not in upstream)
    └── codebase/          # Codebase analysis documents
```

## File Naming Conventions

- `.C` extension for C++ source (pre-standard convention)
- `.h` extension for headers
- PascalCase for class names (`WindowManager`, `Client`, `Border`)
- `m_` prefix for member variables
- No namespaces — everything in global scope

## Key Locations

### Entry Point
- `upstream-wm2/Main.C:7` — `main()` function

### Core Logic
- `upstream-wm2/Manager.C:27` — WindowManager constructor (initialization)
- `upstream-wm2/Manager.C:230` — Screen initialization
- `upstream-wm2/Events.C:6` — Main event loop
- `upstream-wm2/Events.C:118` — nextEvent (select-based wait)

### Client Management
- `upstream-wm2/Client.C` — Client lifecycle, state transitions
- `upstream-wm2/Manager.C:378` — scanInitialWindows (startup scan)
- `upstream-wm2/Manager.C:397` — windowToClient (lookup/create)

### Visual/Border
- `upstream-wm2/Border.C` — Frame decoration, shaped windows
- `upstream-wm2/Buttons.C` — Button clicks, root menu, move/resize
- `upstream-wm2/Rotated.C` — Rotated text rendering for tabs

### Configuration
- `upstream-wm2/Config.h` — All compile-time settings (fonts, colors, focus policy, delays)
- `upstream-wm2/General.h:33` — `Boolean` typedef, `NewString` macro
- `upstream-wm2/General.h:52` — Atom declarations (ICCCM only)

### Build
- `upstream-wm2/Makefile` — Hardcoded gcc/g++ with `/usr/X11R6/` paths

## Code Size

| File | Lines | Role |
|------|-------|------|
| Events.C | ~664 | Event loop + handlers |
| Border.C | ~500 | Frame decorations, shaping |
| Client.C | ~500 | Window lifecycle |
| Rotated.C | ~500 | Font rotation (3rd party) |
| Manager.C | ~573 | Central controller |
| Buttons.C | ~400 | Interaction, menu |
| Main.C | 20 | Entry point |

**Total:** ~3,157 lines of C++ (including 500 lines of third-party Rotated code)

## Include Graph

```
Main.C
 └── Manager.h
      ├── General.h
      │    ├── Config.h
      │    ├── <X11/*.h>
      │    └── <X11/extensions/shape.h>
      └── listmacro2.h

Client.h
 ├── General.h
 ├── Manager.h
 └── Border.h
      └── Rotated.h

Buttons.C (included by Manager.C at link time)
 └── Manager.h, Client.h

Events.C (included by Manager.C at link time)
 └── Manager.h, Client.h
```

## What's NOT Here

- No tests
- No config files (compile-time only)
- No documentation beyond README
- No CI/CD
- No packaging
- No man page
