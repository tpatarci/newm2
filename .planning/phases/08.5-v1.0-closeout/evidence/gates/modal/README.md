# Ledger 8 — the modal grab loops now observe SIGTERM

Six loops held the window manager's only thread while a button was down: the
root menu and `releaseGrab()` in `src/Buttons.cpp` waited in `XMaskEvent`; the
move and resize loops in `src/Client.cpp` and the tab-button loop in
`src/Border.cpp` slept 50 ms between `XCheckMaskEvent` sweeps; the gesture
recogniser in `src/Client.cpp` waited in `XMaskEvent`. None watched the exit
flag or the self-pipe, so SIGTERM was honoured only when the button came up.

`WindowManager::modalWait(mask, out, timeoutMs)` (`src/Events.cpp`) replaces
every one of them: `XCheckMaskEvent`, then `poll()` on the X descriptor and the
self-pipe read end. It returns `Event`, `Timeout` (bounded callers only) or
`Interrupted`; on `Interrupted` each caller leaves with nothing chosen, cleaned
up as its release path cleans up, and the main loop's `shutdownOnSignal()` does
the rest. The pipe is not drained by the modal wait.

## Reproduction

`SIGTERM while the root menu is held open is honoured without waiting for the
release` (`tests/test_wm_runtime.cpp`, `[wm_process]`): open the menu with
Button1 held via XTest, send SIGTERM to the fixture's own pid, require a clean
exit within 3000 ms, release only after the verdict.

| Tree | Log | Result |
| --- | --- | --- |
| before the fix | `red-before-fix.log` | `REQUIRE( clean )` false after 3.31 s |
| after the fix | `green-after-fix.log` | passed in 0.27 s |

Twelve related labels (`wm_process`, `wm_menulabel`, `menupaint`,
`wm_menureopen`, `wm_button`, `wm_geometry`, `wm_focus`, `wm_config_runtime`,
`wm_bevel`, `wm_circulate`, `wm_lifecycle`, `wm_state`; 73 cases) pass after
the fix. A first draft dropped the `MotionNotify` the wait had already dequeued
and failed `A window dragged off the top-left keeps its tab on screen`; the
event is now handled like any the sweep found.

## What this does not change

The loops are still modal: a `MapRequest` or `DestroyNotify` for another
client that arrives during a held grab is still processed only after the grab
ends. That is the design inherited from wm2 and is recorded in the release
notes as a known limitation rather than fixed here.
