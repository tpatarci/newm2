# Phase 8: Xrandr + VNC Compatibility + Focus/Rules - Context

**Gathered:** 2026-08-11
**Status:** Ready for planning

<domain>
## Phase Boundary

Make the compiled window manager **provably** correct and portable: display/geometry handling across remote X servers, graceful extension degradation, user-controllable focus behavior, per-window rules, and the process-level test/sanitizer/evidence gates recorded in `COMPILED_CODE_BEHAVIOR_CHECKLIST.md`.

Requirements: XDIS-01 through XDIS-05, FOCUS-01, FOCUS-02, RULES-01, RULES-02, TEST-05 through TEST-08.

**In scope:**
- Environment/dependency preflight and a working build on the dev host
- Process-level tests that drive the real `wm2-born-again` binary under Xvfb/Xephyr
- All 13 "Missing Automated Coverage To Add" items from the checklist
- Remediation of the four recorded scan findings
- Xrandr resolution-change handling with a non-Xrandr fallback path
- Shape-extension fallback made real and testable
- Focus policy wired from config to runtime + focus stealing prevention
- Window matching rules (WM_CLASS / WM_NAME / window type) with no-decorate, position/size, skip-taskbar actions
- Real-server validation on TigerVNC, XRDP, X2Go + release evidence capture

**Out of scope:**
- Multi-monitor / per-CRTC awareness (single-screen by design)
- Multi-desktop / workspace support (single-desktop by design, Phase 6)
- Core X font rendering revival (removed in Phase 4 — see D-09)
- Config GUI (Phase 9)

</domain>

<decisions>
## Implementation Decisions

### Dependency Preflight and Build Gates

- **D-01:** Install `libxft-dev` and `libfontconfig1-dev` as a Phase 8 task **and** add `scripts/preflight.sh` that verifies pkg-config deps (`x11`, `xext`, `xft`, `fontconfig`, `xrandr`), X11 tooling (`Xvfb`, `Xephyr`, `xprop`, `xwininfo`, `xdotool`, `xdpyinfo`), a simple X client, and fontconfig fallback resolution via `fc-match`. Confirmed on 2026-08-11: `xft` and `fontconfig` pkg-config files are **missing** on this host, so the project currently cannot be configured or built. Everything else in the tool list is present; `cppcheck` and `clang-tidy` are absent.
- **D-02:** The preflight is both a standalone script and a **ctest fixture** that fails loudly before any behavioral test runs. Not a CMake configure-time check — that would block plain binary builds on machines that do not run tests.
- **D-03:** `cppcheck` + `clang-tidy` are **blocking gates**, implemented as a checked-in **baseline**: current findings are recorded as the accepted baseline, and any *new* finding blocks completion. This makes the gate real without forcing a codebase-wide cleanup inside Phase 8. — **Reversibility:** reversible — the baseline file can be tightened or regenerated at any time.
- **D-04:** Four **hard blockers** for Phase 8 completion: (1) Debug + Release build clean with full `ctest` passing, (2) ASan/UBSan clean, (3) static analysis with no new findings vs baseline, (4) runtime smoke transcript captured. All four must pass — none may be waived as an accepted exception.
- **D-05:** Release evidence (build logs, ctest output, `xprop -root` / `xwininfo -root -tree` transcripts, ASan logs, interaction checklist results, accepted deviations) is **committed** under `.planning/phases/08-xrandr-vnc-compatibility-focus-rules/evidence/`.
- **D-06:** `COMPILED_CODE_BEHAVIOR_CHECKLIST.md` **stays at the repo root** — it is an ongoing developer release/signoff checklist, not a Phase 8-only artifact.

### Process-Level Test Harness

- **D-07:** Process-level tests are a **Catch2 C++ test binary** (e.g. `tests/test_wm_process.cpp`) that forks/execs the real `wm2-born-again` binary onto the test display and asserts via `XGetWindowProperty`. This stays inside the existing ctest/Catch2 world, reuses the Xvfb fixtures, and works under ASan. Necessary because `WindowManager` starts its event loop in its constructor (`src/Manager.cpp:59-176`) and therefore cannot be unit-tested in-process. — **Reversibility:** costly — the harness shape determines how all 13 coverage items are written.
- **D-08:** Input is synthesized with **XTEST** (`XTestFakeButtonEvent` / `XTestFakeMotionEvent`) from the test process — not `xdotool` (runtime tool dependency, timing flakiness) and not raw `XSendEvent` (bypasses the real grab/pointer path, so it would not actually prove behavior). Note: XTEST adds `libXtst` as a **test-only** dependency.
- **D-09:** **All 13** "Missing Automated Coverage To Add" items from the checklist land in Phase 8 — not a subset. This is a large scope; expect 08-01 to split into several plans.
- **D-10:** Manual-vs-automated split: everything reachable via XTEST is automated (menus, drags, middle-click maximize, long-press delete, circular gesture, grab release). A human pass at phase verification covers only visual/feel judgments (rendering fidelity, remote latency behavior).

### Extension Fallbacks

- **D-11:** All Shape usage routes through a **single `combineShape(...)` wrapper** in `Border` that no-ops when the extension is unavailable. There are currently ~17 raw `XShapeCombineRectangles` call sites in `src/Border.cpp` and `hasShapeExtension()` (`include/Manager.h:55`) guards only one helper (`src/Border.cpp:151`). One wrapper means one place to test and a greppable invariant. — **Reversibility:** costly — undoing means re-inlining 17 call sites.
- **D-12:** The no-Shape path is forced in tests via a **hidden env var `WM2_FORCE_NO_SHAPE=1`** (no user-facing CLI flag). This makes the fallback testable end-to-end against the real binary, since no realistic modern X server actually lacks Shape.
- **D-13:** XDIS-04 ("core X font fallback when XRender unavailable") is satisfied at the **fontconfig-fallback level**, not by reviving core X fonts. Xft/fontconfig is already a hard build dependency; the requirement is met by proving the fontconfig fallback chain resolves on every target. Reintroducing core-font rendering would undo Phase 4's deliberate removal.
- **D-14:** **Amend XDIS-04's wording in `REQUIREMENTS.md`** to match D-13 so requirement and implementation do not silently diverge. Same pattern as Phase 7's D-02 wording fix.

### Focus Policy and Focus Stealing

- **D-15:** **Finding (verified 2026-08-11):** `clickToFocus`, `raiseOnFocus`, and `autoRaise` (`include/Config.h:22-24`, parsed at `src/Config.cpp:152-154`) are read **only by `tests/test_config.cpp`** — there are zero runtime consumers. The runtime unconditionally performs delayed focus-follows-pointer with auto-raise. FOCUS-02 is therefore **unimplemented**, not merely unproven.
- **D-16:** Keep the **three existing booleans** and wire them to runtime behavior. Do not introduce a `focus-policy=` enum. This matches upstream wm2's `CONFIG_*` semantics and preserves Phase 5's tested config surface and CLI flags with no breaking change.
- **D-17:** Set the defaults so that **today's shipped behavior is preserved** (pointer focus + auto-raise effectively on), now honestly represented in config. Note the current defaults are all `false` while the runtime behaves otherwise — that contradiction must be resolved in favor of current behavior, not the literal current defaults.
- **D-18:** FOCUS-01: compare the window's `_NET_WM_USER_TIME` against the last user-interaction timestamp. If newer, grant focus. If stale, **map unfocused and set the urgency / `_NET_WM_STATE_DEMANDS_ATTENTION` hint** so the tab signals the window wants attention. Not a silent deny.
- **D-19:** Focus stealing prevention is **configurable with an off switch**, defaulting on. Legacy X clients that set no `_NET_WM_USER_TIME` will otherwise feel broken.

### Window Rules

- **D-20:** Rules use **repeated ordered key groups** in the existing key=value config: `rule-match-class=` starts a new rule and subsequent `rule-*` keys attach to it. This is exactly the `menu-entry-name` / `-command` / `-category` precedent from Phase 7 (D-07) — same parser shape, no new format, hand-editable by non-programmers.
- **D-21:** Matching supports **exact and substring** criteria on WM_CLASS / WM_NAME / window type; a rule with multiple criteria requires **all** to match (AND). No glob, no regex — predictable and nothing for the user to learn.
- **D-22:** On multiple matches, **all matching rules apply in file order, later wins per-action**. This lets a user layer a broad rule plus a specific override.
- **D-23:** Actions shipping in Phase 8: **`no-decorate`**, **`position` + `size`**, **`skip-taskbar`**. The `specific workspace` action from RULES-02 is **excluded** — the WM is single-desktop by design (`_NET_NUMBER_OF_DESKTOPS=1`, Phase 6), so the action has nothing to target. **Flag:** RULES-02's wording should be amended or annotated to record this exclusion and its reason.

### Xrandr and Screen Geometry

- **D-24:** **Finding:** there is currently **no Xrandr code at all** in `src/` or `include/`, and `libxrandr` is not a CMake dependency. This is greenfield (host has 1.5.2 available).
- **D-25:** On resolution change, **clamp offscreen windows back into view** — windows fully or mostly offscreen after a shrink are **moved, not resized**, so their tab is reachable; windows still visible are left exactly where the user put them. Least surprising, and important on VNC where clients resize the desktop at will.
- **D-26:** `libxrandr` becomes a **required** pkg-config dependency alongside `x11`/`xext`/`xft`/`fontconfig`, with a **runtime** check for whether the server actually offers the extension (which is what satisfies XDIS-02). It is ubiquitous on Ubuntu 22.04+.
- **D-27:** Screen geometry is tracked through a **single accessor** (`screenWidth()` / `screenHeight()`) as the one source of truth, fed by `RRScreenChangeNotify` when Xrandr is available and by `DisplayWidth`/`DisplayHeight` otherwise. Deliberately mirrors the Shape wrapper (D-11): one place to force-disable and test.
- **D-28:** **Single screen only.** The X screen is treated as one rectangle regardless of CRTC layout, and the limitation is stated **explicitly in release notes** (the checklist already calls for this). VNC/VPS targets are single-head.

### Remote Desktop Validation

- **D-29:** Targets actually exercised in Phase 8: **TigerVNC, XRDP, and X2Go**. `xrdp` is already installed on this host; `tigervnc-standalone-server` is installable from apt; X2Go needs standing up.
- **D-30:** **TightVNC is declared untested** with a recorded rationale (superseded by TigerVNC, Xvnc-derived so behavior overlaps) naming its closest tested proxy — logged as an accepted deviation with owner and follow-up per the checklist.
- **D-31:** **Xephyr/Xvfb carry the automated ctest suite**; real VNC/XRDP/X2Go sessions are exercised once per release as manual smoke evidence. Keeps ctest fast, hermetic, and consistent with the project's no-CI / run-gates-locally policy.
- **D-32:** The 512MB low-resource check is verified by **measuring resident memory (RSS) and idle CPU under a multi-window load and asserting against a documented budget** — a real pass/fail bar, not just recorded numbers. Directly serves the 512MB VPS constraint in PROJECT.md.

### Claude's Discretion

- Whether to replace the hardcoded Xvfb display `:99` with dynamic free-display allocation, or keep `:99` with a collision guard. Note: `:99` is hardcoded across ~5 sites in `CMakeLists.txt` (lines 88, 100, 143, 162, 178), so dynamic allocation is real work — the planner decides based on how the process-level harness shapes up.
- Exact `combineShape()` signature and how the `ShapeUnion`/`ShapeSubtract`/`ShapeSet` variants collapse into it.
- Exact config key names for rule actions (`rule-action=`, `rule-no-decorate=`, etc.) within the D-20 repeated-group shape.
- Static-analysis baseline file format and location.
- How 08-01 splits into multiple plans given all 13 coverage items (D-09).
- Precise RSS / idle-CPU thresholds for D-32.
- Whether `libXtst` is linked test-only or unconditionally.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase 8 Primary Driver
- `COMPILED_CODE_BEHAVIOR_CHECKLIST.md` — **The central Phase 8 artifact.** Contains the four current scan findings, environment preflight, build/test gates, the 13 missing-coverage items, runtime smoke checklist, user interaction checklist, ICCCM/EWMH checklist, rendering/geometry checklist, robustness checklist, remote-desktop checklist, and required release evidence. Success criteria 6 and 7 of Phase 8 are defined by this file.

### Project Context
- `.planning/PROJECT.md` — Core value, 512MB VPS / VNC constraints, key decisions
- `.planning/REQUIREMENTS.md` — XDIS-01..05, FOCUS-01..02, RULES-01..02, TEST-05..08 (note D-14 and D-23 amendment flags)
- `.planning/ROADMAP.md` — Phase 8 goal, 7 success criteria, and the 3-plan breakdown

### Prior Phase Decisions
- `.planning/phases/05-configuration-system/05-CONTEXT.md` — key=value config format, XDG paths, CLI override precedence. Window rules and focus policy extend **this same** file and parser.
- `.planning/phases/06-ewmh-compliance/06-CONTEXT.md` — window type handling, `_NET_WM_STATE` dispatch, dock strut/border-strip path (reused by `no-decorate`), single-desktop decision that excludes the workspace rule action
- `.planning/phases/07-root-menu-application-discovery/07-CONTEXT.md` — D-07 repeated-key-group config precedent that D-20 follows; D-02 wording-fix precedent that D-14 follows
- `.planning/phases/04-border-xft-font-rendering/04-CONTEXT.md` — Xft/fontconfig decisions and the deliberate removal of core fonts that D-13 must not undo

### Codebase Analysis
- `.planning/codebase/TESTING.md` — current test surface and the X11-display barrier
- `.planning/codebase/CONCERNS.md` — existing technical debt
- `.planning/codebase/ARCHITECTURE.md` — event flow and component boundaries

### Source Files (Primary Targets)
- `src/Events.cpp:237-283` — `eventDestroy` lifetime hazard: erases the owning `unique_ptr<Client>` then dereferences `c->isDock()`. Preserve `wasDock` before erase; prove under ASan. (Scan finding 2)
- `src/Border.cpp` — ~17 `XShapeCombineRectangles` call sites (lines 163, 167, 180, 188, 346, 352, 376, 391, 438, 447, 483, 502, 538-568) to route through the D-11 wrapper
- `src/Border.cpp:151` — existing `hasShapeExtension()` consumer, the only currently-guarded site
- `include/Manager.h:55` — `hasShapeExtension()`; `include/Manager.h:131` — `m_shapeEvent`
- `src/Manager.cpp:160` — where `m_shapeEvent = -1` records Shape unavailability
- `include/Config.h:22-24` and `src/Config.cpp:152-154` — the three dead focus booleans (D-15)
- `src/Manager.cpp:938-1030` — focus/auto-raise timer state machine; `src/Manager.cpp:810`, `src/Client.cpp:880` (`focusIfAppropriate`), `src/Client.cpp:1329-1334` — the unconditional focus-follows-pointer path FOCUS-02 must gate
- `src/Manager.cpp:59-176` — `WindowManager` constructor runs the event loop, forcing the D-07 child-process harness
- `src/Manager.cpp:230` — `WindowManager::fatal()` exits, forcing subprocess-based error-path tests
- `src/Client.cpp:1144` — `fixResizeDimensions()` size-hint constraints (coverage item)
- `src/Client.cpp:702` — `gravitate()` gravity modes (coverage item)
- `src/Client.cpp:270`, `src/Client.cpp:311` — fullscreen/maximize geometry restore (coverage item)
- `src/Client.cpp:775`, `src/Client.cpp:793` — `hide()` / `unhide()` hidden-list transfers (coverage item)
- `src/Events.cpp:292` — `eventClient()` EWMH client messages (coverage item)
- `CMakeLists.txt:11-14` — pkg-config dependency list to extend with `xrandr`
- `CMakeLists.txt:70-223` — test registration and Xvfb fixture wiring (`:99` at lines 88, 100, 143, 162, 178)

### Upstream Reference
- `upstream-wm2/Config.h` — original `CONFIG_CLICK_TO_FOCUS` / `CONFIG_RAISE_ON_FOCUS` / `CONFIG_AUTO_RAISE` semantics that D-16 preserves

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- Phase 5 config parser (`src/Config.cpp`) — repeated-key-group handling already proven by `menu-entry-*`; window rules (D-20) reuse the same mechanism directly
- Phase 6 dock border-strip/restore path — the `no-decorate` rule action (D-23) reuses it rather than inventing a new undecorated path
- Phase 6 `_NET_WM_STATE` dispatch — `skip-taskbar` sets `_NET_WM_STATE_SKIP_TASKBAR`/`SKIP_PAGER` through the existing state machinery
- Catch2 + Xvfb ctest fixtures (`CMakeLists.txt:85-101`) — the process-level harness reuses `FIXTURES_REQUIRED xvfb_display` rather than building new lifecycle management
- `include/x11wrap.h` RAII wrappers — Xrandr resources and any new X11 handles follow the same ownership pattern
- Existing auto-raise timer arithmetic (`m_autoRaiseDeadline`, `src/Manager.cpp:950-1030`) and its tests (`tests/test_autoraise.cpp`) — focus policy gating layers on top, it does not replace the timing

### Established Patterns
- `m_` prefix for members; `bool`/`std::vector`/`std::string` throughout (C++17, post-Phase-1)
- Hand-rolled parsing over new dependencies (Phase 5 config, Phase 7 .desktop and appcache) — argues for hand-rolled rule parsing
- Runtime capability checks stored as a member and exposed via a predicate (`m_shapeEvent` / `hasShapeExtension()`) — D-27's Xrandr accessor follows the same shape
- Fatal-on-unrecoverable via `WindowManager::fatal()`; warnings via `fprintf(stderr, "wm2: warning: ...")`

### Integration Points
- `src/Config.cpp` `applyKeyValue()` — where `rule-*` keys and the focus-stealing toggle are parsed
- `src/Client.cpp` manage/map path — where window rules are matched and applied at map time, and where FOCUS-01 timestamp comparison gates initial focus
- `src/Manager.cpp` focus state machine — where the three focus booleans finally gate behavior
- `src/Events.cpp` event loop — where `RRScreenChangeNotify` is dispatched (alongside the existing `m_shapeEvent` dispatch at `src/Events.cpp:116`)
- `src/Border.cpp` — all Shape calls funnel into the D-11 wrapper
- `CMakeLists.txt` — `xrandr` dependency, `libXtst` for tests, preflight fixture, sanitizer/static-analysis targets

</code_context>

<specifics>
## Specific Ideas

- The Shape wrapper (D-11) and the screen-geometry accessor (D-27) are deliberately the **same shape of solution**: one funnel per capability, so each has exactly one place to force-disable in tests. Keep them symmetric.
- "Prove, don't claim" is the governing principle for this phase — every checklist box needs an automated test, a recorded manual run, or an explicitly accepted exception with a named reason and owner.
- Phase 8 must not silently narrow: where a requirement's literal wording cannot be met as written (XDIS-04 core fonts, RULES-02 workspace), the requirement text gets amended with the reason rather than quietly ignored.

</specifics>

<deferred>
## Deferred Ideas

- **Per-CRTC / multi-monitor awareness** — clamping windows to the nearest CRTC instead of the whole screen. A genuine new capability; would need its own phase. (Raised under Xrandr reflow, D-28.)
- **`specific workspace` rule action** (RULES-02) — nothing to target while the WM is single-desktop by design. Would require reopening multi-desktop support. (Raised under window rule actions, D-23.)
- **`--no-shape` as a user-facing CLI flag** — potentially useful for operators debugging a broken remote X server, but Phase 8 ships only the hidden env var (D-12).
- **`focus-policy=` enum config key** — clearer for non-programmers and aligned with the PROJECT.md non-programmer goal, but breaks Phase 5's tested config surface. Reconsider alongside the Phase 9 config GUI, which could present the booleans as a single control without changing the file format.

</deferred>

---

*Phase: 8-Xrandr + VNC Compatibility + Focus/Rules*
*Context gathered: 2026-08-11*
