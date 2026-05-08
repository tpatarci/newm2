---
phase: 5
slug: configuration-system
status: draft
nyquist_compliant: true
wave_0_complete: false
created: 2026-05-07
---

# Phase 5 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 (existing) |
| **Config file** | CMakeLists.txt (already configured) |
| **Quick run command** | `ctest --test-dir build -R test_config --output-on-failure` |
| **Full suite command** | `cmake --build build && ctest --test-dir build --output-on-failure` |
| **Estimated runtime** | ~10 seconds |

---

## Sampling Rate

- **After every task commit:** Run `ctest --test-dir build -R test_config --output-on-failure`
- **After every plan wave:** Run `cmake --build build && ctest --test-dir build --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 15 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 05-01-01 | 01 | 1 | CONF-01 | — | N/A | unit | `ctest --test-dir build -R test_config --output-on-failure` | ❌ W0 | ⬜ pending |
| 05-01-02 | 01 | 1 | CONF-02, CONF-03 | — | N/A | unit | `ctest --test-dir build -R test_config --output-on-failure` | ❌ W0 | ⬜ pending |
| 05-02-01 | 02 | 2 | CONF-04 | — | N/A | unit | `ctest --test-dir build -R test_config --output-on-failure` | ❌ W0 | ⬜ pending |
| 05-03-01 | 03 | 3 | CONF-01, CONF-02, CONF-03 | — | N/A | unit | `cmake --build build && ctest --test-dir build --output-on-failure` | ❌ W0 | ⬜ pending |
| 05-03-02 | 03 | 3 | CONF-02, CONF-04 | — | N/A | unit | `cmake --build build && ctest --test-dir build --output-on-failure` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_config.cpp` — stubs for CONF-01 through CONF-04
- [ ] Existing Catch2 + Xvfb infrastructure covers test execution

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Config file creates on first run | CONF-02 | Requires filesystem inspection | Run WM without config, check ~/.config/wm2-born-again/config exists |
| CLI flags override config | CONF-04 | Requires running WM binary with args | Run `wm2-born-again --click-to-focus` and verify behavior |
| Restart picks up new config | CONF-04 | Requires two WM lifecycle runs | Edit config, restart WM, verify new settings applied |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 15s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
