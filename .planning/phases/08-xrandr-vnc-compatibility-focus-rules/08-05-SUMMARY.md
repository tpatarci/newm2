---
phase: 08-xrandr-vnc-compatibility-focus-rules
plan: 05
subsystem: window-management
tags: [x11, xrandr, randr, geometry, resolution-change, fallback, capability-detection, ewmh, catch2, ctest, cmake, xvfb]

requires:
  - phase: 08-xrandr-vnc-compatibility-focus-rules
    plan: 01
    provides: WmFixture (its xvfbArgs and childEnv hooks are what make both degraded fixtures possible), tests/lsan.supp, the preflight fixture
  - phase: 08-xrandr-vnc-compatibility-focus-rules
    plan: 02
    provides: scripts/gates/build-all.sh and scripts/analysis/run-static-analysis.sh, used as this plan's verification evidence
  - phase: 08-xrandr-vnc-compatibility-focus-rules
    plan: 03
    provides: the capability triple (negative sentinel + predicate + single funnel) and the env-lever convention this plan copies verbatim for RANDR
  - phase: 08-xrandr-vnc-compatibility-focus-rules
    plan: 04
    provides: screenWidth()/screenHeight() over m_lastKnownScreenW/H -- the single cache this plan refreshes, which is why one write makes all 16 former call sites correct at once
provides:
  - WindowManager::handleScreenGeometryChange() -- the single settling point for a resolution change, shared by both entry paths
  - WindowManager::hasRandrExtension() / m_randrEventBase -- the RANDR capability triple
  - WM2_FORCE_NO_RANDR -- the lever that forces the RANDR path inert on a RANDR-capable server
  - StructureNotifyMask on the root window -- the no-RANDR fallback's only event source
  - PkgConfig::XRANDR as a required build dependency of the WM target
  - tests/test_wm_geometry.cpp [wm_norandr] group -- the phase's home for XDIS-02 coverage
affects: [08-06, 08-07, 08-14]

actuals:
  tokens: 14024
  tasks: 3
  commits: 5

tech-stack:
  added: [libXrandr linked into the shipped WM binary (link audit 23 -> 24 entries)]
  patterns:
    - "One settling point, two entry paths: a capability-specific event and a core-protocol fallback converge on a single idempotent handler"
    - "Re-read state from the server rather than from the event that announced it -- events carry stale values and arrive more than once"
    - "A degraded-capability claim is split by what the environment can actually demonstrate: a real extension-less server for the claims it can prove, a lever for the one it structurally cannot"
    - "Every guard is negative-tested by deleting the guard, not by reading it"

key-files:
  created: []
  modified:
    - CMakeLists.txt
    - include/Manager.h
    - src/Manager.cpp
    - src/Events.cpp
    - tests/test_wm_geometry.cpp
    - tests/lsan.supp
    - .planning/phases/08-xrandr-vnc-compatibility-focus-rules/deferred-items.md

key-decisions:
  - "XRRSelectInput moved out of the constructor's capability block into initialiseScreen(), because m_root is still None where the plan placed it and a select-input against None would exit(1) through errorHandler during initialisation"
  - "The handler re-reads root geometry with XGetWindowAttributes rather than re-reading screenWidth()/screenHeight() as the RESEARCH example does -- since 08-04 those accessors return this manager's own cache, so asking them after a resize returns the value the handler is about to replace"
  - "The idempotence case was rewritten after measurement: re-running xrandr at an unchanged geometry emits NO events at all, so the obvious 'resize twice' test passed with the coalescing guard deleted. It now synthesises the stale-dimension redelivery the real server sends"
  - "The fullscreen case asserts the FRAME's position, not just the client's: the client is held still by deferred item 8 regardless of the guard, so only the frame assertion actually pins the guard"
  - "XDIS-02's reflow claim is proven with WM2_FORCE_NO_RANDR on a RANDR-capable server, because on a genuinely RANDR-less server no resolution change can be produced at all -- the only resize mechanism in X is the missing extension"
  - "The 32-byte libXrandr leak is suppressed rather than worked around, on the evidence of a standalone twelve-line reproducer with no wm2 code linked"

patterns-established:
  - "When a helper waits for a window to settle, key the wait on a value that must CHANGE; waiting on a value that is already satisfied is a race, not a wait"
  - "Before asserting 'X did not happen', wait for an observable acknowledgement that the triggering work ran -- otherwise the case passes by arriving early"

requirements-completed: [XDIS-01, XDIS-02]

coverage:
  - id: D1
    description: "After the X screen is resized, screenWidth()/screenHeight() return the new dimensions, because the handler refreshes the manager-owned cache from a live root geometry query before re-reading (D-27, XDIS-01)"
    requirement: XDIS-01
    verification:
      - kind: integration
        ref: "tests/test_wm_geometry.cpp -- 'A resolution change updates _NET_WORKAREA to the new screen rectangle': 1280x1024 -> 1024x768 makes the published workarea read 0,0,1024,768. _NET_WORKAREA is computed from the accessors, so it is a direct read-out of the refreshed cache"
        status: pass
      - kind: other
        ref: "negative-tested: removing the updateWorkarea() call reddens 4 cases; taking the dimensions off the event instead of the server reddens the stale-redelivery case"
        status: pass
    human_judgment: false
  - id: D2
    description: "Windows left fully or partly offscreen by a shrink are moved back into view; windows still fully visible are left exactly where the user put them; windows are never resized by the reflow (D-25)"
    requirement: XDIS-01
    verification:
      - kind: integration
        ref: "tests/test_wm_geometry.cpp -- 'A window left offscreen by a shrink is moved back into view, not resized' (925,708 240x180 -> 783,587 240x180, exactly newBound-1-size on both axes) and 'A window that still fits after a shrink is not moved' (145,98 unchanged)"
        status: pass
      - kind: other
        ref: "negative-tested both directions: deleting the ensureVisible() loop reddens the move case; making the clamp unconditional (moving windows that already fit) reddens the not-moved case"
        status: pass
    human_judgment: false
  - id: D3
    description: "_NET_WORKAREA is recomputed to the new screen rectangle after a resolution change"
    requirement: XDIS-01
    verification:
      - kind: integration
        ref: "asserted in 4 of the 5 new [wm_geometry] cases and in the [wm_norandr] reflow case; removing the updateWorkarea() call reddens all of them"
        status: pass
    human_judgment: false
  - id: D4
    description: "The change handler is idempotent: the two events that arrive per logical resize, and any intermediate event carrying stale dimensions, together produce one settled result rather than a flapping one"
    requirement: XDIS-01
    verification:
      - kind: integration
        ref: "tests/test_wm_geometry.cpp -- 'Redelivered screen-change notifications carrying stale dimensions change nothing': five root ConfigureNotify events claiming 1280x1024 while the server is at 1024x768 move neither a deliberately-parked offscreen window nor the workarea"
        status: pass
      - kind: other
        ref: "negative-tested twice over: deleting the coalescing guard reddens it (the parked window gets dragged back), and reading the geometry off the event reddens it (the workarea reverts to 1280x1024)"
        status: pass
      - kind: other
        ref: "the real four-event burst is confirmed present by a compiled probe on this host: RRScreenChangeNotify 1280x1024 / root ConfigureNotify 1280x1024 / RRScreenChangeNotify 1024x768 / root ConfigureNotify 1024x768"
        status: pass
    human_judgment: false
  - id: D5
    description: "On a server without the RANDR extension the WM starts, warns once, manages windows normally, and still reflows on a resolution change -- because the root window's own configure event is selected and handled (XDIS-02)"
    requirement: XDIS-02
    verification:
      - kind: integration
        ref: "tests/test_wm_geometry.cpp [wm_norandr] -- on a server started with `-extension RANDR`: the WM reaches full readiness and logs the genuine-absence warning with no fatal line; a client is framed at exactly 300,220 and published in _NET_CLIENT_LIST; SIGTERM exits 0"
        status: pass
      - kind: integration
        ref: "the reflow half: 'With RANDR inert the root-ConfigureNotify fallback still reflows' -- same outcome as the RANDR case (workarea follows the screen down, offscreen window moved to 783,587, size untouched), with the transcript first asserting the forced-off line so the RANDR path is provably inert"
        status: pass
      - kind: other
        ref: "negative-tested: deleting the root-ConfigureNotify branch reddens it; removing StructureNotifyMask from the root mask reddens it. Control case confirms the default fixture takes the other path, and goes red if pointed at the RANDR-less server"
        status: partial
    human_judgment: true
    rationale: "The reflow half is proven with the WM's RANDR path forced inert on a RANDR-capable server, NOT on the extension-less server, because on the latter a resolution change cannot be produced at all -- the only mechanism for resizing an X screen is RANDR's own RRSetScreenSize (measured: `xrandr --fb` there answers 'RandR extension missing' and the screen stays 1280x1024). The forced configuration is faithful (with the sentinel below zero the WM never calls XRRSelectInput, so no screen-change notification is selected and the RANDR arm cannot match), but it is a stand-in, and the three claims that CAN be made on a real extension-less server are made there. A human should confirm this split is acceptable; the real-session evidence in 08-14 is where TigerVNC/XRDP/X2Go turn it into field fact."
  - id: D6
    description: "libxrandr is a required pkg-config build dependency, while RANDR availability is a runtime check that degrades to a warning, never a fatal error (D-26)"
    requirement: XDIS-02
    verification:
      - kind: other
        ref: "pkg_check_modules(XRANDR REQUIRED IMPORTED_TARGET xrandr) + PkgConfig::XRANDR on the WM target; ldd build/release/wm2-born-again shows libXrandr, link audit 24 entries all intended, no test-only library; `grep -c fatal src/Manager.cpp` unchanged at 9"
        status: pass
      - kind: other
        ref: "all three runtime paths exercised as foreground runs: RANDR present -> '  Xrandr extension available.'; WM2_FORCE_NO_RANDR=1 -> the forced-off line; `-extension RANDR` -> the genuine-absence line. The WM survives all three"
        status: pass
    human_judgment: false
  - id: D7
    description: "A fullscreen client is not repositioned by the reflow"
    requirement: XDIS-01
    verification:
      - kind: integration
        ref: "tests/test_wm_geometry.cpp -- 'A fullscreen client is not repositioned by the resolution-change reflow'. Asserts BOTH the client rectangle and the frame rectangle are unchanged"
        status: pass
      - kind: other
        ref: "negative-tested: deleting the fullscreen guard from ensureVisible() reddens it. Measured -- with the guard the frame stays at 900,700; without it the reflow clamps it to 758,579"
        status: pass
    human_judgment: true
    rationale: "Upgrades 08-04's D5, which was recorded as `partial` because circulate() could not reach a non-Normal client. It is now a real assertion, but read the split: only the FRAME assertion pins the guard. The CLIENT assertion is true and overdetermined -- deferred item 8 leaves a fullscreen client reparented to root and detached from its frame, so ensureVisible()'s move cannot reach the client whether the guard exists or not. The guard is therefore proven to suppress the reposition the WM would otherwise issue, which is the behaviour that matters once item 8 is fixed."

duration: 78min
completed: 2026-08-11
status: complete
---

# Phase 08 Plan 05: RANDR Wiring and the Resolution-Change Reflow Summary

**The WM now notices when the desktop is resized — through RANDR where it exists and through root's own ConfigureNotify where it does not — and both paths converge on one handler that re-reads the geometry from the server, coalesces the four events the server actually sends into a single reflow, moves only the windows the new screen left behind, and republishes the workarea.**

## Performance

- **Duration:** 78 min
- **Started:** 2026-08-11T14:24Z
- **Completed:** 2026-08-11T15:42Z
- **Tasks:** 3
- **Files modified:** 7 (0 created, 7 modified)

## Accomplishments

- **Wired RANDR into a tree that had none.** Before this plan `grep -r XRR src/ include/` returned nothing. There is now a capability query, an event subscription, a dispatch arm, the mandatory Xlib cache update, and an env lever — all following 08-03's stated triple rather than a new shape.
- **Made the no-RANDR fallback real rather than nominal.** XDIS-02 previously had no event to hook at all: `SubstructureNotifyMask` covers root's *children*, and a resolution change arrives as a ConfigureNotify on root *itself*. One mask bit and one branch, and the fallback is proven to carry the whole reflow on its own.
- **Built the handler around what the server actually does, not what the API suggests.** A compiled probe confirmed the documented hazard on this host: four events per logical resize, the first two carrying the *pre*-resize dimensions. The handler never reads an event's dimension fields and returns early on an unchanged geometry, so duplicates, stale intermediates and redelivery all settle to one result.
- **Caught two hollow tests before they shipped**, both by negative-testing rather than by reading the code (see Deviations). One would have passed with the coalescing guard deleted; the other with the fullscreen guard deleted. Both are now red exactly when their guard is absent.
- **Closed a coverage gap 08-04 had to leave open.** The fullscreen clamp guard was recorded there as `partial` because `circulate()` could not reach a non-Normal client without wedging the WM. The reflow calls `ensureVisible()` directly, so the guard is on a live path and is now negative-tested.
- **Traced a 32-byte ASan leak to a libXrandr defect with a standalone reproducer** instead of suppressing it on a plausible-looking stack trace.
- **Found and fixed a race in this plan's own test helper** that had a measured ~1-in-10 failure rate, and recorded a rare pre-existing WM startup hang with the evidence for and against it being pre-existing stated rather than assumed.

## Task Commits

1. **Task 1: RANDR build dependency, capability query and event selection (D-24, D-26)** — `8a0f024` (feat)
2. **Task 2 RED: failing tests for the resolution-change reflow** — `37f0f56` (test)
3. **Task 2 GREEN: idempotent handler and the D-25 reflow** — `556682c` (feat)
4. **Task 3: prove XDIS-02 against a genuinely RANDR-less X server** — `78f5f55` (test)
5. **Follow-up: remove a race in the test helper that placed a client** — `5ab2c91` (fix)

## Files Created/Modified

- `src/Manager.cpp` — the RANDR capability block beside Shape's, the `XRRSelectInput` subscription in `initialiseScreen()`, `StructureNotifyMask` on the root mask, and `handleScreenGeometryChange()`.
- `src/Events.cpp` — `ConfigureNotify` split out of the no-op group for root, and the RANDR arm ahead of the Shape comparison in `default:`.
- `include/Manager.h` — `hasRandrExtension()`, `m_randrEventBase`, and the handler declaration.
- `CMakeLists.txt` — `xrandr` as the fifth required pkg-config module, linked into the WM target.
- `tests/test_wm_geometry.cpp` — 5 new `[wm_geometry]` reflow cases, a `[wm_geometry]` control, 4 `[wm_norandr]` cases, and the resize/placement helpers.
- `tests/lsan.supp` — the libXrandr entry, with its reproducer recorded inline.
- `deferred-items.md` — item 10.

## Decisions Made

See `key-decisions` in the frontmatter. The three worth reading twice:

**The handler asks the server, not the accessors — which is the opposite of what the research example says.** `08-RESEARCH.md`'s Code Example 1 refreshes the cache by calling `XRRUpdateConfiguration(&ev)` and then re-reading `screenWidth()`/`screenHeight()`. That was written before 08-04 existed. Since 08-04 those accessors return *this manager's own cache*, so re-reading them after a resize returns exactly the value the handler is about to overwrite — the refresh would be a no-op and every reflow would run against the old screen. `XGetWindowAttributes` on root is one round trip and is correct on both paths, including the one where there is no RANDR event to hand to the updater in the first place. `XRRUpdateConfiguration` is still called on the RANDR path, but for Xlib's benefit rather than the WM's.

**XDIS-02's reflow claim is proven with the lever, and that is a finding rather than a shortcut.** The plan asked for the reflow to be demonstrated on a genuinely RANDR-less server. It cannot be, and the reason is structural: the only mechanism for changing an X screen's size is RANDR's own `RRSetScreenSize`. On a server started with `-extension RANDR`, `xrandr --fb 1024x768` answers `RandR extension missing` and the dimensions stay at 1280x1024 (measured). A resolution change literally cannot be produced there. The three claims that *can* be made on a real extension-less server — starts and warns, manages windows, exits cleanly — are made there; the fourth is made with `WM2_FORCE_NO_RANDR=1` on a server that can resize, where the WM never calls `XRRSelectInput` and root's ConfigureNotify is provably the only thing that can reach the handler.

**The fullscreen case asserts the frame, and says why the client assertion is not enough.** Asserting only that the client did not move would have been true and worthless: deferred item 8 leaves a fullscreen client reparented to root, so `ensureVisible()`'s move — which acts on the frame — cannot reach it either way. Measured: with the guard the frame stays at 900,700; without it the reflow clamps it to 758,579. The frame assertion is the one that goes red when the guard is deleted.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] `XRRSelectInput` could not go where the plan put it**
- **Found during:** Task 1
- **Issue:** The plan (following the research example) places the whole RANDR block "immediately after the Shape query block", including the event subscription. At that point in the constructor `m_root` is still `None` — `initialiseScreen()` is what establishes it, and it runs *after*. A select-input against `None` raises `BadWindow`, and `errorHandler()` calls `exit(1)` on any error while `m_initialising` (`src/Manager.cpp:320-323`). The WM would have died at startup on every RANDR-capable server.
- **Fix:** the query, the env lever, the sentinel and both log lines stay exactly where the plan asked; only `XRRSelectInput` moves into `initialiseScreen()`, immediately after the root window exists, guarded by `hasRandrExtension()`. Both sites carry a comment explaining the split. `grep -c XRRSelectInput src/Manager.cpp` is still 1.
- **Verification:** foreground runs on all three server configurations start cleanly and log the expected line.
- **Committed in:** `8a0f024`

**2. [Rule 1 - Bug] The idempotence test was hollow: replaying a resize emits no events at all**
- **Found during:** Task 2, negative-testing
- **Issue:** The case was written as the plan describes — park a window offscreen, re-run the same resize, assert nothing moved. Deleting the coalescing guard left it **green**. Investigated with a compiled listening probe rather than by guessing: the first `xrandr --fb 1024x768` delivers four events, and an identical second invocation delivers **zero** — `RRSetScreenSize` at the current size is a server-side no-op. The WM was never asked anything, so the guard was never consulted.
- **Fix:** the case now synthesises the hazard the server really produces — five root `ConfigureNotify` events claiming the stale 1280x1024 while the server is genuinely at 1024x768. That pins both halves of the truth in one case: the parked window is not dragged back (the guard returned early) and the workarea does not revert (the geometry was re-read, not taken from the event). Legitimate on this path for the same reason 08-04's `triggerClamp()` gave: `loop()` applies no `send_event` guard to `ConfigureNotify`.
- **Verification:** red when the guard is deleted; red when the dimensions are read off the event; green otherwise.
- **Committed in:** `556682c`

**3. [Rule 1 - Bug] The fullscreen case was hollow: the client is held still by a different defect**
- **Found during:** Task 2, negative-testing
- **Issue:** Deleting the fullscreen guard from `ensureVisible()` left the case green. `Border::stripForFullscreen()` reparents the child to root, so the move `ensureVisible()` issues goes to a frame the client is no longer inside — the client stays put with or without the guard.
- **Fix:** the client is parked at 900,700 before the transition so the clamp arithmetic would genuinely fire, and the case now also asserts the **frame** did not move. Measured: guard present → frame at 900,700; guard deleted → frame at 758,579.
- **Verification:** red when the guard is deleted.
- **Committed in:** `556682c`

**4. [Rule 3 - Blocking] A 32-byte libXrandr leak failed the ASan gate**
- **Found during:** Task 3, running `ctest --test-dir build/asan -L '^wm_norandr$'`
- **Issue:** the clean-shutdown case exited with the ASan sentinel 42 and three reports appeared, each `Direct leak of 32 byte(s)` allocated inside `libXrandr.so.2` via `XRRQueryExtension`. Only on the RANDR-*less* server; the RANDR-capable runs were clean.
- **Fix:** verified as a library defect before suppressing it, with a twelve-line program that opens a display, calls `XRRQueryExtension` once and calls `XCloseDisplay`, with no wm2 code linked — RANDR present: clean; RANDR absent: the same 32 bytes. libXrandr allocates per-display bookkeeping on its negative path and never registers the close hook that would free it. Added `leak:libXrandr.so` to `tests/lsan.supp` with the reproducer recorded in the file.
- **Verification:** `[wm_norandr]` and `[wm_geometry]` both green under ASan with **0** report files; `build-all.sh asan` reports no sanitizer findings.
- **Committed in:** `78f5f55`

**5. [Rule 1 - Bug] A race in this plan's own `placeClient()` test helper**
- **Found during:** final verification, at a measured rate of about one run in ten
- **Issue:** the helper waited for the client rectangle to report the requested size and then hold still for two reads. Every call site moves a window *without* resizing it, so the size condition was already satisfied when the move was issued; if both reads landed before the WM processed the `ConfigureRequest`, the helper reported the window settled at its **old** position and the caller derived its expectations from the wrong rectangle. Observed as `565 > 1023` — a window still at x=325 treated as though it had been parked at 900.
- **Fix:** keyed on the **frame** reaching the requested coordinates, a value that must change and whose settled position is exact (`eventConfigureRequest()` hands the requested position straight to `Border::configure()`).
- **Verification:** 10 consecutive green runs of the combined gate afterwards; every negative test re-run and still discriminating.
- **Committed in:** `5ab2c91`

### Plan-text corrections (no code impact)

**6. The gates report one more test than the criteria predict**
`-L '^(wm_geometry|wm_norandr)$'` reports 16, not 15: 15 Catch2 cases plus the `preflight` fixture setup test ctest pulls in automatically. `-L '^wm_geometry$'` reports 12 for 11 cases, `-L '^wm_norandr$'` reports 5 for 4. Same arithmetic 08-01, 08-03 and 08-04 each recorded.

**7. `grep -c 'wm_geometry ... 10 tests passed'` — the geometry group is 11 cases, not 10**
The plan's Task 2 criterion predicted 5 old + 5 new = 10. Task 3 then adds the RANDR-availability control to the same group, so the settled count is 11 cases (12 reported).

**8. Several grep-count criteria initially failed because the comments explaining the code named the tokens**
Exactly the failure mode 08-01, 08-02 and 08-03 each hit and documented. `m_randrEventBase` counted 3 in the header, `XRRSelectInput` and `WM2_FORCE_NO_RANDR` counted 2 in `src/Manager.cpp`, `XRRUpdateConfiguration` counted 2 in `src/Events.cpp`, and `fatal` went 9 → 10 because a comment said "never a fatal error". All reworded to describe the calls in prose rather than by name, with a note at each site saying why. Every criterion now holds exactly.

---

**Total deviations:** 5 auto-fixed (1 startup-fatal ordering bug, 2 hollow tests, 1 sanitizer gate blocker, 1 test-helper race), 3 plan-text corrections. No architectural decisions required.

## Issues Encountered

- **A rare pre-existing WM startup hang** — observed once, as two fixtures failing in one run with `event loop not pumping`. The WM is alive with its full banner printed and `_NET_SUPPORTING_WM_CHECK` published, but wedged before `loop()`. Most likely `timestamp()`'s unbounded `XMaskEvent(PropertyChangeMask)` (`src/Manager.cpp:723-737`), which is called from the constructor in exactly that interval. Not reproduced in 12 further runs (6 on this tree, 6 on the pre-plan baseline at equivalent fixture load), so the samples cannot separate the trees — but the hanging code path is untouched by this plan. Recorded as deferred item 10 with that evidence stated in full, including its limits.
- **`cppcoreguidelines-init-variables` unchanged at 142**, `bugprone-branch-clone` at 6. No new reported-only diagnostics; no new translation unit was added.
- **The `RenderBadPicture` X error from 08-01 is still present** in process-level runs and still non-fatal.
- **The Release-only `-Wunused-result` on `write()` in `sigHandler`** remains the single warning line in `build/release/build.log` (deferred item 4).

## Known Stubs

None. Every artifact this plan claims exists, is reachable at runtime, and is exercised by a passing assertion that has been shown to go red when the thing it asserts is removed. The two places where a claim is weaker than it looks — the client half of the fullscreen case, and the fact that the fallback reflow is driven with the lever rather than on the extension-less server — are stated as such in the test file, in the coverage table above, and in Decisions. They are qualified assertions, not stubs.

## Deferred Issues

**10. Rare WM startup hang before the event loop begins** — new, recorded in `deferred-items.md` with the full diagnosis, the suspected cause, and the measured rates on both trees. Recommended for 08-11 … 08-13 alongside item 9, which is also an event-loop timing defect. One backtrace from a hung child would settle it.

Items 1–9 are unchanged. Items 6 and 8 are directly relevant to what this plan could assert:
- **6 (`circulate()` spins forever)** — not triggered anywhere in this plan. The reflow reaches `ensureVisible()` directly, which is exactly why the fullscreen guard became testable here. The blocker carried forward from 08-04 was respected: no test drives the clamp through `circulate()` on a non-Normal client.
- **8 (EWMH fullscreen loses its position)** — still present, and it is what makes the client half of the fullscreen assertion overdetermined. The case is written so that fixing item 8 will not break it.

## Threat Flags

None. No new network endpoint, auth path, file-access pattern or trust-boundary schema change.

The plan's own register is addressed:
- **T-8-GEO (mitigate)** — the coalescing guard is the mitigation and it is negative-tested: a burst of notifications at one geometry produces at most one reflow pass, proven by the parked window surviving five redelivered events.
- **T-8-STRUT (mitigate)** — `updateWorkarea()`'s existing clamp now runs against the refreshed geometry rather than a stale cache, so an oversized strut cannot outlive a shrink. The recomputation is asserted in five cases and reddens four of them when removed. Note the mitigation is structural: no dock-with-strut case was written, so the strut arithmetic itself is covered only by 08-04's existing workarea cases.
- **T-8-RANDR (mitigate)** — the negative sentinel, the warn-and-continue path and the root-configure fallback are all in place, and three of the four claims are proven against a real RANDR-less server. See coverage D5 for what the fourth rests on.
- **T-8-SC (accept)** — `libxrandr` 1.5.2 from the Ubuntu 22.04 archive, already installed, X.Org upstream. No new ecosystem dependency.

## Verification Results

| Plan verification step | Result |
|---|---|
| `ctest -L '^(wm_geometry\|wm_norandr)$' --no-tests=error` | PASS (16/16 — 15 cases + preflight) |
| Stable across three consecutive runs | PASS (and 10/10 after the helper race was fixed) |
| `ctest --test-dir build/debug --no-tests=error` (full suite) | PASS (164/164) |
| `ldd build/release/wm2-born-again` shows libXrandr | PASS (1 match; link audit 24 entries, no test-only library) |
| `bash scripts/analysis/run-static-analysis.sh` | PASS (exit 0; cppcheck 18/18 accounted for, 0 fatal clang-tidy) |
| `bash scripts/gates/build-all.sh` | PASS (debug + release + asan; asan reports no sanitizer findings) |
| `ctest --test-dir build/asan -L '^wm_geometry$'` | PASS (12/12, **0** sanitizer report files) |
| `ctest --test-dir build/asan -L '^wm_norandr$'` | PASS (5/5, **0** sanitizer report files) |
| Task 1 grep criteria (9 counts incl. `fatal` unchanged at 9) | PASS (after rewording comments — see correction 8) |
| Task 2 grep criteria (8 counts incl. 0 event-field reads in the handler) | PASS |
| Task 3 grep criteria (`wm_norandr` 6, `extension` 21, `--fb` 3) | PASS |
| Startup transcript discriminates all three paths | PASS (available / forced off / genuinely absent) |
| Negative: coalescing guard deleted | PASS (stale-redelivery case red) |
| Negative: geometry read off the event | PASS (stale-redelivery case red) |
| Negative: `ensureVisible()` reflow loop deleted | PASS (offscreen-move + norandr-reflow cases red) |
| Negative: `updateWorkarea()` call deleted | PASS (4 cases red) |
| Negative: fullscreen guard deleted | PASS (fullscreen case red at frame 758,579) |
| Negative: clamp made unconditional | PASS (still-fits case red) |
| Negative: root-ConfigureNotify branch deleted | PASS (norandr-reflow case red) |
| Negative: `StructureNotifyMask` removed from the root mask | PASS (norandr-reflow case red) |
| Negative: control case pointed at the RANDR-less server | PASS (control red) |
| Probe: four events per resize, two carrying stale dimensions | CONFIRMED on this host |
| Probe: replaying a resize at the same geometry emits zero events | CONFIRMED (this is what made deviation 2 necessary) |
| Probe: no resize possible at all on a RANDR-less server | CONFIRMED (`RandR extension missing`) |
| Probe: libXrandr leaks 32 bytes when the extension is absent | CONFIRMED with a standalone reproducer |

## Self-Check: PASSED

All five commit hashes (`8a0f024`, `37f0f56`, `556682c`, `78f5f55`, `5ab2c91`) resolve in `git log`. Every modified file exists on disk with the claimed content. `src/`, `include/` and `CMakeLists.txt` are byte-identical to their committed state after the negative-check experiments (`git diff` empty for those paths); the temporary baseline build tree used for the flake comparison was removed.

## Next Phase Readiness

- **The geometry path is complete end to end.** 08-04 gave one place to read; this plan gives one place to write. Anything later in the phase that changes what a screen dimension *means* now has exactly two functions to reason about.
- **08-06 (RENDER) can copy a third instance of the same convention.** The capability triple now has two worked examples in the tree — Shape and RANDR — and RANDR is the closer template for RENDER, because `-extension RENDER` gives a genuinely capability-less server just as `-extension RANDR` does.
- **`test_wm_geometry` carries two tag groups** and takes both a `-screen` override and a `childEnv` override through `WmFixture` with no build plumbing. A third group needs cases and a tag, nothing more.
- **Carried constraint, still live:** deferred item 6 (`circulate()` spins forever with no Normal client) is untouched and remains a 100%-CPU freeze reachable by right-clicking the root of a freshly started WM. It is the single most user-visible defect the phase has found. 08-07 … 08-10 are the next scheduled visit to `src/Buttons.cpp`.
- **Carried planner assumption, unchanged:** XDIS-01 makes no claim about intermediate visual states during a multi-event resize, and no claim about resize *growth* — untestable on this server, whose RANDR maximum equals its start geometry. XDIS-02's behaviour on real remote servers is proven here only on Xvfb; TigerVNC, XRDP and X2Go are 08-14's job.
- **New for 08-14:** the release notes should say that `libxrandr` is now a hard build dependency, and that a RANDR-less remote server still tracks resolution changes through the root window.

---
*Phase: 08-xrandr-vnc-compatibility-focus-rules*
*Completed: 2026-08-11*
