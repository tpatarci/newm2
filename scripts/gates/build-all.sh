#!/usr/bin/env bash
set -euo pipefail
#
# wm2-born-again build-and-test gate (D-04 blockers 1 and 2, TEST-06).
#
# Reproduces the checklist's "Build And Test Gates" section as ONE runnable
# command: configure, build and run the full test suite in three trees --
# Debug, Release, and ASan/UBSan -- and exit non-zero naming whichever tree
# failed.
#
#   bash scripts/gates/build-all.sh            # all three trees
#   bash scripts/gates/build-all.sh debug      # one tree, for iteration
#
# Shell conventions follow scripts/preflight.sh: `set -euo pipefail` on line 2,
# every failure line prefixed `wm2: ` on stderr to match the binary's own
# convention, and failures accumulated across trees so one run shows the full
# picture rather than only the first problem.
#
# Two deliberate design points:
#
#   1. `--no-tests=error` is mandatory (D-33, established in 08-01 Step F) and
#      is not decoration. Without it, a tree that configured with
#      BUILD_TESTS=OFF -- or whose test discovery silently produced nothing --
#      reports a green full-suite run having executed zero tests, which would
#      let this gate certify D-04 blocker 2 against an empty set. This script
#      runs the UNFILTERED suite, so there is no label expression here; the
#      flag alone is the protection. There is exactly one test-runner
#      invocation in this file and it carries the flag.
#
#   2. Sanitizer findings inside forked WM children are captured to files under
#      the build tree (never a bare fixed /tmp name -- threat T-8-TMP) and the
#      gate fails if any report file exists after the run. A child that writes
#      a report to its own stderr and exits is otherwise invisible to a green
#      test result (08-RESEARCH.md Pitfall 4).
#
# Compiler and linker warnings are recorded per tree in build/<tree>/build.log
# with a printed summary count, so the checklist's "no new warnings" line has an
# artifact to point at. Warnings are NOT fatal in this plan -- only recorded.

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
cd "$REPO_ROOT"

SAN_COMPILE_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
SAN_LINK_FLAGS="-fsanitize=address,undefined"

# Sanitizer report sinks, both under the CMake binary directory (T-8-TMP).
#   - GATE_LOG_PREFIX: reports from the ASan-instrumented test binaries ctest
#     launches directly.
#   - FIXTURE_LOG_DIR: reports from the WM children WmFixture forks, whose
#     log_path is set inside the fixture (tests/support/WmFixture.h).
SAN_REPORT_DIR="$REPO_ROOT/build/asan/sanitizer-reports"
GATE_LOG_PREFIX="$SAN_REPORT_DIR/gate"
FIXTURE_LOG_DIR="$REPO_ROOT/build/asan/wm-process-tests"

usage() {
    echo "usage: build-all.sh [debug|release|asan]"
    echo "  no argument   configure, build and test all three trees"
    echo "  debug         Debug tree only"
    echo "  release       Release tree only (also runs the link audit)"
    echo "  asan          AddressSanitizer/UBSan tree only"
}

# ---------------------------------------------------------------------------
# Argument handling
# ---------------------------------------------------------------------------

ALL_TREES=(debug release asan)
REQUESTED=()

case $# in
    0)
        REQUESTED=("${ALL_TREES[@]}")
        ;;
    1)
        case "$1" in
            debug|release|asan) REQUESTED=("$1") ;;
            -h|--help) usage; exit 0 ;;
            *)
                echo "wm2: build-all: unknown tree '$1' (expected one of: debug release asan)" >&2
                exit 2
                ;;
        esac
        ;;
    *)
        echo "wm2: build-all: too many arguments (expected at most one tree name)" >&2
        usage >&2
        exit 2
        ;;
esac

FAILED_TREES=()

fail_tree() {
    echo "wm2: build-all: $2" >&2
    FAILED_TREES+=("$1")
}

# ---------------------------------------------------------------------------
# Preflight -- a missing dependency must fail with a named cause here, not as
# an opaque configure error three steps later.
# ---------------------------------------------------------------------------

echo "== preflight =="
if ! bash "$REPO_ROOT/scripts/preflight.sh" --quiet; then
    echo "wm2: build-all: preflight failed; aborting before any build" >&2
    exit 1
fi
echo "  preflight OK"

# ---------------------------------------------------------------------------
# Per-tree steps
# ---------------------------------------------------------------------------

configure_tree() {
    local tree="$1"
    case "$tree" in
        debug)
            cmake -S "$REPO_ROOT" -B "build/$tree" \
                -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
            ;;
        release)
            cmake -S "$REPO_ROOT" -B "build/$tree" \
                -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
            ;;
        asan)
            cmake -S "$REPO_ROOT" -B "build/$tree" \
                -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON \
                -DCMAKE_CXX_FLAGS="$SAN_COMPILE_FLAGS" \
                -DCMAKE_EXE_LINKER_FLAGS="$SAN_LINK_FLAGS"
            ;;
    esac
}

build_tree() {
    local tree="$1"
    local log="build/$tree/build.log"
    cmake --build "build/$tree" --parallel 2>&1 | tee "$log"
}

report_warnings() {
    local tree="$1"
    local log="build/$tree/build.log"
    local count=0
    if [ -f "$log" ]; then
        count=$(grep -c 'warning:' "$log" || true)
    fi
    echo "  $tree: $count compiler/linker warning line(s) recorded in $log"
}

# The single test-runner invocation in this file. Every future edit must keep
# the --no-tests=error flag attached to it.
run_suite() {
    local tree="$1"
    ctest --test-dir "build/$tree" --output-on-failure --no-tests=error
}

# Runs the suite for one tree, exporting the sanitizer environment first when
# the tree is the instrumented one. Called in a subshell so the exports never
# leak into the other trees.
test_tree() {
    local tree="$1"
    if [ "$tree" = "asan" ]; then
        export ASAN_OPTIONS="log_path=${GATE_LOG_PREFIX}:exitcode=42:detect_leaks=1:abort_on_error=0"
        export LSAN_OPTIONS="suppressions=${REPO_ROOT}/tests/lsan.supp:fast_unwind_on_malloc=0:print_suppressions=1"
        export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0"
    fi
    run_suite "$tree"
}

# A LeakSanitizer log whose entire content is the "Suppressions used:"
# accounting block is informational, not a finding -- it is written on every
# clean run because the gate deliberately asks for that accounting (the WM
# fixture does not; it wants a zero-file predicate). Reporting it here is how a
# stale tests/lsan.supp entry becomes visible instead of rotting silently.
#
# The classification is fail-closed: a file is informational only if EVERY line
# matches that block's shape. Any unrecognised line -- and a real report always
# carries ERROR:/SUMMARY:/runtime error: lines -- makes the whole file a
# finding.
is_informational_report() {
    ! grep -qvE '^-+$|^Suppressions used:$|^ *count +bytes +template$|^ +[0-9]+ +[0-9]+ +[^ ]+$|^$' "$1"
}

# Any report file left under either prefix means a sanitizer fired somewhere in
# the process tree -- including inside a forked WM child, whose own stderr the
# test result would otherwise never surface (Pitfall 4).
check_sanitizer_reports() {
    local candidates=()
    local findings=()
    local informational=()
    local f
    if [ -d "$SAN_REPORT_DIR" ]; then
        for f in "$GATE_LOG_PREFIX".*; do
            [ -f "$f" ] && candidates+=("$f")
        done
    fi
    if [ -d "$FIXTURE_LOG_DIR" ]; then
        for f in "$FIXTURE_LOG_DIR"/asan*; do
            [ -f "$f" ] && candidates+=("$f")
        done
    fi

    for f in ${candidates[@]+"${candidates[@]}"}; do
        if is_informational_report "$f"; then
            informational+=("$f")
        else
            findings+=("$f")
        fi
    done

    if [ ${#informational[@]} -ne 0 ]; then
        echo "  asan: ${#informational[@]} suppression-accounting log(s); matched suppressions:"
        cat ${informational[@]+"${informational[@]}"} \
            | grep -E '^ +[0-9]+ +[0-9]+ +[^ ]+$' | sort -u | sed 's/^/    /' || true
    fi

    if [ ${#findings[@]} -eq 0 ]; then
        echo "  asan: no sanitizer findings"
        return 0
    fi

    echo "wm2: build-all: ${#findings[@]} sanitizer report file(s) found:" >&2
    for f in "${findings[@]}"; do
        echo "wm2: --- $f ---" >&2
        cat "$f" >&2
    done
    return 1
}

clear_sanitizer_reports() {
    mkdir -p "$SAN_REPORT_DIR"
    rm -f "$GATE_LOG_PREFIX".* 2>/dev/null || true
    if [ -d "$FIXTURE_LOG_DIR" ]; then
        rm -f "$FIXTURE_LOG_DIR"/asan* 2>/dev/null || true
    fi
}

# ---------------------------------------------------------------------------
# Link audit (checklist: "Verify the final executable links only to intended
# runtime libraries"). libXtst is the specific trap: it is a test-only
# dependency and must never appear in the shipped binary.
# ---------------------------------------------------------------------------

ALLOWED_LIBS="linux-vdso ld-linux-x86-64 ld-linux libX11 libXext libXft libfontconfig libXrender libXrandr libfreetype libm libstdc++ libgcc_s libc libdl libpthread librt libxcb libXau libXdmcp libexpat libuuid libpng16 libz libbrotlidec libbrotlicommon libbsd libmd"

link_audit() {
    # Parameterised so the audit itself can be negative-tested against a binary
    # that legitimately links libXtst (the test targets do).
    local bin="${1:-build/release/wm2-born-again}"
    if [ ! -x "$bin" ]; then
        echo "wm2: build-all: link audit skipped -- $bin not built" >&2
        return 1
    fi

    echo "== link audit ($bin) =="

    local out
    if ! out=$(ldd "$bin" 2>&1); then
        echo "wm2: build-all: ldd failed on $bin" >&2
        return 1
    fi

    local bad=0
    local soname base name
    while read -r soname _rest; do
        [ -z "$soname" ] && continue
        base=${soname##*/}
        name=${base%%.so*}
        case " $ALLOWED_LIBS " in
            *" $name "*) ;;
            *)
                if [ "$name" = "libXtst" ]; then
                    echo "wm2: build-all: link audit: libXtst is TEST-ONLY and must not be linked into the shipped binary" >&2
                else
                    echo "wm2: build-all: link audit: unexpected runtime library '$base'" >&2
                fi
                bad=$((bad + 1))
                ;;
        esac
    done <<< "$out"

    if [ "$bad" -ne 0 ]; then
        echo "wm2: build-all: link audit FAILED with $bad unexpected librar(ies)" >&2
        return 1
    fi

    echo "  link audit OK ($(echo "$out" | wc -l) entries, all in the intended runtime set)"
    return 0
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

for tree in "${REQUESTED[@]}"; do
    echo ""
    echo "== $tree =="

    if ! configure_tree "$tree"; then
        fail_tree "$tree" "$tree: configure failed"
        continue
    fi

    if ! build_tree "$tree"; then
        fail_tree "$tree" "$tree: build failed (see build/$tree/build.log)"
        report_warnings "$tree"
        continue
    fi
    report_warnings "$tree"

    if [ "$tree" = "asan" ]; then
        clear_sanitizer_reports
    fi

    if ! ( test_tree "$tree" ); then
        fail_tree "$tree" "$tree: test suite failed"
        if [ "$tree" = "asan" ]; then
            check_sanitizer_reports || true
        fi
        continue
    fi

    if [ "$tree" = "asan" ]; then
        if ! check_sanitizer_reports; then
            fail_tree "$tree" "asan: sanitizer report files were produced"
            continue
        fi
    fi

    if [ "$tree" = "release" ]; then
        if ! link_audit; then
            fail_tree "$tree" "release: link audit failed"
            continue
        fi
    fi

    echo "  $tree: OK"
done

echo ""
if [ ${#FAILED_TREES[@]} -ne 0 ]; then
    echo "wm2: build-all FAILED in tree(s): ${FAILED_TREES[*]}" >&2
    exit 1
fi

echo "build-all OK: ${REQUESTED[*]}"
exit 0
