---
phase: 09-config-gui-ipc
verified: 2026-09-06T19:57:48Z
status: human_needed
score: 58/60 must-haves verified
covered_files:
  - ".planning/REQUIREMENTS.md"
  - ".planning/WINDOWS.md"
  - ".planning/phases/09-config-gui-ipc/09-01-PLAN.md"
  - ".planning/phases/09-config-gui-ipc/09-01-SUMMARY.md"
  - ".planning/phases/09-config-gui-ipc/09-02-PLAN.md"
  - ".planning/phases/09-config-gui-ipc/09-02-SUMMARY.md"
  - ".planning/phases/09-config-gui-ipc/09-03-PLAN.md"
  - ".planning/phases/09-config-gui-ipc/09-03-SUMMARY.md"
  - ".planning/phases/09-config-gui-ipc/09-04-PLAN.md"
  - ".planning/phases/09-config-gui-ipc/09-04-SUMMARY.md"
  - ".planning/phases/09-config-gui-ipc/09-05-PLAN.md"
  - ".planning/phases/09-config-gui-ipc/09-05-SUMMARY.md"
  - ".planning/phases/09-config-gui-ipc/09-06-PLAN.md"
  - ".planning/phases/09-config-gui-ipc/09-06-SUMMARY.md"
  - ".planning/phases/09-config-gui-ipc/09-07-PLAN.md"
  - ".planning/phases/09-config-gui-ipc/09-07-SUMMARY.md"
  - ".planning/phases/09-config-gui-ipc/09-08-PLAN.md"
  - ".planning/phases/09-config-gui-ipc/09-08-SUMMARY.md"
  - ".planning/phases/09-config-gui-ipc/09-09-PLAN.md"
  - ".planning/phases/09-config-gui-ipc/09-09-SUMMARY.md"
  - ".planning/phases/09-config-gui-ipc/09-CONTEXT.md"
  - ".planning/phases/09-config-gui-ipc/09-RESEARCH.md"
  - ".planning/phases/09-config-gui-ipc/09-REVIEW-PASS2.md"
  - ".planning/phases/09-config-gui-ipc/09-REVIEW.md"
  - "CMakeLists.txt"
  - "COMPILED_CODE_BEHAVIOR_CHECKLIST.md"
  - "apps/wm2-config/AppearancePage.cpp"
  - "apps/wm2-config/BehaviourPage.cpp"
  - "apps/wm2-config/ConnectionState.h"
  - "apps/wm2-config/FormState.cpp"
  - "apps/wm2-config/MenuModel.h"
  - "apps/wm2-config/MenuPage.cpp"
  - "apps/wm2-config/ProtocolClient.cpp"
  - "apps/wm2-config/main.cpp"
  - "apps/wm2-ctl/main.cpp"
  - "docs/RELEASE-NOTES.md"
  - "include/Border.h"
  - "include/Config.h"
  - "include/ConfigFileWriter.h"
  - "include/ConfigProtocol.h"
  - "include/DesktopEntry.h"
  - "include/Manager.h"
  - "include/SocketServer.h"
  - "packaging/wm2-born-again.desktop"
  - "packaging/wm2-config.desktop"
  - "scripts/gates/build-all.sh"
  - "scripts/gates/doc-keys.sh"
  - "scripts/gates/install-components.sh"
  - "scripts/preflight.sh"
  - "src/Border.cpp"
  - "src/Buttons.cpp"
  - "src/Client.cpp"
  - "src/Config.cpp"
  - "src/ConfigFileWriter.cpp"
  - "src/DesktopEntry.cpp"
  - "src/Events.cpp"
  - "src/Manager.cpp"
  - "src/SocketServer.cpp"
  - "tests/test_config.cpp"
  - "tests/test_config_protocol.cpp"
  - "tests/test_config_socket.cpp"
  - "tests/test_config_writer.cpp"
  - "tests/test_desktopentry.cpp"
  - "tests/test_wm2_config_smoke.cpp"
  - "tests/test_wm_config_live.cpp"
  - "tests/test_wm_runtime.cpp"
  - "tests/test_wm_socket.cpp"
covered_digest: "v1:sha256:926679d453f2e1b7e9714b92123b4b33c283282e23682803c146f28cba02000b"
behavior_unverified: 2
overrides_applied: 0
behavior_unverified_items:
  - truth: "09-04 T7 — Two `set` messages arriving together are applied in arrival order, in the event loop's own thread"
    test: "Issue two overlapping `set` messages on two connections (e.g. `set frame-thickness 11` on conn A and `set frame-thickness 21` on conn B, written back-to-back before either reply is read) and assert which value the window manager ends on, and that it matches the order the writes hit the socket."
    expected: "The later-arriving `set` wins; neither is dropped; `wm2-ctl get frame-thickness` reports the second value."
    why_human: "No case in the suite issues two overlapping sets. 09-04-SUMMARY.md D7 records this itself as `human_judgment: true` — the claim rests on a structural argument (one thread, one frame at a time) rather than on an observation. Presence checks see only that there is no second thread; they cannot see the ordering the invariant asserts."
  - truth: "09-07 T1 — every one of the nine Behaviour settings changes the running window manager the moment it is committed"
    test: "For each of the eight settings other than click-to-focus (raise-on-focus, auto-raise, focus-stealing-prevention, auto-raise-delay, pointer-stopped-delay, destroy-window-delay, new-window-command, exec-using-shell), commit the value through the GUI's own BehaviourPage/FormState/ProtocolClient path and then observe the resulting DESKTOP behaviour change in the same case."
    expected: "The behaviour observed after a GUI-driven commit is the same behaviour the equivalent `[wm_config_live]` case observes after a `wm2-ctl set`."
    why_human: ".planning/WINDOWS.md row 20 (status `open`) records exactly this: eight of nine settings are proven here only as far as the window manager ADOPTING the value ([wm2_config_smoke] #227); that adopting it changes an observable behaviour is owned separately by the [wm_config_live] cases from 09-05. Only click-to-focus has both halves in one case (#213, the tracer). The join is an inference for the other eight."
human_verification:
  - test: "09-06 Appearance-page review — look at the screenshots under `.planning/phases/09-config-gui-ipc/evidence/wm2-config/` (appearance-page.png, appearance-page-default-size.png, appearance-page-font-chooser-open.png) and answer: (1) is every control's purpose obvious to someone who has never edited the config file? (2) is the config-file spelling beside each chooser readable and clearly secondary to it? (3) is the connection state in the header noticeable without being loud? (4) does the reset affordance read as \"put this back\" rather than as a delete?"
    expected: "`approved`, or a described change."
    why_human: "Visual judgement. The plan's own `<human-check>` block in 09-06-PLAN.md. No automated case reads legibility, visual weight or affordance semantics."
  - test: "09-07 whole-tool review — look at the five screenshots under `.planning/phases/09-config-gui-ipc/evidence/wm2-config/` and answer: (1) three pages — is anything on the wrong page or missing from all of them? (2) Behaviour page — would a non-programmer understand each setting from the label alone? (3) Menu page — is Add/Edit/Remove obvious and does a row read clearly? (4) close prompt — are Save, Discard and Cancel unambiguous about what Discard does to the running desktop? (5) does this look like one tool or three pages built separately?"
    expected: "`approved`, or a described change."
    why_human: "Visual and copy judgement. The plan's own `<human-check>` block in 09-07-PLAN.md."
  - test: "09-09 remote-desktop pass — open `wm2-config` in a real remote-desktop session with a viewer ATTACHED and answer: (1) does it feel usable over the link, or laggy enough that a user would give up? (2) do live changes appear on the desktop as fast as they do locally? (3) read the recorded resident-memory figure (98.7 MB first instance, 49.4 MB later ones) — is that acceptable against the 512 MB VPS constraint, or does it change what this project should claim? (4) confirm the evidence bundle carries no hostname, no address and nothing from a session error log."
    expected: "`approved`, or a described change."
    why_human: "Questions 1 and 2 are perceptual and were NOT answerable from the committed capture: `evidence/remote-desktop/README.md:79` records \"No viewer was attached; the pointer input below is XTEST\". Question 3 is a product decision. Question 4 the verifier re-ran mechanically and found clean, but the plan asks for the operator's own confirmation."
  - test: "09-08 T5 / re-run of the four build gates at HEAD (`e32542a`) — `bash scripts/gates/build-all.sh debug|asan|release|nogtk`."
    expected: "All four green at 572 tests; nogtk skips exactly six cases, each with a stated reason; release link audit OK."
    why_human: "The gates were last run in full at `557550a` (recorded in 09-REVIEW-PASS2.md). HEAD is `e32542a`, two commits later, both documentation-only — but this verification deliberately did not run the full suite (host under memory pressure), so the four-tree claim at HEAD rests on the docs-only nature of the delta rather than on a run."
advisory: []
---

# Phase 9: Config GUI + IPC — Verification Report

**Phase Goal:** Non-programmer users can configure the WM visually through a separate GTK3 application that communicates with the running WM via IPC.
**Verified:** 2026-09-06T19:57:48Z
**HEAD:** `e32542a` — *docs(09-review2): dispositions, gate results and the two recorded residuals*
**Status:** human_needed
**Re-verification:** No — initial verification.

> **Citation convention.** ctest indices are *filter-dependent* — the same case
> carries a different number under `ctest -N`, `ctest -L <label> -N` and a
> `-R` run. Every `#NNN` below is the index from the **label-scoped listing in
> the build/debug tree** recorded in the Behavioural Spot-Checks table. The
> **test name in italics is the canonical identifier**; where a row gives only a
> number, resolve it through that table's label.

---

## Goal Achievement

### Roadmap Success Criteria

| # | Success criterion | Status | Evidence |
|---|---|---|---|
| SC-1 | Running `wm2-config` opens a GTK3 window where users can edit fonts, colors, focus policy, frame thickness, and menu entries | ✓ VERIFIED | Real binary launched under Xvfb and its window found and confirmed viewable: `tests/test_wm2_config_smoke.cpp:1192` *"wm2-config opens a window and reports itself connected…"* (ran, PASS). Three-page notebook built at `apps/wm2-config/main.cpp:143-175` (Appearance / Behaviour / Menu); bottom bar with Revert + Save at `:205-217`. Page contents partitioned and asserted: #231 *"the Appearance page carries what D-09 assigns to it and nothing else"*, #214 (Behaviour), #224 *"the Appearance and Behaviour pages partition the settable keys between them"*. Fonts + colours + frame thickness on Appearance (`AppearancePage.cpp:239,276,327`), focus policy on Behaviour, menu entries on Menu (#250, #257). |
| SC-2 | The config GUI communicates with the running WM via a Unix domain socket using JSON messages — changes apply immediately without restart where possible | ✓ VERIFIED | `AF_UNIX`/`SOCK_STREAM` server at `src/SocketServer.cpp`; newline-delimited JSON codec `include/ConfigProtocol.h` (26 `[config_protocol]` cases ran, PASS — incl. #426 *"A line that is not an object is rejected as malformed"*, #448 round-trip). End-to-end through the GUI's own client: #254 *"a colour committed through the form state and the protocol client reaches the desktop"*, #213 click-to-focus tracer. 42 `[wm_config_live]` cases ran, PASS, covering all nine colours, both fonts, all nine behaviour settings, frame thickness and menu entries live. |
| SC-3 | The WM runs perfectly without GTK3 installed — the config GUI is an optional separate package | ✓ VERIFIED | `ldd build/{release,debug,asan,nogtk}/wm2-born-again` — no GTK/GLib/GObject/GDK/pango/cairo in any tree (run). `ldd build/release/wm2-ctl` — libstdc++, libgcc, libc, libm only; no X11 either. `grep -rn "dlopen\|gtk\|glib" src/ include/` returns only `BinaryScanner.cpp:21` (a *string literal* used to classify OTHER binaries) and two `glibc` comments. `build/nogtk` (`BUILD_CONFIG_GUI=OFF`) contains `wm2-born-again` + `wm2-ctl` and **no** `wm2-config`. `bash scripts/gates/install-components.sh build/nogtk` (run) → `wm` component exactly 5 files, `config-gui` component empty, exit 0. |
| SC-4 | Changes made in the config GUI are persisted to the config file so they survive WM restarts | ✓ VERIFIED | Round trip through the *real* writer and the *real* parser the WM starts with: `tests/test_wm2_config_smoke.cpp:1911` *"the Menu page's rows survive a save and come back as the same list"* → `configFileWrite(...)` then `Config::applyFile(userFile)`. Every managed key: #488 *"Every managed key survives a write and a read back through Config::applyFile"*. #216 *"a save in file-only mode writes the user file and leaves the system file alone"*. 39 `[config_writer]` cases ran, PASS. |

---

### Observable Truths — plan `must_haves.truths` (60)

#### 09-01 — fonts become configuration (CGUI-03)

| # | Truth | Status | Evidence |
|---|---|---|---|
| 1-1 | `tab-font=<pattern>` changes the tab label font in a running WM, no source edit, no recompile | ✓ VERIFIED | `include/Config.h:87`, parser `src/Config.cpp:164`, consumed at `src/Border.cpp:271` (`config().tabFont` feeds rungs 1 and 3). Test #325 *"tab-font changes the thickness of the tab a real frame is built with"* (ran, PASS). |
| 1-2 | `menu-font=<pattern>` changes the root menu font in a running WM | ✓ VERIFIED | `include/Config.h:88`, `src/Config.cpp:165`, consumed at `src/Manager.cpp:787` (`m_config.menuFont`). Test #342 *"menu-font changes the height of the root menu a real click opens"* (ran, PASS). |
| 1-3 | `--help` lists both keys because usage is generated from the option table `getopt_long` is handed | ✓ VERIFIED | One declaration `kOptionSpecs` at `src/Config.cpp:491`, rows 502-503; usage loop at `:533`; parse at `:661-662`. `build/release/wm2-born-again --help` (run) prints `--tab-font` and `--menu-font` at lines 18-19. |
| 1-4 | With neither key set anywhere, the WM draws exactly the fonts it drew before | ✓ VERIFIED | `git show 2ad3aad -- src/Border.cpp` removes the literal `"Ubuntu,Noto Sans,DejaVu Sans,Sans:bold:size=12"`; `Config.h:87` is that string character-for-character. `git show 8dacac3 -- src/Manager.cpp` removes `"Ubuntu,Noto Sans,DejaVu Sans,Sans:size=12"`; `Config.h:88` is that string. |
| 1-5 | An unresolvable `tab-font` degrades down the four-rung ladder and never terminates the process | ✓ VERIFIED | Rungs 2 and 4 deliberately keep their own literals (`src/Border.cpp:238-312`, comment at `:271`). Test #341 *"An unresolvable tab-font still leaves the window manager framing windows"* (ran, PASS); #324 confirms the only exit is the no-sans-font-at-all menu case, unchanged. |

#### 09-02 — the display-free halves: codec and surgical writer (CGUI-02, CGUI-03)

| # | Truth | Status | Evidence |
|---|---|---|---|
| 2-1 | Every message round-trips through one newline-terminated line with no display/X/GTK/WM | ✓ VERIFIED | #448 *"Every message type round-trips the fields it carries"*, #427 *"Decoding is a pure function of the line it was handed"*, #439 *"Every encoded line ends with exactly one newline and carries no other"*. `test_config_protocol` links no display library (`CMakeLists.txt:1238-1248` audit). 26/26 ran, PASS. |
| 2-2 | A line over the bound is rejected before any parsing and the decoder never grows its buffer | ✓ VERIFIED | #436 *"The length bound is checked before any scan of the content"*; #432 *"A line at exactly the bound decodes and one byte over is rejected as too long"*; #434 *"Decoding never reads past the terminating newline"*. |
| 2-3 | An unknown message type resolves to a named enumerator, never a silent fallthrough | ✓ VERIFIED | #435 *"An object whose type this version does not speak resolves to UnknownType"*; #428, #429, #446. |
| 2-4 | Saving replaces/appends only managed keys; comments, blanks, `rule-*` and unknown keys survive byte-for-byte | ✓ VERIFIED | #475 *"A comment, a blank line, an unknown key and a rule group survive a save byte for byte"*; #508 *"A managed key is replaced in place, keeping its position and its spacing"*; #493, #498, #504. |
| 2-5 | Saving is atomic — temp file beside the target, renamed over it; a mid-way failure leaves the original | ✓ VERIFIED | `src/ConfigFileWriter.cpp:622-689` — `mkstemp` in the target's own directory, `fsync` before `rename` (`:673`, `:683`), `unlink` on any failure. #494 *"A temporary that cannot be created in the target's own directory fails the save and leaves the original untouched"* (ran, PASS on this non-root host; WINDOWS row 16 records the euid==0 SKIP). |
| 2-6 | Removing a key deletes its line rather than writing the built-in default (D-13) | ✓ VERIFIED | #503 *"A removal deletes the line and never writes the built-in default"*; #485, #474. |
| 2-7 | Writing the same edit set twice produces byte-identical output the second time | ✓ VERIFIED | #495 *"Running the same edit set twice produces byte-identical output"*. |

#### 09-03 — the socket the WM answers on (CGUI-02)

| # | Truth | Status | Evidence |
|---|---|---|---|
| 3-1 | A client that connects, hellos and asks status gets a reply while the WM is idle | ✓ VERIFIED | #158 *"An idle window manager answers hello and status"* (ran, PASS). |
| 3-2 | The same round trip succeeds under a modal grab, because both poll sites share one descriptor set | ✓ VERIFIED | #149 *"The socket answers while a modal pointer grab is held"*, plus #146/#147/#150 confirming `modalWait` still returns events, still reports interruption and still times out (ran, PASS). |
| 3-3 | The path is published on the root window as `_WM2_CONFIG_SOCKET`, so a client discovers it rather than reconstructing it | ✓ VERIFIED (narrowed) | `Atoms::wm2_configSocket` interned `src/Manager.cpp:193`, published `:979-982` right after `_NET_SUPPORTING_WM_CHECK`. #159 *"The socket path is published on the root window and is a socket"* (ran, PASS), and the `[wm_socket]` fixture is a genuine discovering client — `Conn c(path)` at `tests/test_wm_socket.cpp:556` connects to the path read off the property. **Narrowing:** no *shipped* client consumes the property — `wm2-ctl` cannot (no X11, documented at `apps/wm2-ctl/main.cpp:36-38`) and `wm2-config` reconstructs from `DISPLAY` (`apps/wm2-config/ProtocolClient.cpp:35-39`). See Warning W-2. |
| 3-4 | A foreign peer uid is closed before any protocol byte and logged once per uid | ✓ VERIFIED | `SO_PEERCRED` at `src/SocketServer.cpp:172`, verdict applied at `:591` before decode. #144 *"A refused peer is closed before a protocol byte and logged once per uid"* (ran, PASS); unit halves #460, #457, #464, #467. |
| 3-5 | Socket directory 0700, socket 0600 | ✓ VERIFIED | `mkdir(…, 0700)` `src/SocketServer.cpp:295`, `fchmod(dirFd, 0700)` `:323` through an `O_NOFOLLOW|O_DIRECTORY` fd (`:279` names the CR-03 rule), `chmod(path, 0600)` `:380`. #154 *"The socket directory is 0700 and the socket is 0600"* (ran, PASS). |
| 3-6 | A socket left by a crashed WM is reclaimed; one a live WM is listening on is not | ✓ VERIFIED | `configSocketStaleVerdict()` `src/SocketServer.cpp:193-223` — never unlinks what it cannot prove dead. #145 *"A socket left by a crashed predecessor is reclaimed"* (ran, PASS); #462, #473, #451, #453. |
| 3-7 | The status reply carries only version, protocol, uptime, geometry and counts — no window identity | ✓ VERIFIED | #151 *"The status counts are real and no window's identity is in the reply"* and #152 *"The status assembly does not reach any per-window identity"* (both ran, PASS). #143 confirms the field set is stable. |
| 3-8 | Two clients at once each get their own replies; one disconnecting mid-message disturbs neither the other nor the WM | ✓ VERIFIED | #157 *"Two clients connected at once each get their own replies"*, #156 *"A client vanishing mid-request costs only its own connection"* (ran, PASS). |
| 3-9 | A client that never sends a hello costs one descriptor and is dropped at a bounded deadline, never blocking the WM's thread | ✓ VERIFIED | #155 *"A client that sends bytes and never a newline is dropped at the bound"* (ran, PASS, 1.58 s); #148 for the over-bound line. |

#### 09-04 — `wm2-ctl` and the first live setting (CGUI-02, CGUI-04)

| # | Truth | Status | Evidence |
|---|---|---|---|
| 4-1 | `wm2-ctl set frame-thickness 15` re-frames every managed window immediately | ✓ VERIFIED | #188 *"set frame-thickness re-frames a window that was already mapped"* (ran, PASS); #192 for the largest thickness with several windows open. |
| 4-2 | `wm2-ctl get <key>` returns the running value, not the disk value | ✓ VERIFIED | #187 *"wm2-ctl get prints the value the window manager is actually using"*; #189 *"a reload discards a set, because a set writes no file"* (both ran, PASS). |
| 4-3 | `status` prints the seven fields; `reload` re-reads the config layers | ✓ VERIFIED | #175 *"wm2-ctl status prints the seven fields the window manager reports"*, #181 *"reload re-reads the file and applies it to windows already open"* (ran, PASS). |
| 4-4 | `wm2-ctl` links no X11, no Xft, no GTK — it runs over SSH with no display | ✓ VERIFIED | `ldd build/release/wm2-ctl` (run) → `libstdc++`, `libgcc_s`, `libc`, `libm`, `ld-linux` only. #178 *"wm2-ctl finds the socket from DISPLAY, and honours --socket"*, #160 *"wm2-ctl exits 2 when there is no window manager to talk to"*. |
| 4-5 | A `set` the parser would reject is refused naming key and reason, and running state is unchanged | ✓ VERIFIED | Validation on a copy then `applyConfig()`; `src/Manager.cpp:2068-2075` validates before storing. #185 *"a value the parser would clamp is refused and changes nothing"*, #165 (bad colour), #183 (malformed menu-entries), #196 (unusable tab-font) — all ran, PASS. |
| 4-6 | The same `set` twice gives the same ack, identical geometry, no second re-frame | ✓ VERIFIED | Diff at `src/Manager.cpp:2150+`; #193 *"applying the same set twice does nothing the second time"*, #166 (colour capture byte-identical), #169 (menu-entries) — ran, PASS. |
| 4-7 | Two `set`s arriving together are applied in arrival order, in the event loop's own thread, and a `set` under a modal grab is applied without waiting for the grab | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | **Two problems.** (a) *Arrival order:* no case issues two overlapping sets — 09-04-SUMMARY.md D7 marks it `human_judgment: true` and rests it on "there is no second thread and no queue to reorder". Structural, unobserved. (b) *Under a grab:* the review fix WR-13 **narrowed** this after the summary was written — `src/Manager.cpp:2049-2065` now **refuses** `tab-font`, `menu-font` and `frame-thickness` while `m_modalDepth != 0`, whole (a reload moving any of them applies none of it). Refusal, not deferral; the retry after the grab succeeds (#172, ran, PASS). The narrowing is honest and documented in `docs/RELEASE-NOTES.md:677-694`, but it is **not recorded in `.planning/WINDOWS.md`** — see Warning W-1. |

#### 09-05 — everything else applies live (CGUI-04)

| # | Truth | Status | Evidence |
|---|---|---|---|
| 5-1 | All nine colour keys change what is on screen the moment they are set — tabs, frames, buttons, borders and the menu repaint | ✓ VERIFIED | Nine keys at `include/Config.h:44-53`. #177 *"every frame colour set over the socket repaints a window already on screen"* covers `tab-foreground`, `tab-background`, `frame-background`, `button-background`, `borders` (seeded and asserted at `tests/test_wm_config_live.cpp:1469-1473`); #197 *"every menu colour set over the socket reaches the next menu opened"* covers the four menu colours; #191 *"the tab bevel is re-derived from the new tab background"*. All ran, PASS. |
| 5-2 | Setting `tab-font` over the socket reloads the Xft face and re-lays out every tab already on screen | ✓ VERIFIED (narrowed) | `Border::reloadTabFont()` `src/Border.cpp:686`, probe-then-commit at `:658-677`. #176 *"setting tab-font re-lays out every tab already on screen"* (ran, PASS). Narrowed by WR-13 only while a modal grab is held (see 4-7). |
| 5-3 | Setting `menu-font` changes the next root menu's row height | ✓ VERIFIED (narrowed) | #199 *"setting menu-font changes the next root menu's row height"* (ran, PASS). Same WR-13 narrowing under a grab. |
| 5-4 | The three focus booleans, focus-stealing prevention, the three delays, the new-window command and the shell flag all take effect at once | ✓ VERIFIED | #168 (click-to-focus), #170 (raise-on-focus), #184 (auto-raise), #201 (focus-stealing-prevention), #186 (auto-raise-delay), #163 (pointer-stopped-delay), #164 (destroy-window-delay), #174 (new-window-command + exec-using-shell) — all ran, PASS. |
| 5-5 | Adding, editing or removing a manual menu entry over the socket changes what the next root menu shows | ✓ VERIFIED | #200 *"a manual menu entry set over the socket appears in the next root menu"*, #195 *"removing every manual entry removes its category from the next menu"* (ran, PASS). |
| 5-6 | When the WM re-reads from disk, every connected client is told | ✓ VERIFIED | #167 *"a reload tells every client that completed a hello, and no stranger"*; #472 *"the client that asked for a reload is not also broadcast to"* (the W-05 fix); #190 *"a client that never reads does not stop the window manager reloading"*. Ran, PASS. |
| 5-7 | A font value fontconfig cannot resolve leaves the previous face in place and reports an error | ✓ VERIFIED | Probe-then-commit `src/Manager.cpp:2143-2153`. #196 *"a tab-font with no usable face is refused and every tab keeps its width"*, #161 *"a reload whose menu-font has no usable face changes neither font"*, #194 the positive control. Ran, PASS. |
| 5-8 | `docs/RELEASE-NOTES.md` names no setting as taking effect only at next start | ✓ VERIFIED | `docs/RELEASE-NOTES.md:657-658` — *"Every setting `wm2-ctl --help` names changes the desktop you are looking at, at the moment you set it, with no window closing and no restart"*. The only "next start" occurrence in the file (`:558`) is the D-03 file-only banner sentence, which is about there being no WM to talk to. |

#### 09-06 — the settings window (CGUI-01, CGUI-03)

| # | Truth | Status | Evidence |
|---|---|---|---|
| 6-1 | `wm2-config` on a GTK3 host opens a window with a three-page notebook, a connection header, and a Save/Revert bottom bar | ✓ VERIFIED | Real binary spawned under Xvfb, window found viewable, state property read as `Connected`: #255 (ran, PASS). Structure: `gtk_notebook_new()` `apps/wm2-config/main.cpp:143`, three `gtk_notebook_append_page` calls at `:153/:163/:173` labelled Appearance / Behaviour / Menu; banner `:117-124`; Revert `:205`, Save `:213`. Visual confirmation is human item 1. |
| 6-2 | With a WM running, choosing a colour on the Appearance page changes the desktop immediately, before anything is saved | ✓ VERIFIED | #254 *"a colour committed through the form state and the protocol client reaches the desktop"* (ran, PASS) — GUI FormState + ProtocolClient, no save. |
| 6-3 | With no socket the window still opens, edits and saving still work, live-apply controls are insensitive, and the banner reads exactly the sentence CONTEXT.md fixes | ✓ VERIFIED (narrowed) | Banner literal has exactly one definition, `apps/wm2-config/ConnectionState.h:33` — *"Not connected to a running wm2-born-again; changes take effect at next start"*, matching 09-CONTEXT.md D-03 verbatim. #248 *"the file-only banner is the sentence the phase decided on"*, #203, #206, #244, #216, #239 (ran, PASS). **Narrowing:** exactly one control goes insensitive — "Re-read files" (`apps/wm2-config/main.cpp:189-195`, `:527`); every setting stays editable, which D-03's own "edits and saves work" requires. Documented in the code at the site. |
| 6-4 | Every colour and font control shows the config-file spelling in an editable field beside the native chooser, and typing is equivalent to choosing | ✓ VERIFIED | `gtk_color_button_new()` `AppearancePage.cpp:239` / `gtk_font_button_new()` `:327` each paired with `gtk_entry_new()` `:276`. Equivalence is structural and exact: `commitRawField()` `:543-580` and `onColourSet()` `:600-612` / `onFontSet()` `:614-638` all funnel to the same `commit(*row, value)`. #241 *"the font value is read through the chooser interface, not the deprecated getter"* (ran, PASS). The GTK entry handler itself has no automated driver; the typed path is covered by the shared funnel plus human item 1 question 2. |
| 6-5 | Each control has a reset affordance that marks the setting for removal from the user file on save, rather than writing the default | ✓ VERIFIED | `addResetButton()` `AppearancePage.cpp:295-309` (`edit-undo-symbolic`, relief none) attached per row at `:255`/`:342`. #238 *"reset marks a key for removal and shows the layer below, not the built-in default"*, #228 *"a reset key is gone from the user file after a save, and no default took its place"*, #258 *"typing a value after a reset cancels the reset"* (ran, PASS). |
| 6-6 | `-DBUILD_CONFIG_GUI=OFF`, or no gtk+-3.0, builds the WM and `wm2-ctl` exactly as before and prints one clear line | ✓ VERIFIED | `CMakeLists.txt:143` (OFF) and `:136` (AUTO probe miss) each print one `message(STATUS)` line. `build/nogtk` has `BUILD_CONFIG_GUI:STRING=OFF` and both binaries, no `wm2-config`. The line is in the committed log: `evidence/remote-desktop/gates/build-all-nogtk.log:5`. The WM target `add_executable(${PROJECT_NAME} …)` `CMakeLists.txt:38-52` is unconditional and references no GUI source. `scripts/preflight.sh:85-89` lists `gtk+-3.0` as optional, never `fail()` (run, `preflight OK`). |
| 6-7 | The window manager binary links no GTK and no GLib in any build configuration | ✓ VERIFIED | `ldd` on all four trees (run) — none. `scripts/gates/install-components.sh` re-checks it on the *installed* files: `bin/wm2-born-again: no GTK/GLib/GObject linkage` (run, both debug and nogtk). |

#### 09-07 — Behaviour and Menu pages (CGUI-03, CGUI-04)

| # | Truth | Status | Evidence |
|---|---|---|---|
| 7-1 | The Behaviour page edits nine settings and every one changes the running WM the moment it is committed | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | #227 *"every Behaviour setting committed through the page's model reaches the running window manager"* proves ADOPTION for all nine through the GUI's own FormState + ProtocolClient (ran, PASS); #213 proves adoption **and** behaviour change end-to-end for click-to-focus only. For the other eight the behaviour half is owned by separate `[wm_config_live]` cases driven by `wm2-ctl`. **`.planning/WINDOWS.md` row 20 records exactly this join as `open`** and it must not be presented as passed. |
| 7-2 | The Menu page is name/command/category rows with Add, Edit, Remove; the dialog's category is a dropdown of the WM's current categories plus free text | ✓ VERIFIED | `apps/wm2-config/MenuPage.cpp` + `MenuModel.h`. #250 *"the Menu page asks the window manager for its categories rather than listing any"*, #261 *"the category list offered by the page is the one the window manager reports"*, #232 (file-only fallback), #234 (draft with no name/command refused), #219 (Remove + Revert). Ran, PASS. Minor residual: WINDOWS row 22 (`open`) — the dialog states the name-override rule unconditionally because no protocol key exposes discovered application names. |
| 7-3 | A menu row added, edited or removed in the GUI changes what the next root menu shows, before anything is saved | ✓ VERIFIED | #257 *"a row added through the page's model appears in the next root menu"* (ran, PASS), with #195/#200 covering the removal and add halves over the same wire. |
| 7-4 | Closing with unsaved live changes asks Save / Discard / Cancel; Discard also returns the running WM to the saved file | ✓ VERIFIED | #233 *"the window's close path offers Save, Discard and Cancel and nothing else"*, #211 *"Discard returns the running window manager to the saved file's values"*, #221 *"closing is silent with nothing unsaved and asks when there is something"*, #239 (Discard in file-only sends nothing). Ran, PASS. |
| 7-5 | On a reload notice the GUI re-reads effective values, keeps and marks unsaved edits, and shows a one-line notice | ✓ VERIFIED | #210 *"a reload notice keeps an unsaved edit and marks it rather than replacing it"*, #251 *"an unsaved edit that a reload made agree with the file is no longer marked"*, #259 *"the window shows the reload notice where the banner is, not in a second status region"*. Ran, PASS. Residual on a *neighbouring* case: WINDOWS row 21 (`open`) — #212 (notice arriving with an entry dialog open) is structural, the live arm is not driven. |
| 7-6 | Reset all on a page marks every setting on that page for removal, and saving removes them rather than writing defaults | ✓ VERIFIED | #204 *"Reset all on a page removes that page's keys from the user file and writes no defaults"*, #260 *"Revert before Save undoes a Reset all"*. Ran, PASS. Built from the same per-setting reset (`AppearancePage.cpp:198-210`). |

#### 09-08 — two install components (CGUI-05)

| # | Truth | Status | Evidence |
|---|---|---|---|
| 8-1 | `cmake --install` with `wm` installs the WM, `wm2-ctl`, the session desktop entry and the docs, and nothing else | ✓ VERIFIED | `bash scripts/gates/install-components.sh build/debug` (run) → exactly `bin/wm2-born-again`, `bin/wm2-ctl`, `share/doc/wm2-born-again/LICENSE`, `share/doc/wm2-born-again/RELEASE-NOTES.md`, `share/xsessions/wm2-born-again.desktop`. The gate asserts EXACT set membership, not containment (`scripts/gates/install-components.sh:8-13`). Rules at `CMakeLists.txt:208-238`. |
| 8-2 | `cmake --install` with `config-gui` installs `wm2-config` and its application entry, and nothing else | ✓ VERIFIED | Same run → exactly `bin/wm2-config`, `share/applications/wm2-config.desktop`. `CMakeLists.txt:241-249`. |
| 8-3 | No file in the `wm` manifest links GTK, GLib or GObject in any build configuration | ✓ VERIFIED | The gate's own `ldd` audit over the *staged* files (run, both trees): `bin/wm2-born-again: no GTK/GLib/GObject linkage`, `bin/wm2-ctl: no GTK/GLib/GObject linkage`. Confirmed independently by direct `ldd` on debug/release/asan/nogtk. |
| 8-4 | A packager can produce a WM package and a config-GUI package from one build tree, and the WM package declares no GTK dependency | ✓ VERIFIED | Two `COMPONENT`s staged separately from one tree in the run above; the GUI-disabled tree stages `config-gui` as empty and still exits 0 (`(nothing -- this tree was configured without the configuration GUI)`). |
| 8-5 | The suite passes with the GUI enabled and disabled, the difference being exactly the GUI's own tests, which skip with a stated reason rather than vanishing | ✓ VERIFIED | `ctest -N` reports **572** registered tests in `build/debug` and **572** in `build/nogtk` — identical registration. In `build/nogtk` the GUI cases report `***Skipped` with a reason: `tests/test_wm2_config_smoke.cpp:418-421` — *"wm2-config was not built in this tree (BUILD_CONFIG_GUI resolved to OFF, or pkg-config could not find gtk+-3.0), so there is no binary to open a window with. The display-free cases in this file still ran."* Confirmed by running *"wm2-config opens a window and reports itself connected…"* and *"wm2-config's resident memory is measured against a stated budget"* in the `build/nogtk` tree — both reported `***Skipped`, not absent. 09-REVIEW-PASS2.md records `build-all.sh nogtk` at 566 passed / 6 skipped. |
| 8-6 | `wm2-config` appears under Settings in the discovered-application list once its desktop entry is installed, through the existing scanner | ✓ VERIFIED | #543 *"the shipped wm2-config entry reaches the discovered-application list through the ordinary scanner"*, #553 *"the shipped wm2-config entry parses and lands under Settings"*, #544 *"…yields no menu entry when the binary is not installed"* (all ran, PASS). `packaging/wm2-config.desktop` carries `Categories=Settings;DesktopSettings;` and `TryExec=wm2-config`. |

#### 09-09 — release close-out (CGUI-01, CGUI-05)

| # | Truth | Status | Evidence |
|---|---|---|---|
| 9-1 | A `wm2-config` on the WM's PATH at startup gives the root menu a Configure entry that launches it; absent, the menu carries no such entry and is otherwise identical | ✓ VERIFIED (narrowed) | #356 *"The root menu carries a Configure entry when wm2-config is on the window manager's PATH at startup, and not when it is not"*, #332 *"Selecting the root menu's Configure entry runs wm2-config and leaves no zombie behind"*, plus display-free index model #43/#48/#49/#51 (all ran, PASS). **Narrowing:** WINDOWS row 25 (`open`) — presence/absence is proven by ROW COUNT and launch identity, not by reading the label off the screen (Xft leaves no strings on the server); the label claim is carried by the `[menupaint]` cases over the shared `RootMenuLayout`. |
| 9-2 | Every config key the binary accepts appears in the release notes, and every key-shaped string the notes claim is a key the binary accepts — checked both ways by a script | ✓ VERIFIED (narrowed) | `bash scripts/gates/doc-keys.sh` (run): *"doc-keys OK: 36 accepted keys, every one documented, and docs/RELEASE-NOTES.md presents no key the binary refuses"*, both direction sections `(none)`. Negative controls committed: `evidence/remote-desktop/gates/doc-keys-shown-to-fail-both-directions.txt`. **Narrowing:** WINDOWS row 26 (`open`) — direction 2 cannot catch a bare unhyphenated word in plain backticks; stated in the script. |
| 9-3 | `COMPILED_CODE_BEHAVIOR_CHECKLIST.md` carries rows for `wm2-config`, `wm2-ctl` and the socket, each done with a named automated test or open with a named reason | ✓ VERIFIED | §"The configuration GUI (Phase 9)" at `:592` and §"The configuration socket (Phase 9)" at `:750`. Every `- [x]` row names its tag and case (e.g. `:439-450` for the four `wm2-ctl` subcommands, `:752-768` for the uid boundary and stale reclamation). Every `- [ ]` row states a reason: `:630` (two settings windows), `:637` (Reset button below the fold), `:792` (dlopen not gated), `:864`/`:880` (footprint, undiagnosed split). |
| 9-4 | The GUI's resident memory is measured, not estimated, against the 512 MB budget, on a real remote-desktop session | ✓ VERIFIED | `evidence/remote-desktop/README.md:91-104` — `/proc/<pid>/statm` field 2 on `Xvnc TigerVNC 1.12.0`: `wm2-config` first instance **101036 kB = 98.7 MB (19%)**, second 50580 kB = 49.4 MB, `wm2-born-again` 12128 kB = 11.8 MB, `Xvnc` 75936 kB = 74.2 MB. Carried into `docs/RELEASE-NOTES.md:484-507`, and *enforced* by #217 *"wm2-config's resident memory is measured against a stated budget"* (ran, PASS). The research assumption log's 30-60 MB estimate was falsified rather than adopted. |
| 9-5 | The release evidence carries no host identifier, no address, and no session-error-log content | ✓ VERIFIED | Verifier re-ran the scan over `evidence/`: `uname -n` string → no occurrences; IPv4-shaped strings → none; `xsession-errors` → three hits, all in `README.md` prose *naming* the prohibition (each carrying an explicit `planner-discipline-allow` marker). The pre-existing 08/08.5 exposure is recorded and was scrubbed by the orchestrator (WINDOWS row 28, `fixed`). |

**Score:** 58/60 truths verified (2 present, behavior-unverified). No truth FAILED.

---

### Required Artifacts

| Artifact | Expected | Status | Details |
|---|---|---|---|
| `include/Config.h` | `tabFont`/`menuFont` fields, nine colours | ✓ VERIFIED | `:44-53` colours, `:87-88` fonts; wired into `Border.cpp:271`, `Manager.cpp:787`. |
| `src/Config.cpp` | parser rows, `kOptionSpecs`, settable-key view | ✓ VERIFIED | `:164-165`, `:491-503`, `:688-700`. 863+ lines. |
| `include/ConfigProtocol.h` | header-only frozen v1 codec | ✓ VERIFIED | 664 lines. Included by `SocketServer.h` (→ the WM), `apps/wm2-ctl/main.cpp`, and four GUI headers — one wire, three ends. |
| `include/ConfigFileWriter.h` / `src/ConfigFileWriter.cpp` | surgical atomic writer | ✓ VERIFIED | 164 + 700 lines. **Not** compiled into the WM target (`CMakeLists.txt:38-52`) — D-01's "the WM stays a pure reader" holds structurally. |
| `include/SocketServer.h` / `src/SocketServer.cpp` | listening socket, uid gate, stale reclamation | ✓ VERIFIED | 439 + 909 lines; compiled into the WM at `CMakeLists.txt:47`. |
| `apps/wm2-ctl/main.cpp` | display-free CLI client | ✓ VERIFIED | 460 lines; four subcommands + four exit codes confirmed by running `--help`. |
| `apps/wm2-config/*` (10 files) | GTK settings window | ✓ VERIFIED | 3 617 lines total; every file non-trivial, every one compiled into the `wm2-config` target (`CMakeLists.txt:161-179`) and the display-free halves also into `test_wm2_config_smoke` (`:832-833`). |
| `packaging/*.desktop` (2) | session + application entries | ✓ VERIFIED | Parsed by this project's own `DesktopEntry` parser in #553/#543, not by eye. |
| `scripts/gates/install-components.sh` | component + linkage gate | ✓ VERIFIED | 100 %+ exercised: run against both `build/debug` and `build/nogtk`, both OK. |
| `scripts/gates/doc-keys.sh` | two-direction key/doc parity | ✓ VERIFIED | Run, OK, 36 keys. |
| `docs/RELEASE-NOTES.md` | every key + every behaviour documented | ✓ VERIFIED | doc-keys gate green both directions; the WR-13 refusal documented at `:677-694`; live-apply promise at `:657-658`; resource table at `:484-507`. |
| `COMPILED_CODE_BEHAVIOR_CHECKLIST.md` | phase-9 rows | ✓ VERIFIED | §592, §750; all open rows reasoned. |
| Nine `tests/test_*.cpp` files | phase-9 coverage | ✓ VERIFIED | `TEST_CASE` counts: `test_config.cpp` 57, `test_config_protocol.cpp` 26, `test_config_writer.cpp` 39, `test_config_socket.cpp` 25, `test_wm_socket.cpp` 17, `test_wm_config_live.cpp` 42, `test_wm2_config_smoke.cpp` 61, `test_desktopentry.cpp` 16. **199 distinct cases run by this verification, all PASS** (see the Behavioural Spot-Checks table). |

No artifact is a stub. No artifact is orphaned.

### Key Link Verification

| From | To | Via | Status | Details |
|---|---|---|---|---|
| `Config::tabFont` | the drawn tab label | `Border::loadTabFont()` | ✓ WIRED | `src/Border.cpp:271` reads `windowManager()->config().tabFont`; measured at `:351-370`, drawn at `:988-1024`. |
| `Config::menuFont` | every root-menu row | `WindowManager::initialiseScreen()` | ✓ WIRED | `src/Manager.cpp:787` `m_config.menuFont.c_str()`. |
| `kOptionSpecs` row | `getopt_long` table **and** `--help` | one declaration | ✓ WIRED | `src/Config.cpp:491` declared, `:533` usage loop, `:661` parse. `--help` output confirms. |
| `ConfigProtocol.h` | WM, `wm2-ctl`, `wm2-config` | header-only include | ✓ WIRED | Six including files; the WM reaches it through `include/SocketServer.h`. |
| `configFileManagedKeys()` | writer, GUI form, settable list | one vector | ✓ WIRED | Defined once `src/ConfigFileWriter.cpp:376`; consumed at `:412`, `apps/wm2-config/FormState.cpp:72`, and cross-checked by #476, #245, #180. |
| socket descriptor set | the ONE poll array both `nextEvent()` and `modalWait()` build | shared set | ✓ WIRED | Proven behaviourally by #149 answering under a grab while #146/#147/#150 confirm `modalWait` is otherwise intact. |
| `Atoms::wm2ConfigSocket` | how a client finds the socket | root-window property | ⚠️ WIRED (no shipped consumer) | Published `src/Manager.cpp:979`; consumed by the test client (`test_wm_socket.cpp:339` → `Conn c(path)`). No shipped client reads it. Warning W-2. |
| `SO_PEERCRED` verdict | accept-or-close **before** decode | `src/SocketServer.cpp:591` | ✓ WIRED | Verdict computed before any `configProtocolDecode()`; #144. |
| a `set` message | `applyKeyValue()` on a copy → `applyConfig()` → live X11 | one funnel | ✓ WIRED | `grep -n "m_config =" src/*.cpp` returns **exactly one** hit: `src/Manager.cpp:2188`, inside `applyConfig()`. No divergent path exists. |
| GTK signal | `Page` → `FormState` → `ProtocolClient` → running WM | callback chain | ✓ WIRED | `apps/wm2-config/main.cpp:148-176` hands every page the same `applyLive` lambda; proven end-to-end by #254 and #213. |
| `FormState` | `ConfigFileWriter` edit list → user file, only on Save | `form.edits()` | ✓ WIRED | `test_wm2_config_smoke.cpp:1938` drives the real path; #229 confirms nothing is written when nothing changed. |
| `BUILD_CONFIG_GUI` AUTO | the `wm2-config` target exists or not; WM target unchanged | pkg-config probe | ✓ WIRED | `CMakeLists.txt:125-179`; WM target at `:38-52` is outside the conditional. |
| installed `wm2-config.desktop` | XDG scanner → root menu Settings | existing scanner | ✓ WIRED | #543, #553. |
| PATH lookup at startup | a boolean the root menu reads → one more row | `src/Buttons.cpp` | ✓ WIRED | #356, #332, #43/#48/#49/#51. |

### Data-Flow Trace (Level 4)

| Artifact | Value | Source | Produces real data | Status |
|---|---|---|---|---|
| `AppearancePage` colour rows | current colour spelling | `FormState` seeded from `configLayersFromDisk()` + `get` over the socket | Yes — #246 shows a system-layer-only value rendered as effective **and naming its layer** | ✓ FLOWING |
| `BehaviourPage` bounds and wording | min/max and summary text | the option table itself | Yes — #240 *"the delay controls are built from the parser's own bounds, not from a third copy"*, #208 (frame thickness), #262 (wording from the table's own summary) | ✓ FLOWING |
| `MenuPage` category dropdown | category names | a `categories` request to the running WM | Yes — #250 asserts the page **asks** rather than listing; #261 asserts the list matches what the WM reports; #232 is the file-only fallback | ✓ FLOWING |
| `wm2-ctl status` | seven fields | `statusReplyMessage()` reading live WM state | Yes — #151 asserts the counts are *real* (they move with mapped/hidden windows) | ✓ FLOWING |
| `wm2-ctl get <key>` | one value | running `m_config`, not the file | Yes — #189 proves a `set` then `get` disagrees with disk | ✓ FLOWING |
| root menu `Configure` row | presence | PATH probe at startup | Yes — #356 runs the fixture twice, one tree with a real built `wm2-config` on PATH and one without | ✓ FLOWING |

No HOLLOW_PROP, no STATIC fallback, no DISCONNECTED value found.

### Behavioural Spot-Checks

| Behavior | Command | Result | Status |
|---|---|---|---|
| Protocol codec is display-free and total | `ctest -L config_protocol` in `build/debug` | 26/26 passed, 0.11 s | ✓ PASS |
| Surgical writer preserves, is atomic, is idempotent | `ctest -L config_writer` | 39/39 passed, 2.68 s | ✓ PASS |
| Socket answers, is uid-gated, survives grabs | `ctest -L wm_socket` | 18/18 passed, 8.33 s | ✓ PASS |
| Every setting applies live over the socket | `ctest -L wm_config_live` | 43/43 passed, 38.99 s | ✓ PASS |
| The settings window, its form and its client | `ctest -L wm2_config_smoke` | 62/62 passed, 22.16 s | ✓ PASS |
| Configure entry + desktop-entry scanning | `ctest -R "Configure row\|Configure entry\|shipped wm2-config entry"` | 10/10 passed, 2.11 s | ✓ PASS |
| Font ladder and independence | `ctest -R "unresolvable tab-font\|tab-font changes the thickness\|menu-font changes the height\|independent settings\|no font available at all"` | 6/6 passed, 2.91 s | ✓ PASS |
| GUI tests skip rather than vanish without GTK | `ctest -L wm2_config_smoke -R "opens a window\|resident memory"` in `build/nogtk` | both `***Skipped` with the `kGuiNotBuilt` reason, 0 failed, 0 absent | ✓ PASS |
| WM links no toolkit | `ldd build/{release,debug,asan,nogtk}/wm2-born-again` | no GTK/GLib/GObject/GDK/pango/cairo in any tree | ✓ PASS |
| `wm2-ctl` links no display library | `ldd build/release/wm2-ctl` | libstdc++, libgcc_s, libc, libm only | ✓ PASS |
| `--help` is generated from the parse table | `build/release/wm2-born-again --help` | `--tab-font` and `--menu-font` present | ✓ PASS |
| `wm2-ctl --help` | `build/release/wm2-ctl --help` | four subcommands, four exit codes, settable key list | ✓ PASS |
| Test registration parity | `ctest -N` in `build/debug` vs `build/nogtk` | 572 vs 572 | ✓ PASS |

**Totals actually executed by this verification, per invocation:** 26 + 39 + 18 + 43 + 62 + 10 + 6 in `build/debug`, and 3 in `build/nogtk` (of which 2 are the reasoned GUI skips) — **207 case executions, 0 failures, 2 reasoned skips**. Discounting the `preflight` fixture that re-runs with each invocation, that is **199 distinct phase-relevant cases**. Plus 4 gate/binary invocations (`install-components.sh` ×2, `doc-keys.sh`, `preflight.sh`) and 6 `ldd` audits. The full 572-test suite was deliberately **not** run (host under memory pressure, per instruction) — see Warning W-4.

### Gate Execution

| Gate | Command | Result | Status |
|---|---|---|---|
| Install components (debug) | `bash scripts/gates/install-components.sh build/debug` | `install-components OK`; `wm` = 5 exact files, `config-gui` = 2 exact files, both binaries clean of GTK | PASS |
| Install components (nogtk) | `bash scripts/gates/install-components.sh build/nogtk` | `install-components OK`; `config-gui` empty and exit 0 | PASS |
| Doc-key parity | `bash scripts/gates/doc-keys.sh` | `doc-keys OK: 36 accepted keys`; both directions `(none)` | PASS |
| Preflight | `bash scripts/preflight.sh` | `preflight OK`; `gtk+-3.0` reported as optional, never `fail()` | PASS |
| Four-tree build gate | `scripts/gates/build-all.sh {debug,asan,release,nogtk}` | **NOT re-run at HEAD.** 09-REVIEW-PASS2.md records all four green at `557550a` (572/572; nogtk 566+6 skipped; release link audit 24 entries). HEAD `e32542a` is two documentation-only commits later. | DEFERRED → human item 4 |

### Requirements Coverage

| Requirement | Source plans | Description | Status | Evidence |
|---|---|---|---|---|
| CGUI-01 | 09-06, 09-09 | Separate GTK3 binary (`wm2-config`) for visual configuration editing | ✓ SATISFIED | 3 617-line GTK app in `apps/wm2-config/`; real binary opens a window under Xvfb (#255); separate `config-gui` install component; six screenshots committed. |
| CGUI-02 | 09-02, 09-03, 09-04 | Communicates with WM via Unix domain socket (JSON protocol) | ✓ SATISFIED | `src/SocketServer.cpp` + `include/ConfigProtocol.h`; 26 codec cases, 25 unit-socket cases, 17 runtime-socket cases, all PASS; `wm2-ctl` as the reference client. |
| CGUI-03 | 09-01, 09-02, 09-06, 09-07 | Edit fonts, colors, focus policy, frame thickness, menu entries | ✓ SATISFIED | Fonts became configuration in 09-01 (the precondition CONF-02 left open); Appearance page carries fonts/colours/thickness (#231), Behaviour carries focus policy (#214), Menu carries entries (#250); #224 proves the pages partition the settable keys with nothing dropped. |
| CGUI-04 | 09-04, 09-05, 09-07 | Changes apply immediately (no restart required) where possible | ✓ SATISFIED | 42 `[wm_config_live]` cases, all PASS. `docs/RELEASE-NOTES.md:657-658` names no restart-only setting. The three keys refused mid-grab are refused **retryably**, not deferred to restart, and are documented (`:677-694`) — "where possible" is honoured and the exception is named. |
| CGUI-05 | 09-08, 09-09 | WM works without GTK installed (GUI is optional dependency) | ✓ SATISFIED | `ldd` across four trees; `install-components.sh` linkage audit on the *installed* manifest; `build/nogtk` builds and runs the same 572-test suite with 6 reasoned skips. |

**No orphaned requirements.** `.planning/REQUIREMENTS.md:193-197` maps CGUI-01..05 to Phase 9 and nothing else; every one is claimed by at least one plan's `requirements:` frontmatter. The `Complete` marking in REQUIREMENTS.md is earned.

### Decision Coverage (09-CONTEXT.md D-01..D-20)

| Decision | Honoured | Evidence |
|---|---|---|
| D-01 GUI owns the file; WM is a pure reader | ✓ | `src/ConfigFileWriter.cpp` is absent from the WM target's source list (`CMakeLists.txt:38-52`); `grep "configFileWrite" src/*.cpp` finds it only in the writer itself. |
| D-02 surgical edit | ✓ | #475, #508, #498. |
| D-03 file-only mode + exact banner | ✓ | `ConnectionState.h:33` verbatim; #248, #203. Narrowed: one control insensitive (see 6-3). |
| D-04 user file only, effective values shown | ✓ | #216, #477, #246. |
| D-05 instant on commit, Revert restores | ✓ | #254, #209. |
| D-06 nothing waits for a restart | ✓ (with a named exception) | `RELEASE-NOTES.md:657-658`. D-06's own reversibility clause ("any one setting can fall back … and the record must say which and why") is what WR-13 exercised for three keys; the record is `RELEASE-NOTES.md:677-694`, but not `.planning/WINDOWS.md` — Warning W-1. |
| D-07 Save/Discard/Cancel on close | ✓ | #233, #211. |
| D-08 reload broadcast, edits kept and marked | ✓ | #167, #210, #251, #472. |
| D-09 three pages, Save/Revert bar, header state | ✓ | `main.cpp:117-217`; #231, #214, #224. |
| D-10 native choosers + editable spelling | ✓ | `AppearancePage.cpp:239/276/327`; #241. |
| D-11 Configure entry on PATH + `.desktop` | ✓ | #356, #332, #543, #553. |
| D-12 rows with Add/Edit/Remove, category dropdown | ✓ | #250, #261, #219, #234. |
| D-13 reset removes keys, not writes defaults | ✓ | #503, #238, #228, #204. |
| D-14 status only, no per-window data | ✓ | #151, #152. |
| D-15 hello handshake both ways | ✓ | #207, #244, #206. |
| D-16 same uid, 0700 dir, 0600 socket, `SO_PEERCRED` | ✓ | `SocketServer.cpp:172/295/323/380/591`; #154, #144, #460, #467, #471. |
| D-17 `wm2-ctl` with no GTK in the WM package | ✓ | `ldd`; `CMakeLists.txt:218-224` installs it under `COMPONENT wm`. |
| D-18 `BUILD_CONFIG_GUI` AUTO, preflight optional | ✓ | `CMakeLists.txt:125-145`; `preflight.sh:85-89`. |
| D-19 two install components | ✓ | Gate run against both trees. |
| D-20 measured VNC pass with RSS | ✓ (measurement) / human (perception) | `evidence/remote-desktop/README.md:91-104`; #217. Questions 1-2 of the plan's own human-check were not answerable from the capture — `README.md:79` records "No viewer was attached". |

20/20 trackable decisions honoured. Non-blocking gate; no status impact.

### Test Quality Audit

| Test file | Linked req | Active | Skipped | Circular | Assertion level | Verdict |
|---|---|---|---|---|---|---|
| `test_config_protocol.cpp` | CGUI-02 | 26 | 0 | No | Value | OK |
| `test_config_writer.cpp` | CGUI-02/03 | 39 | 1 conditional (euid==0, not this host) | No | Value / byte-identity | OK |
| `test_config_socket.cpp` | CGUI-02 | 25 | 3 conditional (root / cannot-chown, each SKIP with a reason) | No | Value | OK |
| `test_wm_socket.cpp` | CGUI-02 | 17 | 0 | No | Behavioural (real WM, real socket) | OK |
| `test_wm_config_live.cpp` | CGUI-04 | 42 | 1 conditional (euid==0) | No | Behavioural (pixel captures, geometry, focus) | OK |
| `test_wm2_config_smoke.cpp` | CGUI-01/03/04 | 61 | 4 conditional `kGuiNotBuilt` + 1 socket-buffer | No | Value / behavioural | OK |
| `test_desktopentry.cpp` | CGUI-05 | 16 | 0 | No | Value | OK |
| `test_wm_runtime.cpp` (phase-9 cases) | CGUI-01/03 | 6+ | 0 | No | Behavioural | OK |

- **Disabled tests on requirements:** 0 unconditional. Every SKIP is a runtime-environment guard with a stated reason (`euid==0`, `BUILD_CONFIG_GUI=OFF`, kernel refuses chown, host socket buffer too small), and none can pass vacuously — each is a SKIP, not a silent success.
- **Circular patterns:** 0. Expected values are literals, parser bounds, or independently derived; the `[config_writer]` cases compare against `Config::applyFile()` — the *parser*, an independent oracle from the writer under test.
- **Insufficient assertions:** 0 requirement-linked case rests on existence/type only. Colour and geometry claims are pixel and dimension comparisons; behaviour claims drive synthesised pointer input.
- **Non-vacuity:** the executor recorded mutation checks for the load-bearing cases (e.g. 09-01's tab metric moving 8→27 px and 24→43 px with the `Border.cpp` hunk in place, 493→1113 px for the menu; 09-04's diff removal reddening #193 with 4 re-shapes). One known exception: WINDOWS row 18 (`open`) — the `m_menuOpen` deferral has no mutation-proof case, because the category vector's buffer is reused rather than freed.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|---|---|---|---|---|
| — | — | `TBD` / `FIXME` / `XXX` across all 30 phase-9 source, header, app, script and packaging files | — | **Zero occurrences.** |
| — | — | `TODO` / `HACK` / `PLACEHOLDER` | — | **Zero occurrences.** |
| — | — | "not implemented" / "coming soon" / "placeholder" prose | — | **Zero occurrences.** |

No debt-marker gate trips. No stub returns, no hardcoded empty data reaching rendering, no console-log-only implementations.

---

## Warnings (non-blocking)

**W-1 — a plan must-have was narrowed by a review fix and the defect ledger carries no row for it.**
`.planning/WINDOWS.md` has no entry mentioning WR-13, the modal grab, or the three refused keys (`grep -c "WR-13" .planning/WINDOWS.md` → 0). 09-04's truth 7 said a `set` under a modal grab "is applied without waiting for the grab to end"; since WR-13 (`src/Manager.cpp:2049-2065`) three of the settable keys are refused instead. The narrowing is correct engineering, is tested (#172), and is documented in the user-facing contract (`docs/RELEASE-NOTES.md:677-694`) — but the ledger is the project's record of *narrowings*, and this one is only in the release notes and the review document. Recommend a `deviation` row so a later reader does not re-derive it.

**W-2 — `_WM2_CONFIG_SOCKET` has no shipped consumer, and the signoff checklist overstates its client half.**
The property is published (`src/Manager.cpp:979`) and works (#159; the `[wm_socket]` fixture genuinely discovers and connects through it). But `wm2-ctl` cannot read it — no X11, documented and deliberate — and `wm2-config` also reconstructs, from `DISPLAY` (`apps/wm2-config/ProtocolClient.cpp:35-39`), despite having a display connection. `COMPILED_CODE_BEHAVIOR_CHECKLIST.md:452-459` marks *"The socket path is discoverable without reconstructing it"* as done and cites, for "the client half", the case *"wm2-ctl finds the socket from DISPLAY, and honours --socket"* — which is the reconstruction path, not the discovery path. The mechanism is sound; the checklist's justification is not. Consider either pointing `ProtocolClient::resolveSocketPath()` at the property (it already has a display) or restating the checklist row.

**W-3 — a known GUI data-loss window is open and unclosed.**
`COMPILED_CODE_BEHAVIOR_CHECKLIST.md:630-636`: two settings windows open at once — the second Save silently discards the first, because each instance writes the whole user file from its own picture. Flagged in 09-06, carried unclosed through 09-07, and judged a v1.1 candidate. Not a phase truth and not a blocker, but it is a silent-loss path in the feature this phase delivers, and it deserves the operator's explicit sign-off rather than a checklist row.

**W-4 — the four-tree build gate was not re-run at HEAD.**
Green at `557550a` (09-REVIEW-PASS2.md); HEAD is `e32542a`, two documentation-only commits later. This verification ran 199 distinct cases at HEAD in the debug tree (plus 3 in nogtk), all PASS, but not the full 572 nor the release or ASan trees. Routed to human item 4.

---

## Deferred Items

| # | Item | Addressed in | Evidence |
|---|---|---|---|
| 1 | An alternative, GTK-free configuration front end for servers where GTK is unwanted or unavailable | Phase 10 | `.planning/ROADMAP.md:419` — *"Every operation the GTK tool supports (Appearance, Behaviour, Menu pages; per-setting and per-page reset; Save/Discard/Cancel on close; reload refresh; file-only mode when no WM is running) works identically, driven by the Phase 9 socket protocol and surgical file writer unchanged."* Phase 9's socket, codec, writer and page design are its fixed inputs. |
| 2 | Reducing the settings window's ~99 MB footprint (or the undiagnosed first-vs-later 49 MB split) | not scheduled | `COMPILED_CODE_BEHAVIOR_CHECKLIST.md:864-885`, WINDOWS row 27 (`open`). Explicitly a decision rather than a defect; the `wm` package already has no GTK dependency and `wm2-ctl` reaches every setting. |

---

## Ledger Items Still Open (recorded, not presented as passed)

`.planning/WINDOWS.md` rows for phase 09 with status `open`, carried forward verbatim: 15 (09-01 acceptance shape), 16 and 17 (root-only SKIPs), 18 (menu-open deferral has no mutation-proof case), 20 (**the eight-of-nine Behaviour join — truth 7-1 above**), 21 (reload-with-dialog-open is structural), 22 (name-override note unconditional), 23 (release tree not run for 09-07), 25 (**Configure row proven by count, not label — truth 9-1**), 26 (**doc-keys direction 2 blind spot — truth 9-2**), 27 (undiagnosed memory split), 29-34 and 36-37 (review-fix residuals with no reachable case, each with its reason). Rows 19, 24, 28 and 5/8/9/11/12/13 are `fixed`.

None of these was treated as closed by this report.

---

## Human Verification Required

### 1. Appearance-page visual review (09-06's own `<human-check>`)

**Test:** Look at `.planning/phases/09-config-gui-ipc/evidence/wm2-config/appearance-page.png`, `appearance-page-default-size.png` and `appearance-page-font-chooser-open.png`, and answer:
1. Is every control's purpose obvious to someone who has never edited the config file?
2. Is the config-file spelling beside each chooser readable and clearly secondary to it, rather than competing with it?
3. Is the connection state in the header noticeable without being loud?
4. Does the reset affordance read as "put this back" rather than as a delete?

**Expected:** `approved`, or a described change.
**Why human:** Legibility, visual weight and affordance semantics. No grep or ctest case can read them.

### 2. Whole-tool review (09-07's own `<human-check>`)

**Test:** Look at the five screenshots under `.planning/phases/09-config-gui-ipc/evidence/wm2-config/` (`appearance-page.png`, `behaviour-page.png`, `menu-page.png`, `menu-entry-dialog.png`, `close-prompt.png`) and answer:
1. Three pages — is anything on the wrong page, or missing from all of them?
2. Behaviour page — would a non-programmer understand what each setting does from the label alone?
3. Menu page — is the Add/Edit/Remove flow obvious, and does a row read clearly?
4. Close prompt — are Save, Discard and Cancel unambiguous about what Discard does to the running desktop?
5. Overall — does this look like one tool, or like three pages built separately?

**Expected:** `approved`, or a described change.
**Why human:** Copy quality and coherence judgement. Note for question 1: `COMPILED_CODE_BEHAVIOR_CHECKLIST.md:637` records that the Appearance page's *Reset this page* button sits below the fold at the default window size.

### 3. Remote-desktop pass with a viewer attached (09-09's own `<human-check>`)

**Test:** Open `wm2-config` in a real remote-desktop session **with a viewer attached** and answer:
1. Does it feel usable over the link, or is it laggy enough that a user would give up?
2. Do the live changes appear on the desktop as fast as they do locally?
3. Read the recorded resident-memory figures — 98.7 MB for the first settings window in a session, 49.4 MB for later ones, against a 512 MB VPS. Is that acceptable, or does it change what this project should claim?
4. Confirm the evidence bundle carries no hostname, no address and nothing from a session error log.

**Expected:** `approved`, or a described change.
**Why human:** Questions 1 and 2 are perceptual and **were not answerable from the committed capture** — `evidence/remote-desktop/README.md:79` records *"No viewer was attached; the pointer input below is XTEST."* Question 3 is a product decision. Question 4 was re-run mechanically by this verification and came back clean (no `uname -n` string, no IPv4-shaped string, `xsession-errors` only in prose naming the prohibition), but the plan asks for the operator's own confirmation.

### 4. Re-run the four build gates at HEAD

**Test:** `bash scripts/gates/build-all.sh debug`, then `asan`, then `release`, then `nogtk`.
**Expected:** All four green at 572 tests; nogtk skips exactly six cases, each with a stated reason; release link audit OK (24 entries).
**Why human:** The gates were last run in full at `557550a`; HEAD is `e32542a`. The two intervening commits are documentation-only, but this verification was instructed not to run the full suite, so the claim at HEAD rests on inspection of the delta rather than on a run.

### 5. Arrival order of two overlapping `set` messages (behaviour-unverified, truth 4-7)

**Test:** Open two connections, write `set frame-thickness 11` on A and `set frame-thickness 21` on B back to back before reading either reply, then `wm2-ctl get frame-thickness`.
**Expected:** The later-arriving write wins; neither is dropped; the window manager stays alive and every window is re-framed exactly once per applied `set`.
**Why human:** No case issues two overlapping sets. 09-04-SUMMARY.md D7 records this as `human_judgment: true`, resting on "there is no second thread and no queue to reorder". Presence checks can see the absence of a second thread; they cannot see the ordering the invariant asserts.

### 6. GUI-driven behaviour change for the eight non-tracer Behaviour settings (behaviour-unverified, truth 7-1)

**Test:** For each of raise-on-focus, auto-raise, focus-stealing-prevention, auto-raise-delay, pointer-stopped-delay, destroy-window-delay, new-window-command and exec-using-shell: change it in the running `wm2-config` Behaviour page and observe the desktop behaviour change (not merely that `wm2-ctl get` reports the new value).
**Expected:** The behaviour matches what the corresponding `[wm_config_live]` case observes after a `wm2-ctl set` of the same key.
**Why human:** `.planning/WINDOWS.md` row 20 (`open`) records that these eight are proven through the GUI only as far as *adoption* (#227); the behaviour half is owned by separate `wm2-ctl`-driven cases. Only click-to-focus has both halves in one case (#213). The join is currently an inference.

---

## Gaps Summary

**None.** No must-have FAILED, no artifact is missing or a stub, no key link is broken, no blocker anti-pattern exists, and every requirement CGUI-01..05 is satisfied by evidence rather than by claim. 199 distinct test cases were executed at HEAD across five labels and two targeted `-R` runs, plus four gate/binary invocations, with zero failures.

The phase goal — *a non-programmer configuring the window manager visually through a separate GTK3 application talking to the running WM over IPC* — is achieved in the codebase. All four roadmap success criteria hold.

Two items are **present and wired but behaviourally unexercised** (truths 4-7 and 7-1), and both correspond to gaps the project's own defect ledger and plan summaries already record as open; they are reported here rather than absorbed. Four items require a human because they are perceptual, and three of those four are the plans' own `<human-check>` blocks, deferred to end-of-phase by design. That is why the status is `human_needed` and not `passed`.

Four non-blocking warnings are recorded above; W-1 (an unrecorded narrowing of a plan must-have) and W-3 (a known silent-overwrite path when two settings windows are open) are the two most worth the operator's attention.

---

_Verified: 2026-09-06T19:57:48Z_
_Verifier: Claude (gsd-verifier)_
