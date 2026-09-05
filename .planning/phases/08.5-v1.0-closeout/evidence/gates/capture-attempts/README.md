# Earlier capture attempts — NOT the gate bundle

**This directory is not the criterion-7 gate bundle.** It records the two
08.5-08 captures that ran before the one at the top level of `evidence/gates/`,
each stopped by something its own logs surfaced. The plan says a red log stays
in the bundle under a distinguishable name and a finding is fixed rather than
retried away; this is where those logs and the fixes they led to are kept.
Nothing here is cited by a checklist row.

## Attempt 1 — commit `8e29d6d`: static analysis red, and a name one byte short

**Static analysis red** (`8e29d6d-static-analysis-FAILED.log`): five cppcheck
findings outside the baseline, all in the ledger-8 modal-loop rewrite from this
closeout, none pre-existing.

| Finding | Where | Cause |
|---|---|---|
| `unreadVariable` 'done' | `src/Border.cpp` tab-button loop | `done = true; break;` — the store is dead because the `break` leaves the `while (!done)` loop |
| `unreadVariable` 'done' ×3 | `src/Client.cpp` move, resize and gesture loops | same pattern |
| `shadowVariable` 'w' | `src/Client.cpp` resize loop | the `ModalWait w` result shadowed the function's width local |

Fixed by deleting the dead stores and naming the result `wait` in all three
loops; the compiler had already eliminated the stores, so no behaviour changed.
The baseline (`scripts/analysis/cppcheck-suppressions.xml`) was **not**
regenerated.

**The window manager published its own name one byte short.** Reading the
runtime-smoke window tree before committing it (T-8-REMOTE) showed the check
window titled `"wm2-born-agai"`. `src/Manager.cpp` passed a literal length of
13 for the 14-byte `_NET_WM_NAME` value on `_NET_SUPPORTING_WM_CHECK`. Present
since the EWMH work landed and visible in every prior transcript; never read.
Fixed by taking the length from the string. The `[wm_process]` case that
checks the supporting window now also reads `_NET_WM_NAME` as `UTF8_STRING`,
byte for byte, and requires `"wm2-born-again"`:
`8e29d6d-red-wm-name-truncated.log` (fails with
`"wm2-born-agai" == "wm2-born-again"`), `8e29d6d-green-wm-name-truncated.log`
(17 assertions after). Both fixes: commit `2a94cbb`.

## Attempt 2 — commit `2a94cbb`: every gate green, one new compiler warning

All three trees green at 335/335, static analysis `OK`, smoke transcript
carrying the full name. Reading `compiler-release.log` before committing it
(`2a94cbb-compiler-release.log`, kept in full) showed **one** warning line, where
Phase 8's release log had none:

```
src/Manager.cpp:434:20: warning: ignoring return value of 'ssize_t write(int, const void*, size_t)'
declared with attribute 'warn_unused_result' [-Wunused-result]
```

That is the self-pipe signal handler from the 08.5-11 event-loop fix. glibc
marks `write()` `warn_unused_result` and GCC does not accept a `(void)` cast as
using the result; the warning appears only in the release tree because the
attribute is enforced under `-O2`. The checklist's "no new compiler or linker
warnings" row is exactly the gate this is for, so it was fixed rather than
recorded: the result is bound to a named local at both self-pipe sites
(`src/Manager.cpp`, `src/Events.cpp`), and a release build of the window
manager target then compiles with no warning line. The plain `write` at
`src/Events.cpp` had not warned but is the same pattern and got the same
treatment.

## Attempt 3 — commit `d08b6e5`: every gate green; superseded by the branch reviews

All four blockers green (335/335 in each tree, zero warning lines, static
analysis OK, smoke transcript and PROVENANCE agreeing on the commit). It was
committed as the bundle. The two reviewer passes over the whole branch that
followed — CodeRabbit CLI and Codex, recorded in `../review-fixes/README.md` —
then changed `src/` and `tests/` four more times (a hide-or-kill on a signal
during a tab-button press, an uninitialised event read on gesture interruption,
a one-pixel-short frameless maximize, two test-helper hardenings), so the
bundle no longer described the shipping tree. Nothing in it was red; nothing
from it is kept here because every file is reproduced by the same commands at
the final commit.

## Unchanged across the attempts, carried into the final bundle

- Six sanitizer report files per ASan run, byte-identical to each other, each
  the LeakSanitizer suppression-accounting stanza
  (`Suppressions used: 2 288 libfontconfig`), not findings.
- `preflight-versions.log` from attempt 1 was byte-identical to Phase 8's: the
  script prints only tool and library versions, the host has not changed, and
  the file carried no commit of its own. From attempt 2 on, two header lines
  bind the reading to its commit, in the form `ldd-release.txt` already uses.
- The smoke run's stderr carries one `X_UnmapWindow ... BadWindow` at shutdown:
  deferred item 13 (the resize handle is a child of the client window, so every
  client destroy logs one). Pre-existing and recorded; not touched.
- The credential screen's only match is the `flake-measurement/README.md`
  sentence that names the screen's own pattern.
- `runtime-smoke/capabilities.txt` stamps the HEAD of the moment the smoke ran,
  which can be a documentation commit after the capture commit; PROVENANCE
  names both and the no-drift diff between them.

## Why the other files from each attempt are not kept

A bundle is bound to one commit. The build-all, compiler, preflight, ldd,
DOC-GUARDS, runtime-smoke and PROVENANCE files from `8e29d6d` and `2a94cbb`
were produced by real runs, but keeping them beside the final capture would
leave three bundles interleaved in one directory, which the plan's coexistence
rule forbids. What each attempt found is kept; what it merely repeated is
superseded by the same commands run again at the fixed commit.
