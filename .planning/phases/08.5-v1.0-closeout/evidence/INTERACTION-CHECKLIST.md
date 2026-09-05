# User Interaction Checklist — per-item results

**Tester:** tpatarci (operator)
**Date:** 2026-08-30
**Target:** TigerVNC `:11`, 1280x1024x24, loopback only
**Commit:** `0244c06` · **Binary:** `build/release/wm2-born-again`
**Session:** `scripts/start-validation-session.sh :11` — wm2 alone, no desktop
environment, isolated `XDG_CONFIG_HOME`

Closes the last open component of **TEST-08**. Phase 8 ran two manual passes and
the second produced a general verdict; this is the per-item form.

## How to read the Result column

| Value | Means |
|---|---|
| **PASS** | Walked step by step with the operator in this session, one row at a time |
| **PASS (verdict)** | Covered by the operator's own statement — *"all tested extensively. all good to go."* — but not walked as an individual row here |
| **DEFERRED** | Explicitly declined by the operator: *"We are not running the seven. It is deferred."* |
| **n/a** | Not applicable in this session, with the reason given |

The three values are kept apart on purpose. Collapsing "walked" into "verdict"
is exactly the blurring that left TEST-08 short after Phase 8, and nine of the
eleven DEFERRED rows — **#4, #5, #7, #8, #9, #10, #20, #21, #22** — are the
ones where **no automated test exists either** — so for those, nothing but a
human can have exercised them, and no human did in this session (row #8's
several-client circulation was exercised in the Phase 8 manual passes, which
produced a general verdict rather than a per-item result). The other two DEFERRED rows
carry some automated coverage in their own Notes even though the row itself
was declined: **#6** (zero-client circulation is covered by `[wm_circulate]`)
and **#19** (the resulting maximize state is covered by `[wm_fsmax]`, even
though the middle-click gesture that would trigger it is not).

---

## Interaction

| # | Item | Result | Notes |
|---|---|---|---|
| 1 | Root menu appears, fits at screen edges, highlights correctly, unmaps after selection | **PASS** | Including the bottom-right edge case |
| 2 | Root menu `New` launches the configured command (plain exec) | **PASS** | xclock appeared, framed |
| 3 | Root menu `New` in shell mode | **n/a** | Needs a WM restart with `--exec-using-shell`; covered automatically by `[wm_config_runtime]`, which proves both settings with a filesystem witness |
| 4 | Hidden-client entries restore through the **menu row** | **DEFERRED** | No automated coverage either |
| 5 | Menu exit item at the lower-right edge, and exits | **DEFERRED** | No automated coverage either |
| 6 | Circulation — zero clients | **DEFERRED** | Automated: `[wm_circulate]` covers zero-client |
| 7 | Circulation — one client | **DEFERRED** | |
| 8 | Circulation — several normal clients | **DEFERRED** | Exercised in the Phase 8 manual passes |
| 9 | Circulation — with a hidden client | **DEFERRED** | No automated coverage either |
| 10 | Circulation — with a transient/dialog | **DEFERRED** | No automated coverage either |
| 11 | Tab/frame click raises and focuses per policy | **PASS (verdict)** | Automated: `[wm_focus]`, six cases |
| 12 | Dragging a tab moves the window | **PASS** | Operator: *"name tab always remains available"* |
| 13 | Dragged hard off the top-left, the tab stays reachable | **PASS** | The `a5a105c` clamp. Operator's own defect report from the Phase 8 pass — confirmed fixed |
| 14 | Resize handle resizes normally | **PASS** | |
| 15 | Constrained resize, horizontal-only and vertical-only | **PASS** | |
| 16 | Tab button short press hides | **PASS** | |
| 17 | Long press past `destroy-window-delay` deletes, cursor restored | **PASS** | **Produced a finding — see below** |
| 18 | The close button is comfortable to hit | **PASS** | Operator: *"feel is just right"*. The `43fb24b` widening, 8×8 → the tab's whole top square. Operator's own defect report — confirmed fixed |
| 19 | Middle-click a tab toggles maximize | **DEFERRED** | Gesture has no automated coverage; the *state* is covered by `[wm_fsmax]` |
| 20 | Right-button circular gesture toggles fullscreen | **DEFERRED** | `detectFullscreenGesture()` has **zero** coverage of any kind |
| 21 | ...and a short/noisy gesture is ignored | **DEFERRED** | As above |
| 22 | Pointer grabs released after every cancel path | **DEFERRED** | No automated coverage as a property |

## Rendering and feel

| # | Item | Result | Notes |
|---|---|---|---|
| 23 | The sideways tab looks right | **PASS** | Re-judged after the 08.5-02 restyle: *"It is OK. All works fine."* |
| 24 | Tab label tracks the title and is legible | **PASS (verdict)** | Ubuntu Bold, rotated. Automated: `[wm_tablabel]` |
| 25 | Long title on a short window truncates without artifacts | **PASS (verdict)** | Automated: `[wm_tablabel]` |
| 26 | Configured colours apply | **PASS (verdict)** | Automated: `[wm_config_runtime]` |
| 27 | Frame thickness at min, default, max | **PASS (verdict)** | Automated: `[wm_config_runtime]` |
| 28 | Latency usable over this connection | **PASS (verdict)** | No number measured; subjective and unprompted |

## Window rules (new in 08.5-01)

| # | Item | Result | Notes |
|---|---|---|---|
| 29 | A title-matched window opens at the rule's position | **n/a** | Not walked with the operator. **Verified by the orchestrator** in a real TigerVNC session at `e390477`: `validation clock` framed at 300,200 (client at +324+208). Automated: `[wm_rules]` |
| 30 | Renaming afterwards does **not** move it (D-8.5-03) | **n/a** | Automated: `[wm_rules]`, with a mutation that reddens if a re-fold is added |
| 31 | `rule-match-instance` works; `rule-match-name` warns as unknown | **n/a** | Orchestrator-verified in session: `xeyes` mapped undecorated as a direct child of root. Automated: `[rules]` |

---

## Findings from this sitting

**1. `destroy-window-delay` was far too long.** Operator: *"The close delay is way
too long. It needs to be near the lowest (shortest) period applicable — this is
software for adept people, not for novices."*

It was **1500 ms**, upstream wm2's `CONFIG_DESTROY_WINDOW_DELAY` from 1997,
carried over verbatim and never revisited. Now **400 ms** (`560f5b3`). Not the
parser minimum of 1, because the failure is asymmetric: a false hide costs
nothing, a false delete sends `WM_DELETE_WINDOW` and can lose work, and an
ordinary click runs 50–150 ms.

**2. The appearance was wrong for the operator.** Reported as disliking the
colours and font, wanting a neutral Ubuntu face and a silver, lightly metallic
look with black elements, plus discrete 3D on the borders and label area.
Delivered in `0244c06`; re-judged **OK** in this session.

**3. A correction to the record, unresolved.** On row 13 the operator noted that
the orchestrator's account of the original off-screen-drag defect is *"somewhat
inaccurate"*, while confirming the bug itself is gone. The specific inaccuracy
was not identified, so **the code comment in `src/Client.cpp` and the
`08-14-SUMMARY.md` narrative still carry the orchestrator's version.** Recorded
here rather than quietly left, because a plausible-sounding but wrong story in a
code comment is the kind of thing that outlives everyone who could correct it.

## Coverage summary

| | Count |
|---|---|
| Walked step by step (**PASS**) | 10 |
| Operator verdict (**PASS (verdict)**) | 6 |
| **DEFERRED** by operator decision | 11 |
| **n/a** with reason | 4 |
| **Total** | **31** |

*Recounted 2026-09-05 from the Result column by the phase verification: row 23 ("The
sideways tab looks right") is recorded **PASS**, so the split is 10 / 6, not the 9 / 7 the
summary carried since `ec84a44`. No Result value was changed; only this summary.*

**The eleven deferred rows are the honest gap in this release**, and they are not
randomly distributed: nine of them are *gestures* — circulation permutations,
middle-click maximize, the circular fullscreen gesture, grab release — plus the
menu's exit and hidden-client rows. That is the same blind spot that let all
three of the Phase 8 manual pass's defects through: the suite asserts the
**states** these controls produce and never the **reachability** of the controls
themselves.

The v1.1 backlog line "Gesture and input coverage" exists for exactly this, and
this table is its evidence.
