---
phase: 1
slug: build-infrastructure-raii-foundation
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-06
---

# Phase 1 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 v3 (via FetchContent) |
| **Config file** | `CMakeLists.txt` (top-level + `tests/CMakeLists.txt`) |
| **Quick run command** | `ctest --test-dir build --output-on-failure -R raii` |
| **Full suite command** | `ctest --test-dir build --output-on-failure` |
| **Estimated runtime** | ~15 seconds |

---

## Sampling Rate

- **After every task commit:** Run `ctest --test-dir build --output-on-failure -R raii`
- **After every plan wave:** Run `ctest --test-dir build --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 15 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 01-01-01 | 01 | 1 | BLD-01, BLD-05 | — | N/A | build | `cmake -B build && cmake --build build` | ❌ W0 | ⬜ pending |
| 01-01-02 | 01 | 1 | BLD-02 | — | N/A | build | `cmake --build build` (C++17 flags) | ❌ W0 | ⬜ pending |
| 01-02-01 | 02 | 1 | BLD-03 | — | N/A | unit | `ctest -R raii` | ❌ W0 | ⬜ pending |
| 01-02-02 | 02 | 1 | BLD-03 | — | Resource released on scope exit | unit | `ctest -R raii` | ❌ W0 | ⬜ pending |
| 01-03-01 | 03 | 2 | TEST-01, TEST-02, TEST-04 | — | N/A | integration | `ctest` on Xvfb | ❌ W0 | ⬜ pending |
| 01-03-02 | 03 | 2 | BLD-04 | — | N/A | build | Clean build on Ubuntu 22.04 | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_raii.cpp` — stubs for RAII wrapper tests (BLD-03)
- [ ] `tests/test_smoke.cpp` — stub for WM smoke test on Xvfb (TEST-01, TEST-02)
- [ ] Catch2 FetchContent in `tests/CMakeLists.txt` — framework install

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Binary runs on clean Ubuntu 22.04 | BLD-04 | Requires clean OS environment | Fresh VM/containers: `cmake -B build && cmake --build build && ./build/wm2-born-again` |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 15s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
