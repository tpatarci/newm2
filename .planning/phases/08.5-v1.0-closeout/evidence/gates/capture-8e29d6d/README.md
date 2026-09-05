# First capture attempt at `8e29d6d` — NOT the gate bundle

**This directory is not the criterion-7 gate bundle.** It is the record of the
first 08.5-08 capture, run at commit `8e29d6d`, which the plan required to stop:
the static-analysis gate went red. The bundle at the top level of
`evidence/gates/` is the second capture, at the commit that fixed what this one
found. Nothing here is cited by a checklist row.

## What the first capture found

**1. Static analysis red — five cppcheck findings outside the baseline**
(`static-analysis-FAILED.log`, kept under its original content with the
distinguishing name the plan asks for). All five are in the ledger-8 modal-loop
rewrite from this closeout, none pre-existing:

| Finding | Where | Cause |
|---|---|---|
| `unreadVariable` 'done' | `src/Border.cpp` tab-button loop | `done = true; break;` — the store is dead because the `break` leaves the `while (!done)` loop |
| `unreadVariable` 'done' ×3 | `src/Client.cpp` move, resize and gesture loops | same pattern |
| `shadowVariable` 'w' | `src/Client.cpp` resize loop | the `ModalWait w` result shadowed the function's width local |

Fixed by deleting the dead stores and naming the result `wait` in all three
loops. No behaviour changes; the compiler had already eliminated the stores.
The baseline (`scripts/analysis/cppcheck-suppressions.xml`) was **not**
regenerated. Static analysis on the cleaned tree: `static-analysis OK`,
"15 finding(s), all accounted for in the baseline", clang-tidy "no enforced
check fired".

**2. The window manager published its own name one byte short.** Reading the
runtime-smoke window tree before committing it (T-8-REMOTE) showed the check
window titled `"wm2-born-agai"`. `src/Manager.cpp` passed a literal length of
13 for the 14-byte `_NET_WM_NAME` value on `_NET_SUPPORTING_WM_CHECK`. Present
since the EWMH work landed; visible in every prior transcript and never read.
Fixed by taking the length from the string. The `[wm_process]` case that
checks the supporting window now also reads `_NET_WM_NAME` as `UTF8_STRING`,
byte for byte, and requires `"wm2-born-again"`:
`red-wm-name-truncated.log` (fails with `"wm2-born-agai" == "wm2-born-again"`),
`green-wm-name-truncated.log` (17 assertions after).

## What else the first capture showed, unchanged in the second

- Debug, release and ASan trees all green at 335/335; six sanitizer report
  files, each the LeakSanitizer suppression-accounting stanza
  (`Suppressions used: 2 288 libfontconfig`), not findings.
- `preflight-versions.log` came out byte-identical to Phase 8's: the script
  prints only tool and library versions, the host has not changed, and the file
  carries no commit of its own. The second capture prefixes two header lines
  binding the reading to its commit, in the form `ldd-release.txt` already uses.
- The smoke run's stderr carries one `X_UnmapWindow ... BadWindow` at shutdown:
  deferred item 13 (the resize handle is a child of the client window, so every
  client destroy logs one). Pre-existing and recorded; not touched here.
- The credential screen's only match was the `flake-measurement/README.md`
  sentence that names the screen's own pattern.

## Why the twelve other files from this attempt are not kept

A bundle is bound to one commit. The build-all, compiler, preflight, ldd,
DOC-GUARDS, runtime-smoke and PROVENANCE files from `8e29d6d` were produced by
real runs, but keeping them beside the second capture's files would leave two
bundles interleaved in one directory, which is what the plan's coexistence rule
forbids. The red log is the artifact the plan says to keep; the rest are
superseded by the same commands run again at the fixed commit.
