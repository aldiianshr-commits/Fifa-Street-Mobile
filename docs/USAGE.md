# Panduan penggunaan

## 1. Menambahkan game: satu file ISO

* **Library → ＋ Tambah ISO**, pilih file `.iso` Xbox 360 milik Anda (dari penyimpanan internal, kartu SD, atau USB OTG). Alternatif: buka ISO dari file manager dengan "Buka dengan → FIFA STREET Recompiled".
* ISO dibaca **di tempat** lewat file descriptor dari Storage Access Framework; izin baca disimpan permanen sehingga library tetap ada setelah aplikasi ditutup atau perangkat di-restart.
* Bila penyedia penyimpanan hanya memberikan stream (umumnya penyedia cloud), aplikasi menyalin ISO sekali ke penyimpanan aplikasi (jika ruang cukup) lalu membacanya dari sana.
* Hasil analisis di-cache per ISO; pemrosesan ulang hanya terjadi bila ISO, kunci, atau versi aplikasi berubah (atau Anda menekan *Proses ulang*).
* *Pindai folder* (opsional) mencari semua ISO di sebuah folder; ini butuh izin "Akses semua file".
* Paket **STFS/Games on Demand** dan executable XEX polos dikenali tetapi diberi status Unsupported dengan alasannya.

## 2. XEX terenkripsi

`default.xex` retail terenkripsi AES. Impor kunci XEX dari konsol Anda sendiri lewat *Detail game → Impor kunci XEX* (16 byte biner atau 32 digit heks). Kunci disimpan privat di aplikasi dan dapat dihapus; semua ISO diproses ulang otomatis. Kunci yang salah terdeteksi (hash blok/MZ tidak valid).

## 3. Membaca status kompatibilitas

Setiap game di Library berstatus **Ready** (semua persyaratan runtime terpenuhi), **Processing** (sedang dipindai/dianalisis, dengan tahap & persentase), **Partial** (format kompatibel, tetapi ada komponen yang belum didukung atau kunci belum ada), atau **Unsupported** (format/komponen tidak kompatibel, mis. bukan ISO Xbox 360, STFS, ISO terpotong). Halaman detail mencantumkan setiap persyaratan (filesystem, kelengkapan data, executable, region, enkripsi, dekompresi, modul lain, terjemahan CPU, impor kernel, thread, audio, file yang dirujuk, modul native ARM64, jalur grafis) dengan status dan penyebab teknisnya.

## 4. Me-recompile XEX Anda (PC Linux/WSL)

```bash
./tools/regen_homebrew.sh build-host          # build fsr-analyze / fsr-recomp
./build-host/fsr-analyze game.iso --key xex.key              # ISO langsung: laporan disc + executable
./build-host/fsr-analyze default.xex --key xex.key           # atau XEX polos
./build-host/fsr-analyze default.xex --key xex.key --json    # JSON
./build-host/fsr-recomp  game.iso --key xex.key --out gen/fifastreet --name fifastreet   # XEX diambil dari ISO
```

Untuk memasukkannya ke APK pribadi: salin `gen/fifastreet/*` ke `android/app/src/main/cpp/generated/fifastreet/`, tambahkan berkasnya di `android/app/src/main/cpp/CMakeLists.txt` dan panggil `fsr::register_module(&fsr_module_fifastreet)` di `RegisterModules` (`session.cpp`), lalu build. Setelah itu persyaratan "Modul native ARM64" pada ISO yang sama otomatis menjadi terpenuhi. **Jangan publikasikan** kode hasil recompile game komersial.

Catatan jujur: untuk FIFA Street, langkah ini saat ini menghasilkan modul yang **belum dapat berjalan sampai selesai** karena thread guest, GPU Xenos, audio, sebagian besar HLE kernel, dan `vpkd3d128`/`vupkd3d128` belum diimplementasikan; runtime akan berhenti dengan `GuestFault` yang menyebut instruksi/impor yang belum ada — informasi itu berguna sebagai daftar pekerjaan berikutnya. Selain itu, sesi Play saat ini memanggil entry point `fsr_frame` milik homebrew; entry game retail (`mainCRTStartup`) membutuhkan thread & kernel yang lebih lengkap.

## 5. Kontrol

* **Kontrol → Panel**: seret tombol, ketuk lalu atur ukuran, transparansi, sensitivitas analog sentuh, getar, tampil/sembunyi.
* **Gamepad**: remap setiap tombol Xbox (A/B/X/Y, LB/RB, LT/RT, D-pad, Start, Back, L3/R3), deadzone radial, sensitivitas, tukar analog, balik sumbu Y.
* **Profil**: buat/ganti nama/hapus, preset Default/Ringkas/Besar, profil per game.

## 6. Grafis

Resolusi internal 480p/720p/1080p, render scale 50–100%, batas FPS 30/60, VSync (FIFO) atau MAILBOX/IMMEDIATE, aspect 16:9 atau regang, filter nearest/linear, profil Performance/Balanced/Quality, adaptif termal, overlay statistik. Opsi yang membutuhkan renderer Xenos (AF, AA, shader cache, efek) ditampilkan nonaktif.

## 7. Uji di perangkat

* **Homebrew Self-Test → Play**: gerakkan bola dengan analog kiri/D-pad, A/B/X/Y mengubah warna, LT/RT ukuran; indikator tombol di bawah layar menyala sesuai input yang diterima kode guest.
* **Pengaturan → Uji runtime**: 20 hasil native vs recompile + waktu eksekusi per iterasi, diukur di perangkat Anda.
* **Pengaturan → Tes audio**: nada 440 Hz melalui AAudio.
* **Log**: log runtime, bagikan, simpan laporan; laporan crash dibuat otomatis.
