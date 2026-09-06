#!/usr/bin/env bash
set -euo pipefail
#
# wm2-born-again install-component gate (D-19, CGUI-05, threats T-9-46/T-9-48).
#
# Stages each of the two CMake install components into its own directory under
# the build tree, lists what each one installed, and asserts three things:
#
#   1. The `wm` component's file list is EXACTLY the window manager, wm2-ctl,
#      the session desktop entry and the documentation. Exactly, not "contains"
#      -- "and nothing else" is half of what a packager is being promised, and a
#      containment check would never notice the GUI binary arriving in the
#      window-manager package.
#   2. The `config-gui` component's list is exactly wm2-config and its
#      application entry. In a GUI-disabled tree the component installs NOTHING
#      and must still exit 0, so the same gate runs in both kinds of tree.
#   3. THE INVARIANT: no executable the `wm` component installed reports a GTK,
#      GLib or GObject dependency. This is the claim the whole two-component
#      split exists to make -- "the window-manager package does not depend on
#      GTK" -- and it is checked here rather than asserted in the release notes.
#
#   bash scripts/gates/install-components.sh              # uses build/debug
#   bash scripts/gates/install-components.sh build/asan   # any configured tree
#
# Shell conventions follow scripts/preflight.sh and scripts/gates/build-all.sh:
# `set -euo pipefail` on line 2, every failure line prefixed `wm2: ` on stderr to
# match the binary's own convention, and failures ACCUMULATED rather than
# stopping at the first, so one run shows the whole picture.
#
# Staging goes under the build tree via mktemp -d with a template there, never a
# fixed path under the system temporary directory -- the same rule this project
# adopted for sanitizer reports, and for the same reason (T-9-48: a fixed name is
# a name another run, or another user, can already own).

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
cd "$REPO_ROOT"

BUILD_DIR="${1:-build/debug}"

usage() {
    echo "usage: install-components.sh [build-dir]"
    echo "  build-dir   a configured CMake build tree (default: build/debug)"
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
esac

# WR-17. BUILD_DIR is used two ways: handed straight to cmake, and concatenated
# after $REPO_ROOT to make the staging root. An absolute argument makes those
# two disagree -- cmake operates on /tmp/mybuild while staging goes to
# "$REPO_ROOT//tmp/mybuild/install-components", so the gate creates a tmp/
# tree inside the checkout, and the cleanup trap's guard still matches, so the
# stage is removed but the directories it made are not. The usage text
# advertises only relative paths; this is what enforces it.
case "$BUILD_DIR" in
    /*) echo "wm2: install-components: build-dir must be relative to the" \
             "repository root (got '$BUILD_DIR')" >&2
        exit 2 ;;
esac

FAILURES=()

fail() {
    echo "wm2: install-components: $1" >&2
    FAILURES+=("$1")
}

# ---------------------------------------------------------------------------
# The build tree
# ---------------------------------------------------------------------------

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "== configuring $BUILD_DIR =="
    cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug
fi

echo "== building the installable targets in $BUILD_DIR =="
# Only the shipped targets. A gate that rebuilt the whole test suite to check an
# install manifest would be one nobody runs.
cmake --build "$BUILD_DIR" --target wm2-born-again wm2-ctl --parallel

# Whether the configuration GUI exists in THIS tree is read off the tree itself
# rather than out of the cache: the binary's presence is the observable the
# install rules key on, and it is the same thing a packager would look at.
GUI_BUILT=false
if cmake --build "$BUILD_DIR" --target wm2-config --parallel >/dev/null 2>&1; then
    GUI_BUILT=true
fi
echo "  configuration GUI in this tree: $GUI_BUILT"

# ---------------------------------------------------------------------------
# Staging (T-9-48: under the build tree, unique name, removed on exit)
# ---------------------------------------------------------------------------

STAGE_ROOT="$REPO_ROOT/$BUILD_DIR/install-components"
mkdir -p "$STAGE_ROOT"
STAGE=$(mktemp -d "$STAGE_ROOT/stage-XXXXXX")

cleanup() {
    case "$STAGE" in
        "$STAGE_ROOT"/stage-*) rm -rf "$STAGE" ;;
    esac
}
trap cleanup EXIT

stage_component() {
    local component="$1"
    local dest="$STAGE/$component"
    mkdir -p "$dest"
    if ! cmake --install "$BUILD_DIR" --component "$component" --prefix "$dest" >"$STAGE/$component.log" 2>&1; then
        fail "$component: cmake --install failed"
        sed 's/^/wm2:   /' "$STAGE/$component.log" >&2
        return 1
    fi
    return 0
}

# The installed file list, relative to the staging prefix, sorted. Symlinks
# count: a symlink into the tree is still a file the package ships.
component_manifest() {
    local component="$1"
    local dest="$STAGE/$component"
    ( cd "$dest" && find . \( -type f -o -type l \) -printf '%P\n' 2>/dev/null | sort )
}

# ---------------------------------------------------------------------------
# The wm component
# ---------------------------------------------------------------------------

echo ""
echo "== component: wm =="

WM_EXPECTED=$(printf '%s\n' \
    "bin/wm2-born-again" \
    "bin/wm2-ctl" \
    "share/doc/wm2-born-again/LICENSE" \
    "share/doc/wm2-born-again/RELEASE-NOTES.md" \
    "share/xsessions/wm2-born-again.desktop" | sort)

WM_MANIFEST=""
if stage_component wm; then
    WM_MANIFEST=$(component_manifest wm)
    echo "$WM_MANIFEST" | sed 's/^/  /'

    if [ "$WM_MANIFEST" != "$WM_EXPECTED" ]; then
        fail "wm: install manifest is not the expected file list"
        diff <(echo "$WM_EXPECTED") <(echo "$WM_MANIFEST") \
            | sed 's/^/wm2:   /' >&2 || true
    fi

    # Named separately from the exact-list check above, because this is the one
    # a reader will look for: the GUI must not be in the window-manager package.
    if echo "$WM_MANIFEST" | grep -q 'wm2-config'; then
        fail "wm: the configuration GUI appears in the window-manager component"
    fi
fi

# ---------------------------------------------------------------------------
# THE INVARIANT: nothing the wm component installs links a toolkit
# ---------------------------------------------------------------------------

echo ""
echo "== wm component: toolkit linkage =="

TOOLKIT_PATTERN='libgtk|libgdk|libglib|libgobject|libgio-|libgmodule|libgthread'

if [ -n "$WM_MANIFEST" ]; then
    checked=0
    while IFS= read -r rel; do
        [ -z "$rel" ] && continue
        f="$STAGE/wm/$rel"
        [ -x "$f" ] || continue
        # A file ldd refuses (a text file, a static object) is not an executable
        # this check has anything to say about.
        ldd_out=$(ldd "$f" 2>/dev/null) || continue
        checked=$((checked + 1))
        hits=$(echo "$ldd_out" | grep -E "$TOOLKIT_PATTERN" || true)
        if [ -n "$hits" ]; then
            fail "wm: $rel links a toolkit library, which the window-manager package must never depend on"
            echo "$hits" | sed 's/^/wm2:   /' >&2
        else
            echo "  $rel: no GTK/GLib/GObject linkage"
        fi
    done <<< "$WM_MANIFEST"

    # An invariant checked over nothing is an invariant nobody checked -- the
    # same failure --no-tests=error exists to prevent, one level down.
    if [ "$checked" -eq 0 ]; then
        fail "wm: the toolkit check examined ZERO executables"
    fi
fi

# ---------------------------------------------------------------------------
# The config-gui component
# ---------------------------------------------------------------------------

echo ""
echo "== component: config-gui =="

CONFIG_GUI_EXPECTED=""
if [ "$GUI_BUILT" = true ]; then
    CONFIG_GUI_EXPECTED=$(printf '%s\n' \
        "bin/wm2-config" \
        "share/applications/wm2-config.desktop" | sort)
fi

if stage_component config-gui; then
    CONFIG_GUI_MANIFEST=$(component_manifest config-gui)
    if [ -z "$CONFIG_GUI_MANIFEST" ]; then
        echo "  (nothing -- this tree was configured without the configuration GUI)"
    else
        echo "$CONFIG_GUI_MANIFEST" | sed 's/^/  /'
    fi

    if [ "$CONFIG_GUI_MANIFEST" != "$CONFIG_GUI_EXPECTED" ]; then
        if [ "$GUI_BUILT" = true ]; then
            fail "config-gui: install manifest is not the expected file list"
        else
            fail "config-gui: a GUI-disabled tree installed files for this component"
        fi
        diff <(echo "$CONFIG_GUI_EXPECTED") <(echo "$CONFIG_GUI_MANIFEST") \
            | sed 's/^/wm2:   /' >&2 || true
    fi
fi

# ---------------------------------------------------------------------------
# Result
# ---------------------------------------------------------------------------

echo ""
if [ ${#FAILURES[@]} -ne 0 ]; then
    echo "wm2: install-components FAILED with ${#FAILURES[@]} problem(s):" >&2
    for f in "${FAILURES[@]}"; do
        echo "wm2:   - $f" >&2
    done
    exit 1
fi

echo "install-components OK: wm and config-gui staged separately from $BUILD_DIR"
exit 0
