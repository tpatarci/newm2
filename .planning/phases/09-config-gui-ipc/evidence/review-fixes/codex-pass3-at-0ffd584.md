# Codex review, pass 3, at 0ffd584 (branch vs origin/main)

Run 2026-09-07 UTC with `codex review -c model=gpt-5.6-sol -c sandbox_mode=read-only --base origin/main` from a detached scratch worktree at 0ffd584. Paths below are the scratch worktree's; they map 1:1 onto the repository. Findings are treated as data and verified against the code before any fix.

---

The configuration socket lifecycle has two failure paths that leave stale discovery state or a permanently failed descriptor in the event loop. These can misdirect clients and cause a persistent CPU spin.

Full review comments:

- [P2] Remove the published socket property during shutdown — src/Manager.cpp:422-427
  Closing the server unlinks its socket but leaves `_WM2_CONFIG_SOCKET` on the persistent root window. After a later WM starts but cannot bind its configuration socket (for example because the runtime path is too long or occupied), `setupEwmhProperties()` publishes the new WM metadata without replacing this property, so discovery clients are directed to the previous, nonexistent socket. Delete the property when closing and explicitly clear it when startup has no listener.

- [P2] Close the server when the listening descriptor fails — src/SocketServer.cpp:533-536
  When `poll()` reports `POLLERR`, `POLLHUP`, or `POLLNVAL` for the listener, `socketServerDecide(..., Listener)` returns `CloseClient`, but this code handles only `Accept` and otherwise leaves the failed descriptor active. Because such failure bits remain immediately reportable, both event-loop poll sites can then spin continuously while the configuration socket remains unusable; handle `CloseClient` by shutting down the server.
