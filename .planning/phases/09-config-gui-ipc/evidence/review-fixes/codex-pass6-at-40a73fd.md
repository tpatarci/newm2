# Codex review, pass 6, at 40a73fd (branch vs origin/main)

Run 2026-09-07 with `codex review -c model=gpt-5.6-sol -c sandbox_mode=read-only --base origin/main` from a detached scratch worktree at 40a73fd. One P2, verified: the requester is excluded from the reloaded broadcast and its direct answer reached a callback that handled only the error arm. Fixed in 5469149.

---

The reload button successfully updates the window manager but does not refresh the requesting GUI, leaving its displayed state stale. This is a functional defect in a primary configuration workflow.

Review comment:

- [P2] Refresh the form after a successful requested reload — apps/wm2-config/main.cpp:534-539
  When the user clicks “Re-read files,” the server excludes the requesting connection from the reload broadcast, and `ProtocolClient` treats its `reloaded` response as a reply rather than a notice. This callback handles only errors, so a successful reload never calls `onReloadNotice()` or re-fetches effective values, leaving the GUI showing stale settings after its own reload request.
