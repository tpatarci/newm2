---
phase: 8
slug: xrandr-vnc-compatibility-focus-rules
# status lifecycle: draft (seeded by plan-phase) → validated (set by validate-phase §6)
# audit-milestone §5.5 distinguishes NOT-VALIDATED (draft) from PARTIAL (validated + nyquist_compliant: false) (#2117)
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-08-11
---

# Phase 8 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.
> Seeded from `08-RESEARCH.md` § Validation Architecture.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 v3.14.0 via `FetchContent` (`CMakeLists.txt:48-58`) |
| **Config file** | `CMakeLists.txt` (no separate config); `catch_discover_tests` per target |
| **Quick run command** | `ctest --test-dir build/debug -R 'config\|rules\|autoraise\|raii\|eventloop' --output-on-failure` |
| **Full suite command** | `ctest --test-dir build/debug --output-on-failure` |
| **Estimated runtime** | quick ~1s (display-free targets); full suite TBD once Xvfb-backed targets land |

**Current surface:** 135 `TEST_CASE`s across 11 files — `config` 40, `ewmh` 16, `raii` 16, `desktopentry` 12, `client` 11, `appcache` 8, `binaryscanner` 8, `smoke` 7, `autoraise` 6, `xft_poc` 6, `eventloop` 5.

**Blocking precondition:** `libxft-dev` + `libfontconfig1-dev` are not installed on this host and the stale `build/` tree records `XFT_FOUND:INTERNAL=1`. `build/` must be deleted, not reused, before any gate below is meaningful.

---

## Sampling Rate

- **After every task commit:** Run `ctest --test-dir build/debug -R 'config|rules|autoraise|raii|eventloop' --output-on-failure`
- **After every plan wave:** Run `ctest --test-dir build/debug --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Phase gate (D-04, all four mandatory):** Debug full ctest green **and** Release full ctest green **and** ASan/UBSan full ctest with zero actionable reports **and** static analysis with no new findings — then the runtime smoke transcript
- **Max feedback latency:** 5 seconds (quick), full suite budget set once Xvfb targets exist

---

## Per-Task Verification Map

*Task IDs are assigned by the planner; this map is seeded from the research requirement→test map and reconciled by `/gsd-validate-phase`.*

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| TBD | 01 | 0 | TEST-07 | — | Preflight fails loudly before any behavioral test | fixture | `scripts/preflight.sh` as `FIXTURES_SETUP` | ❌ W0 | ⬜ pending |
| TBD | 01 | — | TEST-05 | T-8-UAF | `eventDestroy` client lifetime hazard proven fixed under ASan | integration | `ctest --test-dir build/asan -R wm_` | ❌ W0 | ⬜ pending |
| TBD | 01 | — | TEST-05 | — | 13 checklist coverage items (destroy, dock destroy, hide/unhide, config→runtime, X11 error paths, no-Shape, size hints, all gravities, EWMH client messages, fullscreen/maximize restore, malformed properties, 100-window stress) | integration | `ctest -R wm_` | ❌ W0 | ⬜ pending |
| TBD | 01 | — | TEST-06 | — | Debug + Release + ASan/UBSan builds all green | build gate | `scripts/gates/build-all.sh` | ❌ W0 | ⬜ pending |
| TBD | 01 | — | TEST-06 | — | No new cppcheck/clang-tidy findings vs baseline | static gate | `scripts/analysis/run-static-analysis.sh` | ❌ W0 | ⬜ pending |
| TBD | 02 | — | XDIS-01 | T-8-STRUT | Resolution change repositions offscreen windows, clamped to screen; visible ones untouched | integration | `ctest --test-dir build/debug -R wm_geometry --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | 02 | — | XDIS-01 | — | `XRRUpdateConfiguration` refreshes `screenWidth()/Height()`; handler idempotent across duplicate events | integration | `ctest -R wm_geometry` | ❌ W0 | ⬜ pending |
| TBD | 02 | — | XDIS-02 | — | WM starts and manages windows on `Xvfb -extension RANDR` | integration | `ctest -R wm_norandr` (fixture `xvfb_norandr`) | ❌ W0 | ⬜ pending |
| TBD | 02 | — | XDIS-03 | — | `WM2_FORCE_NO_SHAPE=1` → rectangular frame, zero Shape requests, single call site | integration + guard | `ctest -R 'wm_noshape\|shape_invariant'` | ❌ W0 | ⬜ pending |
| TBD | 02 | — | XDIS-04 | — | Fonts resolve and the WM does not `fatal()` on `Xvfb -extension RENDER` | integration | `ctest -R wm_norender` (fixture `xvfb_norender`) | ❌ W0 | ⬜ pending |
| TBD | 02 | — | XDIS-04 | — | fontconfig fallback chains resolve (`fc-match`) | preflight | `scripts/preflight.sh` | ❌ W0 | ⬜ pending |
| TBD | 02 | — | D-32 | — | RSS and idle CPU within documented budget under multi-window load | integration | `ctest -R wm_resource_budget` | ❌ W0 | ⬜ pending |
| TBD | 03 | — | FOCUS-01 | T-8-FOCUS | Stale `_NET_WM_USER_TIME` → mapped unfocused + `DEMANDS_ATTENTION` set | integration | `ctest -R wm_focus` | ❌ W0 | ⬜ pending |
| TBD | 03 | — | FOCUS-01 | — | `_NET_WM_USER_TIME == 0` → not focused; property absent → focused (D-19) | integration | `ctest -R wm_focus` | ❌ W0 | ⬜ pending |
| TBD | 03 | — | FOCUS-01 | T-8-FOCUS | `_NET_ACTIVE_WINDOW` source=2 granted, source=1 arbitrated | integration | `ctest -R wm_focus` | ❌ W0 | ⬜ pending |
| TBD | 03 | — | FOCUS-02 | — | Each of the three focus booleans changes observable runtime behavior | integration | `ctest -R wm_focus` | ❌ W0 | ⬜ pending |
| TBD | 03 | — | FOCUS-02 | — | Config parsing + CLI precedence for the booleans and the new off switch | unit | `ctest -R config` | ✅ extend `tests/test_config.cpp` | ⬜ pending |
| TBD | 03 | — | RULES-01 | T-8-CFG | `rule-*` repeated-group parsing, ordering, malformed input, orphan keys, length limits | unit | `ctest -R rules` | ❌ W0 | ⬜ pending |
| TBD | 03 | — | RULES-01 | — | Exact vs substring, AND across criteria, all 4 window types | unit | `ctest -R rules` | ❌ W0 | ⬜ pending |
| TBD | 03 | — | RULES-02 | T-8-STRUT | `no-decorate` / `position`+`size` / `skip-taskbar` applied at map, clamped via `ensureVisible()`; later-wins ordering | integration | `ctest -R wm_rules` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] **Install `libxft-dev` + `libfontconfig1-dev`** — not a file, but the true Wave 0 blocker
- [ ] Delete stale `build/`; create `build/debug`, `build/release`, `build/asan`
- [ ] `scripts/preflight.sh` — TEST-07, unblocks everything else
- [ ] `tests/support/WmFixture.h` — fork/exec + dynamic display allocation + readiness polling + ASan report collection
- [ ] `tests/support/XTestDriver.h` — dedicated connection + `XTestGrabControl`
- [ ] `tests/test_wm_process.cpp` — harness self-test plus lifecycle coverage items
- [ ] `tests/test_wm_geometry.cpp`, `tests/test_wm_fallbacks.cpp`, `tests/test_wm_focus.cpp`, `tests/test_wm_rules.cpp`, `tests/test_rules.cpp`
- [ ] `CMakeLists.txt`: add `xrandr` to required modules (lines 11-14), add `xtst` **test-only**, add `xvfb_norandr` / `xvfb_norender` fixtures, add `RESOURCE_LOCK` to the `:99` group
- [ ] `scripts/analysis/cppcheck-suppressions.xml` + `.clang-tidy` — the two-mechanism D-03 gate
- [ ] `lsan.supp` — libX11 / libXft / fontconfig suppressions
- [ ] Update `COMPILED_CODE_BEHAVIOR_CHECKLIST.md:88-91` expected-surface baseline (102 → 135)
- [ ] `evidence/` directory (D-05)

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Capability + EWMH evidence captured per remote target | XDIS-05 | TigerVNC / TightVNC / XRDP / X2Go cannot be stood up in CI on this host (D-29, D-31) | Run `scripts/capture-display-capabilities.sh` inside a real session of each target; commit transcript to `evidence/` |
| Runtime smoke transcript (`xprop -root`, `xwininfo -root -tree`) + interaction results | TEST-08 | Requires a real interactive session and human confirmation of visual behavior | `scripts/capture-display-capabilities.sh` → `evidence/`; record pass/fail per interaction |
| Rotated `FcMatrix` Xft font on a RENDER-less server | XDIS-04 | Open Question 1 — unverified; `src/Border.cpp:49` currently calls `fatal()` on load failure | Spike first in plan 08-02; make the path non-fatal regardless of outcome, then assert non-fatal via `ctest -R wm_norender` |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 5s (quick suite)
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
