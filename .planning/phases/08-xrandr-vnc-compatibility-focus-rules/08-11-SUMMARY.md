---
phase: 08-xrandr-vnc-compatibility-focus-rules
plan: 11
subsystem: testing
tags: [process-level-tests, window-lifecycle, size-hints, window-gravity, asan, use-after-free, denial-of-service, xft]

requires:
  - phase: 08-xrandr-vnc-compatibility-focus-rules/08-01
    provides: "tests/support/WmFixture.h, tests/support/XTestDriver.h, tests/lsan.supp -- the process-level harness every case here runs on"
  - phase: 08-xrandr-vnc-compatibility-focus-rules/08-02
    provides: "scripts/gates/build-all.sh and scripts/analysis/run-static-analysis.sh -- the gates this plan's evidence comes from"
  - phase: 08-xrandr-vnc-compatibility-focus-rules/08-07
    provides: "the spaced settleWm() methodology, without which every negative assertion here would read stale values (deferred item 9)"
  - phase: 08-xrandr-vnc-compatibility-focus-rules/08-08
    provides: "the revert-chain scrub in eventDestroy and the map-time focus grant that makes a freshly mapped client active -- both preconditions these cases rely on"
provides:
  - "tests/test_wm_lifecycle.cpp and the test_wm_lifecycle target: 17 process-level cases in three groups ([wm_lifecycle], [wm_sizehints], [wm_gravity])"
  - "Checklist coverage items 2, 4, 8 and 9 closed against the real binary, green in the debug AND sanitizer builds"
  - "Client::fixResizeDimensions() hardened against client-controlled size hints (threat T-8-DOS): per-axis increment validation, normalised inverted maximum, strictly-positive floor"
  - "A third heap-use-after-free fixed in the client destruction path (threat T-8-UAF): the focus candidate is now scrubbed regardless of the tracking flag"
  - "Border::~Border releases its XftDraw before destroying the drawable it is bound to -- one RenderBadPicture per closed window, gone"
  - "An X-protocol-error assertion idiom: the WM logs and continues after a protocol error, so an entire defect class is invisible without it"
  - "deferred item 13: the destroy-path BadWindow, traced to the resize handle being a child of the client window"
affects: [08-12, 08-13, any later work touching Client::manage(), Client::resize(), Client::gravitate(), Border teardown or WindowManager::eventDestroy]

actuals:
  tokens: 19965
  tasks: 3
  commits: 7

tech-stack:
  added: []
  patterns:
    - "Asserting on the WM's X protocol error log, because errorHandler() logs and returns 0 -- the WM survives a protocol error, so nothing about windows or properties can notice one"
    - "Locating a frame sub-window by asking the server for the client's children rather than computing where it ought to be, so a frame-thickness change cannot silently move a synthetic press off target"
    - "Proving a vector-to-vector transfer through the ORDER of the published client list, since membership alone cannot distinguish 'moved between the two lists' from 'left where it was and merely marked'"
    - "Pinning the WM's internal coordinates against the server by provoking a synthetic ConfigureNotify -- the only route by which an un-inverted placement adjustment is observable"

key-files:
  created:
    - tests/test_wm_lifecycle.cpp
  modified:
    - CMakeLists.txt
    - src/Client.cpp
    - src/Border.cpp
    - src/Events.cpp
    - .planning/phases/08-xrandr-vnc-compatibility-focus-rules/deferred-items.md

key-decisions:
  - "The focus-candidate destroy case stretches the auto-raise delay to 20s rather than shortening it as the plan suggested. The branch under test is reachable only while the deadline is still pending, and stretching it makes that precondition OBSERVABLE from outside the WM -- if the candidate had already been focused the active window would have changed. A short delay would have raced the test into the far less interesting post-expiry path and made the precondition an assumption instead of an assertion."
  - "The protocol-error assertion excludes BadWindow in every case, not only the destroy cases. WmFixture destroys its own readiness probe during startup, so the pre-existing teardown BadWindow (deferred item 13) can surface in a case that destroys nothing of its own. Excluding one error CODE is a far smaller concession than excluding the assertion -- and every other class still fails, which is exactly what caught the XftDraw defect."
  - "The hidden-list cases assert the published client-list ORDER as a proxy for the vector transfer, explicitly NOT as a claim about EWMH ordering (which asks for initial mapping order, and this is not that). Without it both cases stayed green with the transfer deleted."
  - "The unhide case restores the LAST hidden entry rather than the first. Restoring the first leaves the published order unchanged whether or not the pointer moved back, so it could not tell the transfer from a no-op."
  - "The inverted-maximum normalisation was implemented as the plan mandates and recorded as an EQUIVALENT MUTANT rather than covered by a contrived test: the trailing minimum floor already produces the identical geometry. Writing a test that appeared to cover it would have been a test that did not test its own name."
  - "A negative resize increment is recorded as arithmetically harmless (the sign cancels between the divide and the multiply) and its section documents that it passes with or without the guards. The genuinely dangerous value is ZERO, per axis, and the two sections that cover it each redden for their own guard and not the other's."

patterns-established:
  - "Mutation testing extended to a coverage-only plan: 26 production branches deleted in turn, 22 confirmed to redden a named case, 4 recorded as equivalent with the mechanism explained"
  - "When a mutation fails to redden, the response is to strengthen the test until it does -- three assertions in this plan exist only because a mutation exposed them as missing"

requirements-completed: []

coverage:
  - id: D1
    description: "Destroying a normal managed client leaves no stale client-list entry, never leaves the destroyed window named as active, and does not crash or trip the sanitizer"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_lifecycle.cpp#Destroying a mapped focused client leaves no stale list or active-window entry"
        status: pass
      - kind: e2e
        ref: "tests/test_wm_lifecycle.cpp#Destroying one of three clients removes exactly that one, in order"
        status: pass
    human_judgment: false
  - id: D2
    description: "Destroying a client while it is the live focus candidate is memory-safe -- the WM's focus-tracking record cannot outlive the client it names"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_lifecycle.cpp#Destroying a client while it is the live focus candidate does not crash (ASan tree; mutation ML1 reddens 5 cases)"
        status: pass
    human_judgment: false
  - id: D3
    description: "Hiding and unhiding a client moves it between the visible and hidden lists, keeps the hidden state and the client list accurate, and unmaps/remaps the frame"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_lifecycle.cpp#Hiding a client moves it to the hidden list, marks it hidden and unmaps its frame"
        status: pass
      - kind: e2e
        ref: "tests/test_wm_lifecycle.cpp#Unhiding a client moves it back to the visible list, clears the state and remaps"
        status: pass
      - kind: e2e
        ref: "tests/test_wm_lifecycle.cpp#Destroying a hidden client removes it from the list without a sanitizer report"
        status: pass
    human_judgment: false
  - id: D4
    description: "Every size-hint constraint the resize path honours -- minimum, maximum, base, increments, fixed-size, and degenerate or contradictory declarations -- is asserted against the real binary through a real drag"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_lifecycle.cpp -- 7 [wm_sizehints] cases; mutations MS1/MS2/MS4/MS5/MS6/MS7/MS9 each redden a named case"
        status: pass
    human_judgment: false
  - id: D5
    description: "A client cannot kill the window manager, or obtain an absurd window, by declaring degenerate size hints (threat T-8-DOS)"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_lifecycle.cpp#A zero, negative or absurd resize increment neither divides by zero nor produces a nonsensical size -- RED before the fix (WM dead on width_inc=0; 64536x64536 window on a negative base), green after"
        status: pass
    human_judgment: false
  - id: D6
    description: "Every window gravity mode the placement path handles puts the client where that mode implies, and no handled mode is silently treated as unknown"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_lifecycle.cpp#Every handled window gravity places the client where that mode implies (10 modes; mutations MG1/MG2/MG3 redden)"
        status: pass
      - kind: e2e
        ref: "tests/test_wm_lifecycle.cpp#A client declaring no gravity is placed as NorthWest"
        status: pass
    human_judgment: false
  - id: D7
    description: "An unhandled gravity value places the window without a negative coordinate and says so on stderr rather than doing something arbitrary in silence"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_lifecycle.cpp#An unhandled gravity value places the window without a negative coordinate (mutation MG4 reddens)"
        status: pass
    human_judgment: false
  - id: D8
    description: "The placement adjustment is its own inverse: a window mapped, unmapped and remapped three times neither moves on screen nor drifts in the coordinates the WM publishes to the client"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_lifecycle.cpp#The placement adjustment is its own inverse across map/unmap cycles (mutations MG5/MG6 redden)"
        status: pass
    human_judgment: false
  - id: D9
    description: "Closing a window no longer emits a RenderBadPicture, and the WM emits no protocol error other than the one deferred item 13 records"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_lifecycle.cpp -- all six [wm_lifecycle] cases assert on xProtocolErrorsExceptBadWindow(); mutation ML10 reddens all six"
        status: pass
      - kind: manual_procedural
        ref: "bare WM plus one client on Xvfb :171 -- RenderBadPicture present before the fix, absent after, same command"
        status: pass
    human_judgment: false
  - id: D10
    description: "Whether the project's chosen resolution of contradictory size hints -- an inverted maximum resolving UP to the minimum, a non-positive increment meaning 'do not quantise' -- is the behaviour a user would want from an application that declares them"
    verification: []
    human_judgment: true
    rationale: "ICCCM does not specify a resolution for either contradiction, so the plan's own planner_assumptions flagged this as project policy rather than conformance. The tests pin the documented policy and prove it is safe; whether it is the RIGHT policy -- an application that says min 300 and max 100 gets a 300-wide window rather than a 100-wide one -- is a judgment about intent that no test settles. Flagged for /gsd-verify-work."

duration: 75min
completed: 2026-08-29
status: complete
---

# Phase 08 Plan 11: Lifecycle, Size-Hint and Gravity Coverage Summary

**Four checklist coverage gaps closed against the real binary — and in the closing, three defects that had been shipping in silence: a one-property-write denial of service that kills the whole window manager, a client-triggerable 64536-pixel window, and a use-after-free in the destruction path that the previous plan's fix had not reached.**

## Performance

- **Duration:** ~75 min
- **Tasks:** 3 of 3
- **Files created:** 1; modified: 5
- **Commits:** 7

## Accomplishments

- **17 process-level cases in one new file**, in three tag groups, closing checklist coverage items 2 (managed window destroy), 4 (hidden-list transfers), 8 (all `XSizeHints` resize constraints) and 9 (all window gravity modes). Green in the debug tree, green under the sanitizer, stable across three consecutive runs of each group.
- **Three genuine defects found and fixed**, two of them security-relevant, one of them fatal to the WM. Every one was found by an assertion the plan asked for, not by reading code speculatively.
- **26 mutations run; 22 reddened a named case, 4 recorded as equivalent** with the mechanism spelled out rather than papered over with a contrived test. Three of this plan's assertions exist *only* because a mutation exposed them as missing — including two in cases that had looked complete.
- **The size-hint group drives real drags on the frame's resize handle**, not configure requests. The constraint arithmetic sits on `Client::resize()`'s path and nowhere else, so the easier route would have tested something else entirely.
- **Zero build warnings introduced**; `cppcheck` still at its 18-finding baseline; clang-tidy `bugprone-branch-clone` still 6.

## Task Commits

1. **Task 1: destroy and hidden-list coverage** — `cb9520a` (test) → `5b38218` (fix, XftDraw teardown) → `495138f` (fix, focus-candidate scrub) → `4ade87b` (docs, deferred item 13)
2. **Task 2: size-hint resize constraints** — `62afbf7` (test/RED) → `53167ec` (fix/GREEN, `fixResizeDimensions` hardening)
3. **Task 3: window gravity** — `b53b8b1` (test)

## The three defects

### 1. A zero resize increment kills the window manager (threat T-8-DOS)

`Client::fixResizeDimensions()` divided by `m_sizeHints.width_inc` and `height_inc` unconditionally. `WM_NORMAL_HINTS` is a property **any client can write, with any values**, so a single property write plus one drag on the resize handle is an integer divide by zero and the whole session is gone. Measured: `REQUIRE(fx.wmAlive())` false, the WM child dead.

Fixed with a **per-axis** guard, and the per-axis part is not fussiness: a client may declare a valid increment on one axis and a degenerate one on the other, and a shared guard would either let the bad axis divide or throw away the good axis's quantisation. The two sections that cover it are deliberately mirror images — deleting the width guard reddens one and not the other, and vice versa.

### 2. A negative base size buys a 64536×64536 window (threat T-8-DOS)

The quantisation origin is the base size, which a client may declare negative. A base of −1000 with an increment of 100000 rounds the multiple to zero and leaves the result **at the base** — a large negative `int`, handed straight to `XMoveResizeWindow`, whose width parameter is `unsigned`. **Measured on a 1024×768 screen: `(45,28 64536x64536)`**, plus twelve `BadMatch` errors as the frame tried to follow.

This was the plan's own prediction ("cases 6 and 7 are the ones most likely to find a genuine defect") landing exactly where it said it would. Fixed with a final strictly-positive floor, placed last so it also catches a negative minimum arriving through the floors above it.

### 3. A third heap-use-after-free in the destruction path (threat T-8-UAF)

The sanitizer gate caught this one on the new cases:

```
READ  Client::root()                    src/Client.cpp:94
      Client::selectOnMotion            src/Client.cpp:1211
      WindowManager::stopConsideringFocus  src/Manager.cpp:1260
      WindowManager::considerFocusChange   src/Manager.cpp:1213
      Client::eventEnter                src/Client.cpp:1705
freed WindowManager::eventDestroy       src/Events.cpp:351
```

`m_focusChanging` and `m_focusCandidate` are **set together but cleared apart**: `stopConsideringFocus()` clears the flag and leaves the pointer, and `Client::focusIfAppropriate()` calls it on the ordinary success path. `eventDestroy`'s STEP 1 read the *flag* first — so by the time a client was destroyed with tracking already stopped, the guard declined to clear the very pointer that was about to dangle.

Fixed by scrubbing whenever the dying client **is** the recorded candidate, flag or no flag — the same discipline as 08-08's `skipInRevert()` scrub two statements below it, for the same reason. The exact interleaving that revived `m_focusChanging` afterwards was not pinned down and is recorded as such; what is proven is the dangling read, both its endpoints, and that the unconditional scrub removes it. Rate before the fix: **2 failures in 6 runs of the group**. After: 0 in 13.

### And one that was hiding behind a survivable error

Not a crash, not a wrong pixel — just a line on stderr for **every managed window ever closed**:

```
wm2: <request-code-139> (0x...): RenderBadPicture (invalid Picture parameter)
```

`Border::~Border()` destroyed `m_tab` and then reset `m_tabDraw`, which is created bound to `m_tab`. `XftDrawDestroy` frees the RENDER Picture for that drawable, so the free named an id the server had already reclaimed. Reproduced against a bare WM with a single client, so not a test artefact. Two lines moved; the mutation that restores the old order reddens all six `[wm_lifecycle]` cases.

**This one is the argument for the protocol-error assertion.** `WindowManager::errorHandler()` logs and returns 0 — the WM *survives* a protocol error — so an entire class of teardown defect is invisible to every assertion about windows, properties and geometry. Nothing else in this file, or in any suite before it, would have noticed.

## Where mutation testing changed the tests

Three assertions in this plan exist only because a deletion failed to redden.

| Mutation | First result | What was added |
|---|---|---|
| `addToHiddenList()` / `removeFromHiddenList()` deleted | **green** | The hidden-list cases asserted membership, which cannot distinguish "moved between the two vectors" from "left where it was and merely marked hidden". They now assert the published client-list ORDER, and the unhide case restores the LAST hidden entry rather than the first — restoring the first leaves the order unchanged either way. |
| Both `gravitate()` calls in `withdraw()` deleted | **green** | The remap path reparents into a frame that never moved, so the on-screen position cannot drift even when the WM's coordinates do. The round-trip case now also provokes a synthetic `ConfigureNotify` and checks the position the WM *publishes* against the server's. |
| `case CenterGravity: case StaticGravity:` deleted | would have been green | Both apply a net offset of exactly zero — the same as the unhandled fallback — so a position-only assertion could never notice. The case asserts the WM logged no `bad window gravity` warning, which is the only server-side difference between "handled, offset zero" and "not handled". Added before the mutation was run, and confirmed by it. |

## Mutation testing (26 deletions)

### Lifecycle (`[wm_lifecycle]`)

| # | Production branch deleted | Reddened |
|---|---|---|
| ML1 | the focus-tracking scrub in `eventDestroy` | **5 cases** (ASan tree) |
| ML2 | the active-client clear in `eventDestroy` | **nothing** — equivalent, see below |
| ML3 | the hidden-vector fallback in `eventDestroy` | the hidden-destroy case |
| ML4 | `updateClientList()` after removal | 4 cases |
| ML5 | `addToHiddenList()` in `hide()` | 3 cases |
| ML6 | `updateNetWmState()` in `hide()` | 3 cases |
| ML7 | `m_border->unmap()` in `hide()` | 2 cases |
| ML8 | `removeFromHiddenList()` in `unhide()` | the unhide case |
| ML9 | `updateNetWmState()` in `unhide()` | the unhide case |
| ML10 | the `Border::~Border` teardown order | **all 6 cases** |

### Size hints (`[wm_sizehints]`)

| # | Production branch deleted | Reddened |
|---|---|---|
| MS1 | the `incW > 0` guard | the degenerate-increment case |
| MS2 | the `incH > 0` guard | the degenerate-increment case |
| MS3 | the inverted-maximum normalisation | **nothing** — equivalent, see below |
| MS4 | the strictly-positive floor | the degenerate-increment case |
| MS5 | the maximum clamp | the maximum case |
| MS6 | the trailing minimum floor | 2 cases |
| MS7 | the whole quantisation branch | 2 cases |
| MS8 | `if (isFixedSize()) return;` in `resize()` | **nothing** — equivalent, see below |
| MS9 | the `!isFixedSize()` guard on mapping the resize handle | the fixed-size case |

### Gravity (`[wm_gravity]`)

| # | Production branch deleted | Reddened |
|---|---|---|
| MG1 | `case NorthGravity` | the all-modes case |
| MG2 | `case CenterGravity` / `case StaticGravity` | the all-modes case (via the warning assertion) |
| MG3 | the `PWinGravity` flag read | 2 cases |
| MG4 | the top-left placement clamp in `manage()` | the unhandled-gravity case |
| MG5 | `gravitate(false)` at the end of `withdraw()` | the round-trip case |
| MG6 | `gravitate(true)` in `withdraw()` | the round-trip case |
| MG8 | the default gravity initialiser | the no-gravity case |

### The four equivalent mutants, and why each is equivalent

- **ML2 — the active-client clear in `eventDestroy`.** Destroying a mapped window generates an `UnmapNotify` *before* the `DestroyNotify`, and `Client::eventUnmap()` already calls `clearFocus()` for an active client, which sets the active client to null and publishes `_NET_ACTIVE_WINDOW` as `None`. By the time `eventDestroy` runs, `m_activeClient` is already null. An unmapped client cannot be the active one, so there is no scenario in which this branch is the only thing clearing it. Defence in depth, not behaviour.
- **MS3 — the inverted-maximum normalisation.** The trailing `if (w < minW) w = minW;` already raises any dimension the maximum clamp drove below the floor, and it uses the same `minW`. Normalising the maximum first therefore produces the identical result for every input. Implemented because the plan mandates it and because it makes the intent legible at the point the contradiction exists, but no test can distinguish it and none was written to pretend otherwise.
- **MS8 — the fixed-size early return in `resize()`.** Two independent reasons. The resize handle is unmapped for a fixed-size client, so the press never routes into `resize()` at all; and even when it does (via the frame's edge regions), `min == max` makes `fixResizeDimensions()` return the current size, `doSomething` stays false, and nothing is configured. The early return is a short-circuit, not the enforcement. The enforcement that IS observable — the unmapped handle — is asserted and reddens as MS9.
- **A negative resize increment** is not a mutation but belongs in the same list: the sign cancels between the divide and the multiply, so `(h - min) / -5 * -5` lands on the same lattice as `/5 * 5`. Its section passes with or without the guards and says so in a comment. It is present because the coverage item names negative increments and because it pins that the guard treats a non-positive increment as "do not quantise" rather than "refuse to resize".

## Decisions Made

- **The focus-candidate destroy case stretches the auto-raise delay to 20 s rather than shortening it** as the plan's action text suggested. The branch under test is reachable only while the deadline is still pending; stretching it makes that precondition *observable from outside the WM* — if the candidate had already been focused, `_NET_ACTIVE_WINDOW` would have changed — so `REQUIRE(activeWindow(d) == other)` is a real check rather than an assumption about timing. A short delay would have raced the case into the post-expiry path, where nothing interesting happens.
- **The case does not stop at the destroy.** It then points the pointer at another window, because that is what calls `considerFocusChange()` → `stopConsideringFocus()` → the dereference. Without that second move the dangling pointer is never read and the case proves only that the WM survived.
- **BadWindow is excluded from the protocol-error assertion in every case, not only the destroy cases.** `WmFixture` destroys its own readiness probe during startup, so deferred item 13's teardown error can surface anywhere. One error code excluded is a far smaller concession than the assertion dropped.
- **The client-list order is asserted as a proxy for the vector transfer, explicitly not as an EWMH ordering claim.** EWMH asks for initial mapping order and this is not that; the comment says so, so a future correction of the publication order moves these expectations with it rather than fighting a test that cemented a wart.
- **The gravity table is enumerated from the placement switch, with the frame indents measured at run time** from a reference window rather than hardcoded — they depend on the tab width, which depends on the font.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `Border::~Border` freed its XftDraw after destroying the drawable**

- **Found during:** Task 1, by the new protocol-error assertion
- **Issue:** `RenderBadPicture (invalid Picture parameter)` on stderr for every managed window ever closed. `m_tabDraw` is created bound to `m_tab`; the destructor destroyed `m_tab` first.
- **Fix:** `m_tabDraw.reset()` moved above the `XDestroyWindow` block.
- **Files modified:** `src/Border.cpp`
- **Verification:** reproduced against a bare WM plus one client before the fix, absent after, same command; mutation ML10 reddens all six `[wm_lifecycle]` cases.
- **Commit:** `5b38218`

**2. [Rule 1 - Bug] Heap-use-after-free through the focus-tracking record (T-8-UAF)**

- **Found during:** Task 1, by the ASan gate on the restructured hidden-list cases
- **Issue:** `eventDestroy`'s STEP 1 guarded on `m_focusChanging`, which is routinely false at destroy time because `stopConsideringFocus()` clears the flag and leaves the candidate pointer.
- **Fix:** scrub whenever the dying client is the recorded candidate; also clear `m_focusCandidateWindow`.
- **Files modified:** `src/Events.cpp`
- **Verification:** 2 failures in 6 group runs before, 0 in 13 after; mutation ML1 reddens 5 cases.
- **Commit:** `495138f`

**3. [Rule 2 - Missing critical functionality] Two client-triggerable denial-of-service paths in `fixResizeDimensions()` (T-8-DOS)**

- **Found during:** Task 2, RED step — both reproduced against the shipped binary
- **Issue:** a zero increment on either axis kills the WM with an integer divide by zero; a negative base with a large increment produces a 64536×64536 window.
- **Fix:** per-axis increment validation, inverted-maximum normalisation, strictly-positive floor. All three are no-ops for well-formed hints.
- **Files modified:** `src/Client.cpp`
- **Commit:** `53167ec` (mandated by the plan's action text and its threat register)

**4. [Rule 3 - Blocking] A dangling-iterator UB in the first draft of the test file**

- **Found during:** Task 1, first run
- **Issue:** `std::count(clientList(d).begin(), clientList(d).end(), a)` calls the helper twice, producing iterators into two different temporaries. Segfaulted one case and silently returned 2 in another.
- **Fix:** an `occurrences()` helper holding one vector.
- **Files modified:** `tests/test_wm_lifecycle.cpp`
- **Commit:** folded into `cb9520a`

### Acceptance-criteria counts that differ from the plan (no action needed)

| Criterion | Expected | Actual | Why |
|---|---|---|---|
| `grep -c 'DISPLAY=:99' CMakeLists.txt` | 4 | **5** | Already 5 at this plan's base commit, exactly as 08-09 predicted and 08-10 recorded. `grep -c 'ENVIRONMENT "DISPLAY=:99"'` is 4, unchanged. Not a defect and deliberately not "fixed". |
| `grep -c 'test_wm_lifecycle' CMakeLists.txt` | ≥3 | 6 | Satisfied. |
| `grep -c 'wm_sizehints' tests/...` | ≥7 | 9 | Satisfied. |
| `grep -c 'PResizeInc\|PBaseSize\|PMinSize\|PMaxSize'` | ≥4 | 12 | Satisfied. |
| `grep -c 'wm_gravity' tests/...` | ≥4 | 5 | Satisfied. |
| `grep -c 'PWinGravity' / 'XTranslateCoordinates'` | ≥1 each | 6 / 2 | Satisfied. |
| `grep -c 'sleep(' ` (comments stripped) | 0 | 0 | Satisfied — every wait is `pollSleep()` inside a deadline-bounded poll. |

## Verification Evidence

| Gate | Result |
|---|---|
| `ctest -L '^wm_lifecycle$'` (debug) | **6/6 passed**, 3 consecutive runs |
| `ctest -L '^wm_sizehints$'` (debug) | **7/7 passed**, 3 consecutive runs |
| `ctest -L '^wm_gravity$'` (debug) | **4/4 passed** |
| `ctest -L '^(wm_lifecycle\|wm_sizehints\|wm_gravity)$'` (debug) | **17/17 passed** (18 with the preflight fixture), 3 consecutive runs |
| Same three groups (asan) | **17/17 passed**, no sanitizer report files left behind |
| `ctest --test-dir build/debug` (full) | **243/243 passed** (2 of 4 runs clean; see below) |
| `scripts/gates/build-all.sh` | **OK: debug release asan** — 243/243 in each tree |
| Build warnings | **0 new**; the only warning anywhere is deferred item 4's pre-existing `-Wunused-result` in the Release tree |
| ASan | **no sanitizer findings** (2 matched `libfontconfig` suppressions) |
| `scripts/analysis/run-static-analysis.sh` | **OK** — cppcheck 18 findings all baselined, clang-tidy no enforced check fired, `bugprone-branch-clone` still 6 |

### On the one full-suite failure

Two of four full-suite runs reported a single failure, both times `tests/test_wm_process.cpp:305` — the dock-strut workarea poll, in a file this plan does not touch. Measured on both sides of the change: **8/8 clean in isolation on the pre-08-11 source, and 8/8 clean in isolation on this plan's source.** Fails only under full-suite load, in a `pollUntil` that does not pump the WM. That is deferred item 12's documented signature exactly ("roughly one test per run, a different test each time, every one of them passes on its own"), and item 9 is its documented cause. Not a regression introduced here.

## Known Stubs

None. No placeholder values, no skipped tests, no unrun `<verify>` blocks. Four equivalent mutants are recorded as equivalent in their own section rather than left as tests that appear to cover something they do not.

## Threat Flags

None new. The plan's register is addressed:

- **T-8-UAF** — Test 3 destroys a client that is the live focus candidate *and then makes the WM use its focus-tracking state again*, which is what turns the case into a real test rather than a survival check; Test 6 destroys a client from the hidden vector. Both run under the sanitizer. The register's threat was found to be live and is now fixed.
- **T-8-DOS** — the degenerate-increment and inverted-maximum cases both landed on genuine defects and both are fixed rather than tolerated; the assertions pin the absence of a crash and of a nonsensical dimension, including in the synthetic `ConfigureNotify` the WM sends the client.
- **T-8-GEO** — the unhandled-gravity case asserts specifically on the absence of a negative coordinate, and mutation MG4 (deleting the placement clamp) reddens it.
- **T-8-SC** — accepted as planned; no dependency of any ecosystem was added.

## Notes for Future Phases

- **`Client::fixResizeDimensions()` now reads its bounds through locals** (`minW`, `minH`, `maxW`, `maxH`) rather than the size-hints struct directly. Anything added to it must go through the same locals or the normalisations are bypassed.
- **`WindowManager::eventDestroy` now has three scrubs, not two.** The focus-tracking record joins the revert chain and the window map. 08-08's note stands and extends: *any* future path that removes a `Client` must scrub every raw `Client*` the manager holds, and there are now three of them.
- **The protocol-error assertion is the cheapest defect detector in this suite** and cost about twenty lines. It found a defect on its first run that nothing else in 243 tests had noticed in eight plans. Worth copying into 08-12 and 08-13.
- **Deferred item 13 is the one thing standing between this file and a strict no-protocol-errors assertion.** Fixing it — reparenting the resize handle under the frame rather than under the client window — would let the exclusion be dropped entirely.
- **TEST-05 is not marked complete.** It is declared by six sibling plans (08-01/03/07/11/12/13) and the shared-ID gate holds it until the last of them lands.

## Self-Check: PASSED

The one created file and the SUMMARY exist on disk; all 7 claimed commit hashes resolve in `git log`.
