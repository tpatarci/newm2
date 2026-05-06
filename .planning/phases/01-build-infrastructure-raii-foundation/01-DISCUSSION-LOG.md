# Phase 1: Build Infrastructure + RAII Foundation - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-06
**Phase:** 1-Build Infrastructure + RAII Foundation
**Areas discussed:** Source layout & migration, RAII wrapper design, C++17 scope in Phase 1, Test harness boundaries

---

## Source Layout & Migration

| Option | Description | Selected |
|--------|-------------|----------|
| New src/ + include/ | Write fresh C++17 code in src/ and include/, keep upstream-wm2/ as untouched reference | ✓ |
| Rename in-place | Rename upstream-wm2/ files from .C to .cpp and modernize in-place | |
| Copy then modernize | Copy upstream files to src/ preserving names, modernize there | |

**User's choice:** New src/ + include/ (Recommended)
**Notes:** Clean separation from legacy code; upstream stays as reference only.

| Option | Description | Selected |
|--------|-------------|----------|
| src/ + include/ (split) | src/*.cpp + include/*.h — conventional CMake layout | ✓ |
| src/ only (co-located) | All in src/, simpler but less conventional | |
| You decide | Let planner decide | |

**User's choice:** Split layout
**Notes:** Conventional CMake project structure.

| Option | Description | Selected |
|--------|-------------|----------|
| src/main.cpp | Matches upstream Main.C (20 lines). Simple and minimal. | ✓ |
| Match binary name | src/wm2.cpp — matches binary name but less conventional. | |

**User's choice:** src/main.cpp

| Option | Description | Selected |
|--------|-------------|----------|
| Match upstream names | Manager.cpp, Client.cpp, Border.cpp — clear lineage from upstream | ✓ |
| Descriptive names | WindowManager.cpp, WindowClient.cpp — self-documenting but deviates | |
| You decide | Let planner decide | |

**User's choice:** Match upstream names

---

## RAII Wrapper Design

| Option | Description | Selected |
|--------|-------------|----------|
| unique_ptr + custom deleters | Lightweight, idiomatic C++17, zero overhead | ✓ |
| Dedicated wrapper classes | More control (validation, logging) but more code | |
| Generic template | DRY for all types but harder to debug/customize | |

**User's choice:** unique_ptr + custom deleters
**Notes:** User emphasized "light, reliable, fast" — drove selection of the lightest option.

| Option | Description | Selected |
|--------|-------------|----------|
| Window stays raw | Window is just an integer ID (XID typedef), not a pointer | ✓ |
| All 7 types wrapped uniformly | XDestroyWindow on destruction — adds overhead for what is an integer | |

**User's choice:** Window stays raw, others wrapped

| Option | Description | Selected |
|--------|-------------|----------|
| Single x11wrap.h | All type aliases in one header, simple to include | ✓ |
| Per-type headers | More modular but overkill for a small WM | |
| You decide | Let planner decide | |

**User's choice:** Single x11wrap.h

---

## C++17 Scope in Phase 1

| Option | Description | Selected |
|--------|-------------|----------|
| All-new C++17 code | Write from scratch with modern idioms throughout | ✓ |
| Port incrementally | Replace char*, Boolean, listmacro step by step | |
| Build infra only | Only CMake + RAII wrappers, no modernization of WM code | |

**User's choice:** All-new C++17 code
**Notes:** Clean slate approach — no legacy patterns in new source tree.

| Option | Description | Selected |
|--------|-------------|----------|
| Working binary, minimal WM | Produces a running WM with basic functionality | ✓ |
| Build infra only, no WM code | Just build system + wrappers, WM code starts in Phase 2 | |

**User's choice:** Working binary with minimal WM functionality
**Notes:** Early validation that the architecture works.

| Option | Description | Selected |
|--------|-------------|----------|
| Keep xvertext as-is | Third-party dep, Phase 4 replaces with Xft. Pragmatic. | ✓ |
| Modernize xvertext now | More work now for code that gets replaced in Phase 4 | |

**User's choice:** Keep xvertext as-is
**Notes:** Don't rewrite code that's being thrown away in Phase 4.

---

## Test Harness Boundaries

| Option | Description | Selected |
|--------|-------------|----------|
| RAII unit tests + WM smoke test | Unit tests for wrappers, integration test that WM starts on Xvfb | ✓ |
| RAII unit tests only | No WM integration testing in Phase 1 | |
| Full test suite | RAII, event loop, client lifecycle — overlaps Phase 2/3 | |

**User's choice:** RAII unit tests + WM smoke test

| Option | Description | Selected |
|--------|-------------|----------|
| CMake-managed Xvfb | CTest starts Xvfb in fixtures. Automated, CI-ready. | ✓ |
| Manual Xvfb setup | Script-based, requires manual step | |

**User's choice:** CMake-managed Xvfb

| Option | Description | Selected |
|--------|-------------|----------|
| Root tests/ directory | tests/test_raii.cpp, tests/test_smoke.cpp. Conventional. | ✓ |
| Co-located with source | src/test_*.cpp. Tighter coupling. | |

**User's choice:** Root tests/ directory

---

## Claude's Discretion

No areas deferred to Claude — user made all decisions.

## Deferred Ideas

None — discussion stayed within phase scope.
