---
status: partial
phase: 05-configuration-system
source: [05-VERIFICATION.md]
started: 2026-05-07T00:00:00Z
updated: 2026-05-07T00:00:00Z
---

## Current Test

[awaiting human testing]

## Tests

### 1. WM startup without config file
expected: WM starts cleanly on Xvfb with all default settings applied (colors, timing, focus policy)

### 2. Config file color override
expected: Create ~/.config/wm2-born-again/config with `tab-foreground=red`, restart WM, verify tab label color changes to red

### 3. CLI frame thickness override
expected: Start WM with `wm2-born-again --frame-thickness=3`, verify window frames are noticeably thinner than default (7)

### 4. Config-driven spawn command
expected: Start WM with `wm2-born-again --new-window-command=alacritty` (or another installed terminal), right-click root menu → New Window opens the configured terminal

## Summary

total: 4
passed: 0
issues: 0
pending: 4
skipped: 0
blocked: 0

## Gaps
