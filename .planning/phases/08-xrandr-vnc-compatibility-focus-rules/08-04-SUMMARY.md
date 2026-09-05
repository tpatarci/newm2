---
phase: 08-xrandr-vnc-compatibility-focus-rules
plan: 04
subsystem: window-management
tags: [x11, geometry, randr-prep, refactor, catch2, ctest, cmake, xvfb, ewmh]

requires:
  - phase: 08-xrandr-vnc-compatibility-focus-rules
    plan: 01
    provides: WmFixture (extended here), the D-33 tags-as-labels convention, tests/lsan.supp, the preflight fixture
  - phase: 08-xrandr-vnc-compatibility-focus-rules
    plan: 02
    provides: scripts/gates/build-all.sh and scripts/analysis/run-static-analysis.sh, used as this plan's verification evidence
  - phase: 08-xrandr-vnc-compatibility-focus-rules
    plan: 03
    provides: the capability-funnel shape this plan's accessor pair deliberately mirrors
provides:
  - WindowManager::screenWidth() / screenHeight() -- the single source of truth for screen geometry (D-27)
  - m_lastKnownScreenW / m_lastKnownScreenH -- the manager-owned cache plan 08-05 refreshes
  - tests/test_wm_geometry.cpp + the test_wm_geometry CMake target -- the phase's home for geometry coverage
  - A WmFixture that no longer hands the test's first window a recycled probe id, and that can report its own startup failures
affects: [08-05, 08-06, 08-07, 08-14]

actuals:
  tokens: 49098
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Geometry funnel: one accessor pair over a manager-owned cache, seeded once from Xlib, refreshed in exactly one later place"
    - "Pinning tests assert exact numbers derived from the source arithmetic, never ranges -- a range survives the off-by-one it exists to catch"
    - "Every pinning case is negative-tested by mutating the specific line it claims to pin"
    - "A process-level fixture retains every connection it opens, so resource-id ranges are never recycled into the test's own windows"

key-files:
  created:
    - tests/test_wm_geometry.cpp
  modified:
    - include/Manager.h
    - src/Manager.cpp
    - src/Buttons.cpp
    - src/Client.cpp
    - CMakeLists.txt
    - tests/support/WmFixture.h
    - .planning/phases/08-xrandr-vnc-compatibility-focus-rules/deferred-items.md

key-decisions:
  - "The cache is seeded in initialiseScreen() immediately after m_root and before setupEwmhProperties(), because that function publishes the initial _NET_WORKAREA from it"
  - "Client.cpp reaches the accessors through windowManager(), replacing five reads that passed a hardcoded screen index of 0 -- identical on this single-screen target, and now consistent with m_screenNumber"
  - "The geometry suite drives ensureVisible() with a synthetic root ButtonPress rather than XTEST, because case 4's oversized window leaves no root pixel for a pointer to land on and WindowManager::eventButton() applies no send_event guard"
  - "Case 5 pins the fullscreen SIZE and deliberately not its position, because the position the WM produces today is a defect (deferred item 8) and pinning it would cement the bug"
  - "Case 5 does not drive the clamp at all: the fullscreen transition leaves the client non-Normal, which sends circulate() into an unbounded loop (deferred item 6). Asserting there would have been green either way"

patterns-established:
  - "Read WM-produced geometry only after waking the WM's event loop -- pumpWm() creates and destroys an inert override-redirect window"
  - "Fixture members that hold X connections are declared last, so the constructor-throw path closes them before the server is killed"

requirements-completed: []

coverage:
  - id: D1
    description: "screenWidth()/screenHeight() are the single source of truth; no other translation unit reads Xlib's cached screen dimensions (D-27)"
    requirement: XDIS-01
    verification:
      - kind: other
        ref: "grep -v '^\\s*//' src/{Buttons,Client}.cpp | grep -c 'DisplayWidth|DisplayHeight' -> 0 each; src/Manager.cpp -> 1 each (the cache seed); grep -c screenWidth include/Manager.h -> 1"
        status: pass
      - kind: other
        ref: "grep -rc 'XRRQueryExtension|XRRSelectInput|XRRUpdateConfiguration' src/ include/ -> 0 for every file (no RANDR leaked into this plan)"
        status: pass
    human_judgment: false
  - id: D2
    description: "All 16 pre-existing direct reads are routed through the accessors"
    requirement: XDIS-01
    verification:
      - kind: other
        ref: "6 in src/Buttons.cpp, 6 in src/Client.cpp, 4 in src/Manager.cpp rewritten; grep -c 'screenWidth()' -> 3 in each of Buttons.cpp and Client.cpp"
        status: pass
    human_judgment: false
  - id: D3
    description: "With no resolution change, every menu-placement, clamp, fullscreen and workarea computation produces exactly its pre-refactor value"
    requirement: XDIS-01
    verification:
      - kind: integration
        ref: "tests/test_wm_geometry.cpp -- 5 [wm_geometry] cases asserting exact numbers (workarea 0,0,1280,1024; frame lands on the requested position; clamp to 1279-w / 1023-h; origin not negative; fullscreen exactly 1280x1024)"
        status: pass
      - kind: other
        ref: "full debug suite 154/154; build-all.sh green on debug + release + asan (154/154 each); the 26 pre-existing xft/client/ewmh/wm_process/wm_noshape cases unchanged"
        status: pass
      - kind: other
        ref: "negative-tested: screenWidth()/screenHeight() returning a wrong value reddens cases 1, 3 and 5; removing the `- 1` reddens case 3; removing the `< 0` guards reddens case 4; perturbing the map-time placement reddens cases 2 and 4"
        status: pass
    human_judgment: true
    rationale: "Menu placement (3 of the 6 Buttons.cpp reads) is covered structurally -- the diff is a pure callee swap with the `- 1` arithmetic preserved -- but is NOT exercised by an assertion, because driving the root menu needs XTEST navigation and the plan's five behaviours do not ask for it. A human should confirm the structural argument is acceptable, or defer it to 08-14's visual signoff."
  - id: D4
    description: "The accessors mirror the Shape funnel: one place to consult, one manager-owned cache seeded from Xlib, one later plan feeding it"
    requirement: XDIS-01
    verification:
      - kind: other
        ref: "declared immediately after hasShapeExtension() in include/Manager.h; members beside m_shapeEvent under the capability-sentinel convention 08-03 documented; the D-27 comment in src/Manager.cpp states the single-reader rule and names 08-05 as the refresher"
        status: pass
    human_judgment: false
  - id: D5
    description: "A fullscreen client is not repositioned by the clamp"
    requirement: XDIS-01
    verification:
      - kind: other
        ref: "NOT asserted. Measured: the fullscreen transition leaves the client non-Normal, so circulate() never reaches ensureVisible(); deleting the fullscreen guard leaves the suite green. Recorded as deferred items 6 and 8."
        status: partial
    human_judgment: true
    rationale: "The plan's fifth behaviour cannot be observed on today's code. Case 5 pins the load-bearing half (the fullscreen rectangle equals the accessor's screen size, negative-tested) and declines to fake the other half. Carried forward for /gsd-verify-work and for whoever fixes deferred item 8."

duration: 52min
completed: 2026-08-11
status: complete
---

# Phase 08 Plan 04: Screen-Geometry Accessor Funnel Summary

**All 16 scattered reads of Xlib's cached screen dimensions now go through one `WindowManager::screenWidth()`/`screenHeight()` pair over a manager-owned cache, and five negative-tested process-level cases pin the exact values the WM produces today so plan 08-05 can change *when* they are recomputed without changing *what* they are.**

## Performance

- **Duration:** 52 min
- **Started:** 2026-08-11T13:18Z
- **Completed:** 2026-08-11T14:10Z
- **Tasks:** 2
- **Files modified:** 7 (1 created, 6 modified)

## Accomplishments

- **Closed the blast radius before wiring RANDR.** 16 direct reads across three files became 2 accessor definitions and one cache seed. Plan 08-05 now has exactly one place to refresh, instead of sixteen places to remember.
- **Kept the commit purely mechanical, as the plan asked.** No arithmetic, variable name or conditional changed. Full suite 154/154 immediately, no new warnings, static analysis unchanged.
- **Corrected five reads that used a hardcoded screen index of 0** while the rest of the codebase used `m_screenNumber` — identical on this single-screen target, so behaviour-preserving, but no longer a discrepancy a multi-screen reader has to reason about.
- **Built a pinning suite that is actually load-bearing.** Every one of the five cases was proven to go red by mutating the specific line it claims to pin — the accessor, the `- 1`, the `< 0` guards, the map-time placement. Nothing here passes because the assertion is vague.
- **Fixed two real defects in the shared harness** that would have bitten every later plan (see Deviations): a resource-id collision that silently moved the test's first window to the origin, and a member-ordering bug that made a failed fixture startup kill the process before it could print its own diagnostics.
- **Found and documented four pre-existing WM defects**, one of them a permanent 100%-CPU freeze reachable by right-clicking the root of a freshly started WM.

## Task Commits

1. **Task 1: route all 16 screen-geometry reads through one accessor pair (D-27)** — `bd9f20c` (refactor)
2. **Task 2: pin the geometry values the accessors produce today** — `f44a138` (test)

## Files Created/Modified

- `tests/test_wm_geometry.cpp` — five `[wm_geometry]` cases on a 1280x1024 fixture display, plus the `pumpWm()` helper and the reasoning for every deliberate omission.
- `include/Manager.h` — the two accessor declarations beside `hasShapeExtension()`, and the two cache members beside `m_shapeEvent` under 08-03's capability-sentinel convention.
- `src/Manager.cpp` — the accessor definitions with the D-27 comment, the cache seed in `initialiseScreen()`, and the four rewritten workarea reads.
- `src/Buttons.cpp` — six rewritten reads (menu placement, submenu placement, geometry popup).
- `src/Client.cpp` — six rewritten reads (`manage()`, `setFullscreen()`, `ensureVisible()`).
- `CMakeLists.txt` — the `test_wm_geometry` target, own display, `ADD_TAGS_AS_LABELS` per D-33.
- `tests/support/WmFixture.h` — retained readiness connection, reordered connection members.
- `deferred-items.md` — items 6 to 9.

## Decisions Made

See `key-decisions` in the frontmatter. The two worth reading twice:

**Case 5 pins the fullscreen size and refuses to pin its position.** EWMH fullscreen on this WM produces a window sized exactly to the screen — which is the D-27 claim, and it goes red on a wrong accessor — but sitting at its old coordinates, because the reparent inside `stripForFullscreen()` triggers an unmap that `eventUnmap()` turns into a `withdraw()` back to the pre-fullscreen position. Writing `CHECK(x == 1100)` would have made the suite green and made the eventual fix fail a test for being correct.

**Case 5 does not drive the clamp, and says why at the point where it declines.** The plan's fifth behaviour is "a fullscreen client is not repositioned by the clamp". On today's code that cannot be observed: the fullscreen transition leaves the client non-Normal, `circulate()` therefore never reaches `ensureVisible()`, and — worse — it spins forever. Deleting the fullscreen guard entirely leaves this file green, which is the definition of a hollow assertion. It is carried as a `partial` coverage item rather than dressed up as a pass.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] WmFixture handed the test's first window the readiness probe's resource id**
- **Found during:** Task 2, when case 2 read the frame at `0,0` instead of the requested `300,220`
- **Issue:** `waitForWmReadyStaged()` opened a connection, created and destroyed a probe window on it, then let the connection close. Closing an X connection returns its whole resource-id range to the server, which hands it to the next client — so the test's first window came back with the probe's exact id. The WM was still unwinding the probe's `Client` at that moment, and that teardown issues `XReparentWindow(<that id>, root, 0, 0)` for a window it believes is already gone (`BadWindow` is suppressed at `src/Events.cpp:270` for exactly that reason). Against a recycled id the request *succeeds*, silently moving the test's brand-new window to the origin. It reads as "the WM placed my window at 0,0" and would have falsified any geometry assertion — including ones written by later plans.
- **Fix:** retain the readiness connection for the fixture's lifetime, so the test's connection is guaranteed a different resource base. No timing assumption, nothing to observe about how fast the WM processes a destroy.
- **Verification:** case 2 now reads the frame at exactly the requested position; proven by instrumenting `Client::Client()` to print the attributes it read (`0,0 240x180` before, `300,220 240x180` after).
- **Committed in:** `f44a138`

**2. [Rule 1 - Bug] A failed fixture startup killed the process before it could report the failure**
- **Found during:** Task 2, chasing the above — every failure printed `X connection to :120 broken` and *no Catch2 output at all*
- **Issue:** when `start()` throws, C++ destroys members in reverse declaration order, and the explicit ordering in `~WmFixture()` never runs. `m_keepAlive` was declared before `m_xvfb`, so Xvfb was killed while the connection was still open, Xlib's default IO-error handler called `exit(1)`, and the staged diagnostics 08-01 built specifically to explain such failures were never printed.
- **Fix:** declare the `x11::DisplayPtr` members last so they are destroyed first, with a comment recording the symptom.
- **Verification:** a deliberately failing startup now prints the full staged diagnostic block instead of one Xlib line.
- **Committed in:** `f44a138`

**3. [Rule 3 - Blocking] Geometry reads from an idle WM report the previous state**
- **Found during:** Task 2 — three of the five cases failed against a correct implementation
- **Issue:** the WM issues the frame's `XConfigureWindow` while handling an event and then blocks; the request is not observable on the server until the loop wakes for a later event. Measured with a client that moves itself and then goes quiet: the frame stayed put through 8 seconds of polling with no other X traffic, and moved the instant any unrelated event reached the WM.
- **Fix in the test only:** `pumpWm()` creates and destroys an inert `InputOnly`, override-redirect 1x1 window before each geometry read. Override-redirect is what keeps it inert — `eventCreate()` returns immediately for such windows, so it never enters `_NET_CLIENT_LIST` and cannot perturb the circulate order the cases depend on.
- **Not fixed in the WM:** behaviour change in an unrelated subsystem, and `nextEvent()` already calls `XFlush()` before `poll()`, so the cause is not yet understood. Recorded as deferred item 9.
- **Committed in:** `f44a138`

### Plan-text corrections (no code impact)

**4. `grep -c 'DISPLAY=:99' CMakeLists.txt` equals 4 — the literal is 5, unchanged**
Same correction 08-03 recorded. The criterion means "the four shared-display `ENVIRONMENT` bindings are unchanged", and that holds exactly: `grep -c 'ENVIRONMENT "DISPLAY=:99"'` is 4 before and after, and the new target adds none. The fifth textual occurrence is inside 08-01's `start_xvfb` readiness poll.

**5. The `-L '^wm_geometry$'` gate reports 6 tests, not the 5 the criterion predicts**
5 Catch2 cases plus the `preflight` fixture setup test ctest pulls in automatically. Same arithmetic as 08-01 and 08-03. All 6 pass.

**6. Task 2's `<behavior>` list could not be honoured literally in two places**
Behaviour 3 says a client whose *requested* geometry is partly offscreen is moved fully back on screen. At map time that is not what happens: `Client::manage()` runs a different clamp that only guarantees the frame indent stays on screen. The offscreen state is therefore established by a post-map configure request — which `Client::eventConfigureRequest()` applies with no clamp at all — and the clamp is then driven through `circulate()`, the only live route to `ensureVisible()`. Behaviour 5 is covered as `partial`; see Decisions.

---

**Total deviations:** 3 auto-fixed (2 harness bugs, 1 blocking test-observability issue), 3 plan-text corrections. No architectural decisions required; the four WM defects found along the way were all deferred rather than fixed, because this plan is a declared behaviour-preserving refactor.

## Issues Encountered

- **`cppcoreguidelines-init-variables` went 141 → 142.** Same mechanism 08-03 recorded at 140 → 141: the new test file is a 24th translation unit including `include/x11wrap.h`, whose line 309 diagnostic is counted once more. No uninitialised variable was added. Reported-only per 08-02's documented backlog.
- **The `RenderBadPicture` X error from 08-01 is still present** in process-level runs and still non-fatal.
- **The pre-existing Release-only `-Wunused-result` on `write()` in `sigHandler`** is the single warning line in `build/release/build.log` (deferred item 4).

## Known Stubs

None. Every artifact this plan claims exists and is exercised by a passing, negative-tested assertion. The two things this plan declines to assert (fullscreen position, fullscreen clamp guard) are stated as such in the test file, in this summary's coverage table, and in `deferred-items.md` — they are absent assertions, not stubbed ones.

## Deferred Issues

Recorded in `deferred-items.md` — all four found while writing the geometry suite, all pre-existing, none caused by this plan:

6. **`WindowManager::circulate()` spins forever when no client is Normal.** Measured 299 CPU ticks over 3 seconds of wall clock. The loop's only non-`break` exit is `j == i`, which can never match when `i` is `-1`. Reachable by right-clicking the root of a freshly started WM, because `m_clients` is non-empty (see item 7) but contains nothing Normal. Recommended for 08-07 … 08-10, the phase's next visit to `src/Buttons.cpp`.
7. **The WM manages its own menu, submenu and WM-check windows as clients**, publishing three phantom 1x1 entries in `_NET_CLIENT_LIST`. They are what makes item 6 reachable with no real windows open.
8. **EWMH fullscreen loses its position.** Correct size, old coordinates, Withdrawn state; traced to the implicit unmap from `stripForFullscreen()`'s reparent being turned into a `withdraw()`.
9. **The WM does not flush its X output until its loop wakes again.** Not yet explained — `nextEvent()` does call `XFlush()` before blocking. For a user this means a self-repositioning application looks frozen until something else happens on the desktop.

## Threat Flags

None. No new network endpoint, auth path, file-access pattern or trust-boundary schema change.

The plan's own register is addressed:
- **T-8-GEO (mitigate)** — the accessor pair is the only reader, enforced by the per-file zero-direct-read counts in Task 1's acceptance criteria and re-checkable with three greps.
- **T-8-STRUT (mitigate)** — the existing bounds are preserved rather than weakened; case 4 pins the negative-coordinate arms and is negative-tested by deleting them.
- **T-8-SC (accept)** — no dependency of any ecosystem added.

## Verification Results

| Plan verification step | Result |
|---|---|
| `ctest --test-dir build/debug --no-tests=error` (full suite) | PASS (154/154) |
| `ctest --test-dir build/debug -L '^wm_geometry$' --no-tests=error` | PASS (6/6 — 5 cases + preflight) |
| `ctest --test-dir build/asan -L '^wm_geometry$' --no-tests=error` | PASS (6/6, **0** sanitizer report files) |
| `bash scripts/analysis/run-static-analysis.sh` | PASS (exit 0; cppcheck 18/18 accounted for, 0 fatal clang-tidy) |
| `bash scripts/gates/build-all.sh` | PASS (debug + release + asan, 154/154 each; link audit 23 entries) |
| `cmake --build build/debug` with no new warnings | PASS (0 warnings in build/debug/build.log) |
| No file outside `src/Manager.cpp` reads Xlib's cached screen dimensions | PASS (0 in Buttons.cpp and Client.cpp; 1 each in Manager.cpp, the seed) |
| No RANDR work leaked into this plan | PASS (0 matches across all of `src/` and `include/`) |
| Negative check: wrong `screenWidth()`/`screenHeight()` | PASS (cases 1, 3, 5 go red) |
| Negative check: `- 1` removed from `ensureVisible()` | PASS (case 3 goes red) |
| Negative check: `< 0` guards removed from `ensureVisible()` | PASS (case 4 goes red) |
| Negative check: map-time placement perturbed | PASS (cases 2 and 4 go red) |

## Self-Check: PASSED

`tests/test_wm_geometry.cpp` exists on disk; both commit hashes (`bd9f20c`, `f44a138`) resolve in `git log`; the working tree is clean apart from this summary and the state files.

## Next Phase Readiness

- **08-05 has exactly one place to write.** `m_lastKnownScreenW` / `m_lastKnownScreenH` are seeded and read by nobody else; refreshing them from a live root-geometry query after `XRRUpdateConfiguration` or a root `ConfigureNotify` makes all 16 former call sites correct at once.
- **`test_wm_geometry` is ready to accumulate the resolution-change cases.** The target exists with its own display; the fixture already starts the server at 1280x1024, which is the RANDR maximum this server will allow, so 08-05 must shrink from there. `pumpWm()`, `serverRect()` and `awaitRect()` are written so the new cases need no restructuring.
- **08-05 must not drive the clamp on a non-Normal client** until deferred item 6 is fixed, or it will wedge the WM at 100% CPU. This is the one carried constraint that matters for the very next plan.
- **Carried planner assumption, unchanged:** the five reads that used a hardcoded screen index of 0 are treated as equivalent to `m_screenNumber` because the project targets a single screen. True today; stated in the release notes by 08-14.
- **Unchanged from 08-02:** bare `ctest --test-dir build/asan` remains red on the pre-existing fontconfig cache leak in three xft tests. Run the asan tree through the gate or with an anchored `-L`, as done here.

---
*Phase: 08-xrandr-vnc-compatibility-focus-rules*
*Completed: 2026-08-11*
