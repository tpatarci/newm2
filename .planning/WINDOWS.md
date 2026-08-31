---
schema_version: 1
open_count: 12
waived_count: 0
fixed_count: 0
total_count: 12
last_updated: 2026-08-31T23:21:06.341Z
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
| 8 | 08.5 | deviation | src/Buttons.cpp | 156 | Modal XMaskEvent loops reorder lifecycle events and cannot be interrupted by the self-pipe. Interleaving: the operator holds Button1 on a tab, so menu()/move()/resize() sit in XMaskEvent selecting only pointer and expose bits; a MapRequest or DestroyNotify for another client arrives and is left queued behind the grab; SIGTERM then writes the self-pipe, which no XMaskEvent is watching, so shutdown waits for the pointer to be released. Same shape at Buttons.cpp:530, Border.cpp:1495, Client.cpp:1592, Client.cpp:1705, Client.cpp:2228. Destination: 08.5-13 modal-loop round. | open |  | 2026-08-31T23:20:41.464Z |  |
| 9 | 08.5 | deviation | src/Buttons.cpp | 530 | The root menu discards foreign Expose events instead of dispatching them. Interleaving: the menu grab is active in the modal loop; another managed client is uncovered and the server sends it an Expose; the loop's mask matches the event, sees a window that is not the menu, and drops it on the floor rather than routing it to eventExposure(); the client's frame and tab label stay blank until some later unrelated Expose repaints them. Destination: 08.5-13 modal-loop round. | open |  | 2026-08-31T23:20:46.869Z |  |
| 10 | 08.5 | deviation | src/Events.cpp | 551 | Enter coalescing plus an ignored Leave can livelock focus. Interleaving: the pointer crosses two frames quickly, so two EnterNotify events queue; the handler at Events.cpp:551 coalesces them by discarding all but the last and never processes the intervening LeaveNotify at Events.cpp:553; m_focusChanging stays true with m_focusPointerMoved already set, so the pointer-stopped deadline armed via Manager.cpp:1537 is re-armed on every motion and never expires; auto-raise for the window actually under the pointer never fires. Destination: 08.5-13 focus round. | open |  | 2026-08-31T23:20:53.072Z |  |
| 11 | 08.5 | deviation | src/Buttons.cpp | 16 | StructureNotifyMask is silently truncated out of the pointer-grab event mask by a 16-bit intermediate. Interleaving: a move or resize grab is taken with the truncated mask, so while the grab is held the WM is not selecting structure events on the grab window; the client resizes or unmaps itself mid-drag; the ConfigureNotify or UnmapNotify is never delivered to the grab, so the frame geometry the drag commits on release is computed from stale dimensions and the frame ends up the wrong size. Same truncation at Border.cpp:1458. Destination: 08.5-13 mask-width round. | open |  | 2026-08-31T23:20:59.396Z |  |
| 12 | 08.5 | deviation | include/Manager.h | 215 | m_currentTime is declared int rather than Time (unsigned long), so server timestamps are truncated and may go negative. Interleaving: the session runs past the point where the server's 32-bit millisecond clock exceeds INT_MAX, or the server starts near wraparound; a ButtonPress carrying that timestamp is assigned to m_currentTime and truncates to a negative int; the value is then passed back as the time argument of XSetInputFocus or XGrabPointer, where the server reads it as a time far in the past and rejects the request as out of date, so focus or the grab silently does not take. Destination: 08.5-12, alongside the timestamp() path in src/Manager.cpp. | open |  | 2026-08-31T23:21:06.341Z |  |

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
  },
  {
    "id": 8,
    "kind": "deviation",
    "phase": "08.5",
    "file": "src/Buttons.cpp",
    "line": 156,
    "description": "Modal XMaskEvent loops reorder lifecycle events and cannot be interrupted by the self-pipe. Interleaving: the operator holds Button1 on a tab, so menu()/move()/resize() sit in XMaskEvent selecting only pointer and expose bits; a MapRequest or DestroyNotify for another client arrives and is left queued behind the grab; SIGTERM then writes the self-pipe, which no XMaskEvent is watching, so shutdown waits for the pointer to be released. Same shape at Buttons.cpp:530, Border.cpp:1495, Client.cpp:1592, Client.cpp:1705, Client.cpp:2228. Destination: 08.5-13 modal-loop round.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-31T23:20:41.464Z",
    "resolved_at": null
  },
  {
    "id": 9,
    "kind": "deviation",
    "phase": "08.5",
    "file": "src/Buttons.cpp",
    "line": 530,
    "description": "The root menu discards foreign Expose events instead of dispatching them. Interleaving: the menu grab is active in the modal loop; another managed client is uncovered and the server sends it an Expose; the loop's mask matches the event, sees a window that is not the menu, and drops it on the floor rather than routing it to eventExposure(); the client's frame and tab label stay blank until some later unrelated Expose repaints them. Destination: 08.5-13 modal-loop round.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-31T23:20:46.869Z",
    "resolved_at": null
  },
  {
    "id": 10,
    "kind": "deviation",
    "phase": "08.5",
    "file": "src/Events.cpp",
    "line": 551,
    "description": "Enter coalescing plus an ignored Leave can livelock focus. Interleaving: the pointer crosses two frames quickly, so two EnterNotify events queue; the handler at Events.cpp:551 coalesces them by discarding all but the last and never processes the intervening LeaveNotify at Events.cpp:553; m_focusChanging stays true with m_focusPointerMoved already set, so the pointer-stopped deadline armed via Manager.cpp:1537 is re-armed on every motion and never expires; auto-raise for the window actually under the pointer never fires. Destination: 08.5-13 focus round.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-31T23:20:53.072Z",
    "resolved_at": null
  },
  {
    "id": 11,
    "kind": "deviation",
    "phase": "08.5",
    "file": "src/Buttons.cpp",
    "line": 16,
    "description": "StructureNotifyMask is silently truncated out of the pointer-grab event mask by a 16-bit intermediate. Interleaving: a move or resize grab is taken with the truncated mask, so while the grab is held the WM is not selecting structure events on the grab window; the client resizes or unmaps itself mid-drag; the ConfigureNotify or UnmapNotify is never delivered to the grab, so the frame geometry the drag commits on release is computed from stale dimensions and the frame ends up the wrong size. Same truncation at Border.cpp:1458. Destination: 08.5-13 mask-width round.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-31T23:20:59.396Z",
    "resolved_at": null
  },
  {
    "id": 12,
    "kind": "deviation",
    "phase": "08.5",
    "file": "include/Manager.h",
    "line": 215,
    "description": "m_currentTime is declared int rather than Time (unsigned long), so server timestamps are truncated and may go negative. Interleaving: the session runs past the point where the server's 32-bit millisecond clock exceeds INT_MAX, or the server starts near wraparound; a ButtonPress carrying that timestamp is assigned to m_currentTime and truncates to a negative int; the value is then passed back as the time argument of XSetInputFocus or XGrabPointer, where the server reads it as a time far in the past and rejects the request as out of date, so focus or the grab silently does not take. Destination: 08.5-12, alongside the timestamp() path in src/Manager.cpp.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-31T23:21:06.341Z",
    "resolved_at": null
  }
]
````
