---
status: testing
phase: 09-config-gui-ipc
source: [09-VERIFICATION.md]
started: 2026-09-06T20:10:00Z
updated: 2026-09-06T20:26:00Z
---

## Current Test

number: 1
name: Appearance-page visual review (09-06's own human check)
expected: |
  Look at `.planning/phases/09-config-gui-ipc/evidence/wm2-config/appearance-page.png`,
  `appearance-page-default-size.png` and `appearance-page-font-chooser-open.png`. Every
  control's purpose is obvious to someone who has never edited the config file; the raw
  config-file spelling beside each chooser is readable and clearly secondary; the connection
  state in the header is noticeable without being loud; the reset icon reads as "put this
  back", not as a delete. Reply `approved` or describe the change.
awaiting: user response

## Tests

### 1. Appearance-page visual review (09-06's own human check)
expected: As above. Note: the operator said "Looks fabulous" on 2026-09-06 after seeing the first two screenshots, but did not answer the four questions individually; the tab strip was widened afterwards (quick task 260906-ldw), so `appearance-page.png` is the current look.
result: [pending]

### 2. Whole-tool review (09-07's own human check)
expected: Across `appearance-page.png`, `behaviour-page.png`, `menu-page.png`, `menu-entry-dialog.png`, `close-prompt.png`: nothing on the wrong page or missing; a non-programmer understands each Behaviour label; the Menu page's Add/Edit/Remove flow is obvious; the close prompt's Save / Discard / Cancel are unambiguous about what Discard does to the running desktop; the three pages read as one tool. Known: the Appearance page's "Reset this page" button sits below the fold at the default window size (`COMPILED_CODE_BEHAVIOR_CHECKLIST.md:637`).
result: [pending]

### 3. Remote-desktop pass with a viewer attached (09-09's own human check)
expected: Open `wm2-config` in a real remote-desktop session with a viewer attached (`bash scripts/start-validation-session.sh` stands up TigerVNC on `:11` with `-localhost yes`; connect a viewer over an SSH tunnel). It feels usable over the link; live changes appear on the desktop as fast as locally; the measured resident memory (98.7 MB first settings window, 49.4 MB later ones, WM 11.8 MB, against a 512 MB VPS) is acceptable or changes what the project claims; the evidence bundle carries no hostname, address or session-error-log content (the verifier re-ran this mechanically: clean).
result: [pending]

### 4. Re-run the four build gates at HEAD
expected: `debug`, `asan`, `nogtk`, `release` all green at 573 tests; nogtk skips exactly six cases with stated reasons; release link audit OK (24 entries).
result: passed 2026-09-06 (orchestrator): debug 573/573 at `b68c207`; at `cc74439` asan 573/573 with no sanitizer findings, nogtk 573 registered / 567 passed / 6 skipped with reasons, release 573/573 with link audit OK (24 entries). Logs under /tmp/w12/gate-head-*.log for this session.

### 5. Arrival order of two overlapping `set` messages (09-04 truth 7)
expected: Two connections write `set frame-thickness 11` and `set frame-thickness 21` back to back before reading either reply; the later-arriving write wins, neither is dropped, the window manager stays alive, and every window is re-framed once per applied `set`. No automated case issues two overlapping sets; 09-04's SUMMARY records the ordering claim as human judgement resting on "no second thread and no queue".
result: [pending]

### 6. GUI-driven behaviour change for the eight non-tracer Behaviour settings (09-07 truth 1)
expected: For raise-on-focus, auto-raise, focus-stealing-prevention, auto-raise-delay, pointer-stopped-delay, destroy-window-delay, new-window-command and exec-using-shell: change each in the running `wm2-config` Behaviour page and observe the desktop behaviour change, matching what the corresponding `[wm_config_live]` case observes after a `wm2-ctl set` of the same key. Ledger row 20 records that these eight are proven through the GUI only as far as adoption.
result: [pending]

## Summary

total: 6
passed: 1
issues: 0
pending: 5
skipped: 0
blocked: 0

## Gaps

None recorded yet. Two of the six items (5 and 6) are behaviour joins the project's own ledger already lists as open; the other four are perceptual.
