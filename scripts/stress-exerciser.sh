#!/usr/bin/env bash
set -uo pipefail
#
# wm2-born-again heavy-use exerciser.
#
#   bash scripts/stress-exerciser.sh [cycles] [windows-per-cycle] [display]
#
# Churns windows through the window manager and samples its resident memory
# after every cycle, to answer a question the resource-budget test cannot: does
# sustained USE grow the process?
#
# WHY THIS EXISTS ALONGSIDE [wm_resource_budget]. That test maps twenty clients,
# measures once, and asserts a ceiling -- a STEADY-STATE measurement. It would
# pass unchanged against a manager that leaked a kilobyte per window, because it
# never returns to the same window count twice. A leak is only visible as a trend
# across cycles that begin and end in the same state, which is what this does.
#
# THE SIGNAL. Each cycle creates N clients, exercises them, and destroys all of
# them. At the end of every cycle the client count is back to zero, so a healthy
# process returns to roughly the same RSS. Growth that does not level off is the
# leak signal; growth that plateaus after the first cycle or two is just the
# allocator's arenas warming up and is expected.
#
# Run it against the ASan tree as well -- `build/asan/wm2-born-again` -- to get
# LeakSanitizer's verdict on top of the RSS trend. RSS says whether memory is
# growing; LSan says which allocation is responsible.

CYCLES="${1:-12}"
WINDOWS="${2:-15}"
DISPLAY_NUM="${3:-:97}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WM="${WM_BINARY:-$REPO_ROOT/build/release/wm2-born-again}"

if [ ! -x "$WM" ]; then
    echo "wm2: $WM missing; build it first" >&2
    exit 1
fi
for t in Xvfb xclock xeyes xdotool; do
    command -v "$t" >/dev/null || { echo "wm2: need $t" >&2; exit 1; }
done

cleanup() {
    [ -n "${CLIENT_PIDS:-}" ] && kill $CLIENT_PIDS 2>/dev/null
    [ -n "${WM_PID:-}"   ] && kill "$WM_PID"   2>/dev/null
    [ -n "${XVFB_PID:-}" ] && kill "$XVFB_PID" 2>/dev/null
    wait 2>/dev/null
}
trap cleanup EXIT

echo "wm2: stress exerciser"
echo "wm2:   binary   $WM"
echo "wm2:   cycles   $CYCLES x $WINDOWS windows"
echo "wm2:   display  $DISPLAY_NUM"
echo

Xvfb "$DISPLAY_NUM" -screen 0 1280x1024x24 -noreset -nolisten tcp >/dev/null 2>&1 &
XVFB_PID=$!
sleep 2
DISPLAY="$DISPLAY_NUM" xdpyinfo >/dev/null 2>&1 || { echo "wm2: Xvfb failed" >&2; exit 1; }

DISPLAY="$DISPLAY_NUM" "$WM" > /tmp/wm2-stress-wm.log 2>&1 &
WM_PID=$!
sleep 2
kill -0 "$WM_PID" 2>/dev/null || { echo "wm2: WM died at startup" >&2; cat /tmp/wm2-stress-wm.log; exit 1; }

rss_kb() { awk '/^VmRSS:/ {print $2}' "/proc/$WM_PID/status" 2>/dev/null || echo 0; }
fds()    { ls "/proc/$WM_PID/fd" 2>/dev/null | wc -l; }

BASE_RSS=$(rss_kb)
BASE_FDS=$(fds)
echo "cycle   clients   RSS(kB)   delta   fds   X-errors"
echo "-----   -------   -------   -----   ---   --------"
printf "%5s   %7s   %7s   %5s   %3s   %8s\n" "0" "0" "$BASE_RSS" "-" "$BASE_FDS" "0"

PREV=$BASE_RSS
for c in $(seq 1 "$CYCLES"); do
    CLIENT_PIDS=""
    # --- create ---------------------------------------------------------
    for i in $(seq 1 "$WINDOWS"); do
        x=$(( (i * 61) % 900 ))
        y=$(( (i * 37) % 700 ))
        if [ $(( i % 2 )) -eq 0 ]; then
            DISPLAY="$DISPLAY_NUM" xclock -geometry 120x120+$x+$y -title "stress-$c-$i" >/dev/null 2>&1 &
        else
            DISPLAY="$DISPLAY_NUM" xeyes -geometry 120x120+$x+$y >/dev/null 2>&1 &
        fi
        CLIENT_PIDS="$CLIENT_PIDS $!"
    done
    sleep 2

    # --- exercise: move, resize, retitle, hide/unhide via EWMH ----------
    for w in $(DISPLAY="$DISPLAY_NUM" xdotool search --name "stress-$c" 2>/dev/null | head -8); do
        DISPLAY="$DISPLAY_NUM" xdotool windowmove "$w" $((RANDOM % 800)) $((RANDOM % 600)) 2>/dev/null
        DISPLAY="$DISPLAY_NUM" xdotool windowsize "$w" $((150 + RANDOM % 300)) $((150 + RANDOM % 300)) 2>/dev/null
        DISPLAY="$DISPLAY_NUM" xdotool set_window --name "renamed-$c-$RANDOM" "$w" 2>/dev/null
    done
    sleep 1

    # --- destroy --------------------------------------------------------
    kill $CLIENT_PIDS 2>/dev/null
    wait $CLIENT_PIDS 2>/dev/null
    CLIENT_PIDS=""
    sleep 2

    if ! kill -0 "$WM_PID" 2>/dev/null; then
        echo
        echo "wm2: *** THE WINDOW MANAGER DIED IN CYCLE $c ***" >&2
        tail -20 /tmp/wm2-stress-wm.log >&2
        exit 1
    fi

    NOW=$(rss_kb)
    # `grep -c` prints its count AND exits non-zero when the count is zero, so a
    # naive `|| echo 0` emits the count and a second 0 on its own line.
    ERRS=$(grep -c "X protocol error\|BadWindow\|BadMatch\|BadValue\|BadDrawable" /tmp/wm2-stress-wm.log 2>/dev/null) || true
    ERRS="${ERRS:-0}"
    printf "%5s   %7s   %7s   %+5s   %3s   %8s\n" "$c" "0" "$NOW" "$((NOW - PREV))" "$(fds)" "$ERRS"
    PREV=$NOW
done

FINAL=$(rss_kb)
GROWTH=$((FINAL - BASE_RSS))
TOTAL=$((CYCLES * WINDOWS))

echo
echo "wm2: ---------------------------------------------------------------"
echo "wm2: windows created and destroyed : $TOTAL"
echo "wm2: RSS at start                  : $BASE_RSS kB"
echo "wm2: RSS at end                    : $FINAL kB"
echo "wm2: total growth                  : $GROWTH kB"
if [ "$TOTAL" -gt 0 ]; then
    echo "wm2: growth per window             : $(awk "BEGIN{printf \"%.3f\", $GROWTH/$TOTAL}") kB"
fi
echo "wm2: file descriptors              : $BASE_FDS -> $(fds)"
echo "wm2: WM still alive                : $(kill -0 "$WM_PID" 2>/dev/null && echo yes || echo NO)"
echo "wm2: ---------------------------------------------------------------"
echo
echo "wm2: READ THE TREND, NOT THE TOTAL. Growth concentrated in the first cycle"
echo "wm2: or two is allocator warm-up. Growth that continues at a steady rate"
echo "wm2: per cycle is the leak signal -- rerun with WM_BINARY pointed at"
echo "wm2: build/asan/wm2-born-again to find out which allocation."
