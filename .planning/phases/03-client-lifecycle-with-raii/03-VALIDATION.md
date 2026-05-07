---
phase: 03
slug: client-lifecycle-with-raii
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-07
---

# Phase 3 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 v3.14.0 |
| **Config file** | CMakeLists.txt (FetchContent) |
| **Quick run command** | `cd build && cmake --build . --target test_client && DISPLAY=:99 ./test_client` |
| **Full suite command** | `cd build && cmake --build . && ctest --output-on-failure` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cmake --build build --target test_client && DISPLAY=:99 ./build/test_client`
- **After every plan wave:** Run `cd build && cmake --build . && ctest --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 10 seconds

---

## Per-Task Verification Map

> **Note on "File Exists" column:** Plans 01 and 02 do not create test_client.cpp -- it is created in Plan 03 (Wave 3). For Plans 01 and 02, verification relies on build success and grep-based acceptance criteria checks. The "build+grep" designation means the task's automated verify command is `cmake --build` plus grep pattern checks in the acceptance_criteria block. Only Plan 03 tasks (and any tests they define) have a test_client.cpp to run against.

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 03-01-01 | 01 | 1 | CLNT-01 | T-3-01 | ServerGrab RAII wraps XGrabServer/XUngrabServer | build+grep | `cmake --build build` | build+grep | pending |
| 03-01-02 | 01 | 1 | CLNT-01 | -- | XReparentWindow called within ServerGrab scope | build+grep | `cmake --build build` | build+grep | pending |
| 03-01-03 | 01 | 1 | CLNT-02 | T-3-03 | unique_ptr erase triggers ~Client(), no delete this | build+grep | `cmake --build build` | build+grep | pending |
| 03-02-01 | 02 | 1 | CLNT-04 | -- | windowToClient returns Client* for known window O(1) | build+grep | `cmake --build build` | build+grep | pending |
| 03-02-02 | 02 | 1 | CLNT-04 | -- | windowToClient falls back for border windows | build+grep | `cmake --build build` | build+grep | pending |
| 03-02-03 | 02 | 1 | CLNT-05 | -- | Valid state transitions accepted | build+grep | `cmake --build build` | build+grep | pending |
| 03-02-04 | 02 | 1 | CLNT-05 | -- | Invalid state transitions logged but applied | build+grep | `cmake --build build` | build+grep | pending |
| 03-03-01 | 03 | 2 | TEST-03 | -- | Window map lifecycle (create, map, unmap, destroy) | integration | `./test_client "Window lifecycle"` | Wave 3 (Plan 03 creates it) | pending |
| 03-03-02 | 03 | 2 | TEST-03 | -- | Window hide/unhide preserves Client identity | integration | `./test_client "Hide unhide"` | Wave 3 (Plan 03 creates it) | pending |
| 03-03-03 | 03 | 2 | TEST-03 | -- | Window deletion cleans up all resources | integration | `./test_client "Delete cleanup"` | Wave 3 (Plan 03 creates it) | pending |

*Status: pending / green / red / flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_client.cpp` -- stubs for CLNT-01 through CLNT-05 and TEST-03
- [ ] CMakeLists.txt entry for test_client target with Xvfb fixture
- [ ] Test helper utilities: create simple X11 window on Xvfb for lifecycle testing

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| None | -- | -- | -- |

All phase behaviors have automated verification.

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 10s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
