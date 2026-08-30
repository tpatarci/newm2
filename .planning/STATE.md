---
gsd_state_version: 1.0
milestone: v1.0
current_phase: 08.5
current_phase_name: v1.0 Closeout
status: executing
stopped_at: HALTED at 08.5-04-PLAN.md Task 1 -- gate capture held (operator ruling B); no criterion-7 bundle produced
last_updated: "2026-08-30T22:42:42.762Z"
last_activity: 2026-08-30
last_activity_desc: Phase 08.5 execution started
state_head: 3b93ca553d8db9601d3cbdb5982aebf721948628
progress:
  total_phases: 10
  completed_phases: 4
  total_plans: 44
  completed_plans: 39
milestone_name: milestone
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-06)

**Core value:** A lightweight, visually distinctive window manager that works well on resource-constrained VPS instances via remote desktop -- simple enough for non-programmers to configure, reliable enough for daily use.
**Current focus:** Phase 08.5 — v1.0 Closeout *(INSERTED)*

## Current Position

Phase: 08.5 (v1.0 Closeout) — READY TO EXECUTE
Plan: 3 of 5 complete (08.5-01, 08.5-02, 08.5-03); 08.5-04 halted; 08.5-05 blocked on it
Status: Stuck on a halt — phase NOT verified, NOT complete. Resolve the halt before re-running verification.
Last activity: 2026-08-30 — Phase 08.5 execution started

Phase 8 closed at `66591ec`, verified with 2 declared gaps (5/7 success criteria).
Phase 8.5 exists to close them: RULES-01, XDIS-05, TEST-08.

Progress: [████████░░] 81% (7/9 phases complete)

## Performance Metrics

**Velocity:**

- Total plans completed: 22
- Average duration: 18min
- Total execution time: 0.6 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 01 | 2 | - | - |
| 02 | 2 | - | - |
| 03 | 3 | - | - |
| 04 | 3 | - | - |
| 05 | 3 | - | - |
| 06 | 3 | - | - |
| 07 | 6 | - | - |

**Recent Trend:**

- Last 5 plans: P01(14min), P02(22min)
- Trend: Steady
- Trend: -

*Updated after each plan completion*
| Phase 01 P01 | 14min | 3 tasks | 12 files |
| Phase 01 P02 | 22min | 3 tasks | 9 files |
| Phase 03 P01 | 15min | 2 tasks | 6 files |
| Phase 06 P01 | 15min | 1 tasks | 6 files |
| Phase 06 P02 | 7min | 1 tasks | 6 files |
| Phase 06 P03 | 5min | 1 tasks | 5 files |
| Phase 07 P01 | 15min | 3 tasks | 5 files |
| Phase 07 P02 | 20min | 2 tasks | 4 files |
| Phase 07 P04 | 25min | 2 tasks | 2 files |
| Phase 07 P03 | 20min | 3 tasks | 7 files |
| Phase 07 P05 | 20min | 2 tasks | 2 files |
| Phase 07 P06 | 25min | 3 tasks | 4 files |
**Per-Plan Metrics:**

| Plan | Duration | Tasks | Files |
|------|----------|-------|-------|
| Phase 08 P01 | 103min | 3 tasks | 7 files |
| Phase 08 P02 | 68min | 3 tasks | 6 files |
| Phase 08 P03 | 39min | 3 tasks | 8 files |
| Phase 08 P04 | 52min | 2 tasks | 7 files |
| Phase 08 P05 | 78min | 3 tasks | 7 files |
| Phase 08 P06 | 71min | 3 tasks | 9 files |
| Phase 08 P07 | 55min | 4 tasks | 11 files |
| Phase 08 P08 | 115min | 4 tasks | 11 files |
| Phase 08 P09 | 60min | 3 tasks | 7 files |
| Phase 08 P10 | 21min | 3 tasks | 6 files |
| Phase 08 P11 | 75min | 3 tasks | 6 files |
| Phase 08 P12 | 95min | 3 tasks | 9 files |
| Phase 08 P13 | 200min | 3 tasks | 9 files |
| Phase 08.5 P03 | 5min | 4 tasks | 1 files |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- [Phase ?]: test_raii linked against X11 because inline RAII destructors call XFree* functions
- [Phase ?]: catch_discover_tests PROPERTIES used for Xvfb fixtures instead of post-discovery set_tests_properties
- [Phase 01 P02]: Client::display() moved to public so Border can access it
- [Phase 01 P02]: installCursorOnWindow() added to Manager for Border to set cursor on resize handle
- [Phase 01 P02]: std::vector<XRectangle> replaces custom RectangleList macro in Border.cpp
- [Phase 01 P02]: Shape extension missing produces warning but not fatal exit
- [Phase ?]: ClientState enum values match X11 constants (Withdrawn=0, Normal=1, Iconic=3)
- [Phase ?]: State transitions validated but applied anyway with warning -- matches upstream permissive behavior
- [Phase ?]: Destructor-only cleanup replaces release() method; unreparent called in destructor for non-withdrawn clients
- [Phase 03 P03]: Tests verify infrastructure types (ServerGrab, ClientState) directly on Xvfb since full WindowManager cannot be unit-tested
- [Phase 03 P03]: Added X11/Xatom.h include for XA_WINDOW atom in colormap property test
- [Phase ?]: 07-01: xdgDataHome() kept internal (anonymous namespace), not exposed in DesktopEntry.h public API
- [Phase ?]: 07-01: field-code-only tokens (e.g. lone %u) are dropped from execArgv rather than kept as empty strings
- [Phase ?]: Local MmapGuard RAII struct in BinaryScanner.cpp's anonymous namespace instead of reusing Manager.h's FdGuard, to keep BinaryScanner X11-free
- [Phase ?]: Binary-scan-discovered apps default to category=Other (distinct from D-07's Custom for manual entries)
- [Phase ?]: 07-04: m_apps/m_submenuWindow initializer-list position follows header declaration order (not the plan text's literal wording) to avoid -Wreorder
- [Phase ?]: 07-04: Task 1/Task 2 commits split via temporary revert-reapply since both touch the same two files
- [Phase ?]: 07-03: write() creates parent config directory if missing (nothing else in the project creates ~/.config/wm2-born-again/)
- [Phase ?]: 07-03: mergeEntries() does not re-apply D-07 Custom default -- applied once at Config::applyKeyValue's menu-entry-name= handler
- [Phase 07]: openCategorySubmenu() signature extended with outerX/outerY/outerMaxWidth/rowIndex params beyond the plan's literal 2-param declaration, since the outer menu's position/hover-row-index are menu()'s local variables not otherwise recoverable
- [Phase ?]: 07-06: wm2-born-again CMake target was missing AppCache.cpp/DesktopEntry.cpp/BinaryScanner.cpp source entries -- fixed in Task 1
- [Phase ?]: 07-06: D-02 wording fix scoped strictly to PROJECT.md/ROADMAP.md's 5 flagged AI-powered occurrences -- AI showcase framing preserved
- [Phase 8]: COMPILED_CODE_BEHAVIOR_CHECKLIST.md is routed into Phase 8 as the first verification/hardening gate rather than a standalone milestone, because its findings overlap XDIS, FOCUS, remote-display compatibility, and runtime proof requirements
- [Phase 08]: 08-01: eventDestroy fix cited as SCAN-02 not D-07 -- D-07 already denotes the child-process harness (08-CONTEXT) and Phase 7's Custom-category default
- [Phase 08]: 08-01: dock-ness read via a 'const Client& dying' alias so the c->isDock() grep guard is a true regression detector
- [Phase 08]: 08-01: fixture Xvfb needs -noreset plus a retained keepalive connection; the readiness probe's disconnect was resetting the server under the WM (~9% flake)
- [Phase 08]: 08-01: XTEST tab click uses press -> 200ms -> release, not click(), to avoid racing the WM's move-loop pointer grab
- [Phase 08]: 08-01: D-33 tag-selection convention established -- Catch2 tags as ctest labels, anchored -L, --no-tests=error on every gate; CMake floor raised to 3.20
- [Phase ?]: cppcheck 2.7's CLI never computes a per-finding hash, so the static-analysis baseline is anchored on hashes the gate computes itself and cppcheck consumes a derived hash-free suppression file generated at run time
- [Phase ?]: clang-tidy's fatal list contains only families already clean on this tree (bugprone-dangling-handle, clang-analyzer-core.*); the three families that fire today are reported-only with a documented promotion path
- [Phase ?]: 08-03: routing the rectangular fallback helpers through combineShape() makes them emit nothing on Shape-less servers -- that silence IS the correct fallback, since an unshaped window is already rectangular
- [Phase ?]: 08-03: capability triple documented in include/Manager.h (negative member sentinel + has*Extension() predicate + one no-op-when-absent funnel); 08-05/08-06 copy it
- [Phase ?]: 08-03: bugprone-branch-clone true pre-existing count is 6, not the 5 recorded by 08-02 (missed src/Client.cpp:1415); re-measured at 08-02's own commit
- [Phase ?]: D-27 geometry funnel: screenWidth()/screenHeight() over a manager-owned cache seeded in initialiseScreen(); all 16 direct DisplayWidth/DisplayHeight reads routed through it
- [Phase ?]: Geometry tests pin exact values and are each negative-tested; fullscreen POSITION is deliberately not pinned because the value the WM produces today is a defect (deferred item 8)
- [Phase ?]: 08-05: the resolution-change handler re-reads root geometry with XGetWindowAttributes rather than re-reading screenWidth()/screenHeight() -- since 08-04 those return the manager's own cache, so re-reading them after a resize returns the value being replaced
- [Phase ?]: 08-05: XDIS-02's reflow claim is proven with WM2_FORCE_NO_RANDR on a RANDR-capable server, because on a genuinely RANDR-less server no resolution change can be produced at all -- the only X resize mechanism is the missing extension
- [Phase ?]: Open Question 1 answered: a rotated FcMatrix Xft font loads, measures identically and draws without protocol error on a RENDER-less server -- libXft's core X11 glyph path keeps the sideways tab, so XDIS-05 targets without XRender lose nothing
- [Phase ?]: Border's tab-font load is a four-rung ladder (rotated preferred, rotated generic, unrotated, none) that cannot terminate the process; rung 1 is silent and every other rung prints one warning
- [Phase ?]: The XRender capability probe carries no sentinel and drives no behaviour -- it exists for the XDIS-05 evidence transcript and as the discriminator for the RENDER-less tests
- [Phase ?]: Deferred item 10's XMaskEvent hypothesis is refuted; the rare startup wedge is deferred item 9 (unflushed WM output), cured by retrying the readiness probe with a fresh window
- [Phase ?]: XDIS-04's requirement text amended (D-14) to state fontconfig-level degradation and record why core X font revival is excluded
- [Phase ?]: 08-07: FOCUS-02 was unimplemented, not merely unproven -- the three focus booleans had zero runtime consumers; each now reaches a real branch (D-15)
- [Phase ?]: 08-07: the auto-raise gate sits where the deadline is ARMED, not in the expiry branches, so computePollTimeout() reports no deadline and the loop blocks indefinitely
- [Phase ?]: 08-07: D-17 resolved in favour of behaviour -- raiseOnFocus and autoRaise now default TRUE, which is what users already got; the all-false defaults never described the binary
- [Phase ?]: 08-07: the WM startup banner now reports the running focus policy instead of claiming 'Focus follows pointer' unconditionally -- that transcript is the XDIS-05 evidence artefact
- [Phase ?]: 08-07: deferred item 6 (circulate() 100%-CPU spin) fixed with a bounded scan; the regression test creates its own transient client so it cannot go vacuous when item 7 is fixed
- [Phase ?]: 08-07: a settle before a non-event assertion must be REPEATED AND SPACED -- 15 pumps back to back left a deleted production gate green; the same 15 at 20ms intervals redden it
- [Phase ?]: 08-07: gating auto-raise at the expiry site was implemented and measured -- it does NOT spin (0 CPU ticks), so the idle-CPU case is not evidence for the gate's placement
- [Phase 08]: 08-08: _NET_ACTIVE_WINDOW arbitrated by EWMH source indication (pager granted, application arbitrated, legacy granted) — supersedes Phase 6 D-10, amended append-only in 06-CONTEXT.md
- [Phase 08]: 08-08: map-time focus granting is NOT gated on the pointer-entry policy; the resulting conflict with 08-07 behaviours 1-2 was resolved by constructing their unfocused precondition with a zero _NET_WM_USER_TIME, leaving every assertion intact
- [Phase 08]: 08-08: _NET_WM_USER_TIME of zero means do-not-focus-me at map time, but absent evidence (arbitrated as stale) on an activation request
- [Phase 08]: RuleTriState uses Off/On, not False/True: Xlib #defines both macros and Config.h now reaches every X11-including translation unit
- [Phase 08]: rule-match-class is tested against both WM_CLASS fields; rule-match-name against the instance name only (asymmetry asserted negatively)
- [Phase 08]: Rule group state is two explicit fields (ruleOpen + ruleLastWasAction), reset per file so a system-config rule cannot absorb a user-config match key
- [Phase 08]: No CLI flags for rules: repeated ordered key groups have no getopt expression
- [Phase 08]: RULES-02: m_ruleNoDecorate kept distinct from isDock() -- both reach the unframed path but only a dock may recompute the workarea
- [Phase 08]: RULES-02: rule geometry is overlaid on the client's request between the size-hint floors and the clamps, never applied after mapping as a second geometry authority
- [Phase 08]: D-23: RULES-02 amended in the requirement text -- three shipping actions, workspace action excluded because the WM is single-desktop by design (Phase 6, D-11)
- [Phase 08]: Plan 08-11: the focus-candidate destroy case stretches the auto-raise delay rather than shortening it, so the 'tracking still pending' precondition is observable from outside the WM instead of assumed
- [Phase 08]: Plan 08-11: the WM's X protocol error log is now a test observable; errorHandler() logs and returns 0, so teardown defects are invisible to any assertion about windows or properties
- [Phase 08]: Plan 08-11: hidden-list transfers are proven through the published _NET_CLIENT_LIST ORDER, explicitly as a proxy for the vector move and not as an EWMH ordering claim
- [Phase 08]: Plan 08-11: fixResizeDimensions() validates each resize increment per axis before its own division; a non-positive increment means 'do not quantise this axis', never a divide by zero
- [Phase 08]: Deferred item 8's recorded cause was corrected: Border::unmap()'s frame/tab/button unmaps each reach Client::eventUnmap() via hasWindow(), and the tab's is what withdrew the fullscreen client -- the reparent guard alone is not sufficient
- [Phase 08]: eventClient's _NET_WM_STATE branch guards on !isWithdrawn(), not !isNormal(): an Iconic client is genuinely managed, Withdrawn is the ICCCM's own word for unmanaged
- [Phase 08]: Maximize insets the workarea by the frame decoration so the FRAME fills it; the client sits at (xIndent, yIndent), the one convention every geometry path in the WM now follows
- [Phase 08]: setMaximized falls back to the screen when the workarea is not a usable rectangle inside it -- a range check, deliberately not a size threshold, which would be a policy about how much screen a panel may claim
- [Phase 08]: clampStrut recorded as an equivalent mutant: Xlib sign-extends format-32 property data into long, so the narrowing conversion is exact and no external observation can distinguish it
- [Phase 08]: Memory-safety mutations must be run in the ASan tree -- two of fifteen are green in debug and kill the WM under the sanitizer
- [Phase 08]: All thirteen of the checklist's missing-coverage items are now automated; 08-13 closed items 5, 6 and 13
- [Phase 08]: A YXSorted SHAPE request is a promise the server validates: Border::combineShapeSorted() is now the only way to issue one, because every rectangle list depends on FRAME_WIDTH and the tab width
- [Phase 08]: XSync(dpy, True) discards EVENTS, not errors: Client::unreparent() was throwing away the WM's whole queued backlog on every client teardown
- [Phase 08]: Xlib out-parameters are not written on failure -- getColormaps() was installing uninitialised colormap XIDs
- [Phase 08]: kOptionSpecs is the single declaration of the CLI surface: getopt_long()'s array and the --help usage text are both generated from it, and --no- negations are derived rather than listed
- [Phase 08]: Deferred item 5 (LSan suppressions not wired into Catch2 binaries) is deliberately still open, with both candidate fixes costed and an owner recommended
- [Phase 08.5]: D-8.5-04 reverses the handoff's X2Go descope on measurement -- `x2goserver`/`nxagent` were already installed (dpkg 2026-08-29 17:43), nxagent runs headless nested on an Xvfb advertising SHAPE/RANDR/RENDER, and the WM frames clients on it. Validation costs ~2 minutes, not a human session, so XDIS-05 is met as written rather than amended
- [Phase 08.5]: D-8.5-02 the WM reads only legacy WM_NAME and never `_NET_WM_NAME` for a client title (`Atoms::net_wmName` is used solely to name the WM's own check window). A title rule on that alone would be dead for modern clients, so the title read becomes EWMH-first with ICCCM fallback
- [Phase 08.5]: D-8.5-01 `rule-match-name` renamed to `rule-match-instance` with NO deprecated alias -- free before v1.0, breaking after; the cost is one `unknown config key` warning for an existing local config
- [Phase 08.5]: D-8.5-03 title rules fold once at map time and are deliberately NOT re-folded on title change -- re-applying geometry on rename would make windows jump when a document is renamed; re-fold goes to the v1.1 backlog
- [Phase 08.5]: 08.5-03: the shipped release notes and the signoff checklist are worded differently on purpose -- COMPILED_CODE_BEHAVIOR_CHECKLIST.md keeps the retired D-8-TIGHTVNC rationale verbatim as a record, the user-facing notes name it only as a retired argument and state the general lesson instead
- [Phase 08.5]: 08.5-03: the literals x2gostartagent and SCOPE.md were deliberately withheld from Task 2's table cells and introduced in Task 3, so Task 3's positive guards could still redden -- a guard pre-greened by an earlier task in the same plan measures nothing, which is the exact defect this plan closes
- [Phase 08.5]: 08.5-04: gate capture held rather than recorded on a suite that is not reliably green in one shot -- a release signoff whose gate needs retries is weaker evidence than no signoff, and the measurement (2/5 red, different case each time) is a rate rather than an attribution

### Pending Todos

- Phase 8 08-01: Turn COMPILED_CODE_BEHAVIOR_CHECKLIST.md into executable process-level Xvfb/Xephyr tests, sanitizer/static-analysis gates, and release evidence capture.
- Phase 8 08-01: Resolve or explicitly accept current scan findings: missing xft/fontconfig build preflight on this host, eventDestroy client lifetime hazard, no-Shape fallback proof, and focus policy config wiring proof.

### Blockers/Concerns

- Phase 4 (Border/Xft): Xft rendering inside shaped windows is poorly documented; build a PoC first
- Phase 7 (App Scanner): Binary scan heuristics for identifying GUI apps are novel
- Phase 8 (Behavior Verification): Current host cannot configure until pkg-config sees xft/fontconfig; do not claim compiled behavior coverage until dependency preflight passes
- Phase 8 (Behavior Verification): Static scan found eventDestroy use-after-free risk, incomplete no-Shape fallback proof, and parsed focus config that needs runtime behavior tests
- Phase 9 (Config GUI): GTK3 performance over SSH X forwarding is unvalidated
- Bare 'ctest --test-dir build/asan' fails 3 xft tests on a pre-existing fontconfig cache leak: tests/lsan.supp is wired only into the forked WM child, not the Catch2 test binaries. Run the asan tree via scripts/gates/build-all.sh until fixed (deferred-items.md item 5).
- ~~PRE-EXISTING: WindowManager::circulate() spins at 100% CPU forever when no client is Normal~~ RESOLVED in 08-07 (`62e9c0d`): bounded scan plus a [wm_circulate] regression test asserting both responsiveness and idleness. deferred-items.md item 6.
- 08-05 deferred item 10: rare WM startup hang between the EWMH publication and the event loop, suspected timestamp()'s unbounded XMaskEvent. Pre-existing code path; needs one backtrace from a hung child to confirm
- Deferred item 11: the sideways tab does not grow with the window title (axis-swapped rotated extents) -- measured in 08-06, out of scope there, recommended for 08-14
- Deferred item 12: the full process-level suite flakes at ~1 test per run on both this tree and the pre-08-06 baseline -- build-all.sh is not reliably green in one shot
- Deferred item 13 (08-11): every client destroy logs one X_UnmapWindow BadWindow because the resize handle is a child of the client window; blocks a strict no-protocol-errors assertion
- 08.5-04 GATE CAPTURE HELD (operator ruling B, 2026-08-30): the full debug suite is red in 2 of 5 runs at 313 tests, a DIFFERENT single case each time -- #198 exec-using-shell spawn-await (1/5 loaded, 0/12 isolated) and #93 interactive drag (test_wm_geometry.cpp:1492). [wm_menureopen] fired 0/5, so the known-intermittent framing does not cover either. Sharpens deferred item 12 from ~1 test per run to a measured 40% red-run rate. Blocks the v1.0 success-criterion-7 gate capture until the flake substrate is diagnosed. Evidence: evidence/gates/flake-measurement/ (af30f47). T-8-SHELL security half failed 0/5.

## Deferred Items

Items acknowledged and carried forward from previous milestone close:

| Category | Item | Status | Deferred At |
|----------|------|--------|-------------|
| *(none)* | | | |

## v1.1 backlog (created by Phase 8.5 drafting)

| Item | Why it is a debt with a name |
|------|------------------------------|
| Gesture and input coverage | Every defect the manual passes found lived in a path the suite already exercised: the suite asserts the *states* controls produce and never the *reachability* of the controls that drive them. Middle-click maximize, the circular fullscreen gesture, the menu's exit and hidden-client rows, and grab-release across cancel paths all have state coverage and no gesture coverage. |
| Rule re-fold on title change | D-8.5-03 declines it deliberately (a renamed document must not move its window). Revisit if users ask for it. |
| X2Go over a real network link | Phase 8.5 measures nxagent nested and locally; the compression proxy under latency is a different question and may remain open after 08.5-02. |

## Session Continuity

Last session: 2026-08-30T18:42:41.077Z
Stopped at: HALTED at 08.5-04-PLAN.md Task 1 -- gate capture held (operator ruling B); no criterion-7 bundle produced
Resume file: None
