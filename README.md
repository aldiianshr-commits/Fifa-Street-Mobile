<p align="center"><img src="docs/img/icon-512.png" width="160" alt="logo"></p>

# FIFA STREET Recompiled (Android ARM64)

Proyek porting **native** FIFA Street versi Xbox 360 ke Android (target: **Xiaomi Pad 6 — Snapdragon 870, Adreno 650, Vulkan**) dengan pendekatan *static recompilation*: kode PowerPC (Xenon) dari `default.xex` diterjemahkan ke C++ lalu dikompilasi oleh Android NDK menjadi kode ARM64. **Tidak memakai emulator Xbox 360, interpreter, atau Xenia sebagai backend tersembunyi.**

> **Status jujur saat ini:** FIFA Street **belum dapat dimainkan**. Yang sudah berfungsi dan teruji adalah fondasinya: pemindai & loader XEX, recompiler PowerPC→C++ termasuk **AltiVec/VMX dan sebagian besar VMX128** (diuji diferensial dengan program homebrew), runtime + HLE kernel dasar, renderer Vulkan (presenter framebuffer), input sentuh/gamepad yang terhubung ke runtime, dan launcher Android lengkap. Komponen besar yang belum ada: GPU **Xenos → Vulkan**, audio **XMA/XAudio2**, **thread/sinkronisasi guest**, sebagian besar **HLE kernel**, serta `vpkd3d128`/`vupkd3d128`. Detail di [docs/FEASIBILITY.md](docs/FEASIBILITY.md) dan hasil uji di [docs/TEST_REPORT.md](docs/TEST_REPORT.md).

## Status fitur

| Komponen | Status | Keterangan |
|---|---|---|
| Pemindai folder game (XEX, ISO XDVDFS, deteksi STFS/GOD) | ✅ Berfungsi | Pilih folder (SAF), pindai ulang, cache analisis, cek ISO terpotong |
| Loader XEX2: header, eksekusi/title info, impor, resource | ✅ Berfungsi | Diuji dengan XEX homebrew buatan sendiri |
| Dekripsi AES XEX | ✅ Berfungsi (kunci dari pengguna) | **Tidak ada kunci yang disertakan.** Impor kunci konsol Anda sendiri atau pakai XEX yang sudah didekripsi |
| Dekompresi basic | ✅ Berfungsi | Diuji (basic + AES dengan kunci uji acak) |
| Dekompresi LZX (normal) | ⚠️ Parsial | Memakai libmspack (dipakai juga oleh Xenia); belum diuji dengan data asli di CI karena tidak ada XEX LZX legal yang bisa disertakan |
| Delta patch (title update `.xexp`) | ❌ Belum tersedia | |
| Parser PE, `.pdata`, penemuan fungsi | ✅ Berfungsi | Termasuk target `bl`, tail call, rantai save/restore register |
| Recompiler PowerPC → C++ (integer, branch, load/store, FPU, CR/XER, atomik, jump table) | ✅ Berfungsi | 64-bit & 32-bit; hasil identik bit-per-bit dengan build native (24/24 nilai + 24/24 frame), di x86-64, ARM64 Linux, dan Android |
| AltiVec / VMX standar (±150 instruksi) | ✅ Berfungsi | 65 operasi berbeda diuji diferensial (auto-vectorization + intrinsik, termasuk bentuk record → CR6); sisanya diimplementasikan dari spesifikasi |
| VMX128 (ekstensi Xenon, register v0–v127) | ⚠️ Parsial | Semua opcode VMX128 kecuali `vpkd3d128`/`vupkd3d128`; diverifikasi dengan 65 cek instruksi yang dienkode manual (tidak ada assembler publik untuk VMX128, jadi belum dibandingkan dengan hardware asli) |
| Runtime: memori guest 4 GiB, panggilan tidak langsung, stack, TLS | ✅ Berfungsi | |
| HLE kernel/XAM | ⚠️ Parsial | 18 fungsi (DbgPrint, NtAllocateVirtualMemory, XamInputGetState, …). Fungsi lain dilaporkan di log |
| Thread guest (ExCreateThread, KeWait*, event) | ❌ Belum tersedia | |
| GPU Xenos → Vulkan (perintah PM4, shader, EDRAM, tekstur) | ❌ Belum tersedia | Komponen terbesar yang tersisa |
| Presenter Vulkan (resolusi internal, render scale, VSync/MAILBOX, filter, aspect, frame pacing) | ✅ Berfungsi | Menampilkan framebuffer yang ditulis kode guest (homebrew) |
| Audio XAudio2/XMA | ❌ Belum tersedia | Jalur AAudio perangkat dapat diuji (nada 440 Hz) |
| Kontrol sentuh layout Xbox (A/B/X/Y, LB/RB, LT/RT, D-pad, 2 analog, L3/R3, Start, Back) | ✅ Berfungsi | Posisi, ukuran, transparansi, sensitivitas; tersambung ke `XamInputGetState` guest |
| Gamepad Bluetooth/USB, remap semua tombol, deadzone, sensitivitas, tukar/balik sumbu | ✅ Berfungsi | |
| Preset & profil kontrol, profil per game | ✅ Berfungsi | |
| Pengaturan grafis global & per game, profil Performance/Balanced/Quality | ✅ Berfungsi | Opsi yang belum ada (AF, AA, shader cache, efek) ditampilkan **nonaktif** |
| Thermal throttling, pause/resume, siklus hidup Android, deteksi perangkat | ✅ Berfungsi | Turun ke 30 FPS saat status termal ≥ SEVERE (opsional) |
| Log runtime & laporan error | ✅ Berfungsi | Termasuk laporan crash otomatis |
| FIFA Street dapat dimainkan | ❌ Belum | Lihat daftar komponen yang belum tersedia |

Pemetaan **fungsi gameplay** FIFA Street (skill move, dsb.) sengaja **tidak** dicantumkan karena tidak dapat diverifikasi dari data game; tombol diteruskan 1:1 sebagai tombol Xbox 360.

## Unduh APK

Setiap push ke `main` dibangun oleh GitHub Actions:
- **Release `nightly`** → `FIFA-STREET-Recompiled-arm64-v8a.apk`
- atau artifact **FIFA-STREET-Recompiled-apk** pada run Actions.

APK ditandatangani dengan kunci uji publik (`android/signing/fsr-test.jks`, password di `app/build.gradle.kts`) agar build berikutnya bisa dipasang menimpa build lama. Ganti dengan kunci Anda sendiri sebelum mendistribusikan.

## Cara pakai singkat

1. Pasang APK, buka aplikasi, berikan izin **Akses semua file**.
2. **Pilih folder** berisi game milik Anda (mis. `/storage/emulated/0/FifaStreet/` berisi `default.xex` + data, atau file `.iso`).
3. Aplikasi memindai, mengekstrak `default.xex` dari ISO bila perlu, lalu menganalisis: title ID, enkripsi/kompresi, section, impor kernel, jumlah fungsi/instruksi, persentase yang dapat diterjemahkan, jumlah VMX.
4. Jika XEX terenkripsi: **Impor kunci XEX** (milik Anda sendiri) atau gunakan XEX yang sudah Anda dekripsi.
5. **Homebrew Self-Test** (bawaan) dapat langsung di-*Play*: bola digerakkan oleh analog kiri/D-pad, warna oleh A/B/X/Y, ukuran oleh LT/RT — ini membuktikan jalur *kontrol → runtime → kode PowerPC hasil recompile → Vulkan* bekerja di perangkat.
6. **Pengaturan → Uji runtime** membandingkan hasil kode native ARM64 vs kode PowerPC hasil recompile di perangkat Anda sendiri dan mengukur waktunya.

Panduan lengkap (termasuk me-recompile XEX Anda sendiri di PC): [docs/USAGE.md](docs/USAGE.md).

## Struktur repo

```
core/        C++17: loader XEX/ISO, decoder & recompiler PowerPC, runtime, HLE kernel, tools, tes
android/     Aplikasi Android (Kotlin UI + JNI/NDK + Vulkan + AAudio)
tools/       regen_homebrew.sh (build tools host, homebrew, recompile, tes)
docs/        ARCHITECTURE, FEASIBILITY, USAGE, TEST_REPORT
```

## Build sendiri

```bash
# Host (Linux): butuh cmake, ninja, clang (target PowerPC) dan ld.lld
./tools/regen_homebrew.sh build-host       # build + tes + regenerasi modul homebrew

# Android: butuh JDK 17, Android SDK 35, NDK 27.2.12479018, CMake 3.22.1
cd android && ./gradlew assembleRelease
```

## Legal

Proyek komunitas, tidak berafiliasi dengan EA, FIFA, atau Microsoft. Repo ini **tidak** berisi file game, firmware, kunci dekripsi, atau kode hasil recompile dari game komersial. Program uji di `core/tests/homebrew` ditulis khusus untuk proyek ini. Gunakan hanya salinan game yang Anda miliki secara sah. Lisensi: MIT untuk kode proyek; komponen pihak ketiga lihat [THIRD_PARTY.md](THIRD_PARTY.md).
