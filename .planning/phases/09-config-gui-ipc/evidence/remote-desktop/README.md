# Phase 9 — the once-per-release remote-desktop pass

D-20 asks for one measured session per release with **the settings window's
resident memory recorded while it is open**. `09-RESEARCH.md`'s assumption log
(A3) called that figure *"the single most consequential unverified number in the
document"*, estimated 30–60 MB, and instructed that no plan skip the measurement
on the estimate's strength.

It was not skipped. **The estimate was low.**

Snapshot taken at commit `8f4275b1ada85358ef7bf86bf0c106c8e054de22` (`8f4275b`),
2026-09-06. Everything here — the measurement, the screenshots and all four gate
logs — was produced by that tree.

**One thing changed after the capture and before this bundle was committed**, in
the same commit that adds it: the release notes' resource table was corrected
from an earlier run's figures to this run's exact ones (98.5 → 98.7 MB, and the
`Xvnc` row added). Of the four gates only `doc-keys.sh` reads that file; it was
re-run afterwards and is still green, and `gates/doc-keys.log` is that later run.
The other three logs are the `8f4275b` runs untouched.

## READ THIS BEFORE ADDING ANYTHING TO THIS DIRECTORY

This repository is **public**. Three standing rules, inherited from 08.5 and
restated as prohibitions in `09-09-PLAN.md`:

- **No host identifier, no network address.** `capabilities.txt` recorded
  `uname -n` — this machine's name — in every capture the project has ever
  taken, and the scan required by this plan found it in this bundle's own first
  draft. `scripts/capture-display-capabilities.sh` no longer emits it. The
  captures committed under `08-…/evidence/` and `08.5-…/evidence/` still
  contained it when this plan closed; the orchestrator replaced the name with
  `<workstation>` in all sixteen tracked files at commit `823a253`, after this
  plan. The pushed history before that commit still carries it; whether to
  rewrite that history is the operator's decision (ledger entry 28).
- **`~/.xsession-errors` is never read, copied, quoted or committed.** On the
  validation host that file carries the operator's whole environment in
  plaintext. Nothing in this bundle came from it. <!-- planner-discipline-allow: xsession-errors -->
- **Screenshots and window trees carry window titles.** Every title in this
  bundle belongs to a program this capture started: `wm2-born-again settings`,
  `wm2-config`, `wm2-born-again`. There was nothing else on the display.

**The scan that was run over this directory before it was committed**, for this
machine's hostname, `xsession-errors` <!-- planner-discipline-allow: xsession-errors -->, any IPv4-shaped string, any `/home/`
path, and `API_KEY|ACCESS_KEY|SECRET|TOKEN|PRIVATE KEY|Bearer `. Two matches,
both reported rather than quietly dropped:

- the `API_KEY|…` line above, which is this paragraph *naming* the pattern —
  the same match 08.5's bundle carries for the same reason;
- `/home/<user>/…` build-tree paths in `gates/build-all-debug.log` and
  `gates/build-all-nogtk.log`, written by `cmake` and `ctest` naming their own
  build directory. That is the home *directory name*, which the plan's
  prohibition explicitly tolerates ("no user name **beyond the home directory
  name**"), and it is what 08.5's committed build logs already carry. They are
  left verbatim rather than rewritten, because a doctored log is not a log.

No hostname, no address, and nothing from a session error log.

## The session

`Xvnc TigerVNC 1.12.0` (`built 2024-01-23`), started **directly** rather than
through the `vncserver` wrapper so that every process has a PID this capture
created:

```
Xvnc :147 -geometry 1280x1024 -depth 24 -localhost yes \
     -rfbauth ~/.vnc/passwd -SecurityTypes VncAuth

DISPLAY=:147 XDG_CONFIG_HOME=<throwaway> PATH=build/release:$PATH \
    build/release/wm2-born-again --new-window-command=xclock

DISPLAY=:147 HOME=<throwaway> build/release/wm2-config
```

`-localhost yes` binds the RFB port to loopback. **No network-exposing flag and
no X access-control flag were used anywhere** (threats T-8-AC, T-9-56). No
viewer was attached; the pointer input below is XTEST.

The window manager's own `PATH` carries `build/release`, so D-11's startup probe
finds the settings window. `wm-stderr.txt` records the result in the banner:

```
  Settings window on PATH: yes, Configure is on the root menu.
```

**Every process this capture started was stopped by the PID this capture
created** — the two `wm2-config` instances, the window manager and `Xvnc`, in
that order, named individually in `session-transcript.txt`. Nothing was
selected, signalled or stopped by a name pattern or a process-table scan.

## The measurement

Resident set size, read from `/proc/<pid>/statm` field 2 — the same field and
the same reader the automated budget cases use, so these figures and the
suite's are comparable rather than merely adjacent.

| Process | Resident | Of a 512 MB machine |
|---|---|---|
| `wm2-config`, the first opened in this session, all three pages visited | **101036 kB — 98.7 MB** | 19% |
| `wm2-config`, a second instance on the same session | 50580 kB — 49.4 MB | 10% |
| `wm2-born-again` | 12128 kB — 11.8 MB | 2% |
| `Xvnc` itself | 75936 kB — 74.2 MB | 14% |

**The desktop as a whole**, on the 512 MB target this project constrains itself
to: about **86 MB (17%)** for the window manager and the VNC server, rising to
about **189 MB (37%)** for as long as the settings window is open.

The window manager's figure agrees with the 11.8 MB the release notes already
quote from `[wm_resource_budget]`, which is worth noting because that figure was
measured on Xvfb and this one on a real VNC server: the window manager costs the
same on both.

### The split nobody explained

The **first** `wm2-config` opened on a freshly started X server holds about twice
what every later one on the same server holds. This is reproducible: three runs
on Xvfb and three on TigerVNC, always ~101 MB for the first and ~50 MB for the
rest.

Two candidate explanations were tested and **both are ruled out**:

- **Not the per-user fontconfig cache.** A fresh `HOME` and a fresh
  `XDG_CACHE_HOME` on *every* run still shows ~101 MB for the first launch and
  ~50 MB from the second onward.
- **Not the cost of being the first client on the server.** An `xclock`
  connected before `wm2-config` does not change the figure.

What it actually is was **not determined**. It is written down as an open
question, here and in `COMPILED_CODE_BEHAVIOR_CHECKLIST.md`, rather than guessed
at — and it is the largest single unexplained term in the number, so it is where
anyone with a reason to reduce the footprint should start.

The higher figure is the one quoted in the release notes and the one the
automated 160 MB budget is set against, because a real session opens the settings
window once.

## What was exercised

Each of the three pages was reached with **real pointer input through XTEST**, on
the notebook tab at the coordinates this session actually puts it, and
screenshotted afterwards — which is what says the page was *reached* rather than
aimed at:

| File | Shows |
|---|---|
| `page-appearance.png` | the Appearance page: nine colour rows, two font choosers, frame thickness |
| `page-behaviour.png` | the Behaviour page: four focus checkboxes, three delays, the new-window command and its shell flag |
| `page-menu.png` | the Menu page: the manual-entry list with Add / Edit / Remove |
| `page-appearance-again.png` | back to Appearance, so the notebook is shown surviving a round trip |

All four show the settings window **framed by this window manager**, with the
sideways tab down its left edge reading `wm2-born-again settings`, and the
banner *"Connected to the running wm2-born-again; changes apply as you make
them"* — so the socket connection over the VNC session is visible in the
capture and not merely asserted.

**Synthetic key events were tried first and did not work.** `xdotool key
--window <id> ctrl+Next` produced no page change at all; the screenshot after
three of them still showed the Appearance page. That is why this capture clicks,
and why the per-page screenshots are kept.

## The four gates, at this commit

Under `gates/`, all four green at `8f4275b`:

| File | Result |
|---|---|
| `build-all-debug.log` | **OK** — 539 tests registered, 100% passed, 0 failed, 0 compiler warning lines |
| `build-all-nogtk.log` | **OK** — 539 registered in both trees, 539 attempted, 534 passed, 5 skipped, `539 − 534 = 5 = 5`, every skip printing its reason |
| `install-components.log` | **OK** — `wm` and `config-gui` staged separately, each manifest exact, no GTK/GDK/GLib/GObject in the `wm` component's `ldd` |
| `doc-keys.log` | **OK** — 36 accepted keys, every one documented, no documented key the binary refuses |

The five skips in the GUI-disabled tree are the five cases that need a built
`wm2-config`, each naming that as its reason. Two of them are new in this plan.

`doc-keys.sh` is also kept here **failing**, because a gate that has never failed
has not been shown to work:

| File | Shows |
|---|---|
| `doc-keys-first-run.txt` | its first run against the tree as it stood: **ten** accepted keys the release notes never presented, including four of the nine colours and the whole `menu-entry-*` family |
| `doc-keys-shown-to-fail-undocumented-key.txt` | a throwaway key added to the option table: named, exit 1, reverted, green again |
| `doc-keys-shown-to-fail-both-directions.txt` | one rule key renamed in the notes: **both** directions redden at once — and the same key moved into an HTML comment stays red, because a reader cannot see a comment |

## What this bundle does NOT settle

- **Nothing was judged by eye over a live connection.** No viewer was attached.
  Whether the settings window *feels* usable over a real VNC link — whether it
  is laggy enough that a user would give up — is unanswered here, and is the
  operator's question in `09-09-SUMMARY.md`.
- **No latency was introduced or measured.** This is loopback. A droplet reached
  across a network is a different experience and this capture does not stand in
  for one.
- **`tree-state: DIRTY` in `tigervnc/capabilities.txt` is expected.** The tree
  carries untracked planning files (`.planning/state.json`,
  `.planning/milestone.lock`, this evidence directory itself) at capture time.
  No tracked file differed from `8f4275b`.
- **One server, one version, one host, one day.** That is what a manual
  compatibility measurement is. The server, its version and the commit are
  recorded above so a later surprise can be attributed rather than argued about.

## Files

| File | What it is |
|---|---|
| `session-transcript.txt` | the whole run, including every PID started and every PID stopped |
| `tigervnc/capabilities.txt` | extension matrix, geometry, RANDR state, fontconfig resolutions, provenance |
| `tigervnc/root-properties.txt` | `xprop -root` — the EWMH properties, with the settings window in `_NET_CLIENT_LIST` |
| `tigervnc/window-tree.txt` | `xwininfo -root -tree` — the settings window reparented into its wm2 frame |
| `page-*.png` | the three pages, reached by click, plus the round trip |
| `wm-stderr.txt` | the window manager's startup banner and its whole session output |
| `gui-stderr.txt`, `gui2-stderr.txt` | the two settings-window instances' output (both empty) |
| `gates/` | the four gates at this commit, plus `doc-keys.sh` shown failing three ways |
