<p align="center"><img src="docs/img/icon-512.png" width="160" alt="logo"></p>

# FIFA STREET Recompiled (Android ARM64)

Proyek porting **native** FIFA Street versi Xbox 360 ke Android (target: **Xiaomi Pad 6 — Snapdragon 870, Adreno 650, Vulkan**) dengan pendekatan *static recompilation*: kode PowerPC (Xenon) dari `default.xex` diterjemahkan ke C++ lalu dikompilasi oleh Android NDK menjadi kode ARM64. **Tidak memakai emulator Xbox 360, interpreter, atau Xenia sebagai backend tersembunyi.**

> **Status jujur saat ini:** FIFA Street **belum dapat dimainkan**. Yang sudah berfungsi dan teruji adalah fondasinya: pemindai & loader XEX, recompiler PowerPC→C++ termasuk **AltiVec/VMX dan sebagian besar VMX128** (diuji diferensial dengan program homebrew), runtime + HLE kernel dasar, renderer Vulkan (presenter framebuffer), input sentuh/gamepad yang terhubung ke runtime, dan launcher Android lengkap. Komponen besar yang belum ada: GPU **Xenos → Vulkan**, audio **XMA/XAudio2**, **thread/sinkronisasi guest**, sebagian besar **HLE kernel**, serta `vpkd3d128`/`vupkd3d128`. Detail di [docs/FEASIBILITY.md](docs/FEASIBILITY.md) dan hasil uji di [docs/TEST_REPORT.md](docs/TEST_REPORT.md).

## Status fitur

| Komponen | Status | Keterangan |
|---|---|---|
| **Input satu file ISO** (pilih lewat SAF / "Buka dengan") | ✅ Berfungsi | ISO dibaca langsung lewat file descriptor (`pread`), tanpa ekstraksi; izin baca disimpan; library & hasil analisis tetap ada setelah aplikasi ditutup |
| Pemindaian & validasi ISO otomatis | ✅ Berfungsi | Layout XGD2/XGD3/rebuilt (+ deteksi XGD1/Xbox original, STFS, XEX polos), validasi descriptor & pohon direktori, inventaris & klasifikasi semua file (magic + ekstensi), deteksi berkas terpotong |
| Deteksi executable, modul, region, enkripsi, file yang dirujuk | ✅ Berfungsi | `default.xex` dibaca dari ISO; XEX lain di disc (DLL/patch) dilaporkan; literal path `game:\`/`d:\` di kode dicocokkan dengan isi ISO |
| Status library Ready / Processing / Partial / Unsupported + progres per tahap | ✅ Berfungsi | Setiap persyaratan runtime dicantumkan dengan alasannya; bisa dibatalkan; cache hasil analisis |
| ISO di-mount sebagai `game:\` untuk runtime (HLE NtCreateFile/NtOpenFile/NtReadFile/NtQueryInformationFile/NtSetInformationFile/NtQueryFullAttributesFile/NtClose) | ✅ Berfungsi | Read-only; diuji 11/11 cek dari kode PowerPC hasil recompile |
| Penyedia penyimpanan tanpa akses acak (stream) | ⚠️ Parsial | Otomatis disalin sekali ke penyimpanan aplikasi bila ruang cukup; belum diuji dengan penyedia cloud sungguhan |
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
| HLE kernel/XAM | ⚠️ Parsial | 25 fungsi (DbgPrint, NtAllocateVirtualMemory, file I/O disc, XamInputGetState, …). Fungsi lain dilaporkan per game |
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

1. Pasang APK, buka aplikasi, ketuk **＋ Tambah ISO**, lalu pilih satu file ISO Xbox 360 milik Anda. Tidak perlu izin "Akses semua file" dan tidak perlu memilih XEX/file internal apa pun.
2. Aplikasi otomatis: validasi format → baca filesystem → pindai & klasifikasi seluruh isi → baca `default.xex` langsung dari ISO → dekripsi/dekompresi → analisis & cek terjemahan kode → periksa modul dan file yang dirujuk. Progres tampil per tahap dan bisa dibatalkan.
3. Game masuk ke Library dengan status **Ready / Processing / Partial / Unsupported**; halaman detail menjelaskan setiap persyaratan yang belum terpenuhi beserta penyebab teknisnya.
4. Jika XEX terenkripsi dan tidak ada kunci, statusnya **Partial** dengan alasan "terenkripsi AES-128 – impor kunci XEX dari konsol Anda". Tidak ada keberhasilan dekripsi yang dikarang.
5. **Homebrew Self-Test (ISO uji bawaan)** berstatus Ready dan bisa di-*Play*: runtime membaca executable dan file data langsung dari ISO uji itu. *Pengaturan → Ekspor ISO uji* menyimpan ISO tersebut agar alur "Tambah ISO" bisa dicoba.

Yang akan Anda lihat untuk FIFA Street saat ini: **Partial** (atau Unsupported bila ISO rusak/terpotong). Alasannya: modul ARM64 hasil recompile untuk executable FIFA Street belum ada di APK, kompilasi C++ → ARM64 butuh toolchain di PC, dan GPU Xenos, thread, serta audio belum diimplementasikan.

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
