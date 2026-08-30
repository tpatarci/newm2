# What this transcript establishes, and what it does not

**Read this before citing `x2go-nxagent/` as X2Go validation.**

## What was run

`nxagent 3.5.99.26` — the X server X2Go uses, from `x2goserver-x2goagent
4.1.0.3-5` — started **nested on a local Xvfb** and driven headlessly:

```
Xvfb :91 -screen 0 1280x1024x24 -noreset -nolisten tcp
DISPLAY=:91 nxagent -R -name wm2-x2go-validation -geometry 1024x768 :92
DISPLAY=:92 build/release/wm2-born-again
DISPLAY=:92 xclock -geometry 240x200+150+150 -title wm2-x2go-validation
```

No X2Go client, no SSH session, no human. About two minutes end to end.

## What it establishes

- `nxagent` advertises **SHAPE, RANDR and RENDER** (23 extensions total), and the
  WM's own capability probes agree with `xdpyinfo` — see `wm-banner.txt` beside
  `capabilities.txt`.
- The window manager **starts, frames and manages** on it: the client is
  reparented into a frame at the standard `+24+8` client offset, the tab and
  button children are present, and the window appears in `_NET_CLIENT_LIST`.
- The full `_NET_SUPPORTED` list is published (`root-properties.txt`).

That is enough to retire the **specific** claim in D-8-X2GO that nothing in the
TigerVNC or XRDP results transfers because the extension surface is unknown. It
is known now, and it is the same surface.

## What it does NOT establish

**Behaviour over a real X2Go connection.** X2Go does not merely run `nxagent`; it
starts it through `x2gostartagent` with its own options and puts an **NX
compression proxy** between the agent and the client, over SSH. That proxy is
the layer D-8-X2GO was actually worried about — it sits in front of exactly the
extension surface this phase depends on, and it is where latency, round-trip
batching and any protocol rewriting would show up.

Nothing here exercises it. Specifically untested:

- menus and drag-move under compression and network latency
- the long-press delete timing, which is the one interaction with a real clock in it
- resize behaviour when the session is reconnected at a different geometry
- whatever the proxy does or does not forward for SHAPE

**Nor does it establish anything by eye.** This is a headless capture: no
screenshot, no judgement about whether the sideways tab looks right.

## Where the rest is

The real-client session is part of plan 08.5-02's human checkpoint. Whether
D-8-X2GO is deleted or restated with this narrower reason depends on whether
that session happened — see the deviation's own text for the outcome.

**This file exists because a directory named `x2go-nxagent` containing three
green transcripts reads, at a glance, like "X2Go: tested".** It is a floor, not
a tick. The same discipline as the Phase 8 bundle's note on
`sanitizer-reports/`, and for the same reason: an artifact that will be skimmed
should carry its own limits.
