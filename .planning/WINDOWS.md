---
schema_version: 1
open_count: 2
waived_count: 0
fixed_count: 0
total_count: 2
last_updated: 2026-08-11T15:03:59.333Z
---

# Broken Windows Ledger

> Cross-phase defect register. With `workflow.windows_enforce` enabled, `/gsd-ship` blocks while `open_count > 0`.
> Waive with `gsd-tools windows waive <id> "<reason>"` (reason required).
> Mark fixed with `gsd-tools windows fixed <id>`.

| id | phase | kind | file | line | description | status | reason | recorded_at | resolved_at |
|----|-------|------|------|------|-------------|--------|--------|-------------|-------------|
| 1 | 08 | unrun-verify | tests/test_wm_geometry.cpp |  | XDIS-02 reflow proven with WM2_FORCE_NO_RANDR on a RANDR-capable server, not on the extension-less server -- no resize is possible there | open |  | 2026-08-11T15:03:59.202Z |  |
| 2 | 08 | deviation | tests/lsan.supp |  | 32-byte libXrandr leak suppressed (library defect on its no-extension path, standalone reproducer recorded in the file) | open |  | 2026-08-11T15:03:59.333Z |  |

````json
[
  {
    "id": 1,
    "kind": "unrun-verify",
    "phase": "08",
    "file": "tests/test_wm_geometry.cpp",
    "line": null,
    "description": "XDIS-02 reflow proven with WM2_FORCE_NO_RANDR on a RANDR-capable server, not on the extension-less server -- no resize is possible there",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-11T15:03:59.202Z",
    "resolved_at": null
  },
  {
    "id": 2,
    "kind": "deviation",
    "phase": "08",
    "file": "tests/lsan.supp",
    "line": null,
    "description": "32-byte libXrandr leak suppressed (library defect on its no-extension path, standalone reproducer recorded in the file)",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-11T15:03:59.333Z",
    "resolved_at": null
  }
]
````
