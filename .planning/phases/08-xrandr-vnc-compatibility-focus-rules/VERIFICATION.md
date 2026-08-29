---
phase: 08-xrandr-vnc-compatibility-focus-rules
verified: 2026-08-29T23:24:27Z
verified_at_commit: aae69df
status: gaps_found
score: 5/7 must-haves verified
behavior_unverified: 0
overrides_applied: 0
gaps:
  - truth: "The WM runs on TigerVNC, TightVNC, XRDP, and X2Go without crashes or missing functionality (ROADMAP SC2, XDIS-05)"
    status: partial
    reason: >-
      The graceful-degradation half is fully delivered and behaviourally proven. The
      four-target half is not: only TigerVNC and XRDP were exercised. TightVNC and
      X2Go have no session, no transcript and no interaction record. Two recorded
      deviations satisfy the checklist row "list of accepted deviations"; they do not
      satisfy a requirement whose subject is the four targets. D-8-X2GO says so in its
      own text: "That is a decision to stop testing... It is not evidence that X2Go
      works, and it must never be recorded as one."
    artifacts:
      - path: ".planning/phases/08-xrandr-vnc-compatibility-focus-rules/evidence/"
        issue: "No x2go/ or tightvnc/ directory. 2 of 4 named targets have transcripts."
      - path: "docs/RELEASE-NOTES.md:239-241"
        issue: >-
          Remote-desktop status table still lists TigerVNC, XRDP AND X2Go all as
          "Validation pending a real session", though two of them were validated. The
          D-8-X2GO deviation is absent from the release notes while D-8-TIGHTVNC is
          present, so the one target with no tested proxy is the one the shipped
          document is quietest about.
    missing:
      - "A TightVNC capability transcript (scripts/capture-display-capabilities.sh <display> tightvnc)"
      - "An X2Go capability transcript (apt install x2goserver, then the same script)"
      - "Update docs/RELEASE-NOTES.md rows for TigerVNC and XRDP to reflect the committed transcripts"
      - "Carry D-8-X2GO into docs/RELEASE-NOTES.md alongside the existing D-8-TIGHTVNC section"
  - truth: "Window rules can match windows by WM_CLASS, WM_NAME and window type (ROADMAP SC5, RULES-01)"
    status: partial
    reason: >-
      WM_CLASS matching (both res_name and res_class) and window-type matching are
      delivered, tested and correct. Matching on the window TITLE (WM_NAME) does not
      exist anywhere in the codebase. RuleWindowFacts carries no title field, and the
      confusingly-named rule-match-name key matches the WM_CLASS instance name. The
      shortfall is aggravated, not mitigated, by the shipped release notes documenting
      rule-match-name as "the window title".
    artifacts:
      - path: "include/Rules.h:52-56"
        issue: "RuleWindowFacts has instanceName, className, type -- no window-title field."
      - path: "src/Rules.cpp:47-52"
        issue: "hasMatchName is compared against facts.instanceName only, never a title."
      - path: "src/Client.cpp:1071-1073"
        issue: >-
          Facts are populated from m_resName/m_resClass (XGetClassHint, line 1033).
          Client::m_name holds WM_NAME and is never passed to the matcher.
      - path: "docs/RELEASE-NOTES.md:154"
        issue: >-
          BLOCKER-grade documentation defect. Documents rule-match-name as "(the window
          title)". A user writing `rule-match-name = Downloads` against a window titled
          "Downloads" gets silence with no warning -- the key is valid and the value
          simply never matches. The shipped docs promise the exact feature the ledger
          honestly declined to claim.
      - path: "docs/RELEASE-NOTES.md:155-156"
        issue: >-
          Documents rule-match-type as accepting `utility`, `splash`, `toolbar`. The
          parser (src/Config.cpp:313-324) rejects all three, warns, and leaves the
          criterion unset -- and an unset sole criterion makes ruleMatches() return
          false for every window (src/Rules.cpp:32-34). Following the documentation
          silently disables the whole rule. include/Rules.h:38-42 explains exactly why
          those names are refused; the release notes offer them anyway.
    missing:
      - "A title field on RuleWindowFacts fed from Client::m_name, plus a rule-match-title criterion, parser branch and unit tests"
      - "A decision, stated in the docs, on WM_NAME mutability: rules fold once at manage time, so a title rule matches the title held at map time"
      - "Correct docs/RELEASE-NOTES.md:154 -- rule-match-name matches the WM_CLASS instance name, not the title"
      - "Correct docs/RELEASE-NOTES.md:155-156 -- accepted types are exactly normal, dock, dialog, notification"
  - truth: "Runtime smoke evidence captured for nested/headless X11 and supported remote desktop targets, including interaction checklist results (TEST-08)"
    status: partial
    reason: >-
      Substantially satisfied and genuinely strong on the parts that landed, but two
      named components are absent. (a) "supported remote desktop targets" is 2 of 4
      with transcripts. (b) "interaction checklist results" does not exist as results:
      two manual passes produced narrative findings and a general verdict ("all works
      like a charm"), not a per-row pass/fail with tester and date. The checklist's own
      Release Evidence row for exactly this is unticked and says so.
    artifacts:
      - path: "COMPILED_CODE_BEHAVIOR_CHECKLIST.md:637"
        issue: "Release-evidence row 'Manual interaction checklist results with tester name and date' is unticked and marked PARTIAL by its own annotation."
      - path: ".planning/phases/08-xrandr-vnc-compatibility-focus-rules/evidence/tigervnc/capabilities.txt"
        issue: >-
          Self-reports commit dba2f30, tree-state DIRTY -- four behaviour-changing
          commits older than HEAD (12f6de8, fa33f2e, a5a105c, 43fb24b). evidence/README.md
          frames the whole bundle as "taken at commit c61cb4b", which papers over this.
          The consequence is visible in the artifact: tigervnc/root-properties.txt shows
          _NET_CLIENT_LIST containing the WM's own menu/check windows (0x40001b, 0x40001c,
          0x40001a) -- deferred item 7, fixed in 12f6de8, AFTER that capture. The
          committed transcript demonstrates a defect that no longer exists.
      - path: ".planning/phases/08-xrandr-vnc-compatibility-focus-rules/evidence/xrdp/capabilities.txt"
        issue: "Self-reports commit 26344b4, tree-state DIRTY. Same staleness class as above."
    missing:
      - "One run of the User Interaction Checklist recorded as a table: row, pass/fail, tester, date"
      - "TightVNC and X2Go transcripts (shared with the XDIS-05 gap above)"
      - "A note in evidence/README.md that per-target transcripts carry their own, older commit stamps and DIRTY tree state"
documentation_defects:  # code is correct; the shipped user-facing doc is not
  - path: "docs/RELEASE-NOTES.md:154"
    severity: blocker
    issue: "rule-match-name documented as 'the window title'. It matches the WM_CLASS instance name."
  - path: "docs/RELEASE-NOTES.md:155-156"
    severity: blocker
    issue: "rule-match-type documented as accepting utility/splash/toolbar. The parser rejects all three and the rule then matches nothing."
  - path: "docs/RELEASE-NOTES.md:159-165"
    severity: warning
    issue: >-
      Heading reads "The three actions that ship" and lists no-decorate, position, size.
      rule-skip-taskbar appears NOWHERE in the release notes (zero matches), despite
      being shipped, tested ([wm_rules]) and named by RULES-02. A delivered action is
      undiscoverable.
  - path: "docs/RELEASE-NOTES.md:239-241"
    severity: warning
    issue: "Remote-desktop table stale for TigerVNC/XRDP; D-8-X2GO deviation not carried into the release notes."
checklist_accounting_errors:
  - path: "COMPILED_CODE_BEHAVIOR_CHECKLIST.md:434"
    issue: >-
      Box is ticked [x] while its own body says "PARTIAL -> ... The hidden-client and
      transient-client permutations have no automated case". Every other PARTIAL in the
      file (447, 487, 492, 588, 602, 637) is unticked. This is the only tick in the file
      I would contest; it should be [ ].
  - path: ".planning/phases/08-xrandr-vnc-compatibility-focus-rules/08-14-SUMMARY.md:97"
    issue: >-
      Claims "The 13 that are not ticked are listed below". The table below is a
      different set of 13: it includes line 434 (a TICKED box) and omits line 117
      (Catch2 via FetchContent from GitHub) -- the one unticked box in the file with no
      inline annotation at all. Verified still live at CMakeLists.txt:73-79. The count
      is right by coincidence.
deferred: []  # Phase 9 (Config GUI + IPC, CGUI-01..05) is the last phase and covers none of these
human_verification:
  - test: "Run the User Interaction Checklist end to end on one XRDP and one TigerVNC session, recording pass/fail per row with tester name and date"
    expected: "A table with a result for each of the ~14 User Interaction rows, replacing the current general verdict"
    why_human: "Gesture reachability -- whether a human can hit the control -- is precisely what the automated suite cannot assert, and is where all three of this phase's manual-pass defects lived"
  - test: "Install x2goserver, start a session, run scripts/capture-display-capabilities.sh <display> x2go, and drive the interaction checklist over it"
    expected: "An evidence/x2go/ directory with capabilities, root properties and window tree; SHAPE/RANDR/RENDER state recorded whatever it turns out to be"
    why_human: "Requires a server install, a live session and a human at a client. nxagent shares no ancestry with Xvnc or xrdp, so nothing already captured transfers to it"
  - test: "Same for TightVNC (scripts/capture-display-capabilities.sh <display> tightvnc)"
    expected: "An evidence/tightvnc/ directory"
    why_human: "Requires a running server and a client session"
---

# Phase 8: Xrandr + VNC Compatibility + Focus/Rules — Verification Report

**Phase Goal:** The WM works reliably across VNC, XRDP, and X2Go with graceful extension fallbacks, users get fine-grained control over focus behavior and per-window rules, and the compiled binary is proven through repeatable build, sanitizer, Xvfb/Xephyr, and runtime smoke gates.

**Verified:** 2026-08-29T23:24:27Z at commit `aae69df`
**Status:** gaps_found
**Re-verification:** No — initial verification

---

## Method note

This verification did not take SUMMARY.md claims as evidence. Every VERIFIED truth
below is backed by source I read at HEAD and by a test I ran myself. I rebuilt the
debug tree and ran the full suite once: **295/295 passed, ctest exit 0**. I ran
`scripts/preflight.sh` independently: **exit 0**, resolving `x11 xext xft fontconfig
xrandr xrender` and all three fontconfig chains. `git diff c61cb4b..HEAD` shows the
evidence bundle's source tree is unchanged from the gate snapshot — only docs and
evidence landed after it — so the committed gate logs describe the code at HEAD.

---

## Goal Achievement

### Observable Truths (ROADMAP Success Criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Resolution changes via xrandr handled correctly — windows reposition within the new screen geometry | ✓ VERIFIED | `handleScreenGeometryChange()` (`src/Manager.cpp:506-543`) re-reads root geometry from the server rather than trusting event fields, coalesces on distinct geometry, reflows both visible and hidden lists via `ensureVisible()`, republishes `_NET_WORKAREA`. Behaviourally proven, not inferred: `[wm_geometry]` "A resolution change updates `_NET_WORKAREA`", "A window left offscreen by a shrink is moved back into view, not resized", "Redelivered screen-change notifications carrying stale dimensions change nothing", "A fullscreen client is not repositioned by the reflow" — all pass in my run |
| 2 | WM runs on TigerVNC, TightVNC, XRDP and X2Go without crashes or missing functionality; unavailable extensions degrade gracefully | ✗ FAILED (split) | **Degradation half VERIFIED.** Shape funnel `Border::combineShape()` (`src/Border.cpp:317-326`) early-returns so no Shape traffic is emitted at all; `[wm_noshape]` 5 cases + `[shape_invariant]` (asserts the Xlib entry point is named exactly once). RANDR: `[wm_norandr]` 4 cases incl. "With RANDR inert the root-ConfigureNotify fallback still reflows". RENDER: `[wm_norender]` 6 cases + `[xft_norender_spike]`. **Four-target half FAILED:** 2 of 4 exercised. See gap 1 |
| 3 | Focus stealing prevention works — new windows do not grab focus unless the user interacted recently | ✓ VERIFIED | `Client::shouldFocusOnMap()` (`src/Client.cpp:385-437`) with wraparound-safe comparison `isUserTimeRecent()` (`src/Manager.cpp:857`), user-time-window proxy handling, bounded type-checked property reads, and the D-19 legacy-client carve-out. Wired at map (`src/Client.cpp:275-279`) and at activation (`src/Events.cpp:448-458`). 12 `[wm_focus]` process-level cases incl. stale/fresh/absent/zero user-time, proxy arbitration, pager-vs-application source, malformed messages, and the off switch — all pass |
| 4 | Users can set focus policy to click-to-focus, focus-follows-pointer, or auto-raise via the config file | ✓ VERIFIED | All three booleans reach real runtime behaviour, not just the startup banner: `clickToFocus` gates pointer focus at `src/Client.cpp:2092`; `raiseOnFocus` at `1513` and `2106`; `autoRaise` arms the deadline at `src/Manager.cpp:1362-1367`. 6 `[wm_focus]` cases drive the real binary with real pointer events and assert **both directions** of each setting, plus "With auto-raise off the WM burns no CPU while focus tracking is live" |
| 5 | Window rules can match by WM_CLASS or WM_NAME and apply no-decorate, position/size, skip-taskbar | ✗ FAILED (split) | **Actions half VERIFIED** — all three ship and are proven at process level: `[wm_rules]` 8 cases incl. no-decorate → no frame, position+size with offscreen clamp, skip-taskbar → skip-taskbar **and** skip-pager, later-wins fold, type-only rule, and two negative cases. Plus 17 display-free `[rules]` unit cases. **Matching half FAILED:** WM_NAME (title) matching does not exist. See gap 2 |
| 6 | Checklist gates pass or have explicit accepted exceptions (preflight, Debug/Release builds, full CTest, ASan/UBSan, runtime smoke, release evidence) | ✓ VERIFIED | Independently re-ran: `cmake --build build/debug` exit 0; `ctest` **295/295**, exit 0; `scripts/preflight.sh` exit 0. Committed gates at `evidence/gates/PROVENANCE.txt`: debug/release/asan/static-analysis all exit 0, 295/295 in all three trees. Checklist baseline of 295 matches the tree's actual count. Both deviations carry reason, owner and follow-up as the section requires |
| 7 | Behavior-scan findings resolved or explicitly accepted: xft/fontconfig build preflight, eventDestroy lifetime hazard, no-Shape fallback proof, focus config wiring proof | ✓ VERIFIED | (a) preflight resolves `xft 2.3.4` + `fontconfig 2.13.1` and all three chains, wired as blocking ctest fixture #293. (b) eventDestroy use-after-free fixed in 08-01; `[wm_lifecycle]` "Destroying a mapped focused client leaves no stale list or active-window entry" + destroy-while-focus-candidate + destroy-while-hidden, clean under ASan. (c) `[wm_noshape]` + screenshot. (d) `[wm_focus]` 6 policy cases — the three settings were dead booleans before 08-07 |

**Score:** 5/7 truths verified (0 present-but-behaviour-unverified)

Both failures are *split* truths: a substantial, well-built half is delivered and the
other half is genuinely absent. Neither is a stub, and neither was misrepresented in
the phase artifacts.

---

## The Three Requested Adjudications

### 1. RULES-01 — Pending is CORRECT. The honest resolution is to **implement**, not amend.

I checked the code rather than the reasoning, and the reasoning holds.
`RuleWindowFacts` (`include/Rules.h:52-56`) carries `instanceName`, `className` and
`type` — no title field. `ruleMatches()` compares `hasMatchName` against
`facts.instanceName` only (`src/Rules.cpp:47-52`). `Client::applyWindowRules()`
populates both strings from `XGetClassHint` (`src/Client.cpp:1033`, `1071-1073`).
`Client::m_name` holds WM_NAME and is never passed to the matcher. Two of three
promised criteria ship. **Pending is correct.**

**Implement, do not amend — and the reason is specific.** XDIS-04 and RULES-02 were
amended because their dropped promises could not be built without undoing a prior
architectural decision: Phase 4 deleted core X fonts and the bundled rotation library
by decision, and Phase 6 fixed `_NET_NUMBER_OF_DESKTOPS` at 1 so a "send to workspace"
action has nothing to target. Those are the good kind of amendment — they remove a
promise that a later phase would have had to reverse a decision to keep. WM_NAME
matching has no such obstacle. `Client::m_name` already holds the value,
`RuleWindowFacts` is a plain struct, `criterionMatches()` is already mode-agnostic,
and the parser already has a match-key group with a state machine that handles it.
It is a field, a criterion pair, a parser branch and a few unit cases.

**There is a second, stronger reason not to amend.** `docs/RELEASE-NOTES.md:154`
already tells users that `rule-match-name` matches *"the window title"*. Amending
RULES-01 to drop WM_NAME would leave the shipped documentation promising a feature the
ledger had just declared out of scope — moving the false promise from the ledger into
the user's hands, which is worse than leaving it in the ledger where it is tracked.

**One design wrinkle to settle before implementing:** WM_NAME is mutable, and rules
fold exactly once at manage time (`applyWindowRules()` is called from the map path).
A title rule would therefore match whatever title the window carried at map time. That
is defensible and is what most window managers do, but it must be *stated*, because a
user will reasonably expect "match the title" to keep matching as the title changes.

If a future maintainer nevertheless amends, the minimum honest amendment must also fix
`docs/RELEASE-NOTES.md:154` and rename or re-document `rule-match-name`.

### 2. XDIS-05 — Pending is CORRECT. Two deviations do not satisfy it.

Recorded deviations satisfy the *checklist row* "List of accepted deviations, each
with owner and follow-up" — which is correctly ticked, and the two entries are
unusually good ones, each carrying reason, owner, follow-up and an explicit statement
of what a user should expect meanwhile. They do not satisfy the *requirement*, whose
subject is the four targets working out of the box.

The distinction is not pedantry: an accepted deviation is a decision about process
("we are shipping without this evidence, here is who owns getting it"), not evidence
about behaviour. D-8-X2GO makes this argument against itself, in its own text: *"That
is a decision to stop testing, which is theirs to make. It is not evidence that X2Go
works, and it must never be recorded as one."* Marking XDIS-05 Complete would
contradict the paragraph offered in support of it.

The two deviations are also asymmetric, and the phase correctly says so. TigerVNC is a
genuine `Xvnc`-lineage proxy for TightVNC. X2Go's `nxagent` is a proxy for nothing
tested, and it interposes a compression layer in front of exactly the SHAPE/RANDR/
RENDER surface this phase depends on. So even a lenient reading that accepted
TightVNC-by-proxy could not accept X2Go.

**Additional finding that strengthens Pending, not raised by the orchestrator:**
`docs/RELEASE-NOTES.md:239-241` still lists TigerVNC, XRDP **and X2Go** all as
"Validation pending a real session… this row is updated from them, not before." The
rows were never updated. So the shipped document simultaneously (a) fails to credit
the two targets that were genuinely validated, and (b) makes X2Go look identical to
them, hiding that it belongs in the same untested category as TightVNC. The
D-8-X2GO deviation is carried in the checklist and in `evidence/README.md` but **not**
in the release notes, while D-8-TIGHTVNC is. The one target with no tested proxy is
the one the user-facing document is quietest about.

### 3. TEST-08 — **Partially satisfied; correctly still Pending.**

Delivered, and it is real work: `xprop -root` and `xwininfo -root -tree` transcripts
for `local-xephyr`, `tigervnc` and `xrdp` (plus capability-only `x11vnc-xvfb`); a
reusable, syntax-clean capture script that records a missing extension as a *result*
rather than a failure and warns about title leakage; seven screenshots including the
shaped/rectangular pair; complete gate logs; and two accepted deviations with reason,
owner and follow-up. That is more evidence than most projects ship.

Not delivered, and both are named in the requirement text:
- **"supported remote desktop targets"** — 2 of 4 have transcripts.
- **"interaction checklist results"** — these do not exist *as results*. Two manual
  passes were run (tpatarci, 2026-08-29 and 2026-08-30) and their findings are recorded
  narratively, but the second produced a general verdict ("all works like a charm"),
  not a per-row pass/fail with tester and date. The checklist's own release-evidence
  row for exactly this is unticked and says so plainly.

The close is small and bounded: run the User Interaction Checklist once as a table.
That alone closes component (b) and would resolve most of the 13 unticked rows one way
or the other.

**A staleness finding nobody raised.** `evidence/README.md` frames the bundle as
"Snapshot taken at commit `c61cb4b`". The per-target transcripts carry their own,
older stamps: `tigervnc/capabilities.txt` self-reports `commit dba2f30`,
`tree-state: DIRTY`; `xrdp/capabilities.txt` reports `26344b4`, `DIRTY`. Both predate
four behaviour-changing commits (`12f6de8`, `fa33f2e`, `a5a105c`, `43fb24b`). The
consequence is visible in the artifact: `tigervnc/root-properties.txt` shows
`_NET_CLIENT_LIST` containing the WM's own menu and check windows (`0x40001b`,
`0x40001c`, `0x40001a`) — deferred item 7, fixed in `12f6de8`, *after* that capture.
The committed transcript demonstrates a defect that no longer exists. This is not
dishonest — each transcript stamps its own commit and DIRTY state, which is exactly
the discipline that let me catch it — but the README's single-commit framing papers
over it, and a reader diffing a future transcript against this one will find a phantom
regression in reverse. One sentence in the README fixes it.

---

## The 13 Unticked Boxes

**Honestly reported: yes, with two accounting errors.**

I verified the annotations against code rather than accepting them. They hold:
`detectFullscreenGesture()` genuinely has no test case anywhere in `tests/`; the
`[servergrab]` cases genuinely concern `XGrabServer` and not pointer grabs;
`WM_COLORMAP_WINDOWS` genuinely has its property read tested (`tests/test_client.cpp`)
and its install order untested. Several annotations are self-incriminating in a way
nobody writes to look good — *"which is a decision to stop, not evidence that X2Go
works"*, *"Remains open"* on twelve rows, and an explicit "Known limitation, not
covered by this tick" attached to a **ticked** box (submenus taller than the screen,
202 of 319 cached apps in `Other`).

Two accounting errors, both minor and neither self-serving:

1. **`COMPILED_CODE_BEHAVIOR_CHECKLIST.md:434` is ticked `[x]` while its own body says
   PARTIAL** with two named uncovered permutations. Every other PARTIAL in the file
   (447, 487, 492, 588, 602, 637) is unticked. This is the only tick in the file I
   would contest; it should be `[ ]`, making the tally 89/103.
2. **`08-14-SUMMARY.md:97` claims "The 13 that are not ticked are listed below"** — the
   table below is a *different* set of 13. It includes line 434 (a ticked box) and
   omits line 117 (Catch2 fetched from GitHub via `FetchContent`, verified still live
   at `CMakeLists.txt:73-79`), which is the one unticked box in the entire file
   carrying no inline annotation at all. The count 13 is right by coincidence.

Neither error inflates the result — error 1 costs a tick, and error 2 hides an open
item that is a policy question rather than a defect. Both are worth correcting so the
document stays trustworthy by construction.

### Is "gesture-level input is thinly covered" fair?

**Yes, and if anything it is understated.** Of the six unticked User Interaction rows,
five are gestures (menu row click for hidden-client restore, menu exit row,
middle-click maximize, right-button circular gesture, pointer-grab release across
cancel/destroy) and the sixth (tab drag) has its resulting *state* asserted but not
the synthetic `ConfigureNotify` it must send. Meanwhile the states those gestures
reach are covered heavily: `[wm_fsmax]` 7 cases, `[wm_state]` 8 cases, `[wm_lifecycle]`
hide/unhide. That is precisely the shape the SUMMARY claims.

It is corroborated by the strongest evidence available — outcomes, not argument. All
three defects the manual pass found were *reachability* defects in paths the suite
already exercised for state: a window draggable past recovery (`a5a105c`), a frame
`ConfigureRequest` teleport (`a5a105c`), and an 8×8 close-button target (`43fb24b`).
The button case is the purest illustration: every test that needed to press that
button computed the coordinate rather than trying to hit it. The conclusion is earned
from evidence, not rationalised after the fact.

---

## Requirements Coverage — Ledger Adjudication

| Requirement | Ledger | My adjudication | Evidence |
|---|---|---|---|
| XDIS-01 | Complete | ✓ **Correct** | `handleScreenGeometryChange()` + `screenWidth()/screenHeight()` single-source accessors; 4 `[wm_geometry]` resolution-change cases pass |
| XDIS-02 | Complete | ✓ **Correct** | `XRRQueryExtension` guarded with `-1` sentinel; root-ConfigureNotify fallback path; `[wm_norandr]` 4 cases incl. reflow-without-RANDR |
| XDIS-03 | Complete | ✓ **Correct** | Single `combineShape()` funnel; `[wm_noshape]` 5 cases; `[shape_invariant]` asserts the Xlib call is named exactly once; screenshot |
| XDIS-04 | Complete (amended) | ✓ **Correct — and the amendment is legitimate** | Four rungs at `src/Border.cpp:156-221`, none calling `fatal()`. `[wm_norender]` 6 + `[xft_norender_spike]` back the claim that libXft renders the rotated face via the core glyph path with RENDER absent. Amendment removes a promise that would require reversing Phase 4; reason recorded in the requirement text itself, not only in a plan |
| XDIS-05 | **Pending** | ✓ **Correct** | 2 of 4 targets. Deviations are process decisions, not behavioural evidence. See adjudication 2 |
| FOCUS-01 | Complete | ✓ **Correct** | `shouldFocusOnMap()` + wraparound-safe `isUserTimeRecent()`; wired at both map and activation; 12 `[wm_focus]` cases |
| FOCUS-02 | Complete | ✓ **Correct** | Three booleans reach real behaviour at 4 named sites; 6 `[wm_focus]` cases assert both directions each |
| RULES-01 | **Pending** | ✓ **Correct** | No title field anywhere. See adjudication 1 |
| RULES-02 | Complete (amended) | ✓ **Correct in code — ⚠️ misdocumented** | All three actions ship and are proven: `[wm_rules]` 8 cases. Amendment legitimate (single desktop by design; `_NET_NUMBER_OF_DESKTOPS = 1` confirmed in the tigervnc transcript). **But `rule-skip-taskbar` appears nowhere in `docs/RELEASE-NOTES.md`** — a delivered, tested, requirement-named action is undiscoverable. Keep the tick; fix the doc |
| TEST-05 | Complete | ✓ **Correct** | `WmFixture` launches the real binary via `WM2_BINARY_PATH`; ~145 process-level cases across 10 files covering create/map/unmap/remap/hide/unhide/fullscreen/maximize/destroy with ICCCM + EWMH property assertions |
| TEST-06 | Complete | ✓ **Correct** | Three trees green at the gate snapshot; I independently re-verified debug at HEAD (295/295, exit 0). Sanitizer reports are suppression accounting, and the bundle says so and ships them in full so the claim can be checked rather than believed |
| TEST-07 | Complete | ✓ **Correct** | Ran it myself: exit 0, resolving `x11 xext xft fontconfig xrandr xrender`, X11 tooling, Xvfb/Xephyr, and all three fontconfig chains. Wired as blocking ctest fixture #293 |
| TEST-08 | **Pending** | ✓ **Correct (partially satisfied)** | See adjudication 3 |

### Is any Complete-marked requirement NOT actually delivered?

**No. All ten Complete marks are honest.** I verified each against source I read at
HEAD and tests I ran, not against SUMMARY claims. This is the finding that matters
most, and it is a clean result: the ledger does not overstate the code anywhere.

Two carry caveats that do not overturn the tick:

- **RULES-02** is delivered in code but its `rule-skip-taskbar` action is absent from
  the shipped release notes. Undiscoverable, not undelivered.
- **XDIS-04** and **RULES-02** are amended requirements. I applied the test — does the
  amendment remove a promise that could not be kept without reversing a prior
  decision, and is the reason recorded in the requirement text rather than only in a
  planning document? Both pass on both counts. These are the good kind of amendment.

**The real defects are not in the ledger — they are in `docs/RELEASE-NOTES.md`, the
shipped user-facing document, and they all point the same way: the docs promise more
than the code does.** Two of them are user-visible failures of exactly the kind this
phase spent fourteen plans eliminating from the code — a control that looks available
and silently does nothing:

| # | Location | Defect | Severity |
|---|---|---|---|
| 1 | `RELEASE-NOTES.md:154` | `rule-match-name` documented as "the window title". It matches the WM_CLASS instance name. A user writing `rule-match-name = Downloads` against a window titled "Downloads" gets silence, with no warning, because the key is valid and the value simply never matches. **This is the exact feature RULES-01 was honestly held Pending for — the ledger declined to claim it and the shipped docs claim it anyway.** | 🛑 Blocker |
| 2 | `RELEASE-NOTES.md:155-156` | `rule-match-type` documented as accepting `utility`, `splash`, `toolbar`. `src/Config.cpp:313-324` rejects all three, warns, and leaves the criterion unset — and an unset sole criterion makes `ruleMatches()` return false for *every* window (`src/Rules.cpp:32-34`). Following the documentation silently disables the entire rule. `include/Rules.h:38-42` explains precisely why those names are refused: *"offering those names here would hand the user a rule that silently never fires."* The release notes offer them. | 🛑 Blocker |
| 3 | `RELEASE-NOTES.md:159-165` | Table headed "The three actions that ship" omits `rule-skip-taskbar` entirely (zero matches in the file) | ⚠️ Warning |
| 4 | `RELEASE-NOTES.md:239-241` | Remote-desktop table stale for TigerVNC/XRDP; D-8-X2GO not carried into the release notes though D-8-TIGHTVNC is | ⚠️ Warning |

---

## Artifact Verification

| Artifact | Expected | Status | Details |
|---|---|---|---|
| `include/Rules.h` / `src/Rules.cpp` | X11-free rule model + matcher | ✓ VERIFIED | No X11 include (load-bearing: keeps `test_rules` display-free, 17 cases in ms). Tri-state actions correctly justified for the later-wins fold |
| `src/Client.cpp` (rules) | Class-hint read + rule application | ✓ VERIFIED | Bounded 1024-byte copy (T-8-PROP), no warning on absent hint by design, actions applied at 166-243 / 710 / 1083-1090 |
| `src/Client.cpp` (focus) | Map-time focus arbitration | ✓ VERIFIED | Type-checked, length-bounded, always-freed property reads; proxy window handling with scoped `ignoreBadWindowErrors` |
| `src/Manager.cpp` | Geometry cache, RANDR, atoms, user-time clock | ✓ VERIFIED | Single-writer/many-reader discipline enforced by comment and by the accessor pair; wraparound-safe timestamp comparison |
| `src/Border.cpp` | Shape funnel + font ladder | ✓ VERIFIED | One `XShapeCombineRectangles` call site, guarded; four font rungs, none fatal |
| `scripts/preflight.sh` | Dependency preflight | ✓ VERIFIED | Ran it: exit 0, all six required rows present |
| `scripts/capture-display-capabilities.sh` | Capability capture | ✓ VERIFIED | `bash -n` clean; emits the extension matrix, `xprop -root`, `xwininfo -root -tree`; documents the title-leak hazard |
| `COMPILED_CODE_BEHAVIOR_CHECKLIST.md` | Walked baseline | ⚠️ VERIFIED with 1 contested tick | 90 `[x]` / 13 `[ ]` / 103 total, counts confirmed. Line 434 contested |
| `docs/RELEASE-NOTES.md` | User-facing release doc | ✗ **DEFECTIVE** | Four defects, two of them blocker-grade. See table above |
| `evidence/` bundle | Committed signoff evidence | ⚠️ VERIFIED with staleness | Complete and well-indexed for what it contains; per-target transcripts carry older commit stamps and DIRTY tree state not disclosed in the README |

## Key Link Verification

| From | To | Via | Status |
|---|---|---|---|
| `Config::rules` | `Client::applyWindowRules()` | `windowManager()->config().rules` at `src/Client.cpp:1068` | ✓ WIRED |
| `applyRules()` | frame / geometry / `_NET_WM_STATE` | `m_ruleOutcome` consumed at 166-243, 710, 1083-1090 | ✓ WIRED |
| `XGetClassHint` | `RuleWindowFacts` | `m_resName`/`m_resClass` at 1071-1073 | ✓ WIRED |
| **WM_NAME (`m_name`)** | **`RuleWindowFacts`** | — | ✗ **NOT WIRED** (RULES-01 gap) |
| `config.clickToFocus/raiseOnFocus/autoRaise` | focus + stacking behaviour | 4 named call sites | ✓ WIRED |
| `shouldFocusOnMap()` | map path and activation path | `Client.cpp:275`, `Events.cpp:448` | ✓ WIRED (both entry points reach the same verdict) |
| RANDR notify + root ConfigureNotify | `handleScreenGeometryChange()` | `src/Events.cpp:111-146` | ✓ WIRED (both converge, coalesced) |
| `combineShape()` | every Shape request | 26 call sites funnelled; invariant test-enforced | ✓ WIRED |

## Behavioural Spot-Checks

| Behaviour | Command | Result | Status |
|---|---|---|---|
| Full test suite at HEAD | `cmake --build build/debug && ctest --test-dir build/debug` | build exit 0; **100% tests passed, 0 failed out of 295** | ✓ PASS |
| Dependency preflight | `bash scripts/preflight.sh` | exit 0; `x11 xext xft fontconfig xrandr xrender` + 3 fontconfig chains resolved | ✓ PASS |
| Test enumeration | `ctest --test-dir build/debug -N` | `Total Tests: 295` — matches the checklist baseline | ✓ PASS |
| Capture script integrity | `bash -n scripts/capture-display-capabilities.sh` | syntax OK | ✓ PASS |
| Evidence-vs-code drift | `git diff --stat c61cb4b..HEAD` | docs and evidence only; **no source changed** after the gate snapshot | ✓ PASS |
| Rule-type parser vs docs | read `src/Config.cpp:313-324` vs `RELEASE-NOTES.md:155` | parser accepts 4 names, docs advertise 7 | ✗ **FAIL** |
| `rule-match-name` semantics vs docs | read `src/Rules.cpp:47-52` vs `RELEASE-NOTES.md:154` | matches instance name; docs say "window title" | ✗ **FAIL** |
| ASan tree bare ctest | not run | known red — deferred item 5; the gate path `scripts/gates/build-all.sh asan` is the supported one and is green | ? SKIP |

## Anti-Pattern Scan

| Pattern | Scope | Result |
|---|---|---|
| `TBD` / `FIXME` / `XXX` | `src/ include/ tests/ scripts/ CMakeLists.txt` | **Zero matches** — no unreferenced debt markers |
| `TODO` / `HACK` / `PLACEHOLDER` / "not yet implemented" | same | **Zero matches** |
| Disabled/skipped tests | `tests/` | None found |
| Stub returns wired to output | `src/` | None found; every rule/focus/geometry value traces to a real X11 read or a config value |

The debt-marker gate passes cleanly. For a 14-plan phase touching this much code, zero
markers is a genuinely strong result.

## Test Quality Audit

| Area | Linked req | Active | Skipped | Circular | Assertion level | Verdict |
|---|---|---|---|---|---|---|
| `test_wm_focus.cpp` (21) | FOCUS-01/02 | 21 | 0 | No | Behavioural | ✓ Strong — real binary, real pointer events, both directions per setting |
| `test_wm_rules.cpp` (8) | RULES-02 | 8 | 0 | No | Behavioural | ✓ Strong — incl. two negative cases |
| `test_rules.cpp` (17) | RULES-01/02 | 17 | 0 | No | Value | ✓ Strong — asymmetry asserted *negatively* so it cannot silently drift |
| `test_wm_geometry.cpp` (17) | XDIS-01 | 17 | 0 | No | Behavioural | ✓ Strong — stale-event redelivery case is exactly right |
| `test_wm_fallbacks.cpp` (13) | XDIS-02/03/04 | 13 | 0 | No | Behavioural | ✓ Strong — `[shape_invariant]` is a source-level guard, a good pattern |
| `test_wm_resource.cpp` (4) | TEST-06 | 4 | 0 | No | Value | ✓ Strong — budget is human-written from a *separate* calibration run and explicitly not derived from the measurement it judges, avoiding the circularity trap |

**Disabled tests on requirements:** 0 · **Circular patterns:** 0 · **Insufficient
assertions:** 0. No blockers from this audit.

Known flake (deferred item 12/17): roughly one test per full run, `[wm_menureopen]`
quantified at 11/1 across 12 isolated runs **on the pre-existing code as well**, so the
menu rework did not cause it. My run was clean 295/295.

## Decision Coverage

Phase decisions D-04/05/06/08/11/14/19/20/21/22/23/24/25/26/27/28/29/30/32/33 were
spot-checked against shipped artifacts and are honoured in code or comment at the sites
they govern (e.g. D-27 single-source geometry accessors, D-21/D-22 matcher semantics,
D-23 workspace exclusion recorded in the requirement text itself, D-32 externally
derived budget). Non-blocking gate; no drift found.

---

## Gaps Summary

Phase 8 built a great deal and reported it unusually honestly. The engineering is
strong: a Shape funnel with a test-enforced source invariant, a geometry cache with a
single writer and a documented reason no one may bypass it, wraparound-safe timestamp
arithmetic, a four-rung font ladder where a `fatal()` used to be, tri-state rule
actions with the fold semantics reasoned out in the header, and a resource budget
derived from a separate calibration run precisely so the test cannot grade its own
measurement. The checklist and evidence README argue *against* the phase's own
interests in several places, which is what made this verification tractable.

Three requirements are correctly Pending, and each is a *split* truth — a substantial
half delivered, a named half absent:

1. **XDIS-05** — two of four remote targets exercised. The deviations are well-formed
   process decisions, not behavioural evidence, and D-8-X2GO says so itself.
2. **RULES-01** — WM_CLASS and window-type matching ship and work; WM_NAME title
   matching does not exist. Implement it rather than amend, because unlike XDIS-04 and
   RULES-02 there is no prior decision to reverse — and because the shipped docs
   already promise it.
3. **TEST-08** — transcripts and gates are real and good; the interaction-checklist
   *results* do not exist as results, and 2 of 4 targets lack transcripts.

**No requirement marked Complete is undelivered.** That is the most important finding
and it is clean.

**The most actionable defects are not in the ledger at all — they are in
`docs/RELEASE-NOTES.md`.** Two are blocker-grade because they describe controls that
look available and silently do nothing: `rule-match-name` documented as matching the
window title (it matches the WM_CLASS instance name — the very feature RULES-01 was
honestly held Pending for), and `rule-match-type` documented as accepting three values
the parser rejects, each of which silently disables the whole rule. A third omits a
shipped action; a fourth leaves the remote-target table stale and drops the X2Go
deviation. These are cheap to fix and worth fixing before anything else here, because
they are the one place in this phase where a document promises more than the code
delivers.

**Milestone note:** Phase 9 (Config GUI + IPC, CGUI-01..05) is the final phase and
covers none of XDIS-05, RULES-01 or TEST-08. None of these gaps is deferred — as the
roadmap stands, the milestone would close with three unmet requirements. That is a
roadmap decision to make deliberately, not one to discover at milestone audit.

---

_Verified: 2026-08-29T23:24:27Z at commit `aae69df`_
_Verifier: Claude (gsd-verifier) — goal-backward, FORCE stance_
