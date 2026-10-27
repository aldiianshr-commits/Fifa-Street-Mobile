#!/usr/bin/env bash
# Smoke test on an Android emulator (x86_64 image; the arm64 APK runs through the
# image's ARM->x86 native bridge). Results are emitted as GitHub annotations.
set -x
APK=${1:-FIFA-STREET-Recompiled-arm64-v8a.apk}
PKG=com.aldiianshr.fifastreet
INFO="abilist=$(adb shell getprop ro.product.cpu.abilist) bridge=$(adb shell getprop ro.dalvik.vm.native.bridge) sdk=$(adb shell getprop ro.build.version.sdk)"
OUT=$(adb install -r -g "$APK" 2>&1)
RC=$?
echo "::notice title=emulator install::${INFO} rc=${RC} $(echo "$OUT" | tail -3 | tr '\n' ' ')"
[ $RC -eq 0 ] || exit 1
adb shell appops set $PKG MANAGE_EXTERNAL_STORAGE allow || true

adb logcat -c
adb shell am start -W -n $PKG/.MainActivity --es autotest selftest
sleep 30
adb logcat -d -s FSR:I > selftest.log
adb shell am force-stop $PKG

adb logcat -c
adb shell am start -W -n $PKG/.MainActivity --es autotest play --ei seconds 12
sleep 35
adb logcat -d -s FSR:I > play.log
adb logcat -d > full.log
adb shell am force-stop $PKG
grep -E "FATAL EXCEPTION|AndroidRuntime|Fatal signal|backtrace:" -A 12 full.log | head -60 > crash.log

note() {  # title file pattern
  local msg
  msg=$(grep -E "$3" "$2" | head -40 | sed 's/%/%25/g' | awk '{printf "%s%%0A", $0}')
  echo "::notice title=$1::${msg:-no matching lines}"
}
note "emulator selftest" selftest.log "AUTOTEST|\[selftest\]|App start"
note "emulator play" play.log "AUTOTEST|\[vk\]|\[session\]|\[guest\]|unimplemented|Vulkan"
if [ -s crash.log ]; then
  msg=$(head -60 crash.log | sed 's/%/%25/g' | awk '{printf "%s%%0A", $0}')
  echo "::warning title=emulator crash::$msg"
fi
grep -q "AUTOTEST selftest ok=true matched=36/36" selftest.log
