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
| **X2Go** | **Not tested. An accepted deviation with a recorded reason** — and a weaker one than TightVNC's. See below. |
| **TightVNC** | **Not tested. Deliberately, with a recorded reason.** See below. |

Two of the four stated targets are exercised. That is the honest count, and the
project requirement covering "compatible with all four out of the box" is held
**unmet** rather than ticked on the strength of the two that were done.

### X2Go: an accepted deviation, and a weaker one than TightVNC's

X2Go is **not** validated for this release. `x2goserver` was never installed on
the validation host, so there is no session, no transcript and no interaction
record.

This should not be read as the same kind of gap as TightVNC below. TightVNC has
a close relative under test — TigerVNC shares its `Xvnc` ancestry, so a TigerVNC
result is genuine evidence about it. X2Go has no such proxy here. Its X server is
`nxagent`, a different codebase from both `Xvnc` and XRDP's backends, with a
compression proxy in front of it — precisely the layer most likely to differ on
the things this release depends on: the Shape and RANDR extension surface, and
the RENDER path the sideways tab font uses. **Nothing in the TigerVNC or XRDP
results transfers to it.**

What to expect meanwhile: X2Go is unexercised, not unsupported. If the sideways
tab renders wrongly or frames come out unshaped under `nxagent`, that is a real
bug worth reporting — and given the RENDER dependency it is the likeliest place
for one to be hiding.

### TightVNC: an accepted deviation, not an omission

TightVNC is **not** validated for this release. The reason, so it can be argued
with rather than guessed at:

- TigerVNC is TightVNC's maintained successor for the Unix server side, and both
  descend from the same `Xvnc` lineage. Their X server behaviour — the extension
  set they advertise, how they handle Shape, how they resize — substantially
  overlaps.
- **Closest tested proxy: TigerVNC.** A TigerVNC transcript is the best available
  evidence for how TightVNC will behave, and it is committed in the evidence
  bundle.
- This is recorded as an accepted deviation with a named owner and a follow-up in
  `COMPILED_CODE_BEHAVIOR_CHECKLIST.md`, which is where this project keeps its
  signoff record. A deviation without an owner and a follow-up is not an accepted
  deviation, it is an omission.

**What that means for you:** if you run TightVNC and something is wrong, it is a
real bug worth reporting, and nobody will tell you the configuration is
unsupported. It simply has not been exercised.

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
