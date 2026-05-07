# Phase 3: Client Lifecycle with RAII - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-07
**Phase:** 3-Client Lifecycle with RAII
**Areas discussed:** Client Ownership Model, O(1) Lookup Indexing, ICCCM Reparenting Atomicity, State Transition Enforcement

---

## Client Ownership Model

| Option | Description | Selected |
|--------|-------------|----------|
| unique_ptr in vector | vector<unique_ptr<Client>> replaces vector<Client*>. WM exclusively owns. Event handlers get raw Client* via .get(). | ✓ |
| Map as sole container | unordered_map<Window, unique_ptr<Client>> for both ownership and lookup. Loses insertion-order iteration. | |
| shared_ptr + weak_ptr | shared_ptr<Client> everywhere. More overhead, overkill for single-threaded WM. | |

**Notes:** User selected the recommended option for all ownership sub-questions.

| Sub-question | Selected |
|--------------|----------|
| Cleanup location | WM erase triggers dtor — no separate release() method |
| Hidden client tracking | Move unique_ptr between m_clients and m_hiddenClients |
| Borrowing in event handlers | Raw Client* pointers (non-owning) |

---

## O(1) Lookup Indexing

| Option | Description | Selected |
|--------|-------------|----------|
| Index client window only | unordered_map<Window, Client*> keyed by m_window. Border lookups fall back to linear scan. | ✓ |
| Index all windows | Index m_window + all border windows. Larger map, more complex insertion/removal. | |

| Sub-question | Selected |
|--------------|----------|
| Border window fallback | Map + vector fallback — map for O(1) client windows, linear scan for border windows |
| Map value type | Raw Client* (non-owning) — unique_ptr stays in the vector |

---

## ICCCM Reparenting Atomicity

| Option | Description | Selected |
|--------|-------------|----------|
| Tight grab around XReparentWindow only | Minimal grab window. Handles the main race. | ✓ |
| Wide grab for entire manage() | Longer grab, freezes all X clients during full setup. | |

| Sub-question | Selected |
|--------------|----------|
| Grab management | RAII ServerGrab class in x11wrap.h — constructor grabs, destructor ungrabs |

---

## State Transition Enforcement

| Option | Description | Selected |
|--------|-------------|----------|
| enum class + validation | enum class ClientState { Withdrawn, Normal, Iconic }. Validated transitions with warnings. | ✓ |
| Keep raw int | No validation, matches X11 convention. | |

| Sub-question | Selected |
|--------------|----------|
| Invalid transition handling | Warn + apply — log to stderr but accept the transition |
| X11 value mapping | Convert to/from X11 int values only at the X11 boundary |

---

## Claude's Discretion

- Exact ServerGrab class structure in x11wrap.h
- Test structure and naming conventions for Phase 3 tests
- unordered_map integration details with event dispatch
- Whether hidden client transfer uses std::move or swap

## Deferred Ideas

None — discussion stayed within phase scope.
