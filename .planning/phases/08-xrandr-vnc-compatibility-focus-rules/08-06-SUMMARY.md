---
phase: 08-xrandr-vnc-compatibility-focus-rules
plan: 06
subsystem: rendering
tags: [x11, xft, fontconfig, xrender, border, font-fallback, capability-detection, catch2, ctest, cmake, xvfb]

requires:
  - phase: 08-xrandr-vnc-compatibility-focus-rules
    plan: 01
    provides: WmFixture (its xvfbArgs hook is what makes a genuinely RENDER-less fixture possible), tests/lsan.supp, the preflight fixture
  - phase: 08-xrandr-vnc-compatibility-focus-rules
    plan: 02
    provides: scripts/gates/build-all.sh and scripts/analysis/run-static-analysis.sh, used as this plan's verification evidence
  - phase: 08-xrandr-vnc-compatibility-focus-rules
    plan: 03
    provides: tests/test_wm_fallbacks.cpp and the env-lever capability convention this plan copies for the two font levers
  - phase: 08-xrandr-vnc-compatibility-focus-rules
    plan: 05
    provides: the second worked example of the capability triple, and the pattern of splitting a claim by what the environment can actually demonstrate
provides:
  - Border::loadTabFont() -- a four-rung tab-font degradation ladder that cannot terminate the process
  - Border::TabFontRung + hasTabFont()/tabFontRotated() -- the rung sentinel and its predicates
  - WM2_FORCE_NO_ROTATED_TAB_FONT / WM2_FORCE_NO_TAB_FONT -- the levers that reach rungs 3 and 4
  - An XRender capability line in the WM startup transcript (evidence-only, nothing branches on it)
  - PkgConfig::XRENDER as a declared build dependency of the WM and the fallback test target
  - tests/test_wm_fallbacks.cpp [xft_norender_spike] and [wm_norender] groups
  - A retrying WmFixture readiness probe -- the cure for deferred item 10
affects: [08-07, 08-11, 08-12, 08-13, 08-14]

actuals:
  tokens: 13431
  tasks: 3
  commits: 3

tech-stack:
  added: [libxrender declared as a pkg-config dependency (previously present only transitively via libXft)]
  patterns:
    - "A degradation ladder whose top rung is silent: only departures from the healthy path are announced, so a transcript with no ladder line IS the evidence that nothing degraded"
    - "Explicit statics-initialised flag rather than inferring initialisation from a resource being non-null, once null becomes a legitimate outcome"
    - "Spike the unknown as a permanent tagged test case, so the answer is re-derivable by one ctest filter rather than by trusting a summary sentence"
    - "When a readiness probe can be lost to a race, retry with a FRESH probe rather than waiting longer on the lost one"

key-files:
  created: []
  modified:
    - src/Border.cpp
    - include/Border.h
    - src/Manager.cpp
    - CMakeLists.txt
    - tests/test_wm_fallbacks.cpp
    - tests/support/WmFixture.h
    - scripts/preflight.sh
    - .planning/REQUIREMENTS.md
    - .planning/phases/08-xrandr-vnc-compatibility-focus-rules/deferred-items.md

key-decisions:
  - "Open Question 1 is answered YES: a rotated FcMatrix Xft font loads, measures identically and draws without a protocol error on a server with RENDER disabled. libXft falls back to its core X11 glyph path, so the sideways-tab identity survives a RENDER-less remote server"
  - "The ladder is built anyway, because the spike narrows WHICH rung is taken, not whether a font failure may kill the WM -- a target with no usable font file is a case no amount of RENDER availability protects against"
  - "Rung 3 sizes the tab from the face's LINE HEIGHT, not from a multi-character sample: m_tabWidth is shared by every frame and by the tab's shape geometry, and a tab several times wider than normal breaks small windows outright (measured -- see deviation 1)"
  - "The XRender probe in the WM carries NO sentinel and nothing branches on it, because the spike proved there is no behaviour to degrade. It exists for the XDIS-05 evidence transcript and as the discriminator that keeps the RENDER-less tests from being false greens"
  - "Deferred item 10's XMaskEvent hypothesis is refuted with evidence and the failure re-attributed to item 9; the readiness probe now retries with a fresh window (5 failures in 144 startups -> 0 in 150)"
  - "Two levers rather than one valued lever, matching the WM2_FORCE_NO_<CAP>=1 shape 08-03 established"

patterns-established:
  - "A degraded-path test group carries BOTH a genuinely capability-less server (for what it can prove) and levers (for what no server option can produce), and says which is which at the point of assertion"
  - "Before calling a flake pre-existing, measure the same command on a worktree at the pre-plan commit and publish both rates"

requirements-completed: [XDIS-04]

coverage:
  - id: D1
    description: "The WM starts, manages windows and shuts down cleanly on a server with XRender disabled -- it never terminates because a font could not be loaded (XDIS-04, XDIS-05)"
    requirement: XDIS-04
    verification:
      - kind: integration
        ref: "tests/test_wm_fallbacks.cpp [wm_norender] -- on a server started with `-extension RENDER`: the WM reaches full readiness (WmFixture throws otherwise), a client is framed and published in _NET_CLIENT_LIST, the frame has non-zero dimensions, and SIGTERM exits 0 (explicitly not 1, not 42, not a signal)"
        status: pass
      - kind: other
        ref: "negative-tested: restoring the unrecoverable-exit call on rung 4 reddens the rung-4 case; the WM dies before framing anything"
        status: pass
    human_judgment: false
  - id: D2
    description: "The rotated tab-font load path degrades through an explicit ladder -- rotated preferred, rotated generic, unrotated, then no label -- instead of calling the fatal-exit path"
    requirement: XDIS-04
    verification:
      - kind: other
        ref: "grep: the font-load message that reached the exit path is gone (0); the five out-of-scope fatal sites survive (5); two rotated factory calls and three font factory calls total; five warning lines; zero core-font pattern strings"
        status: pass
      - kind: integration
        ref: "tests/test_wm_fallbacks.cpp -- rung 3 and rung 4 each exercised end-to-end through their lever, each asserting its own rung line AND the absence of the other two, plus the distinctly-worded forced line"
        status: pass
      - kind: other
        ref: "negative-tested: deleting rung 3 makes the rung-3 case red (the ladder falls through to rung 4's line)"
        status: pass
    human_judgment: false
  - id: D3
    description: "When the tab label cannot be rendered at all, frames are still drawn, windows are still manageable, and a single warning is emitted"
    requirement: XDIS-04
    verification:
      - kind: integration
        ref: "tests/test_wm_fallbacks.cpp -- the rung-4 case: client framed, frame non-zero, present in _NET_CLIENT_LIST, exactly the rung-4 line and neither of the other two, clean exit 0, no ASan report"
        status: pass
      - kind: other
        ref: "negative-tested: removing the three null-font guards on the draw path makes the case red at `frame != None` -- the WM dies before framing"
        status: pass
    human_judgment: false
  - id: D4
    description: "The normal path is unchanged when the rotated font loads (the sideways-tab identity is non-negotiable)"
    requirement: XDIS-04
    verification:
      - kind: other
        ref: "the rotated branch of the tab-width computation and the whole rotated half of drawLabel() are byte-identical to their pre-plan text; every new branch is entered only when tabFontRotated() is false"
        status: pass
      - kind: integration
        ref: "full debug suite 171/171 including the 6 [xft] rendering cases and the 5 [wm_noshape] framing cases; the [wm_norender] control case asserts no ladder rung is entered on the default server"
        status: pass
    human_judgment: true
    rationale: "Proven structurally (the rotated path is textually unchanged and every new branch is guarded by a predicate that is false on it) and functionally (every existing rendering and framing test passes), but NOT pixel-compared. Same standing as 08-03's D3; the before/after screenshot evidence is 08-14's job. Note deferred item 11: the rotated tab length was ALREADY wrong before this plan and is deliberately left wrong here."
  - id: D5
    description: "The fontconfig fallback chains the WM actually uses resolve to real font files, checked by preflight rather than asserted (D-13)"
    requirement: XDIS-04
    verification:
      - kind: other
        ref: "scripts/preflight.sh now checks all three patterns the WM requests, each required to name a readable file. Measured on this host: preferred -> NotoSans-SemiCondensed(.Bold).ttf, generic sans -> DejaVuSans-Bold.ttf"
        status: pass
    human_judgment: false
  - id: D6
    description: "XDIS-04's requirement text states the fontconfig-fallback reading rather than core-X-font revival (D-14)"
    requirement: XDIS-04
    verification:
      - kind: other
        ref: ".planning/REQUIREMENTS.md line 55 rewritten with the four rungs, the exclusion, and the recorded reason (Phase 4 removed core X fonts by decision). The old promise string is gone (0 occurrences); the traceability row and ID count are unchanged (2)"
        status: pass
    human_judgment: false
  - id: D7
    description: "The XRender capability fact is recorded in the evidence transcript for XDIS-05"
    requirement: XDIS-04
    verification:
      - kind: integration
        ref: "the WM prints an XRender availability line beside the Shape and RANDR ones; the [wm_norender] cases assert the absent-line on the disabled server and the present-line on the default one, each asserting the other is absent"
        status: pass
      - kind: other
        ref: "negative-tested: pointing the RENDER-less case at the default fixture server reddens it"
        status: pass
    human_judgment: false

duration: 71min
completed: 2026-08-11
status: complete
---

# Phase 08 Plan 06: XRender-less Font Degradation Summary

**A rotated Xft tab font turns out to survive a server with XRender disabled completely — measured, not assumed — and the tab-font load path now degrades through four announced rungs instead of taking the process down, so no font failure of any origin can leave a remote user with no window manager at all.**

## Performance

- **Duration:** 71 min
- **Started:** 2026-08-11T14:39Z
- **Completed:** 2026-08-11T15:50Z
- **Tasks:** 3
- **Files modified:** 9 (0 created, 9 modified)

## Open Question 1 (08-RESEARCH.md): the answer

> *Does the rotated Xft tab font load and render on a server without RENDER?*

**YES — completely, with no measurable difference.** Recorded permanently as the `[xft_norender_spike]` case, re-derivable with one ctest filter. The four facts, as the gate prints them:

```
[spike] fact 1  XRender present on this connection: NO
[spike] fact 2  rotated preferred-chain font loaded: YES
[spike] CONCLUSION: rotated fonts DO survive without XRender; libXft's core path renders the sideways tab.
[spike] fact 3  extents "M": w=12 h=13   "Hello": w=12 h=39
[spike] fact 4  X protocol errors during rotated draw: 0
```

The extents are byte-identical to the RENDER-present control, and the width/height asymmetry (`"Hello"` grows only on the height axis) proves the `FcMatrix` rotation was honoured rather than silently dropped. libXft falls back to its core X11 glyph path.

**What this means for the product:** the sideways-tab visual identity — the non-negotiable one — is *not* lost on a RENDER-less remote server. The planner's flagged assumption that such targets might get horizontal or absent labels is **retired**: they get the normal tab. That is a better outcome than the plan budgeted for, and 08-14's release notes should say so rather than warning about a degradation that does not occur.

**It does not make the ladder optional.** The spike narrows *which rung* a RENDER-less server takes; it says nothing about a target with no usable font file, which is a failure no amount of RENDER availability protects against. The exit path had to go regardless, and it did.

## Accomplishments

- **Removed the phase's largest open risk.** A failure to produce the rotated tab font used to reach the unrecoverable-initialisation exit. Four rungs now cover it: preferred rotated chain, generic rotated chain, unrotated face (labels read horizontally, truncated on a UTF-8 boundary), and no font at all (frames drawn, tabs blank). Rung 1 is silent; every other rung prints one line saying what degraded.
- **Proved the lower rungs rather than shipping them as a claim.** No server option can make fontconfig fail to resolve a font, so rungs 3 and 4 are reached with levers in the shape 08-03 established, and each case asserts its own rung line *and* the absence of the other two. Without those two cases the ladder's bottom half would have been untested code calling itself a fallback — which is the exact failure this plan exists to remove.
- **Found and fixed two real defects in the new code before they shipped** (both in Deviations): a tab-sizing choice that made the server answer `BadMatch` and abort framing on small windows, and the fact that `m_tabFont == nullptr` stops being a valid "not yet initialised" guard the moment a null font becomes legitimate.
- **Closed deferred item 10 with evidence instead of a hypothesis.** 08-05 recorded a rare startup wedge and named `XMaskEvent` as the suspect. This plan's new group reproduced it at a measurable rate (5 in 144 fixture startups), refuted the hypothesis on the code, tested and refuted a second one directly, re-attributed it to deferred item 9, and fixed the harness accordingly — 0 in 150 afterwards.
- **Quantified the suite-wide flakiness for the first time, and proved it pre-existing** by building the pre-plan commit in a throwaway worktree and running the same command on both: baseline 8 failing executions in 4 full runs, this tree 3. Recorded as deferred item 12 rather than left as a feeling.
- **Found a genuine appearance defect the phase had not noticed** — the sideways tab does not grow with the window title, because the two rotated-extent reads have their axes swapped. Measured on a live WM (54 px vs 58 px of tab for a 1-character vs 34-character title) and deferred, because this plan is explicitly forbidden from changing the healthy path's appearance.

## Task Commits

1. **Task 1: spike Open Question 1 — rotated Xft on a RENDER-less server** — `17dc853` (test)
2. **Task 2: replace the fatal font-load path with a degradation ladder** — `1760c01` (feat)
3. **Task 3: prove XDIS-04 on a RENDER-less server and amend the requirement** — `3f7c7fb` (test)

## Files Created/Modified

- `src/Border.cpp` — `loadTabFont()` and the ladder; rung-aware, null-safe `fixTabHeight()`, `drawLabel()` and the new `drawLabelHorizontal()`; the explicit statics guard and its teardown reset.
- `include/Border.h` — the `TabFontRung` enum, the rung and statics-guard members, and the two predicates consumers branch on.
- `src/Manager.cpp` — the XRender capability line, beside the Shape and RANDR ones.
- `CMakeLists.txt` — `xrender` as the sixth required pkg-config module, linked into the WM and into `test_wm_fallbacks` (with XFT and FONTCONFIG, which the spike also needs directly).
- `tests/test_wm_fallbacks.cpp` — 1 `[xft_norender_spike]` case and 6 `[wm_norender]` cases, plus the X-error trap and frame-size helpers.
- `tests/support/WmFixture.h` — the retrying readiness probe.
- `scripts/preflight.sh` — the generic sans chain the ladder's rung 2 requests.
- `.planning/REQUIREMENTS.md` — the D-14 XDIS-04 amendment.
- `deferred-items.md` — item 10 update (refutation + real cause), new items 11 and 12.

## Decisions Made

See `key-decisions` in the frontmatter. The two worth reading twice:

**The XRender probe drives nothing, on purpose.** Shape and RANDR each got a negative sentinel and a predicate because the WM genuinely has to behave differently without them. RENDER does not: the spike measured that the font path is unaffected, and the thing that can actually fail is *producing a font*, which the ladder keys on directly. A sentinel here would have been a capability triple with nothing on the other end — cargo-culting 08-03's pattern rather than applying it. What the probe *is* for is stated at the site: the capability fact belongs in the per-target evidence transcript for XDIS-05, and it is the discriminator that stops every `[wm_norender]` case from being a false green on a RENDER-capable server.

**Rung 3's tab is sized from the face's line height, and that costs legibility deliberately.** The obvious choice — size the tab from a four-character sample so a truncated title is readable — was implemented first and is what produced the `BadMatch` failure below. `m_tabWidth` is shared by every frame *and* by the tab's shape geometry; a tab three times wider than normal overflows small windows and inverts the `m_tabHeight >= m_tabWidth` relationship the shape code assumes. Legibility is bought back by truncating in `drawLabelHorizontal()` instead, which costs characters but cannot break geometry. On a display where rung 3 is reached, expect roughly two characters of title. That is a real limitation, stated rather than smoothed over — and the spike's answer means it should essentially never be reached in the field.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Rung 3's tab sizing made the server answer `BadMatch` and abort framing**
- **Found during:** Task 3, running `ctest --test-dir build/asan -L '^wm_norender$'`
- **Issue:** rung 3 originally sized `m_tabWidth` from a four-character sample (≈52 px against the normal 17). `fixTabHeight()` then floored `m_tabHeight` at 10. On a small window — the fixture's own 60x40 readiness probe — `maxHeight` goes negative, `m_tabHeight` lands on the floor, and `shapeTab()` builds rectangles of height `m_tabHeight - m_tabWidth + TAB_TOP_HEIGHT`. `XRectangle` fields are unsigned, so a negative height wraps to ~65535, the list stops being `YXSorted`, and the server answers `BadMatch`. Four such errors in the transcript and the client was never framed.
- **Fix:** two parts. `m_tabWidth` for rung 3 is now the face's line height (`ascent + descent + 4`), which keeps the tab in the same order of magnitude as the rotated path; and the degraded rungs floor `m_tabHeight` at `m_tabWidth` rather than at 10, which makes the subtraction non-negative by construction. Both carry the reason at the site.
- **Verification:** `[wm_norender]` green in debug and ASan; 0 sanitizer report files; the four `BadMatch` lines gone from the transcript.
- **Committed in:** `3f7c7fb`
- **Note:** the same latent hazard exists on the rotated path for windows under about 20 px tall (`m_tabHeight` is clamped to `maxHeight` with no floor). Untouched — pre-existing and out of this plan's scope.

**2. [Rule 1 - Bug] `m_tabFont == nullptr` is not a valid "statics not yet built" guard once a null font is legitimate**
- **Found during:** Task 2, writing rung 4
- **Issue:** the constructor's whole static-initialisation block was guarded by "is the tab font still null". Rung 4 leaves it null forever, so *every subsequent* `Border` would have re-run the block — re-allocating colours and creating a fresh GC per frame, leaking one GC per window.
- **Fix:** an explicit `m_staticsInitialised` flag, cleared in the refcounted teardown so a later `Border` correctly rebuilds the statics rather than running with a destroyed GC.
- **Verification:** rung-4 case maps and frames a client with no ASan report; clean exit 0.
- **Committed in:** `1760c01`

**3. [Rule 3 - Blocking] The readiness probe made this plan's new group flake at ~3.5%**
- **Found during:** Task 3, running the `[wm_norender]` gate repeatedly
- **Issue:** 5 failures in 144 fixture startups, all `event loop not pumping (probe window never reparented)` — the symptom 08-05 recorded as deferred item 10 with `XMaskEvent` as the suspect.
- **Investigation, rather than a longer timeout:** the `XMaskEvent` hypothesis is refuted on the code — root carries `PropertyChangeMask` and `setupEwmhProperties()` queues a dozen matching events *before* the wait begins, so the call returns immediately. A second hypothesis (the probe being adopted by `scanInitialWindows()` and therefore never map-requested) was tested directly by inserting a 700 ms delay ahead of the scan; the probe was still reparented and the case still passed with retries disabled. What remains matches deferred item 9 exactly — the WM has *issued* the reparent but its output is not on the wire, and the fixture is polling with nothing to wake it.
- **Fix:** `proveEventLoopLive()` retries with a **fresh** probe window (three attempts) instead of waiting longer on the lost one. Creating the second window is itself the unrelated event that wakes the WM — item 9's own documented workaround.
- **Verification:** 0 failures in 150 fixture startups of the same gate afterwards. Item 10 updated in `deferred-items.md` with the refutation, the re-attribution, and the limits of the evidence stated plainly.
- **Committed in:** `3f7c7fb`

**4. [Rule 2 - Missing critical functionality] The WM had no way to say whether XRender was there**
- **Found during:** Task 3, writing the RENDER-less end-to-end cases
- **Issue:** the plan's Task 3 asks the RENDER-less cases to prove they were really RENDER-less rather than passing for the ordinary reason. The spike's answer removes the obvious discriminator — no font rung is entered, so there is no degradation line to look for. Without a capability line from the WM itself, every `[wm_norender]` case would pass identically on a RENDER-capable server. The plan's own `key_links` also require the probe to reach the XDIS-05 evidence transcript.
- **Fix:** an XRender capability line in the WM startup transcript, in the Shape/RANDR block's exact style but with no sentinel, plus `PkgConfig::XRENDER` on the WM target. `src/Manager.cpp` and the WM link line are outside this plan's declared file list, which is why this is recorded here.
- **Verification:** negative-tested — pointing the RENDER-less case at the default fixture server reddens it; the control case reddens if pointed the other way.
- **Committed in:** `1760c01`

**5. [Rule 2 - Missing critical functionality] preflight did not check the ladder's generic rung**
- **Found during:** Task 3, checking the plan's D-13 truth
- **Issue:** the plan requires preflight to check "the same patterns the Border font loader requests". It checked the menu chain and the preferred bold chain, but not `sans-serif:bold:size=12`, which is rung 2 — the thing standing between a font-poor target and no labels at all.
- **Fix:** third pattern added to the existing loop, with the reason at the site. `scripts/preflight.sh` is outside the declared file list.
- **Verification:** `bash scripts/preflight.sh` exits 0 and names a readable file for all three.
- **Committed in:** `3f7c7fb`

### Plan-text corrections (no code impact)

**6. `<X11/extensions/render.h>` cannot be used for the probe**
The plan names that header. It is the protocol/type header and declares no entry points; the capability query lives in `<X11/extensions/Xrender.h>`, which includes it. Both the spike and the WM include the latter.

**7. Task 3's "13 tests passed (6 fallback/invariant cases, 6 pre-existing xft cases, 1 spike)" — right total, wrong arithmetic**
The gate reports **13** exactly as predicted, but the `[xft]` group is not in the label filter. The real composition is 6 `[wm_norender]` + 5 `[wm_noshape]` + 1 spike = 12 Catch2 cases, plus the `preflight` fixture setup test ctest pulls in automatically. Same +1 arithmetic 08-01, 08-03, 08-04 and 08-05 each recorded.

**8. Task 1's `grep -c 'XRenderQueryExtension'` initially counted 2**
The comment explaining the fixture named the entry point. Exactly the failure mode every prior plan in this phase hit; reworded to prose with a note at the site saying why.

---

**Total deviations:** 5 auto-fixed (2 defects in this plan's own new code, 1 harness blocker with a refuted hypothesis behind it, 2 gaps that would have made the plan's own truths unprovable), 3 plan-text corrections. No architectural decisions required.

## Issues Encountered

- **The full process-level suite flakes at roughly one test per run**, and this is **pre-existing**: measured on a throwaway worktree at the pre-plan commit (8 failing executions in 4 full release runs) against this tree (3 in 4 release runs, 1 in 3 debug runs). Every failing test passes on its own, repeatedly. Recorded as deferred item 12 with the numbers; suspected to be deferred item 9 under load. It means `build-all.sh` is not reliably green in one shot — the three-tree gate passed here on a re-run, and the individual gates this plan's criteria name are green consistently.
- **The sideways tab does not grow with the window title** (new deferred item 11). Measured: 54 px vs 58 px of tab for a 1-character vs 34-character title. The two rotated-extent reads have their axes swapped — for a rotated font the string runs along `height` and the thickness is `width`, and the code uses each where the other belongs. Deliberately not fixed: this plan forbids changing the healthy path's appearance, and the change needs before/after screenshots to land safely.
- **`cppcoreguidelines-init-variables` unchanged at 142**, `bugprone-branch-clone` at 6, cppcheck 18/18 accounted for. No new translation unit; no new reported-only diagnostics.
- **The `RenderBadPicture` X error from 08-01 is still present** in process-level runs and still non-fatal. This plan touched the XRender-adjacent code and did not make it better or worse; it is unrelated to the capability probe, which is a single query with no picture involved.
- **The Release-only `-Wunused-result` on `write()` in `sigHandler`** remains the single warning in `build/release/build.log` (deferred item 4).

## Known Stubs

None. Every rung of the ladder is reachable at runtime and is exercised by a passing assertion that has been shown to go red when the thing it asserts is removed. The two places where a claim is weaker than it looks are stated rather than hidden:

- **Rung 3's labels are roughly two characters wide.** That is a deliberate trade (see Decisions), not an unfinished implementation, and it is documented at the site.
- **The unchanged-appearance claim (D4) is structural and functional, not pixel-compared.** Same standing as 08-03's D3; 08-14 owns the screenshot evidence.

## Deferred Issues

- **11. The sideways tab does not grow with the window title** — new, measured, recommended for 08-14 or a follow-up rendering plan. Two-line change; the risk is in the review, not the edit.
- **12. The full process-level suite flakes at ~1 test per run** — new, quantified on both this tree and the pre-plan baseline. Recommended for 08-11 … 08-13 together with items 9 and 10.
- **10. Rare WM startup hang** — updated, not closed: its cause is now identified as item 9 and the harness no longer flakes on it, but the underlying defect is untouched.
- Items 1–9 unchanged. Item 6 (`circulate()` spins) was respected: no test here drives the root menu.

## Threat Flags

None. No new network endpoint, auth path, file-access pattern or trust-boundary schema change.

The plan's own register is addressed:

- **T-8-FONT (mitigate)** — the four-rung ladder replaces the exit path, and the RENDER-less end-to-end case asserts exit status 0 explicitly against 1, 42 and signal death. Negative-tested: restoring the exit call reddens it.
- **T-8-RENDER (mitigate)** — the in-process capability probe plus a real RENDER-disabled server; frames render with non-zero dimensions even in the rung where labels cannot.
- **T-8-FCFILE (accept)** — unchanged. Read-only consumption of system font paths through the standard library; preflight verifies the three chains resolve to readable files rather than validating their contents.
- **T-8-SC (accept)** — `libxrender` 1.3.9 from the Ubuntu 22.04 archive, already installed and already linked transitively via libXft; declaring it adds no new ecosystem dependency. Release link audit: 24 entries, all intended, no test-only library.

## Verification Results

| Plan verification step | Result |
|---|---|
| `ctest -L '^(wm_norender\|wm_noshape\|xft_norender_spike)$' --no-tests=error` | PASS (13/13 — 12 cases + preflight) |
| `ctest --test-dir build/debug --no-tests=error` (full suite) | PASS (171/171) |
| `ctest --test-dir build/asan -L '^wm_norender$'` | PASS (7/7, **0** sanitizer report files) |
| `bash scripts/analysis/run-static-analysis.sh` | PASS (exit 0; cppcheck 18/18, 0 fatal clang-tidy) |
| `bash scripts/gates/build-all.sh debug` | PASS (0 warnings in build.log) |
| `bash scripts/gates/build-all.sh` (all three trees) | PASS on re-run; see deferred item 12 for the one-shot flakiness and its measured baseline |
| `bash scripts/preflight.sh` | PASS (all three fontconfig chains resolve to readable files) |
| Task 1 grep criteria (spike tag ≥1, capability query =1, extents ≥1) | PASS (after rewording a comment — correction 8) |
| Task 2 grep criteria (exit-path message 0, `fatal(` 5, rotated factory 2, factory total 3, warnings 5, core-font strings 0) | PASS |
| Task 3 grep criteria (`wm_norender` 9, old promise string 0, `XDIS-04` 2, `fontconfig` 4, `Phase 4` 6) | PASS |
| Every `XftTextExtentsUtf8` site dominated by a non-null-font guard | PASS (8 sites; 3 guard points, one per function) |
| Release link audit | PASS (24 entries, libXrender present, no test-only library) |
| Negative: restore the exit path on rung 4 | PASS (rung-4 case red — WM dies before framing) |
| Negative: delete rung 3 | PASS (rung-3 case red — ladder falls to rung 4's line) |
| Negative: remove the three null-font guards on the draw path | PASS (rung-4 case red at `frame != None`) |
| Negative: point the RENDER-less case at the default fixture server | PASS (case red) |
| Probe: rotated font + extents + pixmap draw with RENDER disabled | CONFIRMED (identical to the RENDER-present control, 0 protocol errors) |
| Probe: `-extension RENDER` after `+render` still disables it | CONFIRMED (the fixture's argument order is sound) |
| Probe: `XMaskEvent` cannot be the item-10 hang | CONFIRMED on the code and by direct test of the alternative |
| Probe: readiness-probe retry rate before/after | CONFIRMED (5/144 → 0/150) |
| Probe: full-suite flake rate, this tree vs pre-plan baseline worktree | CONFIRMED (3/4 runs vs 8/4 runs — pre-existing) |
| Probe: tab length vs title length on a live WM | CONFIRMED (54 px vs 58 px — deferred item 11) |

## Self-Check: PASSED

All three commit hashes (`17dc853`, `1760c01`, `3f7c7fb`) resolve in `git log`. Every modified file exists on disk with the claimed content. `src/`, `include/`, `tests/`, `scripts/` and `CMakeLists.txt` are byte-identical to their committed state after the six negative-test experiments (`git diff` empty for those paths); the baseline comparison worktree was removed and `git worktree list` shows only the main checkout.

## Next Phase Readiness

- **XDIS-04 is closed and XDIS-05 is materially de-risked.** The one measured unknown behind both — whether a RENDER-less remote server costs the rotated tab — is answered, and the answer is favourable. 08-14's release notes should say the sideways tab survives RENDER-less servers, not warn about a degradation that does not happen.
- **The capability transcript is now complete for the XDIS-05 matrix.** A single WM startup on any target prints Shape, RANDR and XRender availability in one place, which is exactly the three lines 08-RESEARCH's Example 5 asks each target to yield. The capture script no longer has to infer them from `xdpyinfo`.
- **`test_wm_fallbacks` carries three tag groups** and takes both a lever set and an Xvfb argument list through `WmFixture` with no build plumbing. A fourth group needs cases and a tag, nothing more.
- **The harness is measurably more reliable** than it was at the start of this plan, and the residual flakiness is quantified with a baseline rather than suspected. 08-11 … 08-13 inherit items 9, 10 and 12 as one defect with three write-ups and a stated likely fix.
- **Carried constraint, still live:** deferred item 6 (`circulate()` spins forever with no Normal client) is untouched and remains a 100%-CPU freeze reachable by right-clicking the root of a freshly started WM. 08-07 is the next scheduled visit to `src/Buttons.cpp`.
- **New for 08-14:** `libxrender` is now a declared build dependency (it was already linked transitively), and deferred item 11 — the tab that does not grow with its title — is a visible appearance defect worth fixing while the screenshot evidence is being captured anyway.

---
*Phase: 08-xrandr-vnc-compatibility-focus-rules*
*Completed: 2026-08-11*
