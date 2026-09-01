#!/usr/bin/env bash
set -euo pipefail
#
# wm2-born-again hang-time stack capture (plan 08.5-09, TEST-08; extended by
# plan 08.5-10 with the poll-timeout and socket receive-queue channels).
#
# Captures the kernel wait channel, a userspace backtrace, the blocking poll's
# own timeout argument and the receive-queue depth of the observed process's own
# sockets, for a wm2-born-again child that a test fixture started, while it is
# still wedged -- and records each channel as PRESENT with content or ABSENT
# with a stated reason.
#
#   bash scripts/diag/wm-stack-capture.sh --probe
#   bash scripts/diag/wm-stack-capture.sh <pid> <absolute-output-directory>
#
# Shell conventions follow scripts/gates/flake-run.sh: shebang on line 1,
# `set -euo pipefail` on line 2, SCRIPT_DIR/REPO_ROOT derived from
# ${BASH_SOURCE[0]} so no path is fixed, every failure line prefixed `wm2: ` on
# stderr to match the binary's own convention.
#
# Six design rules, each of which is a constraint this capture depends on:
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
#      ptrace_scope value, wchan readability and the socket-statistics tool, and
#      exits 0 whether or not each is available: a capability report is a
#      reading, not a pass or a fail. Nothing is installed by this script.
#
#   6. THE SOCKET READING IS INTEGERS ONLY, AND BELONGS TO THE OBSERVED PROCESS.
#      The host's unix socket table is READ but never written. That table names
#      every socket on the machine, including other users' paths, and these
#      bundles are committed to a public repository. Only rows whose local
#      inode belongs to a descriptor of the observed process are kept, and only
#      a socket count and integer queue depths are emitted -- no address, no
#      path, no peer, no row for any other process. A gate over the committed
#      bundle matches the whole line against an integers-only grammar rather
#      than searching for known-bad content, so a shape nobody anticipated
#      fails too.
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

    if command -v ss >/dev/null 2>&1; then
        echo "ss: PRESENT $(command -v ss) $(ss -V 2>/dev/null | head -1)"
    else
        echo "ss: ABSENT no socket-statistics tool on PATH"
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

# NOT truncated here, deliberately, and a review suggestion to do so was
# declined. capability.txt is CO-AUTHORED: this script contributes the channels
# it can observe from outside the process, and its caller appends further
# channels of its own (wake-arm among them) to the same file. Truncating at this
# point discards whatever the caller wrote before invoking the capture, which
# turns an eight-channel bundle into a five-channel one.
#
# The concern behind the suggestion is real but belongs to the CALLER: two
# captures aimed at one output directory do accumulate. Every caller in-tree
# gives each capture its own directory, so the accumulating case does not arise
# today.

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
# Channel: poll-timeout
#
# Derived from the backtrace file ALREADY ON DISK. No second debugger
# invocation, and nothing new is read from the observed process -- design rule 3
# permits exactly one debugger command and this channel does not spend it.
#
# What it decides: the blocking poll's own timeout argument. A NEGATIVE value
# means no deadline was armed, which is what an indefinitely blocked event loop
# looks like from outside. This is the discriminator plan 08.5-09 captured but
# never extracted: every committed bundle from that round would read the same
# way, and the reading only becomes evidence when it is compared against an arm
# that changes the outcome.
#
# The absent reasons are a small fixed set so a later comparison can group them.
# ---------------------------------------------------------------------------

if [ ! -f "$BT" ]; then
    echo "poll-timeout: ABSENT no backtrace file" >> "$CAP"
else
    FRAME0=$(grep -m1 '^#0[[:space:]]' "$BT" || true)
    if [ -z "$FRAME0" ]; then
        echo "poll-timeout: ABSENT no frame zero" >> "$CAP"
    else
        PT=$(printf '%s' "$FRAME0" | grep -oE 'timeout=-?[0-9]+' | head -1 | cut -d= -f2 || true)
        if printf '%s' "$FRAME0" | grep -q 'poll' && [ -n "$PT" ]; then
            echo "poll-timeout: PRESENT $PT" >> "$CAP"
        else
            echo "poll-timeout: ABSENT frame zero does not name the poll entry point with a timeout argument" >> "$CAP"
        fi
    fi
fi

# ---------------------------------------------------------------------------
# Channel: socket-recvq (design rule 6)
#
# What this channel EXCLUDES, and what it does NOT -- stated here because a
# reading recorded without its exclusion scope beside it gets read later as more
# than it is:
#
#   It does NOT distinguish "the bytes were already consumed by the transport"
#   from "the request was never sent". Both produce a receive-queue depth of
#   zero and this channel cannot tell them apart.
#
#   What it DOES exclude is the rival explanation in which bytes sit unread on
#   the connection while the poll fails to report the descriptor readable. That
#   would be a defect in the descriptor set or in the returned-events handling
#   rather than in the readiness check, and a non-zero depth at a trip would be
#   its signature.
#
# The host's unix socket table is read once and never written out. Rows are
# filtered to inodes belonging to this process's own descriptors, and only a
# socket count and integer queue depths are emitted.
# ---------------------------------------------------------------------------

if ! command -v ss >/dev/null 2>&1; then
    echo "socket-recvq: ABSENT no socket-statistics tool on PATH" >> "$CAP"
else
    INODES=""
    for FDLINK in "/proc/$PID/fd"/*; do
        [ -e "$FDLINK" ] || continue
        TARGET=$(readlink "$FDLINK" 2>/dev/null || true)
        case "$TARGET" in
            socket:\[*\])
                TARGET="${TARGET#socket:[}"
                INODES="$INODES ${TARGET%]}"
                ;;
        esac
    done

    if [ -z "${INODES// /}" ]; then
        echo "socket-recvq: ABSENT no descriptor of the socket form on the observed process" >> "$CAP"
    else
        # Fields 1-4 of every row are Netid, State, Recv-Q and Send-Q and are
        # positionally stable whatever the local address contains. The local
        # inode is the first purely numeric field from 5 onward. Only the
        # Recv-Q integer of a matching row is ever emitted.
        RECVQS=$(ss -x -a -n -H 2>/dev/null | awk -v want="$INODES" '
            BEGIN { n = split(want, a, " "); for (i = 1; i <= n; i++) if (a[i] != "") s[a[i]] = 1 }
            {
                # Field 6 is the local socket INODE, named explicitly rather
                # than found by scanning for the first numeric field from 5
                # onwards. Field 5 is the local address, and for a bound unix
                # socket that is a PATHNAME: a path whose last component is all
                # digits is itself numeric, so the old scan could match the path
                # and then test THAT against the inode set. A row matched on
                # such a coincidence belongs to some other process, and its
                # receive queue would have been reported as ours.
                if ($6 ~ /^[0-9]+$/ && $6 in s) print $3
            }' | tr '\n' ',' || true)
        RECVQS="${RECVQS%,}"

        if [ -z "$RECVQS" ]; then
            echo "socket-recvq: ABSENT no unix socket table row matched this process's descriptors" >> "$CAP"
        else
            SOCKETS=$(printf '%s' "$RECVQS" | tr ',' '\n' | grep -c . || true)
            MAXQ=$(printf '%s' "$RECVQS" | tr ',' '\n' | sort -n | tail -1)
            echo "socket-recvq: PRESENT sockets=$SOCKETS recvq=$RECVQS max=$MAXQ" >> "$CAP"
        fi
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
