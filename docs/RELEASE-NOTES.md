# wm2-born-again — Release Notes

A modernised resurrection of Chris Cannam's wm2 (1997), adapted for VPS droplets
reached over VNC or RDP. The sideways-tab look is unchanged and deliberately so;
the internals underneath it are not.

These notes cover **Phase 8**, which is the release that makes the window manager
honest about the servers it runs on: what it does when an X extension is missing,
which windows are allowed to steal your focus, how to write a rule that moves a
window where you want it, and — the part most likely to surprise you — how it
treats a display made of more than one physical screen.

Read the [Limitations](#limitations) section before the feature list. It is
first on purpose.

---

## Limitations

### Single-screen only: the screen is one rectangle, however many monitors are behind it

**wm2-born-again is a single-screen window manager. It treats the whole X screen
as a single rectangle, regardless of how many physical outputs or CRTCs it
spans.**

Concretely: if your session presents a 3840x1080 screen made of two side-by-side
1920x1080 monitors, this window manager sees one 3840x1080 desktop. A window
being kept on-screen is clamped to *that whole rectangle*, not to the nearest
monitor. Maximise fills both monitors. Fullscreen covers both monitors. A window
nudged back into view may land straddling the bezel.

**This is a deliberate design choice, not an oversight.** The target this project
is built for is a VPS droplet reached over VNC or RDP, where the session is one
virtual display of one size. Per-output awareness — Xinerama or RANDR CRTC
geometry, "maximise on the monitor the window is mostly on", per-monitor
workareas — is a real feature that a real desktop needs, and it is a *possible
future capability*, listed as out of scope for this version rather than as
something already half-built.

What you should expect today:

| You have | What happens |
|---|---|
| One monitor, or one virtual remote display | Everything behaves as you would expect. This single-screen case is the tested configuration. |
| Two monitors as one X screen | Windows are managed correctly, but maximise, fullscreen and on-screen clamping all use the combined rectangle. |
| Two monitors as two X screens (`:0.0`, `:0.1`) | Not supported. The window manager manages one screen. |

The window manager does track **resolution changes**: if RANDR is available and
the screen is resized under it — which is exactly what happens when you reconnect
a VNC client at a different size — windows are reflowed back into the new
rectangle rather than stranded off the edge.

### Other things not in this release

- **No virtual desktops or workspaces.** There is one desktop. `_NET_NUMBER_OF_DESKTOPS`
  is fixed at 1. This is why window rules have no "send to workspace" action; see
  [Window rules](#window-rules) below.
- **No keyboard shortcuts or key bindings.** The mouse and the root menu are the
  whole interface.
- **No compositing, transparency, icons or a system tray.** wm2's philosophy, kept.
- **No session management (XSMP).**

---

## Appearance

The silhouette is unchanged — the sideways tab down the left edge of every
window, with the title running down it and a small button at its top. What
changed in this release is the surface.

**Ubuntu, in bold, for tab labels and menus — by default.** The default chain is
`Ubuntu,Noto Sans,DejaVu Sans,Sans`, so a host without the Ubuntu family still
resolves something sensible; nothing about the fallback ladder changed. Bold is
deliberate rather than decorative: on a server without the RENDER extension —
TightVNC, for one — Xft falls back to unantialiased rendering, and bold survives
that where lighter weights go ragged. The tab label is bold and the menu is not,
which is why they are two settings rather than one.

**The two fonts are settings now.** `tab-font` sets the face the sideways tab
label is drawn in, `menu-font` the face of the root menu, each taking a
fontconfig pattern — `Monospace:size=14`, `Noto Sans:bold:size=11` — in the
config file or on the command line as `--tab-font=` and `--menu-font=`. There is
no separate size key on purpose: the pattern already carries the size, and two
ways of saying it could disagree. Leave either unset and you get exactly the
face the previous release drew, character for character.

A pattern fontconfig cannot resolve is substituted rather than refused, and the
tab's fallback ladder is deliberately not configurable, so no font value can
leave you without a window manager.

**A font change takes effect the next time the window manager starts.** It is
the one setting in this release that does not apply to windows already on
screen; the configuration tool will apply it live.

**A silver palette with black text.** The defaults are now a single cool-cast
family, `#C8CACC` for the tab and menu, `#DCDEE0` for the frame, `#A8ACB0` for
the menu highlight. Every silver is very slightly blue — two parts per channel —
which is what makes it read as metal rather than as concrete. On a 16-bit remote
session that cast quantises away and you get plain grey, which is simply the
older look rather than a broken one.

**A one-pixel bevel, on the focused window only.** The tab and its button carry a
highlight along their top and left edges and a shadow along the bottom and right,
so the focused window appears very slightly raised. Unfocused windows are flat.

That last point is the useful part: it extends what this window manager already
did — an unfocused window's frame is hidden, so activity was already something
you could see — rather than adding a competing colour to keep track of. The
diagonal at the tab's foot is deliberately left plain, because a bevel following
a stair-stepped edge is a row of disconnected pixels rather than a highlight.

The bevel shades are **derived from whichever tab background you configure**, not
fixed. Set a dark palette and you get bevels that belong to it. There are no
separate keys to set, and so no way to set them inconsistently.

All nine colours remain configurable — `tab-background`, `frame-background`,
`menu-highlight` and the rest — in the config file and on the command line, and
as of this release the two fonts, `tab-font` and `menu-font`, are configurable
the same way.

---

## Focus behaviour

### Focus-stealing prevention is on by default

A window that maps itself while you are typing somewhere else **does not get your
focus**. The window manager compares the window's `_NET_WM_USER_TIME` against the
time of your last real interaction; a window that cannot show it was opened
because of something *you* did is mapped, framed and left unfocused.

This is on by default because the alternative — the behaviour most window
managers shipped for a decade — is that a slow-starting application steals your
keystrokes half a sentence into a different window.

If you want the old behaviour:

```
focus-stealing-prevention = false
```

or on the command line, `--no-focus-stealing-prevention`.

The same arbitration applies to `_NET_ACTIVE_WINDOW` requests: a request from a
pager or a taskbar is honoured, because a pager only sends one when a human
clicked it. A request from an application asking for itself is subject to the
same user-time test as a map.

### The three focus-policy settings now actually work

**This is a bug fix, and it is worth stating plainly: `click-to-focus`,
`raise-on-focus` and `auto-raise` were parsed and then ignored.** They had been
dead settings since the configuration struct was written. They are wired to
behaviour now, and each one is covered by a test that drives the real binary
with real pointer events.

If you had these in your config file and wondered why nothing changed — nothing
was changing. It will now.

**The documented defaults were corrected to match what the binary already did**,
rather than the binary being changed to match the documentation. Nobody's
existing session behaves differently as a result:

| Setting | Default | What it does |
|---|---|---|
| `click-to-focus` | `false` | When false, focus follows the pointer. When true, you must click. |
| `raise-on-focus` | `true` | A window that takes focus is also raised. |
| `auto-raise` | `true` | A window under a pointer that has stopped moving is raised, after `auto-raise-delay` (default 400 ms). |

### Right-click circulation no longer freezes the window manager

Right-clicking the root window to cycle through windows could wedge the window
manager at 100% CPU, permanently, and it did not take an exotic situation to
trigger — a right-click on a freshly started session with nothing open was enough.
Fixed, with a regression test that asserts both that the window manager is still
*responsive* and that it is still *idle*, because "alive" and "working" are
different claims and the bug satisfied the first.

---

## Window rules

Rules let you say "this application always opens there, that size, without a
frame". They live in the same `key = value` configuration file as everything else
— there is no second file format and no numbered-key scheme to keep in step.

```
rule-match-class    = Firefox
rule-match-instance = navigator
rule-position       = 100,100
rule-size           = 800x600

rule-match-type     = dialog
rule-no-decorate    = true
```

### Where a rule begins

One sentence, and it is the only thing you have to remember:

> **A new rule begins at the first `rule-match-*` line that follows an action
> line — or at the very first rule line in the file.**

Consecutive match lines attach to the rule that is currently open and are AND-ed
together, so the first block above matches a window that is *both* class `Firefox`
*and* name `navigator`. Action lines attach to that same open rule. Keys that are
not rule keys are transparent: putting `frame-thickness = 9` in the middle of a
rule does not end it.

### What you can match on

| Key | What it matches |
|---|---|
| `rule-match-class` | The `WM_CLASS` hint. The forgiving one: it is tested against **both** WM_CLASS fields, so "Firefox" works whether that is the instance name or the class name. |
| `rule-match-instance` | The `WM_CLASS` **instance name** only. The precise one. |
| `rule-match-title` | The window's **title** — the text the application puts in its own title bar, and the text this window manager paints down the sideways tab. |
| `rule-match-type` | An EWMH window type. Exactly four are accepted: `normal`, `dock`, `dialog`, `notification`. |
| `rule-match-mode` | How the text matches — exact or substring. |

Criteria combine with AND: every one you set must match. A rule with no criteria
at all matches **nothing**, so a typo that drops your only match line disables
that rule rather than applying it to every window on the screen.

**A title rule looks at the title the window has when it opens.** Rules are
applied once, at the moment a window appears, and are not re-checked afterwards.
So a rule keyed on a title the application only sets later will not fire — and,
just as deliberately, renaming a document will not make its window jump to the
position some rule specifies. That second half is the reason for the first: a
window that re-positioned itself every time its title changed would be unusable
in a text editor or a browser. Most window managers resolve it the same way.

If you want to catch an application whose title changes, match on its class
instead — `rule-match-class` and `rule-match-instance` read the `WM_CLASS` hint,
which applications set once and rarely change.

> **If you used `rule-match-name` before v1.0**, rename it to
> `rule-match-instance`. It compared the `WM_CLASS` instance name while its name
> said `WM_NAME`, which was confusing enough to have put a wrong sentence in this
> very document. The old spelling is gone rather than kept as an alias, so a
> configuration still using it will warn about an unknown key on startup.

**Only those four window types are accepted.** `utility`, `splash` and `toolbar`
are *not*: the window manager collapses them into `normal` internally, so a rule
naming one could never fire. Writing one produces a warning and leaves the
criterion unset — and since a rule with no criteria matches nothing, a rule whose
only line was `rule-match-type = utility` silently does nothing at all. Use
`normal` for those windows.

### The four actions that ship

| Action | Effect |
|---|---|
| `rule-no-decorate` | The window is managed but gets no frame and no tab. |
| `rule-position` | `x,y` — where the window is placed when it maps. |
| `rule-size` | `WxH` — the size it is given when it maps. |
| `rule-skip-taskbar` | `true`/`false` — sets `_NET_WM_STATE_SKIP_TASKBAR` so pagers and taskbars leave the window out. |

**There is deliberately no "send to workspace" action.** This window manager is
single-desktop by design, so there is no second workspace for a rule to send a
window to. The requirement that originally named a fourth workspace action was
amended rather than left as a promise that could never be kept — implementing it
would mean acquiring a desktop model this project does not intend to have.

### Rules are configuration-file only

There are no `--rule-*` command-line flags, and that is a decision rather than an
omission. Repeated ordered groups do not fit a command line: there is no way to
express "this `--rule-position` belongs to *that* `--rule-match-class`". Every
other setting has a flag; these do not.

---

## Behaviour on servers that are missing pieces

Remote-desktop X servers vary enormously in which extensions they offer. Three
matter to this window manager, and none of the three is fatal any more. You can
check what a given server offers with the capability-capture script shipped in
this repository:

```
bash scripts/capture-display-capabilities.sh :2 my-session
```

### Without the Shape extension

The frames and tabs are **rectangular** instead of shaped. Everything works —
moving, resizing, the tab, the button, the menu — it simply does not have the
cut-out silhouette. Importantly, on a server without Shape the window manager
issues **no Shape requests at all**, rather than issuing them and having each one
rejected.

You can see this fallback on a server that *does* have Shape by setting
`WM2_FORCE_NO_SHAPE=1` in the environment, which is how it is tested.

### Without RANDR

Screen geometry is read once at startup and the window manager does not learn
about later resolution changes. Windows are not reflowed when you reconnect a
client at a different size, so a window that was on the right of a wide desktop
can end up off the edge of a narrower one. Everything else is unaffected. The
equivalent environment lever for testing is `WM2_FORCE_NO_RANDR=1`.

### Without XRender

Font rendering falls back to the X server's core glyph path inside libXft. This
was measured on a RENDER-less server during development, and the good news is
better than expected: **the sideways tab still renders rotated**. The wm2 visual
identity survives a server with no RENDER extension.

What can still differ, and what you should not be surprised by: on a server that
is *also* short of fonts, a tab label may render unrotated, or be absent
altogether. The window manager degrades down a ladder — the preferred font family
chain, then a generic sans chain, then an unrotated face, then no label — and it
keeps running and keeps managing your windows at every rung. It will not exit
because it could not find a font. Earlier versions did exactly that.

If your labels are missing or wrong, the fontconfig section of the capability
capture above tells you which of those rungs your server landed on.

---

## Remote-desktop support status

The stated target set for this project is TigerVNC, TightVNC, XRDP and X2Go.

| Target | Status |
|---|---|
| **Xvfb** (headless) | **Validated continuously.** The entire automated suite — hundreds of cases, including everything that drives the real compiled binary — runs on Xvfb on every build. |
| **Xephyr** (nested) | **Validated.** Runtime smoke transcript captured in the release evidence bundle. |
| **TigerVNC** | **Validated.** Real session, human at the client. SHAPE, RANDR and RENDER all present; capability transcript, root properties and window tree committed under the phase evidence bundle. |
| **XRDP** | **Validated.** As above, same extension result. |
| **X2Go** | **Partially validated.** `nxagent 3.5.99.26` — the X server X2Go uses — was captured headlessly, nested on a local Xvfb. It advertises SHAPE, RANDR and RENDER (23 extensions in total), and the window manager frames clients on it at the standard `+24+8` client offset. What was **not** exercised is X2Go's own path: its agent start-up wrapper, with an NX compression proxy over SSH in front of the agent. Transcript committed under `.planning/phases/08.5-v1.0-closeout/evidence/x2go-nxagent/`, whose scope note states that difference. See below. |
| **TightVNC** | **Validated.** Headless capability capture against `TightVNC 1.3.10` (from `tightvncserver 1:1.3.10-5`), started directly, with no viewer attached and no network-exposing flag. It advertises seven extensions in total — SHAPE present, RANDR absent, RENDER absent — which makes it the only server in this project's evidence that genuinely lacks the extensions the fallbacks were built for. The window manager started on it, both fallback ladders announced themselves on stderr, a client was framed, and no X protocol error was logged. Transcript committed under `.planning/phases/08.5-v1.0-closeout/evidence/tightvnc/`, which records beside it what the capture does not settle. See below. |

All four stated targets now have a committed capability transcript, each captured
at a known commit. Two of them — TigerVNC and XRDP — were exercised in real
sessions with a person at the client. The other two — TightVNC, and `nxagent`,
the server that sits behind X2Go — were captured headlessly, with no viewer
attached and nobody in the loop. On that evidence the project requirement
covering "compatible with all four out of the box" is **met**.

Four transcripts is not the same thing as four fully exercised targets, and the
table above keeps those apart on purpose. What a headless capture settles is the
extension surface a server offers, and that this window manager starts, frames
and manages windows on it. What it does not settle is how that target *feels* to
use — whether the tab label looks right, whether a drag or a menu behaves over a
real connection. That is what the two sections below, and the interaction
checklist in the release evidence bundle, are for.

### X2Go: measured at the agent, untested through the proxy

X2Go is **partially** validated for this release, and the two halves are worth
keeping apart.

**What was measured.** `nxagent 3.5.99.26` — the X server X2Go runs — was
captured headlessly, nested on a local Xvfb, with this window manager started on
it and a client framed. It advertises SHAPE, RANDR and RENDER, 23 extensions in
total, and the window manager's own capability probes agree with `xdpyinfo`. The
client is reparented at the standard `+24+8` offset and appears in
`_NET_CLIENT_LIST`. The whole capture took about two minutes, with nobody sitting
through it.

That corrects two things an earlier draft of this document asserted. It said the
X2Go server package was absent from the validation host; `/var/log/dpkg.log`
records `x2goserver-common:all 4.1.0.3-5` installed at **2026-08-29 17:43**, so
it was there all along. And it said none of the TigerVNC or XRDP result carried
over, on the grounds that `nxagent`'s extension surface was unknown. It is known
now, and it is the same surface.

**What was not measured, and this is the part that matters.** X2Go does not
simply run `nxagent`. It starts the agent through `x2gostartagent`, with an **NX
compression proxy** between agent and client over SSH — and that proxy sits in
front of exactly the extension surface everything above depends on. Nothing in
this capture exercises it. Specifically untested:

- menus and drag-move under compression and network latency;
- the long-press delete timing, the one interaction with a real clock in it;
- resize behaviour when a session is reconnected at a different geometry;
- whatever the proxy does or does not forward for SHAPE.

Nothing was judged by eye either. This was a headless capture: no screenshot, and
no verdict on how the sideways tab looks.

**So this is a floor, not a tick.** The transcript directory carries a `SCOPE.md`
that says so in the same words, because three green files in a directory named
for X2Go read, at a glance, like "X2Go: tested".

What to expect meanwhile: X2Go is unexercised, not unsupported. If the sideways
tab renders wrongly or frames come out unshaped under `nxagent`, that is a real
bug worth reporting — and given the RENDER dependency it is the likeliest place
for one to be hiding.

### TightVNC: measured, and the result disproved the reason for skipping it

TightVNC **is** validated for this release, headlessly. It turned out to be the
most informative capture in the bundle, because it is the only one taken against
a server that genuinely lacks the extensions this window manager builds fallbacks
for.

| Server | Extensions | SHAPE | RANDR | RENDER |
|---|---|---|---|---|
| TigerVNC 1.12.0 | many | yes | yes | yes |
| **TightVNC 1.3.10** | **7** | **yes** | **no** | **no** |

TigerVNC 1.12 is a current server; TightVNC's Unix server side is still 1.3.10,
from 2009. An earlier draft of this document skipped TightVNC on the argument
that a TigerVNC transcript was evidence enough for it, the two servers being
related. On the two extensions that argument named, the two servers do not
overlap at all. The argument was retired rather than softened, and why it failed
is kept in `COMPILED_CODE_BEHAVIOR_CHECKLIST.md`, because the general form of it
is worth recognising: a family resemblance between two servers is not a
measurement of either one.

**What the window manager did about it.** It started, and both fallback ladders
announced themselves on stderr rather than degrading in silence:

```
  Shape extension available.
wm2: warning: no xrandr extension, screen geometry will track resolution changes
     via the root window only
wm2: warning: no xrender extension, tab labels will be drawn through the core
     X11 glyph path
```

The client was framed normally — frame, tab, button and resize handle all
present — and no X protocol error was logged. There is one visible difference
from the RENDER-capable targets, and it is expected: the client sits at `+21+8`
inside its frame here rather than at `+24+8`, because the tab width is measured
from whatever font fontconfig resolves, and a lower rung of the font ladder
resolves a different one. The frame narrows to match. The geometry follows the
font, exactly as it is designed to.

**What this does not tell you.** Nothing was judged by eye: no viewer was
attached and no screenshot was taken, so whether the core-X11 glyph path *looks*
acceptable is unanswered — and that is precisely where a fallback tends to be
ugly rather than broken. No interaction was exercised over a real TightVNC
connection. And resolution changes were not tested, which on a server with no
RANDR is the interesting case, since the fallback watches the root window
instead. Those limits travel with the transcript, in its own `SCOPE.md`.

### A note on how "compatible" should be read

Each validated target is validated by a session on one server version, on one
host, on one day. That is what a manual compatibility check *is*; it is not
continuous testing, and a different server version could behave differently. The
committed transcripts record the exact server, extension set and commit each
result belongs to, so a later surprise can be attributed rather than argued about.

---

## Resource use

The project constrains itself to running comfortably on a 512 MB VPS alongside a
VNC server. From this release that is a test the suite enforces rather than a
sentence in a document:

- **Resident memory** with twenty windows open is asserted against a fixed budget
  established from a separate calibration run. Measured on the reference host:
  **11.8 MB** with twenty windows managed, against a 24 MB budget. A managed
  window costs roughly six kilobytes.
- **Idle CPU** with twenty windows open and no input is asserted to stay under 1%
  of wall clock over a thirty-second window. Measured: **zero CPU ticks**. The
  event loop blocks; it does not poll.

---

## For developers

`COMPILED_CODE_BEHAVIOR_CHECKLIST.md` at the repository root is the release and
signoff checklist for this project. It is an ongoing developer artifact rather
than a document belonging to any one release: a checkbox in it is not done until
there is an automated test, a recorded manual run, or an explicit accepted
exception with a named reason and owner.

Useful commands:

```
bash scripts/preflight.sh                       # every declared dependency, on this host
bash scripts/gates/build-all.sh                 # Debug, Release and sanitizer trees, full suite
bash scripts/analysis/run-static-analysis.sh    # cppcheck and clang-tidy against the baseline
bash scripts/capture-display-capabilities.sh :2 label
```

`./wm2-born-again --help` lists every setting, and every setting it lists is one
the binary will accept — the usage text is generated from the same table the
option parser is handed, so the two cannot drift apart.

### The configuration socket

A running window manager listens on a Unix domain socket, so a configuration
tool can ask it questions and, in a later release, change settings without a
restart.

The path is published on the root window as the property `_WM2_CONFIG_SOCKET`,
a `STRING` holding the socket's filesystem path:

```
xprop -root _WM2_CONFIG_SOCKET
```

Read the property rather than reconstructing the path. The path itself is
`$XDG_RUNTIME_DIR/wm2-born-again/socket<display>`, falling back to
`/tmp/wm2-born-again-<uid>/socket<display>` when `XDG_RUNTIME_DIR` is not set to
an absolute path, with every character of the display name outside
`[A-Za-z0-9._-]` replaced by `_` — so `:1` becomes `socket_1`. Two window
managers on two displays therefore never contend for one socket.

Only the user running the window manager may connect. The socket lives in a
directory created mode 0700 and is itself mode 0600, and every accepted
connection's peer credentials are checked against the window manager's own uid
before a single byte is read. Any other uid, root included, is closed and the
refusal is logged once per uid. A refused connection never reaches the protocol
at all.

Messages are one JSON object per line, terminated by a newline, and a line
longer than 4096 bytes is refused. The first message on a connection must be a
`hello` naming the client program and the protocol version; anything else closes
the connection. This release answers `hello` and `status`. The `status` reply
carries the window manager version, the protocol version, uptime in seconds, the
screen width and height, and counts of managed and hidden windows — and nothing
else. No window title, class or geometry is ever sent over the socket.

If the socket cannot be created the window manager says so on stderr and carries
on managing windows normally; only the configuration connection is lost.
