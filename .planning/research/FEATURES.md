# Feature Landscape

**Domain:** Lightweight floating X11 window manager for VPS/VNC use
**Researched:** 2026-05-06

## Table Stakes

Features users and applications expect. Missing = apps break or product feels broken.

### ICCCM Compliance (Foundation Layer)

wm2 already handles most ICCCM basics. These are required for any X11 WM.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| WM_PROTOCOLS (WM_DELETE_WINDOW, WM_TAKE_FOCUS) | Apps need graceful close and focus assignment | Low | wm2 already does this |
| WM_TRANSIENT_FOR handling | Dialogs must stay above parents | Low | wm2 already does this |
| WM_NORMAL_HINTS (min/max size, resize increment, gravity) | Apps specify their sizing constraints; ignoring causes broken layouts | Med | wm2 partially does this; must honor win_gravity per EWMH section 9.7 |
| WM_NAME / WM_ICON_NAME | Window titles in frame | Low | wm2 does this via core X strings |
| WM_HINTS (urgency bit) | Apps signal needing attention (e.g., finished compile, incoming message) | Low | Must set urgency hint on frame; wm2 does not handle this currently |
| Colormap management | Legacy and scientific apps use custom colormaps | Low | wm2 already handles this |
| Reparenting | Standard WM behavior; wm2 already does | N/A | Already done |
| ConfigureNotify synthetic events | ICCCM 4.2.3 requires WM send these after moving/resizing | Med | wm2 must send correct synthetic ConfigureNotify with root-relative coords |

### EWMH Compliance (Modern Application Compatibility Layer)

Without these, modern applications silently break. The EWMH spec (v1.5, 2011) is the authoritative source.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| _NET_SUPPORTED | Lists all EWMH atoms the WM supports; required first atom | Low | MUST set on root window |
| _NET_SUPPORTING_WM_CHECK | Identifies the WM as EWMH-compliant; apps check this first | Low | MUST set on root and child window with _NET_WM_NAME |
| _NET_CLIENT_LIST | External panels/taskbars need this to list windows | Low | SHOULD set and update |
| _NET_ACTIVE_WINDOW | Apps activate each other (chat links to browser); pagers track focus | Med | MUST set; MUST honor client messages with source indication |
| _NET_WM_NAME (UTF-8) | Modern apps use UTF-8 titles; without this, non-ASCII titles are garbled | Low | Prefer over WM_NAME |
| _NET_WM_WINDOW_TYPE | Critical for dialogs, docks, splash screens, notifications | Med | Must handle at minimum: NORMAL, DIALOG, DOCK, NOTIFICATION, SPLASH, UTILITY |
| _NET_WM_STATE | Fullscreen, maximized, above/below, demands attention, skip taskbar | Med | Must handle at minimum: FULLSCREEN, MAXIMIZED_VERT, MAXIMIZED_HORZ, ABOVE, BELOW, DEMANDS_ATTENTION, HIDDEN, FOCUSED |
| _NET_WM_ALLOWED_ACTIONS | Tells pagers/taskbars what actions are available per window | Low | Set on each managed window |
| _NET_FRAME_EXTENTS | Apps need to know frame border widths for positioning calculations | Low | MUST set on each managed window |
| _NET_CLOSE_WINDOW | Pagers/taskbars close windows via this message | Low | MUST honor |
| _NET_NUMBER_OF_DESKTOPS | Even for a single-desktop WM, set to 1 | Low | Set to 1; wm2 has no virtual desktops |
| _NET_CURRENT_DESKTOP | Set to 0 (single desktop) | Low | Always 0 |
| _NET_DESKTOP_VIEWPORT | Set to (0,0) for non-scrolling desktop | Low | Always (0,0) |
| _NET_DESKTOP_GEOMETRY | Set to screen dimensions | Low | Equal to root window size |
| _NET_WORKAREA | Usable area excluding struts; maximized windows use this | Med | Calculate from screen minus strut reservations |
| _NET_WM_USER_TIME | Focus stealing prevention timestamp | Med | Read on new windows to decide whether to focus them |
| _NET_WM_PID | Process ID for killing hung windows | Low | Read from clients; use with _NET_WM_PING |

### EWMH Desktop-Related (Simplified for Single Desktop)

wm2 has no virtual desktops. However, these atoms MUST still be set so pagers/panels do not break.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| _NET_NUMBER_OF_DESKTOPS = 1 | Panels expect this to exist | Low | Fixed value |
| _NET_CURRENT_DESKTOP = 0 | Panels expect this to exist | Low | Fixed value |
| _NET_DESKTOP_VIEWPORT = (0,0) | EWMH spec requires for non-large desktops | Low | Fixed value |
| _NET_DESKTOP_GEOMETRY = screen size | Reflects actual screen dimensions | Low | Update on XRANDR changes |
| _NET_WORKAREA | Panels and maximized windows need usable area | Med | Update when struts change or screen resizes |

### Window State Handling

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Fullscreen (_NET_WM_STATE_FULLSCREEN) | Browsers (F11), video players, presentations all use this | Med | Remove decorations, cover entire screen, remember original geometry |
| Maximize vert/horiz (_NET_WM_STATE_MAXIMIZED_*) | Window buttons and app requests; very common user expectation | Med | Remember pre-maximize geometry; support separate vert/horiz |
| Above/below (_NET_WM_STATE_ABOVE/BELOW) | "Always on top" for terminals over browser, keep-under for background apps | Low | Adjust stacking layer |
| Demands attention (_NET_WM_STATE_DEMANDS_ATTENTION) | Flashing taskbar entries; apps signaling completion | Low | Visual feedback on frame (e.g., change tab color) |
| Skip taskbar (_NET_WM_STATE_SKIP_TASKBAR) | Panels and docks should not show certain utility windows | Low | Read and respect from clients |

### Window Type Handling

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| DIALOG type | Dialogs must float above parent, no decorations or minimal decorations | Low | Use WM_TRANSIENT_FOR to find parent |
| DOCK type | Panels (tint2, polybar, xfce4-panel) must be undecorated, kept on screen edge, reserve strut space | Med | Must not add frame decorations; must honor _NET_WM_STRUT_PARTIAL |
| NOTIFICATION type | Notification bubbles should appear above everything, no decorations | Low | No frame, layer above normal windows |
| SPLASH type | App splash screens should be undecorated, centered | Low | No frame decorations |
| UTILITY type | Tool palettes and toolboxes; minimal decorations, often transient | Low | Consider smaller frame or no frame |
| DESKTOP type | Desktop icon windows (e.g., pcmanfm --desktop) | Low | No decorations, below everything |

### Modern Font Rendering

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Xft/fontconfig for text | Core X fonts are deprecated; UTF-8 and antialiasing require Xft | Med | Replace xvertext rotation library; Xft supports rotation natively |
| UTF-8 title rendering | Modern apps use Unicode titles; core X fonts cannot render them | Med | Xft handles this automatically with fontconfig |

### VNC/RDP Compatibility

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Graceful Shape extension fallback | wm2's identity uses shaped borders; VNC servers may not support Shape | Med | Detect Shape availability; fall back to rectangular frames with clear visual indicator |
| XRANDR for resolution changes | VNC sessions need dynamic resolution when client resizes window | Med | Listen for XRANDR events; update _NET_DESKTOP_GEOMETRY and _NET_WORKAREA; reposition windows |
| No composite extension dependency | VNC servers rarely support compositing | Low | Already not using compositing |
| Minimal font package requirements | VPS instances have few fonts installed | Low | Rely on fontconfig to find available fonts; document minimal font packages |

### Basic Configuration

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Runtime config file (~/.config/wm2-born-again/config) | Users should not need to recompile to change colors/fonts/focus policy | Med | Parse on startup; optionally watch for changes |
| Command-line options | Standard for any modern Linux program | Low | --display, --config-file, --version, --help |
| Configurable focus policy | Focus-follows-mouse vs click-to-focus; wm2 already has both compile-time | Low | Make runtime switchable |

## Differentiators

Features that set wm2-born-again apart from other minimal WMs. Not expected, but valuable.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| Preserved wm2 sideways-tab visual identity | No other active WM has this distinctive look; strong brand recognition among Unix old-timers | Low | Already exists in codebase; preserve through Xft migration |
| AI-powered application discovery for root menu | Scans /bin, /sbin, and .desktop files to auto-populate root menu; novel feature, fits AI showcase theme | Med | Two-phase approach: (1) scan XDG .desktop files as primary source, (2) binary scan as fallback for apps without .desktop entries |
| GUI configuration tool | Non-programmers can change colors, fonts, focus policy without editing text files | Med | GTK3 dialog; reads/writes same config file format; use GTK for accessibility |
| Window matching rules (per-application config) | Openbox-style `<applications>` matching; set position, size, decoration, stacking per WM_CLASS | Med | Match by class, name, role (via WM_CLASS, _NET_WM_WINDOW_TYPE); Openbox is the reference implementation |
| Startup notification support | Visual feedback (busy cursor) while apps launch; matching windows to launch context via DESKTOP_STARTUP_ID | Med | Listen for _NET_STARTUP_INFO_BEGIN/_NET_STARTUP_INFO on root; track sequences; end on window map or timeout |
| Focus stealing prevention | Use _NET_WM_USER_TIME to decide whether new windows get focus; prevents annoying interruptions | Med | Compare timestamp of new window vs current active window's last activity; if user was recently active elsewhere, deny focus and set DEMANDS_ATTENTION |
| _NET_WM_PING (hung window detection) | Detect and offer to kill unresponsive apps; critical for VPS use where you cannot just Ctrl-Alt-Backspace | Med | Send _NET_WM_PING; if no response in ~5s, offer kill via root menu option |
| Xft font rotation for sideways tabs | Modern antialiased rotated text in tabs; rare among WMs | Med | Xft supports matrix transforms for rotation; cleaner than xvertext |

## Anti-Features

Features to explicitly NOT build. These conflict with wm2's philosophy or the VPS use case.

| Anti-Feature | Why Avoid | What to Do Instead |
|--------------|-----------|-------------------|
| Icons / icon tray | wm2's philosophy is "no icons" -- windows are either visible or hidden | Keep hidden-window menu; hide/reveal is wm2's icon substitute |
| Virtual desktops / workspaces | wm2 is intentionally single-desktop; adds complexity contrary to minimalism | Set EWMH desktop atoms to fixed 1 desktop; do not add workspace switching |
| Taskbar / panel | wm2 provides window management itself; external panels can be used if users install them | Honor _NET_WM_STRUT_PARTIAL for external panels users may run; do not build one |
| Compositing / transparency | Adds GPU dependency and complexity; VNC servers rarely support it | Not needed for VPS use; opaque windows are fine |
| Theming engine | Config file for colors/fonts is sufficient; full theming is scope creep | Simple key=value config file for colors, fonts, border widths |
| Tiling layout | wm2 is a floating WM; tiling is a fundamentally different paradigm | Stay floating; users who want tiling should use i3/dwm |
| Keyboard shortcuts / keybindings | Defer to v2; wm2 is mouse-driven and that is its identity | Implement in v2 as optional enhancement |
| Session management (XSMP) | Overkill for VPS use; adds significant complexity | Not needed; VPS sessions are long-running |
| Multi-monitor (Xinerama) | VPS typically has single virtual display; Xinerama adds complexity | Handle XRANDR for resolution changes only |
| Window snapping / edge resistance | Not in wm2's character; adds code for minimal benefit | Allow via per-application rules if users configure it |
| Right-click context menus on frames | wm2 uses button 1 for move, button 2 for resize, button 3 for raise/lower -- do not add menus | Keep existing button bindings |

## Feature Dependencies

```
EWMH _NET_SUPPORTED --> all other EWMH features (must be set first)
EWMH _NET_SUPPORTING_WM_CHECK --> required for apps to detect EWMH WM
EWMH _NET_WM_WINDOW_TYPE --> _NET_WM_STATE (type informs state defaults)
EWMH _NET_WM_STATE_FULLSCREEN --> geometry save/restore (must remember pre-fullscreen size)
EWMH _NET_WM_USER_TIME --> focus stealing prevention logic
ICCCM WM_NORMAL_HINTS --> _NET_FRAME_EXTENTS (frame size depends on size constraints)
Xft migration --> all text rendering (sideways tabs, titles, menus)
XRANDR --> _NET_DESKTOP_GEOMETRY, _NET_WORKAREA updates
Config file --> GUI config tool (tool reads/writes same format)
XDG .desktop scanning --> AI menu discovery (primary app source)
_NET_WM_STRUT_PARTIAL --> _NET_WORKAREA calculation
_NET_WM_PING --> hung window kill feature
Startup notification --> DESKTOP_STARTUP_ID env var propagation
```

## EWMH Atom Implementation Priority

Based on what breaks most visibly when missing, and what other minimal floating WMs (JWM, Openbox, Fluxbox) implement.

### Tier 1: Must Have (apps visibly break without these)

| Atom | Purpose | JWM | Openbox | Fluxbox |
|------|---------|-----|---------|---------|
| _NET_SUPPORTED | Declare EWMH support | Yes | Yes | Yes |
| _NET_SUPPORTING_WM_CHECK | WM identification | Yes | Yes | Yes |
| _NET_CLIENT_LIST | Window enumeration | Yes | Yes | Yes |
| _NET_ACTIVE_WINDOW | Focus tracking | Yes | Yes | Yes |
| _NET_WM_NAME | UTF-8 window titles | Yes | Yes | Yes |
| _NET_WM_WINDOW_TYPE | Window type hints | Yes | Yes | Yes |
| _NET_WM_STATE | Window state management | Yes | Yes | Yes |
| _NET_FRAME_EXTENTS | Frame border widths | Yes | Yes | Yes |
| _NET_WM_ALLOWED_ACTIONS | Action availability | Yes | Yes | Yes |
| _NET_CLOSE_WINDOW | Close windows from pagers | Yes | Yes | Yes |
| _NET_NUMBER_OF_DESKTOPS | Desktop count (set to 1) | Yes | Yes | Yes |
| _NET_CURRENT_DESKTOP | Active desktop (set to 0) | Yes | Yes | Yes |
| _NET_WORKAREA | Usable screen area | Yes | Yes | Yes |
| _NET_DESKTOP_GEOMETRY | Screen size | Yes | Yes | Yes |
| _NET_DESKTOP_VIEWPORT | Viewport (set to 0,0) | Yes | Yes | Yes |

### Tier 2: Should Have (noticeable quality gap without these)

| Atom | Purpose | JWM | Openbox | Fluxbox |
|------|---------|-----|---------|---------|
| _NET_WM_USER_TIME | Focus stealing prevention | Yes | Yes | Yes |
| _NET_WM_PID | Process tracking | Yes | Yes | Yes |
| _NET_WM_STRUT_PARTIAL | Panel space reservation | Yes | Yes | Yes |
| _NET_WM_PING | Hung window detection | No | Yes | Yes |
| _NET_CLIENT_LIST_STACKING | Stacking-order window list | Yes | Yes | Yes |
| _NET_REQUEST_FRAME_EXTENTS | Pre-map frame size estimate | Yes | Yes | Yes |
| _NET_MOVERESIZE_WINDOW | Pager-initiated positioning | Yes | Yes | Yes |
| _NET_RESTACK_WINDOW | Pager-initiated restacking | No | Yes | Yes |
| _NET_WM_FULL_PLACEMENT | Signal WM handles placement | No | Yes | No |

### Tier 3: Nice to Have (can defer)

| Atom | Purpose | JWM | Openbox | Fluxbox |
|------|---------|-----|---------|---------|
| _NET_WM_SYNC_REQUEST | Synchronized resize | No | Yes | No |
| _NET_WM_FULLSCREEN_MONITORS | Multi-monitor fullscreen | No | Yes | No |
| _NET_WM_ICON | Application icons | Yes | Yes | Yes |
| _NET_DESKTOP_NAMES | Desktop names | Yes | Yes | Yes |
| _NET_SHOWING_DESKTOP | Show desktop mode | Yes | Yes | Yes |
| _NET_WM_WM_MOVERESIZE | Client-initiated move/resize | No | Yes | Yes |
| _NET_WM_ICON_GEOMETRY | Icon position hint | No | Yes | No |
| _NET_WM_OPAQUE_REGION | Compositor optimization | No | No | No |

## AI-Powered Application Discovery: Approach

This is wm2-born-again's signature differentiator. Research shows two clear approaches:

### Primary: XDG Desktop Entry Scanning (HIGH confidence)

Scan .desktop files in standard XDG locations. This is how every modern Linux desktop discovers applications.

**Search paths:**
- `$XDG_DATA_DIRS/applications/` (default: `/usr/share/applications/`, `/usr/local/share/applications/`)
- `$XDG_DATA_HOME/applications/` (default: `~/.local/share/applications/`)

**Relevant .desktop file keys:**
- `Name` -- display name for menu
- `Exec` -- command to execute
- `Icon` -- icon name (wm2 ignores icons but can use name for identification)
- `Categories` -- menu categorization (Development, Network, Utility, etc.)
- `Terminal` -- whether to run in terminal
- `NoDisplay` -- if true, exclude from menu
- `StartupNotify` -- whether app supports startup notification
- `StartupWMClass` -- WM_CLASS for startup notification matching
- `Type=Application` -- only include Application entries

**Advantages:** Standard, reliable, already categorized, includes display names, handles Terminal=true automatically.

### Secondary: Binary Scan Fallback (MEDIUM confidence)

For apps without .desktop files (common on minimal VPS installs), scan standard binary paths.

**Search paths:** `/usr/bin/`, `/usr/sbin` (not `/sbin` or `/usr/local/sbin` -- too much noise)

**Filtering strategy:**
- Check if binary is executable (`access(path, X_OK)`)
- Skip known non-GUI binaries (ls, cat, grep, systemd-*, etc.) via blacklist
- Optionally check ELF headers to confirm executables
- For remaining binaries, check if they link against X11 or GUI libraries via `/proc/self/maps` or `ldd` (expensive -- do on first scan only, cache results)

**Advantages:** Catches apps installed without .desktop files (common on headless servers where admin installed via apt without --install-recommends).

### Implementation Recommendation

Implement as a two-phase scanner:
1. **Phase 1:** Parse all .desktop files in XDG paths -- fast, reliable, produces categorized results
2. **Phase 2:** Scan binary directories for GUI applications not already found in Phase 1 -- slower, less reliable, but catches edge cases
3. **Cache results** in `~/.config/wm2-born-again/app-cache` with file timestamps for incremental updates
4. **Run on startup** and optionally on menu open if cache is stale (>24h old)

No existing X11 WM implements this kind of AI-assisted discovery. The closest analogue is the menu generation in openbox (which requires manual pipe menus) or jwm (which requires manual XML configuration). This is genuinely novel for a WM.

## MVP Recommendation

### Phase 1: Foundation (must ship first)
1. EWMH Tier 1 atoms -- without these, modern apps break
2. Xft/fontconfig migration -- without this, UTF-8 titles are broken
3. Runtime config file -- without this, users must recompile
4. _NET_WM_WINDOW_TYPE handling for DIALOG, DOCK, SPLASH -- without these, panels and dialogs are broken

### Phase 2: Usability
5. _NET_WM_STATE_FULLSCREEN and maximize support
6. Focus stealing prevention via _NET_WM_USER_TIME
7. XRANDR support for VNC resolution changes
8. Shape extension graceful fallback for VNC
9. Window matching rules (per-application config)

### Phase 3: Polish
10. AI-powered application discovery (XDG .desktop scan + binary fallback)
11. Startup notification support
12. _NET_WM_PING for hung window detection
13. GUI configuration tool
14. Command-line options

### Defer to v2
- Keyboard shortcuts/keybindings
- Session management
- Multi-monitor support

## Sources

- EWMH Specification v1.5: https://specifications.freedesktop.org/wm/latest-single (authoritative, read in full)
- ICCCM Section 4 (Client to WM Communication): https://tronche.com/gui/x/icccm/sec-4.html
- JWM EWMH Support Checklist: https://joewing.net/projects/jwm/ewmh.html (practical reference for floating WM EWMH coverage)
- Startup Notification Protocol v0.2: https://specifications.freedesktop.org/startup-notification/0.2 (read in full)
- GNOME Shell Focus Stealing Prevention (Sept 2024): https://blogs.gnome.org/shell-dev/2024/09/20/understanding-gnome-shells-focus-stealing-prevention/
- Openbox Per-Application Settings: https://openbox.org/help/Applications
- XDG Desktop Entry Specification: https://specifications.freedesktop.org/desktop-entry-spec/desktop-entry-spec-latest.html
- ArchWiki Window Manager Reference: https://wiki.archlinux.org/title/Window_manager
- x11vnc FAQ (Shape extension and RANDR with VNC): https://github.com/LibVNC/x11vnc/blob/master/doc/FAQ.md
- dwm EWMH patches: https://dwm.suckless.org/patches/ewmhtags/
- wm2 successor wmx (EWMH version): https://github.com/bbidulock/wmx
