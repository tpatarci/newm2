# Phase 9: Config GUI + IPC - Context

**Gathered:** 2026-09-06
**Status:** Ready for planning

<domain>
## Phase Boundary

A separate GTK3 program, `wm2-config`, that lets a non-programmer edit fonts, colours, focus
policy, frame thickness, delays and manual menu entries; talks to the running window manager over
a Unix domain socket with JSON messages so that changes apply immediately; and persists them to the
user's config file so they survive a restart. The window manager builds, installs and runs exactly
as today when GTK is absent. A small no-GTK command-line client, `wm2-ctl`, ships with the WM as the
reference client of the same socket. Requirements: CGUI-01..05 (`.planning/REQUIREMENTS.md:67-71`).

Not in this phase: editing window rules (`rule-*` groups) in the GUI, a per-window list over the
socket, keyboard bindings, any change to the WM's look (the operator has a separate, pending
request for a visual refresh).

</domain>

<decisions>
## Implementation Decisions

### Who owns the config file
- **D-01:** The GUI owns the form state and **writes the user's config file itself**; the WM stays a
  pure reader of config. Over the socket the GUI sends live changes and a reload request; it never
  asks the WM to write anything. — **Reversibility:** costly — moving the writer into the WM later
  means a config serializer in the WM and a second code path for every key.
- **D-02:** Saving is a **surgical edit**: only the lines for keys the GUI manages are replaced or
  appended; comments, blank lines, `rule-*` groups and unknown keys stay byte-for-byte where they
  were. Power users and the GUI share one file. The `menu-entry-*` groups the GUI manages are
  rewritten as a block in place of the existing ones.
- **D-03:** With no WM socket (WM not running, or a build without IPC) the GUI opens in
  **file-only mode**: edits and saves work, live-apply controls are disabled, and a banner reads
  "Not connected to a running wm2-born-again; changes take effect at next start".
- **D-04:** The GUI edits the **user file only** (`$XDG_CONFIG_HOME/wm2-born-again/config`) and
  shows **effective values** (system layer plus user overrides, as the WM reports them). It never
  writes the system-wide file.

### Live-apply feel
- **D-05:** Every change applies to the running WM **instantly on commit** (colour chosen, slider
  released, field left) over the socket. A **Revert** button restores the last saved file state in
  both the form and the WM. Nothing touches the file until **Save**.
- **D-06:** **Nothing waits for a restart.** Frame thickness re-frames every managed window in place;
  a font change reloads the Xft font and re-lays out every tab; focus policy and delays flip at
  once; menu entries rebuild the root menu; colours reallocate and repaint. "Where possible" in
  CGUI-04 is to be read as "always". — **Reversibility:** reversible per setting — any one setting
  can fall back to "takes effect at next start" with a note if its live path proves unsafe, and the
  record must say which and why.
- **D-07:** Closing the GUI with unsaved live changes asks **Save / Discard / Cancel**; Discard also
  reverts the running WM to the saved file, so the desktop never silently differs from the file
  after the GUI closes.
- **D-08:** When the file changes underneath an open GUI and the WM reloads, the **WM broadcasts a
  reload notice** on the socket; the GUI re-reads effective values, keeps and marks its unsaved
  edits, and shows a one-line notice.

### GUI shape and how it is launched
- **D-09:** One window with **three pages**: Appearance (tab and menu fonts, tab / frame / button /
  border / menu colours, frame thickness), Behaviour (click-to-focus, raise-on-focus, auto-raise,
  focus-stealing prevention, the three delays, new-window command, exec-using-shell), Menu (manual
  entries). Save and Revert in a bottom bar; connection state in the header.
- **D-10:** Colours and fonts use the **native GTK choosers** (`GtkColorButton`, `GtkFontButton`)
  with the **config-file spelling shown and editable beside each** (for example `#C8CACC`,
  `Sans:bold:size=11`), so a power user can paste values and a non-programmer never has to.
- **D-11:** The WM adds a **"Configure..." entry to the top level of its root menu** when a
  `wm2-config` binary is found on `PATH` at WM startup; otherwise no entry. The GUI also installs a
  `.desktop` file so it appears under Settings in the discovered-apps submenu.
- **D-12:** The Menu page is a **list of name / command / category rows with Add, Edit and Remove**;
  the edit dialog's category is a dropdown of the categories the WM currently shows plus free text.
  Rows map one-to-one onto `menu-entry-name` / `-command` / `-category` groups.
- **D-13:** **Reset to defaults** exists per setting (a small arrow beside each control) and per page
  (Reset all). "Default" means the built-in default. Resetting **removes the key from the user file
  on save** rather than writing the default value, so the system layer shows through again.

### The socket and what it carries
- **D-14:** Beyond settings, the socket exposes **status only**: WM version, protocol version,
  uptime, screen geometry, counts of managed and hidden windows. No per-window data (titles,
  classes, geometry) leaves the WM over the socket. — **Reversibility:** reversible — a window list
  can be added later as a new message without changing existing ones.
- **D-15:** **Hello handshake first**, both ways: program name and protocol version. Anything else,
  or a version the GUI does not speak, drops the connection and the GUI switches to file-only mode
  with the reason in the banner. The GUI never sends settings to a stranger.
- **D-16:** Only the **same uid** may connect: socket in a mode-0700 directory under
  `$XDG_RUNTIME_DIR` (fallback `/tmp/wm2-born-again-<uid>`), socket mode 0600, **and** every
  accepted connection's peer uid (`SO_PEERCRED`) must equal the WM's uid. Anyone else, root
  included, is closed and logged once per uid. — **Reversibility:** one-way in spirit — loosening
  this later widens a security boundary that release notes will have promised; treat as fixed.
- **D-17:** A **`wm2-ctl` command-line client with no GTK dependency ships in the WM package**:
  `status`, `get <key>`, `set <key> <value>`, `reload`. Droplets without a desktop open can drive
  the WM over SSH, and the test suite uses it as the reference client of the protocol.

### Dependency and packaging
- **D-18:** A `BUILD_CONFIG_GUI` CMake option defaulting to **AUTO**: when `pkg-config` finds
  `gtk+-3.0` the GUI is built; otherwise configure prints one clear line and the WM builds exactly
  as today. `scripts/preflight.sh` learns `gtk+-3.0` as an **optional** module. Nothing about the
  WM binary changes either way.
- **D-19:** **Two CMake install components**, `wm` (window manager, `wm2-ctl`, its `.desktop` and
  docs) and `config-gui` (`wm2-config` and its `.desktop`), so a packager can produce
  `wm2-born-again` and `wm2-born-again-config` separately and the WM package never depends on GTK.
  The project has no `install()` rules yet; this phase adds them for both components.
- **D-20:** Validation: Catch2 tests for the protocol (through `wm2-ctl` and a raw client) and for
  the surgical file editor; a GTK smoke test of `wm2-config` under Xvfb in ctest, skipped with a
  reason when the GUI was not built; and **once per release a VNC pass on the local droplet with
  the GUI's RSS measured while open**, recorded like the other remote-desktop evidence. The 512 MB
  budget is measured, not assumed (Phase 8 D-32).

### Claude's Discretion
- JSON message set, framing (newline-delimited JSON is the expected shape) and error replies; the
  socket path's exact spelling and how the GUI discovers it (a root-window property naming the
  path is the expected shape; the display number belongs in the socket name).
- Whether `SIGHUP` becomes "reload config" (today `SIGHUP` shares the exit handler,
  `src/Manager.cpp:152-154`) or reload stays socket-only. If it changes, the release notes say so.
- Names of the new config keys the GUI needs and which do not exist today: **fonts are not
  configurable at all** (`grep -ci font src/Config.cpp` is 0; the menu font is hardcoded at
  `src/Manager.cpp:684-690` and the tab font in `src/Border.cpp:loadTabFont()`), so `tab-font` and
  `menu-font` (or similar) must be added first, with the current hardcoded strings as defaults, and
  CONF-02's "fonts" box closed.
- Mechanics of live re-framing, font reload and colour reallocation inside the WM; how the running
  WM represents "effective config" versus "saved file" for the reload notice.
- Layering behaviour beyond D-04 (what the GUI shows when a key is set only in the system layer).
- GTK smoke-test technique under Xvfb (GTK's own test helpers vs XTEST vs accessibility bus).

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Requirements and roadmap
- `.planning/REQUIREMENTS.md` §Configuration GUI (lines 65-71) — CGUI-01..05, the phase's five
  requirements; §Configuration (lines 58-63) — CONF-01..04, of which CONF-02's "fonts" is not yet
  met and is a precondition here.
- `.planning/ROADMAP.md` §Phase 9 (line 356 onward) — goal and the four success criteria.
- `.planning/PROJECT.md` §Key Decisions — "Config file + GUI tool" and "GTK for config GUI"
  (both Pending, resolved by this phase); §Constraints — 512 MB, no special server configuration.

### Prior-phase decisions that bind this phase
- `.planning/phases/08-xrandr-vnc-compatibility-focus-rules/08-CONTEXT.md` — D-16/D-17 (the three
  focus booleans stay booleans), D-20 (repeated ordered key groups), D-31 (Xvfb carries ctest; real
  sessions once per release), D-32 (RSS measured against a documented budget).
- `.planning/phases/07-root-menu-application-discovery/07-CONTEXT.md` — D-07/D-08 (manual
  `menu-entry-*` entries, "Custom" category, name-based override).
- `.planning/phases/08.5-v1.0-closeout/08.5-CONTEXT.md` — D-8.5-01 (clean key renames, no aliases,
  before v1.0 only: any new key name chosen here is permanent).
- `.planning/phases/08.5-v1.0-closeout/08.5-SECURITY.md` — the evidence-handling and process rules
  every later phase inherits (PID-only kills, `-nolisten tcp`, no `~/.xsession-errors`, no host
  identifiers in committed evidence).

### Release contract
- `docs/RELEASE-NOTES.md` — every user-visible key and behaviour added here must appear there; the
  documentation guards in `scripts/gates/` check parity of `rule-match-*` keys and the pattern
  should be extended to the new keys.
- `COMPILED_CODE_BEHAVIOR_CHECKLIST.md` — the release/signoff checklist that will gain rows for
  the GUI, `wm2-ctl` and the socket.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `include/Config.h` / `src/Config.cpp` — the `Config` struct (colours, three focus booleans,
  focus-stealing prevention, three delays, `frameThickness`, `newWindowCommand`, `execUsingShell`,
  `manualMenuEntries`, `rules`), `Config::applyFile()` key=value parser with the `menu-entry-*`
  and `rule-*` accumulators, `xdgConfigHome()` / `xdgConfigDirs()`, and `--help` output listing
  the paths. The GUI and `wm2-ctl` can reuse the parser as a library target to read effective
  values offline; the surgical writer is new.
- `include/AppCache.h` — category names the Menu page's dropdown should offer come from the same
  source the root menu uses.
- `include/MenuPaint.h` — the pure-header, display-free pattern from 08.5-13 is the model for the
  protocol codec and the surgical file editor: header-only logic with a display-free test binary.
- `tests/support/WmFixture.h` — starts the real WM on a free Xvfb display, kills by stored PID,
  measures RSS; the socket tests attach to the same fixture.
- `include/TimestampWait.h` — the `poll()`-driven bounded wait is the shape for socket reads in
  the WM's event loop (the loop already multiplexes the X fd and a self-pipe via `select`/`poll`).
- `scripts/preflight.sh:69` — the pkg-config module loop to extend with an optional `gtk+-3.0`.
- `scripts/gates/build-all.sh` — the three-tree gate; the GUI target must build warning-free in
  all three when GTK is present and be absent, not broken, when it is not.

### Established Patterns
- Config is loaded once in `main` and handed to `WindowManager(const Config&, apps)`;
  `m_config` is a value member (`include/Manager.h:156`). Live apply means the WM needs a
  `applyConfig(const Config&)` path that diffs and re-applies (colours: `allocateColour` /
  `XftColorWrap` at `src/Manager.cpp:623-699`; tab font: `Border::loadTabFont()`; frame geometry:
  `Border`).
- All process termination in tests and scripts is by explicit PID; all X servers in tests are
  `-nolisten tcp`; nothing writes host identifiers into evidence. The socket work inherits these.
- Documentation guards compare shipped key sets against the release notes by grep.
- No `install()` rules exist in `CMakeLists.txt`; `BUILD_TESTS` is the only option (line 70).

### Integration Points
- Event loop: add the listening socket's fd (and accepted connections) to the existing
  multiplexer in `WindowManager::loop()` / `modalWait()`; the modal loops from ledger 8 must keep
  the socket responsive or the GUI's live apply will stall while a menu is held.
- Root menu top level: the "Configure..." entry (D-11) joins the existing flat entries in
  `src/Buttons.cpp` `WindowManager::menu()`.
- Startup: publish the socket path (root-window property) after `_NET_SUPPORTING_WM_CHECK`.
- CMake: new targets `wm2-ctl` (always), `wm2-config` (AUTO on `gtk+-3.0`), a config/protocol
  library shared by all three, two install components, `.desktop` files.

</code_context>

<specifics>
## Specific Ideas

- The banner text for file-only mode is fixed by decision: "Not connected to a running
  wm2-born-again; changes take effect at next start".
- Raw value fields beside the pickers must show the exact spelling that goes into the file.
- The operator judges UI work by Xvfb screenshots shown in chat (their stated preference).
- The socket must never carry window titles (D-14) and never be reachable by another uid (D-16).

</specifics>

<deferred>
## Deferred Ideas

- Rules editor in the GUI (`rule-*` groups) — not in CGUI-03; the surgical writer leaves rules
  untouched, so a later phase can add it without file-format work.
- Per-window list over the socket (for scripting: find or unhide a window) — explicitly excluded by
  D-14; add as a new message in a later phase if wanted.
- Visual refresh of the WM's own look (tab font size, active-tab colour, menu padding and
  highlight, root background colour, cleaner top-level categories) — the operator's standing
  request, to be raised when the planned work is done; not part of Phase 9.
- Live preview of a sample tab inside the GUI, and reloading the app cache on Save — raised as
  candidate gray areas, not discussed; planner may include the former on the Appearance page if
  cheap, otherwise backlog.

</deferred>

---

*Phase: 09-config-gui-ipc*
*Context gathered: 2026-09-06*
