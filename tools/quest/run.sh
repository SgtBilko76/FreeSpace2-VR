#!/usr/bin/env bash
# Launch FreeSpace 2 on the Quest (headset kept awake) with optional extra engine args,
# then print the FSO log tail and save a headset screenshot.
#   tools/quest/run.sh [seconds] ["-pilot Bilko -start_mission $FIRST_MISSION"]
set -u
source "$(cd "$(dirname "$0")" && pwd)/game.sh"
ADB="${ADB:-$HOME/Android/Sdk/platform-tools/adb}"
WAIT="${1:-40}"
ARGS="${2:-}"
OUT="${TMPDIR:-/tmp}/fs2_quest_$(date +%H%M%S).jpg"

"$ADB" shell am broadcast -a com.oculus.vrpowermanager.prox_close >/dev/null
"$ADB" shell input keyevent KEYCODE_WAKEUP
"$ADB" shell am force-stop $PKG
"$ADB" logcat -c
if [ -n "$ARGS" ]; then
  "$ADB" shell am start -n $ACTIVITY --es fsoArgs "'$ARGS'" >/dev/null
else
  "$ADB" shell am start -n $ACTIVITY >/dev/null
fi
sleep "$WAIT"
echo "pid: $("$ADB" shell pidof $PKG)"
"$ADB" shell run-as $PKG tail -8 files/data/fs2_open.log
"$ADB" logcat -d | grep -E 'QuestVR|FS2VR|DEBUG.*Abort|F DEBUG' | tail -10
"$ADB" shell am startservice -n com.oculus.metacam/.capture.CaptureService -a TAKE_SCREENSHOT >/dev/null
sleep 4
F=$("$ADB" shell ls -t /sdcard/Oculus/Screenshots | head -1)
"$ADB" pull "/sdcard/Oculus/Screenshots/$F" "$OUT" >/dev/null && echo "screenshot: $OUT"
