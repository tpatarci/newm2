---
phase: 03-client-lifecycle-with-raii
verified: 2026-05-07T15:35:00Z
status: passed
score: 12/12 must-haves verified
overrides_applied: 0
re_verification: false
---

# Phase 3: Client Lifecycle with RAII Verification Report

**Phase Goal:** Replace raw C++ resource management with modern RAII patterns for client lifecycle -- unique_ptr ownership, type-safe state enums, RAII scope guards, and automated tests
**Verified:** 2026-05-07T15:35:00Z
**Status:** passed
**Re-verification:** No -- initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | ServerGrab RAII class grabs and releases the X server around XReparentWindow in Border::reparent() | VERIFIED | `class ServerGrab` in `include/x11wrap.h:252-267` with XGrabServer/XUngrabServer. Used at `src/Border.cpp:716`: `x11::ServerGrab grab(display());` |
| 2 | ClientState enum class replaces raw int m_state with type-safe values matching X11 constants | VERIFIED | `enum class ClientState` in `include/Client.h:18-22` with Withdrawn=0, Normal=1, Iconic=3. `ClientState m_state` member at line 124. State queries at lines 37-39 use `ClientState::` enum values. |
| 3 | State transitions are validated with warnings on invalid paths | VERIFIED | `isValidTransition()` at `src/Client.cpp:322-331` validates all transitions. `setState()` at line 336 logs warning on invalid transition. Both `setState(ClientState)` and `setState(int)` overloads exist. |
| 4 | Colormap arrays use std::vector instead of malloc/free | VERIFIED | `std::vector<Window> m_colormapWindows` and `std::vector<Colormap> m_windowColormaps` in `include/Client.h:135-136`. `m_colormapWindows.assign(cw, cw + n)` followed by `XFree(cw)` at `src/Client.cpp:453-454`. No malloc/free for colormaps anywhere. |
| 5 | No release() method exists in Client -- destructor is the sole cleanup path | VERIFIED | `void release()` absent from `include/Client.h`. Destructor at `src/Client.cpp:55-70` handles unreparent, border reset, and deactivate. `grep "void Client::release" src/Client.cpp` returns empty. |
| 6 | Client objects are created via std::make_unique and destroyed exclusively by erasing unique_ptr from vector | VERIFIED | `std::make_unique<Client>(this, w)` at `src/Manager.cpp:409`. `vector<unique_ptr<Client>>` at `include/Manager.h:105-106`. No `new Client` in any source file. |
| 7 | No delete this or raw new Client anywhere in the codebase | VERIFIED | `grep -rn "delete this" src/ include/` returns only a comment at `include/Client.h:33`. `grep -rn "new Client" src/` returns empty. |
| 8 | windowToClient() checks hash map first for O(1) lookup, falls back to linear scan for border windows | VERIFIED | `m_windowMap.find(w)` at `src/Manager.cpp:393` with fallback `checkVector` lambda at lines 397-402. Hash map declared at `include/Manager.h:107`. |
| 9 | Hiding a client moves its unique_ptr from m_clients to m_hiddenClients; unhiding moves it back | VERIFIED | `addToHiddenList` at `src/Manager.cpp:479-488` uses `std::move(*it)`. `removeFromHiddenList` at lines 491-499 uses `std::move(*it)` in reverse direction. |
| 10 | eventDestroy clears focus tracking and map entry BEFORE erasing unique_ptr | VERIFIED | `src/Events.cpp:243-268`: Step 1 focus tracking clear (244-248), Step 2 active client clear (251-253), Step 3 map erase (255), Step 4 vector erase (257-265). Correct ordering verified. |
| 11 | Automated tests verify ServerGrab, ClientState, and window lifecycle operations | VERIFIED | `tests/test_client.cpp` has 11 TEST_CASE sections: 3 ServerGrab tests, 2 ClientState tests, 5 window lifecycle tests, 1 colormap property test. All 47 tests pass on Xvfb via ctest. |
| 12 | Tests run on Xvfb without physical display | VERIFIED | `CMakeLists.txt` configures `test_client` with `DISPLAY=:99` and `FIXTURES_REQUIRED xvfb_display`. All 47 tests pass with 100% pass rate. |

**Score:** 12/12 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `include/x11wrap.h` | ServerGrab RAII class | VERIFIED | Class at lines 252-267, non-copyable, XGrabServer/XUngrabServer RAII |
| `include/Client.h` | ClientState enum, vector colormap members | VERIFIED | Enum at lines 18-22, vector members at lines 135-136, no release() method |
| `include/Manager.h` | unique_ptr vector members, unordered_map member | VERIFIED | `vector<unique_ptr<Client>>` at lines 105-106, `unordered_map<Window, Client*>` at line 107 |
| `src/Client.cpp` | setState validation, vector colormaps, destructor cleanup | VERIFIED | isValidTransition at line 322, setState at line 336, destructor at line 55, vector colormaps at line 453 |
| `src/Border.cpp` | ServerGrab-wrapped reparent | VERIFIED | `x11::ServerGrab grab(display());` at line 716 |
| `src/Manager.cpp` | map-first windowToClient, unique_ptr lifecycle, move-based hide/unhide | VERIFIED | Map-first lookup at line 393, make_unique at line 409, move-based transfers at lines 485/498 |
| `src/Events.cpp` | unique_ptr-aware eventDestroy | VERIFIED | Correct ordering with map erase before vector erase at lines 243-268 |
| `src/Buttons.cpp` | unique_ptr vector iteration | VERIFIED | `.get()` for pointer comparison at lines 45/61/109, `->` for method calls |
| `tests/test_client.cpp` | TEST-03 lifecycle tests | VERIFIED | 300 lines, 11 test cases covering ServerGrab, ClientState, lifecycle, reparenting, properties |
| `CMakeLists.txt` | test_client target with Xvfb fixture | VERIFIED | add_executable, target_link_libraries, catch_discover_tests all present |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `src/Border.cpp:716` | `include/x11wrap.h:252` | ServerGrab usage in reparent() | WIRED | `x11::ServerGrab grab(display());` instantiates the RAII class |
| `src/Client.cpp:322-355` | `include/Client.h:18-22` | ClientState enum in setState() | WIRED | `ClientState::Withdrawn/Normal/Iconic` used throughout |
| `src/Client.cpp:453` | `include/Client.h:135-136` | Vector colormap members | WIRED | `m_colormapWindows.assign()` populates vector, `XFree(cw)` at line 454 |
| `src/Manager.cpp:393` | `include/Manager.h:107` | m_windowMap usage in windowToClient | WIRED | `m_windowMap.find(w)` at line 393, `m_windowMap[raw->window()] = raw` at line 412 |
| `src/Events.cpp:254` | `include/Manager.h:107` | m_windowMap.erase before unique_ptr erase | WIRED | `m_windowMap.erase(c->window())` at line 254, vector erase at lines 257-265 |
| `src/Manager.cpp:485,498` | `src/Manager.cpp:105-106` | std::move(unique_ptr) between vectors | WIRED | `m_hiddenClients.push_back(std::move(*it))` at line 485, reverse at line 498 |
| `CMakeLists.txt` | `tests/test_client.cpp` | add_executable + catch_discover_tests | WIRED | test_client target configured with Xvfb fixture |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|-------------------|--------|
| `src/Manager.cpp:windowToClient()` | `m_windowMap` | `make_unique<Client>` at line 409, map insert at line 412 | Yes -- real Client* stored in map | FLOWING |
| `src/Client.cpp:getColormaps()` | `m_colormapWindows` | `getProperty_aux` X11 property read, `.assign()` copy from X11 memory | Yes -- real X11 colormap window list | FLOWING |
| `src/Client.cpp:setState()` | `m_state` | `isValidTransition()` validation, `XChangeProperty` write to X server | Yes -- state written to X11 property | FLOWING |
| `src/Events.cpp:eventDestroy()` | focus/map cleanup | `m_windowMap.erase()`, `setActiveClient(nullptr)`, vector erase | Yes -- real cleanup before destruction | FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| All tests pass | `ctest --output-on-failure` | 47/47 tests passed, 0 failed | PASS |
| test_client tests pass | `ctest -R client --output-on-failure` | 11 client tests all passed | PASS |
| Build succeeds | `cmake --build .` | All targets built, 0 errors | PASS |
| No delete this in source | `grep -rn "delete this" src/ include/` | Only comment match | PASS |
| No raw new Client | `grep -rn "new Client" src/` | No output (empty) | PASS |
| No release() method | `grep -n "void Client::release" src/Client.cpp` | No output (empty) | PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| CLNT-01 | 03-01 | ICCCM-compliant reparenting using XGrabServer/XUngrabServer | SATISFIED | ServerGrab RAII class wraps Border::reparent() |
| CLNT-02 | 03-02 | Replace delete this with proper lifecycle management via WindowManager | SATISFIED | unique_ptr ownership in m_clients/m_hiddenClients, no delete this |
| CLNT-03 | 03-01 | Replace custom listmacro2.h with std::vector<Client*> | SATISFIED | vector<unique_ptr<Client>> replaces raw pointer vectors |
| CLNT-04 | 03-02 | O(1) client lookup by window ID (hash map) | SATISFIED | unordered_map<Window, Client*> with map-first lookup |
| CLNT-05 | 03-01 | Client state transitions correct (Withdrawn->Normal->Iconic->Withdrawn) | SATISFIED | ClientState enum with isValidTransition validation |
| TEST-03 | 03-03 | Core WM operations tested: window map, move, resize, hide/unhide, delete | SATISFIED | 11 test cases covering lifecycle operations on Xvfb |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| (none) | - | - | - | No anti-patterns detected |

### Human Verification Required

None required. All truths are programmatically verifiable through code inspection and automated tests.

### Gaps Summary

No gaps found. All 6 requirements (CLNT-01 through CLNT-05, TEST-03) are satisfied. All 12 observable truths verified with codebase evidence. Build succeeds with 0 errors. All 47 tests pass (36 existing + 11 new). No anti-patterns detected in modified files.

---

_Verified: 2026-05-07T15:35:00Z_
_Verifier: Claude (gsd-verifier)_
