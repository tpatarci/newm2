---
schema_version: 1
open_count: 4
waived_count: 8
fixed_count: 6
total_count: 18
last_updated: 2026-09-06T10:53:30.413Z
---

# Broken Windows Ledger

> Cross-phase defect register. With `workflow.windows_enforce` enabled, `/gsd-ship` blocks while `open_count > 0`.
> Waive with `gsd-tools windows waive <id> "<reason>"` (reason required).
> Mark fixed with `gsd-tools windows fixed <id>`.

| id | phase | kind | file | line | description | status | reason | recorded_at | resolved_at |
|----|-------|------|------|------|-------------|--------|--------|-------------|-------------|
| 1 | 08 | unrun-verify | tests/test_wm_geometry.cpp |  | XDIS-02 reflow proven with WM2_FORCE_NO_RANDR on a RANDR-capable server, not on the extension-less server -- no resize is possible there | waived | Not testable as stated: without the RANDR extension the server cannot change resolution, so no reflow can be provoked on it. XDIS-02's reflow path is proven with WM2_FORCE_NO_RANDR on a RANDR-capable server, which exercises the same no-RANDR code path; the residual gap is the extension-less server's own inability to resize, recorded as a stated limit of the phase-8 record. | 2026-08-11T15:03:59.202Z | 2026-09-05T11:24:40.436Z |
| 2 | 08 | deviation | tests/lsan.supp |  | 32-byte libXrandr leak suppressed (library defect on its no-extension path, standalone reproducer recorded in the file) | waived | A libXrandr defect on its no-extension path, with a standalone reproducer recorded in tests/lsan.supp beside the suppression. The correct disposition is the suppression, not a change to wm2; kept so the reproducer stays with the reason. | 2026-08-11T15:03:59.333Z | 2026-09-05T11:24:40.615Z |
| 3 | 08 | deviation | src/Events.cpp |  | T-8-UAF: revert-chain scrub added to eventDestroy; any future Client removal path must call skipInRevert() before freeing | waived | A recorded invariant, not an open defect: the T-8-UAF revert-chain scrub is in place in eventDestroy and covered by its case; the row exists to tell a future Client-removal path that it must call skipInRevert() before freeing. | 2026-08-29T08:59:33.424Z | 2026-09-05T11:24:40.796Z |
| 4 | 08 | deviation | src/Events.cpp |  | 08-12 modified src/Events.cpp, src/Border.cpp and include/Client.h beyond the plan's files_modified list; each is a one-place fix for a defect a case in the plan found | waived | A process deviation recorded by 08-12's summary: three files edited beyond the plan's files_modified, each a one-place fix for a defect one of the plan's own cases found. Nothing remains to change in code. | 2026-08-29T12:09:46.815Z | 2026-09-05T11:24:40.975Z |
| 5 | 08 | deviation | tests/support/WmFixture.h |  | deferred item 14: stale ASan reports are attributed to the next fixture handed the same display number, so a mutation run poisons later runs; one-line fix left to 08-13 | fixed |  | 2026-08-29T12:09:46.982Z | 2026-09-05T11:12:13.162Z |
| 6 | 08.5 | deviation | .planning/phases/08.5-v1.0-closeout/08.5-10-PLAN.md |  | 08.5-10 Task 1/Task 2 knob-enumeration verify command is a bash syntax error: X="$(... \|\| true); echo ..." is missing the closing double quote, so the gate cannot run as written. Executed the corrected form; no check weakened. | waived | A shell-syntax error in a frozen plan document's verify command; the corrected form was executed at run time and recorded in 08.5-10-SUMMARY.md with no check weakened. The plan file is ADD-ONLY and is not edited. | 2026-08-31T16:10:14.925Z | 2026-09-05T11:24:41.160Z |
| 7 | 08.5 | deviation | src/Manager.cpp | 605 | 08.5-10 plan prose calls PropertyChangeMask the last term of the mask assignment spanning src/Manager.cpp:603-605; it is the FIRST term on line 605 and StructureNotifyMask is the last. Citation corrected in the code comment and the record. | waived | A prose citation error in a frozen plan document; the code comment at src/Manager.cpp:605 and the record were corrected at run time. The plan file is ADD-ONLY and is not edited. | 2026-08-31T16:10:15.105Z | 2026-09-05T11:24:41.352Z |
| 8 | 08.5 | deviation | src/Buttons.cpp | 156 | Modal XMaskEvent loops reorder lifecycle events and cannot be interrupted by the self-pipe. Interleaving: the operator holds Button1 on a tab, so menu()/move()/resize() sit in XMaskEvent selecting only pointer and expose bits; a MapRequest or DestroyNotify for another client arrives and is left queued behind the grab; SIGTERM then writes the self-pipe, which no XMaskEvent is watching, so shutdown waits for the pointer to be released. Same shape at Buttons.cpp:530, Border.cpp:1495, Client.cpp:1592, Client.cpp:1705, Client.cpp:2228. Destination: 08.5-13 modal-loop round. | fixed |  | 2026-08-31T23:20:41.464Z | 2026-09-05T11:24:40.081Z |
| 9 | 08.5 | deviation | src/Buttons.cpp | 530 | The root menu discards foreign Expose events instead of dispatching them. Interleaving: the menu grab is active in the modal loop; another managed client is uncovered and the server sends it an Expose; the loop's mask matches the event, sees a window that is not the menu, and drops it on the floor rather than routing it to eventExposure(); the client's frame and tab label stay blank until some later unrelated Expose repaints them. Destination: 08.5-13 modal-loop round. | fixed |  | 2026-08-31T23:20:46.869Z | 2026-09-01T07:36:32.700Z |
| 10 | 08.5 | deviation | src/Events.cpp | 551 | Enter coalescing plus an ignored Leave can livelock focus. Interleaving: the pointer crosses two frames quickly, so two EnterNotify events queue; the handler at Events.cpp:551 coalesces them by discarding all but the last and never processes the intervening LeaveNotify at Events.cpp:553; m_focusChanging stays true with m_focusPointerMoved already set, so the pointer-stopped deadline armed via Manager.cpp:1537 is re-armed on every motion and never expires; auto-raise for the window actually under the pointer never fires. Destination: 08.5-13 focus round. | waived | Audit mechanism re-read line by line against src/Events.cpp (MotionNotify at :96-108, eventEnter at :601-609) and src/Manager.cpp (considerFocusChange :1479-1498, checkDelaysForFocus :1538-1570): Enter coalescing keeps the LAST EnterNotify, i.e. the window now under the pointer, and considerFocusChange() first calls stopConsideringFocus() then resets m_focusPointerMoved, m_focusPointerNowStill and both deadlines for the new candidate; the pointer-stopped deadline is armed once on the first motion and re-armed only at expiry, never per motion. The described livelock cannot arise from that code. LeaveNotify is ignored by design inherited from upstream wm2. Not a defect on the stated mechanism; a separate question (raising a candidate the pointer has since left for the root) is not what this row records. | 2026-08-31T23:20:53.072Z | 2026-09-05T11:15:53.497Z |
| 11 | 08.5 | deviation | src/Buttons.cpp | 16 | StructureNotifyMask is silently truncated out of the pointer-grab event mask by a 16-bit intermediate. Interleaving: a move or resize grab is taken with the truncated mask, so while the grab is held the WM is not selecting structure events on the grab window; the client resizes or unmaps itself mid-drag; the ConfigureNotify or UnmapNotify is never delivered to the grab, so the frame geometry the drag commits on release is computed from stale dimensions and the frame ends up the wrong size. Same truncation at Border.cpp:1458. Destination: 08.5-13 mask-width round. | fixed |  | 2026-08-31T23:20:59.396Z | 2026-09-05T11:12:13.358Z |
| 12 | 08.5 | deviation | include/Manager.h | 215 | m_currentTime is declared int rather than Time (unsigned long), so server timestamps are truncated and may go negative. Interleaving: the session runs past the point where the server's 32-bit millisecond clock exceeds INT_MAX, or the server starts near wraparound; a ButtonPress carrying that timestamp is assigned to m_currentTime and truncates to a negative int; the value is then passed back as the time argument of XSetInputFocus or XGrabPointer, where the server reads it as a time far in the past and rejects the request as out of date, so focus or the grab silently does not take. Destination: 08.5-12, alongside the timestamp() path in src/Manager.cpp. | fixed |  | 2026-08-31T23:21:06.341Z | 2026-09-05T11:12:13.551Z |
| 13 | 08.5 | deviation | tests/test_wm_runtime.cpp | 1508 | The exec-using-shell case failed in the FULL debug suite at 08.5-13's round base (e835737): REQUIRE(openRootMenu(...)) returned false after 25.70 s, so the whole 20 s stage budget was SPENT -- the opposite signature to the menu-colour flake 08.5-13 attributed, where the budget was never spent at all. Passed alone on two consecutive re-runs. This call site uses raw openRootMenu() rather than openRootMenuVerified(), so it has no retry around deferred item 17 (the WM's own menu window comes back BadWindow for XMoveResizeWindow/XMapRaised/XUnmapWindow and no menu ever appears). Not addressed by 08.5-13, which fixed the fallback rather than the never-opens case. Destination: whichever round takes deferred item 17. | fixed |  | 2026-09-01T07:36:41.533Z | 2026-09-05T11:24:40.251Z |
| 14 | 08.5 | deviation | .planning/phases/08.5-v1.0-closeout/08.5-13-PLAN.md |  | 08.5-13 Task 3's FOREIGN-DISPATCHED gate cannot distinguish naming the Foreign verdict from acting on it. Verified both ways at run time: it printed FOREIGN-DISPATCHED against the Task 1 tree as well, where the foreign Expose was still being DISCARDED, because its sed range stops at the first break; and both the discarding and the dispatching arms mention Foreign before that point. The plan already labels it a wiring check and rests the behavioural claim on the acceptance criterion and on foreign-expose-is-named, so no check was weakened -- recorded so a later reader does not mistake it for a behavioural gate. A behavioural gate would need to assert eventExposure is reached from the Foreign arm. | waived | A wiring gate in a frozen plan document that cannot distinguish naming the Foreign verdict from acting on it, recorded so a later reader does not mistake it for a behavioural gate; the behavioural claim rests on the acceptance criterion and on foreign-expose-is-named. The plan file is ADD-ONLY and is not edited. | 2026-09-01T07:36:48.440Z | 2026-09-05T11:24:41.540Z |
| 15 | 09 | deviation | docs/RELEASE-NOTES.md |  | 09-01 Task 3 acceptance asked for one contiguous git-diff hunk, which its own two-paragraph action clause cannot produce; verified the stated intent (no line outside the Appearance section changed) instead | open |  | 2026-09-06T00:38:35.082Z |  |
| 16 | 09 | deviation | tests/test_config_writer.cpp | 604 | The T-9-07/T-9-08 atomicity case SKIPs when euid==0: directory permissions are not enforced for root, so on a root-only host that evidence is not collected | open |  | 2026-09-06T07:41:36.481Z |  |
| 17 | 09 | deviation | tests/test_wm_config_live.cpp | 793 | The reload-unreadable-file case SKIPs when euid==0: root can read a mode-000 file, so on a root-only host the 'reload names the file and changes nothing' evidence is not collected | open |  | 2026-09-06T09:16:12.670Z |  |
| 18 | 09 | unrun-verify | tests/test_wm_config_live.cpp |  | The menu-open deferral (T-9-32) has no mutation-proof case: removing 'if (m_menuOpen)' leaves 'a menu held open across a menu-entry change is not disturbed' green in both the debug and the ASan tree, because the category vector's buffer is reused rather than freed | open |  | 2026-09-06T10:53:30.413Z |  |

````json
[
  {
    "id": 1,
    "kind": "unrun-verify",
    "phase": "08",
    "file": "tests/test_wm_geometry.cpp",
    "line": null,
    "description": "XDIS-02 reflow proven with WM2_FORCE_NO_RANDR on a RANDR-capable server, not on the extension-less server -- no resize is possible there",
    "status": "waived",
    "reason": "Not testable as stated: without the RANDR extension the server cannot change resolution, so no reflow can be provoked on it. XDIS-02's reflow path is proven with WM2_FORCE_NO_RANDR on a RANDR-capable server, which exercises the same no-RANDR code path; the residual gap is the extension-less server's own inability to resize, recorded as a stated limit of the phase-8 record.",
    "recorded_at": "2026-08-11T15:03:59.202Z",
    "resolved_at": "2026-09-05T11:24:40.436Z"
  },
  {
    "id": 2,
    "kind": "deviation",
    "phase": "08",
    "file": "tests/lsan.supp",
    "line": null,
    "description": "32-byte libXrandr leak suppressed (library defect on its no-extension path, standalone reproducer recorded in the file)",
    "status": "waived",
    "reason": "A libXrandr defect on its no-extension path, with a standalone reproducer recorded in tests/lsan.supp beside the suppression. The correct disposition is the suppression, not a change to wm2; kept so the reproducer stays with the reason.",
    "recorded_at": "2026-08-11T15:03:59.333Z",
    "resolved_at": "2026-09-05T11:24:40.615Z"
  },
  {
    "id": 3,
    "kind": "deviation",
    "phase": "08",
    "file": "src/Events.cpp",
    "line": null,
    "description": "T-8-UAF: revert-chain scrub added to eventDestroy; any future Client removal path must call skipInRevert() before freeing",
    "status": "waived",
    "reason": "A recorded invariant, not an open defect: the T-8-UAF revert-chain scrub is in place in eventDestroy and covered by its case; the row exists to tell a future Client-removal path that it must call skipInRevert() before freeing.",
    "recorded_at": "2026-08-29T08:59:33.424Z",
    "resolved_at": "2026-09-05T11:24:40.796Z"
  },
  {
    "id": 4,
    "kind": "deviation",
    "phase": "08",
    "file": "src/Events.cpp",
    "line": null,
    "description": "08-12 modified src/Events.cpp, src/Border.cpp and include/Client.h beyond the plan's files_modified list; each is a one-place fix for a defect a case in the plan found",
    "status": "waived",
    "reason": "A process deviation recorded by 08-12's summary: three files edited beyond the plan's files_modified, each a one-place fix for a defect one of the plan's own cases found. Nothing remains to change in code.",
    "recorded_at": "2026-08-29T12:09:46.815Z",
    "resolved_at": "2026-09-05T11:24:40.975Z"
  },
  {
    "id": 5,
    "kind": "deviation",
    "phase": "08",
    "file": "tests/support/WmFixture.h",
    "line": null,
    "description": "deferred item 14: stale ASan reports are attributed to the next fixture handed the same display number, so a mutation run poisons later runs; one-line fix left to 08-13",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-08-29T12:09:46.982Z",
    "resolved_at": "2026-09-05T11:12:13.162Z"
  },
  {
    "id": 6,
    "kind": "deviation",
    "phase": "08.5",
    "file": ".planning/phases/08.5-v1.0-closeout/08.5-10-PLAN.md",
    "line": null,
    "description": "08.5-10 Task 1/Task 2 knob-enumeration verify command is a bash syntax error: X=\"$(... || true); echo ...\" is missing the closing double quote, so the gate cannot run as written. Executed the corrected form; no check weakened.",
    "status": "waived",
    "reason": "A shell-syntax error in a frozen plan document's verify command; the corrected form was executed at run time and recorded in 08.5-10-SUMMARY.md with no check weakened. The plan file is ADD-ONLY and is not edited.",
    "recorded_at": "2026-08-31T16:10:14.925Z",
    "resolved_at": "2026-09-05T11:24:41.160Z"
  },
  {
    "id": 7,
    "kind": "deviation",
    "phase": "08.5",
    "file": "src/Manager.cpp",
    "line": 605,
    "description": "08.5-10 plan prose calls PropertyChangeMask the last term of the mask assignment spanning src/Manager.cpp:603-605; it is the FIRST term on line 605 and StructureNotifyMask is the last. Citation corrected in the code comment and the record.",
    "status": "waived",
    "reason": "A prose citation error in a frozen plan document; the code comment at src/Manager.cpp:605 and the record were corrected at run time. The plan file is ADD-ONLY and is not edited.",
    "recorded_at": "2026-08-31T16:10:15.105Z",
    "resolved_at": "2026-09-05T11:24:41.352Z"
  },
  {
    "id": 8,
    "kind": "deviation",
    "phase": "08.5",
    "file": "src/Buttons.cpp",
    "line": 156,
    "description": "Modal XMaskEvent loops reorder lifecycle events and cannot be interrupted by the self-pipe. Interleaving: the operator holds Button1 on a tab, so menu()/move()/resize() sit in XMaskEvent selecting only pointer and expose bits; a MapRequest or DestroyNotify for another client arrives and is left queued behind the grab; SIGTERM then writes the self-pipe, which no XMaskEvent is watching, so shutdown waits for the pointer to be released. Same shape at Buttons.cpp:530, Border.cpp:1495, Client.cpp:1592, Client.cpp:1705, Client.cpp:2228. Destination: 08.5-13 modal-loop round.",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-08-31T23:20:41.464Z",
    "resolved_at": "2026-09-05T11:24:40.081Z"
  },
  {
    "id": 9,
    "kind": "deviation",
    "phase": "08.5",
    "file": "src/Buttons.cpp",
    "line": 530,
    "description": "The root menu discards foreign Expose events instead of dispatching them. Interleaving: the menu grab is active in the modal loop; another managed client is uncovered and the server sends it an Expose; the loop's mask matches the event, sees a window that is not the menu, and drops it on the floor rather than routing it to eventExposure(); the client's frame and tab label stay blank until some later unrelated Expose repaints them. Destination: 08.5-13 modal-loop round.",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-08-31T23:20:46.869Z",
    "resolved_at": "2026-09-01T07:36:32.700Z"
  },
  {
    "id": 10,
    "kind": "deviation",
    "phase": "08.5",
    "file": "src/Events.cpp",
    "line": 551,
    "description": "Enter coalescing plus an ignored Leave can livelock focus. Interleaving: the pointer crosses two frames quickly, so two EnterNotify events queue; the handler at Events.cpp:551 coalesces them by discarding all but the last and never processes the intervening LeaveNotify at Events.cpp:553; m_focusChanging stays true with m_focusPointerMoved already set, so the pointer-stopped deadline armed via Manager.cpp:1537 is re-armed on every motion and never expires; auto-raise for the window actually under the pointer never fires. Destination: 08.5-13 focus round.",
    "status": "waived",
    "reason": "Audit mechanism re-read line by line against src/Events.cpp (MotionNotify at :96-108, eventEnter at :601-609) and src/Manager.cpp (considerFocusChange :1479-1498, checkDelaysForFocus :1538-1570): Enter coalescing keeps the LAST EnterNotify, i.e. the window now under the pointer, and considerFocusChange() first calls stopConsideringFocus() then resets m_focusPointerMoved, m_focusPointerNowStill and both deadlines for the new candidate; the pointer-stopped deadline is armed once on the first motion and re-armed only at expiry, never per motion. The described livelock cannot arise from that code. LeaveNotify is ignored by design inherited from upstream wm2. Not a defect on the stated mechanism; a separate question (raising a candidate the pointer has since left for the root) is not what this row records.",
    "recorded_at": "2026-08-31T23:20:53.072Z",
    "resolved_at": "2026-09-05T11:15:53.497Z"
  },
  {
    "id": 11,
    "kind": "deviation",
    "phase": "08.5",
    "file": "src/Buttons.cpp",
    "line": 16,
    "description": "StructureNotifyMask is silently truncated out of the pointer-grab event mask by a 16-bit intermediate. Interleaving: a move or resize grab is taken with the truncated mask, so while the grab is held the WM is not selecting structure events on the grab window; the client resizes or unmaps itself mid-drag; the ConfigureNotify or UnmapNotify is never delivered to the grab, so the frame geometry the drag commits on release is computed from stale dimensions and the frame ends up the wrong size. Same truncation at Border.cpp:1458. Destination: 08.5-13 mask-width round.",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-08-31T23:20:59.396Z",
    "resolved_at": "2026-09-05T11:12:13.358Z"
  },
  {
    "id": 12,
    "kind": "deviation",
    "phase": "08.5",
    "file": "include/Manager.h",
    "line": 215,
    "description": "m_currentTime is declared int rather than Time (unsigned long), so server timestamps are truncated and may go negative. Interleaving: the session runs past the point where the server's 32-bit millisecond clock exceeds INT_MAX, or the server starts near wraparound; a ButtonPress carrying that timestamp is assigned to m_currentTime and truncates to a negative int; the value is then passed back as the time argument of XSetInputFocus or XGrabPointer, where the server reads it as a time far in the past and rejects the request as out of date, so focus or the grab silently does not take. Destination: 08.5-12, alongside the timestamp() path in src/Manager.cpp.",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-08-31T23:21:06.341Z",
    "resolved_at": "2026-09-05T11:12:13.551Z"
  },
  {
    "id": 13,
    "kind": "deviation",
    "phase": "08.5",
    "file": "tests/test_wm_runtime.cpp",
    "line": 1508,
    "description": "The exec-using-shell case failed in the FULL debug suite at 08.5-13's round base (e835737): REQUIRE(openRootMenu(...)) returned false after 25.70 s, so the whole 20 s stage budget was SPENT -- the opposite signature to the menu-colour flake 08.5-13 attributed, where the budget was never spent at all. Passed alone on two consecutive re-runs. This call site uses raw openRootMenu() rather than openRootMenuVerified(), so it has no retry around deferred item 17 (the WM's own menu window comes back BadWindow for XMoveResizeWindow/XMapRaised/XUnmapWindow and no menu ever appears). Not addressed by 08.5-13, which fixed the fallback rather than the never-opens case. Destination: whichever round takes deferred item 17.",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-09-01T07:36:41.533Z",
    "resolved_at": "2026-09-05T11:24:40.251Z"
  },
  {
    "id": 14,
    "kind": "deviation",
    "phase": "08.5",
    "file": ".planning/phases/08.5-v1.0-closeout/08.5-13-PLAN.md",
    "line": null,
    "description": "08.5-13 Task 3's FOREIGN-DISPATCHED gate cannot distinguish naming the Foreign verdict from acting on it. Verified both ways at run time: it printed FOREIGN-DISPATCHED against the Task 1 tree as well, where the foreign Expose was still being DISCARDED, because its sed range stops at the first break; and both the discarding and the dispatching arms mention Foreign before that point. The plan already labels it a wiring check and rests the behavioural claim on the acceptance criterion and on foreign-expose-is-named, so no check was weakened -- recorded so a later reader does not mistake it for a behavioural gate. A behavioural gate would need to assert eventExposure is reached from the Foreign arm.",
    "status": "waived",
    "reason": "A wiring gate in a frozen plan document that cannot distinguish naming the Foreign verdict from acting on it, recorded so a later reader does not mistake it for a behavioural gate; the behavioural claim rests on the acceptance criterion and on foreign-expose-is-named. The plan file is ADD-ONLY and is not edited.",
    "recorded_at": "2026-09-01T07:36:48.440Z",
    "resolved_at": "2026-09-05T11:24:41.540Z"
  },
  {
    "id": 15,
    "kind": "deviation",
    "phase": "09",
    "file": "docs/RELEASE-NOTES.md",
    "line": null,
    "description": "09-01 Task 3 acceptance asked for one contiguous git-diff hunk, which its own two-paragraph action clause cannot produce; verified the stated intent (no line outside the Appearance section changed) instead",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T00:38:35.082Z",
    "resolved_at": null
  },
  {
    "id": 16,
    "kind": "deviation",
    "phase": "09",
    "file": "tests/test_config_writer.cpp",
    "line": 604,
    "description": "The T-9-07/T-9-08 atomicity case SKIPs when euid==0: directory permissions are not enforced for root, so on a root-only host that evidence is not collected",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T07:41:36.481Z",
    "resolved_at": null
  },
  {
    "id": 17,
    "kind": "deviation",
    "phase": "09",
    "file": "tests/test_wm_config_live.cpp",
    "line": 793,
    "description": "The reload-unreadable-file case SKIPs when euid==0: root can read a mode-000 file, so on a root-only host the 'reload names the file and changes nothing' evidence is not collected",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T09:16:12.670Z",
    "resolved_at": null
  },
  {
    "id": 18,
    "kind": "unrun-verify",
    "phase": "09",
    "file": "tests/test_wm_config_live.cpp",
    "line": null,
    "description": "The menu-open deferral (T-9-32) has no mutation-proof case: removing 'if (m_menuOpen)' leaves 'a menu held open across a menu-entry change is not disturbed' green in both the debug and the ASan tree, because the category vector's buffer is reused rather than freed",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T10:53:30.413Z",
    "resolved_at": null
  }
]
````
