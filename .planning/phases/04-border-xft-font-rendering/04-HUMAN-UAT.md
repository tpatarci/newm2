---
status: partial
phase: 04-border-xft-font-rendering
source: [04-VERIFICATION.md]
started: "2026-05-07T18:30:00.000Z"
updated: "2026-05-07T18:30:00.000Z"
---

## Current Test

[awaiting human testing]

## Tests

### 1. Classic wm2 visual identity
expected: Window border tabs display the classic wm2 sideways-tab visual identity with shaped borders and antialiased rotated text — visually indistinguishable from original wm2 at first glance
result: [pending]

### 2. Non-ASCII rendering quality
expected: Window titles containing non-ASCII characters (accented letters, CJK, etc.) display correctly in tab labels with proper glyphs and no mojibake
result: [pending]

### 3. Shape fallback appearance
expected: On displays without the Shape extension, windows fall back to rectangular frames without crashes or rendering artifacts
result: [pending]

## Summary

total: 3
passed: 0
issues: 0
pending: 3
skipped: 0
blocked: 0

## Gaps
