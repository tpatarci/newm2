---
phase: 2
slug: event-loop-modernization
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-07
---

# Phase 2 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 v3.14.0 |
| **Config file** | CMakeLists.txt (FetchContent) |
| **Quick run command** | `ctest -R test_smoke --output-on-failure` |
| **Full suite command** | `ctest --output-on-failure` |
| **Estimated runtime** | ~10 seconds |

---

## Sampling Rate

- **After every task commit:** Run `ctest -R test_smoke --output-on-failure`
- **After every plan wave:** Run `ctest --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 10 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 02-01-01 | 01 | 1 | EVNT-01 | — | N/A | unit | `ctest -R test_eventloop` | ❌ W0 | ⬜ pending |
| 02-01-02 | 01 | 1 | EVNT-03 | T-2-01 | Signal handler only calls write() + volatile assignment | unit | `ctest -R test_eventloop` | ❌ W0 | ⬜ pending |
| 02-01-03 | 01 | 1 | EVNT-01 | — | N/A | unit | `ctest -R test_eventloop` | ❌ W0 | ⬜ pending |
| 02-02-01 | 02 | 2 | EVNT-02 | — | N/A | unit | `ctest -R test_autoraise` | ❌ W0 | ⬜ pending |
| 02-02-02 | 02 | 2 | EVNT-02 | — | N/A | unit | `ctest -R test_autoraise` | ❌ W0 | ⬜ pending |
| 02-02-03 | 02 | 2 | EVNT-02 | — | N/A | integration | `ctest --output-on-failure` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_eventloop.cpp` — stubs for EVNT-01 (poll-based loop), EVNT-03 (signal shutdown)
- [ ] `tests/test_autoraise.cpp` — stubs for EVNT-02 (timer computation, deadline arithmetic)

*If none: "Existing infrastructure covers all phase requirements."*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Clean shutdown visibly reparents windows on real display | EVNT-03 | Requires running WM with visible windows | Run WM, open xterm, send SIGTERM, verify xterm loses frame |
| Auto-raise visual timing matches original wm2 | EVNT-02 | Subjective timing perception | Hover over window, observe raise happens after ~400ms stillness |

*If none: "All phase behaviors have automated verification."*

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 10s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
