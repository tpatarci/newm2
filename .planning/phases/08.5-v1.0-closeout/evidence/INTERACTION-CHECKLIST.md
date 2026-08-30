# User Interaction Checklist — per-item results

**Tester:** _(your name)_
**Date:** _(YYYY-MM-DD)_
**Target:** _(TigerVNC / XRDP / TightVNC / X2Go — whichever session you ran)_
**Commit:** _(output of `git rev-parse --short HEAD`)_
**Binary:** `build/release/wm2-born-again`

This closes the last open component of **TEST-08**. Phase 8 ran two manual
passes; the second produced a general verdict ("all works like a charm"), which
is not the same as a result for each row. This file is the per-item form.

**How to fill it in:** put `PASS`, `FAIL` or `n/a` in the Result column, and use
Notes for anything surprising. A row you did not get to is `n/a` with the reason
— leaving it blank or guessing is worse than an honest gap, and the unticked
rows in `COMPILED_CODE_BEHAVIOR_CHECKLIST.md` exist precisely because that
discipline was kept last time.

Launch the WM in the session with something you can open windows with:

```
DISPLAY=<your display> build/release/wm2-born-again --new-window-command=xclock
```

The root menu is **Button 1** (left). Button 3 (right) is circulate.

---

## The rows

| # | What to check | Result | Notes |
|---|---|---|---|
| 1 | Root left-click menu appears, fits at the screen edges, highlights the row under the pointer, and unmaps after a selection | | |
| 2 | Root menu `New` launches the configured command (plain exec) | | |
| 3 | Root menu `New` in shell mode — start with `--exec-using-shell --new-window-command="xmessage shell-ok"` | | |
| 4 | Root menu hidden-client entries **restore** a hidden client (hide one with a tab-button click first) | | |
| 5 | Root menu exit item appears only at the lower-right screen edge, and exits the WM | | |
| 6 | Right-click circulation — zero clients | | |
| 7 | Right-click circulation — one client | | |
| 8 | Right-click circulation — several normal clients | | |
| 9 | Right-click circulation — with a hidden client present | | |
| 10 | Right-click circulation — with a transient/dialog present | | |
| 11 | Clicking a tab or frame raises and focuses per the configured policy | | |
| 12 | Dragging a tab moves the window, and the window keeps up with the pointer | | |
| 13 | Dragging a window hard toward the top-left leaves its tab reachable (the `a5a105c` clamp) | | |
| 14 | Dragging the resize handle resizes normally | | |
| 15 | Constrained resize — horizontal-only and vertical-only paths | | |
| 16 | Tab button **short** press hides the window | | |
| 17 | Tab button **long** press (past `destroy-window-delay`) deletes it, and the cursor is restored | | |
| 18 | The close button is comfortable to hit — the `43fb24b` widening (this is the one you reported) | | |
| 19 | Middle-click on a tab toggles maximize | | |
| 20 | Right-button circular gesture toggles fullscreen | | |
| 21 | ...and a short or noisy gesture is ignored rather than firing | | |
| 22 | Pointer grabs are released after every cancel path — press another button mid-drag, destroy a window mid-interaction | | |

## Rendering and feel — by eye

| # | What to check | Result | Notes |
|---|---|---|---|
| 23 | The sideways tab looks right — this is the non-negotiable visual identity | | |
| 24 | The tab label tracks the window title and is legible | | |
| 25 | A long title on a short window truncates without artifacts | | |
| 26 | Configured colours apply (`--tab-background`, `--tab-foreground`) | | |
| 27 | Frame thickness works at its minimum, default and maximum | | |
| 28 | Latency is usable over this connection — menus, move, resize, long-press delete | | |

## New in Phase 8.5 — window rules

Write these to `~/.config/wm2-born-again/config` and restart the WM:

```
rule-match-title = clock
rule-position    = 300,200
```

| # | What to check | Result | Notes |
|---|---|---|---|
| 29 | A window whose **title** matches opens at the rule's position | | |
| 30 | Renaming that window afterwards does **not** move it (this is deliberate — D-8.5-03) | | |
| 31 | `rule-match-instance = xclock` also works, and `rule-match-name` now warns as an unknown key | | |

---

## Anything that failed

_(One line each: what you did, what happened, what you expected. A failure here
is either fixed or recorded as an accepted deviation with an owner and a
follow-up — the two requirement amendments in Phase 8 are the precedent for how
to write one honestly.)_

## Anything that surprised you but was not a failure

_(Worth capturing. The malformed-window-dressing report and the close-button
complaint both started here, and one of them turned out to be a real defect that
292 automated tests had missed.)_

---

**Note on `~/.xsession-errors`:** if a session misbehaves and you want help
diagnosing it, paste the **window manager's own stderr**, not that file. It holds
your environment in plaintext, including four live API keys you have decided not
to rotate. That decision is recorded; the handling rule is what is left, and it
is the reason this warning sits at the bottom of the file you will have open when
something goes wrong.
