#!/usr/bin/env bash
# Build the FSO Android engine, stage its libraries and package the Quest APK.
#   tools/quest/build_apk.sh [--install] [--run]
set -euo pipefail
source "$(cd "$(dirname "$0")" && pwd)/game.sh"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NDK="${ANDROID_NDK_HOME:-$HOME/Android/Sdk/ndk/29.0.14206865}"
ADB="${ADB:-$HOME/Android/Sdk/platform-tools/adb}"
NINJA="${NINJA:-$HOME/.local/bin/ninja}"
HOST="$ROOT/build/linux"
B="$ROOT/build/android-arm64"
OUT="$ROOT/build/quest"

# 1) host embedfile tool
if [ ! -x "$HOST/bin/embedfile" ]; then
  mkdir -p "$HOST"
  cmake -S "$ROOT" -B "$HOST" -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" -DCMAKE_BUILD_TYPE=Release \
    -DFSO_BUILD_QTFRED=OFF -DFSO_BUILD_TOOLS=ON -DFSO_BUILD_WITH_VULKAN=OFF -DFSO_BUILD_WITH_OPENXR=OFF
  "$NINJA" -C "$HOST" embedfile
fi

# 2) engine for arm64
if [ ! -f "$B/build.ninja" ]; then
  cmake -S "$ROOT" -B "$B" -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" -DCMAKE_BUILD_TYPE=Release \
    -DFSO_BUILD_WITH_OPENGL_ES=ON -DFSO_BUILD_WITH_OPENXR=ON -DFSO_BUILD_WITH_VULKAN=OFF \
    -DFSO_BUILD_QTFRED=OFF -DFSO_RELEASE_LOGGING=ON \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-28 -DEMBEDFILE_PATH="$HOST/bin/embedfile"
fi
sed -i 's/-lusb-1.0//g' "$B/build.ninja"
"$NINJA" -C "$B"

# 3) stage shared libraries (stripped)
rm -rf "$OUT/jniLibs"; mkdir -p "$OUT/jniLibs/arm64-v8a"
STRIP="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"
for so in "$B"/bin/*.so; do "$STRIP" --strip-unneeded -o "$OUT/jniLibs/arm64-v8a/$(basename "$so")" "$so"; done
ENGINE="$(basename "$(ls "$B"/bin/libfs2_open_*.so | head -1)" .so)"; ENGINE="${ENGINE#lib}"
sed -i "s/ENGINE = \".*\"/ENGINE = \"$ENGINE\"/" "$ROOT/android/app/src/main/java/com/bilko/fs2vr/BuildConfigLib.java"

# 4) APK
cd "$ROOT/android"
./gradlew -q assembleDebug
APK="$ROOT/android/app/build/outputs/apk/debug/app-debug.apk"
ls -la "$APK"

for a in "$@"; do
  case "$a" in
    --install) "$ADB" install -r -g "$APK"
               "$ADB" shell appops set $PKG MANAGE_EXTERNAL_STORAGE allow ;;
    --run)     "$ADB" shell am broadcast -a com.oculus.vrpowermanager.prox_close >/dev/null
               "$ADB" shell input keyevent KEYCODE_WAKEUP
               "$ADB" shell am force-stop $PKG
               "$ADB" logcat -c
               "$ADB" shell am start -n $ACTIVITY ;;
  esac
done
