# Phase 3: Client Lifecycle with RAII - Context

**Gathered:** 2026-05-07
**Status:** Ready for planning

<domain>
## Phase Boundary

Replace raw pointer client management with proper RAII lifecycle control. WindowManager exclusively owns Client objects via unique_ptr. O(1) window lookup via hash map. ICCCM-compliant reparenting with XGrabServer. Type-safe state machine with validation. Automated tests for core operations.

CLNT-03 (std::vector replacing listmacro2) is already done — not in scope. Colormap array modernization (malloc/free → std::vector) is in scope as part of RAII cleanup.

</domain>

<decisions>
## Implementation Decisions

### Client Ownership Model
- **D-01:** `std::vector<std::unique_ptr<Client>>` for m_clients and m_hiddenClients. WindowManager exclusively owns all Client objects. No shared_ptr, no manual new/delete.
- **D-02:** Moving a Client between visible and hidden lists transfers the unique_ptr (hide: move from m_clients to m_hiddenClients; unhide: move back). Zero duplication, single ownership at all times.
- **D-03:** Client cleanup stays in the destructor (unreparent, colormap free, deactivate). No separate release() method — the dtor is the single cleanup path. WindowManager triggers destruction by erasing the unique_ptr from the vector.
- **D-04:** Raw `Client*` for all borrowing (event handlers, focus tracking, revert chains). No observer_ptr wrapper — standard non-owning pointer pattern. Borrowers never outlive the unique_ptr owner because the event loop is single-threaded.

### O(1) Lookup Indexing
- **D-05:** `std::unordered_map<Window, Client*>` keyed by client window ID only (m_window). Border window lookups fall back to linear scan of vector checking `hasWindow()`. Border events are rare — fallback is acceptable.
- **D-06:** Map stores raw `Client*` (non-owning). Updated when unique_ptrs move between m_clients and m_hiddenClients, and on client creation/destruction. The map is a lookup accelerator, not an owner.
- **D-07:** `windowToClient()` checks map first (O(1) for client windows). On miss, falls back to linear scan through both vectors checking `hasWindow()` (for border window events).

### ICCCM Reparenting Atomicity
- **D-08:** Tight XGrabServer/XUngrabServer scope — only around the XReparentWindow call in Border::reparent(). Not the entire manage() flow. Minimal X server stall.
- **D-09:** RAII `ServerGrab` class in x11wrap.h alongside existing RAII types. Constructor calls XGrabServer, destructor calls XUngrabServer. Exception-safe, no chance of forgotten ungrab.

### State Transition Enforcement
- **D-10:** `enum class ClientState { Withdrawn, Normal, Iconic }` replaces raw int state. All internal code uses the enum. Conversion to/from X11 int values only at the X11 boundary (WM_STATE property reads/writes).
- **D-11:** State transitions validated in setState(). Invalid transitions log a warning via `fprintf(stderr)` and apply anyway. Matches upstream wm2's permissive approach — warnings catch logic bugs without crashing the WM.
- **D-12:** Valid transitions: Withdrawn→Normal, Normal→Iconic, Iconic→Normal, Normal→Withdrawn, Iconic→Withdrawn. Redundant transitions (Normal→Normal) are logged but accepted.

### Colormap Resource Management
- **D-13:** Replace raw `malloc`/`free` colormap arrays (`m_colormapWindows`, `m_windowColormaps`) with `std::vector<Window>` and `std::vector<Colormap>`. Consistent with C++17 RAII pattern established in Phase 1.

### Claude's Discretion
- Exact structure of the RAII ServerGrab class (constructor params, placement in x11wrap.h)
- Whether m_hiddenClients should also be `vector<unique_ptr<Client>>` or clients should stay in m_clients with state checking (move-based approach is preferred per D-02)
- Test structure and naming conventions for Phase 3 tests
- Implementation details of unordered_map integration with existing event dispatch

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Project Context
- `.planning/PROJECT.md` — Project definition, core value, constraints, key decisions
- `.planning/REQUIREMENTS.md` — CLNT-01 through CLNT-05, TEST-03 requirements with traceability
- `.planning/ROADMAP.md` — Phase 3 goal, success criteria, and requirements mapping

### Prior Phase Decisions
- `.planning/phases/01-build-infrastructure-raii-foundation/01-CONTEXT.md` — Phase 1 decisions (RAII wrappers via unique_ptr+custom deleters in x11wrap.h, C++17 idioms, source layout)
- `.planning/phases/02-event-loop-modernization/02-CONTEXT.md` — Phase 2 decisions (poll()-based loop, self-pipe signal wakeup, chrono timer patterns)

### Codebase Analysis
- `.planning/codebase/ARCHITECTURE.md` — Client/WindowManager class structure, window lifecycle, event flow
- `.planning/codebase/CONCERNS.md` — Known issues with delete this (line 124-129), O(n) lookup (line 131-139), client lifecycle fragility (line 182-186)

### Source Files (Primary Targets)
- `src/Client.cpp` — Client lifecycle, state management, reparenting, event handlers
- `include/Client.h` — Client class declaration, state, properties
- `src/Manager.cpp` — windowToClient(), client creation/destruction, hidden list management
- `include/Manager.h` — WindowManager class with client lists, focus tracking
- `include/x11wrap.h` — RAII wrapper types; ServerGrab class to be added here

### Upstream Reference
- `upstream-wm2/Client.C` — Original client lifecycle for behavioral reference
- `upstream-wm2/Manager.C` — Original windowToClient() and client list management

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `include/x11wrap.h` — Existing RAII wrappers (DisplayPtr, UniqueCursor, GCPtr, FontStructPtr). ServerGrab class follows the same pattern. Custom deleters already demonstrate the approach.
- `include/Manager.h:103-106` — `std::vector<Client*>` already in use. Upgrade path: change to `std::vector<std::unique_ptr<Client>>`.
- `include/Client.h:106` — `std::unique_ptr<Border>` already owns the border. Client ownership of Border is already RAII — WindowManager→Client ownership follows the same pattern.
- `FdGuard` in `include/Manager.h:15-28` — RAII for POSIX fds. ServerGrab follows the same constructor/destructor pattern.

### Established Patterns
- `m_` prefix for member variables — retained
- RAII for all resources — ServerGrab continues this
- Singleton WindowManager owns everything — Client ownership is a natural extension
- Raw pointers for borrowing — already used in event handlers

### Integration Points
- `Manager.cpp:386-399` — `windowToClient()` is the primary rewrite target for hash map lookup
- `Manager.cpp:458-475` — `addToHiddenList()`/`removeFromHiddenList()` become unique_ptr moves
- `Client.cpp:196-200` — `m_reparenting` flag and `Border::reparent()` call site — where XGrabServer wraps
- `Client.cpp:58-63` — `~Client()` currently calls `release()` — becomes the sole cleanup point
- `Client.h:118` — `int m_state` becomes `ClientState m_state`

</code_context>

<specifics>
## Specific Ideas

No specific requirements — the upstream wm2 behavior is the reference for state transitions. The RAII modernization follows patterns already established in Phase 1 (unique_ptr + custom deleters).
</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope.
</deferred>

---

*Phase: 3-Client Lifecycle with RAII*
*Context gathered: 2026-05-07*
