---
phase: 4
slug: border-xft-font-rendering
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-07
---

# Phase 4 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 3.x |
| **Config file** | CMakeLists.txt (Catch2 integrated via FetchContent) |
| **Quick run command** | `ctest --test-dir build -R xft_poc --output-on-failure` |
| **Full suite command** | `ctest --test-dir build --output-on-failure` |
| **Estimated runtime** | ~10 seconds |

---

## Sampling Rate

- **After every task commit:** Run `ctest --test-dir build --output-on-failure`
- **After every plan wave:** Full suite + manual visual inspection on Xvfb
- **Before `/gsd-verify-work`:** Full suite must be green + visual inspection passed
- **Max feedback latency:** 15 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 04-01-01 | 01 | 1 | VISL-02 | -- | N/A | integration | `ctest -R xft_poc` | ❌ W0 | ⬜ pending |
| 04-01-02 | 01 | 1 | VISL-04 | -- | N/A | unit | `ctest -R xft_poc` | ❌ W0 | ⬜ pending |
| 04-01-03 | 01 | 1 | VISL-02, VISL-04 | -- | N/A | integration | `ctest -R xft_poc` | ❌ W0 | ⬜ pending |
| 04-02-01 | 02 | 2 | VISL-01, VISL-03 | -- | N/A | integration | `ctest --test-dir build --output-on-failure` | N/A | ⬜ pending |
| 04-02-02 | 02 | 2 | VISL-05 | -- | N/A | integration | `ctest --test-dir build --output-on-failure` | N/A | ⬜ pending |
| 04-03-01 | 03 | 2 | VISL-02 | -- | N/A | integration | `ctest --test-dir build --output-on-failure` | N/A | ⬜ pending |
| 04-03-02 | 03 | 2 | VISL-04 | -- | N/A | integration | `ctest --test-dir build --output-on-failure` | N/A | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_xft_poc.cpp` — Xft font loading, rotated font, draw creation, string rendering, UTF-8, color allocation on Xvfb
- [ ] `CMakeLists.txt` update — add Xft/fontconfig pkg-config, add test_xft_poc target
- [ ] Dev package installation: `sudo apt install libxft-dev libfontconfig1-dev`
- [ ] `include/Manager.h` update — add hasShapeExtension() public accessor

*Existing Catch2 + Xvfb infrastructure from Phase 1 covers test harness.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Visual: sideways-tab looks like original wm2 | VISL-01 | Requires human visual comparison | Run wm2-born-again on Xvfb, compare tab appearance to upstream wm2 screenshot |
| Visual: antialiased text vs bitmap | VISL-02 | Requires human visual inspection | Compare tab labels -- should be smooth, not pixelated |
| Visual: non-ASCII rendering | VISL-03 | Font coverage best verified visually | Open window with title "Cafee 日本語" -- verify characters render correctly |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 15s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
