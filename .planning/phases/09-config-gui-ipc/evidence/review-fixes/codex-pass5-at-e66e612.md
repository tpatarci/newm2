# Codex review, pass 5, at e66e612 (branch vs origin/main)

Run 2026-09-07 06:12Z with `codex review -c model=gpt-5.6-sol -c sandbox_mode=read-only --base origin/main` from a detached scratch worktree at e66e612. All three findings verified against the code before any fix: GDK accepts CSS colour spellings X11 rejects and a bad tab colour is fatal at the next start (Border::allocateXftColors); the wire parser for menu entries enforces the trim rule but not the 256-byte or newline rules; reloadConfigFromDisk treats any failed existence check as a missing file.

---

The new configuration paths can persist colors the window manager cannot load, acknowledge menu data the file format cannot preserve, and silently discard an inaccessible user layer during reload. These are functional correctness issues despite the extensive test coverage.

Full review comments:

- [P1] Validate raw colors with the X11 grammar — apps/wm2-config/AppearancePage.cpp:560-562
  When a user enters a GDK-valid but X11-invalid spelling such as a CSS `rgb(...)`/`rgba(...)` color, this check accepts it and updates the form, while the live `set` is later refused by `XAllocNamedColor`. The error callback only updates the status text, so Save still persists the rejected value; on the next launch, affected startup color allocation can call `fatal()` and prevent the window manager from starting. Canonicalize accepted input to `#RRGGBB`, validate with the backend's grammar, or prevent rejected values from being saved.

- [P2] Enforce file limits on socket menu entries — src/Config.cpp:942-949
  For a raw client such as `wm2-ctl`, this path accepts menu-entry fields longer than 256 bytes or names/categories containing decoded line breaks, even though `Config::applyFile()` skips overlong values and `configFileWrite()` refuses both forms. The server consequently acknowledges live state that cannot be saved or reproduced from the configuration file; validate each `val` against the same per-field size and line-break rules before applying it.

- [P2] Distinguish a missing config from a failed existence check — src/Manager.cpp:2481-2482
  If the user config cannot be examined because a parent directory is inaccessible, the path has a symlink loop, or the filesystem returns an I/O error, the first `access(..., F_OK)` returns failure and this condition treats it exactly like a missing file. `Config::load()` then silently skips the user layer, applies defaults/system values, and reports the reload as successful, unexpectedly changing the running desktop. Inspect `errno` and refuse reload for failures other than `ENOENT`.
