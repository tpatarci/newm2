# Heavy-use exerciser results

**Date:** 2026-08-30 · **Commit:** `e390477` · **Host:** Ubuntu 22.04, Xvfb
**Tool:** `scripts/stress-exerciser.sh`

Run because the operator asked whether the window manager is light on resources
and whether sustained heavy use produces excess memory growth. The existing
`[wm_resource_budget]` test could not answer that: it maps twenty clients,
measures once, and asserts a ceiling. That is a **steady-state** measurement, and
it would pass unchanged against a manager leaking a kilobyte per window, because
it never returns to the same window count twice.

A leak is only visible as a **trend across cycles that begin and end in the same
state**. Each cycle here creates N clients, moves, resizes and retitles them,
then destroys all of them — so every cycle ends at zero clients, and a healthy
process returns to roughly the same RSS.

---

## Run 1 — release build, 400 windows

`bash scripts/stress-exerciser.sh 20 20 :99`

| Cycle | RSS (kB) | Delta |
|---|---|---|
| 0 (baseline) | 11576 | — |
| 1–3 | → 11876 | +300 total |
| **4–11** | **11876** | **0 each** |
| 12 | 11880 | +4 |
| **13–20** | **11880** | **0 each** |

- **Windows created and destroyed:** 400
- **RSS 11576 → 11880 kB** — total growth **304 kB**
- **All growth in the first three cycles**, then flat: sixteen of the last
  seventeen cycles moved RSS by exactly zero
- **File descriptors 6 → 6**
- **X protocol errors: 0**
- **Process alive at the end:** yes

The shape is the important part. Growth concentrated in the opening cycles and
then perfectly flat is what allocator arena warm-up looks like. A leak would show
as a steady per-cycle increment that never levels off, and there is none: the
`+4 kB` at cycle 12 is a single page and is followed by eight more cycles of
zero.

## Run 2 — ASan/LSan build, 120 windows

`WM_BINARY=build/asan/wm2-born-again bash scripts/stress-exerciser.sh 10 12 :98`

RSS climbed steadily, roughly 500 kB per cycle, 32336 → 44960 kB. **That is not
a leak signal and must not be read as one:** AddressSanitizer's quarantine holds
freed memory deliberately so use-after-free can be detected, and redzones and
allocation metadata accumulate. Growing RSS is what a correct program looks like
under ASan.

The verdict that counts is LeakSanitizer's at exit:

```
-----------------------------------------------------
Suppressions used:
  count      bytes template
    136       5535 libfontconfig
-----------------------------------------------------
```

**No leaks.** The only entry is the known `libfontconfig` cache suppression
already accounted for in the Phase 8 evidence bundle — a one-time allocation
inside the font library, not per-window.

## The one X error observed

Cycle 4 of the ASan run logged a single:

```
wm2: X_GetProperty (0x1a0000a): BadWindow (invalid Window parameter)
```

A client died between the WM deciding to read a property from it and the server
processing that read. This is inherent X11 asynchrony rather than a defect: a
client may vanish at any instant, and the only way to close the window entirely
would be a server grab around every property read, which would cost far more than
it saves. Logging and continuing — what `WindowManager::errorHandler()` does — is
the correct handling.

Frequency is consistent with a timing race and not a logic error: **zero in 400
release-build destroys, one in 120 under ASan**, which slows everything down and
widens every race window. Distinct from deferred item 13, which is a different
call (`X_UnmapWindow`) on every destroy.

---

## What this establishes

- No memory leak under sustained window churn, by two independent instruments —
  a flat RSS trend and LeakSanitizer's own verdict.
- No file-descriptor leak: 6 at start, 6 after 400 windows.
- Steady-state footprint of **~11.9 MB** with the churn history above, against
  the enforced 24 MB budget and the project's 512 MB VPS constraint. That is
  roughly **2.3% of the target machine's RAM.**
- The process survives the churn.

## What it does NOT establish

- **Nothing about a long-lived session.** Twenty cycles is minutes, not days.
  Slow growth below this run's resolution would not show.
- **Nothing under a real remote-desktop transport.** This ran on Xvfb; VNC and
  XRDP add their own server-side buffers, which are not this process's RSS but
  do count against the same 512 MB.
- **Nothing about CPU under load.** The exerciser samples memory and descriptors,
  not processor time. Idle CPU is covered separately by
  `[wm_resource_budget]` (0 ticks over 30 s with twenty clients mapped).
- **Nothing about pathological clients** — no window storms faster than the WM
  can process, no clients that map and unmap in tight loops, no deliberately
  malformed properties. `[wm_malformed]` covers the last of those from a
  different angle.
