# 08-14 — Release evidence, resource budget, and the manual validation pass

**Status:** complete, with openly-reported gaps. Verified by `gsd-verifier`,
which found four defects in the shipped release notes that this plan had missed;
all four are fixed and described under "What verification caught" below.
**Final commit for this plan:** `c61cb4b`. Gate snapshot taken there.

This was the checkpoint plan: the one that stops being about code and starts
being about whether the code's claims survive contact with a real display and a
real person. It did, mostly. What it also did — and this is the part worth
carrying forward — is demonstrate that the automated suite, at 292 passing cases,
had not found three defects that a user found within minutes.

## What was delivered

| Task | Outcome |
|---|---|
| 1. Capability capture script + resource budget | Done (`2122010`). Budget is enforced by a test, not reported in prose. |
| 2. Release notes, checklist baseline | Done (`d478e83`). |
| 3. Manual remote-desktop pass | Done for XRDP and TigerVNC. **X2Go not validated** — see D-8-X2GO. |
| 4. Evidence bundle, index, checklist walk | Done (this commit). |

Plus seven authorised fixes that came out of Task 3, listed below.

## The four hard blockers

All four run together at `c61cb4b`, all green, all captured under
`evidence/gates/`:

| Blocker | Result |
|---|---|
| Builds and tests, three trees | debug **295/295**, release **295/295**, asan **295/295** |
| Sanitizer | exit 0, `no sanitizer findings` |
| Static analysis | exit 0; cppcheck 15 findings all in baseline, clang-tidy no enforced check fired |
| Runtime smoke transcript | `evidence/local-xephyr/` |

`ldd build/release/wm2-born-again` names no test-only library: XTEST is linked
into test targets only, which is what keeps the shipped binary inside the 512 MB
VPS constraint.

**One acceptance criterion is not met and cannot be met honestly:** the plan
requires evidence subdirectories for `tigervnc`, `xrdp`, `x2go` and
`local-xephyr`. There are three. `x2goserver` was never installed. Fabricating
the fourth was the one thing this task must not do.

## What the manual pass found that 292 automated tests did not

Three defects, all reported by the operator, all reproduced from a clean start
before being fixed:

**1. A window dragged off the top-left could not be recovered** (`a5a105c`).
Found live at frame `(-699,-618)`. wm2's tab is the only pointer handle and it
sits at the frame's left edge, so a frame pushed past `x=0` takes its own handle
with it. No bug was needed to get there — grabbing a large window's top strip
near its right end makes the origin trail the pointer by most of the window's
width. Reproduced from clean at `x = -740`. `Client::move()` had no clamp of any
kind; the correct primitive (`ensureVisible()`) existed but was wired only to the
RANDR path. Fixed with a clamp that bounds the **frame origin**, not the window,
so the body may still hang off any edge.

**2. A ConfigureRequest naming the frame teleported the window** (`a5a105c`).
Found by the orchestrator while recovering defect 1. Two requests, for `(60,60)`
and `(950,120)`, both put the frame at exactly `(24,8)` — the client's
inside-the-frame indents applied to the frame itself — while `m_x`/`m_y` kept the
requested values, desyncing model from screen permanently. Now declined:
honouring it would hand every X client on the display a window-teleport
primitive.

**3. The close/hide button's target was 8×8** (`43fb24b`). 64 square pixels, and
a near-miss did something *worse than nothing*: four pixels to any side hit the
tab and started a drag; one or two right or below fell through the frame's shaped
notch to the root window and opened the menu. Now the tab's whole top square,
about 3.6× the area, with the painted square unchanged.

Two earlier fixes in the same pass: the menu highlight erasing its own row label
(`e5a8a6e`), and the menu going dead after a submenu episode (`12f6de8`,
`fa33f2e` — a rework to one grab and one loop, deleting the nested second grab
that owned every pointer event).

### The methodological point

Every one of these was in a code path the suite already exercised. The suite
asserted the *state* those paths produce and never the *reachability* of the
controls that drive them. A test can confirm that hiding a window works while
saying nothing about whether a human can hit the thing that hides it.

Two of the three fixes also needed harness defects fixed before their tests were
worth anything. The drag case was a silent no-op at first — `pumpWm()` cannot
settle a drag, because `Client::move()`'s loop drains every queued event in one
sweep and dispatches only the last, so an unspaced ButtonRelease is drained
alongside the motions. After fixing that, 1 run in 4 still passed *vacuously*
because the frame press is dropped unless the client is already active. Both are
now guarded, including a vacuity check that turns a no-op into a failure. This is
the eighth consecutive plan from 08-07 onward to find a test that did not test
its own name.

## Checklist walk

**89 of 103 boxes ticked, each with a citation.** The 14 that are not ticked are
listed below rather than quietly ticked, and each carries its reason inline in
the checklist itself.

*(Corrected after verification: the first cut of this walk ticked the circulation
row while its own annotation said PARTIAL, and the summary table then inherited
that error and also omitted the Catch2 row. Both are fixed here. The
inconsistency was found by the phase verifier, not by me.)*

| Gap | Why it is open |
|---|---|
| Synthetic `ConfigureNotify` on tab drag | the move is now covered; the ConfigureNotify is not asserted |
| Menu hidden-client restore | `hide()`/`unhide()` are tested directly, but not through the menu row a user actually clicks |
| Menu exit item | no automated case; no per-item manual result |
| Middle-click tab maximize | the maximize *state* is covered thoroughly, but only via EWMH messages — no case middle-clicks a tab |
| Right-button circular fullscreen gesture | `detectFullscreenGesture()` has no coverage at all |
| Pointer grabs always released | no case asserts the property across cancel and destroy paths |
| `WM_TRANSIENT_FOR` raise ordering | self-referencing transients are guarded; ordering is not tested |
| `WM_COLORMAP_WINDOWS` install order | the property read is tested; the order is not |
| `ignoreBadWindowErrors` scope | the flag's scope is not asserted |
| Circulation with hidden/transient clients | zero-client and multi-client covered; those two permutations are not |
| Catch2 fetched from GitHub at configure time | no CI here by policy, so it is a developer-onboarding dependency rather than a CI one — smaller than the row assumes, not absent |
| X2Go / four-target verification | D-8-X2GO |
| Behaviour over measured latency | exercised subjectively, never measured |
| Per-item manual results | two passes run, but the second gave a general verdict |

Several of these cluster: **gesture-level input has thin coverage**. The states
reached by middle-click, the circular gesture, and the menu's exit and
hidden-client rows are all well tested; the gestures that reach them are not.
That is the same blind spot that let all three of this pass's defects through,
and it is the most useful thing this plan learned about where to aim next.

## Deviations recorded

- **D-8-TIGHTVNC** — untested; TigerVNC is a genuine proxy (shared `Xvnc`
  ancestry).
- **D-8-X2GO** — untested, and **weaker than D-8-TIGHTVNC**: `nxagent` shares no
  ancestry with either tested server and puts a compression proxy in front of
  exactly the extension surface this phase depends on. Nothing transfers.

Both carry reason, owner and follow-up.

## Security

`~/.xsession-errors` on the validation host contains the operator's full
environment in plaintext — live `OPENAI_API_KEY`, `HF_TOKEN`,
`LARA_ACCESS_KEY_SECRET`, `LARA_ACCESS_KEY_ID` — rewritten on every xrdp login.
It is excluded from the bundle and the exclusion is stated at the top of
`evidence/README.md`, because it is the first file anyone debugging an X session
reaches for. **Key rotation was advised and has not been confirmed.**

The committed XRDP transcript was screened before commit: zero matches for
key/secret/token patterns.

`-localhost no` on a TigerVNC command line exposes the port to the network. It
was used on this local box and must not be carried into a droplet recipe; the
capture script deliberately suggests no server flags for that reason.

## Deferred items after this plan

Resolved during Phase 8: **6, 7, 8, 11, 14**.

Still open: **5** (bare `ctest --test-dir build/asan` is red — LSan suppressions
are not wired into the Catch2 binaries; use `scripts/gates/build-all.sh asan`),
**9**, **10**, **12**, **13**, **17**.

**Item 17** deserves a note for whoever meets it: `[wm_menureopen]` fails
intermittently with the menu simply never appearing. Measured 12 isolated runs
each at 11/1 with the menu rewrite, 11/1 with an added settle, and **11/1 against
the pre-existing two-loop `menu()`** — equal rate on the old code, so the rework
did not cause it. It surfaces in a different tree on almost every full run.
Treat a single such failure as this known flake only after reproducing it in
isolation, never by assumption. The retry in `openRootMenuVerified()` stays until
the cause is understood.

Two items from the manual pass are recorded as **open and unexplained** rather
than closed: the "malformed window dressing" (never reproduced; the leading
hypothesis was ruled out by measurement, and the drag clamp may have removed only
its precondition) and the sideways tab-label geometry nit (never pinned to
specifics across two passes).

## What verification caught

The phase verifier read the code rather than these summaries and found four
defects **in `docs/RELEASE-NOTES.md`** — the shipped, user-facing document. Two
are the same failure class this phase spent fourteen plans removing from the
code: a control that looks available and silently does nothing.

1. **`rule-match-name` was documented as matching "the window title".** It
   matches the WM_CLASS instance name. This is precisely the gap RULES-01 was
   being held Pending for — so the false promise had been moved off the tracked
   ledger and into users' hands, which is worse than leaving it on the ledger.
   Now documented accurately, with title matching stated as not implemented.

2. **`rule-match-type` advertised `utility`, `splash` and `toolbar`.**
   `src/Config.cpp` deliberately rejects all three — its own comment says
   accepting them "would hand the user a rule that silently never fires" — and a
   rejected criterion leaves the rule with none, which `ruleMatches()` treats as
   matching nothing. So following the documentation produced a rule that did
   exactly nothing. Now only the four accepted types are listed, with the reason.

3. **`rule-skip-taskbar` was entirely undocumented** while the notes claimed
   "the three actions that ship". There are four. It is implemented
   (`src/Config.cpp`, `src/Rules.cpp`) and named in RULES-02.

4. **The remote-target table still read "validation pending"** for TigerVNC and
   XRDP after both were validated, and carried D-8-TIGHTVNC without D-8-X2GO.

It also found two accounting errors in my own checklist walk: the circulation row
was ticked while its annotation said PARTIAL (every other PARTIAL is unticked),
and the gap table inherited that error while omitting the Catch2 row. Corrected —
the real figures are **89 ticked, 14 open**.

The lesson is not subtle. This plan's whole subject was the gap between what a
system claims and what it does, and the documentation describing it had drifted
in exactly that way while the code underneath was being held to measurement. The
verifier was right to read `src/` instead of `.md`.
