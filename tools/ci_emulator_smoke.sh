#!/usr/bin/env bash
# Smoke test on an Android emulator (x86_64 image; the arm64 APK runs through the
# image's ARM->x86 native bridge). Results are emitted as GitHub annotations.
set -x
APK=${1:-FIFA-STREET-Recompiled-arm64-v8a.apk}
PKG=com.aldiianshr.fifastreet
note() {  # title file pattern
  local msg
  msg=$(grep -E "$3" "$2" | head -40 | sed 's/%/%25/g' | awk '{printf "%s%%0A", $0}')
  echo "::notice title=$1::${msg:-no matching lines}"
}
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
# ---- Real Storage Access Framework flow: pick the ISO in DocumentsUI.
unzip -p "$APK" assets/homebrew/homebrew.iso > fsr-test.iso
adb push fsr-test.iso /sdcard/Download/fsr-test.iso
adb shell am broadcast -a android.intent.action.MEDIA_SCANNER_SCAN_FILE -d file:///sdcard/Download/fsr-test.iso >/dev/null
adb logcat -c
adb shell am start -W -n $PKG/.MainActivity --es autotest pickiso
picked=0
for attempt in 1 2 3 4 5 6; do
  sleep 4
  adb shell uiautomator dump /sdcard/ui.xml >/dev/null 2>&1
  adb pull /sdcard/ui.xml ui.xml >/dev/null 2>&1 || continue
  xy=$(python3 - <<'PY'
import re
s = open("ui.xml", encoding="utf-8", errors="ignore").read()
m = re.search(r'text="fsr-test\.iso"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', s)
if m:
    a, b, c, d = map(int, m.groups())
    print((a + c) // 2, (b + d) // 2)
PY
)
  if [ -n "$xy" ]; then
    echo "texts before tap: $(grep -o 'text="[^"]*"' ui.xml | head -40 | tr '\n' ' ')" > saf_ui.txt
    adb shell input tap $xy; picked=1; break
  fi
done
sleep 6
adb shell uiautomator dump /sdcard/ui2.xml >/dev/null 2>&1; adb pull /sdcard/ui2.xml ui2.xml >/dev/null 2>&1
echo "texts after tap: $(grep -o 'text="[^"]*"' ui2.xml | head -40 | tr '\n' ' ')" >> saf_ui.txt
echo "resumed: $(adb shell dumpsys activity activities | grep -m2 -E 'mResumedActivity|ResumedActivity')" >> saf_ui.txt
sleep 6
adb logcat -d -s FSR:I > saf.log
adb shell am force-stop $PKG
# Restart: the library must come back from library.json + persisted URI permission + cache.
adb logcat -c
adb shell am start -W -n $PKG/.MainActivity
sleep 8
adb logcat -d -s FSR:I > saf_restart.log
adb shell am force-stop $PKG
note "emulator SAF pick (picked=$picked)" saf.log "."
note "emulator SAF ui" saf_ui.txt "."
note "emulator SAF after restart" saf_restart.log "Library dimuat|fsr-test|cache|tidak dapat diakses"

grep -E "FATAL EXCEPTION|AndroidRuntime|Fatal signal|backtrace:" -A 12 full.log | head -60 > crash.log

note "emulator selftest" selftest.log "AUTOTEST|\[selftest\]|\[scan\]|\[disc-test\]|App start"
note "emulator play" play.log "AUTOTEST|\[vk\]|\[session\]|\[guest\]|\[fs\]|Library|unimplemented|Vulkan"
if [ -s crash.log ]; then
  msg=$(head -60 crash.log | sed 's/%/%25/g' | awk '{printf "%s%%0A", $0}')
  echo "::warning title=emulator crash::$msg"
fi
grep -q "AUTOTEST selftest ok=true matched=36/36" selftest.log && grep -q "AUTOTEST scan status=ready" selftest.log && grep -q "AUTOTEST discio passed=11/11" selftest.log
RC=$?
grep -q "persisted=true" saf.log && grep -q "fsr-test.iso: ready" saf.log && grep -q "fsr-test.iso: hasil analisis dari cache (ready)" saf_restart.log || echo "::warning title=SAF flow::SAF pick/persistence check did not fully pass"
exit $RC
