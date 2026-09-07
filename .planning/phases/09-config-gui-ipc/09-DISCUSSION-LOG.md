# Phase 9: Config GUI + IPC - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-09-06
**Phase:** 09-config-gui-ipc
**Areas discussed:** Who owns the config file, Live-apply feel, GUI shape and how it is launched, Dependency and packaging, What the socket exposes beyond settings, Reset to defaults, Command-line client for scripts and SSH, Who may connect to the socket

---

## Who owns the config file

**Q:** Who writes the config file when you change something in the GUI?

| Option | Selected |
|--------|----------|
| GUI writes, WM reloads (Recommended) | ✓ |
| WM is the single writer |  |

**User's choice:** GUI writes, WM reloads

**Q:** When the GUI saves, what happens to the parts of the file it does not manage?

| Option | Selected |
|--------|----------|
| Surgical edit (Recommended) | ✓ |
| Rewrite from the model |  |

**User's choice:** Surgical edit

**Q:** What should the GUI do when no WM socket is found?

| Option | Selected |
|--------|----------|
| File-only mode with a banner (Recommended) | ✓ |
| Refuse to start |  |

**User's choice:** File-only mode with a banner

**Q:** Which config layer does the GUI edit and show?

| Option | Selected |
|--------|----------|
| User file only, show effective values (Recommended) | ✓ |
| You decide |  |

**User's choice:** User file only, show effective values

---

## Live-apply feel

**Q:** When you change a colour, font or delay in the GUI, when does the desktop change?

| Option | Selected |
|--------|----------|
| Instantly on commit, with Revert (Recommended) | ✓ |
| Apply and Cancel buttons |  |

**User's choice:** Instantly on commit, with Revert; file untouched until Save

**Q:** Which settings are allowed to wait for a WM restart?

| Option | Selected |
|--------|----------|
| None: everything applies live (Recommended) | ✓ |
| Frame thickness and font may wait |  |

**User's choice:** None: everything applies live (frame thickness re-frames, font reloads and re-lays out tabs, menu rebuilds)

**Q:** Closing the GUI with unsaved changes that are already live: what happens?

| Option | Selected |
|--------|----------|
| Ask: Save / Discard / Cancel (Recommended) |  |
| Keep live state, do not save |  |

**User's choice:** Ask Save / Discard / Cancel; Discard reverts the running WM to the saved file

**Q:** File edited externally while the GUI is open and the WM reloads?

| Option | Selected |
|--------|----------|
| Refresh from the WM and say so (Recommended) |  |
| You decide |  |

**User's choice:** WM broadcasts a reload notice; GUI refreshes effective values, keeps and marks unsaved edits, shows a one-line notice

---

## GUI shape and how it is launched

**Q:** How should the GUI window be organised?

| Option | Selected |
|--------|----------|
| Pages: Appearance, Behaviour, Menu (Recommended) | ✓ |
| One scrolling form |  |

**User's choice:** Pages: Appearance, Behaviour, Menu; Save/Revert bottom bar; connection state in the header

**Q:** How are colours and fonts chosen?

| Option | Selected |
|--------|----------|
| Native GTK choosers plus the raw value (Recommended) |  |
| Text fields only |  |

**User's choice:** Native GTK choosers (GtkColorButton, GtkFontButton) plus the raw config spelling shown and editable beside each

**Q:** How do non-programmers find the tool?

| Option | Selected |
|--------|----------|
| 'Configure...' on the root menu when installed (Recommended) |  |
| No root-menu entry |  |

**User's choice:** 'Configure...' on the root menu top level when a wm2-config binary is on PATH at WM startup; plus a .desktop file under Settings

**Q:** How are manual menu entries edited?

| Option | Selected |
|--------|----------|
| List with Add / Edit / Remove (Recommended) |  |
| You decide |  |

**User's choice:** List of name/command/category rows with Add/Edit/Remove dialog; category dropdown of current categories plus free text; rows map 1:1 to menu-entry-* groups

---

## Dependency and packaging

**Q:** How does the build decide whether to produce wm2-config?

| Option | Selected |
|--------|----------|
| Auto-detect, default on (Recommended) |  |
| Explicit option, default off |  |

**User's choice:** BUILD_CONFIG_GUI=AUTO: built when pkg-config finds gtk+-3.0, otherwise one clear configure line and the WM builds as today; preflight learns the optional dependency

**Q:** How are the two binaries installed?

| Option | Selected |
|--------|----------|
| Two install components (Recommended) |  |
| One component, optional file |  |

**User's choice:** Two CMake install components ('wm', 'config-gui'); the phase adds install() rules for both; WM package never depends on GTK

**Q:** Socket belonging to a different program or incompatible build?

| Option | Selected |
|--------|----------|
| Hello handshake, then file-only on mismatch (Recommended) |  |
| You decide |  |

**User's choice:** Hello handshake (program name + protocol version) both ways; mismatch drops the connection and switches to file-only mode with the reason in the banner

**Q:** How is the GUI validated before release?

| Option | Selected |
|--------|----------|
| Xvfb tests plus one droplet VNC pass (Recommended) |  |
| Xvfb only |  |

**User's choice:** Catch2 tests for protocol and file editing; GTK smoke under Xvfb in ctest; once per release a VNC pass on the local droplet with RSS measured while open, recorded as remote-desktop evidence

---

## What the socket exposes beyond settings

**Q:** Beyond reading and changing settings, what should the socket expose?

| Option | Selected |
|--------|----------|
| Status only (Recommended) | ✓ |
| Status plus the window list |  |
| Settings only |  |

**User's choice:** Status only: WM version, protocol version, uptime, screen geometry, counts of managed and hidden windows; no per-window data

---

## Reset to defaults

**Q:** How does 'reset to defaults' work in the GUI?

| Option | Selected |
|--------|----------|
| Per-setting arrow and a Reset all (Recommended) |  |
| Reset all only |  |

**User's choice:** Per-setting reset arrow and a per-page Reset all; reset removes the key from the user file on save so the system layer shows through

---

## Command-line client for scripts and SSH

**Q:** Should there be a command-line client for the socket?

| Option | Selected |
|--------|----------|
| Yes, wm2-ctl in the WM package (Recommended) |  |
| No command-line client |  |

**User's choice:** Yes: wm2-ctl, no GTK, in the WM package; status / get <key> / set <key> <value> / reload; reference client for tests

---

## Who may connect to the socket

**Q:** Who may connect to the socket?

| Option | Selected |
|--------|----------|
| Same uid via SO_PEERCRED plus permissions (Recommended) |  |
| Filesystem permissions only |  |

**User's choice:** Socket in a 0700 dir under XDG_RUNTIME_DIR (fallback /tmp/wm2-born-again-<uid>), 0600, plus SO_PEERCRED uid must equal the WM's uid; others incl. root closed and logged once per uid

---

## Claude's Discretion

- JSON message set and framing; socket path spelling and discovery property.
- Whether SIGHUP becomes reload.
- Names of the new font keys (fonts are not configurable today) and any other new key.
- Live re-frame / font reload / colour reallocation mechanics; effective-vs-saved representation.
- Layering display beyond D-04; GTK smoke-test technique under Xvfb.

## Deferred Ideas

- Rules editor in the GUI.
- Per-window list over the socket.
- Visual refresh of the WM's look (operator's standing request, raised when planned work is done).
- Live sample-tab preview in the GUI; app-cache reload on Save.
