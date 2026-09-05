# Phase 8.5 (v1.0 closeout) evidence bundle

Snapshot taken at commit `39de54811084154ddac1d2115e5f950b0c9f1565` (`39de548`)
on branch `worktree-agent-a0ef668e51c39c8dd`, 2026-09-05
(`gates/PROVENANCE.txt`). This directory is committed deliberately: it is the
basis on which `COMPILED_CODE_BEHAVIOR_CHECKLIST.md` certifies the tree, so it
travels with the code rather than living on one machine.

The bundle is a **snapshot tied to one commit**. Nothing re-validates it when
later commits land and no automated check asserts it still matches the tree —
refreshing it is a manual per-release action, the same planner assumption
(TEST-08) Phase 8's bundle carried.

## READ THIS BEFORE ADDING ANYTHING TO THIS DIRECTORY

### `~/.xsession-errors` must never enter this bundle — SEVERITY: HIGH

On the validation host that file contains the operator's entire environment in
plaintext, rewritten by `dbus-update-activation-environment` on every xrdp
login: live `OPENAI_API_KEY`, `HF_TOKEN`, `LARA_ACCESS_KEY_SECRET`,
`LARA_ACCESS_KEY_ID`. **The operator has decided these four keys will not be
rotated (2026-08-30, `08.5-CONTEXT.md` security section).** That is their call
and it is recorded here, not re-argued. It does change one thing: with
rotation off the table, the handling rule below is the **only** remaining
control, which makes it more binding, not less.

**Do not read, copy, quote, or commit its contents.** It is the natural file
to reach for when an X session misbehaves, which is exactly why this warning
leads the index rather than sitting in a summary — this phase added two more
remote-desktop sessions (`tightvnc/`, `x2go-nxagent/`), which is exactly when
someone goes looking for it.

**The screen that was run:** a credential grep over this bundle —
`API_KEY|ACCESS_KEY|SECRET|TOKEN|PRIVATE KEY|Bearer ` — before any transcript
here was committed. Its only match anywhere under `evidence/gates/` is
`flake-measurement/README.md`'s own sentence *naming* that pattern; no actual
credential value appears in the bundle.

### T-8-REMOTE — window titles leak into transcripts

`xwininfo -root -tree` records every window's title. This bundle's captures
were taken on displays started fresh for their own runs, carrying only test
client titles (e.g. `wm2-smoke-39de548`, `wm2-tightvnc-validation`,
`wm2-x2go-validation`). **Anything added later must be read before it is
committed.** Screenshots carry the same exposure and are worse, because a
reviewer skims them without reading.

---

## One correction to the snapshot framing

The `gates/` material below is all from one commit, `39de548`. The two
per-target transcripts are **not**: each stamps its own commit in its
provenance header — `tightvnc/` at `9869aea` and `x2go-nxagent/` at `f5bbea7`
— captured earlier in this phase, before the gate bundle's commit. Both also
stamp `tree-state: DIRTY`, which is the established convention across every
transcript in this bundle and in Phase 8's four (not a regression here): the
exact source state behind a transcript cannot be reconstructed from its commit
hash alone, only from the hash plus the knowledge that the tree was dirty at
capture time.

## Layout

### `gates/` — the release-signoff bundle

| File / directory | What it is | Satisfies |
|---|---|---|
| `PROVENANCE.txt` | commit, branch, date, per-gate exit code, per-tree test totals, bundle inventory | Release Evidence: commit hash and branch |
| `preflight-versions.log` | distro, compiler, CMake, pkg-config, X11 tooling, fontconfig chains | Environment Preflight (all rows) |
| `build-all-debug.log` | configure + build + full ctest, Debug | Build And Test Gates: Debug |
| `build-all-release.log` | the same for Release, plus the link audit | Build And Test Gates: Release |
| `build-all-asan.log` | the same for ASan/UBSan | Build And Test Gates: ASan/UBSan |
| `compiler-debug.log`, `compiler-release.log`, `compiler-asan.log` | compiler/linker warning capture per tree | "no new warnings" row |
| `static-analysis.log` | cppcheck 2.7 + clang-tidy 14.0.0 | Build And Test Gates: static analysis |
| `sanitizer-reports/` | six LSan accounting logs — **see below** | ASan/UBSan logs (Release Evidence) |
| `ldd-release.txt` | runtime link surface of the shipped binary | "links only to intended libraries" |
| `runtime-smoke/` (`capabilities.txt`, `root-properties.txt`, `window-tree.txt`) | this phase's own nested-server smoke transcript, captured at the gate commit rather than inherited | Runtime smoke transcript (Release Evidence) |
| `DOC-GUARDS.txt` | the gap-1 documentation guards (XDIS-05) and the rule-key parity check (RULES-01), run at this commit | closes gap 1 recorded as open in `08.5-VERIFICATION.md` |

**Results at this snapshot, quoted from `PROVENANCE.txt`:** debug, release and
asan each exit 0 and each report "100% tests passed, 0 tests failed out of
337"; static-analysis exits 0; preflight exits 0; runtime-smoke exits 0.

**On `sanitizer-reports/`:** these six files are **not findings**. Each holds
only LSan suppression accounting — 2 allocations, 288 bytes, matched by the
`libfontconfig` template — and the ASan gate's own verdict line reads "no
sanitizer findings". They are copied here in full precisely so that claim can
be checked rather than believed.

### Measurement and record directories — NOT gate results

`evidence/gates/` also holds directories that document *how* the green gate
above was reached, not additional gate results. Each is described here by its
own README's opening line; none of them counts toward the totals above.

| Directory | What it is (its own README's opening) |
|---|---|
| `capture-attempts/` | Records the two 08.5-08 captures that ran, and were superseded, before the gate bundle at the top level — not the gate bundle itself. |
| `flake-measurement/` | The before-rate measurement: the success-criterion-7 bundle was held rather than produced against a flaking tree. |
| `attribution/` | Diagnosis rounds 2-3 narrowing the flake's cause; the criterion-7 bundle was still held. |
| `wakeup/` | Round 4 of the same diagnosis — the wake-up intervention; the criterion-7 bundle was still held. |
| `reproducer/` | A targeted, deterministic reproducer for the diagnosed defect, distinct from the sampling rounds around it. |
| `eventloop/` | Plan 08.5-11's round record for the event loop's readiness-ordering defect: four deterministic cases, RED then GREEN. |
| `timestamp/` | Plan 08.5-12's round record for the timestamp path: three defects fixed by reading and reproducing deterministically — no rate, no sampling, no p-value. |
| `menupaint/` | Plan 08.5-13's round record for dismembering the menu open-paint-sample path. |
| `modal/` | Ledger 8: the six modal grab loops that now observe SIGTERM instead of blocking it out. |
| `review-fixes/` | The Codex review series on PR #5 (six rounds) — findings reproduced RED, fixed, and confirmed GREEN. |
| `fix-measurement/` | **Not the gate bundle, and not a rate proof.** Three series of ten consecutive green debug runs: at `5ffee20` (330 tests), at `70fbd8f` (335) and at the final capture commit `39de548` (337), operational validation that the fix holds under repetition — not a statistical claim; see its own "What this does not establish" for the p-value arithmetic it deliberately declines to lean on. |

### Per-target transcripts

Each directory holds `capabilities.txt`, `wm-banner.txt`, `root-properties.txt`
and `window-tree.txt`, plus its own `SCOPE.md` stating what the capture does
and does not establish.

| Directory | Commit | Status |
|---|---|---|
| `tightvnc/` | `9869aea` | **the degraded-server result, and the one worth reading.** SHAPE yes, RANDR no, RENDER no — the only capture here against a server genuinely lacking what Phase 8 built fallbacks for. Retires D-8-TIGHTVNC on evidence. See `tightvnc/SCOPE.md`. |
| `x2go-nxagent/` | `f5bbea7` | **a floor, not a tick.** Headless `nxagent`, SHAPE/RANDR/RENDER all present, no X2Go client and no NX proxy in the path. See `x2go-nxagent/SCOPE.md`. |

### `INTERACTION-CHECKLIST.md`

The per-item manual walkthrough. **Tester** tpatarci (operator). **Date**
2026-08-30. **Target** TigerVNC `:11`, 1280x1024x24, loopback only. **Commit**
`0244c06`. 31 rows, four-value split: 10 PASS, 6 PASS (verdict), 11 DEFERRED, 4
n/a.

### `STRESS-RESULTS.md`

The churn measurement: repeated create/move/resize/retitle/destroy cycles that
return to zero clients each time, answering a question the enforced resource
budget (`[wm_resource_budget]`) cannot — that test maps clients once and
measures once, a steady-state check, not a trend. This file's two runs (400
windows release, 120 windows ASan/LSan) look for a leak across cycles instead;
it finds none, by a flat RSS trend and LeakSanitizer's own verdict.

## What this bundle does not establish

- **X2Go over a real connection.** `x2go-nxagent/` runs the agent nested and
  headless; it never puts X2Go's own NX compression proxy, its SSH channel or
  a real client in the path. That is what D-8-X2GO is restated around.
- **Anything judged by eye on TightVNC.** No viewer was attached and no
  screenshot taken; whether the RENDER-less core-X11 glyph path *looks*
  acceptable is unanswered by `tightvnc/`.
- **The eleven declined interaction rows.** `INTERACTION-CHECKLIST.md` records
  eleven rows the operator explicitly deferred. Nine of them have no automated
  coverage of any kind: the hidden-client menu row (#4), the menu exit row
  (#5), four circulation permutations (#7–#10), the circular fullscreen gesture
  and its noisy-input negative (#20, #21), and grab release as a property
  (#22). The other two carry partial automated coverage (#6 zero-client
  circulation, #19 the maximize state behind the middle-click). The v1.1
  "Gesture and input coverage" backlog line exists for exactly this.
- **Behaviour over measured latency.** No capture in this bundle measures
  latency; the closest evidence is the manual passes recorded outside this
  phase, over real XRDP and TigerVNC sessions.
- **That any of this stays true.** A gate bundle is a snapshot; nothing here
  re-validates it when a later commit lands. See the framing note at the top.
- **A rate for the flake fix.** `fix-measurement/`'s ten-run tables are
  operational validation — the debug gate green, ten times in a row, with no
  run omitted or retried — not a statistical rate proof. Its own README says
  so explicitly and gives the Fisher-exact arithmetic it declines to lean on;
  that wording is inherited here rather than softened.
