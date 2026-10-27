#!/usr/bin/env bash
# Rebuilds the host tools, the homebrew test XEX and its recompiled C++ module
# used by the Android app, then runs the host test-suite.
#   tools/regen_homebrew.sh [build-dir]
# Requirements: cmake, ninja, clang (with PowerPC target) and ld.lld.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${1:-$ROOT/build-host}
mkdir -p "$BUILD" && BUILD=$(cd "$BUILD" && pwd)
cmake -S "$ROOT/core" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=${CC:-clang} -DCMAKE_CXX_COMPILER=${CXX:-clang++}
cmake --build "$BUILD"
(cd "$BUILD" && ctest --output-on-failure)

GEN="$ROOT/android/app/src/main/cpp/generated/hb64"
ASSET="$ROOT/android/app/src/main/assets/homebrew"
rm -rf "$GEN"
mkdir -p "$GEN" "$ASSET"
"$BUILD/fsr-recomp" "$BUILD/tests/hb/hb64.xex" --out "$GEN" --name hb64 \
  --syms "$BUILD/tests/hb/hb64.syms" --export fsr_selftest --export fsr_frame --export fsr_vectest \
  --export fsr_vectest_dump --export fsr_vmx128_test
cp "$BUILD/tests/hb/hb64.xex" "$ASSET/hb64.xex"
(cd "$ASSET" && "$BUILD/fsr-analyze" hb64.xex > hb64.analysis.txt)
echo "regen_homebrew: generated $(ls "$GEN" | wc -l) files in $GEN"
