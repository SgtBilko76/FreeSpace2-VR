#!/usr/bin/env bash
# Headless smoke test: start Wing Commander Saga mission 1 on the Quest, click through
# fiction viewer + briefing with adb key events, fly for a while, report FPS + screenshot.
#   tools/quest/mission_test.sh [seconds_in_mission]
set -u
ADB="${ADB:-$HOME/Android/Sdk/platform-tools/adb}"
FLY="${1:-60}"
DIR="$(cd "$(dirname "$0")" && pwd)"

"$DIR/run.sh" 50 "-pilot Bilko -start_mission M01-BG-Hermes.fs2" >/dev/null
"$ADB" shell input keyevent KEYCODE_ENTER                              # new user tip
sleep 20
"$ADB" shell input keycombination KEYCODE_CTRL_LEFT KEYCODE_ENTER      # fiction viewer
sleep 8
"$ADB" shell input keycombination KEYCODE_CTRL_LEFT KEYCODE_ENTER      # briefing -> mission
sleep "$FLY"
PID=$("$ADB" shell pidof com.bilko.wcsagavr)
echo "pid: $PID"
"$ADB" logcat -d | grep "$PID .*VrApi   : FPS" | tail -5 | grep -o -E 'FPS=[^,]*|App=[^,]*|GPU%=[^,]*|CPU4/GPU=[^,]*' | paste - - - -
"$ADB" logcat -d | grep -E 'WCSagaVR' | tail -5
"$ADB" shell run-as com.bilko.wcsagavr grep 'frames executed' files/data/fs2_open.log | tail -3
"$ADB" shell am startservice -n com.oculus.metacam/.capture.CaptureService -a TAKE_SCREENSHOT >/dev/null
sleep 4
F=$("$ADB" shell ls -t /sdcard/Oculus/Screenshots | head -1)
OUT="${TMPDIR:-/tmp}/wcs_mission_$(date +%H%M%S).jpg"
"$ADB" pull "/sdcard/Oculus/Screenshots/$F" "$OUT" >/dev/null && echo "screenshot: $OUT"
