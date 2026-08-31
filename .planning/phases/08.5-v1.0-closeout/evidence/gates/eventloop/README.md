# 08.5-11 — the event loop's readiness-ordering defect

Round base: `7de1bdc4aa6328571f3be999796b75eb7ae5d8a4`

Host: Ubuntu 22.04.5 LTS, Linux 6.8.0-124-generic x86_64, libX11 2:1.7.5-1ubuntu0.3.

This directory holds the round's pins, its reference reproduction, and the
before/after logs for four deterministic cases. Every figure quoted here is
traceable to a committed log in this directory; nothing is restated from
memory.

## The cases

- **`pump-reports-what-is-available`** — with an event genuinely available, the
  pump must not report zero. It does, after the flush has moved the event into
  Xlib's queue and drained the socket.
- **`pump-reports-zero-when-idle`** — with nothing available, the pump reports
  zero. Passes at the round base and must keep passing, so that a pump
  rewritten to a constant cannot satisfy the case above.
- **`readable-is-not-deliverable`** — a readable descriptor whose bytes are a
  protocol error rather than an ordinary event must not yield
  deliver-an-event. It does.
- **`exit-flag-stops-the-loop`** — with the exit flag set, the decision must be
  to stop. The flag is accepted and never read.

## Status at this point in the round

`red-before-fix.log` records the three failing cases at Task 1's extraction
tip, captured before any fix existed. The remaining sections — the mechanism,
the correction of the superseded account, the before/after pairs, and the
round's limits — are completed in Task 4 against the green log.
