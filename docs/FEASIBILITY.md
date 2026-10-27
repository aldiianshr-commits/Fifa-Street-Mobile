# Riset & kelayakan: FIFA Street (Xbox 360) → Android ARM64 native

## Proyek rujukan

* **XenonRecomp** (hedge-dev) — mengubah executable Xbox 360 menjadi C++ yang bisa dikompilasi ulang untuk platform lain; terinspirasi N64: Recompiled. Proyek ini **tidak menyediakan runtime**: kernel, GPU, audio menjadi tanggung jawab port masing-masing. Batas fungsi diambil dari `.pdata`/analisis/TOML, jump table perlu dideteksi per game, alamat fungsi save/restore register harus dikonfigurasi manual, MMIO dan exception belum didukung. Sebagian besar VMX memakai intrinsik x86, ARM64 didukung lewat SIMD Everywhere. Contoh sukses: *Unleashed Recompiled* (Sonic Unleashed) di PC — dengan runtime, renderer, dan HLE yang ditulis khusus untuk game itu.
* **ReXGlue SDK** — runtime + toolkit recompilation Xbox 360 yang berakar dari Xenia dan terinspirasi XenonRecomp. Masih tahap awal (API bisa berubah). Build CI tercantum untuk Windows amd64, Linux amd64 dan Linux arm64; **Android tidak disebut**.
* **Xenia** — emulator Xbox 360 (BSD). Dipakai di sini **hanya** sebagai rujukan format (struktur XEX2) dan tabel nama ekspor kernel; tidak ada kode emulator yang dipakai.

## Apa yang dibutuhkan agar FIFA Street berjalan native

1. **Executable**: `default.xex` retail hampir pasti terenkripsi + kompresi LZX. Loader proyek ini sudah mendukung keduanya *dengan kunci milik pengguna* (atau XEX yang sudah didekripsi pengguna).
2. **CPU**: recompiler ini menerjemahkan instruksi integer/FPU/branch/load-store/atomik, AltiVec/VMX standar, dan VMX128 (kecuali `vpkd3d128`/`vupkd3d128` – konversi format D3D) dan lolos uji diferensial. Catatan: VMX128 hanya bisa diuji dengan instruksi yang dienkode manual karena tidak ada assembler/kompiler publik yang menghasilkannya; mode denormal VMX (VSCR[NJ]) dan presisi instruksi estimasi (`vrefp`, `vrsqrtefp`, `vexptefp`, `vlogefp`) belum meniru hardware secara bit-exact. Analyzer di aplikasi menampilkan instruksi apa saja pada XEX Anda yang masih belum diterjemahkan.
3. **Kernel/XAM HLE**: game retail mengimpor ratusan fungsi (thread, event, semaphore, file I/O `NtCreateFile/NtReadFile` ke `game:\`, memori fisik, video `Vd*`, XAM konten/profil/UI). Saat ini 18 fungsi diimplementasikan; analyzer menampilkan berapa impor FIFA Street yang sudah/ belum ada.
4. **Thread**: Xenon punya 6 hardware thread; game memakai banyak thread. Runtime saat ini menjalankan guest di satu thread host.
5. **GPU Xenos → Vulkan**: pemrosesan ring buffer PM4, state register, terjemahan shader microcode Xenos ke SPIR-V, EDRAM & resolve, untiling + konversi format tekstur. Ini komponen terbesar dan paling berisiko untuk Adreno 650. **Belum dibuat.** Renderer Vulkan saat ini hanya presenter framebuffer.
6. **Audio**: XMA decoding + XAudio2 mixing. **Belum dibuat.**
7. **Konfigurasi per game**: alamat jump table/save-restore yang tidak terdeteksi otomatis, hook untuk bagian yang perlu perlakuan khusus.

## Kesimpulan kelayakan

* Pendekatan static recompilation **layak secara teknis** untuk CPU — dibuktikan oleh uji diferensial (kode PowerPC 64-bit dan 32-bit, termasuk AltiVec, hasil recompile memberi hasil identik dengan build native di x86-64 Linux, ARM64 Linux, dan Android 11 (emulator, translasi ARM); dapat diuji di perangkat Anda lewat *Pengaturan → Uji runtime*).
* Menjalankan FIFA Street masih memerlukan pekerjaan besar pada HLE kernel, thread, GPU Xenos, audio, dan sisa VMX128 (pack/unpack format D3D). Tidak ada klaim jadwal atau performa di sini karena belum bisa diukur.
* Kode hasil recompile game komersial tidak boleh di-commit ke repo publik; alur yang disediakan adalah pengguna me-recompile XEX miliknya sendiri lalu membangun APK pribadi (lihat [USAGE.md](USAGE.md)).

## Sumber

* XenonRecomp README — https://cdn.jsdelivr.net/gh/hedge-dev/XenonRecomp@main/README.md
* ReXGlue SDK — https://github.com/rexglue/rexglue-sdk
* Xenia — https://github.com/xenia-project/xenia (BSD-3-Clause)
