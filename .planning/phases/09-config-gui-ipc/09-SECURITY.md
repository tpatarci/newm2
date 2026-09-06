---
phase: "09"
slug: "config-gui-ipc"
status: secured
# threats_open = count of OPEN threats at or above workflow.security_block_on severity (the blocking gate)
threats_open: 0
asvs_level: 1
created: "2026-09-06"
---

# Phase 09 — Security

> Per-phase security contract: threat register, accepted risks, and audit trail.

Written at phase close, before the pull request. The register is the union of the `<threat_model>`
blocks of the nine plans (09-01 to 09-09): 57 rows, 28 high, 22 medium, 7 low; 52 disposed
`mitigate`, 5 `accept`. Verification was done by a read-only auditor (opus, 74 tool calls) after
two code-review passes and their 37 fix commits had landed, and returned `OPEN_THREATS` with one
high row open: T-9-34, the client half of the handshake, which the register described and no
client implemented. The lead closed it at `b68c207` with a case shown failing first; the commands
are in the audit trail. Threat IDs are as the plans number them; `T-9-SC` is 09-06's supply-chain
row.

---

## Trust Boundaries

| Boundary | Description | Data Crossing | Control |
|----------|-------------|---------------|---------|
| X client → window manager | Titles, class hints, properties reach the matcher, tab renderer and EWMH publisher | Arbitrary bytes, server-allocated | Bounded on read (carried from 08.5); the `status` reply gathers no per-window datum at all (`src/Manager.cpp:1827-1851`) |
| Local socket peer → window manager | Any process that can `connect()` reaches the window manager's single thread | Newline-framed JSON, ≤ 4096 B per line | 0700 directory opened `O_NOFOLLOW` and checked by `fstat`, 0600 socket, `SO_PEERCRED` uid equality before the first read (`src/SocketServer.cpp:295-330, 380, 589-598`); 16 connections, 10 s hello deadline, 64 KiB pending cap, one `recv` per notification, every descriptor non-blocking |
| Config file on disk → `Config::applyFile()` / `configFileWrite()` | User- or packager-authored text parsed before any value is used; the file the GUI replaces | Lines ≤ 4096 B, values ≤ 256 B | Parser bounds (`src/Config.cpp:100, 129`); writer refuses a symlink target, holds a bounded directory lock over the read-modify-write, writes a temp beside the target, `fsync`, `rename` (`src/ConfigFileWriter.cpp:469-497, 622-687`) |
| GUI / CLI client → window manager | `wm2-config` and `wm2-ctl` hold no privilege of their own; they are clients of the boundary above | `hello`, then `get`/`set`/`reload`/`status` | Handshake before any setting: version AND program name both required (`apps/wm2-config/ProtocolClient.cpp`, `apps/wm2-ctl/main.cpp`, T-9-34); refusal puts the GUI in file-only mode with the reason in its banner |
| PATH lookup for the Configure entry | The window manager launches a binary named by an environment variable it did not set | Resolved `wm2-config` path, once at startup | Accepted risk AR-05 (T-9-51); spawn is `execvp`, never a shell, and reaped (`src/Buttons.cpp:740-747`, `src/Manager.cpp:1579-1608`) |
| Gate scripts → filesystem and public repository | Install staging, build logs, screenshots and transcripts enter a public repository | File lists, `ldd` output, window titles, PNGs | Staging under the build tree via `mktemp -d`, absolute build-dir refused (`scripts/gates/install-components.sh:58, 116-118`); evidence scanned for hostname, address, `xsession-errors` and secret patterns before commit; every process killed by a PID the session created |

---

## Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation | Status |
|-----------|----------|-----------|----------|-------------|------------|--------|
| T-9-01 | Denial of Service | unresolvable `tab-font` (09-01) | medium | mitigate | `src/Border.cpp` fallback ladder; `tests/test_wm_runtime.cpp:1388` | closed |
| T-9-02 | Denial of Service | unresolvable `menu-font` (09-01) | medium | mitigate | `tests/test_wm_runtime.cpp:1656`, `:2032` | closed |
| T-9-03 | Tampering | oversized / control-character font pattern (09-01) | low | mitigate | `src/Config.cpp:100-101` (line), `:129-130` (value); the two keys are plain strings at `:164-165` | closed |
| T-9-04 | Information Disclosure | font pattern holding a filesystem path (09-01) | low | accept | Accepted risk AR-01 | closed |
| T-9-05 | Denial of Service | oversized line to the decoder (09-02) | high | mitigate | `include/ConfigProtocol.h:543` bound before the newline search; `tests/test_config_protocol.cpp:261, 291` | closed |
| T-9-06 | Denial of Service | nested / escaped input (09-02) | high | mitigate | one-level grammar, no recursion, `include/ConfigProtocol.h:536-664`; `tests/test_config_protocol.cpp:356`; asan tree green | closed |
| T-9-07 | Tampering | writer corrupting the config file (09-02) | high | mitigate | `src/ConfigFileWriter.cpp:622-687` temp beside target → `fsync` → `rename`; `tests/test_config_writer.cpp:551` | closed |
| T-9-08 | Tampering | writer writing outside its target (09-02) | high | mitigate | only two paths constructed (`ConfigFileWriter.cpp:622, 683`); `tests/test_config_writer.cpp:525`; the GUI writes `userFilePath` only (`apps/wm2-config/main.cpp:465`) | closed |
| T-9-09 | Information Disclosure | error text echoing a path (09-02) | low | accept | Accepted risk AR-02 | closed |
| T-9-10 | Repudiation | partially applied save (09-02) | medium | mitigate | outcome binary by `rename()` (`ConfigFileWriter.cpp:683`); `tests/test_config_writer.cpp:551` | closed |
| T-9-11 | Spoofing | foreign uid connecting (09-03) | high | mitigate | dir 0700 `src/SocketServer.cpp:295-330`, socket 0600 `:380`, peer uid before first read `:589-598` via `:179-187`; `tests/test_config_socket.cpp:282`, `tests/test_wm_socket.cpp:891, 929` | closed |
| T-9-12 | Elevation of Privilege | root pushing config (09-03) | high | mitigate | uid equality, not a capability test (`src/SocketServer.cpp:186`); `tests/test_config_socket.cpp:282` | closed |
| T-9-13 | Information Disclosure | window title / class / geometry leaking (09-03) | high | mitigate | `src/Manager.cpp:1827-1851` seven fields, no per-window datum; `tests/test_wm_socket.cpp:1255, 1515` | closed |
| T-9-14 | Denial of Service | oversized / newline-free message (09-03) | high | mitigate | `src/SocketServer.cpp:650-655, 677-680, 722-726`; `tests/test_wm_socket.cpp:1009, 1054` with liveness checks | closed |
| T-9-15 | Denial of Service | blocking read stalling the one thread (09-03) | high | mitigate | all descriptors non-blocking `src/SocketServer.cpp:99-106, 582`; one `recv` per notification `:636`; poll precedes every read at both sites `src/Events.cpp:279, 287, 397, 409`; `tests/test_wm_socket.cpp:612` | closed |
| T-9-16 | Denial of Service | connection exhaustion (09-03) | medium | mitigate | cap 16 (`include/SocketServer.h:224`, `src/SocketServer.cpp:600-604`), oldest silent dropped first `:791-802`, 10 s deadline `:777-788`; `tests/test_wm_socket.cpp:1054` | closed |
| T-9-17 | Tampering | stale / pre-placed socket path (09-03) | high | mitigate | CR-03 fix `b902245`: `O_NOFOLLOW\|O_DIRECTORY` `src/SocketServer.cpp:302-303`, `fstat` owner and type `:311-319`, `fchmod` through the fd `:323`; stale verdict `lstat`+`S_ISSOCK` `:193-199`, bounded probe `:210-239`; `tests/test_config_socket.cpp:334, 341, 372, 894, 989, 1049, 1237` | closed |
| T-9-18 | Denial of Service | over-long socket path (09-03) | medium | mitigate | `src/SocketServer.cpp:157-161, 262-272`; `tests/test_config_socket.cpp:255`, `tests/test_wm_socket.cpp:1099` | closed |
| T-9-19 | Repudiation | refused connection leaving no trace (09-03) | low | mitigate | once per uid with a saturation bound `src/SocketServer.cpp:831-889`; `tests/test_config_socket.cpp:1168, 1193` | closed |
| T-9-20 | Tampering | out-of-range / nonsense `set` (09-04) | high | mitigate | validate → apply to a copy → fail-closed read-back `src/Manager.cpp:2318-2371`; unknown key `:2308-2316`; `tests/test_wm_config_live.cpp:1158, 1210, 1713` | closed |
| T-9-21 | Denial of Service | maximum thickness with many windows (09-04) | medium | mitigate | clamp 1..50; single walk, no reparent `src/Manager.cpp:2195-2205`; `tests/test_wm_config_live.cpp:1240` | closed |
| T-9-22 | Denial of Service | repeated `set` re-layout (09-04) | medium | mitigate | diff before act `src/Manager.cpp:2195, 2219, 2231, 2244`; `tests/test_wm_config_live.cpp:1090, 1806, 2877` | closed |
| T-9-23 | Tampering | `reload` reading a replaced file (09-04) | medium | accept | Accepted risk AR-03 | closed |
| T-9-24 | Elevation of Privilege | `wm2-ctl` from another account (09-04) | high | mitigate | no privilege of its own; the boundary is T-9-11's check (`src/SocketServer.cpp:589-598`) | closed |
| T-9-25 | Repudiation | `set` with no trace (09-04) | low | accept | Accepted risk AR-04 | closed |
| T-9-26 | Denial of Service | colour / font failing after the old was released (09-05) | high | mitigate | allocate-then-swap `src/Manager.cpp:1926-1946`; both faces opened before either installs (CR-04 fix `70e8bbf`) `:2150-2180`; palette pre-flight `:2087-2138`; `tests/test_wm_config_live.cpp:1754, 1915, 2923` | closed |
| T-9-27 | Denial of Service | Xft face leak on repeated reload (09-05) | high | mitigate | one close per swap `src/Manager.cpp:2003-2004`; diff prevents no-change work `:2147-2148`; asan gate green (`evidence/remote-desktop/gates/`) | closed |
| T-9-28 | Tampering | menu entry introducing a command (09-05) | medium | mitigate | whitespace tokenisation, never a shell (`src/Config.cpp:232`); `execvp` spawn `src/Manager.cpp:1579-1608, 1665`; `tests/test_wm_runtime.cpp:1835` | closed |
| T-9-29 | Elevation of Privilege | `exec-using-shell` over the socket (09-05) | medium | mitigate | shell reached only through the pre-existing flag `src/Manager.cpp:1630, 1647`; `tests/test_wm_config_live.cpp:2425`, `tests/test_wm_runtime.cpp:1835` | closed |
| T-9-30 | Denial of Service | broadcast blocking on a non-reading client (09-05) | high | mitigate | non-blocking send, drop on overflow `src/SocketServer.cpp:722-726, 734-751` (`MSG_NOSIGNAL`); `tests/test_wm_config_live.cpp:2803` | closed |
| T-9-31 | Information Disclosure | reload notice carrying values (09-05) | low | mitigate | `Reloaded` encodes no members `include/ConfigProtocol.h:381-387`; `src/Manager.cpp:2440-2443`; `tests/test_wm_config_live.cpp:2738` | closed |
| T-9-32 | Denial of Service | mutating the category list under a modal loop (09-05) | high | mitigate | deferred while a menu is mapped `src/Manager.cpp:2244-2255`; `tests/test_wm_config_live.cpp:2633` (its mutation gap is ledger row 18) | closed |
| T-9-33 | Tampering | GTK linkage leaking into the WM binary (09-06) | high | mitigate | probe only inside the resolved branches `CMakeLists.txt:130-145`; the only `PkgConfig::GTK3` reference is `:179`; `ldd` evidence in `09-06-SUMMARY.md`; manifest invariant `scripts/gates/install-components.sh:180-211` | closed |
| T-9-34 | Spoofing | `wm2-config` connecting to a socket some other process owns (09-06) | high | mitigate | **Open at audit**: neither client read the inbound `program` member. Closed at `b68c207`: `kConfigProtocolWindowManagerProgram` required by `apps/wm2-config/ProtocolClient.cpp` (after the version check) and `apps/wm2-ctl/main.cpp` before Connected / exit 0; `tests/test_wm2_config_smoke.cpp` "a peer that speaks our version but is not the window manager is refused by name" (`FakeServer::Reply::WrongProgram`), shown failing first | closed |
| T-9-35 | Tampering | GUI corrupting / truncating the config file (09-06) | high | mitigate | through the atomic writer (`apps/wm2-config/main.cpp:465`); `tests/test_wm2_config_smoke.cpp:1008, 1087` | closed |
| T-9-36 | Information Disclosure | screenshots carrying host identity (09-06) | medium | mitigate | private Xvfb with `HOME`/`XDG_CONFIG_HOME` in the build tree (`09-06-SUMMARY.md:270`); images re-inspected by the auditor | closed |
| T-9-37 | Denial of Service | a11y bridge blocking with no session bus (09-06) | medium | mitigate | assertion on the window mapping, never on clean stderr `tests/test_wm2_config_smoke.cpp:1192-1222` | closed |
| T-9-38 | Elevation of Privilege | GUI writing outside the user's config (09-06) | high | mitigate | sole writable path `apps/wm2-config/FormState.cpp:50` → `main.cpp:465`; `tests/test_wm2_config_smoke.cpp:1008, 1426` | closed |
| T-9-SC | Tampering | GTK3 build dependency (09-06) | high | mitigate | `09-RESEARCH.md` Package Legitimacy Audit: official signed Ubuntu archive package, installed by the operator, never by a script | closed |
| T-9-39 | Repudiation | live change never saved, file and screen divergent (09-07) | medium | mitigate | unsaved state tracked `apps/wm2-config/FormState.cpp:206, 217, 227-231`; close prompt `tests/test_wm2_config_smoke.cpp:2112, 2144`; `evidence/wm2-config/close-prompt.png` | closed |
| T-9-40 | Tampering | shell metacharacter silently evaluated (09-07) | high | mitigate | parser's whitespace tokenisation, dialog shows the argv (`apps/wm2-config/MenuModel.h`); `tests/test_wm2_config_smoke.cpp:1753, 1778`; `evidence/wm2-config/menu-entry-dialog.png` | closed |
| T-9-41 | Elevation of Privilege | shell flag turned on unwittingly (09-07) | medium | mitigate | label states the consequence ("Run that command through a shell (/bin/sh -c)"), unchecked by default; `tests/test_wm2_config_smoke.cpp:1544` | closed |
| T-9-42 | Tampering | reload notice overwriting an unsaved edit (09-07) | high | mitigate | dirty field kept and marked, never replaced `apps/wm2-config/FormState.cpp:113-139`; `tests/test_wm2_config_smoke.cpp:2228, 2278, 2333` | closed |
| T-9-43 | Repudiation | closing leaves the desktop matching no file (09-07) | high | mitigate | three-way prompt and active revert `tests/test_wm2_config_smoke.cpp:2144, 2157, 2201` | closed |
| T-9-44 | Denial of Service | oversized menu-entry list on one line (09-07) | medium | mitigate | measured on the encoded line, refused before sending `apps/wm2-config/MenuModel.h:233-249`; `tests/test_wm2_config_smoke.cpp:1848` | closed |
| T-9-45 | Information Disclosure | screenshots carrying host identity (09-07) | medium | mitigate | evidence tree hostname-free (auditor grep and visual inspection); 09-07's SUMMARY counts the files but does not state the scan, an attestation gap only | closed |
| T-9-46 | Tampering | GTK dependency in the `wm` manifest (09-08) | high | mitigate | per-executable `ldd` gate `scripts/gates/install-components.sh:180-211` with a zero-executables guard; shown failing in `09-08-SUMMARY.md` | closed |
| T-9-47 | Tampering | desktop entry with an unintended command (09-08) | medium | mitigate | `packaging/wm2-config.desktop:6-7`, `packaging/wm2-born-again.desktop:5-6`; both directions asserted (`09-08-SUMMARY.md`) | closed |
| T-9-48 | Denial of Service | gate writing to a fixed temp path (09-08) | medium | mitigate | `mktemp -d` under the build tree, absolute build-dir refused, pattern-guarded cleanup `scripts/gates/install-components.sh:58, 116-122` | closed |
| T-9-49 | Repudiation | GUI-disabled build certified by fewer tests (09-08) | high | mitigate | registered-vs-passed accounting and verbose re-run of every skip `scripts/gates/build-all.sh:280-380`; `evidence/remote-desktop/gates/build-all-nogtk.log` | closed |
| T-9-50 | Information Disclosure | gate logs carrying absolute paths (09-08) | low | mitigate | logs under the build tree; the two `/home/<user>/` build-tree matches in committed evidence are within the plan's own wording and reported, not doctored | closed |
| T-9-51 | Elevation of Privilege | hostile `wm2-config` earlier on PATH (09-09) | medium | accept | Accepted risk AR-05 | closed |
| T-9-52 | Denial of Service | Configure entry leaving a zombie (09-09) | medium | mitigate | same spawn path as New (`src/Buttons.cpp:740-747` → `src/Manager.cpp:1579-1608`); `tests/test_wm_runtime.cpp:5118-5187` zero zombies | closed |
| T-9-53 | Information Disclosure | committed evidence carrying host identity (09-09) | high | mitigate | `scripts/capture-display-capabilities.sh:143` no longer emits `uname -n`; scan documented in `evidence/remote-desktop/README.md`; older bundles scrubbed at `823a253` (ledger row 28) | closed |
| T-9-54 | Repudiation | resource claim with no measurement (09-09) | high | mitigate | measured TigerVNC run at `8f4275b` in `evidence/remote-desktop/README.md` (98.7 MB first launch); budget case `tests/test_wm2_config_smoke.cpp:2489` | closed |
| T-9-55 | Tampering | docs drifting from the accepted key set (09-09) | medium | mitigate | `scripts/gates/doc-keys.sh:265-300` both directions; shown failing both ways in `evidence/remote-desktop/gates/` | closed |
| T-9-56 | Denial of Service | capture process left running (09-09) | medium | mitigate | transcript kills by PID only (`evidence/remote-desktop/session-transcript.txt:32-35`); repo-wide grep for `pkill`/`killall`/`pgrep` finds only the prohibiting comment in `scripts/diag/wm-stack-capture.sh:27` | closed |

---

## Accepted Risks Log

| Risk ID | Threat Ref | Rationale | Accepted By | Date |
|---------|------------|-----------|-------------|------|
| AR-01 | T-9-04 | A font pattern that names a file path is read by the same user who wrote it; no privilege boundary is crossed, and the value is bounded at 256 bytes | plan 09-01, delegated lead | 2026-09-06 |
| AR-02 | T-9-09 | Writer error text names the target path, which is the user's own config file; the evidence tree is scanned for host identifiers before commit | plan 09-02, delegated lead | 2026-09-06 |
| AR-03 | T-9-23 | A `reload` reads whatever file the same uid last wrote; a writer with the user's uid already has every capability the window manager has | plan 09-04, delegated lead | 2026-09-06 |
| AR-04 | T-9-25 | A `set` writes nothing to disk by design (D-01); the release notes state it, and the GUI's Save is the only path to persistence | plan 09-04, delegated lead | 2026-09-06 |
| AR-05 | T-9-51 | The Configure entry resolves `wm2-config` on the PATH the user's session gave the window manager, the same trust basis as `TryExec` and the New-window command; the spawn is `execvp`, never a shell | plan 09-09, delegated lead | 2026-09-06 |

*Accepted risks do not resurface in future audit runs.*

---

## Observed, not in the register

Recorded for the next threat model, not graded here: the environment levers `WM2_SOCKET_FORCE_FOREIGN` and `WM2_FORCE_{TAB,MENU}_FONT_RELOAD_FAILURE` are read by the shipping binary (each only makes behaviour stricter, and the socket one announces itself on stderr); `--socket <path>` on both clients is the reachability path that made T-9-34 matter; `SO_PEERCRED` is captured at `connect()` time; four SUMMARYs (09-06 to 09-09) carry no Threat Flags section; `configSocketDirectory()` accepts any absolute `XDG_RUNTIME_DIR` (defended by the `O_NOFOLLOW`/`fstat`/0700 sequence); `std::bad_alloc` on the socket path is defended by the two RAII guards but has no row.

---

## Security Audit Trail

| Audit Date | Threats Total | Closed | Open | Run By |
|------------|---------------|--------|------|--------|
| 2026-09-06 | 57 | 56 | 1 (T-9-34) | gsd-security-auditor (opus, read-only, OPEN_THREATS) after review passes 1 and 2 |
| 2026-09-06 | 57 | 57 | 0 | lead: T-9-34 closed at `b68c207`, red first; re-read below |

Lead re-read at `b68c207`: `grep -n '\.program' apps/wm2-config/ProtocolClient.cpp apps/wm2-ctl/main.cpp` → one outbound assignment and one inbound comparison in each; `grep -n 'kConfigProtocolWindowManagerProgram' include/ConfigProtocol.h src/Manager.cpp` → the definition and the server's `ack.program`; the new case run against the pre-fix client: `test cases: 1 | 1 failed`, `assertions: 5 | 1 passed | 4 failed`, empty reason; against the fixed client: `All tests passed (5 assertions in 1 test case)`; labels `wm2_config_smoke|config_protocol|wm_config_live` → 131/131; `bash scripts/gates/doc-keys.sh` → OK at 36 keys after the notes gained the two-way handshake sentence.

---

## Sign-Off

- [x] All threats have a disposition (mitigate / accept / transfer)
- [x] Accepted risks documented in Accepted Risks Log
- [x] Every mitigated threat cites the code and, where the plan promised one, the test
- [x] threats_open: 0 at or above the block threshold (high)
