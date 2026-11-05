#!/usr/bin/env bash
# Show the radial ring (left grip) in flight via the debug hook and screenshot it.
#   tools/quest/radial_shot.sh [item]
set -u
source "$(cd "$(dirname "$0")" && pwd)/game.sh"
ADB="${ADB:-$HOME/Android/Sdk/platform-tools/adb}"
DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="${TMPDIR:-/tmp}"
shot() {
  "$ADB" shell am startservice -n com.oculus.metacam/.capture.CaptureService -a TAKE_SCREENSHOT >/dev/null
  sleep 4
  local f
  f=$("$ADB" shell ls -t /sdcard/Oculus/Screenshots | head -1)
  "$ADB" pull "/sdcard/Oculus/Screenshots/$f" "$OUT/$1.jpg" >/dev/null && echo "screenshot: $OUT/$1.jpg"
}
"$ADB" shell setprop debug.questvr.radial "${1:-5}"
"$DIR/mission_test.sh" 15 >/dev/null 2>&1
shot ring_left
"$ADB" shell setprop debug.questvr.radial "''"
"$ADB" logcat -d | grep "VrApi   : FPS" | tail -1 | grep -o "FPS=[^,]*"
