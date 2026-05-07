# Phase 2: Event Loop Modernization - Context

**Gathered:** 2026-05-07
**Status:** Ready for planning

## Phase Boundary

Replace the `select()`+`goto` event loop with a clean `poll()`-based architecture. Implement real auto-raise timing (80ms pointer-stopped + 400ms raise delay) using C++17 chrono. Add self-pipe signal wakeup for reliable clean shutdown. The loop must process X11 events with the same behavior as the original wm2.

## Implementation Decisions

### Signal Wakeup Mechanism
- **D-01:** Use the self-pipe trick for signal-safe poll() wakeup. Signal handler writes a byte to a pipe monitored by poll(). Wakes the loop immediately, no race conditions between checking `m_signalled` and calling poll(). Standard POSIX pattern.

### Timer Architecture
- **D-02:** Use `<chrono>` steady_clock deadlines with poll() timeout. Track timer deadlines in C++ types, compute minimum timeout for poll(). No extra file descriptors, no platform-specific timerfd. Pure C++17.
- **D-03:** Two timers needed: pointer-stopped detection (80ms) and auto-raise countdown (400ms). Both managed through the same chrono deadline mechanism.

### Auto-Raise Behavior
- **D-04:** Match original wm2 behavior exactly. When pointer enters a window: start tracking pointer movement via MotionNotify. If pointer stops for 80ms (pointer-stopped), begin 400ms auto-raise countdown. If pointer moves during countdown, reset. After 400ms of stillness, raise the window.
- **D-05:** Use the existing Phase 1 member variables (`m_focusChanging`, `m_focusPointerMoved`, `m_focusPointerNowStill`, `m_focusCandidate`, `m_focusTimestamp`) — they are already wired into the event loop. Replace the stubs with real timing logic.

### Event Compression
- **D-06:** Match original behavior — no active event compression. MotionNotify handler sets flags for focus tracking (already cheap). XCheckMaskEvent in `eventEnter` already drains queued EnterNotify. Sufficient for a lightweight WM on VPS.

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Project Context
- `.planning/PROJECT.md` — Project definition, core value, constraints, key decisions
- `.planning/REQUIREMENTS.md` — EVNT-01, EVNT-02, EVNT-03 requirements with traceability
- `.planning/ROADMAP.md` — Phase 2 goal, success criteria, and requirements mapping

### Prior Phase Decisions
- `.planning/phases/01-build-infrastructure-raii-foundation/01-CONTEXT.md` — Phase 1 decisions (RAII wrappers, source layout, C++17 idioms)

### Codebase Analysis
- `.planning/codebase/ARCHITECTURE.md` — Event flow diagram, WindowManager class responsibilities, current event dispatch
- `.planning/codebase/CONCERNS.md` — Known issues with the current event loop

### Source Files (Primary Targets)
- `src/Events.cpp` — Current event loop implementation (`loop()`, `nextEvent()`) with select()+goto
- `src/Manager.cpp` — Signal handler, focus stubs, constructor that calls `loop()`
- `include/Manager.h` — WindowManager class with focus tracking members, `nextEvent()` declaration

### Upstream Reference
- `upstream-wm2/Manager.C` — Original event loop and auto-raise timing logic for behavioral reference

## Existing Code Insights

### Reusable Assets
- `include/x11wrap.h` — RAII wrappers. The self-pipe will need its own fd management (not X11 resources), but the RAII pattern applies.
- `include/Manager.h:115-121` — Focus tracking members (`m_focusChanging`, `m_focusPointerMoved`, etc.) already declared and initialized. Just need real implementation.
- `src/Events.cpp:92-96` — MotionNotify handler already sets focus tracking flags. Wire into the new timer system.

### Established Patterns
- Singleton WindowManager — the loop runs inside the constructor, returns exit code. This pattern continues.
- `m_` prefix for member variables — retained.
- RAII for all resources — the self-pipe fds should follow this pattern (custom deleter or unique_ptr with close).

### Integration Points
- `Manager.cpp:103` — `loop()` is called from constructor. Return value becomes exit code.
- `Events.cpp:119` — `nextEvent()` is the blocking wait function. This is the primary rewrite target.
- `Events.cpp:9-116` — `loop()` dispatch is clean switch statement. Only `nextEvent()` needs full rewrite.
- `Manager.cpp:65-73` — Signal handlers already installed via `sigaction()`. Just need to add pipe write.

## Specific Ideas

No specific requirements — the original wm2 behavior is the reference. Key constraint: the new loop must produce identical observable behavior (timing, event ordering, signal response) while using clean poll()-based code with no goto statements.

## Deferred Ideas

None — discussion stayed within phase scope.

---

*Phase: 2-Event Loop Modernization*
*Context gathered: 2026-05-07*
