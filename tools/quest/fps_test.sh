#!/usr/bin/env bash
# Start the first mission on the Quest, let it fly for a while and print the VrApi
# frame rate / GPU stats (logged once per second by the Meta runtime).
#   [FSO_EXTRA="-shadow_quality 2"] tools/quest/fps_test.sh [seconds of flight]
set -u
source "$(cd "$(dirname "$0")" && pwd)/game.sh"
ADB="${ADB:-$HOME/Android/Sdk/platform-tools/adb}"
FLY="${1:-30}"
DIR="$(cd "$(dirname "$0")" && pwd)"

"$DIR/run.sh" 50 "-pilot Bilko -start_mission $FIRST_MISSION ${FSO_EXTRA:-}" >/dev/null
"$ADB" shell input keyevent KEYCODE_ENTER
sleep 25
for i in 1 2 3 4; do "$ADB" shell input keycombination KEYCODE_CTRL_LEFT KEYCODE_ENTER; sleep 12; done
sleep 15
"$ADB" logcat -c
sleep "$FLY"
"$ADB" logcat -d -s VrApi | grep -o 'FPS=[0-9]*/[0-9]*.*GPU%=[0-9.]*' | tail -n "$FLY" |
  awk '{print} match($0,/FPS=[0-9]+/){s+=substr($0,RSTART+4,RLENGTH-4);n++} END{if(n) printf "average FPS %.1f over %d s\n", s/n, n}'
