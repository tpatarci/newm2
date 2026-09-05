# Phase 8: Xrandr + VNC Compatibility + Focus/Rules - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-08-11
**Phase:** 8-xrandr-vnc-compatibility-focus-rules
**Areas discussed:** Dependency preflight, Process-level test harness, Shape-fallback architecture, Evidence & signoff bar, Focus policy model, Window rules syntax, Xrandr reflow policy, Remote-target test scope

---

## Dependency Preflight

**Host scan performed before questions:** `xft` and `fontconfig` pkg-config files MISSING; `x11` 1.7.5, `xext` 1.3.4, `xrandr` 1.5.2 present. Xvfb, Xephyr, xprop, xwininfo, xdotool, xdpyinfo, xclock, xmessage, xrandr, cmake all present. cppcheck and clang-tidy MISSING.

| Option | Description | Selected |
|--------|-------------|----------|
| Install now + add preflight script | Install packages AND add scripts/preflight.sh | ✓ |
| Install now only | Rely on CMake's pkg-config failure message | |
| Preflight script only | Checker added, installation left to operator | |

**User's choice:** Install now + add preflight script

| Option | Description | Selected |
|--------|-------------|----------|
| Install and make advisory | Run static analysis, record findings, only ASan blocks | |
| Install and make blocking | cppcheck/clang-tidy findings block alongside sanitizers | ✓ |
| Skip — accepted exception | Sanitizers + ctest are the gate | |

**User's choice:** Install and make blocking
**Notes:** Claude flagged that `--enable=all` on a codebase derived from 1997 sources would likely produce a large pre-existing finding set, and asked a follow-up to define "blocking" concretely (see next table).

| Option | Description | Selected |
|--------|-------------|----------|
| Baseline now, new findings block | Check in current findings as baseline; new findings block | ✓ |
| Zero findings, full cleanup | Drive to clean before Phase 8 completes | |
| Blocking for error/warning severity only | Curated check list blocks; style/perf advisory | |

**User's choice:** Baseline now, new findings block

| Option | Description | Selected |
|--------|-------------|----------|
| Standalone script + ctest fixture | Runnable by hand plus a fixture that fails before behavioral tests | ✓ |
| Standalone script only | Manual run at signoff | |
| CMake configure-time check | Fail at cmake configure if tools missing | |

**User's choice:** Standalone script + ctest fixture

| Option | Description | Selected |
|--------|-------------|----------|
| Allocate a free display | Harness picks an unused display number | |
| Keep :99, add a guard | Fixed display with a clear in-use message | |
| You decide | Leave to the planner | ✓ |

**User's choice:** You decide
**Notes:** Claude then found `:99` hardcoded across ~5 sites in CMakeLists.txt (lines 88, 100, 143, 162, 178) and recorded that dynamic allocation is real work, for the planner to weigh.

---

## Process-Level Test Harness

| Option | Description | Selected |
|--------|-------------|----------|
| Child process + XTEST from test | Fork/exec the real binary, synthesize input via XTestFake* | ✓ |
| Child process + xdotool | Drive via xdotool CLI | |
| Child process + synthetic XSendEvent | Send events directly to windows | |

**User's choice:** Child process + XTEST from test

| Option | Description | Selected |
|--------|-------------|----------|
| Catch2 C++ test binary | tests/test_wm_process.cpp, asserts via XGetWindowProperty | ✓ |
| Shell driver script per scenario | Scripts using xprop/xwininfo/xdotool via add_test | |
| Both — C++ for assertions, shell for smoke | Split by purpose | |

**User's choice:** Catch2 C++ test binary

| Option | Description | Selected |
|--------|-------------|----------|
| Scan-finding-driven subset | Items tied to the four findings plus core lifecycle | |
| All 13 items | Full coverage of every missing-coverage bullet | ✓ |
| Only the four scan findings | Minimum to close recorded hazards | |

**User's choice:** All 13 items
**Notes:** Claude noted this substantially expands 08-01 and will likely require splitting it into multiple plans.

---

## Shape-Fallback Architecture

**Code scan before questions:** ~17 `XShapeCombineRectangles` call sites in `src/Border.cpp`; `hasShapeExtension()` exists at `include/Manager.h:55` but guards only `src/Border.cpp:151`.

| Option | Description | Selected |
|--------|-------------|----------|
| Single wrapper function | One combineShape(...) that no-ops when unavailable | ✓ |
| Guard each call site | Early-return check per site | |
| Injectable shape backend | Interface with real and no-op implementations | |

**User's choice:** Single wrapper function

| Option | Description | Selected |
|--------|-------------|----------|
| Hidden env var | WM2_FORCE_NO_SHAPE=1, no user-facing surface | ✓ |
| Documented CLI flag | --no-shape as a real user option | |
| Both | CLI for users, env var for the harness | |

**User's choice:** Hidden env var

| Option | Description | Selected |
|--------|-------------|----------|
| Fontconfig-level fallback, not core fonts | Prove the fontconfig chain resolves on every target | ✓ |
| Implement real core-font fallback | Add an XLFD rendering path | |
| Fail clean with a clear message | Exit with an actionable error | |

**User's choice:** Fontconfig-level fallback, not core fonts
**Notes:** Claude raised that XDIS-04's literal wording conflicts with Phase 4's deliberate removal of all core-font code.

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, amend and note why | Update XDIS-04 text plus a CONTEXT.md note | ✓ |
| Leave text, record a deviation | Keep as written, log accepted deviation | |

**User's choice:** Yes, amend and note why

---

## Evidence & Signoff Bar

| Option | Description | Selected |
|--------|-------------|----------|
| Committed under .planning/phases/08-*/evidence/ | Evidence beside phase artifacts | ✓ |
| Top-level docs/release-evidence/ | Durable project-level location | |
| Generated, gitignored, summarized only | Only a signed-off summary committed | |

**User's choice:** Committed under the phase dir

| Option | Description | Selected |
|--------|-------------|----------|
| Debug + Release build clean, full ctest passing | Both configurations, entire suite | ✓ |
| ASan/UBSan clean | No UAF, invalid enum, double-free, OOB | ✓ |
| Static analysis — no new findings vs baseline | cppcheck + clang-tidy vs baseline | ✓ |
| Runtime smoke transcript captured | Xephyr run with xprop/xwininfo output | ✓ |

**User's choice:** All four selected — every gate is a hard blocker, none waivable

| Option | Description | Selected |
|--------|-------------|----------|
| Automate what XTEST can, manual for the rest | Automate mechanical interactions, human pass for visual/feel | ✓ |
| Fully manual checklist run | Human runs the whole interaction checklist | |
| Fully automated, no manual pass | Drop the manual signoff | |

**User's choice:** Automate what XTEST can, manual for the rest

| Option | Description | Selected |
|--------|-------------|----------|
| Move into the phase dir | Treat as a Phase 8 working artifact | |
| Keep at repo root | Ongoing developer release/signoff checklist | ✓ |
| Keep at root, move to docs/ later | Relocate in a later phase | |

**User's choice:** Keep at repo root

---

## Focus Policy Model

**Code scan before questions:** `clickToFocus` / `raiseOnFocus` / `autoRaise` confirmed to have zero runtime consumers — referenced only in `tests/test_config.cpp`. Runtime unconditionally performs delayed focus-follows-pointer with auto-raise, while all three config defaults are `false`.

| Option | Description | Selected |
|--------|-------------|----------|
| Keep 3 booleans, wire them up | Preserve Phase 5 config surface and CLI flags | ✓ |
| Single focus-policy= enum | Clearer for non-programmers; breaks existing keys and ~15 tests | |
| Enum with booleans as aliases | Both, with more parsing complexity | |

**User's choice:** Keep 3 booleans, wire them up

| Option | Description | Selected |
|--------|-------------|----------|
| Preserve today's behavior as the default | No surprise for existing users | ✓ |
| Match upstream wm2 defaults | 1997 fidelity, changes current behavior | |
| Click-to-focus by default | Safer over high-latency VNC/RDP | |

**User's choice:** Preserve today's behavior as the default

| Option | Description | Selected |
|--------|-------------|----------|
| Timestamp compare, deny stale, mark urgent | Map unfocused + set urgency/demands-attention hint | ✓ |
| Timestamp compare, silently deny | No visual signal | |
| Time-window heuristic | Grant if launcher interacted within N seconds | |

**User's choice:** Timestamp compare, deny stale, mark urgent

| Option | Description | Selected |
|--------|-------------|----------|
| Configurable with an off switch | Default on, disableable for legacy clients | ✓ |
| Always on | No config surface | |
| You decide | Leave to the planner | |

**User's choice:** Configurable with an off switch

---

## Window Rules Syntax

| Option | Description | Selected |
|--------|-------------|----------|
| Repeated ordered key groups | rule-match-class= starts a rule; Phase 7 menu-entry-* precedent | ✓ |
| Indexed keys | rule.1.match-class= — explicit, order-independent | |
| Single-line compound value | Compact, needs its own mini-grammar | |

**User's choice:** Repeated ordered key groups

| Option | Description | Selected |
|--------|-------------|----------|
| Exact + substring, multiple criteria ANDed | Predictable, nothing to learn | ✓ |
| Glob patterns | Shell-style wildcards | |
| Full regex | Most powerful, ReDoS/error surface | |

**User's choice:** Exact + substring, multiple criteria ANDed

| Option | Description | Selected |
|--------|-------------|----------|
| All apply in file order, later wins per-action | Allows broad rule + specific override | ✓ |
| First match wins, stop | Simplest to debug | |
| Most specific wins | Often surprising | |

**User's choice:** All apply in file order, later wins per-action

| Option | Description | Selected |
|--------|-------------|----------|
| no-decorate | Reuses Phase 6 dock border-strip path | ✓ |
| position + size | Force geometry at map time | ✓ |
| skip-taskbar | Set SKIP_TASKBAR/SKIP_PAGER | ✓ |
| specific workspace | Nothing to target under single-desktop design | |

**User's choice:** no-decorate, position + size, skip-taskbar
**Notes:** Claude flagged in the option text that the WM is single-desktop by design (Phase 6), so the workspace action has no target; the user correctly excluded it. RULES-02 wording flagged for amendment.

---

## Xrandr Reflow Policy

**Code scan before questions:** no Xrandr/XRR/randr references anywhere in `src/`, `include/`, or `CMakeLists.txt` — greenfield.

| Option | Description | Selected |
|--------|-------------|----------|
| Clamp offscreen windows back into view | Move, not resize; leave visible windows alone | ✓ |
| Clamp position and shrink oversized windows | Also resize to fit | |
| Proportionally rescale everything | Scale all geometry | |
| Leave windows alone | Only update workarea | |

**User's choice:** Clamp offscreen windows back into view

| Option | Description | Selected |
|--------|-------------|----------|
| Required dependency | libxrandr in pkg-config + runtime availability check | ✓ |
| Optional, compile-time conditional | #ifdef HAVE_XRANDR | |
| Runtime dlopen | No link-time dependency | |

**User's choice:** Required dependency

| Option | Description | Selected |
|--------|-------------|----------|
| Single accessor with Xrandr/core fallback | One source of truth, mirrors the Shape wrapper | ✓ |
| Handle root ConfigureNotify as a fallback | Second signal path | |
| Both | Xrandr preferred, ConfigureNotify second, core as floor | |

**User's choice:** Single accessor with Xrandr/core fallback

| Option | Description | Selected |
|--------|-------------|----------|
| Single screen only, document explicitly | Treat X screen as one rectangle, state in release notes | ✓ |
| Per-CRTC awareness | Real multi-monitor support | |

**User's choice:** Single screen only, document explicitly

---

## Remote-Target Test Scope

**Host scan before questions:** `xrdp` present; no VNC server installed; `tigervnc-standalone-server` installable from apt.

| Option | Description | Selected |
|--------|-------------|----------|
| TigerVNC | Most common VPS/droplet VNC server | ✓ |
| XRDP | Already installed here, cheap to set up | ✓ |
| TightVNC | Superseded by TigerVNC, Xvnc-derived overlap | |
| X2Go | NX-based, most different, most involved | ✓ |

**User's choice:** TigerVNC, XRDP, X2Go

| Option | Description | Selected |
|--------|-------------|----------|
| Declare untested with rationale | Accepted deviation naming reason and closest tested proxy | ✓ |
| Claim support via proxy reasoning | Unverified claim in release notes | |
| Drop from XDIS-05 | Amend requirement to verified targets only | |

**User's choice:** Declare untested with rationale (applies to TightVNC)

| Option | Description | Selected |
|--------|-------------|----------|
| Yes for automation, real servers for signoff | Xephyr carries ctest; real servers once per release | ✓ |
| Automate against real servers too | Strongest proof, slow and flaky | |
| Xephyr only | Leaves XDIS-05 essentially unverified | |

**User's choice:** Yes for automation, real servers for signoff

| Option | Description | Selected |
|--------|-------------|----------|
| Measure RSS + idle CPU, assert thresholds | Real pass/fail bar against a documented budget | ✓ |
| Constrained cgroup/VM run | Most faithful, more setup | |
| Record measurements, no threshold | Numbers as evidence only | |

**User's choice:** Measure RSS + idle CPU, assert thresholds

---

## Claude's Discretion

- Xvfb display allocation strategy (dynamic vs `:99` with a guard) — explicitly deferred to the planner
- `combineShape()` signature and how the Shape operation variants collapse into it
- Exact `rule-*` action key names within the repeated-group shape
- Static-analysis baseline file format and location
- How 08-01 splits into plans given all 13 coverage items
- Precise RSS / idle-CPU thresholds
- Whether `libXtst` is linked test-only or unconditionally

## Deferred Ideas

- Per-CRTC / multi-monitor awareness — new capability, own phase
- `specific workspace` rule action — blocked by single-desktop design
- `--no-shape` as a user-facing CLI flag — only the hidden env var ships in Phase 8
- `focus-policy=` enum config key — reconsider alongside the Phase 9 config GUI

## Requirement Amendment Flags

Two requirements cannot be met as literally written and were explicitly flagged rather than quietly ignored:

- **XDIS-04** — "core X font fallback" conflicts with Phase 4's removal of core-font code; amend to fontconfig-level fallback (user approved amendment)
- **RULES-02** — "specific workspace" action has no target under single-desktop design; annotate with the exclusion reason
