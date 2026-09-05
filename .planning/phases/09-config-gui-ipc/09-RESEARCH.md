# Phase 9: Config GUI + IPC - Research

**Researched:** 2026-09-06
**Domain:** GTK3 desktop application + Unix domain socket IPC protocol, integrated into a single-threaded Xlib event loop
**Confidence:** MEDIUM (source-level facts are HIGH/VERIFIED; GTK3-specific API and packaging facts are MEDIUM/CITED; RSS and remote-desktop behavioural predictions are LOW/ASSUMED pending the D-20 measured pass)

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Who owns the config file**
- **D-01:** The GUI owns the form state and **writes the user's config file itself**; the WM stays a
  pure reader of config. Over the socket the GUI sends live changes and a reload request; it never
  asks the WM to write anything. — Reversibility: costly.
- **D-02:** Saving is a **surgical edit**: only the lines for keys the GUI manages are replaced or
  appended; comments, blank lines, `rule-*` groups and unknown keys stay byte-for-byte where they
  were. The `menu-entry-*` groups the GUI manages are rewritten as a block in place of the existing ones.
- **D-03:** With no WM socket the GUI opens in **file-only mode**: edits and saves work, live-apply
  controls are disabled, banner reads "Not connected to a running wm2-born-again; changes take effect
  at next start".
- **D-04:** The GUI edits the **user file only** (`$XDG_CONFIG_HOME/wm2-born-again/config`) and shows
  **effective values** (system layer plus user overrides). It never writes the system-wide file.

**Live-apply feel**
- **D-05:** Every change applies to the running WM **instantly on commit** over the socket. A
  **Revert** button restores the last saved file state in both the form and the WM. Nothing touches
  the file until **Save**.
- **D-06:** **Nothing waits for a restart.** Frame thickness re-frames every managed window in place;
  a font change reloads the Xft font and re-lays out every tab; focus policy and delays flip at once;
  menu entries rebuild the root menu; colours reallocate and repaint. "Where possible" in CGUI-04 is
  to be read as "always". — Reversibility: reversible per setting.
- **D-07:** Closing the GUI with unsaved live changes asks **Save / Discard / Cancel**; Discard also
  reverts the running WM to the saved file.
- **D-08:** When the file changes underneath an open GUI and the WM reloads, the **WM broadcasts a
  reload notice** on the socket; the GUI re-reads effective values, keeps and marks its unsaved edits,
  and shows a one-line notice.

**GUI shape and how it is launched**
- **D-09:** One window with **three pages**: Appearance, Behaviour, Menu. Save and Revert in a bottom
  bar; connection state in the header.
- **D-10:** Colours and fonts use the **native GTK choosers** (`GtkColorButton`, `GtkFontButton`) with
  the **config-file spelling shown and editable beside each**.
- **D-11:** The WM adds a **"Configure..." entry to the top level of its root menu** when a
  `wm2-config` binary is found on `PATH` at WM startup; otherwise no entry. The GUI also installs a
  `.desktop` file so it appears under Settings in the discovered-apps submenu.
- **D-12:** The Menu page is a **list of name / command / category rows with Add, Edit and Remove**;
  category is a dropdown of categories the WM currently shows plus free text. Rows map one-to-one onto
  `menu-entry-name` / `-command` / `-category` groups.
- **D-13:** **Reset to defaults** exists per setting and per page. Resetting **removes the key from the
  user file on save** rather than writing the default value.

**The socket and what it carries**
- **D-14:** Beyond settings, the socket exposes **status only**: WM version, protocol version, uptime,
  screen geometry, counts of managed and hidden windows. No per-window data leaves the WM. —
  Reversibility: reversible, a window list can be added later.
- **D-15:** **Hello handshake first**, both ways: program name and protocol version. Anything else, or
  an unspoken version, drops the connection and the GUI switches to file-only mode with the reason in
  the banner. The GUI never sends settings to a stranger.
- **D-16:** Only the **same uid** may connect: socket in a mode-0700 directory under
  `$XDG_RUNTIME_DIR` (fallback `/tmp/wm2-born-again-<uid>`), socket mode 0600, **and** every accepted
  connection's peer uid (`SO_PEERCRED`) must equal the WM's uid. Anyone else, root included, is closed
  and logged once per uid. — Reversibility: treat as fixed.
- **D-17:** A **`wm2-ctl` command-line client with no GTK dependency ships in the WM package**:
  `status`, `get <key>`, `set <key> <value>`, `reload`.

**Dependency and packaging**
- **D-18:** A `BUILD_CONFIG_GUI` CMake option defaulting to **AUTO**: when `pkg-config` finds
  `gtk+-3.0` the GUI is built; otherwise configure prints one clear line and the WM builds exactly as
  today. `scripts/preflight.sh` learns `gtk+-3.0` as an **optional** module.
- **D-19:** **Two CMake install components**, `wm` (window manager, `wm2-ctl`, its `.desktop` and
  docs) and `config-gui` (`wm2-config` and its `.desktop`). No `install()` rules exist yet; this phase
  adds them for both components.
- **D-20:** Validation: Catch2 tests for the protocol (through `wm2-ctl` and a raw client) and for the
  surgical file editor; a GTK smoke test of `wm2-config` under Xvfb in ctest, skipped with a reason
  when the GUI was not built; and **once per release a VNC pass on the local droplet with the GUI's
  RSS measured while open**. The 512 MB budget is measured, not assumed.

### Claude's Discretion
- JSON message set, framing (newline-delimited JSON is the expected shape) and error replies; the
  socket path's exact spelling and how the GUI discovers it (a root-window property naming the path is
  the expected shape; the display number belongs in the socket name).
- Whether `SIGHUP` becomes "reload config" (today `SIGHUP` shares the exit handler,
  `src/Manager.cpp:152-154`) or reload stays socket-only. If it changes, the release notes say so.
- Names of the new config keys the GUI needs and which do not exist today: fonts are not configurable
  at all, so `tab-font` and `menu-font` (or similar) must be added first, with the current hardcoded
  strings as defaults, and CONF-02's "fonts" box closed.
- Mechanics of live re-framing, font reload and colour reallocation inside the WM; how the running WM
  represents "effective config" versus "saved file" for the reload notice.
- Layering behaviour beyond D-04 (what the GUI shows when a key is set only in the system layer).
- GTK smoke-test technique under Xvfb (GTK's own test helpers vs XTEST vs accessibility bus).

### Deferred Ideas (OUT OF SCOPE)
- Rules editor in the GUI (`rule-*` groups) — surgical writer leaves rules untouched.
- Per-window list over the socket — explicitly excluded by D-14.
- Visual refresh of the WM's own look — the operator's standing request, not part of Phase 9.
- Live preview of a sample tab in the GUI, and reloading the app cache on Save — planner may include
  the former on the Appearance page if cheap, otherwise backlog.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| CGUI-01 | Separate GTK3 binary (`wm2-config`) for visual configuration editing | Standard Stack (GTK3 plain-C-API pattern), Architecture Patterns (three-page window), CMake `BUILD_CONFIG_GUI` AUTO option |
| CGUI-02 | Communicates with WM via Unix domain socket (JSON protocol) | Protocol design section, Code Examples (socket server integration into `modalWait()`/`nextEvent()`, SO_PEERCRED, NDJSON framing) |
| CGUI-03 | Edit fonts, colors, focus policy, frame thickness, menu entries | Runtime state inventory of existing `Config` struct, new `tab-font`/`menu-font` keys, GtkColorButton/GtkFontButton patterns |
| CGUI-04 | Changes apply immediately (no restart required) where possible | Live re-apply section: `Config::applyFile`/`applyKeyValue` diff-and-reapply, `Border::loadTabFont()`, `allocateColour`/`XftColorWrap` reallocation |
| CGUI-05 | WM works without GTK installed (GUI is optional dependency) | CMake AUTO pattern, two install components, `scripts/preflight.sh` optional-module pattern |
</phase_requirements>

## Summary

This phase adds two new binaries (`wm2-config`, a GTK3 GUI; `wm2-ctl`, a dependency-free CLI) and a
Unix domain socket server inside the existing `wm2-born-again` window manager, none of which exist in
any form today — there are zero references to `gtk`, `socket`, `json`, `wm2-config` or `wm2-ctl`
anywhere in the current tree (`CMakeLists.txt`, `scripts/preflight.sh`,
`scripts/gates/build-all.sh` — all searched, all empty). The WM's `Config` struct
(`include/Config.h:20-134`) already has a mature key=value parser with a repeated-group accumulator
pattern (`menu-entry-*`) that both the GUI and `wm2-ctl` can reuse as a read-only library; the GUI adds
a **new, separate surgical file writer** (D-02), since nothing in the current parser writes anything.
Fonts are a real gap: `grep -c -i font src/Config.cpp` returns `0` — no font key exists yet — so the
menu font (hardcoded at `src/Manager.cpp:684-685`, `"Ubuntu,Noto Sans,DejaVu Sans,Sans:size=12"`) and
the tab font (hardcoded at `src/Border.cpp:223`, `"Ubuntu,Noto Sans,DejaVu Sans,Sans:bold:size=12"`)
must gain config keys before CGUI-03 and CONF-02 can be satisfied.

The single hardest integration fact this research turned up: the WM's event loop multiplexes exactly
two file descriptors — the X connection and a self-pipe — via a **fixed-size `struct pollfd fds[2]`**
declared identically in two places, `WindowManager::nextEvent()` (`src/Events.cpp:187`) and
`WindowManager::modalWait()` (`src/Events.cpp:284`). Every modal grab loop in the codebase (the root
menu, move, resize, the tab-button hold, the gesture recognizer) already funnels through
`modalWait()` — this is the single choke point the phase context's "modal loops must keep the socket
serviced" requirement (D-06) refers to, and it means the socket's listening fd and every accepted
client fd need to join **both** `nextEvent()`'s and `modalWait()`'s poll arrays, which today are
stack-local fixed-size arrays sized for exactly two descriptors. This is a structural change to both
functions, not an additive one, and it is the plan's central engineering risk.

**Primary recommendation:** build the socket layer as a small header-only, display-free module (the
established house pattern — `include/EventPump.h`, `include/TimestampWait.h`, `include/MenuPaint.h` —
logic with no X11 dependency, callable from a Catch2 unit test with no Xvfb) that owns a
`std::vector<pollfd>` the two poll sites populate before calling `poll()`, use plain C GTK3 (not
gtkmm) kept to a thin presentation layer over a display-free protocol/file-editor core, hand-roll a
tiny newline-delimited JSON codec rather than adding nlohmann-json as a new dependency (the WM binary
must build with zero new libraries per D-18/D-19's "the WM package never depends on GTK" spirit,
extended here to "never depends on a new JSON library either"), and treat every RSS/remote-desktop
claim below as provisional until D-20's measured VNC pass runs.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Config file read (effective values) | WM process (Config library, reused) | GUI process (offline read for file-only mode) | `Config::applyFile()`/`applyKeyValue()` already exists and is X11-free; both processes link it as a static library target |
| Config file write (surgical edit) | GUI process | — | D-01: WM is a pure reader; the GUI is the sole writer, new code, no existing counterpart |
| Live settings apply | WM process (event loop + `applyConfig`-style diff) | — | Colour/font/frame state lives in `WindowManager`/`Border` member fields; only the WM process can safely mutate live X11 resources |
| IPC transport (socket server, framing, auth) | WM process (listener) | GUI + `wm2-ctl` (clients) | D-16 requires the WM to own accept()/SO_PEERCRED enforcement; clients only ever connect out |
| Protocol codec (JSON encode/decode) | Shared header-only library | — | Must be linked into WM, GUI and `wm2-ctl` identically so no two ends can disagree on wire shape |
| GUI presentation (widgets, dialogs) | GUI process (GTK3) | — | Never linked into the WM binary; CGUI-05 requires zero GTK coupling in the WM |
| Root menu "Configure..." entry | WM process (`Buttons.cpp` `menu()`) | — | Reuses the existing `findOnPath()` helper (`src/DesktopEntry.cpp:52-60`) already used for `.desktop` `TryExec` checks |
| `.desktop` file discovery of `wm2-config` | Filesystem / XDG data dirs | — | Installed by the `config-gui` CMake component; discovered by the WM's existing `AppCache`/`DesktopEntry` scanner like any other app |

## Standard Stack

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| `gtk+-3.0` (plain C API) | 3.24.33-1ubuntu2.2 (Jammy, `libgtk-3-dev`) `[VERIFIED: apt-cache policy libgtk-3-dev, this host, 2026-09-06]`; 3.24.41-4ubuntu1.3 (Noble) `[CITED: launchpad.net/ubuntu/noble/+package/libgtk-3-0t64]` | GUI toolkit for `wm2-config` | Only mainstream, packaged GTK3 toolkit; the project already targets X11-only, GTK3's X11 backend is mature and well-documented |
| Unix domain sockets (`AF_UNIX`, `SOCK_STREAM`) | POSIX, no library | IPC transport between WM and GUI/`wm2-ctl` | Already the project's OS-level idiom (it uses `pipe()` for the self-pipe); `AF_UNIX` + `SO_PEERCRED` is the standard same-host, uid-authenticated local IPC primitive on Linux `[CITED: man7.org/linux/man-pages/man7/unix.7.html]` |
| Hand-rolled NDJSON codec (header-only, project-internal) | n/a — new code | Wire format for the socket protocol | See "Don't Hand-Roll" below for why this is the *recommended* exception, not a contradiction |

### Supporting

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `gtkmm-3.0`/`libgtkmm-3.0-dev` | 3.24.5-1build1 (Jammy) `[VERIFIED: apt-cache policy libgtkmm-3.0-dev, this host]` | C++ binding over GTK3 with RAII widget ownership and typed signals | **Not recommended for this phase** — see rationale below; listed only as the documented alternative |
| `nlohmann-json3-dev` | 3.10.5-2 (Jammy, official) `[VERIFIED: apt-cache policy nlohmann-json3-dev, this host]`; 3.11.3-1 (Noble) `[CITED: launchpad.net/ubuntu/noble/+package/nlohmann-json3-dev]` | Full-featured JSON library | **Not recommended** — see "Don't Hand-Roll"; listed as the documented alternative if the hand-rolled codec proves insufficient |
| `libatk-bridge2.0-0`, `adwaita-icon-theme`, `hicolor-icon-theme` | pulled transitively | AT-SPI accessibility bridge, icon fallback theme | Automatic — these are **hard `Depends:`** of `libgtk-3-0`, not optional, confirmed via `apt-cache show libgtk-3-0` on this host: `Depends: adwaita-icon-theme, hicolor-icon-theme, ..., libatk-bridge2.0-0 (>= 2.5.3), ...` `[VERIFIED: apt-cache show libgtk-3-0, this host, 2026-09-06]` — a bare droplet that installs `libgtk-3-0` will always get a real icon theme, closing the "no icon theme" failure mode by construction |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Plain C GTK3 API from C++ (static callbacks + `user_data` pattern) | gtkmm-3.0 | gtkmm gives RAII and typed signals but adds a second, heavier dependency (`libgtkmm-3.0-dev` pulls in `libsigc++`) for a single-window, three-page tool; the codebase's own convention (`.C`/no-STL upstream aside, the *current* modernized code is plain C++17 with RAII wrappers hand-written per-resource, e.g. `x11::XftFontPtr`) already favours thin, explicit wrappers over adopting a binding framework. Plain C GTK3 keeps the GUI's dependency surface identical to what `pkg-config gtk+-3.0` reports, which is exactly what `BUILD_CONFIG_GUI AUTO` probes for `[ASSUMED — architectural judgment, not verified against an authoritative "which is standard" source]` |
| Hand-rolled NDJSON codec | nlohmann-json (header-only) | nlohmann-json is objectively more robust (handles escaping, Unicode, nested structures) but is a new dependency for a protocol whose entire message set (D-14: hello, get, set, reload, status, one broadcast) is flat key/value pairs with no nesting; a ~150-line hand-rolled encoder/decoder for that shape is smaller than the risk surface of adding a new required header to the WM binary just to satisfy the "no per-window data, status-only" protocol |
| GTK's native `GtkColorChooser`/`GtkFontChooser` interfaces via `GtkColorButton`/`GtkFontButton` | A custom color-swatch/font-picker widget | D-10 already locks this decision — native choosers are required, custom would be strictly more code for a worse non-programmer experience |

**Installation:**
```bash
# Build-time (only when BUILD_CONFIG_GUI resolves to ON via AUTO)
sudo apt install libgtk-3-dev pkg-config

# Runtime (config-gui package only; WM package never requires this)
sudo apt install libgtk-3-0
```

**Version verification:** Verified against this workstation (Ubuntu 22.04.5 LTS, confirmed via
`/etc/os-release`) using `apt-cache policy`:
```
$ pkg-config --exists gtk+-3.0 ; echo $?
1                                          # NOT found -- matches the phase's stated starting fact
$ apt-cache policy libgtk-3-dev
  Candidate: 3.24.33-1ubuntu2.2 (jammy-updates/jammy-security)
$ apt-cache policy nlohmann-json3-dev
  Candidate: 3.10.5-2 (jammy/universe)
```
Ubuntu 24.04 (Noble) figures are `[CITED: Launchpad]`, not verified on this host (no 24.04 machine
available in this session).

## Package Legitimacy Audit

The formal npm/pypi/crates Package Legitimacy Gate (`gsd_run query package-legitimacy check`) **does
not apply to this phase** — it only accepts `--ecosystem npm|pypi|crates`
(`[VERIFIED: gsd-tools package-legitimacy check --help output, this session]`), and this project has
no such ecosystem: every dependency is an Ubuntu/Debian `apt` package, resolved by `pkg-config` and
installed by the system package manager, never by a language-level package manager. Apt packages from
the official Ubuntu archive carry a different, arguably stronger provenance signal than a registry
lookup (they are cryptographically signed by Canonical and built by Launchpad from a public source
package), so the slopsquatting concern the gate exists for does not transfer. In place of the formal
gate, here is the provenance actually available for every package this phase would add:

| Package | Registry | Age (first seen) | Install base | Source | Verdict | Disposition |
|---------|----------|-------------------|---------------|--------|---------|-------------|
| `libgtk-3-dev` / `libgtk-3-0` | Ubuntu main archive (Jammy `jammy-updates`) | GTK3 first released 2011; this exact revision `3.24.33-1ubuntu2.2` is a security-channel update | Default dependency of nearly every desktop app on Ubuntu; already runtime-installed on this workstation as `libgtk-3-0` | `packages.ubuntu.com` / Launchpad `gtk+3.0` source package | OK | Approved |
| `nlohmann-json3-dev` | Ubuntu universe archive (Jammy) | Upstream project since 2013; packaged in Ubuntu since ≥18.04 | Widely used; ~9k GitHub stars project (not independently re-verified this session) | `packages.ubuntu.com`, `github.com/nlohmann/json` | OK (not selected — see Standard Stack) | Not used this phase |
| `libgtkmm-3.0-dev` | Ubuntu main archive (Jammy) | gtkmm project since 2002 | Standard C++ GTK binding | `packages.ubuntu.com` | OK (not selected — see Standard Stack) | Not used this phase |

**Packages removed due to [SLOP] verdict:** none.
**Packages flagged as suspicious [SUS]:** none — every package considered is an official-archive
Ubuntu package with a long, well-known history.

## Architecture Patterns

### System Architecture Diagram

```
                     ┌─────────────────────────────┐
                     │   wm2-config (GTK3 process)  │
                     │                              │
  User edits  ──────▶│  Appearance / Behaviour /    │
  a widget           │  Menu pages (GtkNotebook)     │
                     │        │                     │
                     │        ▼                     │
                     │  Form-state model  ◀──────┐   │
                     │   (display-free)          │   │
                     │        │                  │   │
              ┌──────┼────────┼──────────────────┼───┼──────┐
              │      │        ▼                  │   │      │
              │      │  Surgical file editor      │  Protocol│
              │      │  (D-02, own module)         │  client  │
              │      └────────┬─────────────────┬─┘   codec  │
              │               │ atomic write     │ NDJSON    │
              │               ▼ (temp+rename)    ▼ over      │
              │     $XDG_CONFIG_HOME/            AF_UNIX     │
              │     wm2-born-again/config        socket      │
              └──────────────────────────────────────┼───────┘
                                                       │
                              (same uid, SO_PEERCRED,  │
                               mode-0700 dir, D-16)    │
                                                       ▼
     ┌─────────────────────────────────────────────────────────────────┐
     │              wm2-born-again (WindowManager process)              │
     │                                                                   │
     │  ┌───────────────┐   accept()/read()/write()  ┌────────────────┐ │
     │  │ Listening      │◀───────────────────────────│ SocketServer   │ │
     │  │ socket (new    │   joins poll() array of    │ (new header-   │ │
     │  │ fd, D-16 path) │   nextEvent()/modalWait()  │ only module)   │ │
     │  └───────────────┘                             └───────┬────────┘ │
     │                                                          │        │
     │   X11 connection fd ──┐                                 ▼        │
     │   self-pipe fd     ───┼──▶ poll(fds[N], N, timeout)  Protocol    │
     │   socket listen fd ───┤        (Events.cpp:187, :284)  dispatch  │
     │   per-client fds  ────┘                                  │       │
     │                                                          ▼       │
     │                                              Config diff+apply   │
     │                                     (colours, fonts, frame,      │
     │                                      focus policy, menu rebuild) │
     │                                                          │       │
     │                                                          ▼       │
     │                              Border/Client/Manager live mutation │
     │                          (allocateColour, XftColorWrap,          │
     │                           Border::loadTabFont, re-frame windows) │
     └───────────────────────────────────────────────────────────────────┘

     wm2-ctl (CLI, no GTK) ──── same NDJSON-over-AF_UNIX protocol client ──▶ (same socket as above)
```

### Recommended Project Structure
```
include/
├── ConfigProtocol.h     # NEW — header-only NDJSON codec + message types (D-14/D-15), display-free
├── SocketServer.h        # NEW — header-only pollfd-vector abstraction the WM's poll() sites consume
├── ConfigFileWriter.h     # NEW — surgical file editor (D-02), display-free, unit-testable
├── Config.h               # EXISTING — gains tab-font/menu-font keys; reused as a read-only library
src/
├── ConfigProtocol.cpp      # if any non-inline pieces are needed
├── SocketServer.cpp         # accept/SO_PEERCRED/cleanup logic that needs libc, not X11
├── ConfigFileWriter.cpp      # surgical edit implementation
apps/
├── wm2-config/
│   ├── main.cpp              # GTK3 entry point, thin — owns GtkApplication/GtkWindow only
│   ├── AppearancePage.cpp/.h   # widget assembly, delegates to Config + protocol client
│   ├── BehaviourPage.cpp/.h
│   ├── MenuPage.cpp/.h
│   └── ProtocolClient.cpp/.h    # GLib main-loop integration over ConfigProtocol.h
└── wm2-ctl/
    └── main.cpp                # no GTK, no GLib main loop — connect, one round trip, exit
```
This mirrors the existing house pattern exactly: `include/MenuPaint.h`, `include/TimestampWait.h`,
`include/EventPump.h` are all display-free, header-only logic extracted from event-loop code
specifically so a Catch2 test can link them without X11 or a production object file. The new
`ConfigProtocol.h`/`SocketServer.h`/`ConfigFileWriter.h` should follow that same shape — this is what
lets D-20's protocol tests run without Xvfb at all (only the smoke test needs Xvfb).

### Pattern 1: Static-callback GTK3 in C++ (no gtkmm)
**What:** GTK signal handlers are plain C function pointers; C++ member functions cannot be used
directly because of the implicit `this` parameter mismatch. The standard workaround is a `static`
member function registered via `g_signal_connect(widget, "signal", G_CALLBACK(StaticCb), this)`,
which casts the `gpointer user_data` back to the owning C++ object and dispatches to a real member
function `[CITED: gtk.org/docs/language-bindings/cpp, community GTK+C++ tutorials]`.
**When to use:** Every GTK3 widget signal in `wm2-config` (button clicks, color/font selection,
notebook page changes).
**Example:**
```cpp
// Source: standard GTK3+C++ static-callback idiom [CITED: gtk.org tutorial pattern]
class AppearancePage {
public:
    explicit AppearancePage(GtkWidget* colorButton) {
        g_signal_connect(colorButton, "color-set",
                          G_CALLBACK(&AppearancePage::onColorSetStatic), this);
    }
private:
    static void onColorSetStatic(GtkColorButton* button, gpointer userData) {
        static_cast<AppearancePage*>(userData)->onColorSet(button);
    }
    void onColorSet(GtkColorButton* button) {
        GdkRGBA rgba;
        gtk_color_button_get_rgba(button, &rgba);
        // ... format as "#RRGGBB", send over protocol, update raw-value field (D-10)
    }
};
```

### Pattern 2: Extending the fixed-size poll() array to a variable one
**What:** `WindowManager::nextEvent()` and `WindowManager::modalWait()` both currently declare
`struct pollfd fds[2]` (`src/Events.cpp:187` and `src/Events.cpp:284`, confirmed identical shape in
both) covering only the X connection and the self-pipe. Adding the listening socket and N accepted
client connections means both call sites need a shared, growable descriptor set.
**When to use:** Any task touching the event loop or a modal grab loop in this phase.
**Example (shape, not literal code — the actual implementation is a planning decision):**
```cpp
// Source: derived from src/Events.cpp:180-230 and :280-320, this session, this repo
// Both nextEvent() and modalWait() would query a shared accessor instead of
// hardcoding fds[2]:
std::vector<pollfd> fds = buildPollSet(ConnectionNumber(display()), m_pipeRead.get(),
                                        m_socketServer);   // new: listen fd + client fds
int r = poll(fds.data(), fds.size(), timeout);
// dispatch: fds[0]=X, fds[1]=pipe, fds[2..]=socket server's own fds -- the
// SocketServer object (header-only, per Pattern in Recommended Project Structure)
// owns the mapping from array index back to which client it is.
```
**Risk:** `modalWait()`'s existing contract (`ModalWait::Interrupted` on `POLLIN` from the self-pipe,
`ModalWait::Event` on a matching X event) must not change for existing callers — the socket case is
purely additive: readable socket fds during a modal grab should be serviced (read the request, queue
or answer it) without returning `Interrupted` or `Event`, i.e. a **third silent case**, analogous to
how `ServiceFocusTick` is handled today in `nextEvent()`'s `eventPumpDecide()` switch
(`include/EventPump.h:81-126`, `src/Events.cpp:238-269`).

### Pattern 3: SO_PEERCRED authentication on accept
**What:** Immediately after `accept()`, call `getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len)`
and compare `cred.uid` to `geteuid()`; close and log-once-per-uid on mismatch (D-16).
**When to use:** Every accepted connection on the WM's listening socket, before any protocol bytes
are read.
**Example:**
```c
// Source: unix(7) man page, struct ucred definition [CITED: man7.org/linux/man-pages/man7/unix.7.html]
struct ucred cred;
socklen_t len = sizeof(cred);
if (getsockopt(clientFd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0
    || cred.uid != geteuid()) {
    close(clientFd);
    // log once per uid per D-16
    return;
}
```
**Caveat, load-bearing:** the man page notes credentials are captured "at the time of the call to
connect()", not at each subsequent read — a process that later drops privileges does not un-authorize
an already-open connection `[CITED: man7.org/linux/man-pages/man7/unix.7.html]`. This does not weaken
D-16 (same-uid at connect time is exactly what D-16 asks for) but should be stated in the threat model
so it is not "discovered" during a later security pass.

### Pattern 4: Publishing the socket path as a root-window property
**What:** After `_NET_SUPPORTING_WM_CHECK` is set (`src/Manager.cpp:798-806`), set a new,
non-standard root-window property (e.g. `_WM2_CONFIG_SOCKET`, `STRING` or `UTF8_STRING` type) whose
value is the socket's filesystem path. The GUI reads this property at startup via `XGetWindowProperty`
on the root window instead of hardcoding a path — this is the mechanism the phase context names as
"Claude's Discretion... the expected shape".
**Example:**
```cpp
// Source: pattern derived from the existing property-setting site,
// src/Manager.cpp:798-806, this session, this repo (verbatim from that block):
//     XChangeProperty(display(), m_root, Atoms::net_supportingWmCheck,
//                     XA_WINDOW, 32, PropModeReplace,
//                     reinterpret_cast<unsigned char*>(&m_wmCheckWindow), 1);
// The new property follows the identical call shape, substituting a new atom
// and a C-string payload:
XChangeProperty(display(), m_root, Atoms::wm2ConfigSocket,
                 XA_STRING, 8, PropModeReplace,
                 reinterpret_cast<const unsigned char*>(socketPath.c_str()),
                 static_cast<int>(socketPath.size()));
```

### Anti-Patterns to Avoid
- **Linking GTK into the WM binary, even behind `#ifdef`:** CGUI-05 and D-19 require the WM package to
  never depend on GTK under any build configuration; even a conditionally-compiled include of a GTK
  header in a WM source file risks a `pkg-config` probe leaking into the main target's
  `target_link_libraries`. Keep the GUI as a fully separate CMake target with its own source tree.
- **Reading the protocol socket with a blocking `read()` inside the WM's `poll()`-driven loop:** every
  socket fd added to the multiplexer must be handled the way the X connection already is — checked for
  readability via `poll()`, then read with a bound (a single `recv()` call, non-blocking fd) — never a
  blocking call that could stall the whole window manager while a menu is held.
- **Trusting `SO_PEERCRED` alone without the directory-mode/socket-mode belt-and-braces from D-16:**
  the decision explicitly requires **both** the filesystem permissions (0700 dir, 0600 socket) **and**
  the peer-uid check; either alone is a weaker boundary (a shared-uid multi-user host could still see
  the socket path via `/proc` without the directory mode restricting `stat()`/`connect()`).

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| GTK3 widget lifecycle / signal dispatch | A custom event-callback registry | GTK's own `g_signal_connect` + static-callback pattern | GObject's signal system already handles disconnect-on-destroy, multiple listeners, and signal blocking; reimplementing any of this is strictly more code and more bugs for zero benefit |
| Colour/font choosing UI | Custom colour-swatch grid or font-list widget | `GtkColorButton`, `GtkFontButton` (native GTK3 dialogs) | D-10 already locks this; native choosers give correct keyboard nav, screen-reader labels and platform-consistent behaviour for free |
| Peer authentication over a Unix socket | A custom handshake token/nonce scheme | `SO_PEERCRED` (`struct ucred`) | This is the exact mechanism systemd, D-Bus and polkit use for local privilege verification `[CITED: man7.org/linux/man-pages/man7/unix.7.html, community summary of systemd/dbus/polkit usage]`; a hand-rolled token scheme adds attack surface (where is the token stored? how is it rotated?) that SO_PEERCRED sidesteps entirely by asking the kernel |
| XDG runtime directory resolution | A guess-based search across `/run/user`, `/var/run/user`, `$HOME` | Read `$XDG_RUNTIME_DIR` per the freedesktop.org Base Directory Specification, fall back to a single documented alternative (`/tmp/wm2-born-again-<uid>` per D-16) | The spec is explicit about the 0700/user-owned invariant `[CITED: freedesktop.org XDG Base Directory Specification, via ArchWiki secondary summary]`; the project's own `xdgConfigHome()`/`xdgConfigDirs()` (`src/Config.cpp:51-77`) is the precedent for "read the env var, fall back to one documented default, do not chase every possible convention" |

**Key insight:** every item on this list already has a well-understood, standard, kernel- or
toolkit-level answer. The *one* place this phase should deliberately go against that grain is the JSON
codec (see Standard Stack "Alternatives Considered") — because the protocol's actual shape (flat
key/value NDJSON messages, D-14's explicit "status only" ceiling) is simple enough that a ~150-line
hand-rolled codec is smaller and has a smaller dependency footprint than pulling in nlohmann-json for
the WM binary, which is the one new-dependency question this phase actually has to answer carefully
given CGUI-05's "WM works without GTK installed" spirit (extended, by the same logic, to "WM does not
gain a new hard dependency for a GUI-only feature's sake").

## Common Pitfalls

### Pitfall 1: `struct pollfd fds[2]` is not one array, it's two, and they must both grow
**What goes wrong:** A task that adds the socket to `nextEvent()`'s poll array but forgets
`modalWait()` (or vice versa) will produce a WM that services the socket only when idle, and appears
to "freeze" the GUI's live-apply the moment a user opens the root menu, starts a drag, or holds the
tab button — exactly the defect class ledger 8 (referenced in `include/Manager.h:115-124`) already
fixed once for signal delivery.
**Why it happens:** The two functions are textually near-duplicates (both declare `fds[2]`, both poll
the same two descriptors) but are separate functions with separate call sites; a change to one is
invisible in the other's diff.
**How to avoid:** Treat the descriptor set as one shared object (Pattern 2 above) constructed once and
consumed by both functions, rather than editing two literal arrays in parallel.
**Warning signs:** A protocol integration test that passes when the GUI is idle but times out the
moment a Catch2 case interacts with the root menu or a drag operation.

### Pitfall 2: SIGHUP already means "exit"
**What goes wrong:** A task assumes `SIGHUP` is free to repurpose as "reload config" (a common Unix
daemon convention) without checking current behaviour, and either breaks existing shutdown semantics
or silently changes them without the release-notes update D-8.5-01's precedent requires for any
user-visible behaviour change.
**Why it happens:** `SIGHUP`/`SIGTERM`/`SIGINT` are wired to the identical handler at
`src/Manager.cpp:150-153` (`sa.sa_handler = sigHandler;` followed by `sigaction(SIGTERM, ...)`,
`sigaction(SIGINT, ...)`, `sigaction(SIGHUP, ...)`) — there is currently no distinction between the
three signals at the handler level; `m_signalled` is set identically for all three.
**How to avoid:** This is explicitly **Claude's Discretion** per CONTEXT.md — the plan must make an
explicit choice (repurpose `SIGHUP` for reload, documented in release notes; or leave `SIGHUP` as exit
and make reload purely a socket message) and record it as a decision, not an assumption.
**Warning signs:** A plan or test that reads "SIGHUP reloads config" as an existing fact rather than a
phase decision.

### Pitfall 3: GTK apps on a bare VPS droplet emit noisy-but-harmless stderr warnings
**What goes wrong:** A GTK smoke test that asserts "zero stderr output" will flake or fail on a
minimal droplet, because `libgtk-3-0`'s hard dependency on `libatk-bridge2.0-0` (the AT-SPI
accessibility bridge) means GTK always attempts to connect to an accessibility bus at startup; on a
host with no D-Bus session running (a bare VPS with no desktop session), this produces a stderr
warning but does **not** block the app from starting `[CITED: community reports of "AT-SPI: Could not
obtain desktop path or name"-class warnings on headless/minimal Linux; the underlying dependency chain
— `libatk-bridge2.0-0` as a hard `Depends:` — is `[VERIFIED: apt-cache show libgtk-3-0, this host]`]`.
**Why it happens:** GTK3's accessibility integration is unconditional at the library level; it is not
gated behind a runtime feature check for "is a desktop session present."
**How to avoid:** The smoke test (D-20) should assert on the window actually mapping (e.g. via
`xwininfo`/XTEST-detectable state, the project's house pattern for TEST-05-style process tests) rather
than on clean stderr. If clean stderr is desired for the release evidence bundle, explicitly filter
known-benign AT-SPI/dbus warnings rather than asserting zero output.
**Warning signs:** A CI-only-passing test that fails specifically on a headless droplet with no
desktop environment installed.

### Pitfall 4: `sun_path` is 108 bytes on Linux — a deep `$XDG_RUNTIME_DIR` can truncate the socket path
**What goes wrong:** `struct sockaddr_un.sun_path` is fixed at 108 bytes on Linux (107 usable plus a
null terminator) `[CITED: unix(7) man page, and cross-referenced against the Linux kernel
`include/linux/un.h` `UNIX_PATH_MAX` definition]`. `$XDG_RUNTIME_DIR` is typically short
(`/run/user/1000`, verified on this host: `XDG_RUNTIME_DIR=/run/user/1000`), but a socket path that
appends a per-display subdirectory and filename (`/run/user/1000/wm2-born-again/socket-:1` or similar)
must be checked against this bound, especially for the `/tmp/wm2-born-again-<uid>` fallback path named
in D-16, which is even shorter and therefore safer.
**Why it happens:** `sockaddr_un` truncates silently at the struct level if the caller does not check
`strlen(path) < sizeof(sun_path)` before calling `bind()`/`connect()` — a path that's too long produces
a confusing `EINVAL` or, worse, a silently truncated path that binds to the wrong location.
**How to avoid:** Validate the constructed socket path length against `sizeof(sockaddr_un::sun_path)`
at socket-creation time and fail loudly (a clear `wm2:` warning) rather than truncating.
**Warning signs:** `bind()`/`connect()` returning `EINVAL` in a test run under a long
`$XDG_RUNTIME_DIR` (e.g. some container or CI runtime environments use nested paths).

### Pitfall 5: `GtkFontButton::get_font_name()` is deprecated since GTK 3.22
**What goes wrong:** The most obvious API for reading back a chosen font
(`gtk_font_button_get_font_name()`) has been deprecated since GTK 3.22 in favour of the
`GtkFontChooser` interface (`gtk_font_chooser_get_font()`)
`[CITED: docs.gtk.org/gtk3/method.FontButton.get_font_name.html]`. Using the deprecated call still
works on 3.24 (both Ubuntu targets ship 3.24.x) but produces a deprecation warning if
`-Wdeprecated-declarations` is enabled, and risks being wrong advice for anyone extending this to GTK4
later.
**Why it happens:** `GtkFontButton` implements the `GtkFontChooser` interface; the button-specific
getter predates that interface and was kept only for backward compatibility.
**How to avoid:** Use `gtk_font_chooser_get_font(GTK_FONT_CHOOSER(fontButton))` (interface method) and
convert the returned string via `pango_font_description_from_string()` when a `PangoFontDescription`
is needed, rather than the button-specific getter.
**Warning signs:** A `-Wdeprecated-declarations` warning in the `config-gui` build target.

## Code Examples

### Newline-delimited JSON message shape (D-14, D-15)
```
// Source: derived from D-14/D-15's requirements, this session -- not copied from
// an external spec, since the project's protocol is bespoke by design.
// Client -> WM (hello):
{"type":"hello","program":"wm2-config","protocol":1}\n
// WM -> Client (hello ack):
{"type":"hello-ack","program":"wm2-born-again","protocol":1,"version":"0.1.0"}\n
// Client -> WM (set one key, live):
{"type":"set","key":"frame-thickness","value":"9"}\n
// WM -> Client (ack or error):
{"type":"ack","key":"frame-thickness"}\n
{"type":"error","key":"frame-thickness","reason":"out of range"}\n
// WM -> all connected clients (broadcast, D-08):
{"type":"reload"}\n
```
Framing: one JSON object per line, `\n`-terminated, matching the "newline-delimited JSON" shape named
in CONTEXT.md's Claude's Discretion section. A message longer than a reasonable bound (e.g. 4096
bytes, matching the existing `Config::applyFile()` per-line length guard at the config-file level,
`src/Config.cpp` line-length check) should be rejected rather than allowed to grow the read buffer
unbounded.

### Reusing the existing Config parser as a read-only library target
```cmake
# Source: pattern derived from existing test targets in CMakeLists.txt, e.g.
# add_executable(test_config tests/test_config.cpp src/Config.cpp) at line 779 --
# the same "compile Config.cpp directly into the consumer" pattern extends to
# wm2-ctl and wm2-config, neither of which needs the rest of the WM's sources.
add_executable(wm2-ctl apps/wm2-ctl/main.cpp src/Config.cpp)
target_include_directories(wm2-ctl PRIVATE ${CMAKE_SOURCE_DIR}/include)
# No X11/GTK linkage at all -- wm2-ctl is a pure socket client (D-17).
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `gtk_font_button_get_font_name()` | `gtk_font_chooser_get_font()` via the `GtkFontChooser` interface | GTK 3.22 (2017) `[CITED: docs.gtk.org]` | Use the interface method from the start; avoids a deprecation warning under `-Wall`/`-Wextra`-style builds this project already uses |
| `GIOChannel` + `g_io_add_watch()` for socket I/O in GLib apps | `GSocket`/`GSocketClient` + `g_socket_create_source()` | GIO's socket API has been the recommended path for new code for many GLib release cycles `[CITED: GNOME developer documentation, "Sockets" overview]` | Relevant only inside `wm2-config`'s protocol client; the WM side does not use GLib at all (it is raw Xlib + POSIX), so this only affects the GUI process |

**Deprecated/outdated:**
- `gtk_font_button_get_font_name()`: superseded by the `GtkFontChooser` interface (GTK 3.22+); still
  functional on the GTK 3.24 shipped by both Ubuntu 22.04 and 24.04, but should not be used in new
  code.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Plain C GTK3 API (no gtkmm) is the "standard/expert" choice for a project of this size and style | Standard Stack, Alternatives Considered | If wrong, the plan adopts a thinner-than-ideal abstraction and the GUI code carries more manual GObject casting boilerplate than a gtkmm-based equivalent would; reversible later by rewriting the presentation layer only, since the protocol/model layer is deliberately GTK-API-agnostic |
| A2 | A ~150-line hand-rolled NDJSON codec is sufficient for the D-14 message set and safer than adding nlohmann-json as a new WM dependency | Standard Stack, Don't Hand-Roll | If the message set grows (e.g. nested per-window data added later per the deferred D-14 reversibility note), a hand-rolled codec may need rework or replacement; low risk given D-14's flat, status-only ceiling is a locked decision, not speculative |
| A3 | A minimal 3-page GTK3 app's RSS will land in the 30-60 MB range on a VPS-class VNC session | Standard Stack rationale / Common Pitfalls context | This is the single most consequential unverified number in this document — the 512 MB VPS budget (CLAUDE.md constraint) could be meaningfully affected if actual RSS is higher; D-20 already mandates a measured VNC pass specifically because this number is not to be trusted until measured — **do not let a plan skip that measurement on the strength of this estimate** |
| A4 | `libatk-bridge2.0-0`'s accessibility-bus connection attempt produces a stderr warning but never blocks GTK3 app startup on a host with no D-Bus session | Common Pitfalls (Pitfall 3) | If wrong (i.e. if it actually blocks or delays startup materially), the D-20 GTK smoke test under Xvfb could flake or need a longer timeout; low risk, this is a widely reported and long-standing GTK behaviour, but not independently reproduced in this session (no bare droplet available to test against) |
| A5 | Ubuntu 24.04 `libgtk-3-dev`/`nlohmann-json3-dev` versions (3.24.41-4ubuntu1.3 / 3.11.3-1) are current as of research date | Standard Stack, Installation | Sourced from Launchpad web search, not verified via `apt-cache` on an actual 24.04 host in this session; if wrong, only affects documentation accuracy, not functional correctness, since `BUILD_CONFIG_GUI AUTO` probes the actual installed version at configure time regardless of what this document says |

**If this table is empty:** N/A — see rows above.

## Open Questions

1. **Exact wire spelling of the protocol's message `type` field values and error taxonomy**
   - What we know: D-14/D-15 name the required operations (hello, get, set, reload, status, one
     broadcast notice) and require idempotency and clear error replies.
   - What's unclear: the precise JSON key names and error-code vocabulary are explicitly left to
     Claude's Discretion in CONTEXT.md; this document proposes a shape (Code Examples section) but the
     plan is where this becomes a locked decision.
   - Recommendation: the plan should treat the protocol's JSON schema itself as a small, versioned
     design artifact (perhaps a comment block in `ConfigProtocol.h` enumerating every message type),
     since `wm2-ctl`, `wm2-config` and the WM's dispatcher all need to agree on it exactly.

2. **Whether `SIGHUP` is repurposed for "reload config"**
   - What we know: today `SIGHUP` shares the WM's exit handler with `SIGTERM`/`SIGINT`
     (`src/Manager.cpp:150-153`); CONTEXT.md leaves this to Claude's Discretion.
   - What's unclear: whether repurposing it is worth the behavioural change and release-note burden,
     versus leaving reload purely as a socket-triggered action.
   - Recommendation: default to leaving `SIGHUP` as-is (lowest risk, no behavioural change, no release
     note needed) unless the plan has a concrete reason a signal-triggered reload is needed
     independent of the GUI (e.g. for `wm2-ctl reload` to work even if the socket is somehow wedged —
     but `wm2-ctl reload` can simply be a protocol message like any other `wm2-ctl` command, which
     does not require touching signal handling at all).

3. **Actual RSS of `wm2-config` under a real VNC/XRDP session**
   - What we know: comparable GTK3 apps observed on this workstation via `/proc/<pid>/status` VmRSS
     range from ~31 MB (gnome-system-monitor) to ~48 MB (gnome-terminal, gedit) — real, currently
     running processes on this host, not launched for this research, sampled read-only via `/proc`.
   - What's unclear: `wm2-config` itself does not exist yet, so this is an analogy, not a measurement,
     and none of those apps are structurally identical (they all use more GTK subsystems — text
     rendering, VTE, etc. — than a 3-page settings form needs).
   - Recommendation: D-20 already requires a measured pass; the plan should make sure the RSS
     measurement helper already used for the WM (`residentKb()`, `tests/test_wm_resource.cpp:275`,
     reading `/proc/<pid>/status` `VmRSS`) is reused or duplicated for `wm2-config`'s own process,
     rather than inventing a new measurement mechanism.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| `pkg-config` | CMake `pkg_check_modules` probing for `gtk+-3.0` | ✓ | present on this host (used throughout existing CMakeLists.txt) | — |
| `gtk+-3.0` (pkg-config module) | `wm2-config` build | ✗ | not found by `pkg-config --exists gtk+-3.0` on this workstation `[VERIFIED: this session]` | `BUILD_CONFIG_GUI AUTO` resolves to OFF; `wm2-config` is simply not built; WM and `wm2-ctl` build unaffected (D-18) |
| `libgtk-3-0` (runtime) | Running a previously-built `wm2-config` | ✓ | 3.24.33-1ubuntu2.2, already installed on this host (dependency of other installed apps) | — |
| `libgtk-3-dev` (build headers) | Building `wm2-config` | ✗ | not installed (`Instalirano: (nema)` = "Installed: (none)") | Install via `sudo apt install libgtk-3-dev`; without it, same AUTO-OFF fallback as above |
| `nlohmann-json3-dev` | Not required — hand-rolled codec recommended instead | ✗ | not installed | N/A — not selected as a dependency (see Standard Stack) |
| Xvfb | D-20's GTK smoke test under ctest | not probed this session (instructed not to run Xvfb) | — | Existing test infrastructure (`tests/support/WmFixture.h`) already depends on Xvfb for the WM's own process tests, so its presence is an existing precondition of this project's test suite, not a new one introduced by this phase |
| `$XDG_RUNTIME_DIR` | D-16's socket directory location | ✓ | `/run/user/1000`, mode `0700` (verified via `ls -ld`) `[VERIFIED: this session, this host]` | Fallback path `/tmp/wm2-born-again-<uid>` per D-16 if unset |

**Missing dependencies with no fallback:** none — every missing dependency above has a documented,
decision-backed fallback (AUTO-OFF for GTK, hand-rolled codec instead of nlohmann-json).

**Missing dependencies with fallback:**
- `gtk+-3.0`/`libgtk-3-dev`: `BUILD_CONFIG_GUI AUTO` → OFF; WM and `wm2-ctl` unaffected (D-18).
- `nlohmann-json3-dev`: not needed at all under the recommended hand-rolled-codec approach.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 v3.14.0 (FetchContent-pinned, `CMakeLists.txt:76`) `[VERIFIED: CMakeLists.txt:76, this repo, this session]` |
| Config file | `CMakeLists.txt` itself (no separate Catch2 config file); tests registered via `catch_discover_tests(... ADD_TAGS_AS_LABELS)`, the project's D-33 convention |
| Quick run command | `ctest --test-dir build/debug -L <new-tag> --no-tests=error` (per-feature tag, matching the existing per-file tag convention, e.g. `[wm_focus]`) |
| Full suite command | `bash scripts/gates/build-all.sh debug` (builds + runs the entire ctest suite; `release` and `asan` trees are the release-signoff gates per TEST-06) |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| CGUI-01 | `wm2-config` builds when GTK3 present, is absent (not broken) when it is not | build-config | `cmake -S . -B build/test-nogtk && cmake --build build/test-nogtk` with `pkg-config` masked, plus the normal build with GTK present | ❌ Wave 0 — new CMake logic |
| CGUI-02 | Socket handshake, get/set/reload/status round-trip via raw client and via `wm2-ctl` | integration | `ctest --test-dir build/debug -L config_protocol` | ❌ Wave 0 — `tests/test_config_protocol.cpp` |
| CGUI-03 | Editing fonts/colors/focus policy/frame thickness/menu entries updates the `Config` struct and the on-disk file surgically | unit | `ctest --test-dir build/debug -L config_writer` | ❌ Wave 0 — `tests/test_config_writer.cpp` |
| CGUI-04 | A `set` message over the socket visibly changes running WM state (frame thickness, colour, font, focus policy, menu) without restart | integration (Xvfb, real WM process) | `ctest --test-dir build/debug -L config_live_apply` | ❌ Wave 0 — extends `tests/support/WmFixture.h` usage pattern with a socket client |
| CGUI-05 | WM builds/runs identically with `BUILD_CONFIG_GUI=OFF` | build-config + smoke | Re-run existing full suite (`build-all.sh debug`) with `-DBUILD_CONFIG_GUI=OFF` and diff test counts against the GTK-present run | ❌ Wave 0 — no new test file, a build-matrix addition to the gate script |

### Sampling Rate
- **Per task commit:** the newly-tagged ctest label for whatever the task touched (e.g.
  `-L config_protocol`), following the existing per-file-tag convention.
- **Per wave merge:** `bash scripts/gates/build-all.sh debug` (full suite, matching current project
  practice for every other phase).
- **Phase gate:** Full `debug`/`release`/`asan` matrix green before `/gsd-verify-work`, per TEST-06 —
  this phase does not get an exception to that standing release-signoff requirement.

### Wave 0 Gaps
- [ ] `tests/test_config_protocol.cpp` — covers CGUI-02, a raw NDJSON socket client plus a `wm2-ctl`
      subprocess driver
- [ ] `tests/test_config_writer.cpp` — covers CGUI-03's surgical-edit half (D-02), display-free, no
      Xvfb needed (follows the `include/MenuPaint.h`/`test_menupaint.cpp` house pattern)
- [ ] `tests/test_config_live_apply.cpp` — covers CGUI-04, a real WM process under `WmFixture`
      receiving socket `set` messages and asserting the visible X11-level effect (frame geometry,
      colour pixel, font metrics)
- [ ] `tests/test_wm2_config_smoke.cpp` (or a shell-driven ctest entry) — the D-20 GTK smoke test
      under Xvfb, skipped with a stated reason (`SKIP`/`std::exit` with informative message via
      Catch2's own skip mechanism, matching how this project already handles conditionally-absent
      capabilities such as the RANDR/Shape-less fallback tests) when `BUILD_CONFIG_GUI` resolved OFF
- [ ] CMake: no `install()` rules exist anywhere in `CMakeLists.txt` today
      (`[VERIFIED: grep '^install(' CMakeLists.txt` returns nothing, this session]`) — D-19's two
      install components (`wm`, `config-gui`) are entirely new CMake surface, not an extension of an
      existing pattern

## Security Domain

`workflow.security_enforcement` is absent from `.planning/config.json`
(`[VERIFIED: grep -i security_enforcement .planning/config.json` returns nothing, this session]`),
which per the governing instruction means **enabled** (absent = enabled).

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | yes | Not username/password — this is local-machine, same-uid process authentication via `SO_PEERCRED` (D-16), the correct primitive for this trust model per the "Don't Hand-Roll" table |
| V3 Session Management | partial | The socket connection itself is the "session"; D-15's hello handshake and D-16's per-connection uid check together are the session's establishment and validation |
| V4 Access Control | yes | D-16's filesystem-mode (0700 dir, 0600 socket) plus the `SO_PEERCRED` check together form the access-control boundary; D-14 additionally scopes *what* an authenticated connection may see (status only, never per-window data) |
| V5 Input Validation | yes | Every protocol message and every config-file key/value the GUI writes must be validated the same way `Config::applyKeyValue()` already validates values from the config file (range checks, e.g. `clampInt`, `src/Config.cpp`); the JSON codec itself must reject malformed/oversized messages (Pitfall 4-adjacent: bound message length) |
| V6 Cryptography | no | No cryptographic material in this phase — the socket is same-host, same-uid, filesystem-permission-gated, not network-exposed; introducing TLS/crypto here would be solving a problem D-16 already declared out of scope by design |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| A second local user (or a compromised process running as a different uid) connects to the socket to read status or push config changes | Spoofing / Elevation of Privilege | D-16's combined filesystem-mode + `SO_PEERCRED` check; **must** be implemented as a hard reject with logging, not merely a UI-level restriction |
| A malformed or oversized JSON message is sent to exhaust the WM's read buffer or crash the parser | Denial of Service | Bound message length at the framing layer (Pitfall-adjacent, "message longer than a reasonable bound... rejected") before any JSON parsing is attempted |
| A malicious `wm2-config`-alike binary (not the real GUI) connects and sends a `set` for an out-of-range value (e.g. negative frame thickness, oversized delay) | Tampering | Every `set` must re-run the exact same validation `Config::applyKeyValue()` already performs on config-file values (`clampInt`, boolean parsing, integer range checks) — the socket path must not be a shortcut around validation that the file-parsing path already enforces |
| The GUI process itself is compromised or buggy and writes a corrupted config file | Tampering | D-02's surgical edit plus an atomic write (temp file + rename) means a crash mid-write leaves the original file intact — this is a correctness property, not just a security one, but it also closes a partial-write attack/failure window |
| A stale socket file left behind by a crashed WM process is treated as live by a new WM instance on restart, or blocks it from binding | Denial of Service (self-inflicted) | Startup must detect and clean up a stale socket (e.g. `connect()` to it first; `ECONNREFUSED` means stale, safe to `unlink()` and re-bind; a successful connect means another WM instance is genuinely running, matching the existing `_WM2_RUNNING` selection-ownership pattern already used to detect "another WM is running") |

## Sources

### Primary (HIGH confidence)
- This repository, read directly this session: `include/Config.h`, `src/Config.cpp`,
  `include/Manager.h`, `src/Manager.cpp`, `src/Border.cpp`, `src/Events.cpp`, `src/Buttons.cpp`,
  `src/DesktopEntry.cpp`, `include/AppEntry.h`, `include/MenuPaint.h`, `include/TimestampWait.h`,
  `include/EventPump.h`, `CMakeLists.txt`, `scripts/preflight.sh`, `scripts/gates/build-all.sh`,
  `tests/support/WmFixture.h`, `tests/test_wm_resource.cpp`
- `unix(7)` Linux manual page (`man7.org/linux/man-pages/man7/unix.7.html`) — `SO_PEERCRED`/
  `struct ucred` semantics, `sun_path` length
- `apt-cache policy`/`apt-cache show` output on this workstation (Ubuntu 22.04.5 LTS) for
  `libgtk-3-dev`, `libgtkmm-3.0-dev`, `nlohmann-json3-dev`, `libgtk-3-0`
- `/proc/<pid>/status` VmRSS readings for currently-running GTK3 processes on this workstation

### Secondary (MEDIUM confidence)
- `docs.gtk.org` GTK3 API reference (`GtkFontButton`, `GtkFontChooser` deprecation notes)
- Launchpad Ubuntu Noble (24.04) package pages for `libgtk-3-dev`/`nlohmann-json3-dev` versions
  (not independently verified on a 24.04 host this session)
- freedesktop.org XDG Base Directory Specification (via ArchWiki secondary summary) for
  `$XDG_RUNTIME_DIR` conventions
- GNOME developer documentation summary for GIO `GSocket`/`GSocketClient` as the modern replacement
  for `GIOChannel`

### Tertiary (LOW confidence)
- Community reports (forum threads, GitHub issues) on GTK3 AT-SPI/dbus warning noise on headless
  Linux — consistent across multiple independent sources but not independently reproduced against an
  actual bare VPS droplet this session
- Anecdotal GTK3/GTK4 "hello world" RSS figures (szibele.com toolkit comparison, a Rust/GTK4 blog
  post) — used only to sanity-check the order of magnitude, superseded in practice by D-20's mandated
  measured pass and by this session's own `/proc` sampling of comparable real GTK3 apps on this host

## Metadata

**Confidence breakdown:**
- Standard stack: MEDIUM — package names and versions verified against this host and cross-checked
  against Launchpad for the second target OS; the "plain C vs gtkmm" recommendation is an architectural
  judgment call (`[ASSUMED]`), not a verified industry consensus
- Architecture: HIGH for the WM-side integration facts (all read directly from source this session,
  with exact line numbers and verbatim quotes); MEDIUM for the GTK3-side patterns (well-documented but
  not verified by compiling anything, since no code was written or built this session)
- Pitfalls: MEDIUM-HIGH — the poll()-array and SIGHUP pitfalls are HIGH confidence (verified from
  source); the RSS and AT-SPI-warning pitfalls are LOW-MEDIUM (reasoned from dependency chains and
  community reports, not measured against a real droplet)

**Research date:** 2026-09-06
**Valid until:** 2026-10-06 (30 days — GTK3/Ubuntu package versions are stable; re-verify if the
target Ubuntu release changes or if a 24.04 build host becomes available for direct verification)
