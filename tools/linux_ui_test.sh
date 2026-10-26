#!/bin/sh
# Desktop UI smoke test under Xvfb: more menu, right-click menu, site info. Samples Lumen's CPU/RSS every second.
# Usage: xvfb-run -s "-screen 0 1280x800x24" tools/linux_ui_test.sh [URL]
R=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-/tmp/lumen-ui}
mkdir -p "$OUT" /tmp/lh
export HOME=/tmp/lh XDG_CONFIG_HOME=/tmp/lh/.config XDG_DATA_HOME=/tmp/lh/.local SDL_AUDIO_DRIVER=dummy
"$R/build/codex-native/lumen" "${1:-file://$R/tests/device/editbar/index.html}" >"$OUT/lumen.log" 2>&1 &
P=$!
(while kill -0 $P 2>/dev/null; do ps -o %cpu=,rss= -p $P; sleep 1; done) >"$OUT/cpu.txt" &
sleep 6
W=$(xdotool search --pid $P | tail -1)
eval "$(xdotool getwindowgeometry --shell "$W")"
shot() { import -window root "$OUT/$1.png"; }
shot 0-start
xdotool mousemove --window "$W" $((WIDTH-22)) 22 click 1; sleep 1.5; shot 1-more
xdotool key Escape; sleep 1
xdotool mousemove --window "$W" $((WIDTH/2)) $((HEIGHT/2)) click 3; sleep 1.5; shot 2-context
xdotool key Escape; sleep 1
xdotool mousemove --window "$W" $((WIDTH-22)) 22 click 1; sleep 1; xdotool mousemove --window "$W" $((WIDTH-150)) 400 click 1; sleep 1.5; shot 3-after-click
xdotool key Escape; sleep 1
xdotool mousemove --window "$W" 160 22 click 1; sleep 1.5; shot 4-site
xdotool key Escape; sleep 1
xdotool mousemove --window "$W" 42 52 click 3; sleep 1.5; shot 5-workspace
xdotool key Escape; sleep 1
xdotool mousemove --window "$W" 300 80 click 3; sleep 1.5; shot 6-field
xdotool key Escape; sleep 1
xdotool mousemove --window "$W" 900 600 click 3; sleep 1; xdotool mousemove --window "$W" 960 700 click 1; sleep 0.7; shot 7-toast
xdotool mousemove --window "$W" 120 226 click 3; sleep 1.5; shot 8-link
xdotool key Escape; sleep 8; shot 9-idle
kill $P; wait $P 2>/dev/null
echo "cpu/rss samples:"; cat "$OUT/cpu.txt" | tr '\n' ';'; echo
grep -ci "error\|segv\|abort" "$OUT/lumen.log"
