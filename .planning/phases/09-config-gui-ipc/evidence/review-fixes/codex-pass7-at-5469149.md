# Codex review, pass 7, at 5469149 (branch vs origin/main)

Run 2026-09-07 with `codex review -c model=gpt-5.6-sol -c sandbox_mode=read-only --base origin/main` from a detached scratch worktree at 5469149. One P1 and two P2, verified against the code before delegation: a Save that lands while a live set is pending makes a later refusal restore the rejected value; only the user file is preflighted before a reload; sourceDetail names the last system file rather than the one that set the key.

---

The asynchronous save path can persist a setting after the window manager rejects it, including values capable of breaking the next startup. Reload handling and system-layer attribution also mishandle multi-layer configurations.

Full review comments:

- [P1] Wait for live-set replies before marking values saved — apps/wm2-config/main.cpp:439-442
  If the user presses Save while this asynchronous `set` is still pending and the window manager later refuses it, `save()` has already written the value and `markSaved()` has replaced `effective` with that same rejected value. The callback then calls `restoreRefused()`, which restores the rejected value rather than the prior working one; for a rejected colour this can persist a configuration that prevents the next startup. Track pending requests and prevent/resolve Save until they complete, or retain the value associated with each request independently of the saved baseline.

- [P2] Validate every active config layer before accepting reload — src/Manager.cpp:2492-2501
  Only the user file is checked before `Config::load()`. If a system file under `XDG_CONFIG_DIRS` contributed settings at startup and later becomes inaccessible, `Config::applyFile()` silently skips it, so reload applies defaults or other layers and broadcasts success while unexpectedly dropping that system configuration. Preflight every configured layer and refuse the reload when an existing layer cannot be examined or read.

- [P2] Track the winning system file per setting — apps/wm2-config/FormState.cpp:83-86
  When multiple system configuration files exist, this records only the last existing path and later uses it as `sourceDetail` for every system-provided field. If an earlier file sets `frame-thickness` while the last file sets only an unrelated key, the tooltip incorrectly attributes `frame-thickness` to the last file. Record key presence while applying each system layer and retain the highest-precedence file that actually set each key.
