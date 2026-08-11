---
phase: 08-xrandr-vnc-compatibility-focus-rules
plan: 01
subsystem: testing
tags: [catch2, ctest, xvfb, xtest, asan, ubsan, lsan, x11, cmake, fontconfig]

requires:
  - phase: 06-ewmh-compliance
    provides: _NET_SUPPORTING_WM_CHECK / _NET_WORKAREA publication used as the readiness signal and the dock-strut assertions
  - phase: 03-client-lifecycle
    provides: x11::DisplayPtr RAII wrappers and the Xvfb-backed Catch2 test conventions the new harness follows
provides:
  - A buildable tree on this host (libxft-dev / libfontconfig1-dev / libxtst-dev installed, three fresh build trees)
  - WmFixture -- process-level RAII harness running the real wm2-born-again binary on a display it exclusively reserves
  - XTestDriver -- XTEST input synthesis on a dedicated, grab-impervious connection
  - scripts/preflight.sh + a blocking `preflight` ctest fixture
  - The D-33 tag-selection convention (Catch2 tags registered as ctest labels) for the whole phase
  - Fix + ASan proof for the eventDestroy heap-use-after-free
affects: [08-02, 08-03, 08-04, 08-05, 08-06, 08-07, 08-08, 08-09, 08-10, 08-11, 08-12, 08-13, 08-14]

actuals:
  tokens: 21400
  tasks: 3
  commits: 3

tech-stack:
  added: [libXtst (test-only), AddressSanitizer/UBSan build tree, LeakSanitizer suppressions]
  patterns:
    - "Process-level WM testing: fork/exec the real binary onto a flock-reserved private display"
    - "Readiness by property poll + event-loop round-trip, never sleep()"
    - "Catch2 tags as ctest LABELS, selected with anchored -L and --no-tests=error"
    - "Greppable regression invariants, with the literal tokens kept out of comment prose"

key-files:
  created:
    - scripts/preflight.sh
    - tests/support/WmFixture.h
    - tests/support/XTestDriver.h
    - tests/test_wm_process.cpp
    - tests/lsan.supp
  modified:
    - CMakeLists.txt
    - src/Events.cpp

key-decisions:
  - "eventDestroy fix cited as SCAN-02, not D-07: D-07 is already the harness decision in 08-CONTEXT.md and the Custom-category default in Phase 7, so reusing it would have created a false cross-reference"
  - "Dock-ness read through a `const Client& dying` alias so the `c->isDock()` grep guard is a true regression detector rather than something to waive"
  - "Xvfb started with -noreset plus a retained keepalive connection: the readiness probe's disconnect was resetting the server underneath the WM"
  - "Tab click point derived from the client's indent inside its frame, because the tab window is a shaped bounding box and geometry heuristics do not find it"
  - "press -> 200ms -> release instead of a back-to-back click, to avoid racing the WM's move-loop pointer grab"
  - "Pre-existing start_xvfb hang fixed here (Rule 3) because it blocked this plan's own -j4 gate"

patterns-established:
  - "Every ctest gate in Phase 8 selects by anchored label and carries --no-tests=error"
  - "Sanitizer options are setenv'd explicitly in the forked child, with log_path under the build tree and a distinctive exitcode so child findings surface as test failures"
  - "Negative-checking new tests: temporarily remove the behavior under test and confirm the test goes red"

requirements-completed: [TEST-05, TEST-07]

coverage:
  - id: D1
    description: "The tree configures and builds from a clean state on this host (Debug, Release, ASan/UBSan)"
    requirement: TEST-07
    verification:
      - kind: other
        ref: "cmake -S . -B build/{debug,release,asan} && cmake --build --parallel"
        status: pass
    human_judgment: false
  - id: D2
    description: "A Catch2 test launches the real wm2-born-again binary on a display it reserved itself and observes it reach readiness"
    requirement: TEST-05
    verification:
      - kind: integration
        ref: "tests/test_wm_process.cpp#WmFixture starts the real WM and it publishes _NET_SUPPORTING_WM_CHECK"
        status: pass
    human_judgment: false
  - id: D3
    description: "Destroying a dock window under the running WM recomputes _NET_WORKAREA and produces no AddressSanitizer report"
    requirement: TEST-05
    verification:
      - kind: integration
        ref: "tests/test_wm_process.cpp#Destroying a dock window restores _NET_WORKAREA with no sanitizer report"
        status: pass
      - kind: other
        ref: "ctest --test-dir build/asan -L '^wm_process$' --no-tests=error"
        status: pass
    human_judgment: false
  - id: D4
    description: "SIGTERM shuts the WM down cleanly with exit status 0 (not the ASan sentinel, not a signal death)"
    requirement: TEST-05
    verification:
      - kind: integration
        ref: "tests/test_wm_process.cpp#SIGTERM shuts the WM down cleanly with exit status 0"
        status: pass
    human_judgment: false
  - id: D5
    description: "scripts/preflight.sh exits non-zero when any declared dependency is unavailable, and runs as a blocking ctest fixture before any behavioral test"
    requirement: TEST-07
    verification:
      - kind: other
        ref: "PATH=<bash+coreutils only> bash scripts/preflight.sh -> 18 named failures, exit 1"
        status: pass
      - kind: integration
        ref: "ctest --test-dir build/debug -L '^wm_process$' runs `preflight` first"
        status: pass
    human_judgment: false
  - id: D6
    description: "Catch2 tags are registered as ctest labels for every target, and a zero-match gate fails loudly"
    requirement: TEST-07
    verification:
      - kind: other
        ref: "ctest --print-labels lists all retrofitted tags; -L '^zzz_nomatch$' --no-tests=error exits 8, exits 0 without the flag"
        status: pass
    human_judgment: false
  - id: D7
    description: "Synthesised XTEST input drives the real WM (activation observed via _NET_ACTIVE_WINDOW)"
    requirement: TEST-05
    verification:
      - kind: integration
        ref: "tests/test_wm_process.cpp#Synthesised XTEST input on a client's tab activates that window"
        status: pass
    human_judgment: true
    rationale: "Proven load-bearing (removing the XTEST calls turns it red), but it does NOT isolate the click from the pointer warp: click-to-focus is not yet a distinct behavior because the three focus booleans are parsed but unwired (D-15). A human should confirm this narrower claim is acceptable until FOCUS-02 lands."
  - id: D8
    description: "Concurrent process-level fixtures reserve distinct displays and never share :99 with the legacy targets"
    requirement: TEST-05
    verification:
      - kind: other
        ref: "40 concurrent test-binary runs, 0 failures; distinct displays :120-:123 observed; RESOURCE_LOCK on the four legacy targets"
        status: pass
    human_judgment: true
    rationale: "Stress-tested empirically rather than asserted by a dedicated test. Orphan cleanup after a SIGKILLed run is explicitly NOT guaranteed (carried planner assumption)."

duration: 103min
completed: 2026-08-11
status: complete
---

# Phase 08 Plan 01: Process-Level WM Harness and Environment Preflight Summary

**A Catch2 test now launches the real `wm2-born-again` binary on a display it flock-reserves for itself, drives a dock window through create/map/destroy, and proves under AddressSanitizer that the `eventDestroy` heap-use-after-free is gone.**

## Performance

- **Duration:** 103 min
- **Started:** 2026-08-11T11:37:44Z (first task commit at 11:20 UTC; plan resumed after a precondition halt)
- **Completed:** 2026-08-11T13:20:32Z
- **Tasks:** 3
- **Files modified:** 7 (5 created, 2 modified)

## Accomplishments

- **Unblocked the build.** `libxft-dev`, `libfontconfig1-dev`, `libxtst-dev` installed by the operator; the stale `build/` tree (which recorded `XFT_FOUND:INTERNAL=1` with an empty `CMAKE_BUILD_TYPE`) was quarantined and three fresh trees configured — Debug, Release, ASan/UBSan.
- **Fixed and *proved* the `eventDestroy` use-after-free.** Not merely patched: reverting the fix reproduces a heap-use-after-free at `Client::isDock()` (`include/Client.h:52`) called from `eventDestroy`, on memory freed by the owning `unique_ptr` erase. The hazard fires on **any** managed window destroy, not only docks, because the read happens regardless of the value returned.
- **Landed the process-level harness** all 13 remaining coverage items depend on, with no `sleep()`-based synchronisation anywhere and deadline-bounded, SIGKILL-escalating teardown.
- **Established the phase-wide tag-selection convention (D-33)** and proved the backstop is live: a bogus label now exits 8 instead of passing green-on-nothing.
- **Made a missing dependency fail loudly** at a named `preflight` fixture before any behavioral assertion runs.
- **Fixed a pre-existing suite-wide hang** that made `ctest` block forever after the tests finished.

## Task Commits

1. **Task 1: tracer — end-to-end dock destroy, ASan-clean** — `0d8f264` (feat)
2. **Task 2: preflight script and blocking ctest fixture** — `f15861d` (feat)
3. **Task 3: XTEST input driver and display-exclusivity guard** — `e66aa77` (feat)

## Files Created/Modified

- `tests/support/WmFixture.h` — RAII fixture: flock display reservation, Xvfb + WM child spawn, readiness by property poll plus an event-loop round-trip, staged failure diagnostics, bounded escalating shutdown.
- `tests/support/XTestDriver.h` — XTEST synthesis on a dedicated, grab-impervious connection.
- `tests/test_wm_process.cpp` — five `[wm_process]` cases against the real binary.
- `tests/lsan.supp` — X/font-stack leak suppressions.
- `scripts/preflight.sh` — the project's first shell script; establishes the shell conventions.
- `CMakeLists.txt` — `test_wm_process` target, preflight fixture, D-33 convention, CMake floor 3.16 → 3.20, `RESOURCE_LOCK` on the legacy `:99` group, `start_xvfb` hang fix.
- `src/Events.cpp` — the `wasDock` capture-before-erase fix.

## Decisions Made

See `key-decisions` in the frontmatter. The two most consequential:

1. **`SCAN-02`, not `D-07`, for the eventDestroy comment.** The plan said to cite in `// D-xx:` form, but D-07 already means the child-process harness (08-CONTEXT.md:46) and, in Phase 7, the Custom-category default. Reusing it would have planted a false cross-reference in production source.
2. **`-noreset` on the fixture's Xvfb.** The readiness probe opened and closed a connection; with zero clients remaining, Xvfb reset the server and the WM's own connection raced that reset. This produced a ~9% failure rate under 4-way concurrency that a less careful pass would have shipped as "flaky tests".

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Xvfb reset race in the new fixture (self-inflicted)**
- **Found during:** Task 1, concurrency stress after the tracer verify passed
- **Issue:** `spawnXvfb()` omitted `-noreset`, which the legacy `:99` fixture has always passed. `waitForXServer()` deliberately opens and closes a probe connection; with no clients left, Xvfb reset the server, and the WM intermittently could not connect. ~9% failure under `-j4`.
- **Fix:** Added `-noreset` and `+render`, plus a keepalive connection retained for the fixture's lifetime so the server never observes zero clients.
- **Verification:** 40 concurrent runs, 0 failures (was 3/32 before).
- **Committed in:** `0d8f264`

**2. [Rule 1 - Bug] Fixture could not explain its own failures**
- **Found during:** Task 1, diagnosing the above
- **Issue:** All startup failures reported one generic message with empty stderr, conflating four distinct stages.
- **Fix:** Added a `ReadyResult` stage enum and a `diagnostics()` dump (per-child pid/alive/exit/signal, both captured logs). This is what localised the reset race to `NoConnection`.
- **Committed in:** `0d8f264`

**3. [Rule 3 - Blocking] Pre-existing `start_xvfb` hang blocked Task 3's gate**
- **Found during:** Task 1, first full-suite run
- **Issue:** `start_xvfb` backgrounds Xvfb inside `bash -c` while it inherits ctest's stdout pipe and never closes it, so ctest blocks forever after "End testing". Confirmed pre-existing and untouched by this plan (`ctest -R '^start_xvfb$'` alone timed out). It blocked Task 3's required `ctest -L '^(wm_process|ewmh|client)$' -j4` criterion.
- **Fix:** Redirect the backgrounded server's output to a file; replace the flat `sleep 1` with a bounded poll on the server actually answering.
- **Verification:** `ctest -R '^start_xvfb$'` now completes in 0.11s; the full 143-test suite completes.
- **Committed in:** `e66aa77`

**4. [Rule 1 - Bug] Greppable acceptance invariants defeated by comment prose**
- **Found during:** Tasks 1 and 3
- **Issue:** Explanatory comments contained the very literals the acceptance greps count (`ADD_TAGS_AS_LABELS`, `DISPLAY=:99`, `TEST_PREFIX`, `c->isDock()`, `XTestGrabControl`, `XSendEvent`), so the comments broke the guards they documented.
- **Fix:** Reworded to describe the mechanisms without the literal tokens, and added an explicit in-file note warning future editors. The counts are now true regression detectors.
- **Committed in:** `0d8f264`, `e66aa77`

**5. [Rule 4-adjacent — resolved without architectural change] Two hollow tests caught and repaired**
- **Found during:** Task 3
- **Issue:** The XTEST expansion test initially passed with the click removed — `_NET_ACTIVE_WINDOW` was already the target from being mapped last. A second attempt used an EWMH client message to establish the precondition; that silently did nothing, leaving the test green for the wrong reason.
- **Fix:** Replaced with a precondition the WM genuinely produces (nothing active while the pointer is parked on root), then re-ran the negative check to confirm the test goes red without XTEST input.
- **Committed in:** `e66aa77`

---

**Total deviations:** 5 auto-fixed (3 bugs, 1 blocking, 1 test-validity).
**Impact on plan:** No scope creep. Four of the five were defects in work this plan itself produced, caught by stress-testing and negative-checking rather than by accepting a first green run.

## Issues Encountered

- **Precondition halt (resolved).** `sudo apt-get` was unavailable at the start; the plan halted rather than working around it. The operator installed the packages and execution resumed.
- **Stray X protocol error, non-fatal.** The WM logs `<request-code-139> (…): RenderBadPicture (invalid Picture parameter)` during process-level runs. It does not fail any test and did not affect assertions. Recorded in `deferred-items.md` — not investigated here as it is outside this plan's scope.

## Known Stubs

None. Every artifact this plan claims is implemented and exercised by a passing test.

## Verification Results

| Plan verification step | Result |
|---|---|
| `bash scripts/preflight.sh` exits 0, names every dep incl. `xrender` | PASS (xrender 0.9.10) |
| `ctest -L '^(wm_process\|preflight)$' --no-tests=error` | PASS (6/6) |
| `ctest --test-dir build/asan -L '^wm_process$'` with no ASan logs left | PASS (6/6, 0 logs) |
| Three consecutive full-suite `ctest -j4` runs, no flakes | PASS (143/143 × 3) |
| `git status --porcelain build` reports nothing | PASS |

Additional stress beyond the plan: 14 consecutive `-L '^(wm_process|ewmh|client)$' -j4` runs green; 40 concurrent raw test-binary runs green.

## Self-Check: PASSED

All 5 created files exist on disk; all 3 commit hashes resolve in `git log`.

## Next Phase Readiness

- The harness contract (`WmFixture`, `XTestDriver`) is proven and stable — plans 08-02 … 08-14 can build directly on it.
- `WmFixture` already accepts extra Xvfb args, extra WM args, and explicit child-env overrides, which is what 08-03/08-04 need for `-extension RANDR` / `-extension RENDER` and the `WM2_FORCE_NO_*` test levers.
- **Carried planner assumptions, unchanged and still unproven:** orphaned Xvfb cleanup after a SIGKILLed run is *not* guaranteed (the next run's allocator skips the leaked lock rather than colliding); `preflight` asserts only about the current host at the moment of the run.
- **Still missing for D-04's hard blockers:** `cppcheck` and `clang-tidy` are not installed on this host.

---
*Phase: 08-xrandr-vnc-compatibility-focus-rules*
*Completed: 2026-08-11*
