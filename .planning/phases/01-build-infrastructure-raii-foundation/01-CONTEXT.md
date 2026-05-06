# Phase 1: Build Infrastructure + RAII Foundation - Context

**Gathered:** 2026-05-06
**Status:** Ready for planning

## Phase Boundary

CMake build system with pkg-config, fresh C++17 codebase written from scratch (not ported), RAII wrappers for all X11 pointer-based resources, and a Catch2 test harness on Xvfb. Phase 1 produces a working window manager binary with minimal functionality — the foundation that all subsequent phases build on.

## Implementation Decisions

### Source Layout & Migration
- **D-01:** Write fresh C++17 code in `src/` and `include/`. Keep `upstream-wm2/` as untouched reference — it is never compiled, only consulted for behavior.
- **D-02:** Split layout: `src/*.cpp` for implementation, `include/*.h` for headers.
- **D-03:** Entry point at `src/main.cpp`.
- **D-04:** File names match upstream classes: `Manager.cpp/h`, `Client.cpp/h`, `Border.cpp/h`, `Rotated.cpp/h`.

### RAII Wrapper Design
- **D-05:** `std::unique_ptr` with custom deleters for all X11 resource management. Zero overhead, idiomatic C++17, compile-time safety.
- **D-06:** `Window` (XID typedef) stays as raw `Window` type — it's an integer ID, not a pointer. All pointer-based resources (Display, GC, Cursor, Font, Pixmap, Colormap) are wrapped.
- **D-07:** All RAII type aliases and custom deleters in a single `include/x11wrap.h` header.

### C++17 Modernization Scope
- **D-08:** All-new C++17 code from scratch. No incremental porting of upstream files. Modern idioms (`std::string`, `std::vector`, `bool`, structured bindings) used throughout new code.
- **D-09:** Phase 1 produces a working WM binary with minimal window management functionality (open display, manage basic windows, basic frame decoration). Not just build infrastructure.
- **D-10:** Keep xvertext (`Rotated.C/h`) as a compiled third-party dependency without modernization. Phase 4 (Xft) replaces it entirely. Include it in CMake but don't rewrite it.

### Test Harness
- **D-11:** RAII unit tests (construct, destruct, move semantics, release) plus a WM smoke test (binary starts on Xvfb, manages a basic window). Not exhaustive coverage — that's for later phases.
- **D-12:** CMake-managed Xvfb setup. `ctest` starts Xvfb in test fixtures automatically. No manual Xvfb step required.
- **D-13:** Test files in root `tests/` directory: `tests/test_raii.cpp`, `tests/test_smoke.cpp`, etc.

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Project Context
- `.planning/PROJECT.md` — Project definition, core value, constraints, key decisions
- `.planning/REQUIREMENTS.md` — All requirements with traceability (BLD-01 through BLD-05, TEST-01 through TEST-04 for Phase 1)
- `.planning/ROADMAP.md` — Phase 1 goal, success criteria, and requirements mapping

### Codebase Analysis
- `.planning/codebase/STACK.md` — Current tech stack, build system, dependencies, platform requirements
- `.planning/codebase/ARCHITECTURE.md` — Class structure, event flow, window lifecycle, data flow, dependency graph
- `.planning/codebase/CONCERNS.md` — 296 lines of tech debt, known bugs, security issues, and fragile areas

### Upstream Reference
- `upstream-wm2/` — Original wm2 source code (untouched reference, never compiled in new build)

## Existing Code Insights

### Reusable Assets
- `upstream-wm2/Config.h` — Compile-time configuration values (colors, fonts, timing, focus policy). Reference for default values that Phase 1 hard-codes and Phase 5 makes configurable.
- `upstream-wm2/General.h` — Type definitions, atom names, utility macros. Reference for atom names and type mappings.
- `upstream-wm2/Cursors.h` — X11 bitmap data for cursor definitions. Can be included as-is in new code.
- `upstream-wm2/listmacro2.h` — Macro-based container. Reference only; replaced with `std::vector` in new code.

### Established Patterns
- Singleton WindowManager pattern — one instance owns everything. Preserved in new code.
- Client/Border circular dependency — resolved via forward declarations. Pattern continues.
- X11 resource lifecycle: create in constructor, destroy in destructor. RAII formalizes this.
- `m_` prefix for member variables — retained in new code for consistency with upstream.

### Integration Points
- `upstream-wm2/Makefile` — Link line shows all X11 libraries: `-lXext -lX11 -lXt -lXmu -lSM -lICE -lm`. CMake should use `pkg-config` for discovery; may drop `-lXt -lXmu -lSM -lICE` if unused (CONCERNS.md notes they appear unnecessary).
- X11 Shape extension — required for wm2's visual identity. CMake must check for it.

## Specific Ideas

No specific requirements — open to standard approaches. User emphasized "light, reliable, fast" for RAII wrappers, which drove the `unique_ptr` + custom deleters choice.

## Deferred Ideas

None — discussion stayed within phase scope.

---

*Phase: 1-Build Infrastructure + RAII Foundation*
*Context gathered: 2026-05-06*
