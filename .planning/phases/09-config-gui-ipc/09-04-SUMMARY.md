---
phase: 09-config-gui-ipc
plan: 04
subsystem: infra
tags: [unix-socket, ipc, cli, live-config, x11-shape, frame-geometry, catch2, posix]

requires:
  - phase: 09-config-gui-ipc
    provides: "09-03's `ConfigSocketServer`, `WindowManager::buildPollSet()`, `handleConfigRequest()` and the `configSocketPath()` / `configSocketPathFits()` helpers -- the socket that answered questions, which this plan turns into one that changes the desktop"
  - phase: 09-config-gui-ipc
    provides: "09-02's frozen version-1 codec in `include/ConfigProtocol.h` (get/value/set/ack/error/reload/reloaded were frozen there and are served here), and the `Status` macro guard that lets an X11 translation unit include it"
  - phase: 09-config-gui-ipc
    provides: "09-01's `configFileManagedKeys()` in `include/ConfigFileWriter.h` -- the settable key set `set` had to agree with, now asserted equal to `configKeySpecs()`"
  - phase: 08.5-v1.0-closeout
    provides: "`tests/support/WmFixture.h` and its `ChildProcess`, the 08.5-06 guard-ordering rule, and 08.5-13's QuietXErrors"
  - phase: 08-xrandr-vnc-compatibility-focus-rules
    provides: "plan 08-13's single option table (`kOptionSpecs`), which this plan turned into the one place the integer bounds are written down"
provides:
  - "`apps/wm2-ctl/main.cpp` and the top-level `wm2-ctl` CMake target: status / get / set / reload, `--socket`, `--help`, linking libc and libstdc++ and nothing else"
  - "DISC-01a's exit codes (0 acknowledged, 1 refused, 2 nothing to talk to, 3 usage) and DISC-01b's display-free path resolution"
  - "`WindowManager::applyConfig(const Config&)` -- DISC-06a's single funnel, diffing before it acts"
  - "`WindowManager::applyConfigSet()` and `WindowManager::reloadConfigFromDisk()`"
  - "`WindowManager::savedConfig()` and the `m_savedConfig` snapshot -- DISC-07's second Config, replaced by every reload and never by a set"
  - "`Border::relayoutForFrameThickness()` and `Client::relayoutFrame()` -- in-place re-framing with no reparent"
  - "`ConfigValueKind`, `ConfigKeySpec`, `configKeySpecs()`, `configKeySpecFor()`, `configValueForKey()` in include/Config.h -- a VIEW of the existing option table, not a second list"
  - "Machine-readable integer bounds on `kOptionSpecs`; `applyKeyValue()` and `applyCliArgs()` now read 1..50 and 1..60000 from those rows"
  - "The get / set / reload half of the dispatcher in `src/Manager.cpp`"
  - "ctest label `wm_config_live` (16 cases, driven through the real `wm2-ctl` binary)"
  - "docs/RELEASE-NOTES.md 'Changing settings while it runs'"
affects: [09-05-live-apply, 09-06-menu-page, 09-07-protocol-client, 09-08-gui, 09-09-release]

actuals:
  tokens: 26000     # chars/4 over the realized diff (106,055 chars, e164d66~1..023a592)
  tasks: 3
  commits: 3

tech-stack:
  added:
    - "A second shipped binary, wm2-ctl, with NO library linkage at all (not even X11)"
  patterns:
    - "A settable-surface VIEW: a third consumer of a table gets a typed view of it rather than a third copy of it"
    - "Validate strictly at the boundary, then apply through the shared parser -- the socket is stricter than the config file and never looser"
    - "Fail-closed read-back: what the parser stored is compared with what was agreed, so a future divergence between validation and parser is a refusal rather than a silent misapply"
    - "The shipped CLI as the test suite's protocol client, so the tool the user runs and the tool the suite proves are one program"
    - "ShapeNotify as the observable for redundant work, because the server suppresses ConfigureNotify for a no-op reconfigure"

key-files:
  created:
    - apps/wm2-ctl/main.cpp
    - tests/test_wm_config_live.cpp
  modified:
    - include/Config.h
    - src/Config.cpp
    - include/Manager.h
    - src/Manager.cpp
    - include/Border.h
    - src/Border.cpp
    - include/Client.h
    - src/Client.cpp
    - src/main.cpp
    - CMakeLists.txt
    - docs/RELEASE-NOTES.md

key-decisions:
  - "DISC-06a -- applyConfig() is the only way a configuration change reaches running state; it diffs field by field, then stores `next` WHOLE so a later plan adds an application and never an assignment"
  - "DISC-07 -- m_savedConfig is populated at startup, replaced by every successful reload, and never touched by a set (because a set writes no file), so the GUI's revert is 'send the saved values back' with no new message type"
  - "DISC-01a -- 0/1/2/3, with 1 (refused) deliberately distinct from 2 (nothing listening); a timeout after connecting reports 2, because a timeout is not a reply"
  - "DISC-01b -- wm2-ctl resolves the socket path from $DISPLAY through configSocketPath() and accepts --socket; it never reads _WM2_CONFIG_SOCKET, because reading a property means linking X11"
  - "A set is validated for kind and range BEFORE Config::applyKeyValue() sees it, then applied through that same parser on a copy -- stricter than the file path (a clamp is a refusal), never looser"
  - "configKeySpecs() is a view of kOptionSpecs rather than a new table, and the integer bounds moved INTO those rows, removing the third and fourth copies of 1..50"
  - "The frame re-layout moves decoration and never content: the client keeps its size AND its absolute screen position, so no synthetic ConfigureNotify is owed"
  - "Hidden clients are re-laid out too, because addToHiddenList() moves rather than copies"
  - "A fullscreen client is skipped: its frame is stripped, and restoreFromFullscreen() reads the new thickness through xIndent()/yIndent() when it rebuilds"
  - "A reload re-runs the WHOLE layered load including the command line, so an override given at startup still wins afterwards"
  - "A user config file that EXISTS but cannot be read is an error naming the file; one that does not exist is not"

patterns-established:
  - "Every live-apply case maps its client BEFORE issuing the command, because a window mapped afterwards would pick up a stored-but-never-applied value at map time"
  - "The observable for 'no redundant work' is ShapeNotify, not ConfigureNotify: the server suppresses the latter for a reconfigure that changes nothing, so a case built on it cannot fail"
  - "Every fixture in a config test overrides XDG_CONFIG_HOME *and* XDG_CONFIG_DIRS into its own temp tree; a test that writes to $HOME is a test that will one day delete somebody's settings"
  - "A guard is recorded as proven only after the defect it forbids has been introduced and seen to redden exactly the case that names it"

requirements-completed: [CGUI-02, CGUI-04]

coverage:
  - id: D1
    description: "`wm2-ctl set frame-thickness` changes the frame of every already-managed window immediately, with no restart and no window closing and reopening"
    requirement: CGUI-04
    verification:
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#set frame-thickness re-frames a window that was already mapped"
        status: pass
      - kind: other
        ref: "mutation check: with applyConfig()'s re-layout removed, this case and the reload case redden and nothing else does"
        status: pass
    human_judgment: false
  - id: D2
    description: "`wm2-ctl get <key>` returns the value the running window manager is actually using, not the value on disk"
    requirement: CGUI-04
    verification:
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#wm2-ctl get prints the value the window manager is actually using"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#a reload discards a set, because a set writes no file"
        status: pass
    human_judgment: false
  - id: D3
    description: "`wm2-ctl status` prints the seven status fields, and `wm2-ctl reload` makes the window manager re-read its config layers from disk"
    requirement: CGUI-02
    verification:
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#wm2-ctl status prints the seven fields the window manager reports"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#reload re-reads the file and applies it to windows already open"
        status: pass
    human_judgment: false
  - id: D4
    description: "`wm2-ctl` links no X11, no Xft and no GTK, so it runs over SSH on a droplet with no display at all"
    requirement: CGUI-02
    verification:
      - kind: other
        ref: "ldd build/debug/wm2-ctl | grep -c -e libX11 -e libXft -e libgtk -e libglib == 0 (and 0 in the ASan tree)"
        status: pass
      - kind: other
        ref: "grep -cE '#include *[<\"](X11|gtk|glib|gdk)' apps/wm2-ctl/main.cpp == 0"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#wm2-ctl exits 2 when there is no window manager to talk to (run with DISPLAY unset)"
        status: pass
    human_judgment: false
  - id: D5
    description: "A `set` whose value the config parser would reject is refused with an error naming the key and the reason, and the running window manager's state is unchanged"
    requirement: CGUI-04
    verification:
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#a value the parser would clamp is refused and changes nothing"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#a boolean set accepts the spellings the config file accepts"
        status: pass
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#the settable key list and the config file's managed key list agree"
        status: pass
    human_judgment: false
  - id: D6
    description: "Applying the same `set` twice produces the same acknowledgement, leaves the window geometry identical, and performs no second re-frame"
    requirement: CGUI-04
    verification:
      - kind: integration
        ref: "tests/test_wm_config_live.cpp#applying the same set twice does nothing the second time"
        status: pass
      - kind: other
        ref: "mutation check: with applyConfig()'s diff removed, this case reddens with 4 re-shapes; green again on revert"
        status: pass
    human_judgment: false
  - id: D7
    description: "Two `set` messages arriving together are applied in arrival order, in the event loop's own thread, and a `set` arriving during a modal grab is applied without waiting for the grab"
    requirement: CGUI-04
    verification:
      - kind: integration
        ref: "tests/test_wm_socket.cpp#The socket answers while a modal pointer grab is held (09-03; the shared descriptor set every message including set now travels through)"
        status: pass
      - kind: other
        ref: "structural: handleConfigRequest() is called from serviceConfigSocket() on the window manager's single thread, one frame at a time; there is no second thread and no queue to reorder"
        status: pass
    human_judgment: true
    rationale: "Arrival ORDER of two simultaneous sets is single-threadedness by construction rather than something a case observes; the held-grab half is asserted by 09-03's case, but no case in this plan issues two overlapping sets and checks which won"
  - id: D8
    description: "A restart-only exception is named in the release notes: frame-thickness applies live, every other key is stored and reported but not yet applied"
    requirement: CGUI-04
    verification:
      - kind: other
        ref: "docs/RELEASE-NOTES.md 'Which settings apply live' paragraph; grep gates for all four subcommands and both the property and the tool name"
        status: pass
      - kind: other
        ref: "build/debug/wm2-ctl --help lists all four subcommands the notes document"
        status: pass
    human_judgment: false

duration: 30 min
completed: 2026-09-06
status: complete
---

# Phase 09 Plan 04: wm2-ctl and Live Configuration Summary

**The configuration socket stopped merely answering questions: `wm2-ctl set frame-thickness 21` typed at a shell now re-frames every window already on the screen, through one funnel, with the same parser the config file uses — and the tool that does it links libc, libstdc++ and nothing else.**

## Performance

- **Duration:** 30 min
- **Started:** 2026-09-06T08:45Z
- **Completed:** 2026-09-06T09:16Z
- **Tasks:** 3 of 3
- **Files modified:** 13 (2 created, 11 modified)
- **Tests added:** 16 (`[wm_config_live]`); full debug suite 437 → 453, all green

## Accomplishments

- **The plan's hardest claim is true and proven hard.** `frame-thickness` changes the geometry of the frame, the sideways tab, the button and the resize handle of every window already mapped — including hidden ones — with no reparent, so no flash and no lost stacking order, and with the client's own window keeping both its size and its absolute position on screen. The mutation check is the evidence: removing the re-layout from `applyConfig()` reddens the live case and the reload case and *nothing else in the suite*.
- **A second shipped binary that links nothing.** `ldd build/debug/wm2-ctl` names `libstdc++`, `libgcc_s`, `libc`, `libm` and the loader. No `libX11`, no `libXft`, no `libgtk`, no `libglib` — in the debug tree and in the ASan tree. That is D-17's whole point: a droplet reached over SSH with no desktop open can still drive the window manager.
- **The prohibition is structural, not merely observed.** A `set` is checked for kind and range *before* `Config::applyKeyValue()` sees it, applied to a **copy**, and then read back and compared with what was agreed; only a clean acceptance reaches `applyConfig()`. The socket is therefore *stricter* than the config-file path — where the file clamps `frame-thickness=500` to 50 and warns to a stderr nobody is reading, the socket refuses and changes nothing — and never looser, because the value that survives goes through the very same parser.
- **The third and fourth copies of "1 to 50" are gone.** The integer bounds moved into `kOptionSpecs`, the single option table plan 08-13 created so `--help` and `getopt_long()` could not drift; `applyKeyValue()`, `applyCliArgs()` and the socket's validation now all read them from there. `configKeySpecs()` is a typed **view** of that table rather than a fifth list, and a case asserts it names exactly the same keys as `configFileManagedKeys()`.
- **DISC-07 is represented in the code rather than inferred by the reader.** The window manager holds `m_config` (what it is drawing with) and `m_savedConfig` (what the files last said). A `set` moves the first and not the second; a `reload` replaces both. The difference between them is exactly the set of unsaved changes, which is what makes the GUI's revert expressible with no new message type.
- **`wm2-ctl` is the suite's protocol client.** Every behavioural case drives the window manager by forking the real binary, so the tool the user has and the tool the suite proves are one program.

## Task Commits

1. **Task 1 (tracer): one setting changes the running desktop, end to end** — `e164d66` (feat)
2. **Task 2: reload, and the two configurations the window manager holds** — `9ee5f2b` (test)
3. **Task 3: document the socket and the tool, checked against the binary** — `023a592` (docs)

## Files Created/Modified

- `apps/wm2-ctl/main.cpp` (new, ~400 lines) — argument parsing, DISC-01b path resolution, the D-15 handshake performed from the client's side (the acknowledgement is *required* before the request is written, so a squatter on the socket path is never handed a value), one bounded reply read, DISC-01a exit codes, `--help` generated from `configKeySpecs()`.
- `tests/test_wm_config_live.cpp` (new, 890 lines) — 16 cases, tag `[wm_config_live]`.
- `include/Config.h` / `src/Config.cpp` — `ConfigValueKind`, `ConfigKeySpec`, `configKeySpecs()`, `configKeySpecFor()`, `configValueForKey()`; integer bounds into `kOptionSpecs`; both integer parse sites re-pointed at them.
- `include/Manager.h` / `src/Manager.cpp` — `applyConfig()`, `applyConfigSet()`, `reloadConfigFromDisk()`, `savedConfig()`, `m_savedConfig`, `m_cliArgs`, the `argc`/`argv` constructor parameters, the `get`/`set`/`reload` dispatcher arms and the key-naming refusal helper.
- `include/Border.h` / `src/Border.cpp` — `relayoutForFrameThickness()`: resize handle re-sized *and* re-shaped (both are set only at creation, so `configure()` alone would leave a corner grabber sized for the old thickness), then `configure(..., force=true)`, then the child moved to the new content offset.
- `include/Client.h` / `src/Client.cpp` — `relayoutFrame()`, with the three skip conditions named individually rather than under one catch-all.
- `src/main.cpp` — `argc`/`argv` handed to the window manager.
- `CMakeLists.txt` — the `wm2-ctl` target (top-level, no `target_link_libraries` at all) and the `test_wm_config_live` target.
- `docs/RELEASE-NOTES.md` — the new "Changing settings while it runs" section, the useful-commands block, and two corrections to the existing socket subsection.

## Decisions Made

Beyond the DISC items in frontmatter:

- **A boolean `set` refuses a spelling the file accepts silently.** `Config`'s `parseBool()` maps every unrecognised string to `false`, which is fine for a file read once by somebody who can see the warning and wrong for a request: `set click-to-focus perhaps` would be acknowledged while meaning the opposite of what was typed. Only `true`/`false`/`1`/`0` are accepted; everything else is refused. Asserted by a case, including that `0` still works.
- **`stoi` is not good enough at a socket boundary.** `std::stoi("12abc")` returns 12. `parseWholeInt()` requires the whole string, so `set frame-thickness seven` is refused rather than half-read.
- **The read-back check fails closed.** If validation and the parser ever disagree, the reply is an error and `m_config` is untouched — a bug surfaces as a refusal, not as an unauthorised change.
- **A timeout after connecting reports exit 2, not 1.** DISC-01a defines 1 as "the window manager replied with an error"; a silence is not a reply. Documented in the release notes' exit-code table so a script author is not guessing.
- **`configFileKeyIsManaged()` is *not* linked into the window manager.** The plan's action names `configFileManagedKeys()` as the acceptance set for `set`. Using it would mean compiling `src/ConfigFileWriter.cpp` — the GUI's file writer — into the window-manager binary for one predicate. `configKeySpecFor()` answers the same question from a table the window manager already carries, and a `[wm_config_live]` case asserts the two lists name exactly the same keys, so the agreement is checked rather than assumed. Recorded as a reconciliation, not a silent substitution.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 — Blocking] The frame re-layout is not reachable without touching `Border` and `Client`**

- **Found during:** Task 1, designing the re-layout.
- **Issue:** The plan's `files_modified` names `src/Client.cpp` but not `src/Border.cpp` or the two headers. The frame's windows — `m_parent`, `m_tab`, `m_button`, `m_resize` — are private to `Border`, and the resize handle's *size* and *shape* are established only in `Border::configure()`'s creation branch. From `Client` alone the handle would keep the old thickness's dimensions and its triangular shape would stop matching the frame around it.
- **Fix:** `Border::relayoutForFrameThickness()` (public, declared with the reason it exists) plus `Client::relayoutFrame()` as the per-client entry point `applyConfig()` calls.
- **Verification:** The live case asserts the frame grew in both dimensions and the client did not move or resize; `ctest -L '^wm_geometry$'` stayed green (13/13), proving the re-layout did not regress an existing geometry guarantee.
- **Committed in:** `e164d66`.

**2. [Rule 2 — Missing critical] `Config` had no way to read a value back, and `set` cannot be checked without one**

- **Found during:** Task 1, implementing `get` and the acceptance check for `set`.
- **Issue:** Nothing in the codebase could answer "what is the effective value of key K?" — `get` needs it, and so does the check that the parser stored what was agreed. Writing that table inside `src/Manager.cpp` would have put a fifth list of key names in the tree (after `kOptionSpecs`, `applyKeyValue`'s chain, `applyCliArgs`'s chain and `configFileManagedKeys()`), and 09-05, 09-07 and 09-08 all need the same function.
- **Fix:** `configValueForKey()` in `include/Config.h`/`src/Config.cpp`, next to the parser it inverts, plus `configKeySpecs()` as a *view* of the existing option table rather than a new one. `include/Config.h` and `src/Config.cpp` are outside the plan's `files_modified`.
- **Verification:** `ctest -L '^(config|config_writer|rules|xdg)$'` green; the key-list agreement case asserts the view and `configFileManagedKeys()` name the same 21 settings.
- **Committed in:** `e164d66`.

**3. [Rule 1 — Bug] The idempotency case could not fail as first written**

- **Found during:** Task 1, mutation-checking.
- **Issue:** The case counted `ConfigureNotify` events on the frame and the client to prove "no second re-frame". Removing `applyConfig()`'s diff — so that *every* message re-lays every frame out — left it **green**: the X server suppresses `ConfigureNotify` for an `XConfigureWindow` that changes nothing, so a window manager doing the redundant work was indistinguishable from one that skipped it. A case that cannot fail is not a case.
- **Fix:** The counter now selects `ShapeNotifyMask` and counts `ShapeNotify`. The shape extension makes no such comparison — every rectangle-combining request produces an event whether or not the region differs — so the counter sees the *work* rather than only its visible effect. `PkgConfig::XEXT` added to the test target for this and only this.
- **Verification:** With the diff removed the case reddens at `reconfigurations == 0` with **4 re-shapes**; green again on revert.
- **Committed in:** `e164d66`.

**4. [Rule 3 — Blocking] `pollSleep()` collided with the fixture's**

- **Found during:** Task 1, first build of the test file.
- **Issue:** `tests/support/WmFixture.h` defines `wm2test::pollSleep()`, and `using namespace wm2test` makes an identically named local an *ambiguity* rather than an override.
- **Fix:** The local is `settleTick()`. Renamed rather than removed, because its call sites pump the window manager between ticks and read better with a distinct name.
- **Committed in:** `e164d66`.

### Planned-scope adjustments

**5. [Task boundary] The tracer landed Task 2's implementation; Task 2's commit is tests only**

`type="tracer"` requires production quality. `reload` and `set` are two arms of one dispatcher and both go through the one `applyConfig()` funnel, and DISC-07's snapshot has to be populated at construction — separating them would have meant shipping an intermediate commit whose `reload` still answered "not served by this build". Task 1 therefore carries `reloadConfigFromDisk()`, `m_savedConfig`, `m_cliArgs`, the constructor's `argc`/`argv` and the `src/main.cpp` change; Task 2's commit is the four reload cases, which *are* separable and were held back for it. Every acceptance criterion of both tasks is met; only the commit that carries each line of implementation differs from the plan's file lists. The same adjustment, for the same reason, was recorded by 09-03.

**6. [Reconciliation] `configFileManagedKeys()` is not the acceptance oracle**

See "Decisions Made" above. The plan's `set` action names it; the implementation uses `configKeySpecFor()` and asserts the two lists are identical. No key is settable that the file writer cannot save, and none is savable that `set` will not accept — which is what the plan's clause was protecting.

**7. [Reconciliation] No doc-parity guard exists in the test suite**

The execution brief said to read the doc-parity guard in the test suite before editing `docs/RELEASE-NOTES.md`. There is none: `grep -rn RELEASE-NOTES tests/ scripts/` finds nothing. The doc guards from 08.5 are recorded *evidence commands* (`.planning/phases/08.5-v1.0-closeout/evidence/gates/DOC-GUARDS.txt`), not ctest cases. This plan's Task 3 `<verify>` block is itself three such commands, all run and recorded below. Recorded so a later reader does not go looking for a guard that was never written.

**8. [Scope] The existing socket subsection was corrected, slightly beyond "the new section"**

Task 3's acceptance criterion asks that the diff be confined to the new section and the developers' command block. Two sentences in the pre-existing "The configuration socket" subsection said this release answers only `hello` and `status` and would change settings "in a later release" — both false as of `e164d66`. They were corrected. The subsection is inside "For developers"; leaving a statement the binary contradicts would have been worse than the wider diff.

**9. [Kept] The font-restart sentence stays; 09-05 owns its removal**

Task 3 says to delete 09-01's "A font change takes effect the next time the window manager starts" **only if** 09-05 has already landed. It has not (`.planning/phases/09-config-gui-ipc/` has no `09-05-SUMMARY.md`). The sentence is left in place, still true, and **09-05 owns its removal**.

---

**Total deviations:** 4 auto-fixed (1 × Rule 1, 1 × Rule 2, 2 × Rule 3) plus 5 recorded scope/reconciliation adjustments.
**Impact on plan:** No scope creep. Each auto-fix was required for a claim the plan itself makes to be true; the largest two (the `Border` reach and the `ShapeNotify` instrument) are things the plan could not have known without building and mutating.

## Evidence

**Mutation check 1 — the live-apply claim (the plan's named `fails_when` signature).** With `applyConfig()`'s frame re-layout disabled and everything else untouched:

```
165 - set frame-thickness re-frames a window that was already mapped (Failed)
170 - reload re-reads the file and applies it to windows already open (Failed)
88% tests passed, 2 tests failed out of 17
```

Exactly the two live-apply cases, red because the pre-existing window's geometry did not change — the signature of a `set` that is stored but never applied. Every other case, including `get`, `status`, the refusals and the key-list agreement, stayed green. Restored, 17/17.

**Mutation check 2 — the diff that makes idempotency true.** With `applyConfig()`'s `next.frameThickness != previousThickness` test replaced by `true ||`:

```
tests/test_wm_config_live.cpp:604: FAILED:
  CHECK( reconfigurations == 0 )
  re-shapes after the second set: 4
```

One case red, and red on the count rather than on the geometry. Green again on revert. (The first form of this case, counting `ConfigureNotify`, stayed green under the same mutation — see deviation 3.)

**Mutation check 3 — the reload layer order.** Passing `argc = 0` to `Config::load()` in `reloadConfigFromDisk()`, so the CLI layer is dropped:

```
163 - a command-line override still wins after a reload (Failed)
94% tests passed, 1 tests failed out of 17
```

**Mutation check 4 — the unreadable-file guard.** Replacing the `access(F_OK) && !access(R_OK)` test with `false`:

```
169 - a reload that cannot read the user file changes nothing and names it (Failed)
94% tests passed, 1 tests failed out of 17
```

Each guard reddens exactly the case that names it, and no other.

**Task 1 acceptance criteria, run literally.**

| Criterion | Result |
|---|---|
| `build/debug/wm2-ctl` exists | yes |
| `ldd build/debug/wm2-ctl \| grep -c -e libX11 -e libXft -e libgtk -e libglib` | **0** |
| same, ASan tree (`build/asan/wm2-ctl`) | **0** |
| `grep -cE '#include *[<"](X11\|gtk\|glib\|gdk)' apps/wm2-ctl/main.cpp` | 0 |
| `wm2-ctl status` with no window manager running | exit **2** |
| `grep -c 'applyConfig' src/Manager.cpp` (needs ≥ 2) | **6** |
| `ctest -L '^wm_geometry$'` | 13/13 |

Full `ldd` output for `wm2-ctl`, verbatim:

```
	linux-vdso.so.1 (0x00007ffeefd1f000)
	libstdc++.so.6 => /lib/x86_64-linux-gnu/libstdc++.so.6 (0x00007f9d64800000)
	libgcc_s.so.1 => /lib/x86_64-linux-gnu/libgcc_s.so.1 (0x00007f9d64a96000)
	libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007f9d64400000)
	libm.so.6 => /lib/x86_64-linux-gnu/libm.so.6 (0x00007f9d64719000)
	/lib64/ld-linux-x86-64.so.2 (0x00007f9d64afb000)
```

**Task 2 acceptance criteria.** `grep -c 'HOME' tests/test_wm_config_live.cpp` = **4** (every fixture overrides `XDG_CONFIG_HOME` and `XDG_CONFIG_DIRS`; no case reads the developer's real configuration). `bash scripts/gates/build-all.sh debug` green.

**Task 3 acceptance criteria.** `grep -c -e 'wm2-ctl' -e '_WM2_CONFIG_SOCKET' docs/RELEASE-NOTES.md` = **18** (needs ≥ 2). The four-subcommand loop prints nothing and exits 0. `build/debug/wm2-ctl --help | grep -c -e status -e get -e set -e reload` = **7** (needs ≥ 4). All four exit codes, the socket path and its `/tmp/wm2-born-again-<uid>/` fallback, the no-per-window-information statement and the `SIGHUP` sentence are each present.

**Gates.**

| Gate | Result |
|---|---|
| `ctest -L '^wm_config_live$'` | 16/16 |
| `ctest -L '^(wm_socket\|wm_geometry)$'` | 31/31 |
| `ctest -L '^(config\|config_writer\|rules\|xdg)$'` | green |
| `bash scripts/gates/build-all.sh debug` | **453/453, OK** (baseline 437 + 16) |
| `bash scripts/gates/build-all.sh asan` | **OK, no sanitizer findings** (2 matched libfontconfig suppressions, pre-existing) |

## Issues Encountered

- **`ConfigureNotify` is not an observable for redundant reconfiguration.** Recorded as deviation 3 and worth carrying forward as a general lesson: any future case asserting "the window manager did no work" must not rest on `ConfigureNotify`, because the server suppresses it when nothing changes. `ShapeNotify` is the instrument that sees the request.
- **The unreadable-file reload case cannot be constructed as root**, which is why it returns early with a `WARN` when `geteuid() == 0` rather than asserting something false. Recorded in the broken-windows ledger (row 17) alongside the identically shaped row 16 that 09-01 left.

## Known Stubs

None. Two things a reader might mistake for stubs, both deliberate and both named in the release notes:

- **`applyConfig()` applies only `frame-thickness` live.** Every other field is stored, reported by `get` and used by anything constructed afterwards; 09-05 adds the remaining branches to this one function, which is what DISC-06a exists to make possible. The release notes state which settings apply live, satisfying the plan's prohibition that no restart-only setting go unnamed.
- **`m_savedConfig` / `savedConfig()` is written but not yet read by any message.** It is live state, not placeholder data: it is populated at construction and replaced by every successful reload. Its consumer is 09-08's revert.

## Threat Flags

None. Every trust boundary this plan opens was in the plan's `<threat_model>`.

- **T-9-20** (a `set` carrying an out-of-range or nonsense value) — mitigated and asserted for both bounds, for a non-numeric value, for an unrecognised boolean spelling and for an unknown key; the read-back check makes it fail closed even if validation and the parser later diverge.
- **T-9-21** (maximum thickness with many windows) — asserted: 50 applied with four windows mapped, and the window manager still frames a fifth afterwards.
- **T-9-22** (repeated `set` causing repeated re-layout) — mitigated by the diff and asserted by the mutation-checked idempotency case.
- **T-9-23** (`reload` reading a replaced file) — accepted, as planned.
- **T-9-24** (`wm2-ctl` from another account) — unchanged: `wm2-ctl` has no privilege of its own and the boundary is 09-03's peer-uid check, which still refuses before any message is read.
- **T-9-25** (a `set` leaving no trace) — accepted, as planned; the release notes now say plainly that a `set` writes nothing.

One note for a later audit, recorded because it is new surface rather than because it is a defect: `wm2-ctl` forwards the key and value **exactly as typed** and validates nothing on the window manager's behalf, by design — a second opinion in the client could only ever disagree with the first. The one exception is framing: a key or value containing a newline is refused locally, because it would end the frame early and turn the tail into a message the window manager never agreed to receive.

## User Setup Required

None. `wm2-ctl` is built by the ordinary build and needs no configuration; the socket is created automatically at startup.

## Next Phase Readiness

Ready for **09-05 (live apply for the remaining settings)**, which is now a matter of adding branches to `WindowManager::applyConfig()` and nothing else:

- The value already arrives validated and already sits in `m_config` by the time any branch runs — `applyConfig()` stores `next` whole before it diffs, so a new branch writes an *application*, never an assignment.
- `configKeySpecFor()` gives kind and range for every key; `configValueForKey()` gives the read-back.
- The font keys are the interesting ones: `Border::m_tabFont` and the menu font are static, loaded once behind `m_staticsInitialised`, so applying them live means a reload ladder rather than a re-layout. 09-05 also owns deleting the release-note sentence that says a font change waits for a restart.
- `ConfigSocketServer::broadcast()` still exists and is still unused; it remains the hook for a settings-changed notification (D-08).

Ready for **09-07 (protocol client)** and **09-08 (GUI)**: `savedConfig()` is the revert source DISC-07 promised, and `wm2-ctl` is a working reference for the handshake-then-request sequence.

No blockers.

---
*Phase: 09-config-gui-ipc*
*Completed: 2026-09-06*

## Self-Check: PASSED

Both created artifacts (`apps/wm2-ctl/main.cpp`, `tests/test_wm_config_live.cpp`) exist on disk, and all three commits (`e164d66`, `9ee5f2b`, `023a592`) are present in the repository history. Every acceptance criterion of all three tasks was executed and its literal output recorded above; both gates (`debug` 453/453, `asan` no findings) are green.
