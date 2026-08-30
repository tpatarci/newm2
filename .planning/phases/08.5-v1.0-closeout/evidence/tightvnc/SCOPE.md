# TightVNC — the degraded-server result

**This transcript is the most useful one in the bundle, and not because it is
green.** It is the only capture taken against a server that genuinely lacks the
extensions this project spent Phase 8 building fallbacks for.

## What was run

`Xvnc TightVNC-1.3.10` (`tightvncserver 1:1.3.10-5`), started directly rather
than through the `vncserver` wrapper, loopback-only, no viewer attached:

```
Xtightvnc :93 -geometry 1280x1024 -depth 24 -rfbauth ~/.vnc/passwd -localhost
DISPLAY=:93 build/release/wm2-born-again
DISPLAY=:93 xclock -geometry 240x200+150+150 -title wm2-tightvnc-validation
```

`-localhost` binds to loopback. No network-exposing flag was used anywhere
(threat T-8-AC).

## The measurement that matters

| Server | Extensions | SHAPE | RANDR | RENDER |
|---|---|---|---|---|
| TigerVNC 1.12.0 | many | yes | yes | yes |
| nxagent 3.5.99.26 | 23 | yes | yes | yes |
| **TightVNC 1.3.10** | **7** | **yes** | **no** | **no** |

TightVNC advertises seven extensions in total: BIG-REQUESTS, MIT-SHM,
MIT-SUNDRY-NONSTANDARD, SHAPE, SYNC, XC-MISC, XTEST.

**D-8-TIGHTVNC's rationale was wrong.** It argued that a TigerVNC result was
genuine evidence about TightVNC because the two descend from the same `Xvnc`
codebase and their "X server behaviour — the extension set advertised, Shape
handling, resize handling, the fontconfig picture — substantially overlaps".
Shared ancestry turns out not to survive a decade of divergence: TigerVNC 1.12
is a modern server, TightVNC's Unix side is still 1.3.10 from 2009, and on the
two extensions the argument specifically named they do not overlap at all.

The deviation was not merely unproven. It was **reasoning from ancestry to
behaviour**, and the behaviour disagreed.

## What the window manager did about it

It ran, and it said so:

```
  Shape extension available.
wm2: warning: no xrandr extension, screen geometry will track resolution changes
     via the root window only
wm2: warning: no xrender extension, tab labels will be drawn through the core
     X11 glyph path
```

Both fallback ladders announced themselves — which is what they were built to do
(XDIS-02, XDIS-04). Rung 1 of the tab-font ladder is the silent one; every other
rung prints, so a transcript records the degradation instead of leaving it to be
inferred from a screenshot.

The client was framed normally: frame 262x209, client reparented inside it, tab
and button present, resize handle present, and **no X protocol errors**.

**One observable difference from the RENDER-capable targets, and it is expected:**
the client sits at `+21+8` inside its frame here, against `+24+8` on nxagent and
TigerVNC. `xIndent()` is `m_tabWidth + FRAME_WIDTH + 1`, and the tab width is
measured from whatever font fontconfig resolves — so a 3 px narrower tab is the
visible consequence of taking a different rung of the font ladder. The frame is
narrower to match. Nothing is broken; the geometry simply follows the font, as it
is designed to.

## What this does NOT establish

- **Nothing was judged by eye.** No viewer was attached and no screenshot taken,
  so whether the core-X11 glyph path *looks* acceptable is unanswered. That is
  the one thing about the RENDER-less rung that a transcript cannot settle, and
  it is precisely where a fallback is most likely to be ugly rather than broken.
- **No interaction was exercised.** Menus, drag-move, resize and the long-press
  delete were not run over a real TightVNC connection.
- **Resolution changes were not tested**, which on a RANDR-less server is the
  interesting case — the fallback watches the root window instead, and this
  transcript does not exercise it.

Those belong to the human sitting in `INTERACTION-CHECKLIST.md`.
