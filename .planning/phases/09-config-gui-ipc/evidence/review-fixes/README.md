# Phase 9 review fixes: the external passes

Two reviewers, serialized (never both on the same commit at once), each
report archived here as it arrived, each finding verified against the code
before anything was changed. The two local passes (09-REVIEW.md,
09-REVIEW-PASS2.md) and the security audit (09-SECURITY.md) are recorded in
the phase directory itself.

## Codex (`codex review --base origin/main`, gpt-5.6-sol, read-only sandbox)

| Pass | Head | Findings | Outcome |
|---|---|---|---|
| 1 | b1963b3 | 4 | fixed (see codex-pass1-at-b1963b3.md) |
| 2 | 2dd6ce6 | 2 P2 | fixed: fullscreen exit replays live changes (60c758f); Reset follows a file re-read (e981cdd) |
| 3 | 0ffd584 | 2 P2 | fixed: the published socket path is withdrawn with the socket (d58e17a); a failed listener shuts the server down (8df55d8) |
| 4 | bcbc5f2 | 4 P2, 1 P3 | fixed: user-layer entries only are written (e4c5307); the entry list is bounded to one reply (01fe14c); socket strings follow the file's rules (b82308b, e66e612 for the newline); the selected duplicate row is edited (1a8d3d2); provenance by presence (ccc9f8e) |
| 5 | e66e612 | 1 P1, 2 P2 | fixed: colours canonicalised to the X11 spelling before the form holds them, and a refused live set is put back (90501ff); wire menu entries follow the file's size and newline rules (3a44168); a reload refuses when the user file cannot be examined (081d6a0) |

Stopping rule, set before pass 3: stop when a pass yields only naming or
exactness findings. Pass 6 is the check that the rule is met.

## CodeRabbit CLI (`cr review --agent -t committed --base-commit 0fec5db`)

The GitHub App is not installed on this repository, so the CLI is the only
CodeRabbit gate. The full diff (142 files, 2.6 MB) was refused at the
connect step three times; a one-commit run went through, so the pre-flight
was chunked by directory with `--dir`, one chunk per shared-pool window
under a bookkeeper grant each.

| Chunk | Head | Files | Findings | Outcome |
|---|---|---|---|---|
| last commit only | 8cdf6f4 | 1 | 1 major | fixed (bcbc5f2): fixture paths as quoted positional parameters |
| src | bcbc5f2 | 11 | 3 minor | fixed (57bfe70, ea40f1d, 4018ef4) |
| apps | bb5d331 | 14 | 3 minor | fixed (e12c0d2, 0a94290, d51143f) |
| include | d51143f | 10 | 1 minor | fixed (7f4a259) |
| tests | e66e612 | 12 | 1 minor | fixed (2dfa33e) |

Not sent: scripts (5 files), docs (1), packaging (2), the two root files,
and the 85 planning records. The four code directories were reviewed; the
remaining windows were yielded to another project's merge-blocking review.
Those directories are covered by the two local review passes and by Codex,
which reviews the whole branch.
