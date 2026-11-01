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
nodes = re.findall(r'<node [^>]*>', s)
target = None
for i, n in enumerate(nodes):
    if 'text="fsr-test.iso"' in n:
        target = i
        break
if target is not None:
    # walk back to the nearest clickable ancestor-ish node (list item) and tap its centre
    pick = nodes[target]
    def box(n):
        return tuple(map(int, re.search(r'bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', n).groups()))
    tb = box(nodes[target])
    for j in range(target, max(-1, target - 20), -1):
        n = nodes[j]
        if 'clickable="true"' not in n or 'preview_icon' in n or 'Preview' in n:
            continue
        b = box(n)
        if b[0] <= tb[0] and b[1] <= tb[1] and b[2] >= tb[2] and b[3] >= tb[3]:  # contains the title
            pick = n
            break
    open("saf_node.txt", "w").write(nodes[target] + "\nTAP: " + pick + "\n")
    m = re.search(r'bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', pick)
    a, b, c, d = map(int, m.groups())
    print((a + c) // 2, (b + d) // 2)
PY
)
  if [ -n "$xy" ]; then
    echo "texts before tap: $(grep -o 'text="[^"]*"' ui.xml | head -40 | tr '\n' ' ')" > saf_ui.txt
    cat saf_node.txt >> saf_ui.txt
    adb shell input tap $xy; picked=1; break
  fi
done
sleep 6
adb shell uiautomator dump /sdcard/ui2.xml >/dev/null 2>&1; adb pull /sdcard/ui2.xml ui2.xml >/dev/null 2>&1
echo "texts after tap: $(grep -o 'text="[^"]*"' ui2.xml | head -40 | tr '\n' ' ')" >> saf_ui.txt
echo "resumed: $(adb shell dumpsys activity activities | grep -m2 -E 'mResumedActivity|ResumedActivity')" >> saf_ui.txt
sleep 6
adb logcat -d -s FSR:I > saf.log
via="documentsui"
if ! grep -q "ISO ditambahkan" saf.log; then
  # Deterministic fallback: open the same ISO through a content:// URI (MediaStore) with an
  # ACTION_VIEW intent, exercising ContentResolver -> ParcelFileDescriptor -> pread.
  via="mediastore-view"
  adb shell am force-stop $PKG
  id=$(adb shell content query --uri content://media/external/file --projection _id \
       --where "_data=\'/storage/emulated/0/Download/fsr-test.iso\'" | sed -n 's/.*_id=\([0-9]*\).*/\1/p' | head -1)
  adb logcat -c
  adb shell am start -W -a android.intent.action.VIEW -d "content://media/external/file/$id" -t application/x-iso9660-image -n $PKG/.MainActivity
  sleep 10
  adb logcat -d -s FSR:I > saf.log
fi
adb shell am force-stop $PKG
# Restart: the library must come back from library.json + persisted URI permission + cache.
adb logcat -c
sleep 1
adb shell am start -W -n $PKG/.MainActivity > restart_start.txt 2>&1
sleep 10
adb logcat -d > restart_full.log
grep " FSR " restart_full.log > saf_restart.log || true
{ cat restart_start.txt; grep -E "fifastreet|AndroidRuntime" restart_full.log | tail -15; } > restart_diag.txt
adb shell am force-stop $PKG
note "emulator SAF pick (picked=$picked via=$via)" saf.log "."
note "emulator SAF ui" saf_ui.txt "."
note "emulator SAF after restart" saf_restart.log "."
note "emulator restart diag" restart_diag.txt "."

grep -E "FATAL EXCEPTION|AndroidRuntime|Fatal signal|backtrace:" -A 12 full.log | head -60 > crash.log

note "emulator selftest" selftest.log "AUTOTEST|\[selftest\]|\[scan\]|\[disc-test\]|App start"
note "emulator play" play.log "AUTOTEST|\[vk\]|\[session\]|\[guest\]|\[fs\]|Library|unimplemented|Vulkan"
if [ -s crash.log ]; then
  msg=$(head -60 crash.log | sed 's/%/%25/g' | awk '{printf "%s%%0A", $0}')
  echo "::warning title=emulator crash::$msg"
fi
grep -q "AUTOTEST selftest ok=true matched=36/36" selftest.log && grep -q "AUTOTEST scan status=ready" selftest.log && grep -q "AUTOTEST discio passed=11/11" selftest.log
RC=$?
grep -q "fsr-test.iso: ready" saf.log && grep -q "fsr-test.iso: hasil analisis dari cache (ready)" saf_restart.log || echo "::warning title=SAF flow::SAF pick/persistence check did not fully pass"
exit $RC
