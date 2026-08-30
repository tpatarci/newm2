# Phase 8 evidence bundle

Snapshot taken at commit `c61cb4b` on branch `main`, 2026-08-30. This directory
is committed deliberately: it is the basis on which Phase 8's requirements are
marked satisfied, so it travels with the code rather than living on one machine.

The bundle is a **snapshot tied to one commit**. Nothing re-validates it when
later commits land and no automated check asserts it still matches the tree —
refreshing it is a manual per-release action (planner assumption TEST-08).

**One correction to that framing.** The `gates/` material is all from `c61cb4b`.
The per-target capability transcripts are NOT: each was captured when its session
was run and stamps its own commit and tree state in its header — `tigervnc/` at
`dba2f30` and `xrdp/` at `26344b4`, both `tree-state: DIRTY`, and both predating
four behaviour-changing commits. That has a visible consequence worth knowing
before reading them: `tigervnc/root-properties.txt` shows `_NET_CLIENT_LIST`
carrying the window manager's own menu and check windows, which was deferred
item 7 and was fixed afterwards in `12f6de8`. The transcripts are honest about
their own provenance; this note exists so the directory-level framing is too.

---

## READ THIS BEFORE ADDING ANYTHING TO THIS DIRECTORY

### `~/.xsession-errors` must never enter this bundle — SEVERITY: HIGH

On this validation host that file contains the operator's **entire environment in
plaintext**, rewritten by `dbus-update-activation-environment` on every xrdp
login. It held live values for `OPENAI_API_KEY`, `HF_TOKEN`,
`LARA_ACCESS_KEY_SECRET` and `LARA_ACCESS_KEY_ID`.

**Do not read, copy, quote, or commit its contents.** It is the natural file to
reach for when an X session misbehaves, which is exactly why this warning is at
the top of the index rather than buried in a summary.

The operator was advised to rotate those four keys. **Rotation has not been
confirmed** as of this snapshot.

The committed XRDP transcript was screened before commit: zero matches for
`API_KEY`, `ACCESS_KEY`, `SECRET`, `TOKEN`, private-key headers or bearer tokens.

### T-8-REMOTE — window titles leak into transcripts

`xwininfo -root -tree` records every window's title. On a real desktop those name
documents, repositories and correspondents. The committed transcripts were taken
on sessions with only test clients, but **anything added later must be read
before it is committed**. Screenshots carry the same exposure and are worse,
because a reviewer skims them without reading.

---

## Layout

### `gates/` — the four hard blockers (Task 4)

| File | What it is | Satisfies |
|---|---|---|
| `PROVENANCE.txt` | commit, branch, date, and the exit code of every gate | Release Evidence: commit hash and branch |
| `preflight-versions.log` | distro, compiler, CMake, pkg-config, X11 tooling, fontconfig chains | Environment Preflight (all six rows) |
| `build-all-debug.log` | configure + build + full ctest, Debug | Blocker 1; Build And Test Gates |
| `build-all-release.log` | the same for Release, plus the link audit | Blocker 1 |
| `build-all-asan.log` | the same for ASan/UBSan | Blocker 2 |
| `compiler-debug.log`, `compiler-release.log` | compiler/linker warning capture | "no new warnings" row |
| `static-analysis.log` | cppcheck 2.7 + clang-tidy 14 | Blocker 3 |
| `sanitizer-reports/` | six LSan accounting logs — **see below** | Blocker 2 |
| `ldd-release.txt` | runtime link surface of the shipped binary | "links only to intended libraries" |

**Results at this snapshot: debug 295/295, release 295/295, asan 295/295,
static-analysis OK, every gate exit 0.**

**On `sanitizer-reports/`:** these six files are **not findings**. Each contains
only LSan suppression accounting — 2 allocations, 288 bytes, matched by the
`libfontconfig` template. The ASan gate's own verdict line reads `no sanitizer
findings`. They are copied here in full precisely so that claim can be checked
rather than believed; a reviewer should open one and confirm it says
`Suppressions used:` and nothing else.

**On `static-analysis.log`:** cppcheck's 15 findings are all accounted for in the
content-hash baseline at `scripts/analysis/cppcheck-suppressions.xml`. Note that
cppcheck 2.7's CLI never computes the `<hash>` element, so the baseline is
keyed on `(id, fileName)` — line numbers deliberately excluded, or every edit
would invalidate it.

### Per-target capability transcripts

Each directory holds `capabilities.txt` (the extension matrix), plus
`root-properties.txt` and `window-tree.txt` where a WM was running.

| Directory | Status |
|---|---|
| `xrdp/` | **exercised.** SHAPE, RANDR, RENDER all present |
| `tigervnc/` | **exercised.** SHAPE, RANDR, RENDER all present |
| `local-xephyr/` | **exercised.** The nested-server runtime smoke transcript |
| `x11vnc-xvfb/` | capability capture only, used to validate the capture script |
| — | **X2Go: absent from THIS bundle.** Captured later, in `08.5-v1.0-closeout/evidence/x2go-nxagent/` (headless `nxagent`, SHAPE/RANDR/RENDER all present). **Correction:** this row previously said `x2goserver` "was never installed" — false; `/var/log/dpkg.log` stamps it at 2026-08-29 17:43, during this phase's own manual-pass window. It was installed and the session was not run. See D-8-X2GO as restated. |
| — | **TightVNC: absent — see deviation D-8-TIGHTVNC.** TigerVNC is a genuine proxy for it; X2Go has no such proxy. |

Both deviations are recorded in full, with reason, owner and follow-up, at the
bottom of `COMPILED_CODE_BEHAVIOR_CHECKLIST.md`.

### `screenshots/`

| File | Shows |
|---|---|
| `shaped-frame-x11vnc.png` | shaped frame where Shape is available |
| `rectangular-fallback-no-shape.png` | the no-Shape rectangular fallback |
| `local-xephyr-nested.png` | nested-server smoke run |
| `deferred-item-11-{BEFORE,AFTER}-*.png` | the sideways tab not tracking, then tracking, the title |
| `menu-highlight-erases-label-{BEFORE,AFTER}.png` | the highlight erasing its own row label, and fixed |

### `resource-budget/`

`BUDGET-DERIVATION.md` plus the debug and ASan calibration runs. The budget is
**enforced** by `[wm_resource_budget]`, not merely reported: measured RSS
11840 kB against a 24576 kB ceiling, idle CPU 0 ticks over 30 s. That is the
512 MB VPS constraint made executable.

---

## A caveat that will otherwise be misread

During the manual passes, Solaar and virt-manager opened on the host's `:0`
session wearing GNOME decoration rather than inside the remote session. **This is
not a wm2 bug.** One shared user D-Bus bus (`/run/user/1000/bus`) routed those
single-instance applications to their already-running instances on the host. It
is an artifact of validating on a workstation that also runs a desktop, and it
cannot occur on a VPS with no competing `:0`.

Recorded here because a future reader comparing transcripts against a screenshot
will otherwise file a phantom bug against window management.

## What this bundle does not establish

- **X2Go and TightVNC behaviour.** Not tested. See the deviations.
- **Per-item manual results.** Two manual passes were run by the operator
  (tpatarci, 2026-08-29 and 2026-08-30) and their findings are recorded in
  `08-14-SUMMARY.md`, but the second produced a general verdict rather than a
  row-by-row result. The 14 unticked rows in the checklist are the consequence.
- **Behaviour over measured latency.** Menus and move/resize were exercised over
  real XRDP and TigerVNC sessions — which is where three defects surfaced that no
  automated test had caught — but no latency was measured.
- **That any of this stays true.** See the snapshot caveat at the top.
