#!/usr/bin/env bash
set -euo pipefail
#
# wm2-born-again flake-measurement wrapper (plan 08.5-06, TEST-08).
#
# Runs ONE observation of the debug gate and records the host conditions it ran
# under, into a log path the caller names. It exists so that a rate measured
# across several runs can be read back with the machine's state beside each run
# rather than reconstructed afterwards from memory.
#
#   bash scripts/gates/flake-run.sh path/to/attribution-debug-run1.log
#
# Shell conventions follow scripts/gates/build-all.sh and scripts/preflight.sh:
# `set -euo pipefail` on line 2, every failure line prefixed `wm2: ` on stderr
# to match the binary's own convention.
#
# Three design points, each of which is a rule this measurement depends on:
#
#   1. The gate is invoked UNMODIFIED, as `bash scripts/gates/build-all.sh
#      debug`. Plan 08.5-04 produced the before-rate by running exactly that
#      command by hand. A wrapper that added a flag, a filter or a second way
#      to run the suite would make the before and after rates incomparable,
#      which is the only thing this measurement is for.
#
#   2. The wrapper REFUSES to overwrite an existing output path, and that check
#      runs before anything else. A measurement is a sequence of distinct
#      observations; a run that overwrites a previous run's file destroys the
#      record of the run it replaces while leaving a directory that still looks
#      like a clean 1..N sequence. Refusing to clobber makes an in-place retry
#      require a deliberate `rm` instead of happening by a repeated command.
#
#   3. Every path is derived from the caller's argument. There is no fixed
#      temporary-directory name anywhere in this file (threat T-8-TMP), and the
#      run ledger is written beside the caller's log, never at a fixed location.
#
# The ledger is append-only: this script only ever uses `>>` against it. That is
# what makes "no run was retried" checkable rather than asserted -- a run
# repeated under a filename it had already used appears twice in the ledger, and
# a log deleted from the directory still leaves its ledger line behind, so the
# ledger and the directory listing disagree and a gate catches it.
#
# No process is stopped by name anywhere in this file. A process-name kill
# matches the killing command's own wrapper and took this host's shell down
# twice on 2026-08-30; the host sweep is a separate, by-PID operation and is
# deliberately NOT automated here.

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)

usage() {
    echo "usage: flake-run.sh <output-log-path>"
    echo "  <output-log-path>   required; must not already exist"
}

# ---------------------------------------------------------------------------
# Argument handling -- the output path is required and is the only argument.
# ---------------------------------------------------------------------------

if [ $# -eq 0 ]; then
    echo "wm2: flake-run: an output log path is required" >&2
    usage >&2
    exit 2
fi

if [ $# -gt 1 ]; then
    echo "wm2: flake-run: too many arguments (expected exactly one output log path)" >&2
    usage >&2
    exit 2
fi

case "$1" in
    -h|--help) usage; exit 0 ;;
esac

OUT_LOG="$1"

if [ -z "$OUT_LOG" ]; then
    echo "wm2: flake-run: the output log path must not be empty" >&2
    exit 2
fi

# ---------------------------------------------------------------------------
# No-clobber. This is the FIRST thing checked after the argument exists.
# ---------------------------------------------------------------------------

if [ -e "$OUT_LOG" ]; then
    echo "wm2: flake-run: refusing to overwrite an existing output path: $OUT_LOG" >&2
    echo "wm2: flake-run: a measurement run never replaces another run's record;" >&2
    echo "wm2: flake-run: remove it deliberately or choose the next run index" >&2
    exit 3
fi

OUT_DIR=$(cd "$(dirname "$OUT_LOG")" 2>/dev/null && pwd) || {
    echo "wm2: flake-run: output directory does not exist: $(dirname "$OUT_LOG")" >&2
    exit 2
}
OUT_BASE=$(basename "$OUT_LOG")
LEDGER="$OUT_DIR/RUN-LEDGER.txt"

cd "$REPO_ROOT"

# ---------------------------------------------------------------------------
# Host-condition header. Written into the run's own log so every figure in the
# per-run table can be read back from the record of the run it describes,
# rather than from a separate reading taken at some other moment.
#
# The orphaned-server count uses a BRACKETED pattern so the counting pipeline
# does not count itself. Nothing in this file spells that process name outside
# the bracketed form, for the same reason.
# ---------------------------------------------------------------------------

ORPHAN_SERVERS=$(ps -eo pid,args | grep -c '[X]vfb' || true)
HEAD_COMMIT=$(git rev-parse HEAD 2>/dev/null || echo "unknown")

{
    echo "=== flake-run host conditions ==="
    echo "date-utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "commit: $HEAD_COMMIT"
    echo "log: $OUT_BASE"
    echo "gate: bash scripts/gates/build-all.sh debug"
    echo "--- uptime ---"
    uptime
    echo "--- free -m ---"
    free -m
    echo "--- orphaned display servers (bracketed match) ---"
    echo "orphan-server-count: $ORPHAN_SERVERS"
    echo "=== gate output follows ==="
    echo ""
} > "$OUT_LOG"

# ---------------------------------------------------------------------------
# The gate. Invoked unmodified. `set -o pipefail` is already in force from
# line 2, so the gate's own exit code survives the tee into the log.
# ---------------------------------------------------------------------------

GATE_STATUS=0
bash scripts/gates/build-all.sh debug 2>&1 | tee -a "$OUT_LOG" || GATE_STATUS=${PIPESTATUS[0]}

{
    echo ""
    echo "=== flake-run result ==="
    echo "exit-code: $GATE_STATUS"
    echo "finished-utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} >> "$OUT_LOG"

# ---------------------------------------------------------------------------
# A red run keeps a name that says so, and keeps its index -- the naming
# precedent set by evidence/gates/flake-measurement/ (build-all-debug-run1-
# FAILED.log). The rename happens HERE, inside the wrapper and before the
# ledger line is written, so the name the ledger records is the name that
# ends up on disk. Renaming after the ledger line instead would make the
# ledger's name set and the directory's name set disagree for every red run,
# which is the check that is supposed to catch a deleted or hand-placed log.
# ---------------------------------------------------------------------------

if [ "$GATE_STATUS" -ne 0 ]; then
    FAILED_LOG="${OUT_LOG%.log}-FAILED.log"
    if [ -e "$FAILED_LOG" ]; then
        echo "wm2: flake-run: refusing to overwrite an existing failed-run path: $FAILED_LOG" >&2
        exit 3
    fi
    mv "$OUT_LOG" "$FAILED_LOG"
    OUT_LOG="$FAILED_LOG"
    OUT_BASE=$(basename "$OUT_LOG")
fi

# ---------------------------------------------------------------------------
# The run ledger. Exactly one appended line per invocation, append-only.
# ---------------------------------------------------------------------------

printf '%s %s %s %s\n' \
    "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$OUT_BASE" "$HEAD_COMMIT" "$GATE_STATUS" \
    >> "$LEDGER"

if [ "$GATE_STATUS" -ne 0 ]; then
    echo "wm2: flake-run: gate exited $GATE_STATUS (recorded in $OUT_BASE)" >&2
fi

exit "$GATE_STATUS"
