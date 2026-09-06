---
phase: 09-config-gui-ipc
plan: 09
subsystem: ui
tags: [root-menu, xdg-path-probe, release-notes, gates, tigervnc, resident-memory, signoff]
status: complete

requires:
  - phase: 09-06
    provides: the wm2-config binary D-11's probe looks for and this plan measures
  - phase: 09-07
    provides: the three settings pages exercised in the remote-desktop pass, and the read-only menu-categories key this plan documents
  - phase: 09-08
    provides: install-components.sh and the nogtk arm, two of the four gates captured at the release commit
provides:
  - "D-11: the root menu's `Configure...` entry, present when a wm2-config binary is on the window manager's PATH at startup and absent when it is not"
  - "DesktopEntry::findOnPath() promoted to the namespace and declared in the header -- one answer to \"is this program installed\", two callers"
  - "include/RootMenuModel.h -- the root menu's top-level index model, display-free and testable"
  - "scripts/gates/doc-keys.sh -- key-to-documentation parity in BOTH directions, shown to fail three ways"
  - "The measured resident memory of wm2-config: 98.7 MB on a real TigerVNC session, against a research estimate of 30-60 MB"
  - "A [wm2_config_smoke] resident-memory budget case for the GUI, sharing one /proc reader with [wm_resource_budget]"
  - "Sixteen COMPILED_CODE_BEHAVIOR_CHECKLIST.md rows for this phase's surface, closed with citations or open with reasons"
  - ".planning/phases/09-config-gui-ipc/evidence/remote-desktop/ -- the once-per-release pass with all four gates at its commit"
affects: [release-signoff, phase-10, packaging]

actuals:
  tokens: 109316
  tasks: 4
  commits: 10
  plan_head_before: ab591938ed84e04613ab08d7e6a07c8697477614

tech-stack:
  added: []
  patterns:
    - "Index arithmetic shared by four sites in one modal loop, extracted into a dependency-free header a display-free test can reach (the MenuPaint.h precedent)"
    - "Two-fixture differential: two window managers whose child environments differ in exactly one variable, compared by a measurement neither fixture is told about"
    - "A documentation gate that reads BOTH the accepted set and the documented set and reports the difference in both directions, with allow lists that carry a reason per entry"
    - "A budget chosen FROM the measurement and stated in the test, never before it"

key-files:
  created:
    - include/RootMenuModel.h
    - scripts/gates/doc-keys.sh
    - .planning/phases/09-config-gui-ipc/evidence/remote-desktop/README.md
  modified:
    - src/Buttons.cpp
    - src/Manager.cpp
    - src/DesktopEntry.cpp
    - include/DesktopEntry.h
    - include/Manager.h
    - tests/test_wm_runtime.cpp
    - tests/test_menupaint.cpp
    - tests/test_wm2_config_smoke.cpp
    - tests/test_wm_resource.cpp
    - tests/support/WmFixture.h
    - docs/RELEASE-NOTES.md
    - COMPILED_CODE_BEHAVIOR_CHECKLIST.md
    - scripts/capture-display-capabilities.sh
    - CMakeLists.txt

key-decisions:
  - "The Configure row's presence is asserted by ROW COUNT and by launch identity, not by reading a label off the screen: the menu paints text with Xft and the server keeps pixels, not strings. The label claim is carried by display-free [menupaint] cases over the same RootMenuLayout src/Buttons.cpp uses."
  - "The selection case launches a shim named wm2-config rather than the GTK binary, because what is under test is that the entry execs a program of that name through the window manager's spawn path -- and a shim leaves nothing running to clean up"
  - "The doc-keys gate reads THREE sources for the accepted set (option table, parser chain, protocol key constants), because the key vocabulary genuinely lives in three places and a check reading only the table would pass while the other two drifted"
  - "The doc-keys gate does not harvest `--flag` spellings: these notes document cmake's command lines too, and that channel produced build/component/install/parallel/prefix as false failures"
  - "The GUI's resident-memory budget is 160 MB, ~1.6x the measured 101 MB -- tighter than the window manager's ~2x, because at this size a 2x budget would be most of a quarter of the machine and would stop being a detector"
  - "The 98.7 MB figure is reported as a consequence rather than absorbed: the release notes state that the settings window costs about a fifth of a 512 MB machine while open, and point a memory-constrained user at wm2-ctl and the GTK-free wm package instead"
  - "The first-versus-later-launch split (~101 MB vs ~50 MB) is recorded as UNDIAGNOSED with two candidate causes explicitly ruled out, rather than explained by a guess"
  - "scripts/capture-display-capabilities.sh no longer records `uname -n`: it wrote this machine's hostname into every capture the project has ever taken, and that output is committed to a public repository"

patterns-established:
  - "When a runtime assertion structurally cannot reach a claim (no readable label channel), extract the decision into a dependency-free header and pin it where nothing can flake -- then say in the runtime case's comment why it lives elsewhere"
  - "Show a new gate FAILING, in every direction it claims to check, and commit the transcripts beside the green run"
  - "State a gate's uncovered case inside the gate, with the measurement that justifies the trade-off"

requirements-completed: [CGUI-01, CGUI-05]

coverage:
  - id: D1
    description: "The root menu carries a Configure entry when wm2-config is on the window manager's PATH at startup, and carries none when it is not, with every other row at the index it already had"
    requirement: CGUI-01
    verification:
      - kind: integration
        ref: "tests/test_wm_runtime.cpp#The root menu carries a Configure entry when wm2-config is on the window manager's PATH at startup, and not when it is not"
        status: pass
      - kind: unit
        ref: "tests/test_menupaint.cpp#without the settings window on PATH every other row keeps the index it had, the exit row included"
        status: pass
      - kind: unit
        ref: "tests/test_menupaint.cpp#the Configure row sits at the end of the top level, before Exit"
        status: pass
    human_judgment: false
  - id: D2
    description: "Selecting the Configure entry launches the settings window through the spawn path that already carries the zombie-reaping guarantee"
    requirement: CGUI-01
    verification:
      - kind: integration
        ref: "tests/test_wm_runtime.cpp#Selecting the root menu's Configure entry runs wm2-config and leaves no zombie behind"
        status: pass
    human_judgment: false
  - id: D3
    description: "Every config key the binary accepts appears in docs/RELEASE-NOTES.md, and every key-shaped string the notes present as a setting is a key the binary accepts"
    requirement: CGUI-05
    verification:
      - kind: command
        ref: "bash scripts/gates/doc-keys.sh (36 accepted keys, both directions clean)"
        status: pass
      - kind: command
        ref: "shown to fail three ways: evidence/remote-desktop/gates/doc-keys-shown-to-fail-*.txt"
        status: pass
    human_judgment: false
  - id: D4
    description: "COMPILED_CODE_BEHAVIOR_CHECKLIST.md carries rows for wm2-config, wm2-ctl and the socket, each done with a named test or open with a named reason"
    verification:
      - kind: command
        ref: "grep -cE '^- \\[[ x]\\]' COMPILED_CODE_BEHAVIOR_CHECKLIST.md -> 121, from a 103 baseline; every new closed row cites a ctest label, a case title, an evidence path or a gate script"
        status: pass
    human_judgment: false
  - id: D5
    description: "The settings window's resident memory is measured against the 512 MB budget on a real remote-desktop session, not estimated"
    requirement: CGUI-05
    verification:
      - kind: integration
        ref: "tests/test_wm2_config_smoke.cpp#wm2-config's resident memory is measured against a stated budget"
        status: pass
      - kind: manual
        ref: ".planning/phases/09-config-gui-ipc/evidence/remote-desktop/README.md (TigerVNC 1.12.0, 101036 kB, commit 8f4275b)"
        status: pass
    human_judgment: false
  - id: D6
    description: "The settings window is usable over a real remote-desktop link, and the measured figure is acceptable against the 512 MB constraint"
    verification:
      - kind: manual
        ref: "evidence/remote-desktop/page-*.png -- three pages reached by XTEST over the VNC session and screenshotted"
        status: pass
    human_judgment: true
    rationale: "Whether the window FEELS usable over a real link, and whether 98.7 MB is acceptable or changes what this project should claim, are judgements no automated check can make. No viewer was attached and this session was loopback with no latency. The four questions are recorded below for the operator."
  - id: D7
    description: "The committed evidence carries no host identifier, no address and no session-error-log content"
    verification:
      - kind: command
        ref: "grep -rIn -e \"$(hostname)\" -e 'xsession-errors' over the evidence tree -> no match outside the README's own warning"
        status: pass
    human_judgment: false

duration: 49 min
completed: 2026-09-06
---

# Phase 9 Plan 9: Release Close-out Summary

The settings window is reachable from the desktop it configures, key-to-documentation parity is a gate instead of a habit, and the 512 MB claim now covers the GUI on a measurement — 98.7 MB, against a research estimate of 30-60 MB.

## Accomplishments

- **D-11, both halves of the runtime one.** The window manager asks once at startup whether `wm2-config` is on its `PATH`, and the root menu carries a `Configure...` row at the end of its top level when it is. Selecting it goes through `spawnArgv()` — the same double-fork the `New` entry uses — so the existing reaping guarantee covers it with no new process code and no shell.
- **`DesktopEntry::findOnPath()` promoted** out of the anonymous namespace and declared in the header. It had internal linkage and one caller; it has two now, and the plan's `<correction_to_pattern_map>` was right that `09-PATTERNS.md` was wrong about it being reusable as it stood.
- **`include/RootMenuModel.h`** — the top level's index arithmetic, which four sites in `menu()` each carried their own copy of. Dependency-free, so a display-free case can pin every index in it.
- **`scripts/gates/doc-keys.sh`** — key-to-documentation parity, both directions, reading three sources for the accepted set. It found **ten** undocumented keys on its first run and has been shown to fail three ways.
- **Sixteen checklist rows** for this phase's surface: closed with a ctest label, a case title, an evidence path or a gate script; open with what is missing and why.
- **The measured remote-desktop pass** — TigerVNC 1.12.0, loopback, release build, three pages reached by real pointer input and screenshotted, with all four gates captured at the same commit.

## Task-by-task

**Task 1 (tracer, TDD) — Configure on the root menu.** Three commits.

- **RED** (`61dea99`): two `[wm_config_runtime]` cases against the tree as it stood. Two window managers whose child environments differ in exactly one variable — one with a directory holding the real built `wm2-config` on `PATH`, one without — compared by row count. Measured red: `rowsWith == rowsWithout + 1` expanded to `3 == 4`, and the selection case's sentinel never appeared because the last row was a category row that does nothing when released. Verified `RED_EVIDENCE_OK`.
- **GREEN** (`f3969cc`): the header promotion, the startup boolean, the index model, and the menu wiring.
- **PIN** (`764f8a3`): five display-free `[menupaint]` cases over `RootMenuLayout`.

The **tracer feedback gate** was evaluated per the workflow: auto mode is off, `human_verify_mode` is `end-of-phase`, and the tracer's `<verify>` carries only `<automated>` blocks — so the whole verify block was re-run end to end (`wm_config_runtime` 14/14; `wm_menulabel|wm_menureopen|wm_menuback|desktopentry` 22/22; `grep -c findOnPath include/DesktopEntry.h` = 1), it passed, and expansion continued without synthesising a checkpoint.

**Task 2 — the documentation guard** (`fe00c2b`). See "What the guard caught" below.

**Task 3 — the checklist rows** (`5f2af13`). Row count 103 → 119 at that commit, 121 after Task 4.

**Task 4 — the measurement and the evidence.** Three commits (`6d41481`, `03b0bbc`, `2d281de`) plus one fix (`8f4275b`).

## What the guard caught on its first run

The measure of whether `doc-keys.sh` was worth writing. Ten accepted keys the release notes had never presented as settings:

| Key | Why it was missing |
|---|---|
| `borders`, `button-background`, `menu-background`, `menu-borders`, `menu-foreground`, `tab-foreground` | the notes said "all nine colours … `tab-background`, `frame-background`, `menu-highlight` **and the rest**". Four of the nine were named nowhere in the document. |
| `menu-entry-name`, `menu-entry-command`, `menu-entry-category` | the manual menu entries were documented only as a single `wm2-ctl set menu-entries` value; the config-file form a user actually writes was absent |
| `menu-categories` | the read-only key plan 09-07 added, which 09-07's own summary flagged for this plan |

All ten are documented now. Direction 2 was already clean.

### Shown to fail, three ways

Transcripts committed under `evidence/remote-desktop/gates/`.

**1 — an accepted key nobody documented.** A throwaway row added to the option table:

```
wm2: doc-keys: the binary accepts 'throwaway-setting' and docs/RELEASE-NOTES.md never presents it as a setting
wm2: doc-keys FAILED with 1 problem(s):
exit=1
```

**2 — the rule-key coverage 08.5 checked by hand, in both directions at once.** `rule-skip-taskbar` renamed to `skip-taskbar-action` in the notes:

```
wm2: doc-keys: the binary accepts 'rule-skip-taskbar' and docs/RELEASE-NOTES.md never presents it as a setting
wm2: doc-keys: docs/RELEASE-NOTES.md presents 'skip-taskbar-action' as a setting and the binary does not accept it
wm2: doc-keys FAILED with 2 problem(s):
exit=1
```

**3 — a key documented only inside an HTML comment.** The same key moved into `<!-- ... -->`: still red, identically. A reader cannot see a comment, so a comment does not document.

Reverted after each; green again at 36 accepted keys.

## The number the research said must not be assumed

`09-RESEARCH.md`'s assumption log A3 called this *"the single most consequential unverified number in the document"*, estimated 30-60 MB, and instructed that no plan skip the measurement on the estimate's strength.

Measured on TigerVNC 1.12.0, `:147`, 1280x1024x24, `-localhost yes`, release build from commit `8f4275b`, with the settings window open and each of its three pages reached by XTEST and screenshotted:

| Process | Resident | Of 512 MB |
|---|---|---|
| `wm2-config`, first in the session | **101036 kB — 98.7 MB** | 19% |
| `wm2-config`, a second instance | 50580 kB — 49.4 MB | 10% |
| `wm2-born-again` | 12128 kB — 11.8 MB | 2% |
| `Xvnc` itself | 75936 kB — 74.2 MB | 14% |

**The estimate was low by roughly 40 MB.** The whole desktop is about 86 MB of a 512 MB machine, rising to about 189 MB while the settings window is open. The release notes state that plainly and point a memory-constrained user at `wm2-ctl` and the GTK-free `wm` package instead of softening it. A checklist row names what would have to change if it stopped being acceptable.

**One thing was measured and not explained.** The first `wm2-config` on a freshly started X server holds about twice what every later one on the same server holds — reproducibly, three runs on Xvfb and three on TigerVNC. Two candidate causes were tested and ruled out: not the per-user fontconfig cache (a fresh `HOME` on every run still shows the low figure from the second launch onward), and not the cost of being the first client on the server (an `xclock` connected first changes nothing). It is recorded as an open question in the README, in the checklist and in `WINDOWS.md`, and is the largest single unexplained term in the figure.

The automated detector is `[wm2_config_smoke]` "wm2-config's resident memory is measured against a stated budget", 160 MB, shown to fail at an 8 MB budget (`100964 <= 8192`). It reads the same `/proc` field through the same reader as `[wm_resource_budget]` — which is why that reader moved into the shared fixture header.

## For the operator — the plan's `<human-check>`, unanswered here

D-20's once-per-release pass carries four questions no automated check stands in for. This session was **loopback with no viewer attached**, so the first two are genuinely open:

1. Open `wm2-config` in the remote-desktop session and use each of the three pages. Does it feel usable over the link, or is it laggy enough that a user would give up?
2. Do the live changes appear on the desktop as fast as they do locally?
3. The recorded resident-memory figure for the GUI is **98.7 MB** — about a fifth of a 512 MB VPS while it is open. Is that acceptable, or does it change what this project should claim?
4. Confirm the evidence bundle carries no hostname, no address and nothing from a session error log. (The scan was run and is reported in the README; question 4 asks you to confirm it.)

`bash scripts/start-validation-session.sh :147` stands up the session; the screenshots under `evidence/remote-desktop/page-*.png` show what this run reached.

Carried forward from earlier plans and still needing the operator's eye: the Appearance page's "Reset this page" button sits below the fold at the default window size (09-07).

## Deviations from Plan

**1. [Rule 1 — Bug] A third `residentKb()` copy made the debug tree fail to build**
- **Found during:** Task 4, by the first of the four gates, at the commit the evidence was being captured against.
- **Issue:** `tests/test_wm_runtime.cpp` carried a third copy of the `/proc/<pid>/statm` reader. Moving `test_wm_resource.cpp`'s copy into `tests/support/WmFixture.h` made every call in that file ambiguous, because `using namespace wm2test` puts both in scope. Targeted builds of the two files I had edited did not reach it.
- **Fix:** removed the third copy; every call site unchanged.
- **Files modified:** `tests/test_wm_runtime.cpp`
- **Verification:** `build-all.sh debug` 539/539, `nogtk` 539 registered / 534 passed / 5 reasoned skips.
- **Commit:** `8f4275b`

**2. [Rule 2 — Missing critical] The capture script wrote this machine's hostname into committed evidence**
- **Found during:** Task 4, by the leakage scan the plan itself requires, run against this bundle's first draft.
- **Issue:** `scripts/capture-display-capabilities.sh` recorded `uname -n` in every `capabilities.txt` it has ever produced. That output is committed to a **public** repository, and a host identifier in committed evidence is a standing prohibition since 08.5 and one of this plan's own.
- **Fix:** the line is gone, with the reason at the site. Kernel and distro stay — they attribute a capture to a platform, which is what the block is for, and neither names the machine.
- **Files modified:** `scripts/capture-display-capabilities.sh`
- **Verification:** the regenerated bundle scans clean for the hostname.
- **Commit:** `03b0bbc`
- **NOT fixed, and recorded rather than hidden:** the captures already committed under `08-…/evidence/` and `08.5-…/evidence/` still contain it. Rewriting another phase's evidence is outside this plan's scope and the value is already in git history; logged in `WINDOWS.md` (id 28) and named in the bundle's README so the operator can decide.

**3. [Narrowing] "Present by label" is asserted as row count plus launch identity, not by reading the label**
- **Found during:** Task 1, writing the RED cases.
- **Issue:** the plan asks for a `[wm_config_runtime]` case "asserting the entry is present by label". The menu paints its labels with Xft; the server keeps pixels, not strings. There is no readable label channel, and the existing `[wm_menulabel]` machinery locates a highlight *band* by colour, which cannot identify text.
- **What was done instead:** the runtime cases assert the row COUNT (derived from the popup height the server reports and the row height measured off the highlight band) and the launch IDENTITY (releasing on the last row execs a program named `wm2-config`). The label claim, and the plan's separate requirement that "the index of the exit slot is unchanged", are carried by five display-free `[menupaint]` cases over the same `RootMenuLayout` that `src/Buttons.cpp` uses — which is a stronger assertion than a pixel test and cannot flake. Shown to fail when `configureIndex()` and `exitIndex()` are swapped.
- **Recorded in:** `WINDOWS.md` (id 25) and the runtime cases' own comment.

**4. [Narrowing] The selection case launches a shim, not the GTK binary**
- **Found during:** Task 1.
- **Issue:** the plan's acceptance asks for "a case selecting the entry and asserting a child appeared and was reaped". Launching the real GTK binary from inside the window manager's double-fork orphans it to init, so the test would have to discover a PID it did not create in order to stop it.
- **What was done instead:** the case puts a shim named `wm2-config` on the window manager's `PATH` that writes a filesystem witness and exits. What is under test is that the entry execs a program of that name through the window manager's spawn path; that GTK can open a window is `[wm2_config_smoke]`'s subject and is covered separately. The zombie assertion is unchanged. Stated in the case's comment and in the checklist row.

**5. [Rule 3 — Blocker] `--flag` harvesting had to come out of the doc-keys gate**
- **Found during:** Task 2.
- **Issue:** admitting every `--flag` token as a documented key reported `build`, `component`, `install`, `parallel` and `prefix` as settings the binary refuses — cmake's flags, from the packaging section's own command lines.
- **Fix:** that channel was removed and the measurement that justifies removing it is written into the script. The residual gap — a bare unhyphenated word in plain backticks that is not an accepted key — is stated inside the gate rather than hidden, and logged in `WINDOWS.md` (id 26). `borders` is the only single-word key the binary has.

**Total deviations:** 2 auto-fixed bugs/omissions, 2 recorded narrowings, 1 blocker resolved. **Impact:** none on the plan's truths. Every `must_haves.truth` holds, and the two narrowings are assertion *mechanisms*, not scope reductions — each substitutes a stronger, non-flaky assertion for one the medium cannot support, and each says so where a reader will meet it.

## Verification

| Check | Result |
|---|---|
| `ctest -L '^wm_config_runtime$'` | **14/14 passed** |
| `ctest -L '^(wm_menulabel\|wm_menureopen\|wm_menuback\|desktopentry)$'` | **22/22 passed** |
| `ctest -L '^menupaint$'` | **10/10 passed** (5 new) |
| `ctest -L '^(wm2_config_smoke\|wm_resource_budget)$'` | **55/55 passed** |
| `grep -c 'findOnPath' include/DesktopEntry.h` | **1** |
| `grep -c 'Configure' src/Buttons.cpp` | **6** |
| `bash scripts/gates/doc-keys.sh` | **OK**, 36 accepted keys, both directions clean |
| `bash scripts/gates/doc-keys.sh --help \| grep -c doc-keys` | **2** |
| `grep -c 'doc-keys.sh' docs/RELEASE-NOTES.md` | **2** |
| `grep -c -e wm2-config -e wm2-ctl -e _WM2_CONFIG_SOCKET CHECKLIST` | **23** (needs ≥ 3) |
| `grep -cE '^- \[[ x]\]' CHECKLIST` | **121**, from a 103 baseline |
| closed rows citing evidence in the file's form | **90** |
| `test -s evidence/remote-desktop/README.md && grep -cE '[0-9]+ (kB\|KB\|MB)'` | **14** |
| hostname / `xsession-errors` scan over the evidence tree | **no match** outside the README's own warning |
| `bash scripts/gates/build-all.sh debug` | **OK** — 539 registered, 100% passed, 0 warning lines |
| `bash scripts/gates/build-all.sh nogtk` | **OK** — 539 in both trees, 534 passed, 5 reasoned skips |
| `bash scripts/gates/install-components.sh` | **OK** — both manifests exact, `ldd` audit clean |

The two long gates were run at `8f4275b`; `git diff 8f4275b..HEAD -- src include tests apps CMakeLists.txt scripts packaging` is **empty**, so nothing they compile or execute changed afterwards. `doc-keys.sh` and `install-components.sh` were re-run at HEAD and are green.

## Process discipline

- **Every process this plan started was stopped by a PID it created.** The four in the remote-desktop pass — two `wm2-config` instances, the window manager and `Xvnc` — are named individually in `session-transcript.txt`. Nothing was selected, signalled or stopped by a name pattern or a process-table scan, in the capture or in any probe.
- **No network-exposing flag and no X access-control flag** anywhere. `Xvnc` ran `-localhost yes`; every Xvfb probe ran `-nolisten tcp`. Display `:147`, chosen from the free lock files, released cleanly.
- `~/.xsession-errors` was never read. <!-- planner-discipline-allow: xsession-errors -->
- Every commit message was written to a file and committed with `git commit -F`.

## Known Stubs

None. Everything this plan built is wired: the probe feeds the menu, the menu feeds the spawn, the gate reads the real option table, and the budget case reads the real process.

Three things are **recorded as open** rather than stubbed, each with a checklist row and a `WINDOWS.md` entry: the undiagnosed first-versus-later-launch split; the doc-keys gate's bare-word gap; and the carried-forward two-instance Save race and the un-gated `dlopen` claim from earlier plans.

## Issues Encountered

None beyond the deviations above. The host's memory pressure did not kill any run.

## Next Phase Readiness

- **The phase is complete.** All nine plans have summaries; D-11 was the last unimplemented decision.
- **CGUI-01 and CGUI-05** are both released by the shared-ID gate at this summary, which is the last plan declaring either.
- **`/gsd-ship` will find `WINDOWS.md` entries 25-28 open.** All four are deliberate records, not defects to fix before shipping; 28 (the hostname already committed under 08- and 08.5- evidence) is the only one with a security dimension and is the operator's call.
- **Phase 10** inherits two gates it did not have to write — `install-components.sh` and `doc-keys.sh` — and a checklist that names what is open rather than only what is finished.

## Self-Check: PASSED

Three created files present on disk (`include/RootMenuModel.h`,
`scripts/gates/doc-keys.sh`,
`.planning/phases/09-config-gui-ipc/evidence/remote-desktop/README.md`), and all
nine commit hashes named above resolve in `git log --all`. `commits: 10` in the
frontmatter is measured — `git rev-list --count ab59193..HEAD` — not narrated;
`ab59193` is the plan's recorded base. Ten rather than nine because the count
includes the metadata commit that carries this file: nine production and
documentation commits, plus the close-out.
