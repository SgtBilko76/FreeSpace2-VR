#!/usr/bin/env bash
# Fly the test mission at several HUD text scales and save a crop of the orders/messages area.
#   tools/quest/hudtext_test.sh 1.0 1.4 1.7
set -u
ADB="${ADB:-$HOME/Android/Sdk/platform-tools/adb}"
DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="${TMPDIR:-/tmp}"
for s in "$@"; do
  "$ADB" shell setprop debug.questvr.hudtext "$s"
  f=$("$DIR/mission_test.sh" 15 2>/dev/null | grep -o "screenshot: .*" | cut -d' ' -f2)
  [ -n "$f" ] && convert "$f" -crop 1000x700+150+200 +repage "$OUT/hudtext_$s.jpg" && echo "$s -> $OUT/hudtext_$s.jpg"
done
"$ADB" shell setprop debug.questvr.hudtext "''"
