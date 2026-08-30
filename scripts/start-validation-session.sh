#!/usr/bin/env bash
set -euo pipefail
#
# wm2-born-again remote-desktop validation session (TEST-08, XDIS-05).
#
#   bash scripts/start-validation-session.sh [display]
#
# Stands up a TigerVNC session running ONLY this window manager, with an
# isolated rule configuration, ready for a human to walk the User Interaction
# Checklist against. Prints the viewer command and stops.
#
# This exists because the last two manual passes produced findings and a general
# verdict rather than a filled-in per-item table, and one reason was that setting
# the session up correctly is fiddly enough to discourage doing it twice. The
# deviations in COMPILED_CODE_BEHAVIOR_CHECKLIST.md name "whoever next stands up
# a remote-desktop validation session" as their owner; this is that job, scripted.
#
# THREE DELIBERATE DESIGN POINTS:
#
#   1. NO NETWORK-EXPOSING FLAGS. `-localhost yes` is passed explicitly rather
#      than relied on as a default (threat T-8-AC). `-localhost no` must never
#      appear here or be added later: it would put the port on the network, and
#      this file is exactly the kind of thing that gets copied into a droplet
#      deployment recipe.
#
#   2. THE CONFIGURATION IS ISOLATED, NOT YOURS. XDG_CONFIG_HOME points at a
#      throwaway tree under /tmp holding the window rules the checklist asks you
#      to exercise. Your real ~/.config/wm2-born-again is neither read nor
#      written, so nothing here can leave your own settings changed.
#
#   3. ONLY THE WINDOW MANAGER RUNS. The stock TigerVNC xstartup launches a full
#      desktop session, whose own window manager would fight this one for the
#      root window. The generated xstartup runs wm2-born-again and nothing else.

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DISPLAY_NUM="${1:-:11}"
GEOMETRY="1280x1024"
DEPTH="24"

WM="$REPO_ROOT/build/release/wm2-born-again"

if [ ! -x "$WM" ]; then
    echo "wm2: $WM is missing or not executable" >&2
    echo "wm2: build it first --  cmake --build build/release --parallel" >&2
    exit 1
fi

if ! command -v vncserver >/dev/null 2>&1; then
    echo "wm2: no vncserver on PATH" >&2
    exit 1
fi

# --- the isolated configuration -------------------------------------------
#
# The rules below are the ones rows 29-31 of the interaction checklist ask you
# to check. rule-match-title is new in plan 08.5-01 and is the reason RULES-01
# could be closed; the checklist wants it exercised by a person as well as by
# the process-level tests.
CFG_BASE="$(mktemp -d /tmp/wm2-validation-XXXXXX)"
mkdir -p "$CFG_BASE/wm2-born-again"
cat > "$CFG_BASE/wm2-born-again/config" <<'CFG'
# Isolated validation configuration -- NOT your real one.
# Rows 29-31 of the interaction checklist exercise these.

# A window whose TITLE contains "clock" opens at 300,200.
rule-match-title = clock
rule-position    = 300,200

# A window whose WM_CLASS INSTANCE name contains "xeyes" opens undecorated.
rule-match-instance = xeyes
rule-no-decorate    = true
CFG

# --- the session's xstartup ------------------------------------------------
XSTARTUP="$CFG_BASE/xstartup"
cat > "$XSTARTUP" <<XS
#!/bin/sh
# Only the window manager. No desktop environment, no second WM.
export XDG_CONFIG_HOME="$CFG_BASE"
exec "$WM" --new-window-command=xclock
XS
chmod +x "$XSTARTUP"

echo "wm2: starting a validation session on $DISPLAY_NUM"
echo "wm2:   commit    $(cd "$REPO_ROOT" && git rev-parse --short HEAD)"
echo "wm2:   binary    $WM"
echo "wm2:   config    $CFG_BASE/wm2-born-again/config  (isolated)"
echo

vncserver "$DISPLAY_NUM" \
    -geometry "$GEOMETRY" \
    -depth "$DEPTH" \
    -localhost yes \
    -xstartup "$XSTARTUP"

cat <<EOF

wm2: session is up on $DISPLAY_NUM.

Connect with:

    xtigervncviewer $DISPLAY_NUM

Then walk the table in:

    .planning/phases/08.5-v1.0-closeout/evidence/INTERACTION-CHECKLIST.md

The root menu is BUTTON 1 (left). Button 3 (right) is circulate.

When you are done:

    vncserver -kill $DISPLAY_NUM
    rm -rf $CFG_BASE

If something misbehaves and you want help diagnosing it, paste the window
manager's own stderr:

    cat ~/.vnc/\$(hostname)$DISPLAY_NUM.log

WARNING: do NOT paste ~/.xsession-errors. On many setups that file captures the
session's whole environment in plaintext, so it can carry API keys, tokens and
other credentials. Never paste it into an issue, a chat or a pull request, and
never copy any part of it into this repository.
EOF
