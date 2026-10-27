# Panduan penggunaan

## 1. Menyiapkan file game (milik Anda sendiri)

Struktur yang didukung di folder pilihan Anda (default yang disarankan `/storage/emulated/0/FifaStreet/`):

```
FifaStreet/
├── default.xex          ← folder hasil ekstrak disc
├── ...data game...
└── FIFA Street.iso      ← atau image disc XDVDFS (XGD2/XGD3/rebuilt)
```

* Paket **STFS/Games on Demand** (berkas tanpa ekstensi berawalan `LIVE`/`CON `) terdeteksi tetapi belum dapat dibaca — ubah ke ISO/folder dengan alat Anda sendiri.
* Aplikasi tidak mengunduh atau menyertakan file game, firmware, maupun kunci.

## 2. XEX terenkripsi

`default.xex` retail terenkripsi AES. Dua pilihan:

* **Impor kunci XEX** (16 byte biner atau 32 digit heksadesimal) dari konsol Anda sendiri melalui *Detail game → Impor kunci XEX*. Kunci disimpan di penyimpanan privat aplikasi dan dapat dihapus kapan saja. Kunci yang salah terdeteksi (hash blok/MZ tidak valid).
* atau gunakan XEX yang sudah Anda dekripsi sendiri.

## 3. Membaca status kompatibilitas

Halaman detail menampilkan setiap komponen dengan label **Berfungsi / Parsial / Belum tersedia / Perlu tindakan**, termasuk:
jumlah fungsi & instruksi, persentase instruksi yang dapat diterjemahkan, jumlah VMX/VMX128, impor kernel yang sudah diimplementasikan, dan apakah ada modul hasil recompile di APK.

## 4. Me-recompile XEX Anda (PC Linux/WSL)

```bash
./tools/regen_homebrew.sh build-host          # build fsr-analyze / fsr-recomp
./build-host/fsr-analyze default.xex --key xex.key           # ringkasan
./build-host/fsr-analyze default.xex --key xex.key --json    # JSON
./build-host/fsr-recomp  default.xex --key xex.key --out gen/fifastreet --name fifastreet
```

Untuk memasukkannya ke APK pribadi: salin `gen/fifastreet/*` ke `android/app/src/main/cpp/generated/fifastreet/`, tambahkan berkasnya di `android/app/src/main/cpp/CMakeLists.txt` dan `&fsr_module_fifastreet` ke `kModules` di `session.cpp`, lalu build. **Jangan publikasikan** kode hasil recompile game komersial.

Catatan jujur: untuk FIFA Street, langkah ini saat ini menghasilkan modul yang **belum dapat berjalan sampai selesai** karena VMX128, thread, GPU Xenos, dan audio belum diimplementasikan; runtime akan berhenti dengan `GuestFault` yang menyebut instruksi/impor yang belum ada — informasi itu berguna sebagai daftar pekerjaan berikutnya. Selain itu, sesi Play saat ini memanggil entry point `fsr_frame` milik homebrew; entry game retail (`mainCRTStartup`) membutuhkan thread & kernel yang lebih lengkap.

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
