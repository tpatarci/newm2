#!/usr/bin/env bash
set -euo pipefail
#
# wm2-born-again static-analysis gate (D-03, D-04 blocker 3, TEST-06).
#
#   bash scripts/analysis/run-static-analysis.sh
#   bash scripts/analysis/run-static-analysis.sh --regenerate-baseline
#
# Exits 0 when the tree produces no static-analysis finding that is not already
# in the checked-in baseline, and non-zero the moment a new one appears.
#
# Shell conventions follow scripts/preflight.sh and scripts/gates/build-all.sh.
#
# ===========================================================================
# Why two mechanisms
# ===========================================================================
#
# The two tools genuinely differ (08-RESEARCH.md Pitfall 8):
#
#   cppcheck    supports a suppression file, so it gets a real baseline.
#   clang-tidy  has no baseline concept at all -- its only suppression is a
#               per-line NOLINT comment -- so it gets an explicit allowlist of
#               fatal checks in the repository .clang-tidy instead.
#
# ===========================================================================
# Why the cppcheck baseline is NOT fed to cppcheck directly
# ===========================================================================
#
# The baseline must survive unrelated edits: a line-number-anchored list
# reports a false "new finding" on the first edit above it, which is how a
# static-analysis gate rots into something people disable.
#
# cppcheck's suppression format does have a <hash> element, but cppcheck 2.7's
# CLI never COMPUTES a per-finding hash. Verified on this host against
# cppcheck 2.7: a suppression carrying <hash>0</hash> matches every finding
# (because the finding's own hash is always 0) and a suppression carrying any
# non-zero hash matches nothing at all. A hash written into a file handed
# straight to cppcheck would therefore silently suppress nothing.
#
# So the anchoring is done here, in two layers:
#
#   Layer 1 (native, coarse).  A DERIVED suppression file is generated at run
#     time from the baseline by dropping the <hash> elements and de-duplicating
#     on (id, fileName). It carries no line numbers, so it is immune to line
#     drift by construction. cppcheck consumes that derived file via
#     --suppress-xml together with --error-exitcode=1, which is what makes an
#     entirely new finding class fail the gate natively.
#
#   Layer 2 (content hash, exact).  Every finding of the current run is hashed
#     over (id, file, message, source line) -- deliberately NOT over the line
#     number. That multiset is compared against the <hash> values in the
#     baseline. This is the layer that catches a new finding of an ALREADY
#     baselined id in an ALREADY baselined file, which layer 1 would absorb,
#     and it is also what surfaces a stale baseline entry whose code has since
#     been fixed.
#
# A consequence worth knowing: editing the source line under a baselined
# finding changes its hash, so it is reported as one new finding plus one stale
# entry. That is intended -- the code under the finding changed and deserves a
# fresh look. Resolve it with --regenerate-baseline and review the diff.

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
cd "$REPO_ROOT"

BASELINE="$REPO_ROOT/scripts/analysis/cppcheck-suppressions.xml"
TIDY_CONFIG="$REPO_ROOT/.clang-tidy"
COMPDB="$REPO_ROOT/build/debug/compile_commands.json"

# Marker prefix on every cppcheck finding line. Chosen so a line of C++ source
# echoed back inside the finding cannot be mistaken for a new record.
CC_MARK='@@WM2FINDING@@'

usage() {
    echo "usage: run-static-analysis.sh [--regenerate-baseline]"
    echo "  (no argument)          run the gate; never rewrites the baseline"
    echo "  --regenerate-baseline  explicit operator action: rewrite"
    echo "                         scripts/analysis/cppcheck-suppressions.xml"
    echo "                         from the current findings, then stop"
}

REGENERATE=0
case $# in
    0) ;;
    1)
        case "$1" in
            --regenerate-baseline) REGENERATE=1 ;;
            -h|--help) usage; exit 0 ;;
            *)
                echo "wm2: static-analysis: unknown argument '$1'" >&2
                usage >&2
                exit 2
                ;;
        esac
        ;;
    *)
        echo "wm2: static-analysis: too many arguments" >&2
        usage >&2
        exit 2
        ;;
esac

# ---------------------------------------------------------------------------
# Preflight. A missing tool or missing compilation database must fail with a
# named cause -- never silently skip half the gate, which would let a green
# result certify nothing.
# ---------------------------------------------------------------------------

PREFLIGHT_FAILURES=0

pf_fail() {
    echo "wm2: static-analysis: $*" >&2
    PREFLIGHT_FAILURES=$((PREFLIGHT_FAILURES + 1))
}

command -v cppcheck >/dev/null 2>&1 \
    || pf_fail "cppcheck not found on PATH (install it: sudo apt-get install -y cppcheck)"
command -v clang-tidy >/dev/null 2>&1 \
    || pf_fail "clang-tidy not found on PATH (install it: sudo apt-get install -y clang-tidy)"
command -v sha256sum >/dev/null 2>&1 \
    || pf_fail "sha256sum not found on PATH (install coreutils)"

[ -f "$COMPDB" ] \
    || pf_fail "compilation database missing: $COMPDB (configure it: cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON)"
[ -f "$TIDY_CONFIG" ] \
    || pf_fail "clang-tidy configuration missing: $TIDY_CONFIG"

if [ "$REGENERATE" -eq 0 ] && [ ! -f "$BASELINE" ]; then
    pf_fail "cppcheck baseline missing: $BASELINE (create it: $0 --regenerate-baseline)"
fi

if [ "$PREFLIGHT_FAILURES" -ne 0 ]; then
    echo "wm2: static-analysis: preflight FAILED with $PREFLIGHT_FAILURES problem(s)" >&2
    exit 1
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

FAILURES=0

fail() {
    echo "wm2: static-analysis: $*" >&2
    FAILURES=$((FAILURES + 1))
}

# ===========================================================================
# cppcheck half
# ===========================================================================

CPPCHECK_VERSION=$(cppcheck --version | awk '{print $2}')

# The scan paths are exactly src and include. upstream-wm2/ is reference-only
# and never compiled, and the FetchContent tree lives under build/; both are
# named in -i as well so the exclusion is explicit rather than incidental.
cppcheck_run() {
    cppcheck --enable=all --std=c++17 -Iinclude \
        -i upstream-wm2 -i build \
        --template="${CC_MARK}|{id}|{file}|{message}|{code}" \
        --template-location="${CC_MARK}LOCATION" \
        "$@" \
        src include
}

# One record per finding: "<hash>\t<id>\t<file>\t<text>".
#
# The hash covers id, file, message and the source line, and deliberately NOT
# the line number -- that omission is the whole anti-rot property.
normalise_findings() {
    local line rest id file text norm h
    while IFS= read -r line; do
        rest=${line#"${CC_MARK}|"}
        id=${rest%%|*}
        rest=${rest#*|}
        file=${rest%%|*}
        text=${rest#*|}
        # Collapse whitespace so reindentation alone does not change the hash.
        norm=$(printf '%s' "$text" | tr -s ' \t' ' ' | sed 's/^ //; s/ $//')
        h=$(printf '%s\037%s\037%s' "$id" "$file" "$norm" | sha256sum | cut -c1-16)
        printf '%s\t%s\t%s\t%s\n' "$h" "$id" "$file" "$norm"
    done
}

echo "== cppcheck $CPPCHECK_VERSION =="

cppcheck_run 2>&1 >/dev/null | grep "^${CC_MARK}|" > "$WORK/raw.txt" || true
normalise_findings < "$WORK/raw.txt" > "$WORK/current.tsv"
CURRENT_COUNT=$(wc -l < "$WORK/current.tsv")
echo "  $CURRENT_COUNT finding(s) in src/ and include/ before suppression"

# --- XML helpers -----------------------------------------------------------

xml_escape() {
    # &, < and > must be escaped; "--" is rewritten because the human-readable
    # anchor is carried in an XML comment and "--" cannot appear inside one.
    printf '%s' "$1" | sed 's/&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g; s/--/- -/g'
}

# --- --regenerate-baseline -------------------------------------------------

if [ "$REGENERATE" -eq 1 ]; then
    {
        echo '<?xml version="1.0"?>'
        echo '<!--'
        echo '  cppcheck static-analysis baseline for wm2-born-again (D-03, TEST-06).'
        echo ''
        echo "  cppcheck version: $CPPCHECK_VERSION"
        echo "  generated:        $(date -u +%Y-%m-%d)"
        echo "  generated by:     scripts/analysis/run-static-analysis.sh --regenerate-baseline"
        echo ''
        echo '  Hash stability is tool-version dependent. A cppcheck upgrade can change'
        echo '  message wording and therefore every hash here; that is why the version is'
        echo '  recorded above. Regenerate and REVIEW THE DIFF after any upgrade.'
        echo ''
        echo '  This file is NOT passed to cppcheck directly. cppcheck 2.7 accepts the'
        echo '  hash element but never computes a per-finding hash, so it would match'
        echo '  nothing. The gate derives a hash-free suppression file from this one at'
        echo '  run time; see the header of run-static-analysis.sh.'
        echo ''
        echo '  Each entry is anchored by a content hash over (id, file, message, source'
        echo '  line) with the line number deliberately excluded, so unrelated edits that'
        echo '  shift line numbers do not produce false new-finding failures.'
        echo '-->'
        echo '<suppressions>'
        while IFS=$'\t' read -r h id file text; do
            echo "  <!-- $(xml_escape "$text") -->"
            echo '  <suppress>'
            echo "    <id>$(xml_escape "$id")</id>"
            # cppcheck reports "nofile" when a finding has no location at all
            # (missingIncludeSystem); such an entry is anchored on id alone.
            if [ -n "$file" ] && [ "$file" != "nofile" ]; then
                echo "    <fileName>$(xml_escape "$file")</fileName>"
            fi
            echo "    <hash>$h</hash>"
            echo '  </suppress>'
        done < "$WORK/current.tsv"
        echo '</suppressions>'
    } > "$BASELINE"

    echo ""
    echo "wm2: static-analysis: BASELINE REWRITTEN -- $CURRENT_COUNT entr(ies) in $BASELINE" >&2
    echo "wm2: static-analysis: this is an explicit operator action, not a gate result." >&2
    echo "wm2: static-analysis: REVIEW 'git diff -- $BASELINE' BEFORE COMMITTING." >&2
    echo "wm2: static-analysis: every added entry is a defect you are choosing to accept." >&2
    exit 0
fi

# --- Layer 1: native cppcheck run against a derived suppression file -------

{
    echo '<?xml version="1.0"?>'
    echo '<!-- GENERATED at gate time from cppcheck-suppressions.xml. Do not edit. -->'
    echo '<suppressions>'
    awk '
        /<suppress>/    { id=""; fn="" }
        /<id>/          { s=$0; sub(/.*<id>/,"",s); sub(/<\/id>.*/,"",s); id=s }
        /<fileName>/    { s=$0; sub(/.*<fileName>/,"",s); sub(/<\/fileName>.*/,"",s); fn=s }
        /<\/suppress>/  { print id "\t" fn }
    ' "$BASELINE" | sort -u | while IFS=$'\t' read -r id fn; do
        [ -z "$id" ] && continue
        echo '  <suppress>'
        echo "    <id>${id}</id>"
        [ -n "$fn" ] && echo "    <fileName>${fn}</fileName>"
        echo '  </suppress>'
    done
    echo '</suppressions>'
} > "$WORK/derived.xml"

DERIVED_COUNT=$(grep -c '<suppress>' "$WORK/derived.xml" || true)
echo "  derived suppression file: $DERIVED_COUNT de-duplicated (id, fileName) entr(ies), no line numbers"

set +e
cppcheck_run --suppress-xml="$WORK/derived.xml" --error-exitcode=1 \
    2> "$WORK/native.err" > /dev/null
NATIVE_STATUS=$?
set -e

grep "^${CC_MARK}|" "$WORK/native.err" > "$WORK/native.txt" || true

# A stale baseline entry (its code was fixed) shows up as unmatchedSuppression.
# That is reported loudly but is NOT fatal: a developer who repairs a
# pre-existing defect must not be blocked by the gate.
UNMATCHED=$(grep -c "^${CC_MARK}|unmatchedSuppression|" "$WORK/native.txt" || true)
OTHER=$(grep -v "^${CC_MARK}|unmatchedSuppression|" "$WORK/native.txt" | grep -c "^${CC_MARK}|" || true)

if [ "$OTHER" -ne 0 ]; then
    echo "wm2: static-analysis: cppcheck reported $OTHER finding(s) outside the baseline:" >&2
    grep -v "^${CC_MARK}|unmatchedSuppression|" "$WORK/native.txt" | sed "s/^${CC_MARK}|/  /" >&2
    fail "cppcheck native run failed (exit $NATIVE_STATUS)"
else
    echo "  native run clean against the derived suppressions"
fi

# --- Layer 2: exact content-hash comparison --------------------------------

grep -o '<hash>[0-9a-f]*</hash>' "$BASELINE" \
    | sed 's/<hash>//; s/<\/hash>//' | sort > "$WORK/baseline.hashes"
cut -f1 "$WORK/current.tsv" | sort > "$WORK/current.hashes"

comm -13 "$WORK/baseline.hashes" "$WORK/current.hashes" > "$WORK/new.hashes"
comm -23 "$WORK/baseline.hashes" "$WORK/current.hashes" > "$WORK/gone.hashes"

NEW_COUNT=$(wc -l < "$WORK/new.hashes")
GONE_COUNT=$(wc -l < "$WORK/gone.hashes")

if [ "$NEW_COUNT" -ne 0 ]; then
    echo "wm2: static-analysis: $NEW_COUNT cppcheck finding(s) NOT in the baseline:" >&2
    while read -r h; do
        grep -m1 "^$h" "$WORK/current.tsv" | cut -f2- | sed 's/^/  /' >&2
    done < "$WORK/new.hashes"
    fail "cppcheck: new finding(s) versus $BASELINE"
fi

if [ "$GONE_COUNT" -ne 0 ]; then
    echo "  NOTE: $GONE_COUNT stale baseline entr(ies) -- the finding is gone, so the"
    echo "        entry is now dead weight. Not fatal. Re-run with"
    echo "        --regenerate-baseline to drop it, and review the diff."
elif [ "$UNMATCHED" -ne 0 ]; then
    echo "  NOTE: cppcheck reported $UNMATCHED unmatched suppression(s) in the derived file."
fi

if [ "$NEW_COUNT" -eq 0 ] && [ "$OTHER" -eq 0 ]; then
    echo "  cppcheck OK: $CURRENT_COUNT finding(s), all accounted for in the baseline"
fi

# ===========================================================================
# clang-tidy half
# ===========================================================================

CLANG_TIDY_VERSION=$(clang-tidy --version | sed -n 's/.*LLVM version \([0-9.]*\).*/\1/p' | head -1)
echo ""
echo "== clang-tidy ${CLANG_TIDY_VERSION:-unknown} =="

# Every project translation unit named by the compilation database, not a
# hand-picked file. Deduplicated (a source compiled into several targets
# appears once per target) and stripped of the FetchContent tree.
grep -o '"file": *"[^"]*"' "$COMPDB" \
    | sed 's/.*"file": *"//; s/"$//' \
    | grep -v '/_deps/' \
    | sort -u > "$WORK/tus.txt"

TU_COUNT=$(wc -l < "$WORK/tus.txt")
if [ "$TU_COUNT" -eq 0 ]; then
    fail "clang-tidy: the compilation database named zero project translation units"
else
    echo "  $TU_COUNT project translation unit(s) from $COMPDB"

    JOBS=$(nproc 2>/dev/null || echo 4)
    set +e
    xargs -a "$WORK/tus.txt" -P "$JOBS" -I{} \
        clang-tidy --config-file="$TIDY_CONFIG" -p "$REPO_ROOT/build/debug" --quiet {} \
        > "$WORK/tidy.out" 2> "$WORK/tidy.err"
    set -e

    # Belt and braces: the header scoping in .clang-tidy should already keep the
    # FetchContent tree out, but a diagnostic from there is dropped here too.
    grep -v '/_deps/' "$WORK/tidy.out" > "$WORK/tidy.filtered" || true

    TIDY_ERRORS=$(grep -c ' error: ' "$WORK/tidy.filtered" || true)
    TIDY_WARNINGS=$(grep -c ' warning: ' "$WORK/tidy.filtered" || true)

    echo "  $TIDY_ERRORS fatal, $TIDY_WARNINGS reported-only diagnostic(s)"
    if [ "$TIDY_WARNINGS" -ne 0 ]; then
        grep ' warning: ' "$WORK/tidy.filtered" \
            | sed 's/.*\[\([a-z0-9-]*\)\]$/\1/' | sort | uniq -c | sort -rn \
            | sed 's/^/    /'
    fi

    if [ "$TIDY_ERRORS" -ne 0 ]; then
        echo "wm2: static-analysis: clang-tidy reported $TIDY_ERRORS fatal diagnostic(s):" >&2
        grep ' error: ' "$WORK/tidy.filtered" | sed 's/^/  /' >&2
        fail "clang-tidy: enforced check(s) fired"
    else
        echo "  clang-tidy OK: no enforced check fired"
    fi
fi

# ===========================================================================

echo ""
if [ "$FAILURES" -ne 0 ]; then
    echo "wm2: static-analysis FAILED ($FAILURES problem(s))" >&2
    exit 1
fi

echo "static-analysis OK"
exit 0
