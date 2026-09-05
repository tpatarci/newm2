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
| **Config file** | `CMakeLists.txt` (no separate config); `catch_discover_tests(<target> ADD_TAGS_AS_LABELS)` per target |
| **Test selection** | **By label, never by name.** `catch_discover_tests` registers each test under its `TEST_CASE` *name*, so `ctest -R <catch2-tag>` matches nothing and ctest exits 0 on zero matches. `ADD_TAGS_AS_LABELS` (Catch2 v3.14) promotes every tag to a ctest LABEL; gates then select with the anchored `ctest -L '^<tag>$'`. Anchoring is required — `-L` is an unanchored regex, so a bare `-L config` would also match `wm_config_runtime`. Established in 08-01 Step F as D-33. |
| **Zero-match backstop** | Every ctest invocation in this phase carries `--no-tests=error` (ctest ≥ 3.20; this host runs 3.22.1), which turns a zero-match run into exit code 8 instead of a silent success. Verified on this host: `ctest -L '^zzz_nomatch$'` → exit 0 without the flag, exit 8 with it. |
| **Quick run command** | `ctest --test-dir build/debug -L '^(config\|rules\|autoraise\|raii\|eventloop)$' --output-on-failure --no-tests=error` |
| **Full suite command** | `ctest --test-dir build/debug --output-on-failure --no-tests=error` |
| **Estimated runtime** | quick ~1s (display-free targets); full suite TBD once Xvfb-backed targets land |

**Current surface:** 135 `TEST_CASE`s across 11 files, counted by their actual Catch2 tag (the value `-L` will select once D-33 lands) — `config` 40, `ewmh` 16, `raii` 16, `desktopentry` 12, `client` 11, `appcache` 8, `binaryscanner` 8, `smoke` 7, `autoraise` 6, `xft` 6, `eventloop` 5.

> Note the tag is `xft`, not `xft_poc`: `tests/test_xft_poc.cpp` tags its cases `[xft][poc]`. Gates that used to say `-R xft_poc` now say `-L '^xft$'`. This is the one place where the plan-set's old token was not a real tag at all, as opposed to a real tag that `-R` simply could not see.

**Blocking precondition:** `libxft-dev` + `libfontconfig1-dev` are not installed on this host and the stale `build/` tree records `XFT_FOUND:INTERNAL=1`. `build/` must be deleted, not reused, before any gate below is meaningful.

---

## Sampling Rate

- **After every task commit:** Run `ctest --test-dir build/debug -L '^(config|rules|autoraise|raii|eventloop)$' --output-on-failure --no-tests=error`
- **After every plan wave:** Run `ctest --test-dir build/debug --output-on-failure --no-tests=error`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Phase gate (D-04, all four mandatory):** Debug full ctest green **and** Release full ctest green **and** ASan/UBSan full ctest with zero actionable reports **and** static analysis with no new findings — then the runtime smoke transcript
- **Max feedback latency:** 5 seconds (quick), full suite budget set once Xvfb targets exist

---

## Per-Task Verification Map

*Keyed to the 14-plan slicing (plan `NN` executes in wave `NN`; `depends_on` is a single chain). Task IDs are the `Task N` ordinals inside each PLAN.md. Reconciled by `/gsd-validate-phase`.*

*29 rows below. The Automated Command column is transcribed from the plans' `<automated>` blocks; where a block chains a build in front of the gate (`cmake --build ... && ctest ...`), only the ctest half is reproduced here. The plan set holds 43 `<automated>` blocks across 41 tasks; the two tasks without one (08-08 Task 1, 08-14 Task 3) are `checkpoint:decision` and `checkpoint:human-verify` respectively, so neither is a Nyquist gap.*

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| Task 1 | 08-01 | 1 | TEST-05 | T-8-UAF | `eventDestroy` dock-destroy lifetime hazard proven fixed under ASan (coverage item 3) | integration | `ctest --test-dir build/asan -L '^wm_process$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Task 2 | 08-01 | 1 | TEST-07 | — | Preflight fails loudly before any behavioral test | fixture | `ctest --test-dir build/debug -L '^preflight$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Task 2 | 08-01 | 1 | XDIS-04 | — | Both fontconfig fallback chains the WM actually requests resolve to real font files (D-13) | preflight | `bash scripts/preflight.sh` | ❌ W0 | ⬜ pending |
| Task 3 | 08-01 | 1 | TEST-05 | T-8-TMP | Display exclusivity: the WM-bearing group and the `:99` group cannot interleave under `ctest -j` | integration | `ctest --test-dir build/debug -L '^(wm_process\|ewmh\|client)$' -j4 --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Task 1 | 08-02 | 2 | TEST-06 | — | Debug + Release + ASan/UBSan builds all green | build gate | `bash scripts/gates/build-all.sh debug` | ❌ W0 | ⬜ pending |
| Task 2 | 08-02 | 2 | TEST-06 | — | No new cppcheck/clang-tidy findings vs baseline | static gate | `bash scripts/analysis/run-static-analysis.sh` | ❌ W0 | ⬜ pending |
| Tasks 1-3 | 08-03 | 3 | XDIS-03, TEST-05 (item 7) | — | `WM2_FORCE_NO_SHAPE=1` → rectangular frame, zero Shape requests, single `combineShape()` funnel | integration + guard | `ctest --test-dir build/debug -L '^(wm_noshape\|shape_invariant)$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Task 2 | 08-04 | 4 | XDIS-01 | — | Pre-refactor geometry pinned before the 16 cached screen-dimension reads are routed through the accessors (D-27 regression fence) | integration | `ctest --test-dir build/debug -L '^wm_geometry$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Tasks 1-2 | 08-05 | 5 | XDIS-01 | — | `XRRUpdateConfiguration` refreshes `screenWidth()/Height()`; handler idempotent across duplicate events | integration | `ctest --test-dir build/debug -L '^wm_geometry$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Task 2 | 08-05 | 5 | XDIS-01 | T-8-STRUT | Resolution change repositions offscreen windows, clamped to screen; visible ones untouched | integration | `ctest --test-dir build/debug -L '^wm_geometry$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Task 3 | 08-05 | 5 | XDIS-02 | — | WM starts and manages windows on `Xvfb -extension RANDR` (fixture `xvfb_norandr`) | integration | `ctest --test-dir build/debug -L '^(wm_geometry\|wm_norandr)$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Task 1 | 08-06 | 6 | XDIS-04 | T-8-RENDER | Spike resolves Open Question 1: does a rotated Xft pattern load, measure and draw without RENDER | integration | `ctest --test-dir build/debug -L '^xft_norender_spike$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Tasks 2-3 | 08-06 | 6 | XDIS-04 | T-8-FONT | WM does not `fatal()` on `Xvfb -extension RENDER`; the degradation rung is announced; frames still render (fixture `xvfb_norender`) | integration | `ctest --test-dir build/debug -L '^(wm_norender\|wm_noshape\|xft_norender_spike)$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Task 1 | 08-07 | 7 | FOCUS-02 | — | The three focus booleans are wired into the manager without regressing the existing raise/auto-raise timing paths | integration | `ctest --test-dir build/debug -L '^(autoraise\|eventloop\|wm_process)$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Task 3 | 08-07 | 7 | FOCUS-02 | — | Each of the three focus booleans changes observable runtime behavior | integration | `ctest --test-dir build/debug -L '^wm_focus$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Task 2 | 08-07 | 7 | FOCUS-02 | — | Config parsing + CLI precedence for the booleans and the new off switch; D-17 defaults | unit | `ctest --test-dir build/debug -L '^config$' --output-on-failure --no-tests=error` | ✅ extend `tests/test_config.cpp` | ⬜ pending |
| Task 3 | 08-08 | 8 | FOCUS-01 | T-8-FOCUS | Stale `_NET_WM_USER_TIME` → mapped unfocused + `DEMANDS_ATTENTION` set | integration | `ctest --test-dir build/debug -L '^wm_focus$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Task 3 | 08-08 | 8 | FOCUS-01 | — | `_NET_WM_USER_TIME == 0` → not focused; property absent → focused (D-19) | integration | `ctest --test-dir build/debug -L '^wm_focus$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Task 4 | 08-08 | 8 | FOCUS-01 | T-8-FOCUS | `_NET_ACTIVE_WINDOW` source=2 granted, source=1 arbitrated | integration | `ctest --test-dir build/debug -L '^wm_focus$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Tasks 2-3 | 08-09 | 9 | RULES-01 | T-8-CFG | `rule-*` repeated-group parsing, ordering, malformed input, orphan keys, length limits | unit | `ctest --test-dir build/debug -L '^rules$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Tasks 1, 3 | 08-09 | 9 | RULES-01 | — | Exact vs substring, AND across criteria, all 4 window types | unit | `ctest --test-dir build/debug -L '^rules$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Task 1 | 08-10 | 10 | RULES-01 | — | `XGetClassHint` read at manage time supplies `res_name`/`res_class` to the matcher (the runtime half of RULES-01) | integration | `ctest --test-dir build/debug -L '^(wm_process\|client)$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Tasks 2-3 | 08-10 | 10 | RULES-02 | T-8-STRUT | `no-decorate` / `position`+`size` / `skip-taskbar` applied at map, clamped via `ensureVisible()`; later-wins ordering | integration | `ctest --test-dir build/debug -L '^wm_rules$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Tasks 1-3 | 08-11 | 11 | TEST-05 | — | Coverage items 2, 4, 8, 9: destroy, hide/unhide hidden-list, size-hint resize constraints, all gravities | integration | `ctest --test-dir build/debug -L '^(wm_lifecycle\|wm_sizehints\|wm_gravity)$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Tasks 1-3 | 08-12 | 12 | TEST-05 | — | Coverage items 10, 11, 12: EWMH client-message matrix, fullscreen/maximize restore, malformed client properties | integration | `ctest --test-dir build/debug -L '^(wm_state\|wm_fsmax\|wm_props)$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Tasks 1-2 | 08-13 | 13 | TEST-05 | — | Coverage items 5, 6: config→runtime behaviour, terminating X11 error paths and the `--help` flag | integration | `ctest --test-dir build/debug -L '^(wm_config_runtime\|wm_errors)$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Task 3 | 08-13 | 13 | TEST-05 | — | Coverage item 13: hundred-window stress cycle clean under the sanitizer | integration | `ctest --test-dir build/asan -L '^wm_stress$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Task 1 | 08-14 | 14 | D-32 | — | RSS and idle CPU within a budget **derived from a measurement**, not invented | integration | `ctest --test-dir build/debug -L '^wm_resource_budget$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| Task 4 | 08-14 | 14 | TEST-06 | — | All four hard blockers pass together at phase end, none waived (D-04) | build + static gate | `bash scripts/gates/build-all.sh && bash scripts/analysis/run-static-analysis.sh && ctest --test-dir build/debug --output-on-failure --no-tests=error && ctest --test-dir build/release --output-on-failure --no-tests=error && ctest --test-dir build/asan --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] **Install `libxft-dev` + `libfontconfig1-dev`** — not a file, but the true Wave 0 blocker
- [ ] Delete stale `build/`; create `build/debug`, `build/release`, `build/asan`
- [ ] `scripts/preflight.sh` — TEST-07, unblocks everything else
- [ ] `tests/support/WmFixture.h` — fork/exec + dynamic display allocation + readiness polling + ASan report collection
- [ ] `tests/support/XTestDriver.h` — dedicated connection + `XTestGrabControl`
- [ ] `tests/test_wm_process.cpp` — harness self-test plus lifecycle coverage items
- [ ] `tests/test_wm_geometry.cpp` (08-04), `tests/test_wm_fallbacks.cpp` (08-05/08-06), `tests/test_wm_focus.cpp` (08-07), `tests/test_rules.cpp` (08-09), `tests/test_wm_rules.cpp` (08-10)
- [ ] `tests/test_wm_lifecycle.cpp` (08-11), `tests/test_wm_state.cpp` (08-12), `tests/test_wm_runtime.cpp` (08-13), `tests/test_wm_resource.cpp` (08-14)
- [ ] `scripts/capture-display-capabilities.sh` and `docs/RELEASE-NOTES.md` (08-14)
- [ ] `CMakeLists.txt`: add `xrandr` to required modules (lines 11-14), add `xtst` **test-only**, add `xvfb_norandr` / `xvfb_norender` fixtures, add `RESOURCE_LOCK` to the four `:99` targets (the `DISPLAY=:99` count stays at 4 — new WM-bearing targets allocate their own display)
- [ ] `CMakeLists.txt`: append `ADD_TAGS_AS_LABELS` to **all** eleven existing `catch_discover_tests()` calls (lines 70, 98, 117, 128, 141, 160, 176, 191, 201, 211, 223) and to every target the phase adds — D-33, 08-01 Step F. Until this lands, *every* `-L` gate in the table above selects zero tests; with `--no-tests=error` they fail loudly rather than passing, which is the intended failure mode but still blocks the phase
- [ ] `CMakeLists.txt`: give the hand-written `preflight` test an explicit `LABELS "preflight"` — `ADD_TAGS_AS_LABELS` only reaches Catch2 cases, and without the manual label `preflight` would be the one gate still needing `-R`
- [ ] `scripts/analysis/cppcheck-suppressions.xml` + `.clang-tidy` — the two-mechanism D-03 gate
- [ ] `lsan.supp` — libX11 / libXft / fontconfig suppressions
- [ ] Update `COMPILED_CODE_BEHAVIOR_CHECKLIST.md:88-91` expected-surface baseline — the stale figure is 102; the replacement is the count **measured at signoff**, not a number fixed now. The tree holds 135 today, but 08-11 through 08-14 add roughly ten new test files after this line was written, so 08-14 Task 2 sets it from `grep -c '^TEST_CASE' tests/*.cpp` summed at that moment.
- [ ] `evidence/` directory (D-05)

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Capability + EWMH evidence captured per remote target | XDIS-05 | TigerVNC / TightVNC / XRDP / X2Go cannot be stood up in CI on this host (D-29, D-31) | Run `scripts/capture-display-capabilities.sh` inside a real session of each target; commit transcript to `evidence/` |
| Runtime smoke transcript (`xprop -root`, `xwininfo -root -tree`) + interaction results | TEST-08 | Requires a real interactive session and human confirmation of visual behavior | `scripts/capture-display-capabilities.sh` → `evidence/`; record pass/fail per interaction |
| Rotated `FcMatrix` Xft font on a RENDER-less server | XDIS-04 | Open Question 1 — unverified; `src/Border.cpp:49` currently calls `fatal()` on load failure | Spike first in plan **08-06 Task 1** (`ctest -L '^xft_norender_spike$'`); 08-06 Task 2 makes the path non-fatal regardless of outcome; 08-06 Task 3 asserts non-fatal via `ctest -L '^wm_norender$'` |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] **Every gate selects a non-empty set.** For each distinct `-L` expression in the table, `ctest --test-dir build/debug -L '<expr>' -N` reports `Total Tests:` greater than 0. A gate that matches nothing is a false green, not a pass
- [ ] **No `-R` tag filters anywhere.** `grep -rn -- "-R " .planning/phases/08-*/08-*.md` returns no Catch2 tag tokens (name-anchored filters like `-R '^somename$'` on hand-written `add_test` entries would be acceptable, but the phase currently has none — `preflight` carries a real label instead)
- [ ] **`--no-tests=error` on every ctest invocation**, in the plans, in `08-VALIDATION.md`, and inside `scripts/gates/build-all.sh`
- [ ] Feedback latency < 5s (quick suite)
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
