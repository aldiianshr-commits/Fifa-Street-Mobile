# Laporan pengujian

Tanggal: 27 Oktober 2026 · commit terbaru di `main` · semua angka di bawah berasal dari eksekusi nyata (lokal dan GitHub Actions), bukan perkiraan.

## Ringkasan

| Lingkungan | Yang diuji | Hasil |
|---|---|---|
| Linux x86-64 (lokal + CI `ubuntu-24.04`) | 92 unit test; uji diferensial homebrew 64-bit & 32-bit | ✅ lulus semua |
| Linux ARM64 (CI `ubuntu-24.04-arm`) | Sama seperti di atas, kode hasil recompile dikompilasi untuk AArch64 | ✅ lulus semua |
| Android 11 emulator x86_64 dengan translasi ARM (`libndk_translation`), Vulkan SwiftShader | Pemasangan APK arm64, self-test runtime di dalam aplikasi, sesi *Play* homebrew dengan input terskrip | ✅ 36/36 identik, VMX128 0 gagal, sesi berjalan tanpa error |
| Xiaomi Pad 6 fisik | — | ⚠️ **Belum diuji**: tidak ada akses ke perangkat fisik dari lingkungan build. Jalankan *Pengaturan → Uji runtime* dan *Homebrew Self-Test → Play* di perangkat Anda |
| FIFA Street | — | ❌ Belum dapat dijalankan (lihat FEASIBILITY.md). Tidak ada file game yang tersedia/diunduh |

## 1. Unit test (`core/tests/unit_tests.cc`) — 92 cek

* AES-128 terhadap vektor FIPS-197 C.1, CBC berantai; SHA-1 terhadap vektor FIPS 180.
* Decoder PowerPC (encoding diketahui: `mflr`, `blr`, `bctr`, `rlwinm`, `rldicl`, `dcbz128`, `sradi`, FPU, `ld/stdu`), target branch, `mask64` termasuk mask melingkar.
* Decoder VMX/VMX128 (encoding standar & VMX128, field register 7-bit, bit Rc), semantik saturasi (`vaddsws` → VSCR[SAT]).
* Helper runtime (shift aritmetika + carry, carry 64-bit, `cntlz`, pembagian overflow, `fctiwz` saturasi, pack/unpack CR).
* Loader XEX: XEX polos; XEX **AES + kompresi basic** dibuat dengan kunci uji acak → `needs-key` tanpa kunci, `key-mismatch` dengan kunci salah, image identik byte-per-byte dengan kunci benar; input rusak ditolak dengan aman; laporan JSON.
* ISO XDVDFS (fixture `mkiso`): deteksi kontainer, pencarian berkas tidak peka huruf besar, ekstraksi `default.xex` identik.

## 2. Uji diferensial homebrew (`core/tests/homebrew_test.cc`)

Program C buatan proyek ini dikompilasi dua kali: (a) ke PowerPC big-endian (64-bit ELFv2 `-mcpu=970` dan 32-bit `-mcpu=750/7400`), dibungkus menjadi XEX oleh `mkxex`, dimuat oleh loader, di-recompile oleh `fsr-recomp`, lalu dikompilasi untuk host; (b) langsung untuk host. Hasil harus identik bit-per-bit.

| Uji | hb64 | hb32 |
|---|---|---|
| `selftest.c` (rekursi, CRC32, sorting, jump table/`bctr`, aritmetika 64-bit, pembagian bertanda/tak bertanda, bit ops, FPU double/single, konversi, function pointer, struct, atomik `lwarx/stwcx.`/`ldarx/stdcx.`, `mulhd`) | 24/24 identik | 24/24 identik |
| `vectest.c` (AltiVec: auto-vectorization + intrinsik; 65 helper VMX berbeda, termasuk saturasi, multiply-sum, pack/unpack, rotate, record → CR6) | 12/12 identik | 12/12 identik |
| `vmx128.S` (16 instruksi VMX128 dienkode manual, register v33–v127, 65 cek termasuk CR6) | 65/65 | 65/65 |
| `demo.c` render 24 frame 320×180 dengan input terskrip via `XamInputGetState` HLE + `DbgPrint` | 24/24 frame identik | 24/24 frame identik |

Statistik recompiler pada `hb64.xex`: 50 fungsi, 3.387 instruksi, 100% diterjemahkan, 844 instruksi VMX (39 encoding VMX128), 2 fungsi dengan dispatch `bctr`.

Catatan jujur: satu kegagalan sempat muncul saat menulis uji `vslo`; penyebabnya adalah ekspektasi uji yang salah (jumlah geser diambil dari bit 121:124), bukan runtime — uji diperbaiki.

## 3. Android (CI, job *Emulator smoke test*)

Emulator: `sdk_gphone_x86_64`, Android 11 / API 30, ABI `x86_64, x86, arm64-v8a, …`, native bridge `libndk_translation.so`. APK yang diuji adalah artefak arm64-v8a yang sama dengan rilis.

* Instalasi: `Performing Streamed Install — Success`.
* Self-test di aplikasi (`--es autotest selftest`): `[selftest] 36/36 match (incl. 12/12 AltiVec), VMX128 failures 0`.
* Sesi *Play* homebrew (`--es autotest play`, 12 detik, input D-pad/A/B/X/Y terskrip): Vulkan `SwiftShader Device (Subzero)` Vulkan 1.3.0, swapchain 640×320, resolusi internal 854×480, ±496 frame tanpa error, ±502 panggilan kernel (polling input tiap frame), log guest `homebrew demo running: 854x480`.
* Angka waktu/FPS dari emulator **tidak mewakili** Xiaomi Pad 6 (CPU ARM ditranslasikan ke x86 dan GPU dirender perangkat lunak), sehingga tidak dicantumkan sebagai benchmark.

## 4. Pemeriksaan APK

* `lib/arm64-v8a/libfsr.so`: ELF64 AArch64, segmen LOAD rata 16 KB (kompatibel perangkat page 16 KB).
* 25 simbol JNI yang diekspor cocok persis dengan 25 deklarasi `external fun` di `NativeBridge.kt`.
* Dependensi: `libandroid`, `liblog`, `libvulkan`, `libaaudio`, `libm`, `libdl`, `libc`.
* Aset: `assets/homebrew/hb64.xex` (program uji buatan proyek, bukan data game).

## 5. Yang belum diuji

* Perangkat fisik (Xiaomi Pad 6 / Adreno 650): performa nyata, perilaku driver Vulkan Adreno, thermal throttling sungguhan, gamepad Bluetooth/USB fisik.
* XEX retail asli: dekripsi dengan kunci konsol, dekompresi LZX data nyata, cakupan instruksi FIFA Street. Proyek tidak memiliki dan tidak mengunduh file game.
* VMX128 terhadap hardware Xenon asli; `vpkd3d128`/`vupkd3d128` belum diimplementasikan.

## Cara mengulang

```bash
./tools/regen_homebrew.sh build-host           # build + ctest (unit_tests, homebrew_test)
./build-host/tests/homebrew_test build-host/tests/hb
# Android (perangkat terhubung / emulator API 30+):
adb install -r FIFA-STREET-Recompiled-arm64-v8a.apk
adb shell am start -n com.aldiianshr.fifastreet/.MainActivity --es autotest selftest
adb logcat -s FSR:I
```
