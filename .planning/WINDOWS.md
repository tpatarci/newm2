---
schema_version: 1
open_count: 31
waived_count: 8
fixed_count: 9
total_count: 48
last_updated: 2026-09-07T04:21:06.736Z
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
| 19 | 09 | unrun-verify | scripts/gates/build-all.sh |  | The release tree and its link audit were not run for plan 09-06; only debug and asan were gated | fixed | Release gate run by the orchestrator after wave 6 closed: build-all.sh release green, 498/498, link audit OK (24 entries, all in the intended runtime set) | 2026-09-06T12:03:32.878Z | 2026-09-06T12:15:16.921Z |
| 20 | 09 | deviation | tests/test_wm2_config_smoke.cpp |  | Eight of the nine Behaviour settings are proven here only as far as the window manager ADOPTING the value through the GUI's own FormState and ProtocolClient; that adopting it changes an observable behaviour is owned per setting by the [wm_config_live] cases from 09-05. Only click-to-focus has both halves in one case (the tracer). The join -- the value that arrived through the GUI's client is the value whose behaviour changed -- is an inference for the other eight | open |  | 2026-09-06T13:15:04.446Z |  |
| 21 | 09 | deviation | tests/test_wm2_config_smoke.cpp |  | 'a reload notice arriving with an entry dialog open leaves the dialog's contents alone' is structural: it holds a MenuEntryDraft across adoptEffectiveMenuEntries() and adds a comment-stripped source guard that MenuPage::refreshFromForm() names no dialog. The live arm -- a real reload while gtk_dialog_run() is spinning -- is not driven, because reaching it means synthesising input into a modal GTK dialog | open |  | 2026-09-06T13:15:04.636Z |  |
| 22 | 09 | deviation | apps/wm2-config/MenuPage.cpp |  | The entry dialog states the name-override rule unconditionally rather than only when the typed name matches a discovered application. Detecting a match needs the discovered application NAMES and no protocol key exposes them; publishing the user's installed-application list over the socket was judged a wider surface than the note is worth | open |  | 2026-09-06T13:15:04.837Z |  |
| 23 | 09 | unrun-verify | scripts/gates/build-all.sh |  | The release tree and its link audit were not run for plan 09-07; only debug (526/526) and asan were gated. No link line of a shipped target changed in this plan -- only the test target gained libXtst -- but that is an argument rather than a run | open |  | 2026-09-06T13:15:05.034Z |  |
| 24 | 09 | unrun-verify | scripts/gates/build-all.sh |  | Release and ASan trees were last run in full at 530 tests (commit 441d62d); the 531st test, a display-free desktop-entry case added afterwards, was re-run in the ASan tree by label only and the release suite was not re-run at the final commit | fixed | Release gate re-run by the orchestrator after wave 8 closed at the final commit: build-all.sh release green, 531/531, link audit OK (24 entries, all in the intended runtime set) | 2026-09-06T15:09:02.517Z | 2026-09-06T15:22:50.706Z |
| 25 | 09 | unmet-truth | src/Buttons.cpp |  | Task 1's runtime cases prove the Configure row is present/absent by ROW COUNT and by launch identity, not by reading the label off the screen: the menu paints text with Xft and the server keeps no strings. The label claim is carried by display-free [menupaint] cases over the shared RootMenuLayout instead. | open |  | 2026-09-06T16:20:59.745Z |  |
| 26 | 09 | unmet-truth | scripts/gates/doc-keys.sh |  | Direction 2 of the doc-keys gate does not catch a bare unhyphenated word presented in plain backticks that is not an accepted key; in prose that form is indistinguishable from an ordinary word. Hyphenated inventions and 'word = value' config lines are caught. Stated in the script. | open |  | 2026-09-06T16:20:59.930Z |  |
| 27 | 09 | unmet-truth | .planning/phases/09-config-gui-ipc/evidence/remote-desktop/README.md |  | The first wm2-config on a freshly started X server holds ~101 MB and every later one ~50 MB, reproducibly on Xvfb and TigerVNC. Two candidate causes ruled out (per-user fontconfig cache; being the first client). The cause was not determined. | open |  | 2026-09-06T16:21:00.108Z |  |
| 28 | 09 | deviation | .planning/phases/08.5-v1.0-closeout/evidence/tightvnc/capabilities.txt |  | Pre-existing: capabilities.txt files committed under 08- and 08.5- evidence contain this machine's hostname from 'uname -n'. The public repo prohibits host identifiers. The capture script was fixed in 09-09; the already-committed captures were NOT rewritten (out of this plan's scope). | fixed | Orchestrator scrub after wave 9: the hostname replaced by the placeholder <workstation> in all 16 tracked files (15 evidence files under phases 08 and 08.5, one comment and one diagnostic string in tests/test_wm_resource.cpp). The pushed history still carries the name; whether to rewrite it is the operator's decision. | 2026-09-06T16:21:00.292Z | 2026-09-06T16:31:16.616Z |
| 29 | 09 | unrun-verify |  |  | WR-04 review fix: the write-failure errno is now captured at each failure site, but no case drives it. The three sites need write(), fsync() or close() to fail on a real descriptor, which needs an LD_PRELOAD shim or a deliberately full filesystem -- neither has a precedent in this project, and both are heavier apparatus than the defect. | open |  | 2026-09-06T17:35:48.226Z |  |
| 30 | 09 | unrun-verify |  |  | WR-14 review fix: the button-hold tick is now measured rather than assumed, but no case drives the over-count. Reproducing it needs a connection within 50 ms of its ten-second silence deadline at the moment the press begins -- a 50 ms window reached after a ten-second wait. The existing [wm_config_runtime] destroy-window-delay case guards both directions of the arithmetic. | open |  | 2026-09-06T17:35:48.425Z |  |
| 31 | 09 | unrun-verify |  |  | WR-16 review fix: showGeometry's buffer is now 32 bytes and snprintf, but the overflow is unreachable through any caller -- every one passes screen-clamped coordinates -- so a case would have to call the method with INT_MIN directly and would prove the arithmetic rather than the window manager. | open |  | 2026-09-06T17:35:48.610Z |  |
| 32 | 09 | unrun-verify |  |  | WR-17 review fix: install-components.sh now refuses an absolute build-dir (verified by running it: exit 2 with the reason). Not automated -- the project has no harness for gate scripts and one built for a two-line argument guard would be more apparatus than the defect. | open |  | 2026-09-06T17:35:48.788Z |  |
| 33 | 09 | unrun-verify |  |  | WR-11 review fix: the GLib source is now detached from ProtocolClient's state handler, but the GLib wiring itself has no case -- it needs a GTK main loop, and the project has no harness that can drive a GLib source and inspect its attachment. The hook the fix hangs off IS covered display-free ([wm2_config_smoke][protocol][nonblocking], 'a disconnect from a request path still announces itself'). | open |  | 2026-09-06T17:35:48.974Z |  |
| 34 | 09 | unrun-verify |  |  | WR-12 review fix: wm2-ctl now skips an unsolicited reloaded, and the classification is unit-tested ([config_protocol][notice]). The end-to-end interleaving is not reachable deterministically from the fixture: it needs a reload to land between wm2-ctl's hello-ack and its own reply, and nothing outside the window manager can schedule that. | open |  | 2026-09-06T17:35:49.158Z |  |
| 35 | 09 | deviation |  |  | Pre-existing, found by the review-fix pass's asan gate: the [wm2_config_smoke] wm2-config RSS budget shipped with the debug constant alone (160 MB, measured 101 MB), so the asan tree failed it on the instrumentation -- 172 MB, stable over three runs. Confirmed pre-existing by rebuilding the settings window from the pre-review sources (c4abfc6) in the same asan tree: 171.9-172.5 MB, the same figure. Given a per-tree constant in the shape tests/test_wm_resource.cpp already established (272 MB asan, ~1.6x the measurement, the same headroom the debug figure carries). | open |  | 2026-09-06T17:59:53.329Z |  |
| 36 | 09 | unrun-verify |  |  | W-05 review-fix residual: the window manager now excludes the reload requester from D-08's broadcast, so each client gets exactly one line per reload of its own (asserted by [config_socket][reentrancy] 'the client that asked for a reload is not also broadcast to'). The RESIDUAL is untested and untestable from this fixture: a foreign notice can still land on a connection between that client SENDING its reload and its reply arriving, and no type-only classifier can tell it from the reply -- the version-1 contract is frozen and carries no correlator. Reaching it needs a second client's reload to be serviced in the window between our send and our service, which nothing outside the window manager can schedule. | open |  | 2026-09-06T19:03:57.766Z |  |
| 37 | 09 | unrun-verify |  |  | W-06 review-fix residual: the CR-03 ownership arm (fstat's st_uid != geteuid) is now driven by a case that REACHES it -- [config_socket][directory] 'a socket directory owned by somebody else is refused' -- but that case can only run as a process able to give a directory away, so it SKIPS on this host (non-root; also skips under 'unshare -Ur', where chown to an unmapped uid returns EINVAL). It never passes vacuously: both the euid gate and the chown failure are SKIPs, not silent successes. The reachable half (the mkdir refusal) is a separate case scoped entirely inside a scratch TempDir, so no value of euid can make either case create anything at the filesystem root. | open |  | 2026-09-06T19:06:03.889Z |  |
| 38 | 09 | deviation | src/Manager.cpp | 2049 | Review fix WR-13 narrowed plan 09-04's must-have T7 after its SUMMARY was written: a set of tab-font, menu-font or frame-thickness arriving while a modal grab is held is now REFUSED with a reason rather than applied; documented in docs/RELEASE-NOTES.md but the plan's truth 'applied without waiting for the grab to end' no longer holds for those three keys | open |  | 2026-09-06T20:05:07.666Z |  |
| 39 | 09 | unrun-verify | apps/wm2-ctl/main.cpp | 214 | Codex PR-review fix P1 (wm2-ctl non-blocking socket, commit 3ef61cc): the CONNECT half is covered by a RED-first [wm_config_live] case (a saturated accept queue; the tool now exits 2 on its own 15 s deadline instead of hanging until the harness watchdog kills it). The SEND half -- the EAGAIN arm that now waits on POLLOUT instead of spinning -- has no automated case. Reaching it needs the peer to stop reading AND more than one socket buffer of data in flight; AF_UNIX stream flow control is charged to the SENDER's SO_SNDBUF (default ~208 KB on this host, not settable from outside the tool) while Linux caps a single argv string at MAX_ARG_STRLEN = 128 KB, so no invocation a test can construct fills it. Verified by reading instead: the arm is the same shape as ProtocolClient.cpp's, which WR-09 closed and which is exercised there. | open |  | 2026-09-06T21:48:51.990Z |  |
| 40 | 09 | unrun-verify | src/Client.cpp |  | Codex PR-review pass 2, P2 fullscreen fix (commit 60c758f): the deferred-refresh flags are driven RED-first for a frame-thickness and a tab-background change applied while a client is fullscreen ([wm_config_live] "a client that was fullscreen while the configuration moved comes back wearing the new one"). Two arms share that code and have no case of their own: a TAB-FONT change during fullscreen (it sets the same m_frameLayoutStale flag and is replayed through the same Border::relayoutForTabFont call, so the case would differ only in which key is set), and a client that is HIDDEN as well as fullscreen when the change arrives (the frame windows are unmapped, so the geometry is assertable but no pixel is). Verified by reading: applyDeferredFrameRefresh() has one path and the thickness case reaches all of it. | open |  | 2026-09-06T22:51:02.152Z |  |
| 41 | 09 | unrun-verify | apps/wm2-config/FormState.cpp |  | Codex PR-review pass 2, P2 lower-layer fix (commit e981cdd): FormState::refreshLowerLayers is driven RED-first for a single setting through the reload path ([wm2_config_smoke] "a file re-read moves what Reset will produce, and keeps the edits it finds"), and the window wiring is asserted by a comment-stripped source guard on onReloadNotice() rather than by a driven GTK reload -- the same precedent row 21 records for the entry-dialog case. Two arms have no case: the MENU-ENTRIES half (m_menuBelowUser, and a pending menu reset following the new lower layer), and the SAVE path, which re-reads the layers for the same reason and now refreshes them the same way. Both are the same three lines of the same function as the covered arm. | open |  | 2026-09-06T22:51:09.093Z |  |
| 42 | 09 | unrun-verify | src/Manager.cpp |  | Codex PR-review pass 3, P2 socket-property fix (commit d58e17a): the SHUTDOWN arm is driven RED-first ([wm_socket] "The published socket path is withdrawn when the window manager exits" -- the fixture's X server outlives the window manager, so the root window is still there to be asked after a clean SIGTERM exit; red on CHECK(withdrawn) with _WM2_CONFIG_SOCKET still naming the unlinked node). The STARTUP arm -- setupEwmhProperties()'s new else, which deletes a PREDECESSOR's stale property when this start could not bind -- has no case. It needs a property planted on root BEFORE the window manager publishes, and WmFixture spawns the window manager inside its own constructor with no hook to run anything first; a second window manager cannot be started on a fixture's display either (the display dies with the fixture, and _WM2_RUNNING makes one owner per display). The nearest driven case, "A socket path too long for the address structure is named, not truncated", asserts the property is absent after a no-listener start but cannot distinguish the else branch from the old no-else code, because nothing stale was there to remove. Verified by reading: the else is one XDeleteProperty through the same helper release() uses, and that helper is exercised. | open |  | 2026-09-07T00:00:00.000Z |  |
| 43 | 09 | unrun-verify | src/Events.cpp |  | Codex PR-review pass 3, P2 failed-listener fix (commit 8df55d8): the TRANSPORT half is driven RED-first ([config_socket] "a listener that poll() reports as failed shuts the server down" -- POLLNVAL injected into the listener's revents by hand; red on isListening(), on the next poll set being non-empty, on the connection count and on the socket node still existing). The WINDOW MANAGER half -- serviceConfigSocket() reading the isListening() transition and withdrawing _WM2_CONFIG_SOCKET when a live listener dies mid-session -- has no case. Nothing outside the process can make the real binary's listening descriptor report POLLERR/POLLHUP/POLLNVAL: the descriptor is private to the window manager, and there is no lever to close or corrupt it (the WM2_FORCE_* levers are all read once at startup). Verified by reading: the transition is a two-line guard around the same unpublishConfigSocketPath() that release() calls, and that call is exercised by [wm_socket] "The published socket path is withdrawn when the window manager exits". | open |  | 2026-09-07T00:00:00.000Z |  |
| 44 | 09 | unrun-verify | src/Events.cpp |  | CodeRabbit src-review F2 (commit ea40f1d): a bounded modalWait() no longer reports Timeout at the socket's clamped poll expiry instead of at the caller's deadline. The fix has NO externally observable consequence in this build, so no behavioural case could be red at the round base, and the red-first evidence is a SOURCE-level guard ([wm_socket][source][modalwait] 'modalWait reports Timeout from its deadline check and nowhere else' -- red at two Timeout returns, green at one). Every bounded caller today already absorbs an early timeout: Border.cpp's tab-button hold MEASURES each wait rather than assuming it lasted its bound (the WR-14 consumer-side fix), and Client.cpp's move and resize drags simply loop on Timeout; the two remaining callers pass -1. The behavioural companion ('a bounded modal wait that crosses a silent connection's deadline still takes its full delay') drives the hold across the moment a pre-hello connection's 10 s deadline expires, which is the only moment the clamp bites, and is green both before and after -- it guards the fix's own risk (a bounded wait that never times out), not the defect. The arm that has no case is a caller that trusts the bound WITHOUT measuring, because no such caller exists yet. | open |  | 2026-09-07T03:29:48.782Z |  |
| 45 | 09 | unrun-verify | src/Manager.cpp |  | CodeRabbit src-review F3 (commit 4018ef4): applyConfig() applying a frame-thickness and a tab-font together now walks the frames through relayoutFrameForFont (the thickness path PLUS the label repaint) instead of through relayoutFrame, which deliberately does not repaint the label. The red-first evidence is a SOURCE-level guard ([wm_socket][source] 'applying a thickness and a tab font together walks the frames once, through the path that repaints the label' -- red on the font walk being conditioned on the thickness having stayed put, and on the thickness walk not standing down). It could not be driven behaviourally, and the reason was MEASURED rather than assumed: the reshape the thickness path performs moves the tab's stair-stepped clip region, the server sends Expose for every region that comes back inside it (13 of them, counted from the test by selecting ExposureMask on the window manager's own tab), and Border::expose() -> drawLabel() repaints the label in the NEW face within one turn of the event loop. So no capture taken after a settle can see the stale glyphs on this X server. The behavioural case ('a thickness and a tab font applied together leave a tab that was already open looking like one opened afterwards') is green both before and after and asserts the combined application produces a correct tab at all. The arm with no case is the one the fix is FOR: a server with backing store -- a VNC session, which is this project's stated deployment -- restores a shrinking window's contents rather than asking for them back and sends no Expose, so the stale label would survive. Reproducing it needs an X server started with backing store enabled, which the shared Xvfb fixture does not provide. | open |  | 2026-09-07T03:29:59.365Z |  |
| 46 | 09 | unrun-verify | apps/wm2-config/BehaviourPage.cpp |  | CodeRabbit apps-chunk A1 (commit e12c0d2): BehaviourPage::addDelayRow's fallback range for a key configKeySpecFor() does not name is now the range the option table gives the other delay keys on the page (and an int-wide range if the table knows neither), rather than the degenerate lo == hi == 1 that was there. The red-first evidence is a SOURCE-level guard, added to the existing '[wm2_config_smoke] the delay controls are built from the parser's own bounds, not from a third copy' case -- red on 'spec->maxValue : 1' still being present and on the table-read fallback being absent, green after. It could not be driven behaviourally: the artifact is a gtk_spin_button range, tests/test_wm2_config_smoke.cpp links Catch2 and X11 with no GTK by design (that is what makes its display-free half honest), and the arm is unreachable in a correct build anyway -- every key this page passes to addDelayRow is one the option table declares, which the 'the Behaviour page carries what D-09 assigns to it and nothing else' case already asserts. The arm with no case is a build in which the option table has lost a delay key. | open |  | 2026-09-07T04:20:55.359Z |  |
| 47 | 09 | unrun-verify | apps/wm2-config/AppearancePage.cpp |  | CodeRabbit apps-chunk A3 (commit d51143f): AppearancePage::renderRow now APPENDS DISC-08's origin line to the thickness slider's own tooltip (Row::baseTooltip) instead of replacing it with gtk_widget_set_tooltip_text. The red-first evidence is a SOURCE-level guard ('[wm2_config_smoke] the thickness slider keeps its own tooltip when the origin line is added' -- red on baseTooltip being absent from both addThicknessRow and renderRow and on the bare replacement still being present, green after). It could not be driven behaviourally: reading a GTK tooltip needs a GTK build and a display, and tests/test_wm2_config_smoke.cpp links Catch2 and X11 with no GTK by design. The arm with no case is a user hovering the slider and reading two sentences instead of one; the smoke half spawns the real wm2-config but asserts only that its window maps. | open |  | 2026-09-07T04:21:06.542Z |  |
| 48 | 09 | unrun-verify | apps/wm2-config/MenuPage.cpp |  | Codex pass-4 C4 (commit 1a8d3d2): MenuPage::edit now finds the row it began on through menuEntryReplacementIndex(), which prefers the captured index and falls back to identity. All three outcomes of that rule ARE driven display-free in tests/test_wm2_config_smoke.cpp ('editing the second of two identical rows replaces the second, not the first'), and the page's use of it is a comment-stripped source guard. What is NOT driven is the live arm: selecting the second of two identical rows in the real GTK list, editing it through gtk_dialog_run(), and observing that the second row changed -- reaching it means synthesising input into a modal GTK dialog, which is the same limit row 21 records for the reload-during-dialog case. | open |  | 2026-09-07T04:21:06.736Z |  |

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
  },
  {
    "id": 19,
    "kind": "unrun-verify",
    "phase": "09",
    "file": "scripts/gates/build-all.sh",
    "line": null,
    "description": "The release tree and its link audit were not run for plan 09-06; only debug and asan were gated",
    "status": "fixed",
    "reason": "Release gate run by the orchestrator after wave 6 closed: build-all.sh release green, 498/498, link audit OK (24 entries, all in the intended runtime set)",
    "recorded_at": "2026-09-06T12:03:32.878Z",
    "resolved_at": "2026-09-06T12:15:16.921Z"
  },
  {
    "id": 20,
    "kind": "deviation",
    "phase": "09",
    "file": "tests/test_wm2_config_smoke.cpp",
    "line": null,
    "description": "Eight of the nine Behaviour settings are proven here only as far as the window manager ADOPTING the value through the GUI's own FormState and ProtocolClient; that adopting it changes an observable behaviour is owned per setting by the [wm_config_live] cases from 09-05. Only click-to-focus has both halves in one case (the tracer). The join -- the value that arrived through the GUI's client is the value whose behaviour changed -- is an inference for the other eight",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T13:15:04.446Z",
    "resolved_at": null
  },
  {
    "id": 21,
    "kind": "deviation",
    "phase": "09",
    "file": "tests/test_wm2_config_smoke.cpp",
    "line": null,
    "description": "'a reload notice arriving with an entry dialog open leaves the dialog's contents alone' is structural: it holds a MenuEntryDraft across adoptEffectiveMenuEntries() and adds a comment-stripped source guard that MenuPage::refreshFromForm() names no dialog. The live arm -- a real reload while gtk_dialog_run() is spinning -- is not driven, because reaching it means synthesising input into a modal GTK dialog",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T13:15:04.636Z",
    "resolved_at": null
  },
  {
    "id": 22,
    "kind": "deviation",
    "phase": "09",
    "file": "apps/wm2-config/MenuPage.cpp",
    "line": null,
    "description": "The entry dialog states the name-override rule unconditionally rather than only when the typed name matches a discovered application. Detecting a match needs the discovered application NAMES and no protocol key exposes them; publishing the user's installed-application list over the socket was judged a wider surface than the note is worth",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T13:15:04.837Z",
    "resolved_at": null
  },
  {
    "id": 23,
    "kind": "unrun-verify",
    "phase": "09",
    "file": "scripts/gates/build-all.sh",
    "line": null,
    "description": "The release tree and its link audit were not run for plan 09-07; only debug (526/526) and asan were gated. No link line of a shipped target changed in this plan -- only the test target gained libXtst -- but that is an argument rather than a run",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T13:15:05.034Z",
    "resolved_at": null
  },
  {
    "id": 24,
    "kind": "unrun-verify",
    "phase": "09",
    "file": "scripts/gates/build-all.sh",
    "line": null,
    "description": "Release and ASan trees were last run in full at 530 tests (commit 441d62d); the 531st test, a display-free desktop-entry case added afterwards, was re-run in the ASan tree by label only and the release suite was not re-run at the final commit",
    "status": "fixed",
    "reason": "Release gate re-run by the orchestrator after wave 8 closed at the final commit: build-all.sh release green, 531/531, link audit OK (24 entries, all in the intended runtime set)",
    "recorded_at": "2026-09-06T15:09:02.517Z",
    "resolved_at": "2026-09-06T15:22:50.706Z"
  },
  {
    "id": 25,
    "kind": "unmet-truth",
    "phase": "09",
    "file": "src/Buttons.cpp",
    "line": null,
    "description": "Task 1's runtime cases prove the Configure row is present/absent by ROW COUNT and by launch identity, not by reading the label off the screen: the menu paints text with Xft and the server keeps no strings. The label claim is carried by display-free [menupaint] cases over the shared RootMenuLayout instead.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T16:20:59.745Z",
    "resolved_at": null
  },
  {
    "id": 26,
    "kind": "unmet-truth",
    "phase": "09",
    "file": "scripts/gates/doc-keys.sh",
    "line": null,
    "description": "Direction 2 of the doc-keys gate does not catch a bare unhyphenated word presented in plain backticks that is not an accepted key; in prose that form is indistinguishable from an ordinary word. Hyphenated inventions and 'word = value' config lines are caught. Stated in the script.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T16:20:59.930Z",
    "resolved_at": null
  },
  {
    "id": 27,
    "kind": "unmet-truth",
    "phase": "09",
    "file": ".planning/phases/09-config-gui-ipc/evidence/remote-desktop/README.md",
    "line": null,
    "description": "The first wm2-config on a freshly started X server holds ~101 MB and every later one ~50 MB, reproducibly on Xvfb and TigerVNC. Two candidate causes ruled out (per-user fontconfig cache; being the first client). The cause was not determined.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T16:21:00.108Z",
    "resolved_at": null
  },
  {
    "id": 28,
    "kind": "deviation",
    "phase": "09",
    "file": ".planning/phases/08.5-v1.0-closeout/evidence/tightvnc/capabilities.txt",
    "line": null,
    "description": "Pre-existing: capabilities.txt files committed under 08- and 08.5- evidence contain this machine's hostname from 'uname -n'. The public repo prohibits host identifiers. The capture script was fixed in 09-09; the already-committed captures were NOT rewritten (out of this plan's scope).",
    "status": "fixed",
    "reason": "Orchestrator scrub after wave 9: the hostname replaced by the placeholder <workstation> in all 16 tracked files (15 evidence files under phases 08 and 08.5, one comment and one diagnostic string in tests/test_wm_resource.cpp). The pushed history still carries the name; whether to rewrite it is the operator's decision.",
    "recorded_at": "2026-09-06T16:21:00.292Z",
    "resolved_at": "2026-09-06T16:31:16.616Z"
  },
  {
    "id": 29,
    "kind": "unrun-verify",
    "phase": "09",
    "file": "",
    "line": null,
    "description": "WR-04 review fix: the write-failure errno is now captured at each failure site, but no case drives it. The three sites need write(), fsync() or close() to fail on a real descriptor, which needs an LD_PRELOAD shim or a deliberately full filesystem -- neither has a precedent in this project, and both are heavier apparatus than the defect.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T17:35:48.226Z",
    "resolved_at": null
  },
  {
    "id": 30,
    "kind": "unrun-verify",
    "phase": "09",
    "file": "",
    "line": null,
    "description": "WR-14 review fix: the button-hold tick is now measured rather than assumed, but no case drives the over-count. Reproducing it needs a connection within 50 ms of its ten-second silence deadline at the moment the press begins -- a 50 ms window reached after a ten-second wait. The existing [wm_config_runtime] destroy-window-delay case guards both directions of the arithmetic.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T17:35:48.425Z",
    "resolved_at": null
  },
  {
    "id": 31,
    "kind": "unrun-verify",
    "phase": "09",
    "file": "",
    "line": null,
    "description": "WR-16 review fix: showGeometry's buffer is now 32 bytes and snprintf, but the overflow is unreachable through any caller -- every one passes screen-clamped coordinates -- so a case would have to call the method with INT_MIN directly and would prove the arithmetic rather than the window manager.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T17:35:48.610Z",
    "resolved_at": null
  },
  {
    "id": 32,
    "kind": "unrun-verify",
    "phase": "09",
    "file": "",
    "line": null,
    "description": "WR-17 review fix: install-components.sh now refuses an absolute build-dir (verified by running it: exit 2 with the reason). Not automated -- the project has no harness for gate scripts and one built for a two-line argument guard would be more apparatus than the defect.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T17:35:48.788Z",
    "resolved_at": null
  },
  {
    "id": 33,
    "kind": "unrun-verify",
    "phase": "09",
    "file": "",
    "line": null,
    "description": "WR-11 review fix: the GLib source is now detached from ProtocolClient's state handler, but the GLib wiring itself has no case -- it needs a GTK main loop, and the project has no harness that can drive a GLib source and inspect its attachment. The hook the fix hangs off IS covered display-free ([wm2_config_smoke][protocol][nonblocking], 'a disconnect from a request path still announces itself').",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T17:35:48.974Z",
    "resolved_at": null
  },
  {
    "id": 34,
    "kind": "unrun-verify",
    "phase": "09",
    "file": "",
    "line": null,
    "description": "WR-12 review fix: wm2-ctl now skips an unsolicited reloaded, and the classification is unit-tested ([config_protocol][notice]). The end-to-end interleaving is not reachable deterministically from the fixture: it needs a reload to land between wm2-ctl's hello-ack and its own reply, and nothing outside the window manager can schedule that.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T17:35:49.158Z",
    "resolved_at": null
  },
  {
    "id": 35,
    "kind": "deviation",
    "phase": "09",
    "file": "",
    "line": null,
    "description": "Pre-existing, found by the review-fix pass's asan gate: the [wm2_config_smoke] wm2-config RSS budget shipped with the debug constant alone (160 MB, measured 101 MB), so the asan tree failed it on the instrumentation -- 172 MB, stable over three runs. Confirmed pre-existing by rebuilding the settings window from the pre-review sources (c4abfc6) in the same asan tree: 171.9-172.5 MB, the same figure. Given a per-tree constant in the shape tests/test_wm_resource.cpp already established (272 MB asan, ~1.6x the measurement, the same headroom the debug figure carries).",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T17:59:53.329Z",
    "resolved_at": null
  },
  {
    "id": 36,
    "kind": "unrun-verify",
    "phase": "09",
    "file": "",
    "line": null,
    "description": "W-05 review-fix residual: the window manager now excludes the reload requester from D-08's broadcast, so each client gets exactly one line per reload of its own (asserted by [config_socket][reentrancy] 'the client that asked for a reload is not also broadcast to'). The RESIDUAL is untested and untestable from this fixture: a foreign notice can still land on a connection between that client SENDING its reload and its reply arriving, and no type-only classifier can tell it from the reply -- the version-1 contract is frozen and carries no correlator. Reaching it needs a second client's reload to be serviced in the window between our send and our service, which nothing outside the window manager can schedule.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T19:03:57.766Z",
    "resolved_at": null
  },
  {
    "id": 37,
    "kind": "unrun-verify",
    "phase": "09",
    "file": "",
    "line": null,
    "description": "W-06 review-fix residual: the CR-03 ownership arm (fstat's st_uid != geteuid) is now driven by a case that REACHES it -- [config_socket][directory] 'a socket directory owned by somebody else is refused' -- but that case can only run as a process able to give a directory away, so it SKIPS on this host (non-root; also skips under 'unshare -Ur', where chown to an unmapped uid returns EINVAL). It never passes vacuously: both the euid gate and the chown failure are SKIPs, not silent successes. The reachable half (the mkdir refusal) is a separate case scoped entirely inside a scratch TempDir, so no value of euid can make either case create anything at the filesystem root.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T19:06:03.889Z",
    "resolved_at": null
  },
  {
    "id": 38,
    "kind": "deviation",
    "phase": "09",
    "file": "src/Manager.cpp",
    "line": 2049,
    "description": "Review fix WR-13 narrowed plan 09-04's must-have T7 after its SUMMARY was written: a set of tab-font, menu-font or frame-thickness arriving while a modal grab is held is now REFUSED with a reason rather than applied; documented in docs/RELEASE-NOTES.md but the plan's truth 'applied without waiting for the grab to end' no longer holds for those three keys",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T20:05:07.666Z",
    "resolved_at": null
  },
  {
    "id": 39,
    "kind": "unrun-verify",
    "phase": "09",
    "file": "apps/wm2-ctl/main.cpp",
    "line": 214,
    "description": "Codex PR-review fix P1 (wm2-ctl non-blocking socket, commit 3ef61cc): the CONNECT half is covered by a RED-first [wm_config_live] case (a saturated accept queue; the tool now exits 2 on its own 15 s deadline instead of hanging until the harness watchdog kills it). The SEND half -- the EAGAIN arm that now waits on POLLOUT instead of spinning -- has no automated case. Reaching it needs the peer to stop reading AND more than one socket buffer of data in flight; AF_UNIX stream flow control is charged to the SENDER's SO_SNDBUF (default ~208 KB on this host, not settable from outside the tool) while Linux caps a single argv string at MAX_ARG_STRLEN = 128 KB, so no invocation a test can construct fills it. Verified by reading instead: the arm is the same shape as ProtocolClient.cpp's, which WR-09 closed and which is exercised there.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T21:48:51.990Z",
    "resolved_at": null
  },
  {
    "id": 40,
    "kind": "unrun-verify",
    "phase": "09",
    "file": "src/Client.cpp",
    "line": null,
    "description": "Codex PR-review pass 2, P2 fullscreen fix (commit 60c758f): the deferred-refresh flags are driven RED-first for a frame-thickness and a tab-background change applied while a client is fullscreen ([wm_config_live] \"a client that was fullscreen while the configuration moved comes back wearing the new one\"). Two arms share that code and have no case of their own: a TAB-FONT change during fullscreen (it sets the same m_frameLayoutStale flag and is replayed through the same Border::relayoutForTabFont call, so the case would differ only in which key is set), and a client that is HIDDEN as well as fullscreen when the change arrives (the frame windows are unmapped, so the geometry is assertable but no pixel is). Verified by reading: applyDeferredFrameRefresh() has one path and the thickness case reaches all of it.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T22:51:02.152Z",
    "resolved_at": null
  },
  {
    "id": 41,
    "kind": "unrun-verify",
    "phase": "09",
    "file": "apps/wm2-config/FormState.cpp",
    "line": null,
    "description": "Codex PR-review pass 2, P2 lower-layer fix (commit e981cdd): FormState::refreshLowerLayers is driven RED-first for a single setting through the reload path ([wm2_config_smoke] \"a file re-read moves what Reset will produce, and keeps the edits it finds\"), and the window wiring is asserted by a comment-stripped source guard on onReloadNotice() rather than by a driven GTK reload -- the same precedent row 21 records for the entry-dialog case. Two arms have no case: the MENU-ENTRIES half (m_menuBelowUser, and a pending menu reset following the new lower layer), and the SAVE path, which re-reads the layers for the same reason and now refreshes them the same way. Both are the same three lines of the same function as the covered arm.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-06T22:51:09.093Z",
    "resolved_at": null
  },
  {
    "id": 42,
    "kind": "unrun-verify",
    "phase": "09",
    "file": "src/Manager.cpp",
    "line": null,
    "description": "Codex PR-review pass 3, P2 socket-property fix (commit d58e17a): the SHUTDOWN arm is driven RED-first ([wm_socket] \"The published socket path is withdrawn when the window manager exits\" -- the fixture's X server outlives the window manager, so the root window is still there to be asked after a clean SIGTERM exit; red on CHECK(withdrawn) with _WM2_CONFIG_SOCKET still naming the unlinked node). The STARTUP arm -- setupEwmhProperties()'s new else, which deletes a PREDECESSOR's stale property when this start could not bind -- has no case. It needs a property planted on root BEFORE the window manager publishes, and WmFixture spawns the window manager inside its own constructor with no hook to run anything first; a second window manager cannot be started on a fixture's display either (the display dies with the fixture, and _WM2_RUNNING makes one owner per display). The nearest driven case, \"A socket path too long for the address structure is named, not truncated\", asserts the property is absent after a no-listener start but cannot distinguish the else branch from the old no-else code, because nothing stale was there to remove. Verified by reading: the else is one XDeleteProperty through the same helper release() uses, and that helper is exercised.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-07T00:00:00.000Z",
    "resolved_at": null
  },
  {
    "id": 43,
    "kind": "unrun-verify",
    "phase": "09",
    "file": "src/Events.cpp",
    "line": null,
    "description": "Codex PR-review pass 3, P2 failed-listener fix (commit 8df55d8): the TRANSPORT half is driven RED-first ([config_socket] \"a listener that poll() reports as failed shuts the server down\" -- POLLNVAL injected into the listener's revents by hand; red on isListening(), on the next poll set being non-empty, on the connection count and on the socket node still existing). The WINDOW MANAGER half -- serviceConfigSocket() reading the isListening() transition and withdrawing _WM2_CONFIG_SOCKET when a live listener dies mid-session -- has no case. Nothing outside the process can make the real binary's listening descriptor report POLLERR/POLLHUP/POLLNVAL: the descriptor is private to the window manager, and there is no lever to close or corrupt it (the WM2_FORCE_* levers are all read once at startup). Verified by reading: the transition is a two-line guard around the same unpublishConfigSocketPath() that release() calls, and that call is exercised by [wm_socket] \"The published socket path is withdrawn when the window manager exits\".",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-07T00:00:00.000Z",
    "resolved_at": null
  },
  {
    "id": 44,
    "kind": "unrun-verify",
    "phase": "09",
    "file": "src/Events.cpp",
    "line": null,
    "description": "CodeRabbit src-review F2 (commit ea40f1d): a bounded modalWait() no longer reports Timeout at the socket's clamped poll expiry instead of at the caller's deadline. The fix has NO externally observable consequence in this build, so no behavioural case could be red at the round base, and the red-first evidence is a SOURCE-level guard ([wm_socket][source][modalwait] 'modalWait reports Timeout from its deadline check and nowhere else' -- red at two Timeout returns, green at one). Every bounded caller today already absorbs an early timeout: Border.cpp's tab-button hold MEASURES each wait rather than assuming it lasted its bound (the WR-14 consumer-side fix), and Client.cpp's move and resize drags simply loop on Timeout; the two remaining callers pass -1. The behavioural companion ('a bounded modal wait that crosses a silent connection's deadline still takes its full delay') drives the hold across the moment a pre-hello connection's 10 s deadline expires, which is the only moment the clamp bites, and is green both before and after -- it guards the fix's own risk (a bounded wait that never times out), not the defect. The arm that has no case is a caller that trusts the bound WITHOUT measuring, because no such caller exists yet.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-07T03:29:48.782Z",
    "resolved_at": null
  },
  {
    "id": 45,
    "kind": "unrun-verify",
    "phase": "09",
    "file": "src/Manager.cpp",
    "line": null,
    "description": "CodeRabbit src-review F3 (commit 4018ef4): applyConfig() applying a frame-thickness and a tab-font together now walks the frames through relayoutFrameForFont (the thickness path PLUS the label repaint) instead of through relayoutFrame, which deliberately does not repaint the label. The red-first evidence is a SOURCE-level guard ([wm_socket][source] 'applying a thickness and a tab font together walks the frames once, through the path that repaints the label' -- red on the font walk being conditioned on the thickness having stayed put, and on the thickness walk not standing down). It could not be driven behaviourally, and the reason was MEASURED rather than assumed: the reshape the thickness path performs moves the tab's stair-stepped clip region, the server sends Expose for every region that comes back inside it (13 of them, counted from the test by selecting ExposureMask on the window manager's own tab), and Border::expose() -> drawLabel() repaints the label in the NEW face within one turn of the event loop. So no capture taken after a settle can see the stale glyphs on this X server. The behavioural case ('a thickness and a tab font applied together leave a tab that was already open looking like one opened afterwards') is green both before and after and asserts the combined application produces a correct tab at all. The arm with no case is the one the fix is FOR: a server with backing store -- a VNC session, which is this project's stated deployment -- restores a shrinking window's contents rather than asking for them back and sends no Expose, so the stale label would survive. Reproducing it needs an X server started with backing store enabled, which the shared Xvfb fixture does not provide.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-07T03:29:59.365Z",
    "resolved_at": null
  },
  {
    "id": 46,
    "kind": "unrun-verify",
    "phase": "09",
    "file": "apps/wm2-config/BehaviourPage.cpp",
    "line": null,
    "description": "CodeRabbit apps-chunk A1 (commit e12c0d2): BehaviourPage::addDelayRow's fallback range for a key configKeySpecFor() does not name is now the range the option table gives the other delay keys on the page (and an int-wide range if the table knows neither), rather than the degenerate lo == hi == 1 that was there. The red-first evidence is a SOURCE-level guard, added to the existing '[wm2_config_smoke] the delay controls are built from the parser's own bounds, not from a third copy' case -- red on 'spec->maxValue : 1' still being present and on the table-read fallback being absent, green after. It could not be driven behaviourally: the artifact is a gtk_spin_button range, tests/test_wm2_config_smoke.cpp links Catch2 and X11 with no GTK by design (that is what makes its display-free half honest), and the arm is unreachable in a correct build anyway -- every key this page passes to addDelayRow is one the option table declares, which the 'the Behaviour page carries what D-09 assigns to it and nothing else' case already asserts. The arm with no case is a build in which the option table has lost a delay key.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-07T04:20:55.359Z",
    "resolved_at": null
  },
  {
    "id": 47,
    "kind": "unrun-verify",
    "phase": "09",
    "file": "apps/wm2-config/AppearancePage.cpp",
    "line": null,
    "description": "CodeRabbit apps-chunk A3 (commit d51143f): AppearancePage::renderRow now APPENDS DISC-08's origin line to the thickness slider's own tooltip (Row::baseTooltip) instead of replacing it with gtk_widget_set_tooltip_text. The red-first evidence is a SOURCE-level guard ('[wm2_config_smoke] the thickness slider keeps its own tooltip when the origin line is added' -- red on baseTooltip being absent from both addThicknessRow and renderRow and on the bare replacement still being present, green after). It could not be driven behaviourally: reading a GTK tooltip needs a GTK build and a display, and tests/test_wm2_config_smoke.cpp links Catch2 and X11 with no GTK by design. The arm with no case is a user hovering the slider and reading two sentences instead of one; the smoke half spawns the real wm2-config but asserts only that its window maps.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-07T04:21:06.542Z",
    "resolved_at": null
  },
  {
    "id": 48,
    "kind": "unrun-verify",
    "phase": "09",
    "file": "apps/wm2-config/MenuPage.cpp",
    "line": null,
    "description": "Codex pass-4 C4 (commit 1a8d3d2): MenuPage::edit now finds the row it began on through menuEntryReplacementIndex(), which prefers the captured index and falls back to identity. All three outcomes of that rule ARE driven display-free in tests/test_wm2_config_smoke.cpp ('editing the second of two identical rows replaces the second, not the first'), and the page's use of it is a comment-stripped source guard. What is NOT driven is the live arm: selecting the second of two identical rows in the real GTK list, editing it through gtk_dialog_run(), and observing that the second row changed -- reaching it means synthesising input into a modal GTK dialog, which is the same limit row 21 records for the reload-during-dialog case.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-07T04:21:06.736Z",
    "resolved_at": null
  }
]
````
