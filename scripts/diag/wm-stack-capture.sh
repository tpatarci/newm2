#!/usr/bin/env bash
set -euo pipefail
#
# wm2-born-again hang-time stack capture (plan 08.5-09, TEST-08).
#
# Captures the kernel wait channel and a userspace backtrace of a wm2-born-again
# child that a test fixture started, while it is still wedged, and records each
# channel as PRESENT with content or ABSENT with a stated reason.
#
#   bash scripts/diag/wm-stack-capture.sh --probe
#   bash scripts/diag/wm-stack-capture.sh <pid> <absolute-output-directory>
#
# Shell conventions follow scripts/gates/flake-run.sh: shebang on line 1,
# `set -euo pipefail` on line 2, SCRIPT_DIR/REPO_ROOT derived from
# ${BASH_SOURCE[0]} so no path is fixed, every failure line prefixed `wm2: ` on
# stderr to match the binary's own convention.
#
# Five design rules, each of which is a constraint this capture depends on:
#
#   1. THE PID IS AN ARGUMENT AND NOTHING ELSE. No process is ever selected by
#      name, by command-line substring, or by scanning the process table. A
#      name-shaped selection matches the killing command's own wrapper and took
#      this host's shell down twice on 2026-08-30. There is deliberately no
#      pkill, no killall and no pgrep anywhere in this file, and a
#      comment-filtered gate over it counts those tokens and requires zero.
#
#   2. REFUSAL COMES BEFORE CAPTURE, AND TAKES BOTH ANCHORS. The tool attaches
#      only when /proc/<pid>/exe -- with a trailing " (deleted)" stripped,
#      because a rebuild leaves exactly that suffix on every already-running
#      child -- equals exactly $REPO_ROOT/build/debug/wm2-born-again or
#      $REPO_ROOT/build/asan/wm2-born-again, AND the DISPLAY in
#      /proc/<pid>/environ names a display inside the fixture's :120-:199 pool.
#      Both conditions, never either alone: a live desktop session launched from
#      this same build tree passes the exe anchor, and the pool condition is the
#      only thing that excludes it. One such process was running on :10.0 on
#      this host on 2026-08-31.
#
#   3. ONE DEBUGGER COMMAND, ARGUMENTS ONLY. No stack locals, no register dump,
#      no memory read. A frame's locals can carry a window title, a path or an
#      environment pointer into a bundle that is about to be committed to a
#      public repository.
#
#   4. THE BACKTRACE VERDICT IS THE FRAME COUNT, NEVER THE EXIT CODE. A batch
#      debugger invocation whose attach the kernel refuses still exits 0 --
#      observed directly on this host. "The tool ran" and "the tool produced a
#      backtrace" are different claims and only the second one is worth
#      anything, so the verdict is decided by counting frames.
#
#   5. NOTHING IS ASSUMED ABOUT THE HOST. --probe reports the debugger, the Yama
#      ptrace_scope value and wchan readability and exits 0 whether or not each
#      is available: a capability report is a reading, not a pass or a fail.
#      Nothing is installed by this script.
#
# Every path is derived from the caller's argument. There is no fixed
# temporary-directory name anywhere in this file (threat T-8-TMP).
#
# Exit codes: 0 success or capability report; 2 usage or argument error;
# 3 refusal (the PID is not an in-pool window manager this tree produced).

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)

PREFIX="wm2: wm-stack-capture:"
YAMA_PATH="/proc/sys/kernel/yama/ptrace_scope"

die() {
    echo "$PREFIX $*" >&2
    exit 2
}

# One prefixed line, naming the PID and the condition that failed -- and
# naming neither the refused process's command line nor its owner.
refuse() {
    echo "$PREFIX refused pid $1: $2" >&2
    exit 3
}

usage() {
    echo "usage: wm-stack-capture.sh --probe"
    echo "       wm-stack-capture.sh <pid> <absolute-output-directory>"
}

# ---------------------------------------------------------------------------
# --probe: a capability report. Always exits 0.
# ---------------------------------------------------------------------------

probe() {
    if command -v gdb >/dev/null 2>&1; then
        echo "gdb: PRESENT $(command -v gdb) $(gdb --version 2>/dev/null | head -1)"
    else
        echo "gdb: ABSENT no debugger on PATH"
    fi

    if [ -r "$YAMA_PATH" ]; then
        echo "ptrace-scope: PRESENT $(cat "$YAMA_PATH")"
    else
        echo "ptrace-scope: ABSENT $YAMA_PATH not readable"
    fi

    if probe_wchan=$(cat /proc/self/wchan 2>/dev/null); then
        echo "wchan: PRESENT self=${probe_wchan:-(empty)}"
    else
        echo "wchan: ABSENT /proc/self/wchan not readable"
    fi

    exit 0
}

# ---------------------------------------------------------------------------
# Argument handling
# ---------------------------------------------------------------------------

if [ $# -eq 0 ]; then
    usage >&2
    die "an argument is required"
fi

if [ "$1" = "--probe" ]; then
    probe
fi

if [ $# -ne 2 ]; then
    usage >&2
    die "capture mode takes exactly two arguments"
fi

PID="$1"
OUTDIR="$2"

case "$PID" in
    ''|*[!0-9]*) die "pid must be a positive integer" ;;
esac
if [ "$PID" -le 0 ]; then
    die "pid must be a positive integer"
fi

# ---------------------------------------------------------------------------
# Refusal gate (design rule 2) -- the first act, before any write.
# ---------------------------------------------------------------------------

EXE=$(readlink "/proc/$PID/exe" 2>/dev/null || true)
EXE="${EXE% (deleted)}"

case "$EXE" in
    "$REPO_ROOT/build/debug/wm2-born-again"|"$REPO_ROOT/build/asan/wm2-born-again") ;;
    *) refuse "$PID" "exe is not one of this repository's window-manager binaries" ;;
esac

DISP=$(tr '\0' '\n' < "/proc/$PID/environ" 2>/dev/null | grep -m1 '^DISPLAY=' || true)
DISP="${DISP#DISPLAY=}"

case "$DISP" in
    :1[2-9][0-9]|:1[2-9][0-9].*) ;;
    *) refuse "$PID" "DISPLAY is outside the fixture display pool :120-:199" ;;
esac

# ---------------------------------------------------------------------------
# Output directory validation
# ---------------------------------------------------------------------------

case "$OUTDIR" in
    /*) ;;
    *) die "output directory must be an absolute path" ;;
esac
case "$OUTDIR" in
    *..*) die "output directory must not contain a parent-directory component" ;;
esac

mkdir -p "$OUTDIR"

CAP="$OUTDIR/capability.txt"

# ---------------------------------------------------------------------------
# Channel: wchan
#
# A literal 0 means the task was running rather than sleeping. That is a
# reading, not an absence, and is recorded as PRESENT.
# ---------------------------------------------------------------------------

if WCHAN=$(cat "/proc/$PID/wchan" 2>/dev/null); then
    printf '%s\n' "$WCHAN" > "$OUTDIR/wchan.txt"
    echo "wchan: PRESENT ${WCHAN:-(empty)}" >> "$CAP"
else
    : > "$OUTDIR/wchan.txt"
    echo "wchan: ABSENT /proc/$PID/wchan not readable" >> "$CAP"
fi

# ---------------------------------------------------------------------------
# Channel: backtrace (design rules 3 and 4)
# ---------------------------------------------------------------------------

BT="$OUTDIR/backtrace.txt"

SCOPE="unknown"
if [ -r "$YAMA_PATH" ]; then
    SCOPE=$(cat "$YAMA_PATH")
fi

if ! command -v gdb >/dev/null 2>&1; then
    : > "$BT"
    echo "backtrace: ABSENT no debugger on PATH" >> "$CAP"
else
    gdb -batch -p "$PID" -ex 'thread apply all bt' > "$BT" 2>&1 || true

    FRAMES=$(grep -c '^#[0-9]' "$BT" || true)
    FRAMES="${FRAMES:-0}"

    if [ "$FRAMES" -ge 1 ]; then
        echo "backtrace: PRESENT frames=$FRAMES" >> "$CAP"
    else
        echo "backtrace: ABSENT zero frames returned, ptrace_scope=$SCOPE" >> "$CAP"
    fi
fi

# ---------------------------------------------------------------------------
# Post-detach process state
#
# Read AFTER the debugger has exited, so a debugger that left the inferior
# stopped is visible in the bundle rather than inferred. The state letter is the
# first whitespace-separated field after the LAST ')' in /proc/<pid>/stat --
# field 3 of the line overall. Parsing starts after the last ')' rather than at
# a fixed token offset because the comm field is parenthesised and may itself
# contain spaces and parentheses.
# ---------------------------------------------------------------------------

STAT_LINE=$(cat "/proc/$PID/stat" 2>/dev/null || true)
if [ -n "$STAT_LINE" ]; then
    STATE=$(printf '%s' "$STAT_LINE" | sed 's/.*) //' | awk '{print $1}')
    echo "proc-state: ${STATE:-?}" >> "$CAP"
else
    echo "proc-state: ?" >> "$CAP"
fi

exit 0
