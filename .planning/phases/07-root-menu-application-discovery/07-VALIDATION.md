---
phase: 7
slug: root-menu-application-discovery
status: draft
nyquist_compliant: true
wave_0_complete: false
created: 2026-07-08
---

# Phase 7 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 v3.14.0 (already vendored via CMake `FetchContent`, per `CMakeLists.txt:46-51`) |
| **Config file** | `CMakeLists.txt` (new test targets added alongside existing `test_config`, `test_client`, etc.) |
| **Quick run command** | `ctest -R "desktopentry\|binaryscanner\|appcache" --output-on-failure` |
| **Full suite command** | `cmake --build build && cd build && ctest --output-on-failure` |
| **Estimated runtime** | ~5-10 seconds |

---

## Sampling Rate

- **After every task commit:** Run `ctest -R "desktopentry|binaryscanner|appcache|config" --output-on-failure`
- **After every plan wave:** Run `cmake --build build && cd build && ctest --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 10 seconds

---

## Per-Task Verification Map

Task IDs are assigned by the planner; rows below are keyed by requirement until plans exist. Cross-reference against each plan's `<verify>` blocks once PLAN.md files are written.

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| TBD | TBD | TBD | APPS-01 | T-7-01 | Reject/neutralize malicious Exec fields (unescaped shell metacharacters, unknown field codes) instead of passing raw to execvp | unit | `ctest -R desktopentry --output-on-failure` | ❌ Wave 0 | ⬜ pending |
| TBD | TBD | TBD | APPS-01 | — | XDG filtering: NoDisplay/Hidden/OnlyShowIn/NotShowIn/TryExec honored per D-05 | unit | `ctest -R desktopentry --output-on-failure` | ❌ Wave 0 | ⬜ pending |
| TBD | TBD | TBD | APPS-02 | T-7-04 | Use `open()`+`fstat()` (not `stat()` then separate `open()`) to avoid TOCTOU on the `/usr/bin` scan | unit | `ctest -R binaryscanner --output-on-failure` | ❌ Wave 0 | ⬜ pending |
| TBD | TBD | TBD | APPS-02 | — | Shebang scripts and non-executable files skipped without attempting ELF parse | unit | `ctest -R binaryscanner --output-on-failure` | ❌ Wave 0 | ⬜ pending |
| TBD | TBD | TBD | APPS-03 | — | `appcache.json` round-trips (write then read produces identical AppEntry list) | unit | `ctest -R appcache --output-on-failure` | ❌ Wave 0 | ⬜ pending |
| TBD | TBD | TBD | APPS-03 | — | Cache invalidation triggers rescan on `.desktop` dir / `/usr/bin` mtime change (D-06) | unit | `ctest -R appcache --output-on-failure` | ❌ Wave 0 | ⬜ pending |
| TBD | TBD | TBD | APPS-04 | — | Manual `menu-entry-*` config keys parse and merge per D-07/D-08 (name override, default Custom category) | unit | `ctest -R "config|appcache" --output-on-failure` | ❌ Wave 0 (extends `test_config.cpp`) | ⬜ pending |
| TBD | TBD | TBD | APPS-05 | — | Root menu displays category submenus with correct entries | manual | Visual verify on Xvfb (see Manual-Only Verifications) | N/A | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_desktopentry.cpp` — covers APPS-01 (pure data parsing incl. Exec/field-code validation, no Xvfb needed, follows `test_config.cpp` pattern)
- [ ] `tests/test_binaryscanner.cpp` — covers APPS-02 (ELF `.dynamic`/DT_NEEDED reader; fixtures can use `/bin/true`, `/bin/ls`, or synthetic minimal ELF byte arrays; no Xvfb needed)
- [ ] `tests/test_appcache.cpp` — covers APPS-03 (pure data round-trip + mtime invalidation, no Xvfb needed)
- [ ] `CMakeLists.txt` additions — three new `add_executable` blocks matching the `test_config` pattern (no X11 link needed for desktopentry/appcache/binaryscanner)
- [ ] No framework install needed — Catch2 already vendored

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Root menu shows category submenus with hover-to-expand | APPS-05 | X11 rendering is not easily assertable in an automated Catch2 test without a screenshot-diffing harness, which this project does not have (consistent with Phase 4/6 precedent) | Run WM on Xvfb, right-click root window, hover a category entry, verify submenu appears with correct discovered/custom entries; click an entry, verify the app launches |
| Discovered app entry launches correctly via execvp | APPS-02, APPS-04 | Process launch side effects are easiest to confirm visually/interactively rather than in a unit test harness | Run WM on Xvfb, open root menu, click a discovered or custom entry, verify the target process starts |

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies — every `type="auto"`/`tdd` task across 07-01..07-06 has an `<automated>` verify command; the sole checkpoint (07-06 Task 2) has an automated pre-check plus a human resume-signal
- [x] Sampling continuity: no 3 consecutive tasks without automated verify — no gaps found
- [x] Wave 0 covers all MISSING references — `tests/test_desktopentry.cpp` (07-01), `tests/test_binaryscanner.cpp` (07-02), `tests/test_appcache.cpp` (07-03) all exist as planned tasks
- [x] No watch-mode flags — confirmed across all verify commands
- [x] Feedback latency < 10s — full-suite estimate ~5-10s (Test Infrastructure table above)
- [x] `nyquist_compliant: true` set in frontmatter

**Revision note (2026-07-08):** gsd-plan-checker flagged pipe-to-tail exit-code masking in the `<verify><automated>` blocks of Plans 07-01 through 07-05 (unguarded `cmd | tail -N` patterns that always report success regardless of the build/compile result) and a missing standalone syntax-check alternative in Plan 07-04 Task 1. All affected commands were corrected to either `set -o pipefail` before the pipe or capture-then-exit with the real upstream exit status; Plan 07-04 Task 1 now has a working `g++ -fsyntax-only` verify command. Re-reviewed against the sign-off criteria above with all fixes applied.

**Approval:** approved (post-revision, planner iteration 1)
