---
phase: 08-xrandr-vnc-compatibility-focus-rules
plan: 12
subsystem: testing
tags: [process-level-tests, ewmh, client-messages, fullscreen, maximize, workarea, malformed-properties, asan, heap-buffer-overflow, denial-of-service, tampering]

requires:
  - phase: 08-xrandr-vnc-compatibility-focus-rules/08-01
    provides: "tests/support/WmFixture.h and tests/lsan.supp -- the process-level harness all 24 cases run on"
  - phase: 08-xrandr-vnc-compatibility-focus-rules/08-02
    provides: "scripts/gates/build-all.sh and scripts/analysis/run-static-analysis.sh -- the gates this plan's evidence comes from"
  - phase: 08-xrandr-vnc-compatibility-focus-rules/08-07
    provides: "the spaced settleWm() methodology (deferred item 9), without which every negative assertion here would read stale values"
  - phase: 08-xrandr-vnc-compatibility-focus-rules/08-08
    provides: "Client::updateNetWmState(), the single _NET_WM_STATE writer whose output every [wm_state] case reads back"
  - phase: 08-xrandr-vnc-compatibility-focus-rules/08-11
    provides: "the X-protocol-error assertion idiom, reused verbatim; without it three of this plan's defects would have been invisible"
provides:
  - "tests/test_wm_state.cpp and the test_wm_state target: 24 process-level cases in three groups ([wm_state], [wm_fsmax], [wm_props])"
  - "Checklist coverage items 10, 11 and 12 closed against the real binary, green in the debug AND sanitizer builds"
  - "deferred item 8 RESOLVED, with its stated cause corrected -- the reparent was not the trigger"
  - "Client::eventUnmap() acts only on the client window's own unmap; frame-component unmaps no longer read as a client withdrawing"
  - "A tampering fix (T-8-MSG): _NET_WM_STATE messages are refused for Withdrawn -- i.e. unmanaged -- windows"
  - "A heap-buffer-overflow fixed in the shared property reader (T-8-PROP): type, format and item count are validated before every dereference"
  - "_NET_WORKAREA can no longer be inverted by a hostile dock (T-8-STRUT)"
  - "Maximize now fills the workarea with the FRAME, keeping the sideways tab on screen"
  - "deferred item 14: stale ASan reports are attributed to the next fixture handed the same display number"
affects: [08-13, any later work touching Client::eventUnmap(), Client::setFullscreen(), Client::setMaximized(), Border::restoreFromFullscreen(), getProperty_aux() or WindowManager::updateWorkarea()]

actuals:
  tokens: 30500
  tasks: 3
  commits: 5

tech-stack:
  added: []
  patterns:
    - "Tracing an event handler with temporary fprintf when a one-line fix that should have worked did not -- the trace named the real trigger in one run, after two rounds of plausible reasoning had named the wrong one"
    - "Measuring the frame's content offset at run time from a reference window rather than hardcoding it, so a tab-width or font change moves the expectations with it"
    - "Asserting a strut invariant (a valid, non-inverted rectangle inside the screen) rather than a specific clamped number, so the case pins the requirement instead of the current policy"
    - "Driving a single-edge strut on a single dock when a mutation survives four-edge struts -- with every edge set, two different implementations collapse to the same number"
    - "Capturing the whole state ARRAY before and after for every negative case, so 'nothing changed' is a claim about preservation rather than about an empty array staying empty"

key-files:
  created:
    - tests/test_wm_state.cpp
  modified:
    - CMakeLists.txt
    - src/Client.cpp
    - src/Events.cpp
    - src/Border.cpp
    - src/Manager.cpp
    - include/Client.h
    - COMPILED_CODE_BEHAVIOR_CHECKLIST.md
    - .planning/phases/08-xrandr-vnc-compatibility-focus-rules/deferred-items.md

key-decisions:
  - "The eventClient guard is !isWithdrawn(), not !isNormal(). An Iconic client is genuinely managed, and the plan's own Task 2 case 4 requires that fullscreen can be requested on a hidden window. Withdrawn is the ICCCM's own word for 'the window manager is not managing this window', which is exactly the set that must be refused -- and it restores the symmetry the other two branches of eventClient() already had."
  - "Deferred item 8's recorded cause was corrected rather than accepted. The fix item 8 proposed -- setting m_reparenting around the strip/restore reparents -- was implemented first and DID NOT WORK. Tracing showed Border::unmap()'s frame, tab and button unmaps each reach Client::eventUnmap() through hasWindow(), and the tab's is what withdrew the client. Both halves are needed and each reddens on its own."
  - "Maximize insets the workarea by the decoration so the FRAME lands on it, rather than passing the raw workarea as the client rectangle. The alternative -- leaving the tab off-screen and calling it the client's problem -- is not defensible for the one operation whose entire purpose is to make a window fully visible."
  - "setMaximized falls back to the screen when the workarea is not a usable rectangle inside it. The condition is a RANGE check, not a size threshold, deliberately: a threshold would be a policy about how much of the screen a panel may claim, and inventing one is not this plan's business."
  - "clampStrut is recorded as an EQUIVALENT MUTANT with the mechanism spelled out rather than covered by a contrived test. Xlib sign-extends format-32 property data into long, so the narrowing conversion is exact today and no observation from outside the WM can distinguish the clamp from a bare cast."
  - "The plan's Task 3 asserted a specific clamped workarea in the first draft. That case was REWRITTEN once the sign-extension fact was measured, because it claimed to catch something it could not."

patterns-established:
  - "Mutation testing carried forward: 15 production branches deleted in turn, 12 confirmed to redden a named case, 3 recorded as equivalent with the mechanism explained"
  - "Mutations run in BOTH trees when the defect class is a memory error -- the format-check deletion is green in debug and kills the WM in six cases under ASan"
  - "When a mutation survives, the test is strengthened until it reddens or the mutant is proven equivalent in writing. Two cases in this plan were rewritten on that basis and one new sub-case was added"

requirements-completed: []

coverage:
  - id: D1
    description: "Every EWMH client message eventClient() handles is exercised: activation-adjacent state add, remove, toggle, the simultaneous two-property form, and the two-property toggle"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_state.cpp -- 5 [wm_state] cases (add/remove, toggle both directions, two-property add, two-property toggle from an asymmetric start, WM_CHANGE_STATE iconify)"
        status: pass
    human_judgment: false
  - id: D2
    description: "An unsupported, wrong-format or misaddressed client message leaves the WM's observable state exactly as it was"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_state.cpp -- 3 negative [wm_state] cases, each comparing the whole sorted state array plus geometry before and after"
        status: pass
    human_judgment: false
  - id: D3
    description: "A client cannot mutate the geometry or published state of a window the WM has not taken under management (threat T-8-MSG)"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_state.cpp#A state message naming an unmanaged window is ignored and does not crash -- RED before the fix, MEASURED (10,10 50x50) -> (0,0 1024x768); mutation M3 reddens"
        status: pass
    human_judgment: false
  - id: D4
    description: "Fullscreen covers the whole screen including dock areas, keeps the client managed, and restores to its exact pre-fullscreen geometry with frame and tab intact"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_state.cpp#Fullscreen covers the whole screen including the dock, and restores exactly (deferred item 8; mutations M1/M2/MF4 redden)"
        status: pass
    human_judgment: false
  - id: D5
    description: "Maximize fills the WORKAREA with the decorated window, keeps the frame and tab on screen, and restores exactly -- both axes and each axis alone"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_state.cpp -- 2 [wm_fsmax] cases; RED before the fix, MEASURED frame (-25,-8 1050x737) on a (0,0 1024x728) workarea; mutations MF1/MF2/MF3 redden"
        status: pass
    human_judgment: false
  - id: D6
    description: "The awkward orderings hold: toggling fullscreen while hidden, destroying while fullscreen, a dock mapped under an already-maximized window, and maximizing straight after leaving fullscreen"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_state.cpp -- 4 [wm_fsmax] cases; the destroy case runs under the sanitizer with no reports; mutation MF5 reddens the hidden case"
        status: pass
    human_judgment: false
  - id: D7
    description: "The fullscreen and maximize saved-geometry slots do not alias"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_state.cpp#Maximizing straight after leaving fullscreen uses workarea geometry"
        status: pass
    human_judgment: false
  - id: D8
    description: "A client-written property of the wrong type, wrong format, zero length or absurd length cannot make the WM read outside an allocation (threat T-8-PROP)"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_state.cpp -- 4 [wm_props] cases (wrong type, wrong format, empty, oversized); mutation MP1 kills the WM in 6 of 9 cases under ASan"
        status: pass
      - kind: manual_procedural
        ref: "ASan report captured from the mutated binary: heap-buffer-overflow, READ of size 32, Client::getColormaps() src/Client.cpp:1166, buffer allocated by XGetWindowProperty at src/Client.cpp:1156"
        status: pass
    human_judgment: false
  - id: D9
    description: "A property deleted after the window is managed is handled on the resulting change notification without a crash and without reading freed data"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_state.cpp#Deleting a property after the window is managed is handled on the notification (seven properties, asan clean)"
        status: pass
    human_judgment: false
  - id: D10
    description: "No dock, however it declares its struts, can make _NET_WORKAREA inverted, empty-by-inversion, or larger than the screen (threat T-8-STRUT)"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_state.cpp -- 2 [wm_props] strut cases; RED before the fix, MEASURED _NET_WORKAREA = (1024,768 -1024x-768); mutations MP4/MP5/MP6 redden"
        status: pass
    human_judgment: false
  - id: D11
    description: "A window name containing invalid UTF-8 renders without crashing and without corrupting subsequent drawing"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_state.cpp#A name containing invalid UTF-8 renders without crashing or corrupting"
        status: pass
    human_judgment: false
  - id: D12
    description: "The whole malformed-property battery run in sequence against one window leaves the WM alive, its client list self-consistent, and its framing correct"
    requirement: TEST-05
    verification:
      - kind: e2e
        ref: "tests/test_wm_state.cpp#The whole malformed-property battery against one window leaves the WM consistent (battery applied both before and after the map)"
        status: pass
    human_judgment: false
  - id: D13
    description: "Whether falling back to the full screen when docks claim the entire workarea is the behaviour a user would want, as against honouring the docks and leaving maximize a no-op"
    verification: []
    human_judgment: true
    rationale: "The EWMH does not say what a window manager should do when the published workarea has zero area. This plan chose to ignore an impossible strut so the user gets a maximized window they can see, on the reasoning that an unreachable window is the worse outcome. A panel author who deliberately reserved the whole screen would disagree. The tests pin that the result is a valid on-screen rectangle -- which is the safety claim -- and deliberately do not pin WHICH rectangle. Flagged for /gsd-verify-work."

duration: 95min
completed: 2026-08-29
status: complete
---

# Phase 08 Plan 12: EWMH State, Fullscreen/Maximize and Malformed Properties Summary

**Three checklist coverage gaps closed, one four-plan-old deferred item resolved with its recorded cause corrected — and seven defects that had been shipping in silence, three of which let any client on the display reach into another application's windows.**

## Performance

- **Duration:** ~95 min
- **Tasks:** 3 of 3
- **Files created:** 1; modified: 8
- **Commits:** 5

## Accomplishments

- **24 process-level cases in one new file**, in three tag groups, closing checklist coverage items 10 (the EWMH client-message matrix), 11 (fullscreen and maximize geometry restore) and 12 (malformed client properties). Green in debug, green under the sanitizer, stable across three consecutive runs.
- **Seven genuine defects found and fixed**, four of them security-relevant. Every one was found by an assertion the plan asked for.
- **Deferred item 8 resolved** — open since 08-04, worked around by 08-05 and 08-08 — and its recorded diagnosis corrected: the fix item 8 itself proposed was implemented first and did not work.
- **15 mutations run; 12 reddened a named case, 3 recorded as equivalent** with the mechanism spelled out. Two cases were rewritten and one sub-case added because a mutation first came back green.
- **The protocol-error assertion 08-11 recommended was copied in**, and earned its place immediately: the single-axis maximize defect surfaced as a protocol error before any geometry assertion could see it.
- **Zero build warnings introduced**; `cppcheck` still at its 18-finding baseline; clang-tidy `bugprone-branch-clone` still 6.

## Task Commits

1. **Task 1: the EWMH client-message matrix** — `1e01033` (test/RED) → `3ff6d97` (fix: the unmanaged-window guard + deferred item 8)
2. **Task 2: fullscreen and maximize geometry** — `cddb979` (tests + four geometry fixes)
3. **Task 3: malformed client properties** — `e07549c` (tests + the property-reader and strut hardening)
4. **Docs** — `7b6826f` (checklist items 10–12, deferred item 8 resolved, deferred item 14 recorded)

## The seven defects

### 1. Any client could reach into another application's unmapped windows (T-8-MSG)

`WindowManager::eventClient()`'s `_NET_WM_STATE` branch guarded on nothing but `c && e->format == 32`. And `windowToClient()` is **not** the same question as "is this window managed": the WM builds a `Client` for every non-override-redirect top-level window at `CreateNotify`, so between `CreateNotify` and the `MapRequest` **every window on the display** has one — Withdrawn, no frame mapped, `manage()` never run, absent from `_NET_CLIENT_LIST`.

**Measured**, from a single message any client can send to root:

```
another connection's unmapped window:  (10,10 50x50)  ->  (0,0 1024x768)
                            and gained _NET_WM_STATE_FULLSCREEN
```

The owning client has no say and no notification. Fixed with `!c->isWithdrawn()`, which restores the symmetry the other two branches of the same function already had — `WM_CHANGE_STATE` checks `isNormal()`, the activation branch checks `isNormal()`, and this one checked nothing.

### 2. Deferred item 8, and why its recorded cause was wrong

Item 8 recorded fullscreen as "sized correctly, wrong position, and Withdrawn", and attributed it to `Border::stripForFullscreen()`'s reparent: `XReparentWindow` implicitly unmaps a mapped window, the WM receives an `UnmapNotify` for its own reparent, and `eventUnmap` takes the withdraw path. The proposed fix was to set `m_reparenting` around the strip/restore the way `Client::manage()` does.

**That fix was implemented first, and the tests stayed red.** Tracing `Client::eventUnmap` on the real binary produced the answer in one run:

```
TRACE markReparenting win=0x800001 map_state=2
TRACE eventUnmap win=<frame> ev=<root>  state=Normal reparenting=1   -> guard consumed
TRACE eventUnmap win=<tab>   ev=<frame> state=Normal reparenting=0   -> withdraw()
```

`WindowManager::eventUnmap()` resolves its event window through `windowToClient()`, whose fallback scan calls `Client::hasWindow()` — **true for the frame, the tab, the button and the resize handle** as well as for `m_window`. `stripForFullscreen()` calls `XUnmapWindow` on three of them, so all three arrived as though the application had unmapped its own window. The first consumed the reparent guard; the **tab's** unmap took the withdraw path, `gravitate(true)` put the window back at its pre-fullscreen coordinates, and `setState(Withdrawn)` ended management.

`Client::hide()` escaped identical treatment only by timing — it calls `Border::unmap()` and then `setState(Iconic)` synchronously, so those three events land on the Iconic arm when they are finally processed. Luck, not design, and not luck any future caller of `Border::unmap()` inherits.

Fixed in both places, and **each half reddens on its own**: deleting the `eventUnmap` window guard reddens 2 cases, deleting both `markReparenting()` calls reddens the same 2. Neither alone is sufficient.

`markReparenting()` queries the window's map state before arming, which is what makes it safe rather than merely effective: a hidden client's reparent generates no `UnmapNotify`, and a flag set unconditionally would survive to swallow a real withdraw.

### 3. A maximized window's tab was pushed off the screen

`Border::configure(x, y, w, h)` describes the **client** and derives the frame by subtracting the decoration. `setMaximized()` passed the raw workarea. **Measured** on a 1024×768 screen with a 40px bottom dock:

```
workarea (0,0 1024x728)  ->  frame (-25,-8 1050x737)
```

The entire sideways tab and the top border, off the left and top edges — on the one operation whose whole purpose is to make a window fully visible. Now inset by the frame indents so the **frame** lands on the workarea, with a strictly-positive floor because the workarea is derived from client-supplied struts.

### 4. Three call sites drew the client underneath its own tab

`setMaximized()` (both branches) and `Border::restoreFromFullscreen()` placed the client at its frame's **origin** instead of the content offset — so the client was drawn over the sideways tab and the frame border, and sat `(xIndent, yIndent)` away from where the WM's own `m_x/m_y` said it was.

**Measured:** a client the WM had placed at `(175,128 300x220)` came back from **every** fullscreen round trip at `(150,120 300x220)`. `Border::reparent()` and `Client::resize()` already used the content offset; these three were the only places in the codebase that did not.

### 5. A single-axis maximize restored to a 0×0 window

The save condition was `newVert && newHorz && !m_isMaximizedVert && !m_isMaximizedHorz`, so maximizing one axis never populated the restore slot. Un-maximizing then configured the window to the zero-initialised slot, issuing `XConfigureWindow` and `XMoveResizeWindow` with width 0 and height 0 — invalid requests the WM's error handler swallows in silence. **This one surfaced through the protocol-error assertion before any geometry assertion saw it.**

### 6. A heap-buffer-overflow in the shared property reader (T-8-PROP)

`getProperty_aux()` checked neither the returned type nor the **format**. Format is the size of an element **in bits**, and it is chosen entirely by the client — `XChangeProperty` accepts `type=ATOM` with `format=8` quite happily. `n` is then a count of **bytes**, while every caller casts the buffer to `Atom*`, `long*` or `Window*` and indexes it `n` times.

**Measured under ASan**, from one `XChangeProperty` call on an unmapped window:

```
ERROR: AddressSanitizer: heap-buffer-overflow
READ of size 32 at 0x50200002ce15
    Client::getColormaps()              src/Client.cpp:1166
    Client::manage(bool)                src/Client.cpp:112
    Client::eventMapRequest(...)        src/Client.cpp:1760
    WindowManager::loop()               src/Events.cpp:35
  allocated by XGetWindowProperty in    src/Client.cpp:1156
```

All five callers were exposed — `getProperty`, `getState`, `getProtocols`, `getWindowType`, `getColormaps` — and so was `updateWorkarea()`'s strut read, which is a direct consumer that does not go through the helper and needed the same three checks of its own. The helper now rejects on type, format and zero count, and frees the buffer and nulls the caller's pointer on **every** path that does not hand it over.

### 7. A hostile dock could invert `_NET_WORKAREA` (T-8-STRUT)

Each strut was clamped to the screen dimension **individually**, so `left == right == screenW` was perfectly reachable and the published width was `screenW - left - right`. **Measured** from one dock declaring 100000 on all four edges:

```
_NET_WORKAREA = (1024, 768, -1024, -768)
```

Not a cosmetic wrong number. `Client::setMaximized()` reads it and hands the result to `XConfigureWindow`, whose width and height parameters are **unsigned** — the same arithmetic that bought a 64536-pixel window in plan 08-11, reachable here by any client that can map a dock. Now capped in combination, so the published extents are non-negative by construction.

`setMaximized()` also falls back to the screen when the workarea is not a usable rectangle inside it: a dock claiming everything produced `(1024,768 0x0)`, and maximizing into that put the window at `(1024,768 27x10)` — off the bottom-right corner of the display, unreachable.

## Where mutation testing changed the tests

Three changes exist only because a mutation, or a measurement taken while chasing one, exposed a case as claiming more than it proved.

| Mutation | First result | What changed |
|---|---|---|
| `clampStrut` → a bare `static_cast<int>` | **green** | The case had asserted a specific clamped workarea, on the belief that Xlib widened format-32 data as unsigned. Measured: Xlib **sign-extends** (`_XRead32` reads through an `INT32*`), so `0xffffff00` arrives as `-256` and every strut is already representable in an `int`. The case was **rewritten** to assert the invariant it can actually prove — a valid, unshrunken rectangle — and the mutant recorded as equivalent. A case pinning a number it could not distinguish would have been a case that did not test its own name. |
| the type/format/count check on the strut read | **green** | No case wrote a *malformed* strut — only absurd ones. A phase-1b sub-case was added: a dock whose `_NET_WM_STRUT_PARTIAL` is type CARDINAL, count 12, format **8** — thirteen bytes, read as thirty-two. It now reddens 1 case in debug and 9 under ASan. |
| the FORMAT check in `getProperty_aux` | green **in debug** | Not a test weakness but a tree weakness, and worth recording: a heap over-read is invisible without the sanitizer. Under ASan the same deletion kills the WM child in 6 of 9 cases. Every memory-safety mutation in this plan was subsequently run in **both** trees. |

## Mutation testing (15 deletions)

### Client messages (`[wm_state]`)

| # | Production branch deleted | Reddened |
|---|---|---|
| M1 | the `e->window != m_window` guard in `Client::eventUnmap` | 2 cases |
| M2 | both `markReparenting()` calls in `setFullscreen` | the same 2 cases |
| M3 | the `!c->isWithdrawn()` guard in `eventClient` | the misaddressed-message case |

### Fullscreen and maximize (`[wm_fsmax]`)

| # | Production branch deleted | Reddened |
|---|---|---|
| MF1 | the single-axis restore-geometry save condition | the single-axis case |
| MF2 | the workarea decoration inset | 3 cases |
| MF3 | the maximize content offset (both branches) | 4 cases |
| MF4 | the `restoreFromFullscreen` content offset | 3 cases |
| MF5 | the `mapRaised()` fullscreen guard | the hidden-fullscreen case |

### Malformed properties (`[wm_props]`)

| # | Production branch deleted | Reddened |
|---|---|---|
| MP1 | the FORMAT check in `getProperty_aux` | 1 case (debug), **6 cases (asan)** |
| MP2 | the TYPE check in `getProperty_aux` | **nothing** — equivalent, see below |
| MP3 | `clampStrut` → a bare narrowing cast | **nothing** — equivalent, see below |
| MP4 | the combined strut cap | the oversized-strut case |
| MP5 | the workarea range fallback in `setMaximized` | the oversized-strut case |
| MP6 | the type/format/count check on the strut read | 1 case (debug), **9 cases (asan)** |
| MP7 | the bounded string copy in `getProperty` | **nothing** — equivalent, see below |

### The three equivalent mutants, and why each is equivalent

- **MP2 — the type check in `getProperty_aux`.** `XGetWindowProperty` called with an explicit `req_type` returns `nitems == 0` whenever the stored type differs, so the `n == 0` arm catches every input the type check would. The check is kept because it states the decision at the point the decision is made, rather than resting on a server-side behaviour a future reader has to already know; it cannot change an outcome.
- **MP3 — `clampStrut`.** Xlib **sign-extends** format-32 property data into `long`, so every strut value that reaches `updateWorkarea()` is already representable in an `int` and the narrowing conversion is exact. Measured, not assumed: a dock declaring `0xffffff00` arrives as `-256`, not as `4294967040`. The clamp is kept because it puts the `[0, limit]` invariant where the untrusted value **enters** the arithmetic — the two guards below it are load-bearing, and having all three read as one policy is worth more than one saved comparison. No observation from outside the WM can distinguish it.
- **MP7 — the bounded string copy in `getProperty`.** Xlib appends a NUL byte after the returned data (documented, and true for a zero-length property too), and the function truncates at the first NUL regardless, so the bounded construction and the NUL-terminated one produce an identical string for every input. What actually made this path safe is MP1's format check; the bounded copy is the shape that stops the next reader from having to rediscover the guarantee. The plan's acceptance criterion asks for no unbounded construction and that is satisfied, but it is honest to say the criterion bought no behaviour here.

## Decisions Made

- **The `eventClient` guard is `!isWithdrawn()`, not `!isNormal()`.** A hidden (Iconic) client is genuinely managed, and the plan's own Task 2 case 4 requires that fullscreen can be requested on one. Withdrawn is the ICCCM's own term for "the window manager is not managing this window", which is exactly the set that must be refused.
- **Deferred item 8's diagnosis was corrected rather than accepted.** The recorded cause named the reparent; the reparent is real but secondary, and the fix item 8 proposed does not work on its own. Recorded in `deferred-items.md` with the trace, so the correction survives this summary.
- **Maximize insets the workarea by the decoration.** The alternative — leaving the tab off-screen — is not defensible for the one operation whose purpose is to make a window fully visible.
- **The degenerate-workarea fallback is a range check, not a size threshold.** A threshold would be a policy about how much of the screen a panel may claim, and inventing one is not this plan's business. The range check only fires when the workarea is not a usable rectangle inside the screen at all.
- **The strut cases assert an invariant, not a number.** `requireSaneWorkarea()` checks that the rectangle is non-negative, non-inverted and inside the screen. A specific expected value would have encoded this WM's clamping policy as though it were a specification.
- **The frame's content offset is measured at run time** from a reference window rather than hardcoded — it depends on the tab width, which depends on the font, which depends on what fontconfig resolves on the host.
- **Every negative case captures the whole sorted state array plus the geometry**, before and after. A one-atom check cannot distinguish "the message was ignored" from "the array was rewritten and happens not to contain that atom".

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing critical functionality] `_NET_WM_STATE` messages applied to unmanaged windows (T-8-MSG)**

- **Found during:** Task 1, first run of the misaddressed-message case
- **Issue:** any client could move, resize and re-label any other application's not-yet-mapped window with one message to root.
- **Fix:** `!c->isWithdrawn()` guard in `WindowManager::eventClient()`.
- **Files modified:** `src/Events.cpp`
- **Verification:** measured `(10,10 50x50) -> (0,0 1024x768)` before, unchanged after; mutation M3 reddens.
- **Commit:** `3ff6d97`

**2. [Rule 1 - Bug] `Client::eventUnmap` treated frame-component unmaps as client withdrawals (deferred item 8)**

- **Found during:** Task 1, after the guard above made the defect blocking — a fullscreen client became Withdrawn and could then never be un-fullscreened.
- **Issue:** `windowToClient()`'s `hasWindow()` fallback matches the frame, tab, button and resize handle, so `Border::unmap()`'s three `XUnmapWindow` calls each entered `Client::eventUnmap()` as a client-initiated unmap.
- **Fix:** act only on `m_window`'s own unmap; plus `Client::markReparenting()` around both fullscreen reparents; plus recording the fullscreen rect in `m_x/m_y/m_w/m_h`.
- **Files modified:** `src/Client.cpp`, `include/Client.h`
- **Verification:** traced on the real binary; mutations M1 and M2 each redden 2 cases.
- **Commit:** `3ff6d97`

**3. [Rule 1 - Bug] Four geometry defects in the maximize and fullscreen-restore paths**

- **Found during:** Task 2, RED step — all four reproduced against the shipped binary
- **Issue:** the maximized frame hung off the top-left of the screen; three call sites drew the client underneath its own tab; a single-axis maximize restored to 0×0 through invalid X requests; unhiding a fullscreen client mapped a stale empty frame over the desktop.
- **Fix:** decoration inset with a positive floor; content offset at all three sites; save on the transition into *any* maximized axis; a fullscreen guard in `Client::mapRaised()`.
- **Files modified:** `src/Client.cpp`, `src/Border.cpp`
- **Verification:** mutations MF1–MF5 each redden a named case.
- **Commit:** `cddb979`

**4. [Rule 2 - Missing critical functionality] Unvalidated property reads and invertible workarea (T-8-PROP, T-8-STRUT)**

- **Found during:** Task 3 — the workarea inversion by a RED case, the over-read by the mandated hardening and confirmed by mutation under ASan
- **Fix:** type/format/count validation and free-on-every-path in `getProperty_aux()` and in both direct consumers; combined strut cap; degenerate-workarea fallback.
- **Files modified:** `src/Client.cpp`, `src/Manager.cpp`
- **Verification:** ASan report captured; mutations MP1/MP4/MP5/MP6 redden.
- **Commit:** `e07549c` (mandated by the plan's action text and its threat register)

### Files modified beyond the plan's `files_modified` list

`src/Events.cpp`, `src/Border.cpp` and `include/Client.h` were changed in addition to the four files the plan named. Each is a one-place fix for a defect a case in this plan found, and each is covered by the plan's own artifacts clause ("only where a case finds a genuine defect, in which case the fix is recorded in the plan summary"). The defect in `Border.cpp` — the fullscreen restore's content offset — is one of the three identical spellings fixed together in deviation 3; splitting it out would have left the restore path wrong.

### Acceptance-criteria counts that differ from the plan (no action needed)

| Criterion | Expected | Actual | Why |
|---|---|---|---|
| `grep -c 'DISPLAY=:99' CMakeLists.txt` | 4 | **5** | Already 5 at this plan's base commit, exactly as 08-09 predicted and 08-10 and 08-11 recorded. `grep -c 'ENVIRONMENT "DISPLAY=:99"'` is 4, unchanged. Not a defect and deliberately not "fixed". |
| `grep -c 'test_wm_state' CMakeLists.txt` | ≥3 | 6 | Satisfied. |
| `grep -c 'SubstructureRedirectMask'` | ≥1 | 2 | Satisfied. |
| `grep -c 'wm_fsmax'` | ≥7 | 9 | Satisfied. |
| `grep -c '_NET_WM_STRUT_PARTIAL'` | ≥1 | 3 | Satisfied. |
| `grep -c 'wm_props'` | ≥9 | 11 | Satisfied. |
| `grep -c 'XDeleteProperty'` | ≥1 | 10 | Satisfied. |
| `grep -c 'XChangeProperty'` | ≥6 | 7 | Satisfied. Most malformed writes go through the file's own `writeProp()` helper, which is one `XChangeProperty` used ~40 times — the helper exists precisely so each call site can state its type, format and count explicitly. |
| `grep -c 'sleep('` (comments stripped) | 0 | 0 | Satisfied — every wait is a deadline-bounded poll. |
| `rg 'reinterpret_cast<char\*>' src/Client.cpp` | no unbounded construction | none at all | Satisfied. |

## Verification Evidence

| Gate | Result |
|---|---|
| `ctest -L '^wm_state$'` (debug) | **8/8 passed** |
| `ctest -L '^wm_fsmax$'` (debug) | **7/7 passed**, 3 consecutive runs |
| `ctest -L '^wm_props$'` (debug) | **9/9 passed** |
| `ctest -L '^(wm_state\|wm_fsmax\|wm_props)$'` (debug) | **24/24 passed** (25 with the preflight fixture), **3 consecutive runs** |
| Same three groups (asan) | **24/24 passed**, no sanitizer report files left behind |
| `ctest --test-dir build/debug` (full) | **267/267 passed** |
| `scripts/gates/build-all.sh` | **OK: debug release asan** — 267/267 in each tree |
| Build warnings | **0 new**; the only warning anywhere is deferred item 4's pre-existing `-Wunused-result` in the Release tree |
| ASan | **no sanitizer findings** (2 matched `libfontconfig` suppressions, 6 accounting logs) |
| `scripts/analysis/run-static-analysis.sh` | **OK** — cppcheck 18 findings all baselined, clang-tidy no enforced check fired, `bugprone-branch-clone` still 6 |

### On the one gate failure, and why it is not a regression

The first `build-all.sh` run reported a single failure in the asan tree: `Destroying a dock window restores _NET_WORKAREA with no sanitizer report`, in `tests/test_wm_process.cpp` — a file this plan does not touch, but one whose subject (dock struts and the workarea) this plan *did* change. That coincidence was not taken on trust.

**Measured on both sides of the change, same command, in isolation:**

| Binary | Runs | Failures |
|---|---|---|
| pre-08-12 (`73682a9` sources for `Client.cpp`/`Manager.cpp`/`Border.cpp`/`Events.cpp`/`Client.h`) | 28 | **0** |
| this plan's sources | 25 | **1** (the original observation) then **0 in 20** |

Both trees are clean in isolation; the failure appears only under full-suite load, in a `pollUntil` that does not pump the WM. That is deferred item 12's documented signature exactly, in the same file and the same dock-strut area that 08-11 recorded as its own flake. A second and third `build-all.sh` run were **green end to end, 267/267 in all three trees**, including this test.

## Known Stubs

None. No placeholder values, no skipped tests, no unrun `<verify>` blocks. Three equivalent mutants are recorded as equivalent in their own section rather than left as tests that appear to cover something they do not — and one case was rewritten, rather than left standing, when its claim turned out to exceed what it could observe.

## Threat Flags

None new. The plan's register is addressed:

- **T-8-PROP** — found live and fixed. The over-read is reproduced under ASan by deleting the format check, with the full stack from `Client::getColormaps()` back to the `XGetWindowProperty` allocation.
- **T-8-STRUT** — found live and fixed. The inverted workarea is measured, and the maximize path that consumes it is hardened separately so a future regression in one does not reach the other.
- **T-8-DOS** — the oversized-array case asserts both survival and a deadline; a hang is as much a failure as a crash and only a deadline distinguishes them.
- **T-8-MSG** — found live and fixed, and it was worse than the register anticipated: the register scoped it to "unsupported, wrong-format and misaddressed messages leave observable state byte-identical", and the misaddressed case turned out to be a live tampering path into other applications' windows rather than a no-op to confirm.
- **T-8-UAF** — the destroy-while-fullscreen case runs under the sanitizer against a stripped frame, a teardown ordering the ordinary destroy case does not reach. No finding.
- **T-8-SC** — accepted as planned; no dependency of any ecosystem was added.

## Notes for Future Phases

- **`Client::eventUnmap()` now ignores unmaps that are not for `m_window`.** Anything that adds a new frame sub-window inherits that protection; anything that starts relying on frame unmaps reaching the client will find they no longer do, and should say so explicitly rather than removing the guard.
- **This WM has exactly one convention for where a client sits inside its frame** — `(xIndent(), yIndent())` — and after this plan every call site follows it. A new geometry path that writes `XMoveResizeWindow(m_window, 0, 0, ...)` is a bug, not a style choice.
- **`_NET_WORKAREA` is untrusted input by proxy.** It is published by this WM, but it is derived from client-supplied struts, so every consumer must range-check it. `Client::setMaximized()` is currently the only consumer and does; a second one must too.
- **Run memory-safety mutations in the ASan tree.** Two of this plan's fifteen mutations are green in debug and catastrophic under the sanitizer. A mutation table built from the debug tree alone would have recorded both as equivalent and quietly removed two real guards.
- **Deferred item 14 will cost the next person time if it is not fixed.** Stale ASan reports are attributed to the next fixture handed the same display number, so one mutation run poisons every later run of the group with a failure that looks exactly like a regression in the code under test. One line in `WmFixture::start()`; left to 08-13 because `WmFixture.h` is shared by six suites.
- **TEST-05 is not marked complete.** It is declared by six sibling plans (08-01/03/07/11/12/13) and the shared-ID gate holds it until the last of them lands. 08-13 is the last.

## Self-Check: PASSED

`tests/test_wm_state.cpp` and this SUMMARY exist on disk; all 5 claimed commit hashes resolve in `git log`.
