---
phase: 09-config-gui-ipc
plan: 08
subsystem: infra
tags: [cmake, packaging, install-components, xdg-desktop-entry, gtk3, ctest, gates]

requires:
  - phase: 09-04
    provides: wm2-ctl, the display-free client D-17 ships inside the window-manager package
  - phase: 09-06
    provides: the BUILD_CONFIG_GUI AUTO/ON/OFF option, the wm2-config target, and the skip-with-a-reason smoke cases
  - phase: 09-07
    provides: the Behaviour and Menu pages that made wm2-config the whole settings window this plan packages
provides:
  - "Two CMake install components, `wm` and `config-gui`, separately installable from one build tree (D-19)"
  - "packaging/wm2-born-again.desktop -- the session entry a display manager reads"
  - "packaging/wm2-config.desktop -- the Settings-category application entry (D-11's second half)"
  - "scripts/gates/install-components.sh -- component manifest gate plus the no-toolkit invariant (T-9-46, T-9-48)"
  - "bash scripts/gates/build-all.sh nogtk -- the GUI-disabled tree, with same-suite accounting (CGUI-05, T-9-49)"
  - "The packaging section of docs/RELEASE-NOTES.md, name-checked against CMakeLists.txt"
affects: [09-09, packaging, release-signoff, phase-10]

actuals:
  tokens: 37031
  tasks: 3
  commits: 6
  plan_head_before: 02b3ef7ba3331da9f9e15285428704e402801942

tech-stack:
  added: []
  patterns:
    - "CMake install COMPONENTs as the project's install identity, not the binary"
    - "Manifest-diff gate: assert a component's file list EXACTLY, never by containment"
    - "Same-suite accounting between two build trees: registered / accounted / reasoned"

key-files:
  created:
    - packaging/wm2-born-again.desktop
    - packaging/wm2-config.desktop
    - scripts/gates/install-components.sh
  modified:
    - CMakeLists.txt
    - scripts/gates/build-all.sh
    - tests/test_desktopentry.cpp
    - docs/RELEASE-NOTES.md

key-decisions:
  - "The `wm` component's install rules are four separate install() calls rather than two, because wm2-ctl is in the package for a different reason (D-17) than the window manager and the reason belongs beside the rule"
  - "The config-gui install rules sit inside if(CONFIG_GUI_ENABLED), so `grep -c '^install(' CMakeLists.txt` counts 4 of the 6 rules -- recorded rather than worked around by un-indenting code to satisfy a grep"
  - "The nogtk tree uses -DBUILD_CONFIG_GUI=OFF, not a doctored PKG_CONFIG_LIBDIR: OFF is the state that does not probe at all, and it is what a packager building a GTK-free package would set"
  - "The nogtk arm's reference count is the GUI-enabled debug tree's REGISTERED count from `ctest -N`, not a second full-suite execution -- enumeration gives the number exactly, and the gate's own debug arm proves those tests pass"
  - "The GUI's desktop entry carries TryExec, so a host with the window-manager package but not the config-gui package gets no menu entry rather than one that fails when clicked"

patterns-established:
  - "Gate scratch space: mktemp -d with a template UNDER the build tree, cleaned by an EXIT trap, never a fixed name in the system temporary directory"
  - "An invariant loop must fail when it examined zero subjects -- the same failure --no-tests=error prevents one level up"
  - "Tests that read XDG directories must build ABSOLUTE scratch paths; a relative XDG_DATA_HOME silently falls back to the developer's own data directory"

requirements-completed: []

coverage:
  - id: D1
    description: "The `wm` component installs the window manager, wm2-ctl, the session entry and the documentation, and nothing else; `config-gui` installs wm2-config and its application entry, and nothing else"
    requirement: CGUI-05
    verification:
      - kind: integration
        ref: "bash scripts/gates/install-components.sh"
        status: pass
      - kind: integration
        ref: "bash scripts/gates/install-components.sh build/nogtk (config-gui stages zero files, exit 0)"
        status: pass
    human_judgment: false
  - id: D2
    description: "No executable the `wm` component installs reports a GTK, GDK, GLib or GObject dependency, and the check has been shown to fail"
    requirement: CGUI-05
    verification:
      - kind: integration
        ref: "bash scripts/gates/install-components.sh (ldd over every executable in the wm manifest)"
        status: pass
      - kind: manual_procedural
        ref: "negative test: wm2-born-again linked against PkgConfig::GTK3 with -Wl,--no-as-needed -> gate exits 1 naming seven libraries; reverted"
        status: pass
    human_judgment: false
  - id: D3
    description: "The full suite passes with the GUI enabled and with it disabled, and the difference is exactly the GUI's own tests, which skip with a stated reason rather than vanishing"
    requirement: CGUI-05
    verification:
      - kind: integration
        ref: "bash scripts/gates/build-all.sh nogtk -> 531 registered in both trees, 528 passed, 3 skipped, all three reasoned"
        status: pass
      - kind: integration
        ref: "bash scripts/gates/build-all.sh debug -> 531/531"
        status: pass
    human_judgment: false
  - id: D4
    description: "Both shipped desktop entries parse with this project's own scanner, and the GUI's entry reaches the discovered-application list under Settings through the ordinary scanAll() path"
    verification:
      - kind: unit
        ref: "tests/test_desktopentry.cpp#the shipped wm2-config entry parses and lands under Settings"
        status: pass
      - kind: unit
        ref: "tests/test_desktopentry.cpp#the shipped wm2-config entry reaches the discovered-application list through the ordinary scanner"
        status: pass
      - kind: unit
        ref: "tests/test_desktopentry.cpp#the shipped wm2-config entry yields no menu entry when the binary is not installed"
        status: pass
    human_judgment: false
  - id: D5
    description: "A packager can produce both packages from docs/RELEASE-NOTES.md alone, and the documentation cannot silently disagree with the build system about their names"
    verification:
      - kind: other
        ref: "for c in wm config-gui; do grep -q \"COMPONENT $c\" CMakeLists.txt && grep -q \"$c\" docs/RELEASE-NOTES.md; done"
        status: pass
    human_judgment: true
    rationale: "The grep proves the two component names have not drifted apart. It cannot prove the staging instructions are followable by someone who has not just written them; a human should read the section once against a real packaging attempt."

duration: 52 min
completed: 2026-09-06
status: complete
---

# Phase 9 Plan 8: Two Install Components and the No-GTK Gate Summary

**Two CMake install components (`wm`, `config-gui`) with the window-manager component's freedom from GTK enforced by a manifest gate that has been shown to fail, plus a fourth build tree that proves the GUI-disabled build ran the same 531 tests rather than fewer.**

## Performance

- **Duration:** 52 min
- **Started:** 2026-09-06T14:18:45Z
- **Completed:** 2026-09-06T15:10Z
- **Tasks:** 3 (Task 1 was a TDD tracer: RED, then two GREEN commits)
- **Files modified:** 7 (3 created, 4 modified)

## Accomplishments

- **This project has install rules for the first time.** `grep '^install(' CMakeLists.txt` was empty before this plan. There are now six rules across two components, and the split is D-19's: `wm` carries the window manager, `wm2-ctl`, the session entry and the documentation; `config-gui` carries `wm2-config` and its application entry. A packager stages each with one `cmake --install --component` line from one build tree.
- **"The window-manager package does not depend on GTK" is a command now.** `scripts/gates/install-components.sh` stages both components, asserts each manifest is *exactly* the expected file list, and runs `ldd` over every executable the `wm` component installed, failing on `libgtk`, `libgdk`, `libglib`, `libgobject`, `libgio`, `libgmodule` or `libgthread`. It also fails if that loop examined zero executables.
- **The gate was shown failing before it was trusted**, and the first attempt to break it did not work — which is the more useful half of the result. See "Decisions Made".
- **`bash scripts/gates/build-all.sh nogtk` proves the GUI-disabled build ran the same suite**, not merely a green one: 531 tests registered in both trees, 528 passed, 3 skipped, `531 - 528 = 3 = 3`, and each of the three skips was re-run verbosely and confirmed to have stated a reason. A test that vanishes from the registry fails the gate; a test that skips silently fails the gate.
- **D-11's second half is checkable now rather than after somebody installs the package.** Four `[packaging]` cases parse the real shipped files, and one of them lays out a scratch XDG data root exactly as the install rule lays out a prefix and asserts the ordinary `scanAll()` — the same call the root menu makes — returns the entry under Settings.
- **The packaging section of the release notes** names both components with the spelling `CMakeLists.txt` uses, gives `BUILD_CONFIG_GUI`'s three values and its `AUTO` default, quotes verbatim the line a host without `libgtk-3-dev` sees, and gives the staging command for each component.

## Task Commits

1. **Task 1 (tracer, TDD) — RED** — `62a9cd9` (test): three `[packaging]` cases against files that did not exist yet; all three failed on `REQUIRE( in.good() )`. Verified `RED_EVIDENCE_OK` / `target_test_failed`.
2. **Task 1 — GREEN (a)** — `c2c6af4` (feat): the two `.desktop` files. 19 assertions in 3 cases, green.
3. **Task 1 — GREEN (b)** — `36242d1` (feat): the install components and `scripts/gates/install-components.sh`.
4. **Task 2** — `441d62d` (feat): the `nogtk` tree and the same-suite accounting in `build-all.sh`.
5. **Task 3** — `0606fb7` (docs): the packaging section and the two new gate commands.
6. **Deviation (Rule 2)** — `f5c27c2` (test): the scanner-composition case, plus the absolute-scratch-path fix it uncovered.

**Plan metadata:** see the `docs(09-08)` commit that follows this summary.

## Files Created/Modified

- `packaging/wm2-config.desktop` — application entry, `Categories=Settings;DesktopSettings;`, `TryExec=wm2-config`. Written against the fields `src/DesktopEntry.cpp` actually reads.
- `packaging/wm2-born-again.desktop` — session entry for a display manager. Installed under `share/xsessions`, which is deliberately not on the applications search path the scanner walks, so it can never appear in the root menu as an app to launch.
- `scripts/gates/install-components.sh` — stages both components into a `mktemp -d` directory under the build tree, diffs each manifest against an exact expected list, and enforces the no-toolkit invariant.
- `CMakeLists.txt` — `include(GNUInstallDirs)` and six `install()` rules across the two components (+78 lines; nothing else in the file changed).
- `scripts/gates/build-all.sh` — the `nogtk` tree, the accounting helpers, `run_suite` now tees to `build/<tree>/ctest.log`, and a header/usage rewrite explaining why the fourth tree is not in the default set.
- `tests/test_desktopentry.cpp` — four `[packaging]` cases and two RAII helpers (`StubBinDir`, `ScopedPath`).
- `docs/RELEASE-NOTES.md` — the "Installing and packaging" section and two new lines in the developers' command block. 67 insertions, 0 deletions, in exactly two hunks.

## Decisions Made

**1. The negative test needed `-Wl,--no-as-needed`, and that is worth writing down.**
The acceptance criterion asked for the toolkit assertion to be shown reddening against a deliberately toolkit-linked window manager. Adding `PkgConfig::GTK3` to the window manager's `target_link_libraries` **did not redden it**: with the toolchain default `--as-needed`, the linker drops `DT_NEEDED` for libraries no symbol uses, so no dependency entered the package and the gate was right to stay green. Adding `target_link_options(... -Wl,--no-as-needed)` produced a real dependency and the gate failed by name on seven libraries. The lesson is that this gate measures the **actual dependency**, not the build file's text — which is the correct thing to measure for a packaging claim, and also means a build-file-only reference that the linker elides is not something it will flag. A regression that would matter (someone including a GTK header and calling into it) produces a real `DT_NEEDED` and is caught. Both arms were reverted and the scratch tree removed; `git diff` over `CMakeLists.txt` confirms only the install block was added.

**2. Four `install()` rules at column 0, six in total.** The two `config-gui` rules sit inside `if(CONFIG_GUI_ENABLED)` and are therefore indented, so `grep -c '^install(' CMakeLists.txt` reports **4**, not 6. The criterion asked for at least 4 and is met, but by four genuinely separate `wm` rules (window manager; `wm2-ctl`; session entry; docs) rather than by un-indenting code to satisfy a grep. Splitting the two binaries apart is the better shape anyway: `wm2-ctl` is in this package for a different reason than the window manager, and the reason now sits beside its rule.

**3. The nogtk reference count comes from `ctest -N`, not a second suite run.** The plan asked to "record the number of tests each of the debug and GUI-disabled trees ran". The gate builds the GUI-enabled debug tree and reads its **registered** count with `ctest -N` rather than executing the whole suite a second time for a number enumeration already gives exactly; the gate's own `debug` arm is what proves those registered tests pass. Recorded here because it is a deliberate narrowing of the literal wording, not an oversight.

**4. `TryExec` is load-bearing, not decoration.** It is what makes a host with the window-manager package but not the config-gui package show no Settings entry rather than one that fails when clicked (T-9-47). A test asserts both directions.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical] A must-have truth had no test, only an inference**
- **Found during:** final verification, after Task 3
- **Issue:** The plan's sixth truth is that `wm2-config` appears under Settings in the discovered-application list *"through the existing scanner rather than through a special case"*. The three committed `[packaging]` cases proved the file parses to `Settings`, and the install gate proved the file lands in `share/applications` — but nothing exercised the composition through `scanAll()`, the call the root menu actually makes. The truth rested on an inference between two proven facts.
- **Fix:** A fourth `[packaging]` case lays out a scratch XDG data root exactly as the install rule lays out a prefix (`<root>/applications/wm2-config.desktop`), points `XDG_DATA_HOME` at it and `XDG_DATA_DIRS` at one nonexistent directory so nothing else can be found, and asserts the single entry `scanAll()` returns is this one, under Settings.
- **Files modified:** `tests/test_desktopentry.cpp`
- **Verification:** `ctest --test-dir build/debug -L '^desktopentry$'` → 16/16; the same label under ASan/UBSan → 16/16 with no sanitizer report files.
- **Committed in:** `f5c27c2`

**2. [Rule 1 - Bug] The test scratch directories were relative, and the scan read the developer's own data directory**
- **Found during:** the deviation above, which went red at `REQUIRE( apps.size() == 1 )` expanding to `6 == 1`
- **Issue:** `StubBinDir` and the new case built scratch paths with `mkdtemp` against the working directory, producing **relative** paths. `DesktopEntry`'s XDG helpers discard any data directory that is not absolute, so a relative `XDG_DATA_HOME` fell through to `$HOME/.local/share/applications` and the scan returned six of the developer's real desktop files. That is wrong twice over: the assertion would have been about whatever that machine happened to contain, and it breaks this project's standing rule against tests touching the real configuration.
- **Fix:** A shared `makeScratchDir()` helper returns an absolute path built from `getcwd()`; both call sites use it. The comment above it says why, because the failure mode is silent.
- **Files modified:** `tests/test_desktopentry.cpp`
- **Verification:** the case now returns exactly one entry; no scratch directory is left behind after the run.
- **Committed in:** `f5c27c2`

---

**Total deviations:** 2 auto-fixed (1 missing-critical verification, 1 bug in the new test helpers). Both are in test code; no production behaviour changed as a result.
**Impact on plan:** None on scope. Both strengthen a claim the plan already made.

## Issues Encountered

**The RED-evidence checker does not read Catch2's output.** `gsd-tools check tdd-red-evidence` parses `node --test`'s TAP summary block (`# tests N` / `# pass N` / `# fail N`). Catch2's TAP reporter emits `ok`/`not ok` lines and a `1..N` plan but no such block, so the first three attempts classified a genuine 3-case failure as `zero_tests_discovered`. Resolved by appending the summary lines computed from the real `ok`/`not ok` counts of the actual run — an adapter, not a fabrication; the verdict then read `RED_EVIDENCE_OK` / `target_test_failed`. Worth knowing for any future C++ TDD task in this repository.

**Release and ASan were last run in full at 530 tests.** The full default gate (`bash scripts/gates/build-all.sh`, all three signoff trees) ran green at commit `441d62d`: **530/530 in each tree**, release link audit clean at 24 entries, no sanitizer findings, 0 warning lines. The 531st test — a display-free desktop-entry case — was added afterwards as deviation 1. The `debug` and `nogtk` arms were re-run in full at the final commit (531/531 and the 531/528/3 accounting), and the added case was re-run in the **ASan** tree by label with sanitizer logging on (16/16, no report files); the **release** suite was not re-run at 531. No production source changed after `441d62d`, so the release link audit's subject is byte-identical. Recorded in `.planning/WINDOWS.md` as an `unrun-verify` entry rather than glossed over.

**CGUI-05 is not marked complete, deliberately.** `requirements.ready-ids` reports it **blocked**: plan 09-09 also declares CGUI-05 and has no SUMMARY yet, so the shared-ID gate holds the requirement Pending until the last plan declaring it finishes. That is the gate working, not a failure. `REQUIREMENTS.md` is therefore unchanged by this plan.

## Verification Results

| Check | Result |
|---|---|
| `bash scripts/gates/install-components.sh` | **OK** — both manifests exact; `wm2-born-again` and `wm2-ctl` report no GTK/GLib/GObject linkage |
| the same gate, shown failing | **exit 1**, naming `libgtk-3`, `libgdk-3`, `libgdk_pixbuf-2.0`, `libgio-2.0`, `libgobject-2.0`, `libglib-2.0`, `libgmodule-2.0` (break reverted) |
| `bash scripts/gates/install-components.sh build/nogtk` | **OK** — `config-gui` staged nothing and exited 0 |
| `bash scripts/gates/build-all.sh` (no argument) | **OK: debug release asan** — exactly the three signoff trees; 530/530 each at `441d62d`; link audit OK (24 entries); no sanitizer findings; 0 warning lines |
| `bash scripts/gates/build-all.sh debug` (final commit) | **OK** — 531/531, 0 warning lines |
| `bash scripts/gates/build-all.sh nogtk` (final commit) | **OK** — 531 registered in both trees, 531 attempted, 528 passed, 3 skipped, `531 - 528 = 3 = 3`, all three reasoned |
| `bash scripts/gates/build-all.sh --help \| grep -c nogtk` | **2** |
| `ctest -L '^desktopentry$'` | **16/16** (debug), **16/16** (ASan, no report files) |
| `grep -c '^install(' CMakeLists.txt` | **4** (of 6 rules; see Decision 2) |
| component-name parity loop, CMakeLists vs RELEASE-NOTES | **passes** |
| `grep -c 'BUILD_CONFIG_GUI' docs/RELEASE-NOTES.md` | **1** |
| `grep -c -e install-components.sh -e 'build-all.sh nogtk' docs/RELEASE-NOTES.md` | **3** |
| `git diff --stat docs/RELEASE-NOTES.md` | **67 insertions, 0 deletions**, two hunks |
| gate scratch space | `mktemp -d` under the build tree; `grep '/tmp' scripts/gates/install-components.sh` → no match |

### The flagged assumption from the plan's edge-probe accounting

The plan surfaced this as an unresolved assumption rather than a criterion: *"no runtime path in the window manager ever attempts to load a toolkit library dynamically."* It cost one command to check and it holds — `grep -rn 'dlopen\|dlsym\|dlvsym' src/ include/ apps/wm2-ctl/` returns nothing, so there is no dynamic-loading call in the window manager or `wm2-ctl` at all, and `ldd build/release/wm2-born-again | grep -cE 'libgtk|libgdk|libglib|libgobject'` is `0`. Recorded here so the assumption is closed rather than carried forward. It is not gated by a script; a future `dlopen` would need one.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- **09-09 can run its evidence capture against real gates.** `bash scripts/gates/build-all.sh nogtk` and `bash scripts/gates/install-components.sh` both exist, both are green, and both print the numbers 09-09 needs to quote.
- **CGUI-05 and CGUI-01 both close in 09-09**, which is the last plan declaring either. The shared-ID gate will release CGUI-05 when 09-09's SUMMARY lands.
- **One open ledger entry:** the release suite was not re-run at 531 tests (`.planning/WINDOWS.md`, `unrun-verify`). Re-running `bash scripts/gates/build-all.sh` once during 09-09's capture closes it and would produce the release-signoff evidence at the final commit in the same run.
- **Nothing blocks phase 10.** The install components are where a second, X11-native configuration tool would add a third component or join `config-gui`; the manifest gate's expected-file lists are the one place that would need editing, by design.

---
*Phase: 09-config-gui-ipc*
*Completed: 2026-09-06*

## Self-Check: PASSED

All eight files named in `key-files` exist on disk; all six task commits
(`62a9cd9`, `c2c6af4`, `36242d1`, `441d62d`, `0606fb7`, `f5c27c2`) are present in
`git log`. `git rev-list --count 02b3ef7..HEAD` = **6**, measured, matching the
`actuals.commits` above. Every acceptance criterion in the plan was re-run at the
final commit except the release and ASan full suites, which are recorded above and
in `.planning/WINDOWS.md` rather than claimed.
