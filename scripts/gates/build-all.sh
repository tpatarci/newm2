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
#   bash scripts/gates/build-all.sh nogtk      # the GUI-disabled tree (CGUI-05)
#
# THE FOURTH TREE IS NOT IN THE DEFAULT SET, deliberately. The default set is the
# RELEASE-SIGNOFF matrix -- Debug, Release, ASan/UBSan -- and quietly adding a
# fourth member to it would change what a signoff means without saying so. The
# nogtk tree answers a different question ("does the window manager still build
# and pass everything with the configuration GUI switched off?", CGUI-05), it is
# selectable by name, and 09-09's evidence capture runs it explicitly.
#
# Shell conventions follow scripts/preflight.sh: `set -euo pipefail` on line 2,
# every failure line prefixed `wm2: ` on stderr to match the binary's own
# convention, and failures accumulated across trees so one run shows the full
# picture rather than only the first problem.
#
# Three deliberate design points:
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
#   3. The nogtk tree does not just have to be GREEN, it has to have run the
#      SAME SUITE. A GUI-disabled build certified by a run that silently
#      executed fewer tests is exactly the repudiation threat T-9-49 names, so
#      this gate compares the tree's registered test count against a GUI-ENABLED
#      reference tree, requires the difference between what the reference runs
#      and what nogtk PASSED to be accounted for entirely by tests that SKIPPED,
#      and re-runs each of those skipped tests verbosely to confirm it stated a
#      reason. A test that vanished from the registry, rather than skipping with
#      an explanation, fails the gate.
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
    echo "usage: build-all.sh [debug|release|asan|nogtk]"
    echo "  no argument   configure, build and test the three release-signoff trees"
    echo "  debug         Debug tree only"
    echo "  release       Release tree only (also runs the link audit)"
    echo "  asan          AddressSanitizer/UBSan tree only"
    echo "  nogtk         BUILD_CONFIG_GUI=OFF tree only (CGUI-05); NOT in the"
    echo "                default set, because the default set is the"
    echo "                release-signoff matrix and this tree answers a"
    echo "                different question. Also checks that the suite did not"
    echo "                shrink: every test the GUI-enabled tree runs must be"
    echo "                registered here too, and every one that did not run"
    echo "                must have skipped with a stated reason."
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
            debug|release|asan|nogtk) REQUESTED=("$1") ;;
            -h|--help) usage; exit 0 ;;
            *)
                echo "wm2: build-all: unknown tree '$1' (expected one of: debug release asan nogtk)" >&2
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
        nogtk)
            # BUILD_CONFIG_GUI=OFF, not a doctored PKG_CONFIG_LIBDIR: OFF is the
            # value that tells CMake NOT TO PROBE AT ALL, which is the stronger
            # of the two GUI-less states and the one a packager building a
            # GTK-free package would actually set.
            cmake -S "$REPO_ROOT" -B "build/$tree" \
                -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON \
                -DBUILD_CONFIG_GUI=OFF
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
#
# The output is teed to build/<tree>/ctest.log as well as to the terminal. That
# log is the artifact the nogtk count comparison below reads: the alternative is
# running the suite a second time to count it, which would double the cost of a
# gate on a machine this project is deliberately frugal about.
run_suite() {
    local tree="$1"
    ctest --test-dir "build/$tree" --output-on-failure --no-tests=error \
        2>&1 | tee "build/$tree/ctest.log"
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
# The GUI-disabled tree's accounting (CGUI-05, threat T-9-49)
#
# "It passed with the GUI off" is worth nothing on its own: a suite that shrank
# is a suite that can certify a build it never exercised. So three separate
# things are checked, and all three have to hold.
#
#   REGISTERED  the number of tests ctest knows about in this tree must equal
#               the number in a GUI-ENABLED reference tree. This is the half
#               that catches a test VANISHING -- a target that stopped being
#               registered because its dependency was not built.
#   ACCOUNTED   reference-registered minus nogtk-passed must equal the number of
#               nogtk tests that skipped. Nothing may go missing between the two
#               numbers without a skip to explain it.
#   REASONED    each skipped test is re-run verbosely and must print a reason.
#               Catch2's SKIP(msg) does; a silent return does not, and a silent
#               return is precisely what this check exists to refuse.
#
# The reference number is the GUI-enabled debug tree's REGISTERED count, read
# with `ctest -N`, which needs the tree built but not re-run. The debug arm of
# this same gate is what proves those registered tests actually pass.
# ---------------------------------------------------------------------------

# "Total Tests: N" from `ctest -N`.
registered_count() {
    local tree="$1"
    ctest --test-dir "build/$tree" -N 2>/dev/null \
        | sed -n 's/^Total Tests: \([0-9][0-9]*\)$/\1/p' | tail -1
}

# "N tests failed out of M" -> M, the number ctest attempted (skipped included).
attempted_count() {
    sed -n 's/.*tests failed out of \([0-9][0-9]*\).*/\1/p' "$1" | tail -1
}

# The names of the tests ctest reported as Skipped, one per line.
skipped_names() {
    sed -n 's/^\t *[0-9][0-9]* - \(.*\) (Skipped)$/\1/p' "$1"
}

# Every skipped test must have SAID WHY. Re-runs each one on its own with
# --verbose and looks for Catch2's SKIPPED marker plus a non-empty message.
check_skips_are_reasoned() {
    local tree="$1"
    shift
    local name rc out unreasoned=0
    for name in "$@"; do
        [ -z "$name" ] && continue
        out=$(ctest --test-dir "build/$tree" --verbose \
                    -R "^$(printf '%s' "$name" | sed 's/[][\\.^$*+?(){}|]/\\&/g')$" 2>&1) || true
        if echo "$out" | grep -q 'SKIPPED:' && echo "$out" | grep -q 'with message:'; then
            echo "  skipped with a reason: $name"
        else
            echo "wm2: build-all: $tree: test skipped WITHOUT a stated reason: $name" >&2
            unreasoned=$((unreasoned + 1))
        fi
    done
    return "$unreasoned"
}

# The whole accounting, run after the nogtk suite has gone green.
check_nogtk_accounting() {
    local tree="nogtk"
    local ref="debug"
    local log="build/$tree/ctest.log"

    if [ ! -f "$log" ]; then
        echo "wm2: build-all: nogtk: no ctest log to account for" >&2
        return 1
    fi

    # The GUI binary must not exist here. Asserted, not assumed -- the whole
    # tree means nothing if the thing it is supposed to be missing is present.
    if [ -e "build/$tree/wm2-config" ]; then
        echo "wm2: build-all: nogtk: wm2-config exists in a tree configured with BUILD_CONFIG_GUI=OFF" >&2
        return 1
    fi
    echo "  nogtk: no wm2-config binary in this tree, as configured"

    # The GUI-enabled reference. Built, not re-run.
    echo "== nogtk: building the GUI-enabled reference tree ($ref) to count against =="
    if ! configure_tree "$ref" >/dev/null; then
        echo "wm2: build-all: nogtk: reference tree $ref failed to configure" >&2
        return 1
    fi
    if ! cmake --build "build/$ref" --parallel >"build/$ref/reference-build.log" 2>&1; then
        echo "wm2: build-all: nogtk: reference tree $ref failed to build (see build/$ref/reference-build.log)" >&2
        return 1
    fi

    local ref_registered nogtk_registered nogtk_attempted
    ref_registered=$(registered_count "$ref")
    nogtk_registered=$(registered_count "$tree")
    nogtk_attempted=$(attempted_count "$log")

    local names_file="build/$tree/skipped-tests.txt"
    skipped_names "$log" > "$names_file"
    local nogtk_skipped
    nogtk_skipped=$(grep -c . "$names_file" || true)

    local nogtk_passed=$(( nogtk_attempted - nogtk_skipped ))

    echo "  $ref (GUI enabled):  $ref_registered tests registered"
    echo "  $tree (GUI disabled): $nogtk_registered registered, $nogtk_attempted attempted, $nogtk_passed passed, $nogtk_skipped skipped"

    local bad=0

    if [ -z "$ref_registered" ] || [ -z "$nogtk_registered" ] || [ -z "$nogtk_attempted" ]; then
        echo "wm2: build-all: nogtk: could not read the test counts out of ctest's own output" >&2
        return 1
    fi

    if [ "$nogtk_registered" -ne "$ref_registered" ]; then
        echo "wm2: build-all: nogtk: the suite SHRANK -- $nogtk_registered tests registered against $ref_registered in $ref. A test that vanishes is not a test that skipped." >&2
        bad=$((bad + 1))
    fi

    if [ "$nogtk_skipped" -eq 0 ]; then
        echo "wm2: build-all: nogtk: not one test skipped, so nothing in this tree noticed the GUI was absent" >&2
        bad=$((bad + 1))
    fi

    local difference=$(( ref_registered - nogtk_passed ))
    if [ "$difference" -ne "$nogtk_skipped" ]; then
        echo "wm2: build-all: nogtk: $difference test(s) of $ref's $ref_registered did not pass here, but only $nogtk_skipped skipped -- the difference is not accounted for" >&2
        bad=$((bad + 1))
    else
        echo "  accounted for: $ref_registered - $nogtk_passed passed = $difference, and $nogtk_skipped skipped"
    fi

    local unreasoned=0
    mapfile -t skipped_list < "$names_file"
    if [ "$nogtk_skipped" -gt 0 ]; then
        check_skips_are_reasoned "$tree" ${skipped_list[@]+"${skipped_list[@]}"} || unreasoned=$?
    fi
    if [ "$unreasoned" -ne 0 ]; then
        echo "wm2: build-all: nogtk: $unreasoned skipped test(s) stated no reason" >&2
        bad=$((bad + 1))
    fi

    [ "$bad" -eq 0 ]
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
        # "failed", not "skipped": this returns 1, and the caller treats it as a
        # failing audit. Calling it skipped described a non-event while the gate
        # went red, which sends whoever reads the log looking for the real
        # failure somewhere else.
        echo "wm2: build-all: link audit FAILED -- $bin not built" >&2
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

    if [ "$tree" = "nogtk" ]; then
        echo ""
        echo "== nogtk: same-suite accounting =="
        if ! check_nogtk_accounting; then
            fail_tree "$tree" "nogtk: the GUI-disabled run was not the same suite"
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
