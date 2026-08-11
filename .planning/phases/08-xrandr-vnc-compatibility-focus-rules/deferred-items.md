# Deferred / Out-of-Scope Discoveries — Phase 08

## 1. Legacy `start_xvfb` ctest fixture hangs (PRE-EXISTING)

**Found during:** 08-01 Task 1, running the full `ctest --test-dir build/debug` suite.

**Symptom:** `ctest -R '^start_xvfb$'` never returns (reproduced in isolation,
exit 124 under a 45s timeout). All behavioral tests actually pass; ctest itself
blocks after "End testing".

**Cause:** `CMakeLists.txt` `start_xvfb` runs
`bash -c "Xvfb :99 ... & echo $! > pidfile && sleep 1"`. The backgrounded Xvfb
inherits ctest's stdout/stderr pipe and never closes it, so ctest waits forever
on the pipe even though the test command itself exited.

**Pre-existing:** yes — `git diff CMakeLists.txt` shows no changes to the
`start_xvfb` / `stop_xvfb` / `XVFB_PID_FILE` lines. 08-01 only appended the
tags-as-labels option to `catch_discover_tests()` calls.

**Impact:** any ctest invocation selecting a test that requires the
`xvfb_display` fixture (`test_smoke`, `test_client`, `test_xft_poc`,
`test_ewmh`) hangs. `test_wm_process` is unaffected — it owns its own display.

**Disposition:** blocks the 08-01 Task 3 acceptance criterion
`ctest -L '^(wm_process|ewmh|client)$' -j4`, so it is fixed under deviation
Rule 3 in Task 3 rather than deferred further. Recorded here because it was
discovered during Task 1 and is not caused by this phase.

## 2. clang-tidy families reported but not enforced (08-02 Task 2)

**Found during:** 08-02 Task 2, standing up the static-analysis gate.

The plan named five clang-tidy families for the fatal allowlist. Three of them
already fire on the current tree, and the same plan forbids editing production
code into compliance while requiring the gate to be green today. They are
therefore configured as reported-only in `.clang-tidy`, with the promotion path
documented in that file.

| Family | Count | Where | Note |
|---|---|---|---|
| `cppcoreguidelines-init-variables` | 140 | src/ + tests/ | Overwhelmingly `XGetWindowProperty` out-parameters (`actualType`, `actualFormat`, `nItems`, `bytesAfter`), uninitialised by design because Xlib fills them. Enforcing this would require a project-wide convention change, not a bug fix. |
| `bugprone-branch-clone` | 5 | `src/Buttons.cpp` x4 (lines 220, 242, 453, 475), `src/Client.cpp` x1 (line 1362) | **The only entries here that may be real defects.** "Repeated branch in conditional chain" in the menu/geometry code. Not investigated in 08-02 (out of scope: this plan builds the gate, it does not fix findings). Worth a look during the focus/rules work that touches `Buttons.cpp`. |
| `bugprone-use-after-move` | 3 | `tests/test_eventloop.cpp` x2, `tests/test_raii.cpp` x1 | Deliberate: the tests read a moved-from object to prove the move-only RAII wrappers null themselves out. Enforcing would require `NOLINT` on intentional test code. |

**Disposition:** deferred. The gate is real today on `bugprone-dangling-handle`
and `clang-analyzer-core.*` (both clean), and tightens by moving a family from
the checks list into the errors list once its findings are resolved.

## 3. cppcheck 2.7 cannot produce a native hash-anchored baseline (08-02 Task 2)

`cppcheck` accepts a `<hash>` suppression element but its CLI never computes a
per-finding hash. Verified on this host: `<hash>0</hash>` matches every finding
and any non-zero hash matches nothing. The gate therefore computes its own
content hash and derives a hash-free suppression file for cppcheck at run time
(see the header of `scripts/analysis/run-static-analysis.sh`).

**Disposition:** revisit if cppcheck is ever upgraded past the Ubuntu 22.04
archive version — a newer release that emits finding hashes would let layer 2
be replaced by cppcheck's own mechanism. Not a blocker; the current two-layer
design is strictly more precise than cppcheck's `(id, fileName)` matching.

## 4. Pre-existing `-Wunused-result` warning in `sigHandler` (Release only)

**Found during:** 08-02 Task 3, once `scripts/gates/build-all.sh` started recording
per-tree build logs and the Manager.cpp edit forced a Release recompile.

```
src/Manager.cpp:311:20: warning: ignoring return value of 'ssize_t write(int, const void*, size_t)'
                       declared with attribute 'warn_unused_result' [-Wunused-result]
  311 |         (void)write(s_pipeWriteFd, &c, 1);
```

**Pre-existing:** yes. `git diff -U0 src/Manager.cpp` for 08-02 Task 3 touches
only `spawn()`, `spawnArgv()` and `launchApp()` (lines 844+); `sigHandler` is
untouched. It appears only in the Release tree because `_FORTIFY_SOURCE` is
active at -O2, and it had never been surfaced before because the Release object
for `Manager.cpp` was already up to date.

**Note:** the `(void)` cast does not suppress `warn_unused_result` in GCC. The
usual fix is to consume the result (`if (write(...) < 0) { }`). Not fixed here:
out of scope for this plan, which builds gates rather than fixing findings.

**Disposition:** deferred. `build-all.sh` records warnings without failing on
them, so this is visible in `build/release/build.log` rather than silent.

## 5. Bare `ctest --test-dir build/asan` is red (PRE-EXISTING); the gate is green

**Found during:** 08-02 Task 3, running the plan's verification commands.

`ctest --test-dir build/asan --output-on-failure --no-tests=error` fails 3 xft
tests (#49 shaped-window text, #50 UTF-8 rendering, #51 rotated font):

```
==...==ERROR: LeakSanitizer: detected memory leaks
SUMMARY: AddressSanitizer: 288 byte(s) leaked in 2 allocation(s).
```

**Cause:** the fontconfig process-lifetime cache leak that `tests/lsan.supp`
exists to suppress. 08-01 wired those suppressions into the *forked WM child*
only (`WmFixture::sanitizerEnv`), never into the ASan-instrumented Catch2 test
binaries themselves. Nothing had surfaced it because 08-01 only ever ran the
asan tree with `-L '^wm_process$'`, and those tests do not touch Xft directly.

Confirmed to be the whole story: re-running the same test with
`LSAN_OPTIONS=suppressions=tests/lsan.supp:fast_unwind_on_malloc=0` passes.
The 288 bytes / 2 allocations match the `libfontconfig` suppression that
`build-all.sh` reports as matched on every asan run.

**Pre-existing:** yes. Independent of the 08-02 Task 3 `_exit()` change, which
touches only `spawn()`, `spawnArgv()` and `launchApp()`.

**Why not fixed in 08-02:** the fix is to attach `LSAN_OPTIONS` to the test
binaries, which means either 12 `catch_discover_tests(... PROPERTIES ...)`
edits in `CMakeLists.txt` (5 of which already carry other properties) or a
`__lsan_default_suppressions()` translation unit linked into every test target.
Both are non-trivial edits to a shared build file that the remaining twelve
Phase 8 plans will also be editing, for a condition this plan did not cause.

**Workaround in place:** `scripts/gates/build-all.sh asan` exports the declared
suppressions itself, so the sanitizer gate is green and reports the matched
suppression counts. Run the asan tree through the gate, not through bare ctest.

**Disposition:** deferred to the test-infrastructure work (08-11 … 08-13).
