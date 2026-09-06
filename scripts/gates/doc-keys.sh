#!/usr/bin/env bash
set -euo pipefail
#
# wm2-born-again key-to-documentation parity gate (plan 09-09, threat T-9-55).
#
# Asserts, in BOTH directions, that the set of configuration keys the binary
# accepts and the set of keys docs/RELEASE-NOTES.md presents to a user are the
# same set:
#
#   1. A key the binary accepts that the release notes never mention fails the
#      gate, naming the key. A setting nobody can find is a setting that does
#      not exist.
#   2. A key-shaped string the release notes present as a setting that the
#      binary does NOT accept fails the gate, naming it. This is the worse of
#      the two: it sends a user to type something that will be refused, or --
#      worse still in a config file -- silently warned about and ignored.
#
# WHY THIS IS A SCRIPT. Until this plan the check existed as a pair of ad-hoc
# `comm -3 <(grep ...) <(grep ...)` lines inside one plan's verify block, run by
# hand, covering the `rule-*` keys only. That caught a real drift once and then
# went away with the plan that carried it. A gate the next phase inherits is a
# command; a habit is not.
#
#   bash scripts/gates/doc-keys.sh              # the whole check
#   bash scripts/gates/doc-keys.sh --list       # print both key sets and stop
#
# Shell conventions follow scripts/preflight.sh, scripts/gates/build-all.sh and
# scripts/gates/install-components.sh: `set -euo pipefail` on line 2, every
# failure line prefixed `wm2: ` on stderr to match the binary's own convention,
# and failures ACCUMULATED rather than stopping at the first, so one run shows
# the whole picture rather than the first third of it.

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
cd "$REPO_ROOT"

CONFIG_SRC="src/Config.cpp"
CONFIG_HDR="include/Config.h"
NOTES="docs/RELEASE-NOTES.md"

usage() {
    echo "usage: doc-keys.sh [--list]"
    echo "  doc-keys.sh checks, in both directions, that every configuration key"
    echo "  $CONFIG_SRC accepts is documented in $NOTES and that every"
    echo "  key-shaped string $NOTES presents as a setting is a key the binary"
    echo "  accepts."
    echo "  --list      print the two key sets and exit without checking"
}

LIST_ONLY=false
case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --list)    LIST_ONLY=true ;;
    "")        ;;
    *)         usage >&2; exit 3 ;;
esac

FAILURES=()

fail() {
    echo "wm2: doc-keys: $1" >&2
    FAILURES+=("$1")
}

for f in "$CONFIG_SRC" "$CONFIG_HDR" "$NOTES"; do
    if [ ! -f "$f" ]; then
        echo "wm2: doc-keys: $f is missing -- run this from a checkout" >&2
        exit 1
    fi
done

# ---------------------------------------------------------------------------
# ALLOW LISTS
#
# Every entry carries a one-line reason. An unexplained exclusion is how a
# guard stops guarding: the next person to hit the gate adds a name to a bare
# list and the check quietly shrinks. Neither list may grow without a reason
# beside the name.
# ---------------------------------------------------------------------------

# Keys the binary accepts that the release notes deliberately do NOT present as
# settings.
ACCEPTED_NOT_DOCUMENTED=(
    # `help` is an ACTION on the command line, not a setting: it prints the
    # usage text and exits, and it can never appear in a config file. The
    # option table lists it because the table is what generates the usage text.
    "help"
)

# Key-shaped strings in the release notes that are NOT settings. Each is a real
# thing a reader needs to see spelled exactly, which is why it is in backticks
# in the first place.
DOCUMENTED_NOT_KEYS=(
    # the three programs this repository builds and installs
    "wm2-born-again"
    "wm2-ctl"
    "wm2-config"
    # a CMake install component name (D-19), which a packager types
    "config-gui"
    # the Debian/Ubuntu package a reader installs to get the settings window
    "libgtk-3-dev"
    # the RETIRED pre-v1.0 spelling of rule-match-instance. The notes name it
    # on purpose, in a migration note telling a user to rename it -- so it must
    # appear here and must NOT be accepted by the binary. If this ever becomes
    # an accepted key again, delete this line and the migration note together.
    "rule-match-name"
)

contains() {
    local needle="$1"; shift
    local item
    for item in "$@"; do
        [ "$item" = "$needle" ] && return 0
    done
    return 1
}

# ---------------------------------------------------------------------------
# What the binary accepts
#
# Three sources, because the key vocabulary genuinely lives in three places and
# a check that read only one would pass while the other two drifted:
#
#   the option table   src/Config.cpp kOptionSpecs -- the settings that have a
#                      command-line flag and generate the usage text
#   the parser chain   src/Config.cpp applyKeyValue()'s `key == "..."` arms --
#                      which includes the config-file-only families,
#                      menu-entry-* and rule-*, that have no flag at all
#   the protocol keys  include/Config.h's k*Key constants -- keys the socket's
#                      `get` verb answers that are not settings in the file,
#                      menu-entries and the read-only menu-categories
# ---------------------------------------------------------------------------

option_table_keys() {
    grep -oE '^[[:space:]]*\{"[a-z][a-z0-9-]*"' "$CONFIG_SRC" \
        | grep -oE '"[a-z][a-z0-9-]*"' | tr -d '"'
}

parser_chain_keys() {
    grep -oE 'key == "[a-z][a-z0-9-]*"' "$CONFIG_SRC" \
        | grep -oE '"[a-z][a-z0-9-]*"' | tr -d '"'
}

protocol_keys() {
    grep -oE 'inline constexpr const char\* k[A-Za-z]+Key[[:space:]]*=[[:space:]]*"[a-z][a-z0-9-]*"' "$CONFIG_HDR" \
        | grep -oE '"[a-z][a-z0-9-]*"' | tr -d '"'
}

ACCEPTED=$( { option_table_keys; parser_chain_keys; protocol_keys; } | sort -u )

if [ -z "$ACCEPTED" ]; then
    fail "extracted NO keys from $CONFIG_SRC -- the option table or the parser chain changed shape and this gate is now vacuous"
fi

# ---------------------------------------------------------------------------
# What the notes document
#
# A key is documented when the notes present it AS a key, which is one of two
# forms and deliberately not a third:
#
#   `like-this`        a backticked token -- how the prose and the tables name
#                      every setting
#   like-this = value  a line inside a fenced config example
#
# COMMENT LINES DO NOT COUNT. An HTML comment is invisible to a reader, so a
# key mentioned only inside one is a key nobody can find, and this gate must
# say so rather than be satisfied by it.
#
# A leading `no-` is stripped when what remains is an accepted key: `--no-auto-
# raise` is the command-line spelling of the `auto-raise` boolean, not a second
# setting, and the notes are right to show it.
#
# WHAT COUNTS AS KEY-SHAPED, and the one gap it leaves. A hyphenated lowercase
# token is key-shaped on sight: no ordinary English word in these notes looks
# like `menu-entry-category`. A token with NO hyphen does not have that
# property -- `true`, `normal`, `dialog`, `fixed` and `exact` are all backticked
# in this document and none of them is a setting -- so a bare word is admitted
# only when something ELSE marks it as a setting: a `--word` command-line
# spelling, a `word = value` configuration line, or its presence in the accepted
# set.
#
# The gap that leaves, stated rather than hidden: a bare word presented in plain
# backticks that is NOT an accepted key slips past direction 2. In prose that
# form is indistinguishable from an ordinary word, so the alternative is a gate
# that fails on `true`, `normal` and `exact`. Every hyphenated invention is
# caught, and so is every bare one written as a `word = value` configuration
# line -- which is the form a reader copies. `borders` is the only single-word
# key the binary has, so the uncovered set is at most "a bare word that is not a
# key, in backticks, never shown in a config line".
# ---------------------------------------------------------------------------

uncommented_notes() {
    grep -v '^[[:space:]]*<!--' "$NOTES"
}

# Every backticked token, undecorated. The broad pool; the shape rules below
# decide which of these is presented as a setting.
backticked_tokens() {
    uncommented_notes \
        | grep -oE '`[^`]+`' | tr -d '`' | tr ' ' '\n' \
        | sed 's/^--//; s/=$//; s/[.,;:]$//'
}

# The left-hand side of a `key = value` line in a fenced configuration example.
# An INDEPENDENT mark of being a setting -- that is the config file's own
# grammar and nothing else in these notes is written in it -- so such a token is
# admitted whether or not it is hyphenated.
#
# Deliberately NOT harvested: `--flag` spellings. These notes document other
# programs' command lines too (`cmake --install ... --component ... --prefix`),
# and a rule that read every double-dash token would report cmake's flags as
# settings this window manager fails to accept. Measured, not supposed: adding
# that channel produced `build`, `component`, `install`, `parallel` and `prefix`
# as false failures on this document.
marked_as_setting() {
    uncommented_notes | grep -oE '^[a-z][a-z0-9-]*[[:space:]]*=' \
        | sed 's/[[:space:]]*=$//'
}

documented_candidates() {
    # Hyphenated tokens are key-shaped on sight.
    backticked_tokens | grep -E '^[a-z][a-z0-9]*(-[a-z0-9]+)+$'
    # Bare words only when the accepted set already names them; see the note
    # above for the gap this leaves and why it is the lesser evil.
    backticked_tokens | grep -E '^[a-z][a-z0-9]*$' | while IFS= read -r w; do
        grep -qx "$w" <<< "$ACCEPTED" && echo "$w"
    done
    marked_as_setting
}

DOCUMENTED_RAW=$(documented_candidates | grep -E '^[a-z][a-z0-9-]*$' | sort -u || true)

# Fold each `no-` negation onto the boolean it negates, and drop the names that
# are not settings at all.
DOCUMENTED=""
while IFS= read -r token; do
    [ -z "$token" ] && continue
    if contains "$token" "${DOCUMENTED_NOT_KEYS[@]}"; then
        continue
    fi
    if ! grep -qx "$token" <<< "$ACCEPTED"; then
        case "$token" in
            no-*)
                base="${token#no-}"
                if grep -qx "$base" <<< "$ACCEPTED"; then
                    token="$base"
                fi
                ;;
        esac
    fi
    DOCUMENTED+="$token"$'\n'
done <<< "$DOCUMENTED_RAW"
DOCUMENTED=$(printf '%s' "$DOCUMENTED" | sort -u)

if [ "$LIST_ONLY" = true ]; then
    echo "== keys the binary accepts =="
    echo "$ACCEPTED" | sed 's/^/  /'
    echo ""
    echo "== keys $NOTES presents =="
    echo "$DOCUMENTED" | sed 's/^/  /'
    exit 0
fi

# ---------------------------------------------------------------------------
# Direction 1: accepted but undocumented
# ---------------------------------------------------------------------------

echo "== keys the binary accepts that $NOTES does not document =="
UNDOCUMENTED=()
while IFS= read -r key; do
    [ -z "$key" ] && continue
    contains "$key" "${ACCEPTED_NOT_DOCUMENTED[@]}" && continue
    grep -qx "$key" <<< "$DOCUMENTED" && continue
    UNDOCUMENTED+=("$key")
done <<< "$ACCEPTED"

if [ ${#UNDOCUMENTED[@]} -eq 0 ]; then
    echo "  (none)"
else
    for key in "${UNDOCUMENTED[@]}"; do
        fail "the binary accepts '$key' and $NOTES never presents it as a setting"
    done
fi

# ---------------------------------------------------------------------------
# Direction 2: documented but not accepted
# ---------------------------------------------------------------------------

echo ""
echo "== keys $NOTES presents that the binary does not accept =="
UNACCEPTED=()
while IFS= read -r key; do
    [ -z "$key" ] && continue
    grep -qx "$key" <<< "$ACCEPTED" && continue
    UNACCEPTED+=("$key")
done <<< "$DOCUMENTED"

if [ ${#UNACCEPTED[@]} -eq 0 ]; then
    echo "  (none)"
else
    for key in "${UNACCEPTED[@]}"; do
        fail "$NOTES presents '$key' as a setting and the binary does not accept it"
    done
fi

# ---------------------------------------------------------------------------
# Anti-vacuity
#
# The rule-group coverage the previous phase checked by hand is subsumed here;
# this asserts the subsumption rather than trusting it. A regex change that
# stopped matching the rule keys would otherwise turn the gate green by
# comparing two empty sets.
# ---------------------------------------------------------------------------

RULE_KEYS=$(grep -cE '^rule-' <<< "$ACCEPTED" || true)
if [ "$RULE_KEYS" -lt 9 ]; then
    fail "only $RULE_KEYS rule-* keys were extracted from $CONFIG_SRC; the previous phase's hand check covered 9, so this gate is now weaker than what it replaced"
fi

# ---------------------------------------------------------------------------
# Result
# ---------------------------------------------------------------------------

echo ""
if [ ${#FAILURES[@]} -ne 0 ]; then
    echo "wm2: doc-keys FAILED with ${#FAILURES[@]} problem(s):" >&2
    for f in "${FAILURES[@]}"; do
        echo "wm2:   - $f" >&2
    done
    exit 1
fi

ACCEPTED_COUNT=$(grep -c . <<< "$ACCEPTED")
echo "doc-keys OK: $ACCEPTED_COUNT accepted keys, every one documented, and $NOTES presents no key the binary refuses"
exit 0
