---
phase: 03-client-lifecycle-with-raii
plan: 02
subsystem: client-lifecycle
tags: [raii, unique_ptr, unordered_map, ownership, x11]

# Dependency graph
requires:
  - phase: 03-client-lifecycle-with-raii
    plan: 01
    provides: ClientState enum, ServerGrab RAII, destructor-only cleanup, vector colormaps
provides:
  - unique_ptr ownership of Client objects via vector<unique_ptr<Client>>
  - O(1) hash map lookup via unordered_map<Window, Client*>
  - Move-based hide/unhide transferring unique_ptr between vectors
  - Safe eventDestroy with correct ordering (focus clear -> map erase -> vector erase)
affects: [04-border-xft-font-rendering]

# Tech tracking
tech-stack:
  added: [std::unordered_map<Window, Client*>]
  patterns: [unique_ptr vector ownership with raw pointer borrowing, map-first lookup with hasWindow fallback, unique_ptr move for list transfers]

key-files:
  created: []
  modified:
    - include/Manager.h
    - src/Manager.cpp
    - src/Events.cpp
    - src/Buttons.cpp

key-decisions:
  - "Map stores non-owning Client* that does not change on unique_ptr moves between vectors -- no map update needed for hide/unhide per D-06, Pitfall 3"
  - "eventDestroy ordering: focus tracking clear -> active client clear -> map erase -> vector erase, strictly before ~Client() runs per Pitfall 1"
  - "release() clears map first, then unparents all clients, then clears vectors -- unique_ptr destructors trigger ~Client() for each"
  - "windowToClient checks map first (O(1) for client windows), falls back to linear scan for border windows per D-05"

requirements-completed: [CLNT-02, CLNT-04]

# Metrics
duration: 7min
completed: 2026-05-07
---

# Phase 03 Plan 02: unique_ptr Ownership and O(1) Hash Map Lookup Summary

**Client objects exclusively owned by WindowManager via vector<unique_ptr<Client>>, O(1) hash map lookup replaces linear scan, move-based hide/unhide transfers, safe eventDestroy with correct ordering before destruction**

## Performance

- **Duration:** 7 min
- **Started:** 2026-05-07T15:05:20Z
- **Completed:** 2026-05-07T15:12:18Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments
- m_clients and m_hiddenClients converted from vector<Client*> to vector<unique_ptr<Client>> with full RAII ownership (CLNT-02)
- m_windowMap (unordered_map<Window, Client*>) provides O(1) lookup for client window IDs, with hasWindow() fallback for border windows (CLNT-04)
- windowToClient checks hash map first, falls back to linear scan of both vectors for border window events
- addToHiddenList/removeFromHiddenList use std::move on unique_ptr to transfer ownership between vectors
- eventDestroy correctly orders: focus tracking clear, active client clear, map erase, then vector erase which triggers ~Client()
- release() clears map, unparents all clients, then clears both vectors for RAII destruction
- No raw new Client, no delete this, no release() calls anywhere in codebase
- All 36 existing tests pass

## Task Commits

Each task was committed atomically:

1. **Task 1: Convert Manager.h client lists to unique_ptr, add hash map, update Manager.cpp** - `cd8cc8f` (feat)
2. **Task 2: Rewrite eventDestroy with unique_ptr erase and map maintenance, update Buttons.cpp** - `c908fde` (feat)

## Files Created/Modified
- `include/Manager.h` - Added #include <unordered_map>, changed m_clients/m_hiddenClients to vector<unique_ptr<Client>>, added m_windowMap member
- `src/Manager.cpp` - Rewrote windowToClient (map-first + hasWindow fallback), addToHiddenList/removeFromHiddenList (unique_ptr move), release() (RAII cleanup), skipInRevert, raiseTransients (unique_ptr iteration)
- `src/Events.cpp` - Rewrote eventDestroy (correct ordering with focus/map cleanup before vector erase), updated eventColormap for unique_ptr iteration
- `src/Buttons.cpp` - Updated circulate() (.get() for comparison, operator-> for methods), menu() hidden list iteration (.get())

## Decisions Made
- Map entries remain valid through hide/unhide because the raw Client* value does not change when unique_ptr moves between vectors -- no map update needed per Pitfall 3
- setActiveClient(nullptr) added in eventDestroy when the destroyed client is the active one -- prevents dangling active pointer after ~Client() runs
- release() simplified: all clients (both normal and hidden) are unparented, then both vectors cleared -- no need to separate normal/iconic since unique_ptr destructors handle everything

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Self-Check: PASSED

All files verified present. All commits verified in git history.

## Next Phase Readiness
- Plan 03 (if any) can proceed directly -- unique_ptr ownership model is complete
- All client lifecycle operations use RAII: unique_ptr for ownership, unordered_map for lookup, destructor for cleanup
- The ownership model is ready for Phase 4 (Border/Xft font rendering) and beyond

---
*Phase: 03-client-lifecycle-with-raii*
*Completed: 2026-05-07*
