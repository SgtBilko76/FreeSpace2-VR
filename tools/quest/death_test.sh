#!/usr/bin/env bash
# Death-menu test: start the test mission (player self-destructs after 20 s; copy
# qvr-deathtest.fs2 to the Quest's missions folder first), click through the briefing,
# wait for the death menu and save a headset screenshot.
#   tools/quest/death_test.sh [screenshot name]
set -u
source "$(cd "$(dirname "$0")" && pwd)/game.sh"
ADB="${ADB:-$HOME/Android/Sdk/platform-tools/adb}"
DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="${TMPDIR:-/tmp}/${1:-death}.jpg"
LOG() { "$ADB" shell run-as $PKG cat files/data/fs2_open.log 2>/dev/null; }

"$DIR/run.sh" 50 "-pilot Bilko -start_mission qvr-deathtest.fs2" >/dev/null
"$ADB" shell input keyevent KEYCODE_ENTER
sleep 20
for i in $(seq 1 12); do
  LOG | grep -q 'GS_EVENT_ENTER_GAME' && break
  "$ADB" shell input keycombination KEYCODE_CTRL_LEFT KEYCODE_ENTER
  sleep 8
done
for i in $(seq 1 20); do
  LOG | grep -q 'GS_EVENT_DEATH_BLEW_UP' && break
  sleep 3
done
sleep 8
"$ADB" shell am startservice -n com.oculus.metacam/.capture.CaptureService -a TAKE_SCREENSHOT >/dev/null
sleep 4
F=$("$ADB" shell ls -t /sdcard/Oculus/Screenshots | head -1)
"$ADB" pull "/sdcard/Oculus/Screenshots/$F" "$OUT" >/dev/null && echo "screenshot: $OUT"
LOG | grep -E 'GS_EVENT_DEATH_BLEW_UP|QDBG' | tail -3
