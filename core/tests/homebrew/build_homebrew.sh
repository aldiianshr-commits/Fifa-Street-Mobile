#!/usr/bin/env bash
# Builds the homebrew test XEX files from source (our own code, no game data).
#   build_homebrew.sh <tools-bin-dir> <out-dir>
# Produces (each also contains vectest.c built with AltiVec): hb64.xex (PowerPC64 ELFv2, like Xenon 64-bit code), hb32.xex
# (32-bit PowerPC), hb64_enc.xex (basic-compressed + AES with a TEST key that
# this script generates - it is not a console key) and matching .syms files.
set -euo pipefail
TOOLS=$(cd "$1" && pwd)
OUT=$(mkdir -p "$2" && cd "$2" && pwd)
SRC=$(cd "$(dirname "$0")" && pwd)
CC=${CLANG:-clang}
LLD=${LLD:-ld.lld}
CF="-O2 -ffp-contract=off -mllvm -ppc-min-jump-table-entries=4 -ffreestanding -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-exceptions -fno-math-errno -mno-altivec"
IMPORTS="--import xam.xex:XamInputGetState --import xboxkrnl.exe:DbgPrint --import xboxkrnl.exe:NtOpenFile --import xboxkrnl.exe:NtReadFile --import xboxkrnl.exe:NtQueryInformationFile --import xboxkrnl.exe:NtSetInformationFile --import xboxkrnl.exe:NtQueryFullAttributesFile --import xboxkrnl.exe:NtClose"

build() {  # name target cpu extra-cflags ld-emulation
  local name=$1 target=$2 cpu=$3 extra=$4 emu=$5 vcpu=$6
  for f in selftest demo crt fileio; do
    $CC --target=$target $extra $CF -mcpu=$cpu -c "$SRC/$f.c" -o "$OUT/$f.$name.o"
  done
  # Vector test: AltiVec enabled (auto-vectorisation + intrinsics).
  $CC --target=$target $extra $CF -mcpu=$vcpu -maltivec -c "$SRC/vectest.c" -o "$OUT/vectest.$name.o"
  $CC --target=$target $extra -c "$SRC/thunks.S" -o "$OUT/thunks.$name.o"
  $CC --target=$target $extra -mcpu=$vcpu -maltivec -c "$SRC/vmx128.S" -o "$OUT/vmx128.$name.o"
  $LLD -m $emu -static -nostdlib -T "$SRC/link.ld" "$OUT"/{selftest,vectest,demo,crt,fileio,thunks,vmx128}.$name.o -o "$OUT/$name.elf"
}

build hb64 powerpc64-unknown-linux-gnu 970 "-mabi=elfv2" elf64ppc 970
build hb32 powerpc-unknown-linux-gnu 750 "-DFSR_NO_WIDE" elf32ppc 7400

"$TOOLS/mkxex" "$OUT/hb64.elf" "$OUT/hb64.xex" $IMPORTS --syms "$OUT/hb64.syms"
"$TOOLS/mkxex" "$OUT/hb32.elf" "$OUT/hb32.xex" $IMPORTS --syms "$OUT/hb32.syms"
# Deterministic throwaway test key (NOT a Microsoft key).
printf '00112233445566778899aabbccddeeff' > "$OUT/test.key"
"$TOOLS/mkxex" "$OUT/hb64.elf" "$OUT/hb64_enc.xex" $IMPORTS --basic --key "$OUT/test.key"

# Test disc image: the homebrew XEX as default.xex plus data files.
printf 'Hello from the disc image! FIFA STREET Recompiled reads game:\\ directly from the ISO.\n' > "$OUT/hello.txt"
head -c 3000 /dev/zero | tr '\0' 'R' > "$OUT/readme.txt"
ISOFILES="default.xex=$OUT/hb64.xex data/hello.txt=$OUT/hello.txt media/readme.txt=$OUT/readme.txt media/intro.wmv=$OUT/hello.txt"
"$TOOLS/mkiso" "$OUT/homebrew.iso" $ISOFILES                    # rebuilt layout (small, bundled in the APK)
"$TOOLS/mkiso" "$OUT/homebrew_xgd2.iso" --xgd2 $ISOFILES        # XGD2 partition offset (sparse file)
