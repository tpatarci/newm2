---
schema_version: 1
open_count: 7
waived_count: 0
fixed_count: 0
total_count: 7
last_updated: 2026-08-31T16:10:15.105Z
---

# Broken Windows Ledger

> Cross-phase defect register. With `workflow.windows_enforce` enabled, `/gsd-ship` blocks while `open_count > 0`.
> Waive with `gsd-tools windows waive <id> "<reason>"` (reason required).
> Mark fixed with `gsd-tools windows fixed <id>`.

| id | phase | kind | file | line | description | status | reason | recorded_at | resolved_at |
|----|-------|------|------|------|-------------|--------|--------|-------------|-------------|
| 1 | 08 | unrun-verify | tests/test_wm_geometry.cpp |  | XDIS-02 reflow proven with WM2_FORCE_NO_RANDR on a RANDR-capable server, not on the extension-less server -- no resize is possible there | open |  | 2026-08-11T15:03:59.202Z |  |
| 2 | 08 | deviation | tests/lsan.supp |  | 32-byte libXrandr leak suppressed (library defect on its no-extension path, standalone reproducer recorded in the file) | open |  | 2026-08-11T15:03:59.333Z |  |
| 3 | 08 | deviation | src/Events.cpp |  | T-8-UAF: revert-chain scrub added to eventDestroy; any future Client removal path must call skipInRevert() before freeing | open |  | 2026-08-29T08:59:33.424Z |  |
| 4 | 08 | deviation | src/Events.cpp |  | 08-12 modified src/Events.cpp, src/Border.cpp and include/Client.h beyond the plan's files_modified list; each is a one-place fix for a defect a case in the plan found | open |  | 2026-08-29T12:09:46.815Z |  |
| 5 | 08 | deviation | tests/support/WmFixture.h |  | deferred item 14: stale ASan reports are attributed to the next fixture handed the same display number, so a mutation run poisons later runs; one-line fix left to 08-13 | open |  | 2026-08-29T12:09:46.982Z |  |
| 6 | 08.5 | deviation | .planning/phases/08.5-v1.0-closeout/08.5-10-PLAN.md |  | 08.5-10 Task 1/Task 2 knob-enumeration verify command is a bash syntax error: X="$(... \|\| true); echo ..." is missing the closing double quote, so the gate cannot run as written. Executed the corrected form; no check weakened. | open |  | 2026-08-31T16:10:14.925Z |  |
| 7 | 08.5 | deviation | src/Manager.cpp | 605 | 08.5-10 plan prose calls PropertyChangeMask the last term of the mask assignment spanning src/Manager.cpp:603-605; it is the FIRST term on line 605 and StructureNotifyMask is the last. Citation corrected in the code comment and the record. | open |  | 2026-08-31T16:10:15.105Z |  |

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
  },
  {
    "id": 3,
    "kind": "deviation",
    "phase": "08",
    "file": "src/Events.cpp",
    "line": null,
    "description": "T-8-UAF: revert-chain scrub added to eventDestroy; any future Client removal path must call skipInRevert() before freeing",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-29T08:59:33.424Z",
    "resolved_at": null
  },
  {
    "id": 4,
    "kind": "deviation",
    "phase": "08",
    "file": "src/Events.cpp",
    "line": null,
    "description": "08-12 modified src/Events.cpp, src/Border.cpp and include/Client.h beyond the plan's files_modified list; each is a one-place fix for a defect a case in the plan found",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-29T12:09:46.815Z",
    "resolved_at": null
  },
  {
    "id": 5,
    "kind": "deviation",
    "phase": "08",
    "file": "tests/support/WmFixture.h",
    "line": null,
    "description": "deferred item 14: stale ASan reports are attributed to the next fixture handed the same display number, so a mutation run poisons later runs; one-line fix left to 08-13",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-29T12:09:46.982Z",
    "resolved_at": null
  },
  {
    "id": 6,
    "kind": "deviation",
    "phase": "08.5",
    "file": ".planning/phases/08.5-v1.0-closeout/08.5-10-PLAN.md",
    "line": null,
    "description": "08.5-10 Task 1/Task 2 knob-enumeration verify command is a bash syntax error: X=\"$(... || true); echo ...\" is missing the closing double quote, so the gate cannot run as written. Executed the corrected form; no check weakened.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-31T16:10:14.925Z",
    "resolved_at": null
  },
  {
    "id": 7,
    "kind": "deviation",
    "phase": "08.5",
    "file": "src/Manager.cpp",
    "line": 605,
    "description": "08.5-10 plan prose calls PropertyChangeMask the last term of the mask assignment spanning src/Manager.cpp:603-605; it is the FIRST term on line 605 and StructureNotifyMask is the last. Citation corrected in the code comment and the record.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-31T16:10:15.105Z",
    "resolved_at": null
  }
]
````
