# Phase 8: Xrandr + VNC Compatibility + Focus/Rules - Research

**Researched:** 2026-08-11
**Domain:** X11 window manager runtime portability (RANDR / extension fallbacks), EWMH focus-stealing prevention, per-window rules, and process-level compiled-behavior verification
**Confidence:** HIGH for the mechanisms and the host environment (verified by direct experiment on this machine); MEDIUM for remote-desktop server behaviour (TigerVNC/XRDP/X2Go not installed here); LOW for Xft-without-XRender rotated-glyph behaviour (needs a spike)

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

#### Dependency Preflight and Build Gates

- **D-01:** Install `libxft-dev` and `libfontconfig1-dev` as a Phase 8 task **and** add `scripts/preflight.sh` that verifies pkg-config deps (`x11`, `xext`, `xft`, `fontconfig`, `xrandr`), X11 tooling (`Xvfb`, `Xephyr`, `xprop`, `xwininfo`, `xdotool`, `xdpyinfo`), a simple X client, and fontconfig fallback resolution via `fc-match`. Confirmed on 2026-08-11: `xft` and `fontconfig` pkg-config files are **missing** on this host, so the project currently cannot be configured or built. Everything else in the tool list is present; `cppcheck` and `clang-tidy` are absent.
- **D-02:** The preflight is both a standalone script and a **ctest fixture** that fails loudly before any behavioral test runs. Not a CMake configure-time check — that would block plain binary builds on machines that do not run tests.
- **D-03:** `cppcheck` + `clang-tidy` are **blocking gates**, implemented as a checked-in **baseline**: current findings are recorded as the accepted baseline, and any *new* finding blocks completion. This makes the gate real without forcing a codebase-wide cleanup inside Phase 8. — **Reversibility:** reversible — the baseline file can be tightened or regenerated at any time.
- **D-04:** Four **hard blockers** for Phase 8 completion: (1) Debug + Release build clean with full `ctest` passing, (2) ASan/UBSan clean, (3) static analysis with no new findings vs baseline, (4) runtime smoke transcript captured. All four must pass — none may be waived as an accepted exception.
- **D-05:** Release evidence (build logs, ctest output, `xprop -root` / `xwininfo -root -tree` transcripts, ASan logs, interaction checklist results, accepted deviations) is **committed** under `.planning/phases/08-xrandr-vnc-compatibility-focus-rules/evidence/`.
- **D-06:** `COMPILED_CODE_BEHAVIOR_CHECKLIST.md` **stays at the repo root** — it is an ongoing developer release/signoff checklist, not a Phase 8-only artifact.

#### Process-Level Test Harness

- **D-07:** Process-level tests are a **Catch2 C++ test binary** (e.g. `tests/test_wm_process.cpp`) that forks/execs the real `wm2-born-again` binary onto the test display and asserts via `XGetWindowProperty`. This stays inside the existing ctest/Catch2 world, reuses the Xvfb fixtures, and works under ASan. Necessary because `WindowManager` starts its event loop in its constructor (`src/Manager.cpp:59-176`) and therefore cannot be unit-tested in-process. — **Reversibility:** costly — the harness shape determines how all 13 coverage items are written.
- **D-08:** Input is synthesized with **XTEST** (`XTestFakeButtonEvent` / `XTestFakeMotionEvent`) from the test process — not `xdotool` (runtime tool dependency, timing flakiness) and not raw `XSendEvent` (bypasses the real grab/pointer path, so it would not actually prove behavior). Note: XTEST adds `libXtst` as a **test-only** dependency.
- **D-09:** **All 13** "Missing Automated Coverage To Add" items from the checklist land in Phase 8 — not a subset. This is a large scope; expect 08-01 to split into several plans.
- **D-10:** Manual-vs-automated split: everything reachable via XTEST is automated (menus, drags, middle-click maximize, long-press delete, circular gesture, grab release). A human pass at phase verification covers only visual/feel judgments (rendering fidelity, remote latency behavior).

#### Extension Fallbacks

- **D-11:** All Shape usage routes through a **single `combineShape(...)` wrapper** in `Border` that no-ops when the extension is unavailable. There are currently ~17 raw `XShapeCombineRectangles` call sites in `src/Border.cpp` and `hasShapeExtension()` (`include/Manager.h:55`) guards only one helper (`src/Border.cpp:151`). One wrapper means one place to test and a greppable invariant. — **Reversibility:** costly — undoing means re-inlining 17 call sites.
- **D-12:** The no-Shape path is forced in tests via a **hidden env var `WM2_FORCE_NO_SHAPE=1`** (no user-facing CLI flag). This makes the fallback testable end-to-end against the real binary, since no realistic modern X server actually lacks Shape.
- **D-13:** XDIS-04 ("core X font fallback when XRender unavailable") is satisfied at the **fontconfig-fallback level**, not by reviving core X fonts. Xft/fontconfig is already a hard build dependency; the requirement is met by proving the fontconfig fallback chain resolves on every target. Reintroducing core-font rendering would undo Phase 4's deliberate removal.
- **D-14:** **Amend XDIS-04's wording in `REQUIREMENTS.md`** to match D-13 so requirement and implementation do not silently diverge. Same pattern as Phase 7's D-02 wording fix.

#### Focus Policy and Focus Stealing

- **D-15:** **Finding (verified 2026-08-11):** `clickToFocus`, `raiseOnFocus`, and `autoRaise` (`include/Config.h:22-24`, parsed at `src/Config.cpp:152-154`) are read **only by `tests/test_config.cpp`** — there are zero runtime consumers. The runtime unconditionally performs delayed focus-follows-pointer with auto-raise. FOCUS-02 is therefore **unimplemented**, not merely unproven.
- **D-16:** Keep the **three existing booleans** and wire them to runtime behavior. Do not introduce a `focus-policy=` enum. This matches upstream wm2's `CONFIG_*` semantics and preserves Phase 5's tested config surface and CLI flags with no breaking change.
- **D-17:** Set the defaults so that **today's shipped behavior is preserved** (pointer focus + auto-raise effectively on), now honestly represented in config. Note the current defaults are all `false` while the runtime behaves otherwise — that contradiction must be resolved in favor of current behavior, not the literal current defaults.
- **D-18:** FOCUS-01: compare the window's `_NET_WM_USER_TIME` against the last user-interaction timestamp. If newer, grant focus. If stale, **map unfocused and set the urgency / `_NET_WM_STATE_DEMANDS_ATTENTION` hint** so the tab signals the window wants attention. Not a silent deny.
- **D-19:** Focus stealing prevention is **configurable with an off switch**, defaulting on. Legacy X clients that set no `_NET_WM_USER_TIME` will otherwise feel broken.

#### Window Rules

- **D-20:** Rules use **repeated ordered key groups** in the existing key=value config: `rule-match-class=` starts a new rule and subsequent `rule-*` keys attach to it. This is exactly the `menu-entry-name` / `-command` / `-category` precedent from Phase 7 (D-07) — same parser shape, no new format, hand-editable by non-programmers.
- **D-21:** Matching supports **exact and substring** criteria on WM_CLASS / WM_NAME / window type; a rule with multiple criteria requires **all** to match (AND). No glob, no regex — predictable and nothing for the user to learn.
- **D-22:** On multiple matches, **all matching rules apply in file order, later wins per-action**. This lets a user layer a broad rule plus a specific override.
- **D-23:** Actions shipping in Phase 8: **`no-decorate`**, **`position` + `size`**, **`skip-taskbar`**. The `specific workspace` action from RULES-02 is **excluded** — the WM is single-desktop by design (`_NET_NUMBER_OF_DESKTOPS=1`, Phase 6), so the action has nothing to target. **Flag:** RULES-02's wording should be amended or annotated to record this exclusion and its reason.

#### Xrandr and Screen Geometry

- **D-24:** **Finding:** there is currently **no Xrandr code at all** in `src/` or `include/`, and `libxrandr` is not a CMake dependency. This is greenfield (host has 1.5.2 available).
- **D-25:** On resolution change, **clamp offscreen windows back into view** — windows fully or mostly offscreen after a shrink are **moved, not resized**, so their tab is reachable; windows still visible are left exactly where the user put them. Least surprising, and important on VNC where clients resize the desktop at will.
- **D-26:** `libxrandr` becomes a **required** pkg-config dependency alongside `x11`/`xext`/`xft`/`fontconfig`, with a **runtime** check for whether the server actually offers the extension (which is what satisfies XDIS-02). It is ubiquitous on Ubuntu 22.04+.
- **D-27:** Screen geometry is tracked through a **single accessor** (`screenWidth()` / `screenHeight()`) as the one source of truth, fed by `RRScreenChangeNotify` when Xrandr is available and by `DisplayWidth`/`DisplayHeight` otherwise. Deliberately mirrors the Shape wrapper (D-11): one place to force-disable and test.
- **D-28:** **Single screen only.** The X screen is treated as one rectangle regardless of CRTC layout, and the limitation is stated **explicitly in release notes** (the checklist already calls for this). VNC/VPS targets are single-head.

#### Remote Desktop Validation

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

### Deferred Ideas (OUT OF SCOPE)

- **Per-CRTC / multi-monitor awareness** — clamping windows to the nearest CRTC instead of the whole screen. A genuine new capability; would need its own phase. (Raised under Xrandr reflow, D-28.)
- **`specific workspace` rule action** (RULES-02) — nothing to target while the WM is single-desktop by design. Would require reopening multi-desktop support. (Raised under window rule actions, D-23.)
- **`--no-shape` as a user-facing CLI flag** — potentially useful for operators debugging a broken remote X server, but Phase 8 ships only the hidden env var (D-12).
- **`focus-policy=` enum config key** — clearer for non-programmers and aligned with the PROJECT.md non-programmer goal, but breaks Phase 5's tested config surface. Reconsider alongside the Phase 9 config GUI, which could present the booleans as a single control without changing the file format.

</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| XDIS-01 | Xrandr support for display configuration and resolution changes | `XRRQueryExtension` + `XRRSelectInput(root, RRScreenChangeNotifyMask)` + **mandatory** `XRRUpdateConfiguration()`; verified end-to-end on Xvfb (see Pitfall 1, Code Example 1). Reflow reuses the existing `Client::ensureVisible()` primitive (`src/Client.cpp:840-856`). |
| XDIS-02 | Graceful fallback when Xrandr unavailable (VNC) | `Xvfb -extension RANDR` **is** supported by Xorg 21.1.4 and yields a genuinely RANDR-less server — so XDIS-02 is fully automatable, no env-var hack required. Fallback path needs `StructureNotifyMask` on root (not currently selected). |
| XDIS-03 | Graceful fallback when Shape unavailable (rectangular frames) | **SHAPE cannot be disabled** on this X server ("Extension \"SHAPE\" can not be disabled"). This independently validates D-12's `WM2_FORCE_NO_SHAPE=1` env var as the only viable test lever. 45 `XShapeCombineRectangles` call sites enumerated for the D-11 wrapper. |
| XDIS-04 | Graceful fallback when XRender unavailable | `Xvfb -extension RENDER` works, so the RENDER-less path **is** testable server-side (stronger than D-13's fc-match-only reading). `XftDefaultHasRender()` is an exported libXft symbol usable as an in-process probe. Open risk: the rotated `FcMatrix` tab font `fatal()`s on load failure (`src/Border.cpp:49`). |
| XDIS-05 | Compatible with TigerVNC, TightVNC, XRDP, X2Go | Extension-matrix guidance + the xrdp reconnect-at-different-resolution scenario that XDIS-01 must survive. `xdpyinfo`-based capability capture is the evidence format. |
| FOCUS-01 | Focus stealing prevention using `_NET_WM_USER_TIME` | EWMH spec text quoted verbatim (Code Example 3, Pitfall 5). Conflicts with Phase 6's "always grant `_NET_ACTIVE_WINDOW`" at `src/Events.cpp:304-309` — flagged. Requires two new atoms in `_NET_SUPPORTED`. |
| FOCUS-02 | Configurable focus policy | The three booleans are dead (`include/Config.h:22-24`); the gating points are enumerated with line numbers in Architecture Patterns / Pattern 3. |
| RULES-01 | Window matching rules (WM_CLASS, WM_NAME, window type) | No `XGetClassHint` call exists anywhere — greenfield. Window-type match vocabulary is fixed at exactly 4 values by `include/Client.h:24-29`. |
| RULES-02 | Per-rule actions | Reuse the Phase 6 dock/no-decorate path (`src/Client.cpp:117-128`) and `updateNetWmState()`; `skip-taskbar` needs two new atoms. Workspace action excluded per D-23. |
| TEST-05 | Process-level integration tests | Harness design (Pattern 1), XTEST grab-imperviousness requirement, and the **display-exclusivity constraint** that makes `:99` sharing unsafe (Pitfall 3). |
| TEST-06 | Debug/Release/ASan gates | ASan child-process report capture via `log_path` + `exitcode` (Pitfall 4); LSan suppressions for libX11/libfontconfig; `std::exit()` in forked children flagged. |
| TEST-07 | Environment preflight | Full audit executed — see Environment Availability. Two build-blocking packages missing. |
| TEST-08 | Runtime smoke evidence | Evidence commands verified working on this host; checklist's own expected-test-count baseline found stale (135 actual vs 102 claimed). |

</phase_requirements>

## Summary

> **Plan-numbering note (added during plan revision).** This document was written against a provisional three-plan skeleton and refers to workstreams as "08-01 / 08-02 / 08-03". The phase was subsequently sliced into **14 plans** (`08-01` … `08-14`, one per wave, a single `depends_on` chain). Where this document names a plan number, read it as the *workstream* unless the reference carries a `RESOLVED:` marker or an explicit correction — those name real plans. `08-VALIDATION.md`'s per-task map is the authoritative plan→requirement mapping.

Phase 8 is three different kinds of work wearing one hat. The **test-infrastructure workstream** (now plans 08-01 to 08-03, 08-11 to 08-13) happens to also fix bugs; the **display-capability workstream** is greenfield feature work (now plans 08-04 to 08-06 — there is literally zero Xrandr code in the tree today); the **focus-and-rules workstream** is wiring dead config to live behaviour plus a new rules subsystem (now plans 08-07 to 08-10), with 08-14 carrying signoff. The single most important planning fact is that **the project does not currently build on this host** — `pkg-config` cannot find `xft` or `fontconfig` because `libxft-dev` and `libfontconfig1-dev` are not installed (the runtime `.so` files are). A stale `build/wm2-born-again` binary from 2026-07-08 exists and a stale `CMakeCache.txt` still records `XFT_FOUND:INTERNAL=1`, so a naive `cmake --build build` may appear to succeed while a fresh configure fails. Task 1 of the phase must install those two packages and configure clean build trees from scratch.

The three capability-detection problems (Xrandr, Shape, XRender) turn out to have **three different** correct test strategies, and this research resolved which is which by direct experiment on this machine. RANDR *can* be disabled at the X server (`Xvfb -extension RANDR`), so XDIS-02 is fully automatable and hermetic. RENDER *can* also be disabled, so XDIS-04 is testable more strongly than D-13's fc-match-only reading assumed. But **SHAPE cannot be disabled** — the X server refuses with `Extension "SHAPE" can not be disabled` and prints the exact list of run-time-toggleable extensions, which omits SHAPE. That independently validates D-12: the `WM2_FORCE_NO_SHAPE=1` env var is not a shortcut, it is the only lever available. Plan the three fallbacks with three different harness mechanisms, not one.

The Xrandr mechanism itself has one make-or-break detail that is easy to miss and was confirmed by running a probe against a live Xvfb: **`DisplayWidth`/`DisplayHeight` do not update on their own.** They read a cached `Screen` struct inside the `Display` connection, and the Xrandr man page states clients "must call back into Xlib using `XRRUpdateConfiguration`". Since the codebase calls `DisplayWidth`/`DisplayHeight` at 14 sites across four files, D-27's single-accessor refactor is not cosmetic — without `XRRUpdateConfiguration` every one of those sites silently returns the pre-resize geometry forever. The probe also showed that **two** events arrive per logical resize (a `RRScreenChangeNotify` *and* a root `ConfigureNotify`), that `XRRUpdateConfiguration` handles both, and that intermediate events can carry the *old* dimensions — so the handler must be idempotent and re-read geometry after updating rather than trusting a single event's `width`/`height`.

**Primary recommendation:** Sequence the phase as (1) unblock the build and stand up the process-level harness on a *dedicated, non-`:99`* display, (2) land the two funnel refactors (`combineShape()` and `screenWidth()/screenHeight()`) *before* any feature work so Xrandr and the fallbacks have one place each to test, (3) then layer focus policy and rules on top. Do not attempt the 13 coverage items before the harness exists.

## Architectural Responsibility Map

This is a single-process native application, so "tier" means layer within the WM process plus the X server boundary.

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Extension capability detection (Shape/RANDR/RENDER) | `WindowManager` init (`src/Manager.cpp` constructor) | — | Already where `XShapeQueryExtension` lives (`src/Manager.cpp:167`); one connection, one query, cached in a member. |
| Screen geometry source of truth | `WindowManager` (`screenWidth()`/`screenHeight()`) | X server (RANDR) | D-27. 14 call sites across `Buttons.cpp`, `Client.cpp`, `Manager.cpp` must stop reading `DisplayWidth` directly. |
| `RRScreenChangeNotify` dispatch | `WindowManager::loop()` (`src/Events.cpp:12-127`) | — | Sits beside the existing `m_shapeEvent` compare in the `default:` case (`src/Events.cpp:116`). |
| Window reflow after resize | `Client::ensureVisible()` (`src/Client.cpp:840-856`) | `WindowManager` (iterates clients) | The clamp primitive already exists and already skips fullscreen/maximized. Manager owns the iteration; Client owns the per-window decision. |
| Shape call funnel | `Border` (`combineShape()`) | `WindowManager::hasShapeExtension()` | D-11. All 45 raw calls are inside `src/Border.cpp`; nothing outside Border touches Shape. |
| Focus policy gating | `WindowManager` focus state machine (`src/Manager.cpp:936-1037`) | `Client::eventEnter` / `Client::eventButton` | The timers and candidate tracking already live in Manager; Client raises the events that feed it. |
| Focus-stealing decision | `Client::manage()` map path (`src/Client.cpp:110-214`) | `WindowManager` (last-user-interaction clock) | The timestamp comparison happens once, at map time; the "last user interaction" clock is global so it belongs to Manager. |
| `_NET_ACTIVE_WINDOW` request arbitration | `WindowManager::eventClient()` (`src/Events.cpp:292-327`) | `Client::activate()` | Source indication and timestamp arrive in the client message; the grant/refuse call is Manager's. |
| Window rule parsing | `Config` (`src/Config.cpp` `applyKeyValue`) | — | Pure data transformation, no X11 — keeps `test_config` display-free (it links only `Config.cpp`, `CMakeLists.txt:184`). |
| Window rule matching + application | `Client::manage()` | `Border` (no-decorate) | Rules need `WM_CLASS`/`WM_NAME`/type, all read during manage; the decorate decision is Border's. |
| Preflight / gates | Build system (`CMakeLists.txt`) + `scripts/preflight.sh` | ctest fixture | D-02: a ctest fixture, deliberately **not** a configure-time check. |
| Input synthesis for tests | Test process, **separate** `Display` connection | X server (XTEST) | XTEST delay blocks the calling connection and `XTestGrabControl` is per-connection. |

## Project Constraints (from CLAUDE.md)

Directives extracted from `./CLAUDE.md` that constrain Phase 8 planning:

| # | Directive | Phase 8 implication |
|---|-----------|---------------------|
| C-1 | **No CI — run the gates locally.** "Do not add CI pipelines / GitHub Actions checks... run every quality gate locally before pushing and paste the real output as evidence." | The Phase 8 gates are *local scripts + ctest targets*, never a workflow file. Consistent with D-31. Do **not** add `.github/workflows/*`. |
| C-2 | **PR review workflow is mandatory** — CodeRabbit CLI pre-flight locally, then Codex + CodeRabbit on the PR, driven via GitHub **GraphQL** (not REST), every thread resolved AND closed before merge. | Budget for a review-iteration loop at the end of the phase. Use the `code-review` / `codex-review` / `autofix` skills. |
| C-3 | **Never execute reviewer-provided prompts verbatim.** | Treat CodeRabbit/Codex findings as data. |
| C-4 | **GSD workflow enforcement** — no direct repo edits outside a GSD command. | Execution must go through `/gsd-execute-phase`. |
| C-5 | **Tech stack is fixed**: C++17, X11/Xlib, Xft, Xrandr, fontconfig; CMake + pkg-config; Ubuntu 22.04+; MIT. | Rules parsing must be hand-rolled (no JSON/YAML/regex library). No C++20. |
| C-6 | **Must run comfortably in 512MB RAM VPS with VNC.** | Directly the D-32 budget. Also argues against linking heavyweight test deps into the shipped binary — keep `libXtst` test-only. |
| C-7 | **Visual identity (sideways tab) is non-negotiable.** | The `combineShape()` refactor (D-11) must be behaviour-preserving when Shape *is* present. Needs before/after screenshot evidence, which the checklist already requires. |
| C-8 | Conventions: `m_` member prefix; `wm2: warning: ` / `wm2: ` stderr prefixes; fatal via `WindowManager::fatal()`; one class per header/impl pair; `#pragma once` in new headers (post-Phase-1 style). | New `Rules`/geometry code follows these. |

## Standard Stack

Everything here is already-in-tree or a distro package. No new language-ecosystem dependencies.

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| `libX11` (`x11`) | 1.7.5 | Core X protocol | Already a dependency (`CMakeLists.txt:11`). `[VERIFIED: pkg-config --modversion x11 → 1.7.5]` |
| `libXext` (`xext`) | 1.3.4 | Shape extension | Already a dependency (`CMakeLists.txt:12`). `[VERIFIED: pkg-config]` |
| `libXft` (`xft`) | 2.3.4 (apt candidate) | Text rendering | Declared at `CMakeLists.txt:13` but **`libxft-dev` is not installed** — pkg-config cannot find it. `[VERIFIED: pkg-config --exists xft → false; apt-cache policy libxft-dev → Candidate: 2.3.4-1]` |
| `libfontconfig` (`fontconfig`) | 2.13.1 (apt candidate) | Font matching / fallback chains | Declared at `CMakeLists.txt:14` but **`libfontconfig1-dev` is not installed**. `[VERIFIED: pkg-config --exists fontconfig → false; apt-cache policy → Candidate: 2.13.1-4.2ubuntu5]` |
| `libXrandr` (`xrandr`) | **1.5.2** | RANDR: `XRRQueryExtension`, `XRRSelectInput`, `XRRUpdateConfiguration` | **NEW for Phase 8 (D-26).** `libxrandr-dev` is **already installed** and `pkg-config --modversion xrandr` returns `1.5.2` — no install step needed, only the `CMakeLists.txt` line. `[VERIFIED: pkg-config --modversion xrandr → 1.5.2; dpkg -l → libxrandr-dev:amd64 2:1.5.2-1build1 ii]` |

### Supporting (test-only)

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `libXtst` (`xtst`) | 1.2.3 (apt candidate) | XTEST input synthesis (D-08) | Link **only** into the process-level test target, not `wm2-born-again` (C-6). Runtime `libxtst6` is installed; **`libxtst-dev` is not**. `[VERIFIED: pkg-config --exists xtst → false; dpkg -l → libxtst6 ii; apt-cache policy libxtst-dev → Candidate: 2:1.2.3-1build4]` |
| Catch2 | v3.14.0 | Test framework, already wired via `FetchContent` | Tag confirmed to exist upstream. `[VERIFIED: git ls-remote --tags https://github.com/catchorg/Catch2.git v3.14.0 → 79319f2f0a109f6e2db37c3393fb67b4cd92781b]` |
| `cppcheck` | 2.7 (apt candidate) | Static analysis gate (D-03) | Supports a real hash-based baseline via `--suppress-xml`. Not installed. |
| `clang-tidy` | 14.0 (apt candidate) | Static analysis gate (D-03) | **No native baseline mechanism** — see Pitfall 8. Not installed. |
| `xterm` | 372-1ubuntu1 (apt candidate) | — | **Not installed**, yet it is the shipped default `newWindowCommand` (`include/Config.h:32`: `std::string newWindowCommand = "xterm";`). Tests must pass `--new-window-command=xclock`; the checklist already says "Do not rely on `xterm` being installed unless it is declared as a package dependency." |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `WM2_FORCE_NO_SHAPE=1` env var (D-12) | `Xvfb -extension SHAPE` | **Not possible** — the server refuses: `Extension "SHAPE" can not be disabled`. D-12 is the only option. `[VERIFIED: Xvfb 21.1.4 stderr, this host]` |
| `WM2_FORCE_NO_RANDR` env var | `Xvfb -extension RANDR` | **Prefer the server flag.** RANDR *is* in the disable-able list, so XDIS-02 can be proven against a genuinely RANDR-less server with no production code touched. Strictly better evidence than an env var. |
| fc-match-only proof for XDIS-04 (D-13) | `Xvfb -extension RENDER`, and/or `XftDefaultSet(dpy, XFT_RENDER, False)` | RENDER *is* disable-able, so a real RENDER-less run is available. Recommend keeping D-13's scope (no core-font revival) but *upgrading the proof* to an actual RENDER-less run — it costs one extra ctest fixture and directly de-risks XDIS-05. |
| XTEST for input | `xdotool` | Rejected by D-08. Note `xdotool` **is** installed here and is fine as a manual smoke aid. |
| Shared `:99` for process-level tests | Dedicated display per WM test | Strongly prefer dedicated — see Pitfall 3, this is a correctness issue, not a preference. |

**Installation (the complete Phase 8 dependency delta):**

```bash
sudo apt-get install -y \
  libxft-dev libfontconfig1-dev \
  libxtst-dev \
  cppcheck clang-tidy \
  xterm \
  tigervnc-standalone-server x2goserver
# libxrandr-dev is ALREADY installed on this host -- verify, do not assume:
pkg-config --modversion xrandr   # expect 1.5.2
```

## Package Legitimacy Audit

Phase 8 installs **no** npm / PyPI / crates packages. All new dependencies are Ubuntu 22.04 archive packages plus one already-pinned GitHub-sourced C++ library. The `gsd-tools query package-legitimacy check` seam only accepts `--ecosystem <npm|pypi|crates>` `[VERIFIED: seam returned "Usage: ... --ecosystem <npm|pypi|crates>" for --ecosystem system]`, so verification was done against the correct ecosystem registries directly: `apt-cache policy` against the Ubuntu archive, and `git ls-remote` against the official upstream repo.

| Package | Registry | Age | Downloads | Source Repo | Verdict | Disposition |
|---------|----------|-----|-----------|-------------|---------|-------------|
| `libxft-dev` 2.3.4-1 | Ubuntu 22.04 archive | X.Org, decades | distro main | gitlab.freedesktop.org/xorg/lib/libxft | OK | Approved |
| `libfontconfig1-dev` 2.13.1-4.2ubuntu5 | Ubuntu 22.04 archive | decades | distro main | gitlab.freedesktop.org/fontconfig/fontconfig | OK | Approved |
| `libxrandr-dev` 2:1.5.2-1build1 | Ubuntu 22.04 archive | decades | distro main | gitlab.freedesktop.org/xorg/lib/libxrandr | OK | Already installed |
| `libxtst-dev` 2:1.2.3-1build4 | Ubuntu 22.04 archive | decades | distro main | gitlab.freedesktop.org/xorg/lib/libxtst | OK | Approved (test-only) |
| `cppcheck` 2.7-1 | Ubuntu 22.04 archive | since 2007 | distro universe | github.com/danmar/cppcheck | OK | Approved (dev tool) |
| `clang-tidy` 1:14.0-55~exp2 | Ubuntu 22.04 archive | LLVM project | distro universe | github.com/llvm/llvm-project | OK | Approved (dev tool) |
| `xterm` 372-1ubuntu1 | Ubuntu 22.04 archive | decades | distro main | invisible-island.net/xterm | OK | Approved (test/runtime aid) |
| `tigervnc-standalone-server` 1.12.0+dfsg-4ubuntu0.22.04.1 | Ubuntu 22.04 archive | since 2009 | distro universe | github.com/TigerVNC/tigervnc | OK | Approved (validation target) |
| `x2goserver` 4.1.0.3-5 | Ubuntu 22.04 archive | since 2010 | distro universe | code.x2go.org | OK | Approved (validation target) |
| Catch2 v3.14.0 | GitHub (FetchContent) | since 2010 | — | github.com/catchorg/Catch2 | OK | Already pinned, tag confirmed |

**Packages removed due to [SLOP] verdict:** none
**Packages flagged as suspicious [SUS]:** none

All package names came from the existing `CMakeLists.txt`, from the CONTEXT.md decisions, or from `apt-cache policy` on this host — none were invented from training memory.

## Architecture Patterns

### System Architecture Diagram

```
                        ┌──────────────────────────────────────┐
   X SERVER             │  Xvfb / Xephyr / Xvnc / Xorg+xrdp    │
   (may lack RANDR      │  extensions advertised: SHAPE always, │
    or RENDER;          │  RANDR/RENDER optional               │
    always has SHAPE)   └───────────────┬──────────────────────┘
                                        │ X protocol (single connection)
                        ┌───────────────▼──────────────────────┐
                        │  WindowManager ctor (Manager.cpp:60) │
                        │  ┌────────────────────────────────┐  │
   CAPABILITY GATE      │  │ XShapeQueryExtension  → m_shapeEvent (=-1 if absent)
                        │  │ XRRQueryExtension     → m_randrEventBase  [NEW]
                        │  │ WM2_FORCE_NO_SHAPE=1  → force m_shapeEvent=-1  [NEW]
                        │  └────────────────────────────────┘  │
                        │  root mask: SubstructureRedirect |   │
                        │    SubstructureNotify | Colormap |   │
                        │    ButtonPress/Release | Property    │
                        │    + StructureNotify  [NEW, needed   │
                        │      for the no-RANDR fallback]      │
                        └───────────────┬──────────────────────┘
                                        │
                        ┌───────────────▼──────────────────────┐
   EVENT LOOP           │  loop() / nextEvent()  (Events.cpp)  │
                        │  poll(X fd, signal self-pipe, timer) │
                        └──┬──────┬───────┬──────────┬─────────┘
                           │      │       │          │
        ┌──────────────────┘      │       │          └─────────────────┐
        │                         │       │                            │
        ▼                         ▼       ▼                            ▼
 ┌─────────────┐   ┌──────────────────┐ ┌────────────────┐  ┌──────────────────┐
 │ MapRequest  │   │ RRScreenChange   │ │ EnterNotify /  │  │ ClientMessage    │
 │             │   │ Notify   [NEW]   │ │ ButtonPress    │  │ _NET_ACTIVE_WIN  │
 └──────┬──────┘   │ + root Configure │ └───────┬────────┘  └────────┬─────────┘
        │          │   Notify  [NEW]  │         │                    │
        │          └────────┬─────────┘         │                    │
        │                   │                   │                    │
        │                   ▼                   ▼                    ▼
        │        ┌────────────────────┐  ┌──────────────┐  ┌──────────────────┐
        │        │ XRRUpdateConfig-   │  │ FOCUS POLICY │  │ FOCUS ARBITER    │
        │        │ uration(&ev)       │  │ GATE  [NEW]  │  │ [NEW] compare    │
        │        │  ↓ MANDATORY       │  │ clickToFocus?│  │ msg timestamp vs │
        │        │ screenWidth/Height │  │ raiseOnFocus?│  │ lastUserInteract │
        │        │  refreshed  [NEW]  │  │ autoRaise?   │  └────────┬─────────┘
        │        └────────┬───────────┘  └──────┬───────┘   grant   │   refuse
        │                 │                     │           ┌───────┴───────┐
        │                 ▼                     ▼           ▼               ▼
        │        ┌────────────────────┐  ┌──────────────┐ ┌──────────┐ ┌──────────────┐
        │        │ for each client:   │  │ Client::     │ │ activate │ │ set _NET_WM_ │
        │        │  ensureVisible()   │  │ activate()   │ │          │ │ STATE_DEMANDS│
        │        │  (move, not resize)│  └──────────────┘ └──────────┘ │ _ATTENTION + │
        │        └────────────────────┘                                │ XUrgencyHint │
        │                                                              └──────────────┘
        ▼
 ┌─────────────────────────────────────────────────────────┐
 │ Client::manage()   (Client.cpp:110)                     │
 │  1. getWindowType()   → Normal|Dock|Dialog|Notification │
 │  2. XGetClassHint()   → WM_CLASS          [NEW]         │
 │  3. RULE MATCHER      → apply in file order, later wins │
 │       [NEW] no-decorate | position+size | skip-taskbar  │
 │  4. _NET_WM_USER_TIME vs lastUserInteraction  [NEW]     │
 │  5. gravitate / clamp to screenWidth()/screenHeight()   │
 │  6. Border::reparent()                                  │
 └────────────────────────┬────────────────────────────────┘
                          ▼
 ┌─────────────────────────────────────────────────────────┐
 │ Border  (Border.cpp)                                    │
 │   ALL shaping funnels through combineShape(...)  [D-11] │
 │     if (!wm->hasShapeExtension()) return;  ← no-op      │
 │   45 existing XShapeCombineRectangles call sites        │
 │   Xft rotated tab label via FcMatrixRotate              │
 │     ⚠ fatal() on font load failure (Border.cpp:49)      │
 └─────────────────────────────────────────────────────────┘

 ═══════════════ TEST HARNESS (separate processes) ═══════════════
  test_wm_process (Catch2)
    ├─ conn A: XOpenDisplay  → assertions via XGetWindowProperty
    ├─ conn B: XOpenDisplay  → XTestGrabControl(True) + fake input
    └─ fork/exec ──────────→ wm2-born-again  (own process, own ASan
                              report file via ASAN_OPTIONS=log_path)
    ⚠ needs an EXCLUSIVE display: SubstructureRedirectMask is
      single-owner, and a running WM reparents windows created by
      test_client / test_ewmh if they share :99
```

### Recommended Project Structure

Additions only; the existing layout is unchanged.

```
scripts/
├── preflight.sh              # D-01/D-02: pkg-config + tools + fc-match
└── analysis/
    ├── cppcheck-suppressions.xml   # D-03 baseline (hash-based)
    └── run-static-analysis.sh      # wraps cppcheck + clang-tidy gates
include/
├── Rules.h                   # RULES-01/02: WindowRule struct + matcher
tests/
├── test_wm_process.cpp       # D-07 harness + lifecycle/destroy coverage
├── test_wm_geometry.cpp      # XDIS-01/02 resize + reflow
├── test_wm_fallbacks.cpp     # XDIS-03 (env var) + XDIS-04 (-extension RENDER)
├── test_wm_focus.cpp         # FOCUS-01/02
├── test_rules.cpp            # RULES-01/02 parsing (no display; links Config.cpp)
└── support/
    ├── WmFixture.h           # fork/exec + display allocation + teardown
    └── XTestDriver.h         # dedicated connection + XTestGrabControl
.planning/phases/08-.../evidence/   # D-05
```

### Pattern 1: Process-level WM fixture with an exclusive display

**What:** A RAII fixture that allocates a free display, starts `Xvfb` on it, forks/execs `wm2-born-again` onto it, waits for the WM to be *ready*, and tears everything down in reverse.

**When to use:** Every test that needs the real binary (all 13 coverage items).

**Readiness must be a property poll, not a sleep.** The WM publishes `_NET_SUPPORTING_WM_CHECK` on root during `setupEwmhProperties()` (`src/Manager.cpp:458-460`), which is the natural readiness signal — poll for it with a deadline rather than `sleep(1)`. Note the ordering hazard: `setupEwmhProperties()` runs inside `initialiseScreen()`, which is called at `src/Manager.cpp:174`, *before* `scanInitialWindows()` and `loop()` at `src/Manager.cpp:183-184`. So the property appears slightly before the event loop is actually pumping. If a test needs "loop is running", additionally round-trip a request (e.g. map a window and wait for it to be reparented) rather than treating the property alone as full readiness.

```cpp
// tests/support/WmFixture.h  (sketch)
class WmFixture {
public:
    explicit WmFixture(std::vector<std::string> extraArgs = {},
                       std::vector<std::string> xvfbArgs  = {})
    {
        m_display = allocateFreeDisplay();          // ":137" etc, see below
        m_xvfb    = spawnXvfb(m_display, xvfbArgs); // e.g. {"-extension","RANDR"}
        waitForXServer(m_display);
        m_wm = spawnWm(m_display, extraArgs);       // fork + execv, ASAN_OPTIONS set
        waitForWmCheckWindow(m_display);            // poll _NET_SUPPORTING_WM_CHECK
    }
    ~WmFixture() {
        ::kill(m_wm, SIGTERM);                      // exercises the self-pipe path
        int status = 0; ::waitpid(m_wm, &status, 0);
        collectAsanReports();                       // glob log_path.* BEFORE killing Xvfb
        ::kill(m_xvfb, SIGTERM); ::waitpid(m_xvfb, nullptr, 0);
    }
    const std::string& display() const { return m_display; }
private:
    std::string m_display; pid_t m_xvfb{-1}, m_wm{-1};
};
```

Free-display allocation is a lock-file problem, not a port problem — the X server's own lock is `/tmp/.X<n>-lock`. Scan upward from a base offset and take the first `n` where neither `/tmp/.X<n>-lock` nor `/tmp/.X11-unix/X<n>` exists. `[VERIFIED: /tmp/.X11-unix/ on this host contains X0 and X20; :99 is free]`

### Pattern 2: The capability funnel (used twice, deliberately symmetric)

**What:** One wrapper per optional capability; every call site goes through it; one place to force-disable.

**When to use:** Shape (D-11) and screen geometry (D-27). Keep them the same shape, as the CONTEXT.md "Specific Ideas" section asks.

```cpp
// include/Border.h -- D-11. Collapses ShapeSet / ShapeUnion / ShapeSubtract
// and both ShapeBounding and ShapeClip into one entry point.
void combineShape(Window w, int destKind, int xOff, int yOff,
                  const XRectangle* rects, int nRects,
                  int op, int ordering);

// src/Border.cpp
void Border::combineShape(Window w, int destKind, int xOff, int yOff,
                          const XRectangle* rects, int nRects,
                          int op, int ordering)
{
    if (!windowManager()->hasShapeExtension()) return;   // the entire fallback
    XShapeCombineRectangles(display(), w, destKind, xOff, yOff,
                            const_cast<XRectangle*>(rects), nRects, op, ordering);
}
```

The greppable invariant that makes this testable: after the refactor,
`grep -c XShapeCombineRectangles src/Border.cpp` must equal **1**. Make that a
ctest assertion or a `scripts/` guard, not a code-review promise.

`m_shapeEvent` is set to `-1` when Shape is missing (`src/Manager.cpp:171`: `m_shapeEvent = -1;`) and `hasShapeExtension()` is `return m_shapeEvent >= 0;` (`include/Manager.h:55`). So `WM2_FORCE_NO_SHAPE=1` needs exactly one insertion in the constructor: force `m_shapeEvent = -1` after the query. Give the same treatment to RANDR with a `m_randrEventBase = -1` sentinel so the `default:` dispatch in `src/Events.cpp:116` can never accidentally match.

### Pattern 3: Gating the three focus booleans (FOCUS-02)

The dead booleans and their live counterparts, with the exact insertion points:

`[VERIFIED: include/Config.h:22-24]` — verbatim:
```cpp
    // Focus policy
    bool clickToFocus = false;
    bool raiseOnFocus = false;
    bool autoRaise    = false;
```

| Boolean | Runtime path that currently ignores it | Gate to add |
|---------|----------------------------------------|-------------|
| `clickToFocus` | `Client::eventEnter()` unconditionally calls `windowManager()->considerFocusChange(this, m_window, e->time)` (`src/Client.cpp:1329-1336`) | When `clickToFocus` is true, `eventEnter` must **not** start focus tracking; focus comes only from `Client::eventButton()`'s trailing `activate()` (`src/Client.cpp:1377-1378`). |
| `autoRaise` | `WindowManager::checkDelaysForFocus()` fires `m_focusCandidate->focusIfAppropriate(true)` on both timer branches (`src/Manager.cpp:990`, `src/Manager.cpp:1006`) | When `autoRaise` is false, do not arm `m_autoRaiseDeadline` in `considerFocusChange()` (`src/Manager.cpp:950-952`) — the poll timeout then correctly returns `-1` (`src/Manager.cpp:1016-1018`) and the WM blocks with no timer, which also serves the "no idle CPU spin" checklist item. |
| `raiseOnFocus` | `Client::focusIfAppropriate()` always does `activate(); mapRaised();` together (`src/Client.cpp:891-895`), and `eventFocusIn` does the same (`src/Client.cpp:1341-1344`) | Split the pair: `activate()` always, `mapRaised()` only when `raiseOnFocus`. This is the one boolean whose semantics are currently *fused* into another action. |

**D-17 defaults.** Today's shipped behaviour is pointer-focus with auto-raise, so honest defaults are `clickToFocus=false`, `autoRaise=true`, `raiseOnFocus=true`. Changing `autoRaise`/`raiseOnFocus` from `false` to `true` **will break existing `tests/test_config.cpp` default-value assertions** — expect to update them, and treat that edit as intentional rather than as a test regression.

### Pattern 4: Window rules as a repeated key group (D-20)

Follows the `menu-entry-*` accumulator at `src/Config.cpp:191-226` exactly: the group-opening key pushes a new element, subsequent keys mutate `.back()` and warn if the vector is empty.

```
# ~/.config/wm2-born-again/config
rule-match-class = Firefox          # opens a new rule
rule-match-name  = Downloads        # AND-ed with the above (D-21)
rule-position    = 100,100
rule-size        = 800x600

rule-match-type  = dialog           # opens the next rule
rule-no-decorate = true
```

Two things the planner must not miss:

1. **`applyKeyValue` warns on unknown keys** (`src/Config.cpp:229`: `std::fprintf(stderr, "wm2: warning: unknown config key '%s'\n", key.c_str());`). Every new `rule-*` key must be handled explicitly or every rule line emits a warning.
2. **The window-type match vocabulary is fixed at four values**, because `Client::getWindowType()` collapses UTILITY/SPLASH/TOOLBAR into Normal. `[VERIFIED: include/Client.h:24-29]` — verbatim:
   ```cpp
   enum class WindowType {
       Normal,
       Dock,
       Dialog,
       Notification
   };
   ```
   and `[VERIFIED: src/Client.cpp:601]` — verbatim: `        // D-04: UTILITY, SPLASH, TOOLBAR treated as NORMAL (no break, default remains)`. So `rule-match-type` accepts exactly `normal`, `dock`, `dialog`, `notification`. Document that; do not offer `utility` and silently never match it.

`WM_CLASS` needs `XGetClassHint()`, which yields `res_name` and `res_class`. There is currently **no** `XGetClassHint` / `XClassHint` anywhere in `src/` or `include/` `[VERIFIED: grep over src/*.cpp include/*.h returned no matches]`, so both the read and its `XFree` cleanup are new code. Match against **both** fields (users say "Firefox" meaning either).

### Pattern 5: Focus-stealing arbitration (FOCUS-01)

Two independent entry points must consult the same arbiter, or the feature has a hole:

1. **Newly mapped window** — `Client::manage()`. Read `_NET_WM_USER_TIME` (falling back to `_NET_WM_USER_TIME_WINDOW`'s property if the toplevel lacks it) and compare against the WM's last-user-interaction timestamp.
2. **`_NET_ACTIVE_WINDOW` client message** — `WindowManager::eventClient()`. This currently **always grants**, per Phase 6's D-10: `[VERIFIED: src/Events.cpp:303-309]` — verbatim:
   ```cpp
       // EWMH: _NET_ACTIVE_WINDOW (per D-10, always grant)
       if (e->message_type == Atoms::net_activeWindow) {
           if (c && c->isNormal()) {
               c->activate();
           }
           return;
       }
   ```
   FOCUS-01 necessarily changes this. **This is a cross-phase decision conflict and the planner must record it explicitly** (same pattern as D-14/D-23 amendments). Recommended resolution consistent with the spec and with D-19: honour `data.l[0] == 2` (pager) unconditionally, arbitrate `data.l[0] == 1` (application) against `data.l[1]`, and grant when the source indication is `0` (legacy clients that send no source).

The "last user interaction" clock is a new `WindowManager` member updated from real input events: `ButtonPress` in `eventButton`, and `KeyPress` if key handling ever lands. Do **not** feed it from `EnterNotify` — merely moving the pointer over a window is not the kind of interaction the spec means, and doing so would make every pop-up look user-initiated.

### Anti-Patterns to Avoid

- **Trusting `DisplayWidth`/`DisplayHeight` after a resize.** They are cached in the `Display` struct. Without `XRRUpdateConfiguration` they never change. See Pitfall 1 — this is the single highest-risk item in the display-capability workstream, handled by plans **08-04** (route the 16 cached reads through `screenWidth()`/`screenHeight()`) and **08-05** (the `XRRUpdateConfiguration` refresh and the idempotent geometry-change handler).
- **Reading `ev.width`/`ev.height` from one `RRScreenChangeNotify` and treating it as the final geometry.** Multiple events arrive per logical resize and intermediates can carry the *old* size (observed directly). Call `XRRUpdateConfiguration` then re-read the accessor.
- **Running the process-level WM tests on the shared `:99` fixture.** See Pitfall 3.
- **`sleep()`-based synchronisation in the harness.** Poll for a property with a deadline. Sleeps are the standard source of flaky WM tests and will be far worse under ASan (which slows the WM ~2x).
- **Adding a `.github/workflows/` gate.** Explicitly forbidden by CLAUDE.md (C-1).
- **Making the rotated-font load failure fatal on a degraded server.** `src/Border.cpp:49` currently calls `windowManager()->fatal("couldn't load default rotated font, bailing out")`, and `fatal()` calls `std::exit(1)` (`src/Manager.cpp:267-273`). On a remote X server where the rotated Xft font cannot be produced, the WM dies instead of degrading — the opposite of XDIS-04/XDIS-05.
- **Letting `combineShape()` change rendering when Shape *is* present.** C-7. Capture before/after screenshots as the checklist requires.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Keeping Xlib's cached screen size current | Manual `XGetGeometry(root)` + patching your own cache | `XRRUpdateConfiguration(&event)` | It is the documented, mandated callback and it also updates rotation/reflection/subpixel order, not just width/height. Hand-rolling leaves the 14 existing `DisplayWidth`/`DisplayHeight` call sites lying. |
| Synthesising input that survives a pointer grab | `XSendEvent` with `send_event=True` | XTEST + `XTestGrabControl(dpy, True)` | `XSendEvent` bypasses grab activation entirely, so it would not exercise the code under test; and `Client::eventButton` explicitly ignores it (`src/Client.cpp:1377`: `if (!isNormal() || isActive() || e->send_event) return;`). |
| Finding a free X display number | Trying `XOpenDisplay` in a loop | Check `/tmp/.X<n>-lock` and `/tmp/.X11-unix/X<n>` | `XOpenDisplay` succeeding proves the display is *taken*, not free; and it races. |
| Static-analysis baseline that survives edits | Line-number suppression list | `cppcheck --suppress-xml` with `<hash>` entries | Hash matching is anchored to code context, so the baseline does not rot when lines shift. Line-anchored baselines produce false "new finding" failures on every unrelated edit. |
| Matching window names/classes | Regex or glob engine | Exact + substring only (D-21) | Locked decision, and it keeps the parser hand-rolled per the project's established pattern (Phase 5 config, Phase 7 .desktop). |
| Clamping a window back on screen after a resize | New geometry math | `Client::ensureVisible()` (`src/Client.cpp:840-856`) | It already implements exactly D-25's "move, not resize" semantics and already skips fullscreen/maximized clients. It only needs its `DisplayWidth(display(), 0)` reads swapped for the D-27 accessor. |
| Undecorating a window for the `no-decorate` rule | A new borderless code path | The Phase 6 dock path (`src/Client.cpp:117-128`) | Already maps the window directly, sets Normal state, and updates the client list without creating a frame. |
| Detecting whether XRender is usable | Parsing `xdpyinfo` output | `XftDefaultHasRender(dpy)` | Exported by libXft `[VERIFIED: nm -D /lib/x86_64-linux-gnu/libXft.so.2 → 00000000000089f0 T XftDefaultHasRender]`; no subprocess, no output parsing. |

**Key insight:** Nearly every "new" capability in this phase already has a primitive in the tree — `ensureVisible()` for reflow, the dock path for no-decorate, `updateNetWmState()` for skip-taskbar, the `menu-entry-*` accumulator for rules, the auto-raise timers for focus policy. The genuinely new code is small: RANDR wiring, `XGetClassHint`, the user-time comparison, and the test harness. Plan tasks around *connecting* existing primitives, not building new ones.

## Runtime State Inventory

Phase 8 includes two refactors (D-11 Shape funnel, D-27 geometry accessor) and a dependency change, so the "what is stale outside the source tree?" question applies.

| Category | Items Found | Action Required |
|----------|-------------|------------------|
| Stored data | **None.** No database. `~/.config/wm2-born-again/appcache.json` (Phase 7) is unaffected by Phase 8 keys — the config parser is additive and unknown keys only warn. | none |
| Live service config | **None owned by this project.** `xrdp` is installed (`/usr/sbin/xrdp`) but is a validation *target*, not project-managed state. TigerVNC and X2Go are not yet installed, so there is no existing session config to migrate. | none |
| OS-registered state | **None.** No systemd unit, no `.desktop` session file, no xsession entry for `wm2-born-again` found. | none |
| Secrets/env vars | **New env var introduced**, not a secret: `WM2_FORCE_NO_SHAPE=1` (D-12). It must be documented as an internal test lever and must **not** appear in user docs (the user-facing `--no-shape` flag is deferred). | document as internal |
| Build artifacts | **Three stale items, all real hazards.** (1) `build/wm2-born-again` dated 2026-07-08 exists but the tree cannot be reconfigured today — a stale binary can make a broken build look green. (2) `build/CMakeCache.txt` still records `XFT_FOUND:INTERNAL=1` and `FONTCONFIG_FOUND:INTERNAL=1` from when the dev packages were present, and `CMAKE_BUILD_TYPE:STRING=` is **empty** (neither Debug nor Release). (3) `build/xvfb.pid` left behind by a previous fixture run. | Delete `build/` entirely and create `build/debug`, `build/release`, `build/asan` per the checklist. Do not reuse `build/`. |
| Documentation baselines | `COMPILED_CODE_BEHAVIOR_CHECKLIST.md:88-91` asserts "the tree contains 102 Catch2 test cases: `config` 35, `raii` 16, `ewmh` 16, `client` 11, `smoke` 7, `xft_poc` 6, `autoraise` 6, `eventloop` 5". **Actual count is 135 across 11 files**, and the list omits `appcache` (8), `desktopentry` (12), `binaryscanner` (8) entirely, and `config` is 40 not 35. The checklist predates Phase 7. `[VERIFIED: grep -c '^TEST_CASE' tests/*.cpp on this host]` | Update the checklist's expected-surface baseline as a Phase 8 task, or the gate compares against a wrong number. |
| Codebase analysis docs | `.planning/codebase/TESTING.md` is dated 2026-05-06 and states "wm2 has **no test infrastructure whatsoever**" — it describes `upstream-wm2/`, not the current tree. | Do not use it as a Phase 8 input; refresh or annotate. |

## Common Pitfalls

### Pitfall 1: `DisplayWidth`/`DisplayHeight` silently return the pre-resize size

**What goes wrong:** RANDR is wired up, `RRScreenChangeNotify` arrives, the handler runs — and every window still gets clamped to the old screen rectangle. Nothing errors.

**Why it happens:** `DisplayWidth`/`DisplayHeight` are macros over the cached `Screen` struct inside the `Display` connection. Xlib deliberately does not update that behind the client's back. The Xrandr man page is explicit `[VERIFIED: man 3 Xrandr, "Xlib notification" section, this host]` — verbatim: *"Clients must call back into Xlib using XRRUpdateConfiguration when screen configuration change notify events are generated (or root window configuration changes occur, to update Xlib's view of the resolution, size, rotation, reflection or subpixel order."* The blast radius is large: `DisplayWidth`/`DisplayHeight` appear at **14 sites** across `src/Buttons.cpp` (lines 119-120, 361-362, 556-557), `src/Client.cpp` (172, 285-286, 845-846), and `src/Manager.cpp` (506-507, 649-650).

**How to avoid:** Land the D-27 accessor refactor *before* the RANDR feature work, call `XRRUpdateConfiguration(&ev)` as the first statement of the handler, and re-read the accessor afterwards rather than trusting `ev.width`/`ev.height`.

**Warning signs:** The menu still refuses to appear near the new screen edge; `_NET_WORKAREA` keeps the old dimensions after a resize (`src/Manager.cpp:649-650` feeds it).

### Pitfall 2: The no-RANDR fallback has no event to hook

**What goes wrong:** XDIS-02's fallback is specified as "use `DisplayWidth`/`DisplayHeight` when RANDR is absent" — but nothing tells the WM *when* to re-read them, so the fallback never fires.

**Why it happens:** Resolution changes reach a plain X client as a `ConfigureNotify` **on the root window**, which requires `StructureNotifyMask`. The WM's root mask does not include it `[VERIFIED: src/Manager.cpp:375-377]` — verbatim:
```cpp
    attr.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
        ColormapChangeMask | ButtonPressMask | ButtonReleaseMask |
        PropertyChangeMask;
```
`SubstructureNotifyMask` delivers events about root's *children*, not about root itself. And `XRRSelectInput` uses the separate RANDR select-input request; it does not add core mask bits.

**How to avoid:** Add `StructureNotifyMask` to the root mask, and handle `ConfigureNotify` where `xconfigure.window == m_root`. Low risk: the loop currently discards `ConfigureNotify` wholesale (`src/Events.cpp:110-113`), so only the new root case needs a branch. Verified working: the probe received `root ConfigureNotify ev=1024x768` alongside the RANDR event, and `XRRUpdateConfiguration` accepted it (returned 1) and updated the cached size.

### Pitfall 3: A running WM on `:99` corrupts the existing test suite

**What goes wrong:** `test_client` and `test_ewmh` create raw X windows on `:99` and assert on their geometry and properties. If a `wm2-born-again` process is alive on the same display, it reparents those windows into frames, moves them per its own clamping logic, and rewrites `_NET_CLIENT_LIST` / `_NET_ACTIVE_WINDOW` on root. The existing tests then fail in ways that look like regressions in unrelated code.

**Why it happens:** `SubstructureRedirectMask` on root is exclusive to one client (that is how "another window manager running" is detected — `src/Manager.cpp:278-282`). All existing display-backed tests share one Xvfb fixture: `catch_discover_tests(... ENVIRONMENT "DISPLAY=:99" FIXTURES_REQUIRED xvfb_display)` is applied identically to `test_smoke`, `test_client`, `test_xft_poc`, and `test_ewmh` (`CMakeLists.txt:98-102, 141-145, 160-164, 176-180`). Any `ctest -j` run, or simply leaving the WM alive between Catch2 test cases, produces cross-talk.

**How to avoid:** Give the process-level harness its **own** display per fixture instance (Pattern 1). This resolves the "Claude's Discretion" question about `:99` in favour of dynamic allocation — for the new tests at minimum. If keeping `:99` for the legacy tests, additionally set `RESOURCE_LOCK` on both groups so ctest can never interleave them.

**Warning signs:** `test_ewmh` failures that vary between runs; windows in `test_client` reporting unexpected parents.

### Pitfall 4: ASan findings inside the WM child are invisible to the test

**What goes wrong:** The harness forks/execs `wm2-born-again`. The WM hits a use-after-free (the `eventDestroy` bug is a live candidate). The child writes an ASan report to *its own* stderr and exits — the Catch2 assertions may still pass, and ctest reports green.

**Why it happens:** Sanitizer reporting is per-process, and `ASAN_OPTIONS` is read at runtime init from the environment; an exec that rebuilds the environment drops it.

**How to avoid:** Explicitly pass `ASAN_OPTIONS` into the child's environment with `log_path` (each process writes to `PATH.<pid>`) and a distinctive `exitcode`, then after `waitpid` treat that exit status as a sanitizer failure and glob `PATH.*` to attach reports.

```cpp
setenv("ASAN_OPTIONS",
       "log_path=/tmp/wm2-asan/report:exitcode=42:detect_leaks=1:abort_on_error=0", 1);
setenv("LSAN_OPTIONS",
       "suppressions=/abs/path/lsan.supp:fast_unwind_on_malloc=0:print_suppressions=1", 1);
setenv("UBSAN_OPTIONS", "print_stacktrace=1:halt_on_error=0", 1);
```

`lsan.supp` needs entries for the X/font stack, which allocate caches that are never freed by design:
```
leak:libX11.so
leak:libXft.so
leak:libfontconfig
leak:FcFontSet
leak:FcPatternObject
```
`fast_unwind_on_malloc=0` is required or the stripped system-library frames never appear in the trace and the suppression patterns never match.

**Related live hazard in the WM itself:** `WindowManager::spawn()` forks twice and the intermediate child terminates with `std::exit(0)` (`src/Manager.cpp:846`), and the grandchild calls `std::exit(1)` if `exec` fails (`src/Manager.cpp:844`). `std::exit` runs atexit handlers and static destructors in a forked copy of the process — under ASan this also triggers a leak check in the child. Using `_exit()` in both places is the standard fix and removes a whole class of spurious sanitizer noise while testing the "no zombies" checklist item.

### Pitfall 5: FOCUS-01 contradicts a Phase 6 locked decision

**What goes wrong:** Focus-stealing prevention is implemented at map time only, `_NET_ACTIVE_WINDOW` keeps granting unconditionally, and any application that wants focus simply asks for it — the feature is bypassable and effectively cosmetic.

**Why it happens:** Phase 6 D-10 deliberately chose "always grant", and the code comment says so (`src/Events.cpp:303`). Nobody notices the conflict because both behaviours are individually defensible.

**How to avoid:** Record the amendment explicitly (the phase's own governing principle: "Phase 8 must not silently narrow"). Use the source indication the spec provides — `[CITED: specifications.freedesktop.org/wm/latest/ar01s03.html]`: *"Source indication should be 1 when the request comes from an application, and 2 when it comes from a pager"*, and *"Depending on the information provided with the message, the Window Manager may decide to refuse the request (either completely ignore it, or e.g. use `_NET_WM_STATE_DEMANDS_ATTENTION`)."*

**Also required and easy to forget:** advertising the new behaviour. `_NET_SUPPORTED` at `src/Manager.cpp:474-488` currently lists 23 atoms and includes none of `_NET_WM_USER_TIME`, `_NET_WM_USER_TIME_WINDOW`, `_NET_WM_STATE_DEMANDS_ATTENTION`, `_NET_WM_STATE_SKIP_TASKBAR`, or `_NET_WM_STATE_SKIP_PAGER`. FOCUS-01 needs the first three; RULES-02's `skip-taskbar` needs the last two. All five must be interned in the constructor's atom block (`src/Manager.cpp:139-163`) *and* added to the `supported[]` array, or compliant clients will never set the properties in the first place.

### Pitfall 6: SHAPE cannot be disabled, so a "real server" test for XDIS-03 does not exist

**What goes wrong:** Time is spent hunting for an X server configuration that lacks SHAPE, to avoid the "hacky" env var.

**Why it happens:** Reasonable instinct, given that RANDR and RENDER *can* be turned off. But the X server rejects it outright `[VERIFIED: Xvfb 21.1.4 stderr, this host]` — verbatim: `[mi] Extension "SHAPE" can not be disabled` followed by ` Only the following extensions can be run-time enabled/disabled:` and the list `Generic Event Extension, MIT-SHM, XTEST, SECURITY, XINERAMA, XFIXES, RENDER, RANDR, COMPOSITE, DAMAGE, MIT-SCREEN-SAVER, DOUBLE-BUFFER, RECORD, DPMS, X-Resource, XVideo, XVideo-MotionCompensation, SELinux, GLX`.

**How to avoid:** Accept D-12 as the correct and only design, and cite this server message in the plan so the decision is not relitigated. Conversely, **do** use `-extension RANDR` for XDIS-02 and `-extension RENDER` for XDIS-04, because for those two the real thing is available.

### Pitfall 7: Xvfb can only shrink, and `xrandr --fb` alone errors

**What goes wrong:** The resize test issues `xrandr --fb 1280x1024` against an Xvfb started at `1024x768` and gets `xrandr: screen cannot be larger than 1024x768`. Or it issues a shrink and gets a `BadValue` on `RRSetCrtcConfig` and assumes the resize failed.

**Why it happens:** Xvfb's RANDR maximum screen size equals its initial `-screen` geometry, and the single CRTC still covers the old area when the framebuffer shrinks. `[VERIFIED: this host — 'Screen 0: minimum 1 x 1, current 1024 x 768, maximum 1280 x 1024' after starting at 1280x1024]`

**How to avoid:** Start Xvfb at the **largest** geometry the test needs and shrink from there. The sequence that produced clean, correctly-ordered events on this host was:
```bash
Xvfb :N -screen 0 1280x1024x24 -ac +render -noreset
xrandr -display :N --output screen --off      # prints a BadValue; harmless
xrandr -display :N --fb 1024x768              # this one lands
```
Alternatively use `Xephyr -resizeable`, which this build supports `[VERIFIED: Xephyr -help → "-resizeable          Make Xephyr windows resizeable"; xserver-xephyr 2:21.1.4-2ubuntu1.7~22.04.16]`.

### Pitfall 8: clang-tidy has no baseline mechanism (D-03 is asymmetric)

**What goes wrong:** The plan says "checked-in baseline for cppcheck + clang-tidy" and a task tries to produce one for both. clang-tidy has no suppression-file concept, so the task stalls or produces a fake baseline.

**Why it happens:** cppcheck genuinely supports `--suppress-xml` with hash-anchored entries; clang-tidy's only suppressions are per-line `NOLINT` comments, and `WarningsAsErrors` promotes everything at once.

**How to avoid:** Implement D-03 with two different mechanisms and say so in the plan:
- **cppcheck** → generate `scripts/analysis/cppcheck-suppressions.xml` with `<hash>` entries, run with `--suppress-xml=... --error-exitcode=1`, and enable unmatched-suppression reporting so stale baseline entries surface.
- **clang-tidy** → no baseline file. Either (a) enumerate an explicit allowlist of enforced checks in `.clang-tidy`'s `WarningsAsErrors` starting deliberately small, or (b) gate on the diff with `clang-tidy-diff.py`. Scope with `header-filter` so `_deps/` (the FetchContent'd Catch2 tree) is excluded — otherwise the run is dominated by third-party findings.

### Pitfall 9: The `eventDestroy` use-after-free is real and reachable

**What goes wrong:** Destroying a dock window reads freed memory.

**Why it happens:** `[VERIFIED: src/Events.cpp:272-284]` — the owning `unique_ptr` is erased (running `~Client()`) and `c` is dereferenced afterwards. Verbatim, the erase:
```cpp
        if (!removeFrom(m_clients)) {
            removeFrom(m_hiddenClients);
        }
```
then, 10 lines later, verbatim:
```cpp
        // EWMH: Recalculate workarea if dock was destroyed
        if (c->isDock()) {
            updateWorkarea();
        }
```

**How to avoid:** Capture `const bool wasDock = c->isDock();` before STEP 3 and branch on the local. The checklist already prescribes exactly this. Pair it with the dock-destroy ASan test (coverage item 3) so the fix is *proven*, not merely applied — that is the difference between checking the box and satisfying success criterion 7.

### Pitfall 10: XTEST events are swallowed by the WM's own grabs

**What goes wrong:** The test synthesises a Button1 press to open the root menu, then a motion + release to select an item, and nothing happens — because `WindowManager::menu()` took a pointer grab.

**Why it happens:** `XTestGrabControl` imperviousness is a property **of the calling connection**, and the fake-event calls also block the calling connection for their `delay`.

**How to avoid:** Use a dedicated `Display` connection for input only, and call `XTestQueryExtension` then `XTestGrabControl(dpy, True)` **on that same connection** before any fake event. Pass `delay = CurrentTime` (0) and sequence with explicit `XFlush` + property polling rather than relying on the delay parameter.

## Code Examples

### Example 1: RANDR init, dispatch, and the mandatory update call

Verified end-to-end against Xvfb 21.1.4 on this host with a compiled probe.

```cpp
// --- Manager.cpp constructor, beside the existing Shape query (line 165-172) ---
#include <X11/extensions/Xrandr.h>

int rrErrorBase = 0;
if (!std::getenv("WM2_FORCE_NO_RANDR") &&
    XRRQueryExtension(display(), &m_randrEventBase, &rrErrorBase)) {
    XRRSelectInput(display(), m_root, RRScreenChangeNotifyMask);
    std::fprintf(stderr, "  Xrandr extension available.\n");
} else {
    std::fprintf(stderr, "wm2: warning: no xrandr extension, "
                         "screen geometry will not track resolution changes\n");
    m_randrEventBase = -1;      // sentinel: mirrors m_shapeEvent = -1 (Manager.cpp:171)
}

// --- Manager.cpp initialiseScreen(), extend the root mask (line 375-377) ---
attr.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
    ColormapChangeMask | ButtonPressMask | ButtonReleaseMask |
    PropertyChangeMask | StructureNotifyMask;   // NEW: root ConfigureNotify

// --- Events.cpp loop(), the ConfigureNotify case (currently line 110) ---
case ConfigureNotify:
    if (ev.xconfigure.window == m_root) {
        XRRUpdateConfiguration(&ev);            // handles root ConfigureNotify too
        handleScreenGeometryChange();
    }
    break;

// --- Events.cpp loop(), the default: case (line 115-121) ---
default:
    if (m_randrEventBase >= 0 &&
        ev.type == m_randrEventBase + RRScreenChangeNotify) {
        XRRUpdateConfiguration(&ev);            // MANDATORY -- see Pitfall 1
        handleScreenGeometryChange();
    } else if (ev.type == m_shapeEvent) {
        std::fprintf(stderr, "wm2: shaped windows are not supported\n");
    } else {
        std::fprintf(stderr, "wm2: unsupported event type %d\n", ev.type);
    }
    break;

// --- Manager.cpp, the D-25 reflow. Idempotent by construction. ---
void WindowManager::handleScreenGeometryChange()
{
    // Do NOT read ev.width/ev.height -- intermediate events carry stale values.
    // XRRUpdateConfiguration has already refreshed Xlib's cache; re-read it.
    const int w = screenWidth(), h = screenHeight();
    if (w == m_lastKnownScreenW && h == m_lastKnownScreenH) return;  // coalesce
    m_lastKnownScreenW = w; m_lastKnownScreenH = h;

    for (const auto& c : m_clients) c->ensureVisible();   // move, not resize (D-25)
    updateWorkarea();                                     // reclamp _NET_WORKAREA
}
```

Observed probe output on this host (initial 1280x1024, then `--fb 1024x768`) — note the duplicate delivery and the stale intermediate:
```
event_base=89 initial=1280x1024
[0] RRScreenChangeNotify ev=1280x1024 update=1 -> Display=1280x1024   <- stale intermediate
[1] root ConfigureNotify ev=1280x1024 update=1 -> Display=1280x1024
[2] RRScreenChangeNotify ev=1024x768  update=1 -> Display=1024x768    <- real change
[3] root ConfigureNotify ev=1024x768  update=1 -> Display=1024x768    <- duplicate
```

### Example 2: XTEST driver on a dedicated connection

```cpp
// tests/support/XTestDriver.h
#include <X11/extensions/XTest.h>

class XTestDriver {
public:
    explicit XTestDriver(const std::string& displayName)
        : m_dpy(XOpenDisplay(displayName.c_str()))
    {
        REQUIRE(m_dpy != nullptr);
        int ev = 0, er = 0, maj = 0, min = 0;
        REQUIRE(XTestQueryExtension(m_dpy, &ev, &er, &maj, &min) == True);
        // Per-connection: MUST be this connection, or grabs will swallow input.
        XTestGrabControl(m_dpy, True);
    }
    ~XTestDriver() { if (m_dpy) XCloseDisplay(m_dpy); }

    void moveTo(int x, int y) {
        XTestFakeMotionEvent(m_dpy, 0, x, y, CurrentTime);  // delay 0: don't stall
        XFlush(m_dpy);
    }
    void click(unsigned int button) {
        XTestFakeButtonEvent(m_dpy, button, True,  CurrentTime);
        XTestFakeButtonEvent(m_dpy, button, False, CurrentTime);
        XFlush(m_dpy);
    }
    void press(unsigned int b)   { XTestFakeButtonEvent(m_dpy, b, True,  CurrentTime); XFlush(m_dpy); }
    void release(unsigned int b) { XTestFakeButtonEvent(m_dpy, b, False, CurrentTime); XFlush(m_dpy); }
private:
    Display* m_dpy{nullptr};
};
```
Long-press delete (`destroy-window-delay`, default 1500ms per `include/Config.h:28`) is expressed as `press()` → wall-clock wait → `release()`, not as a `delay` argument — the delay parameter would block the driver connection for the whole interval.

### Example 3: Focus-stealing arbitration

Spec text, quoted `[CITED: specifications.freedesktop.org/wm/latest/ar01s05.html]`:
- `_NET_WM_USER_TIME` is **CARDINAL/32**; clients set it *"before mapping the window, to the timestamp of the user interaction that caused the window to appear."*
- *"The special value of zero on a newly mapped window can be used to request that the window not be initially focused when it is mapped."*
- `_NET_WM_USER_TIME_WINDOW` is **WINDOW/32** and *"contains the XID of a window on which the client sets the `_NET_WM_USER_TIME` property."*
- `_NET_WM_STATE_DEMANDS_ATTENTION` *"may be set by the Window Manager if the window requested activation but the Window Manager refused it"* and *"should be unset by the Window Manager when it decides the window got the required attention (usually, that it got activated)."*

```cpp
// WindowManager: one global clock, fed only by real user input.
Time m_lastUserInteraction = CurrentTime;   // updated in eventButton()

// Client::manage(), after getWindowType()/getClassHint(), before the map decision.
bool Client::shouldFocusOnMap()
{
    if (!windowManager()->config().focusStealingPrevention) return true;  // D-19 off switch

    Window src = m_window;
    Window utw = None;
    if (readWindowProperty(m_window, Atoms::net_wmUserTimeWindow, &utw) && utw != None) {
        src = utw;                                    // read from the proxy window
    }

    Time userTime = 0;
    if (!readCardinalProperty(src, Atoms::net_wmUserTime, &userTime)) {
        return true;   // D-19: legacy clients set nothing -- do not punish them
    }
    if (userTime == 0) return false;                  // explicit "do not focus me"

    // X timestamps wrap at 2^32ms (~49.7 days). Compare as a signed delta,
    // never with a bare `<`, or focus breaks once per wrap.
    const long delta = static_cast<long>(userTime) -
                       static_cast<long>(windowManager()->lastUserInteraction());
    return delta >= 0;
}

// Refusal is not silent (D-18): signal, then let the user decide.
void Client::demandAttention()
{
    addNetWmState(Atoms::net_wmStateDemandsAttention);   // via updateNetWmState()
    if (XWMHints* h = XGetWMHints(display(), m_window)) {
        h->flags |= XUrgencyHint;
        XSetWMHints(display(), m_window, h);
        XFree(h);
    }
    decorate(false);   // tab renders inactive; attention styling is a Border concern
}
```

### Example 4: Hermetic fallback fixtures (three different mechanisms)

```cmake
# XDIS-02 -- a genuinely RANDR-less server. No production code involved.
add_test(NAME start_xvfb_norandr COMMAND bash -c
    "Xvfb :101 -screen 0 1024x768x24 -ac +render -noreset -extension RANDR & \
     echo $! > ${CMAKE_BINARY_DIR}/xvfb_norandr.pid && sleep 1")
set_tests_properties(start_xvfb_norandr PROPERTIES FIXTURES_SETUP xvfb_norandr)

# XDIS-04 -- a genuinely RENDER-less server. Xft must degrade, not die.
add_test(NAME start_xvfb_norender COMMAND bash -c
    "Xvfb :102 -screen 0 1024x768x24 -ac -noreset -extension RENDER & \
     echo $! > ${CMAKE_BINARY_DIR}/xvfb_norender.pid && sleep 1")
set_tests_properties(start_xvfb_norender PROPERTIES FIXTURES_SETUP xvfb_norender)

# XDIS-03 -- SHAPE cannot be disabled server-side, so force it in-process (D-12).
catch_discover_tests(test_wm_fallbacks
    PROPERTIES ENVIRONMENT "WM2_FORCE_NO_SHAPE=1"
               FIXTURES_REQUIRED xvfb_display)
```

The XDIS-03 assertion should be a *negative protocol* assertion, not just "it did not crash": run the WM with `WM2_FORCE_NO_SHAPE=1`, map a client, and assert via `xwininfo`/`XGetWindowAttributes` that the frame is rectangular and that no `XShapeCombineRectangles` request was issued. The cheap, robust proxy for "no Shape requests" is the greppable invariant from Pattern 2 (exactly one call site, inside the guarded wrapper) plus `XShapeQueryExtents` reporting the frame as unshaped.

### Example 5: Capability evidence capture for XDIS-05 (D-05 artifact)

One script, run once per target, output committed under `evidence/`.

```bash
#!/usr/bin/env bash
# scripts/capture-display-capabilities.sh <display> <label>
set -euo pipefail
D="$1"; LABEL="$2"; OUT="evidence/${LABEL}"
mkdir -p "$OUT"
{
  echo "## $LABEL  ($(date -Is))"
  echo "### extensions"; DISPLAY="$D" xdpyinfo | sed -n '/number of extensions/,/^default screen/p'
  echo "### geometry";   DISPLAY="$D" xdpyinfo | grep -E 'dimensions|resolution|depth of root'
  echo "### randr";      DISPLAY="$D" xrandr --query 2>&1 || echo "RandR extension missing"
  echo "### fontconfig"
  fc-match "Noto Sans,DejaVu Sans,Sans:size=12"
  fc-match "Noto Sans,DejaVu Sans,Sans:bold:size=12"
} | tee "$OUT/capabilities.txt"

DISPLAY="$D" xprop -root _NET_SUPPORTING_WM_CHECK _NET_SUPPORTED _NET_CLIENT_LIST \
                   _NET_ACTIVE_WINDOW _NET_WORKAREA > "$OUT/xprop-root.txt"
DISPLAY="$D" xwininfo -root -tree                  > "$OUT/xwininfo-tree.txt"
```
The three capability lines to extract per target for the XDIS-05 matrix are `SHAPE` present, `RANDR` present, `RENDER` present. On this host, plain `Xvfb -screen 0 1024x768x24 +render` advertises 23 extensions including all three `[VERIFIED: xdpyinfo on :77, this host]`.

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Poll `XGetGeometry(root)` or recompute from `XRRGetScreenInfo` | `XRRSelectInput` + `XRRUpdateConfiguration` on the event | RandR 1.2 era; the Xlib-notification requirement is documented in `Xrandr(3)` | The library maintains Xlib's cached view for you — but only if you call it. |
| `RRSetScreenConfig` / `xrandr --size` (RandR 1.1 screen-config model) | `RRSetScreenSize` (`xrandr --fb`) + per-CRTC `RRSetCrtcConfig` | RandR 1.2 | Xvfb here reports `RANDR 1.6` and advertises exactly one mode, so `xrandr -s` fails with "Size not found in available modes" while `--fb` works. Test scripts must use the 1.2 model. |
| Core X fonts + xvertext for rotated tab labels | Xft + fontconfig + `FcMatrixRotate` | Phase 4 of this project | D-13 forbids reverting. The residual risk is the FcMatrix path on a RENDER-less server (see Open Questions). |
| WM grants every focus request | `_NET_WM_USER_TIME` + source indication arbitration; refusal signalled via `_NET_WM_STATE_DEMANDS_ATTENTION` | EWMH 1.3 (2004-ish); universal in modern WMs | What FOCUS-01 implements. |
| Line-anchored static-analysis suppression lists | cppcheck hash-anchored `--suppress-xml`; clang-tidy diff-based gating | cppcheck 1.90+ | Determines the two-mechanism shape of D-03. |
| `xdotool` for X integration testing | XTEST from within the test binary | — | D-08. Removes a runtime tool dependency and the shell-timing flakiness. |

**Deprecated/outdated:**
- `xrandr -s <WxH>` against modern servers advertising one mode — use `--fb`.
- Treating `SubstructureNotifyMask` as covering root's own configure events — it does not.
- `COMPILED_CODE_BEHAVIOR_CHECKLIST.md`'s "102 Catch2 test cases" baseline — stale, actual is 135.
- `.planning/codebase/TESTING.md` — describes `upstream-wm2/`, predates all test infrastructure.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Xft's non-RENDER core path renders `FcMatrix`-rotated glyphs correctly (only "started without error" was observed, not glyph output) | Pitfall / XDIS-04 | XDIS-04 fails on a RENDER-less target and, worse, `src/Border.cpp:49` `fatal()`s — the WM exits rather than degrading. **Spiked as 08-06 Task 1; the fatal call is removed by 08-06 Task 2 regardless of the spike's outcome.** |
| A2 | TigerVNC's Xvnc advertises SHAPE and RANDR; TightVNC historically lacks RENDER | XDIS-05 | Not installed on this host; from community reports, not first-party docs. The capture script (Example 5) turns this into fact on first run — run it early. |
| A3 | X2Go's NX-based agent (`nxagent`) supports SHAPE and RENDER but has historically limited RANDR | XDIS-05 | Same as A2 — resolve empirically. X2Go is the least-certain of the three targets and the most likely source of an accepted deviation. |
| A4 | Adding `StructureNotifyMask` to the root mask introduces no unwanted event traffic | Pitfall 2 | Extra events would be root-only `ConfigureNotify`/`MapNotify`, already discarded by the loop — low risk, but verify no CPU-spin regression against the checklist's idle-CPU item. |
| A5 | `_exit()` instead of `std::exit()` in the forked children of `spawn()`/`spawnArgv()`/`launchApp()` is safe and removes spurious ASan leak checks | Pitfall 4 | Standard POSIX practice; the only behaviour change is that atexit handlers no longer run in the child, which is the desired outcome. Low risk. |
| A6 | `libXtst` can be linked test-only without pulling it into the shipped binary | Standard Stack | Straightforward in CMake (target-scoped `target_link_libraries`), but it resolves an open "Claude's Discretion" item — confirm with `ldd build/release/wm2-born-again` (already a checklist line). |
| A7 | Changing `autoRaise`/`raiseOnFocus` defaults to `true` (D-17) requires editing existing `tests/test_config.cpp` default assertions | Pattern 3 | If the tests are structured differently than expected, the edit is smaller. Either way this is a deliberate change, not a regression. |
| A8 | X timestamp wraparound (~49.7 days) matters for the FOCUS-01 comparison | Code Example 3 | Unlikely to bite in practice on a rebooted VPS, but signed-delta comparison costs nothing and a bare `<` produces a total focus outage once per wrap. |

## Open Questions (RESOLVED)

All six questions below were resolved during planning; each carries a `RESOLVED:` line naming the plan and task that acts on the recommendation. Nothing here is live uncertainty — the residual *empirical* unknowns (does a rotated font actually load without RENDER; what are the real RSS numbers) are carried as executable spikes and measurements inside those plans, not as planning gaps.

1. **Does the rotated Xft tab font load and render on a server without RENDER?**
   - What we know: `Xvfb -extension RENDER` produces a genuinely RENDER-less server `[VERIFIED]`; `xclock -face "DejaVu Sans-12"` (which links libXft) starts and maps a window there `[VERIFIED]`; `XftDefaultHasRender()` is exported by libXft `[VERIFIED]`; the tab font is opened with `FcMatrixRotate` at `include/x11wrap.h:296-306`, and a load failure calls `fatal()` at `src/Border.cpp:49`.
   - What's unclear: whether a *rotated* font pattern survives the core path, and whether `XftTextExtentsUtf8` returns sane extents there (`src/Border.cpp:56-60` already notes "Rotated Xft fonts have zero height").
   - Recommendation: **Spike this first in 08-06** — a Catch2 case on `:N -extension RENDER` that opens the rotated font, measures extents, and draws into a pixmap. Regardless of the outcome, make the `Border.cpp:49` path non-fatal (fall back to an unrotated font, then to no label) — a WM that exits on a degraded remote server fails XDIS-05 outright.
   - **RESOLVED: 08-06.** Task 1 is the spike, tagged `[xft_norender_spike]`, kept in-tree so the answer stays reproducible. Task 2 replaces the fatal call with the four-rung ladder unconditionally, so the fix does not depend on the spike's outcome. Task 3 proves it end-to-end against a RENDER-less server.

2. **Where exactly does `WM2_FORCE_NO_SHAPE` get read?**
   - What we know: forcing `m_shapeEvent = -1` in the constructor is one line and makes every `hasShapeExtension()` consumer correct at once.
   - What's unclear: whether the test also wants to force it *after* startup (it does not — resolution/extension capability is fixed per connection).
   - Recommendation: read it once, immediately after `XShapeQueryExtension`, alongside the existing warning at `src/Manager.cpp:170`. Log a distinct message so the evidence transcript shows the forced path was actually taken.
   - **RESOLVED: 08-03.** Task 2 reads the lever once at the capability query and forces the `-1` sentinel there, mirroring the RANDR sentinel shape.

3. **How is "no Shape requests were issued" positively proven?**
   - What we know: the greppable single-call-site invariant plus `XShapeQueryExtents` on the frame is a good proxy.
   - What's unclear: whether the phase wants protocol-level proof (an `xtrace`/`x11trace` capture).
   - Recommendation: the invariant + unshaped-frame assertion is sufficient and hermetic; note `x11trace` as a manual escalation only if a reviewer disputes it. `xtrace` is not installed here.
   - **RESOLVED: 08-03.** Task 1 collapses the call sites into the single `combineShape()` funnel; Task 3 asserts the greppable single-funnel invariant plus the unshaped-frame check. No protocol-level trace is planned.

4. **Does the process-level harness get its own display, or is `:99` retained with locks?**
   - What we know: sharing is genuinely unsafe (Pitfall 3). `:99` appears at `CMakeLists.txt:88, 100, 143, 162, 178` — five lines, but only **four** of them are `DISPLAY=:99` test-environment assignments (100, 143, 162, 178, one each for `test_smoke`, `test_client`, `test_xft_poc`, `test_ewmh`). Line 88 is the `Xvfb :99 -screen ...` server-launch command, not a target binding. So there are **four legacy display-backed targets**, not five `[VERIFIED: grep -c 'DISPLAY=:99' CMakeLists.txt → 4]`.
   - Recommendation: **allocate dynamically for the new WM tests**, leave `:99` for the four legacy display-backed targets, and add `RESOURCE_LOCK` so `ctest -j` can never interleave the two groups. This is the smallest change that is actually correct.
   - **RESOLVED: 08-01.** Task 1's `WmFixture` allocates a free display per instance and the `test_wm_process` target deliberately gets no `DISPLAY=:99` binding; Task 3 adds `RESOURCE_LOCK "x_display_99"` to the four legacy targets. The `DISPLAY=:99` count therefore stays at 4 for the whole phase — every plan that touches `CMakeLists.txt` re-asserts that, so an accidental fifth binding fails immediately.

5. **What are the D-32 RSS and idle-CPU numbers?**
   - What we know: the constraint is a 512MB VPS running a VNC server *and* the WM; the current stale binary is 583,696 bytes.
   - Recommendation: measure baseline RSS with 0/1/5/20 mapped `xclock` clients first, then set the budget at roughly 2x the measured 20-client figure and record the measurement in the evidence directory. Do not invent a number before measuring. Idle CPU should assert `< 1%` over a 30s window, which also covers the checklist's "no steady CPU spin" item.
   - **RESOLVED: 08-14.** Task 1 measures first and derives the budget from the measurement; the plan carries an explicit prohibition against inventing a number before measuring.

6. **Is `--help` expected to work?**
   - What we know: it does not. `[VERIFIED: ./build/wm2-born-again --help → "unrecognized option '--help'" then "Try './build/wm2-born-again --help' for more information."]` — `getopt_long` rejects it and `src/Config.cpp:298-302` prints a message pointing at the very option that just failed, then `exit(2)`.
   - Recommendation: out of scope for the requirement IDs, but it is a 10-line fix that makes every piece of release evidence and every manual smoke run less confusing. Worth one task or an explicit deferral note.
   - **RESOLVED: 08-13.** Folded into Task 2 ("Terminating X11 error paths and the help flag") rather than deferred — it is in the same file and the same commit as the exit-path coverage.

## Environment Availability

Audited on this host, 2026-08-11. Ubuntu 22.04.5 LTS, g++ 11.4.0, CMake 3.22.1, pkg-config 0.29.2.

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| `pkg-config x11` | build | ✓ | 1.7.5 | — |
| `pkg-config xext` | build (Shape) | ✓ | 1.3.4 | — |
| **`pkg-config xft`** | **build** | **✗** | — | **none — BLOCKING** |
| **`pkg-config fontconfig`** | **build** | **✗** | — | **none — BLOCKING** |
| `pkg-config xrandr` | XDIS-01/02 (D-26) | ✓ | **1.5.2** | — |
| `pkg-config xtst` | TEST-05 (D-08) | ✗ | — | `xdotool` (present) — rejected by D-08 |
| `pkg-config xrender` | (transitive via Xft) | ✓ | 0.9.10 | — |
| Xvfb | TEST-05/07/08 | ✓ | Xorg 21.1.4 | — |
| Xephyr | TEST-08, manual smoke | ✓ | 21.1.4 (`-resizeable` supported) | — |
| xprop / xwininfo / xdpyinfo | TEST-08 evidence | ✓ | — | — |
| xrandr (CLI) | XDIS-01 test driver | ✓ | — | — |
| xdotool | manual smoke aid | ✓ | — | — |
| fc-match | TEST-07 (D-01) | ✓ | — | — |
| xclock / xmessage | runtime smoke clients | ✓ | — | — |
| **xterm** | shipped default `new-window-command` | **✗** | — | `xclock` for tests; **install it or change the default** |
| cppcheck | TEST-06 (D-03) | ✗ | — | none — D-04 makes it a hard blocker |
| clang-tidy | TEST-06 (D-03) | ✗ | — | none — D-04 makes it a hard blocker |
| ASan/UBSan | TEST-06 (D-04) | ✓ | built into g++ 11.4 | — |
| valgrind | (not required) | ✗ | — | ASan covers it |
| TigerVNC (`Xvnc`/`vncserver`) | XDIS-05 (D-29) | ✗ | — | `x11vnc` is present but is a *different* architecture (attaches to an existing display); not a substitute for Xvnc |
| xrdp | XDIS-05 (D-29) | ✓ | `/usr/sbin/xrdp` | — |
| X2Go | XDIS-05 (D-29) | ✗ | — | none — must be stood up (D-29 anticipated this) |
| Display `:99` | existing ctest fixture | ✓ free | `/tmp/.X11-unix/` holds `X0`, `X20` only | — |

**Missing dependencies with no fallback (block execution):**
- `libxft-dev`, `libfontconfig1-dev` — **the project cannot be configured or built until these are installed.** This is Task 1 of the phase, full stop.
- `libxtst-dev` — blocks the D-08 input-synthesis harness.
- `cppcheck`, `clang-tidy` — D-04 declares the static-analysis gate a hard blocker that may not be waived.
- `tigervnc-standalone-server`, `x2goserver` — block two of the three D-29 validation targets.

**Missing dependencies with fallback:**
- `xterm` — tests use `--new-window-command=xclock`. Installing it anyway is cheap and lets the shipped default be exercised for real.

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | Catch2 v3.14.0 via `FetchContent` (`CMakeLists.txt:48-58`) |
| Config file | `CMakeLists.txt` (no separate config); `catch_discover_tests(<target> ADD_TAGS_AS_LABELS)` per target |
| Quick run command | `ctest --test-dir build/debug -L '^(config\|rules\|autoraise\|raii)$' --output-on-failure --no-tests=error` (display-free targets, sub-second) |
| Full suite command | `ctest --test-dir build/debug --output-on-failure --no-tests=error` |
| Current surface | **135 `TEST_CASE`s across 11 files** — `config` 40, `ewmh` 16, `raii` 16, `desktopentry` 12, `client` 11, `appcache` 8, `binaryscanner` 8, `smoke` 7, `autoraise` 6, `xft` 6, `eventloop` 5 `[VERIFIED: grep -c '^TEST_CASE' tests/*.cpp]` |

> **Selection convention (D-33, added during plan revision).** The commands in this section originally read `ctest -R <catch2-tag>`. That is wrong and silently so: `catch_discover_tests` registers each ctest test under its `TEST_CASE` *name*, so a tag-shaped `-R` expression matches zero of the registered tests, and ctest exits 0 on zero matches — every such gate would have been green without running anything. The corrected form uses per-test labels (`catch_discover_tests(<target> ADD_TAGS_AS_LABELS)`, Catch2 v3.14) selected with an anchored `ctest -L '^<tag>$'`, plus `--no-tests=error` on every invocation so a future zero-match fails loudly. Anchoring matters: `-L` is an unanchored regex, so a bare `-L config` would also drag in `wm_config_runtime`. Also note the tag in `tests/test_xft_poc.cpp` is `[xft][poc]` — there is no `xft_poc` tag, so that token became `^xft$`.

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| XDIS-01 | Resolution change repositions offscreen windows; visible ones untouched | integration (process) | `ctest --test-dir build/debug -L '^wm_geometry$' --output-on-failure --no-tests=error` | ❌ Wave 0 |
| XDIS-01 | `XRRUpdateConfiguration` refreshes `screenWidth()/Height()`; handler is idempotent across duplicate events | integration | same | ❌ Wave 0 |
| XDIS-02 | WM starts and manages windows on `Xvfb -extension RANDR` | integration | `ctest -L '^wm_norandr$' --no-tests=error` (fixture `xvfb_norandr`) | ❌ Wave 0 |
| XDIS-03 | `WM2_FORCE_NO_SHAPE=1` → rectangular frame, zero Shape requests, single call site | integration + guard | `ctest -L '^(wm_noshape\|shape_invariant)$' --no-tests=error` | ❌ Wave 0 |
| XDIS-04 | Fonts resolve and the WM does not `fatal()` on `Xvfb -extension RENDER` | integration | `ctest -L '^wm_norender$' --no-tests=error` (fixture `xvfb_norender`) | ❌ Wave 0 |
| XDIS-04 | fontconfig fallback chains resolve (`fc-match`) | preflight | `scripts/preflight.sh` (ctest fixture, D-02) | ❌ Wave 0 |
| XDIS-05 | Capability + EWMH evidence captured per target | **manual** | `scripts/capture-display-capabilities.sh` — real VNC/XRDP/X2Go sessions per D-31 | ❌ Wave 0 |
| FOCUS-01 | Stale `_NET_WM_USER_TIME` → mapped unfocused + `DEMANDS_ATTENTION` set | integration | `ctest -L '^wm_focus$' --no-tests=error` | ❌ Wave 0 |
| FOCUS-01 | `_NET_WM_USER_TIME == 0` → not focused; property absent → focused (D-19) | integration | same | ❌ Wave 0 |
| FOCUS-01 | `_NET_ACTIVE_WINDOW` source=2 granted, source=1 arbitrated | integration | same | ❌ Wave 0 |
| FOCUS-02 | Each of the three booleans changes observable runtime behavior | integration | same | ❌ Wave 0 |
| FOCUS-02 | Config parsing + CLI precedence for the booleans and the new off switch | unit | `ctest -L '^config$' --no-tests=error` | ✅ extend `tests/test_config.cpp` |
| RULES-01 | `rule-*` repeated-group parsing, ordering, malformed input, orphan keys | unit | `ctest -L '^rules$' --no-tests=error` | ❌ Wave 0 |
| RULES-01 | Exact vs substring, AND across criteria, all 4 window types | unit | same | ❌ Wave 0 |
| RULES-02 | `no-decorate` / `position`+`size` / `skip-taskbar` applied at map; later-wins ordering | integration | `ctest -L '^wm_rules$' --no-tests=error` | ❌ Wave 0 |
| TEST-05 | 13 checklist coverage items (destroy, dock destroy, hide/unhide, config→runtime, X11 error paths, no-Shape, size hints, all gravities, EWMH client messages, fullscreen/maximize restore, malformed properties, 100-window stress) | integration | `ctest -L '^wm_' --no-tests=error` (prefix, deliberately not `$`-anchored — this row spans every `wm_*` label) | ❌ Wave 0 |
| TEST-06 | Debug + Release + ASan/UBSan builds all green | build gate | `scripts/gates/build-all.sh` | ❌ Wave 0 |
| TEST-06 | No new cppcheck/clang-tidy findings vs baseline | static gate | `scripts/analysis/run-static-analysis.sh` | ❌ Wave 0 |
| TEST-07 | Preflight fails loudly before any behavioral test | fixture | `scripts/preflight.sh` as `FIXTURES_SETUP` | ❌ Wave 0 |
| TEST-08 | `xprop -root` / `xwininfo -root -tree` transcripts + interaction results committed | **manual + script** | `scripts/capture-display-capabilities.sh` → `evidence/` | ❌ Wave 0 |
| D-32 | RSS and idle CPU within documented budget under multi-window load | integration | `ctest -L '^wm_resource_budget$' --no-tests=error` | ❌ Wave 0 |

### Sampling Rate

- **Per task commit:** `ctest --test-dir build/debug -L '^(config|rules|autoraise|raii|eventloop)$' --output-on-failure --no-tests=error` — the display-free targets, fast enough to run on every commit.
- **Per wave merge:** `ctest --test-dir build/debug --output-on-failure --no-tests=error` — full suite including all Xvfb-backed and process-level tests.
- **Phase gate (D-04, all four mandatory):** Debug full ctest green **and** Release full ctest green **and** ASan/UBSan full ctest with zero actionable reports **and** static analysis with no new findings — then the runtime smoke transcript, then `/gsd-verify-work`.

### Wave 0 Gaps

- [ ] `scripts/preflight.sh` — TEST-07, and it unblocks literally everything else
- [ ] **Install `libxft-dev` + `libfontconfig1-dev`** — not a file, but the true Wave 0 blocker
- [ ] Delete stale `build/`; create `build/debug`, `build/release`, `build/asan`
- [ ] `tests/support/WmFixture.h` — fork/exec + display allocation + readiness polling + ASan report collection
- [ ] `tests/support/XTestDriver.h` — dedicated connection + `XTestGrabControl`
- [ ] `tests/test_wm_process.cpp` — harness self-test plus the lifecycle coverage items
- [ ] `tests/test_wm_geometry.cpp`, `tests/test_wm_fallbacks.cpp`, `tests/test_wm_focus.cpp`, `tests/test_wm_rules.cpp`, `tests/test_rules.cpp`
- [ ] `CMakeLists.txt`: add `xrandr` to the required modules (line 11-14 block), add `xtst` **test-only**, add the `xvfb_norandr` / `xvfb_norender` fixtures, add `RESOURCE_LOCK` to the `:99` group
- [ ] `scripts/analysis/cppcheck-suppressions.xml` + `.clang-tidy` — the two-mechanism D-03 gate
- [ ] `lsan.supp` — libX11 / libXft / fontconfig suppressions
- [ ] Update `COMPILED_CODE_BEHAVIOR_CHECKLIST.md:88-91` expected-surface baseline (102 → current)
- [ ] `evidence/` directory (D-05)

## Security Domain

`security_enforcement` is not set to `false` in `.planning/config.json`, so this section applies. This is a local X11 window manager with no network surface of its own; the relevant threat model is hostile/buggy *X clients* and untrusted *config content*.

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | No user accounts. X11 access control is the server's (`-ac` is test-only — must never appear in a production/VNC recipe). |
| V3 Session Management | no | No sessions. |
| V4 Access Control | **yes** | `_NET_ACTIVE_WINDOW` arbitration (FOCUS-01) *is* an access-control decision: which client may seize input focus. Source indication + timestamp is the standard control. |
| V5 Input Validation | **yes** | Two untrusted inputs: (a) client-supplied X properties — enforce expected `type`/`format`/`nitems` on every `XGetWindowProperty` and clamp values; (b) config file — the existing 4096-char line / 256-char value limits (`src/Config.cpp:94, 123`) must extend to all new `rule-*` keys. |
| V6 Cryptography | no | None used, none needed. |
| V12 File Handling | **yes** | Config is read from `$XDG_CONFIG_DIRS` then `$XDG_CONFIG_HOME` (`src/Config.cpp:236-251`). New evidence/log paths must not be world-writable temp paths chosen by predictable name. |
| V14 Configuration | **yes** | `exec-using-shell` is shell-evaluated and defaults to `false`; window rules must **not** introduce any new command-execution action. |

### Known Threat Patterns for C++17 / Xlib / X11

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Malicious client sets a huge/absurd `_NET_WM_STRUT` or size hint | Denial of Service | Clamp to screen geometry — `updateWorkarea()` already clamps; new rule-driven position/size must clamp identically via `ensureVisible()`. Checklist item exists. |
| Client property with wrong atom type / format / oversized array | Tampering, DoS | Check `actualType`, `actualFormat`, and `nItems` on every read; always `XFree`. Coverage item 12 tests exactly this. |
| Use-after-free reachable from client-controlled event ordering | Elevation of Privilege | The live `eventDestroy` bug (Pitfall 9) is triggerable by a client destroying a dock window. Fix + prove under ASan. |
| Focus stealing by a background application | Spoofing | FOCUS-01. This *is* the mitigation. |
| Config-driven shell execution | Elevation of Privilege | Keep `execUsingShell` default `false`; Phase 7's T-7-06 already restricts the shell path to Manual entries only (`src/Client.cpp`/`src/Manager.cpp:900`). **Add no shell-capable rule action.** |
| Predictable temp paths for ASan logs / Xvfb lock files in a shared-tmp environment | Tampering | Use process-scoped subdirectories under the CMake binary dir, not bare `/tmp/<fixed-name>`. |
| `-ac` (disable access control) leaking from test recipes into deployment docs | Elevation of Privilege | `-ac` appears in `CMakeLists.txt:88` and throughout the checklist's smoke commands. Ensure the VNC/XRDP/X2Go deployment guidance never copies it. |
| Unbounded `XGetWindowProperty` read on `WM_NAME`/`WM_CLASS` | DoS | Bound the request length (`Client::getWindowType` already caps at `1024L`, `src/Client.cpp:587`); do the same for the new `XGetClassHint` consumer path. |

## Sources

### Primary (HIGH confidence — verified by direct execution/inspection on this host, 2026-08-11)

- `man 3 Xrandr` (local, libxrandr-dev 1.5.2) — the "Xlib notification" mandate for `XRRUpdateConfiguration`, event semantics, rotation/reflection model
- `/usr/include/X11/extensions/Xrandr.h` — `XRRQueryExtension`, `XRRSelectInput`, `XRRUpdateConfiguration` signatures; `XRRScreenChangeNotifyEvent` layout
- `/usr/include/X11/extensions/randr.h` — `RRScreenChangeNotifyMask (1L<<0)`, `RRScreenChangeNotify 0`, `RANDR_MAJOR 1` / `RANDR_MINOR 6`
- **Compiled probe against live Xvfb** (`/tmp/rrprobe2.c`) — dual event delivery, stale intermediate values, `XRRUpdateConfiguration` return value and effect on `DisplayWidth`/`DisplayHeight`
- **Xvfb / Xorg 21.1.4 server behaviour** — the run-time enable/disable extension list; `Extension "SHAPE" can not be disabled`; RANDR and RENDER successfully disabled; RENDER-less Xft client starts cleanly
- `pkg-config`, `dpkg -l`, `apt-cache policy` — dependency and version facts
- `nm -D /lib/x86_64-linux-gnu/libXft.so.2` — `XftDefaultHasRender` exported
- `git ls-remote https://github.com/catchorg/Catch2.git v3.14.0` → `79319f2f0a109f6e2db37c3393fb67b4cd92781b`
- Repository source read this session: `CMakeLists.txt`, `include/Manager.h`, `include/Client.h`, `include/Config.h`, `src/Manager.cpp`, `src/Client.cpp`, `src/Events.cpp`, `src/Config.cpp`, `tests/test_smoke.cpp`, plus targeted greps over `src/Border.cpp`

### Secondary (MEDIUM confidence — official specification, fetched this session)

- EWMH / wm-spec, `specifications.freedesktop.org/wm/latest/ar01s05.html` — `_NET_WM_USER_TIME`, `_NET_WM_USER_TIME_WINDOW`, `_NET_WM_STATE_DEMANDS_ATTENTION`
- EWMH / wm-spec, `specifications.freedesktop.org/wm/latest/ar01s03.html` — `_NET_ACTIVE_WINDOW` message fields and source indication
- `XTestFakeButtonEvent(3)` / `XTestFakeMotionEvent(3)` / `XTestGrabControl(3)` man pages (Arch / Debian / X.Org mirrors) — per-connection imperviousness, delay blocking semantics

### Tertiary (LOW confidence — web search, cross-check before relying on)

- TigerVNC / Xvnc extension coverage and the `+extension NAME` / `-extension NAME` toggle syntax — community reports (ArchWiki, TigerVNC issue tracker, Manjaro forum), not first-party docs → assumption A2
- neutrinolabs/xrdp issue #914 (reconnect-at-different-resolution) — a 2017 report; useful as a scenario description, not as current behaviour
- cppcheck `--suppress-xml` hash-matching workflow and clang-tidy's lack of a baseline mechanism (DeepWiki, LLVM docs, MongoDB C++ driver rollout)
- LSan suppression patterns for libX11/fontconfig (Chromium, Skia, Mozilla bug 1149719) and ASan `log_path` / `exitcode` child-capture recipe (google/sanitizers wiki)
- Xft core-path behaviour with `FcMatrix`-transformed fonts — the source explicitly flagged this as unverified context → assumption A1, drives Open Question 1

## Metadata

**Confidence breakdown:**
- Standard stack: **HIGH** — every version and availability fact came from `pkg-config`/`dpkg`/`apt-cache` on the target host, not from memory
- Architecture / integration points: **HIGH** — all line references read from source this session and quoted verbatim where they are load-bearing
- Xrandr mechanism: **HIGH** — verified with a compiled probe against a live X server, including the failure mode
- Extension-fallback test strategy: **HIGH** — the disable-able/not-disable-able split was proven by the server's own error message
- Focus / EWMH semantics: **MEDIUM-HIGH** — official spec quoted verbatim, but not exercised against a running WM
- Remote-desktop targets (TigerVNC / XRDP / X2Go): **LOW-MEDIUM** — two of three not installed; capture script provided to convert this to fact on first run
- Xft without XRender (XDIS-04): **LOW** — the honest gap in this research; spike required (Open Question 1)
- Static-analysis / sanitizer tooling: **MEDIUM** — well-documented practice, neither tool installed here to verify locally

**Research date:** 2026-08-11
**Valid until:** 2026-09-10 (30 days). The X11/Xlib/EWMH facts are effectively permanent; the host-environment audit is valid only until packages are installed, which is Task 1 of this phase — re-run `scripts/preflight.sh` rather than trusting the Environment Availability table after that point.
