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

## Fourth re-review (Codex), 2026-09-05

One P2, confirmed by reading: the 256-atom cap was compared against
`count + 8` (the retry slack), so a property holding 249..256 atoms, inside the
documented cap, was left untouched and an explicit `rule-skip-taskbar=false`
did nothing. The cap now applies to the count the property holds; the slack
only widens the re-read length.

Boundary case at exactly 256 atoms (skip-taskbar first, skip-pager last, 254
unrelated atoms between, survivors checked for count and order):
`red-256-atom-cap.log` (fails on the first `REQUIRE_FALSE` before the fix),
`green-256-atom-cap.log` (11 assertions after). The original Off case still
passes.

## Fifth re-review (Codex), 2026-09-05

One P2, confirmed by reading: the widened second read asks for `present + 8`
atoms, so a client appending between the two reads could return 257..264
atoms with `bytesAfter == 0`, and the property would be processed past the
stated cap. The count that actually arrived is now checked against the cap
once more before filtering. The race is not deterministically testable; the
256-atom case and the original Off case still pass. Re-review series closed
here: five passes, 4 -> 3 -> 1 -> 1 -> 1 findings, each narrower than the
last and the final two confined to the exactness of one bound.

## Sixth re-review (Codex), 2026-09-05 — clean

No findings: "The added guard correctly enforces the 256-atom processing limit
after the widened re-read and frees the Xlib allocation before returning. The
project builds successfully, and no regression was identified." The series
ends on a clean pass rather than on a stopping rule.

## CodeRabbit CLI pre-flight over the whole branch (`cr review --agent -t committed --base main`), 2026-09-05

Eight findings, read as data and each verified against the tree before acting.

| # | Severity | Finding | Verified? | Disposition |
|---|---|---|---|---|
| 1 | minor | `src/Border.cpp` tab-button loop: on `ModalWait::Interrupted` the loop breaks with `action` still 1 (hide) or 2 (kill), and the code after the loop acts on it | **Yes — a real defect from the ledger-8 rewrite.** A signal during a tab-button press would hide, or after a long in-bounds hold close, the client on the way out | Fixed: `action = 0` before the break. By reading; the observable (a hide or a WM_DELETE_WINDOW during the manager's own shutdown) has no clean assertion point from a client, so no case was added. The two `[wm_button]` cases still pass |
| 2 | minor | `tests/test_wm_rules.cpp` `wmState()` read only the first 64 atoms, so a `REQUIRE_FALSE(hasState(...))` for an atom past the prefix would pass vacuously | Yes — the 256-atom cap case puts `SKIP_PAGER` last, where a 64-atom read cannot see it (the case's count-and-order check would still have caught a survivor, but the helper was misleading) | Fixed: the helper reads the whole property in 256-atom chunks until `bytesAfter` is zero. Both skip cases pass |
| 3 | minor | `tests/test_eventloop.cpp` child wait: on a `waitpid` error other than `EINTR` the loop set `killed` and broke without signalling the child | Yes — a child holding its X connection open could outlive the case | Fixed: `kill(pid, SIGKILL)` on that branch, the explicit pid `fork()` returned, never a scan |
| 4 | major | `DOC-GUARDS.txt` reports set sizes and the symmetric difference but does not assert exact membership | Yes as a strengthening; the plan's design was sizes plus difference with the expectation stated | Appended a verdict section: exact shipped-set check PASS, exact difference check PASS, same commands re-run at HEAD with zero source or release-notes drift from the capture commit |
| 5 | major | `fix-measurement/README.md` opening presented the `70fbd8f` series as the final tree | Yes — stale after the capture's two source fixes | Opening rewritten: the first two series are the pre-capture precondition; the final-tree series is a third section added after it runs |
| 6 | major | `STATE.md` PR #5 entries still say "under remote review" | Yes | Fixed in the closeout STATE update (PR #5 merged 11:59Z; PR #6 recorded separately) |
| 7 | major | `STATE.md` cursor fields still show 08.5-08 pending | Yes | Fixed in the closeout STATE update |
| 8 | major | `ROADMAP.md` line 298 status paragraph carries pre-ruling and held-chain statements and stale counts | Yes | Rewritten with the current statuses and recomputed counts |

Finding 1 changes `src/`, findings 2 and 3 change `tests/`, so the bundle at
`d08b6e5` no longer describes the shipping tree; the capture is re-run at the
commit that carries these fixes and the Codex pass over the whole branch diff,
and the ten-run series at that final commit follows it.

## Codex review of the whole branch diff (`codex review --base origin/main`), 2026-09-05

Two P2 findings, both confirmed by reading, both fixed.

| Finding | Verified | Fix | Evidence |
|---|---|---|---|
| The interruptible gesture loop (ledger 8) breaks on `Interrupted` with `event` never written, then reads `event.xbutton.time` and `event.type` — undefined behaviour on the shutdown path | Yes: `XEvent event;` was uninitialised and only `modalWait` returning `Event` ever wrote it | `event` is value-initialised, an `interrupted` flag is set on the non-Event return, and that path ungrabs with the press's own `e->time` (the one defined timestamp it has) and evaluates no gesture | By reading; a signal mid-gesture has no deterministic assertion point from a client |
| Frameless maximize kept the decorated frame's one-pixel outer-border allowance (`ww - xi - 1`), so a no-decorate window maximized one pixel short of the workarea on each axis | Yes | `edge = m_frameless ? 0 : 1` | New `[wm_rules]` case "A no-decorate window maximizes to the workarea exactly": `red-frameless-maximize-one-pixel-short.log` fails `1023 == 1024`; `green-frameless-maximize-one-pixel-short.log` passes 12 assertions |

The source changed again after the gate bundle at `d08b6e5`, so the bundle is re-captured at the commit carrying these fixes and the CodeRabbit ones above; the test surface moves to 334 source cases / 336 registered and the checklist's test-surface row is recomputed at that commit.

## CodeRabbit CLI over the delta since the first pass (`--base-commit 7f67bc6`), 2026-09-05

Six findings, all documentation. Five fixed by wording; one declined with the computation.

| Finding | Disposition |
|---|---|
| `evidence/README.md` called nine declined rows "gestures" | Reworded: the nine rows with no automated coverage are named by number (#4, #5, #7–#10, #20–#22); the two with partial coverage (#6, #19) named too |
| `evidence/README.md` fix-measurement entry names two series | Updated with the third series once its ledger closed (see the fix-measurement commit) |
| `capture-attempts/README.md` counted two attempts in its opening and closing | Now three, `d08b6e5` named in both |
| `08.5-05-SUMMARY.md` "336 registered" | Reworded: 336 ctest tests = 333 registered Catch2 cases + 3 fixture tests |
| `INTERACTION-CHECKLIST.md` preamble said no human had ever exercised the nine rows; row #8 was exercised in Phase 8's manual passes | Reworded to this session, with #8's Phase 8 exercise named |
| **major** `COMPILED_CODE_BEHAVIOR_CHECKLIST.md` per-file table "missing 16 cases and one file" against its totals | **Declined.** Recomputed from the table text: 23 `test_*.cpp` rows summing to 334, equal to the stated 334 cases / 23 files (`grep -oE '\`test_[a-z_]+\.cpp\` \| [0-9]+'` over the table, summed). The plan's own per-file staleness loop also prints nothing. The table is a two-column layout, which is the likely misreading |
