---
phase: 6
slug: ewmh-compliance
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-07
---

# Phase 6 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 v3.14.0 (via FetchContent) |
| **Config file** | CMakeLists.txt |
| **Quick run command** | `ctest --test-dir build -R test_ewmh --output-on-failure` |
| **Full suite command** | `cmake --build build && cd build && ctest --output-on-failure` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `ctest --test-dir build -R test_ewmh --output-on-failure`
- **After every plan wave:** Run `cmake --build build && cd build && ctest --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 10 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 06-01-01 | 01 | 1 | EWMH-01, EWMH-02, EWMH-07, EWMH-08 | — | N/A | smoke | `ctest -R ewmh_supported` | Wave 0 | ⬜ pending |
| 06-01-02 | 01 | 1 | EWMH-03, EWMH-04 | — | Validate ClientMessage format=32 | integration | `ctest -R ewmh_client_list` | Wave 0 | ⬜ pending |
| 06-01-03 | 01 | 1 | EWMH-05 | T-6-01 | Re-validate on PropertyNotify | unit | `ctest -R ewmh_window_type` | Wave 0 | ⬜ pending |
| 06-02-01 | 02 | 1 | EWMH-06 | — | Add/remove/toggle semantics per spec | unit | `ctest -R ewmh_state` | Wave 0 | ⬜ pending |
| 06-02-02 | 02 | 1 | EWMH-09 | T-6-02 | Clamp strut values to screen dims | unit | `ctest -R ewmh_workarea` | Wave 0 | ⬜ pending |
| 06-02-03 | 02 | 1 | EWMH-05 | — | DOCK/NOTIFICATION: no decoration | integration | `ctest -R ewmh_window_type` | Wave 0 | ⬜ pending |
| 06-03-01 | 03 | 2 | EWMH-06 | — | Circular gesture on frame window | manual | Visual verify on Xvfb | N/A | ⬜ pending |
| 06-03-02 | 03 | 2 | EWMH-06 | — | Tab click toggles maximize | manual | Visual verify on Xvfb | N/A | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_ewmh.cpp` — covers EWMH-01 through EWMH-09 (atom interning, property setting, state machine)
- [ ] CMakeLists.txt: Add `test_ewmh` executable linked against Catch2 + X11, with Xvfb fixture

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Circular right-button gesture triggers fullscreen | EWMH-06 | Requires mouse gesture simulation not available in Xvfb test harness | Run WM on Xvfb, create window, perform circular gesture on frame, verify fullscreen toggle |
| Tab click toggles maximize | EWMH-06 | Button event targeting on shaped frame requires visual verification | Run WM on Xvfb, create window, click tab area, verify maximize toggle |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 10s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
