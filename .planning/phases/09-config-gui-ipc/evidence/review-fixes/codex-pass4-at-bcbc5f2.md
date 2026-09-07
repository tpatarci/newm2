# Codex review, pass 4, at bcbc5f2 (branch vs origin/main)

Run 2026-09-07 03:11Z with `codex review -c model=gpt-5.6-sol -c sandbox_mode=read-only --base origin/main` from a detached scratch worktree at bcbc5f2. Findings are data and were verified against the code before any fix.

---

The new configuration workflow mishandles layered menu entries, rejects valid existing menu configurations, and allows live values that cannot round-trip through the file format. It also edits the wrong duplicate menu row and can misreport configuration provenance.

Full review comments:

- [P2] Keep inherited menu entries out of user-file rewrites — apps/wm2-config/main.cpp:469-471
  When a system configuration defines manual menu entries, `m_form.menuEntries()` contains both system and user entries. Passing that merged list to the user-file writer means any Menu-page save copies system entries into the user file, so the next layered load duplicates them; Reset similarly writes the inherited entries instead of removing the user block. Track and write only the user-layer entries, using an empty block for reset.

- [P2] Support valid menu lists larger than one protocol frame — include/ConfigProtocol.h:153-153
  A configuration file may contain an arbitrary number of individually valid `menu-entry-*` groups, but `get menu-entries` aggregates all of them into one reply. Once that reply exceeds 4096 bytes, both clients reject it as `TooLong` and wm2-config disconnects during its initial read, even though the window manager successfully loaded the configuration. The protocol needs framing capable of carrying the supported menu model, or the file parser must enforce a matching total bound.

- [P2] Apply the config-file constraints to socket strings — src/Manager.cpp:2366-2371
  For string settings, this branch accepts values verbatim even though `Config::applyFile()` rejects values over 256 bytes and trims surrounding whitespace. Consequently a raw protocol client can receive an `ack` for a long or whitespace-delimited `new-window-command` that the file path cannot preserve or reload, violating the intended shared validation semantics. Reject strings that cannot round-trip through the configuration file before applying them.

- [P2] Preserve the selected occurrence when editing duplicate rows — apps/wm2-config/MenuPage.cpp:432-442
  If two menu rows have identical name, category, and argv, selecting the second one and editing it always replaces the first matching row because this search starts at the beginning and ignores the selected occurrence. Menu order is significant, so the wrong row is modified; retain the selected index or occurrence identity while still handling reloads during the dialog.

- [P3] Detect explicit user overrides independently of value equality — apps/wm2-config/FormState.cpp:79-82
  When the user file explicitly sets a key to the same value supplied by the lower layer, this comparison classifies it as built-in or system-provided rather than `UserFile`. The settings tooltip therefore reports the wrong source, and later changes to the system layer can make the significance of that explicit override confusing; source provenance must be determined from whether the file contains the key, not whether applying it changed the value.
