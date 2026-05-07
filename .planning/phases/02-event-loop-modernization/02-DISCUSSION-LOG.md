# Phase 2: Event Loop Modernization - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-07
**Phase:** 2-Event Loop Modernization
**Areas discussed:** Signal wakeup mechanism, Timer architecture, Auto-raise behavior, Event compression

---

## Signal Wakeup Mechanism

| Option | Description | Selected |
|--------|-------------|----------|
| Self-pipe trick | Signal handler writes a byte to a pipe monitored by poll(). Wakes immediately, no race conditions. Standard POSIX pattern. | ✓ |
| signalfd | Linux-specific fd that becomes readable when signals arrive. Cleaner API but less common in X11 WMs. | |
| EINTR + flag check | Rely on poll() returning EINTR. Simplest but has race condition (signal between flag check and poll()). | |

**User's choice:** Self-pipe trick (Recommended)
**Notes:** Reliability on VPS/VNC is the priority. Self-pipe is well-proven and race-free.

---

## Timer Architecture

| Option | Description | Selected |
|--------|-------------|----------|
| poll() timeout | Calculate minimum timeout from active timers, pass to poll(). Timer logic mixed with event logic. | |
| timerfd | Linux timerfd objects for each timer. Cleaner separation but adds fds and Linux-specific API. | |
| chrono + poll timeout | steady_clock deadlines with poll() timeout. Pure C++17, no platform APIs, no extra fds. | ✓ |

**User's choice:** chrono + poll timeout
**Notes:** Clean C++17 types, no extra file descriptors, no platform-specific APIs. Good fit for two simple timers.

---

## Auto-Raise Behavior

| Option | Description | Selected |
|--------|-------------|----------|
| Match original exactly | 80ms pointer-stopped check, then 400ms auto-raise countdown. Uses existing Phase 1 member variables. | ✓ |
| Config-ready timing | Same timing but structured for Phase 5 config overrides. More abstraction than needed now. | |
| Simplified (skip stopped check) | Just track entry + 400ms elapsed. Less faithful to original. | |

**User's choice:** Match original exactly (Recommended)
**Notes:** Phase 1 member variables are already set up for this pattern. Just need real implementation replacing stubs.

---

## Event Compression

| Option | Description | Selected |
|--------|-------------|----------|
| Match original | No active compression. MotionNotify handler sets flags (cheap). XCheckMaskEvent in eventEnter drains queued events. | ✓ |
| Compress MotionNotify | Drain all queued MotionNotify, process only last one. Standard technique in modern WMs. | |
| Compress all high-freq events | Compress MotionNotify, EnterNotify, LeaveNotify, ConfigureNotify, Expose. More reduction but more code and risk. | |

**User's choice:** Match original (Recommended)
**Notes:** Sufficient for lightweight WM on VPS. The flag-setting approach is already cheap.

---

## Claude's Discretion

None — user made all decisions directly.

## Deferred Ideas

None — discussion stayed within phase scope.
