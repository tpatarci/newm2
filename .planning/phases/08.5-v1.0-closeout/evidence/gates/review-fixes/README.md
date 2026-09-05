# Review fixes — Codex review of PR #5, 2026-09-05

Codex (`codex review --base origin/main`, gpt-5.6-sol) returned four findings.
Each was read against the source, reproduced by a deterministic case that
FAILED against the pre-fix window manager, and then fixed; the same case
PASSES after. Logs in this directory: `red-<case>.log` before, `green-<case>.log`
after. `green-SIGTERM_while_the_root_menu.log` is the ledger-8 case re-run at the
fixed tree as a regression check.

| Finding | Case | Before | After |
| --- | --- | --- | --- |
| P1 rule-no-decorate windows unmanaged: `activate()` refused them ("bad parent") | `A no-decorate window can take focus and is published as the active window` (`[wm_rules]`) | `REQUIRE( active )` false | passed |
| P1 same, click path: no grab, no focus, no replay | `A click on an unfocused no-decorate window focuses it, reaches it, and leaves the root menu working` (`[wm_process]`) | `REQUIRE( focused )` false | passed |
| P1 submenu taller than the screen, rows unreachable | `A category submenu with more entries than fit stays inside the screen` (`[wm_menulabel]`) | `1453 <= 768` false | passed |
| P2 explicit `rule-skip-taskbar=false` not published | `An explicit skip-taskbar=false rule removes a client's own skip states` (`[wm_rules]`) | `SKIP_TASKBAR` still set | passed |
| P1 reflow of frameless clients configured the root window | covered by reading: `Client::ensureVisible()` now moves the client window itself when frameless | — | — |

## The frameless managed path (issue #3)

`Client::m_frameless` is set on the dock / notification / rule-no-decorate
path in `manage()`, together with `m_managed = true` and the save-set. Every
frame operation checks it: `activate()`/`deactivate()` grab and ungrab on the
CLIENT window (pointer-synchronous, so `eventButton()` can activate and then
`XAllowEvents(ReplayPointer)` so the click also lands); `decorate()`,
`rename()`, `stripForFullscreen()`/`restoreFromFullscreen()` and the maximize
`configure()` calls are skipped or redirected at the client window;
`mapRaised()`, `lower()`, `hide()`, `withdraw()` and `ensureVisible()` act on
the client window; the `ConfigureRequest` branch keys off `m_managed &&
!m_frameless`. Product decision recorded here: DOCK and NOTIFICATION windows
never take focus (`isFocusableFrameless()` is false for them); a
`rule-no-decorate` normal window does.

## Submenu overflow

`openSubmenu()` sizes the popup to the rows that fit (`subRows`); `subFirst`
is the scroll offset. Hovering the popup's last (or first) row scrolls one
entry per motion event. Drawing, highlighting and hit-testing all go through
the visible-row mapping.

## What this does not establish

Full-suite results at this tree are in the gate capture, not here. One run of
the full debug suite at `-j2`, concurrent with a ten-run measurement in a
sibling worktree, failed two cases (`readable-is-not-deliverable`,
`Destroying a mapped focused client ...`) that then passed 2/2 each alone; the
gate runs serially and its serial result is what counts.

## Re-review of the fix commit (Codex, `--commit`), 2026-09-05

Three findings, all confirmed by reading and fixed in the follow-up commit:

| Finding | Fix | Evidence |
| --- | --- | --- |
| P1 the `ButtonRelease` re-ran `pointerAt()` and scrolled once more, so an edge-row release launched the entry BELOW the one shown | `pointerAt(rx, ry, allowScroll)`: only `MotionNotify` may scroll; the release commits the displayed entry | by reading (the release path is one call) |
| P2 `m_frameless` survived a withdraw/re-manage whose rule outcome changed | reset to `false` at the top of `manage()`, set on the frameless path only | by reading |
| P2 the Off rule called `updateNetWmState()`, which rebuilds the whole property and erased client-set states the WM never imported | `stripNetWmStates(SKIP_TASKBAR, SKIP_PAGER)` removes exactly the two the rule overrides | the Off case now also sets `_NET_WM_STATE_DEMANDS_ATTENTION` before mapping and requires it to survive: `green2-skip-taskbar_off_keeps_unrelated_state.log` |

## Second re-review (Codex, `--commit` on the re-review fix), 2026-09-05

| Finding | Fix |
| --- | --- |
| P1 a focusable frameless client that withdrew kept deactivate()'s passive grab on its window; re-managed framed, nothing ever ungrabbed it and its clicks were swallowed | `withdraw()` ungrabs the client window when frameless; `manage()` ungrabs it again before clearing the flag |
| P2 `stripNetWmStates()` read only the first 64 atoms | reads the whole property, re-fetching with a covering length while `bytesAfter` is non-zero |

Both by reading; the four review cases and the no-decorate geometry case pass after.

## Third re-review (Codex), 2026-09-05

One finding, confirmed: the whole-property read had no size or retry bound on a
client-controlled property. Now one retry and a 256-atom cap; past either the
property is left unchanged. The passive-grab cleanup was judged sound.
