# Komponen pihak ketiga

| Komponen | Lokasi | Lisensi | Kegunaan |
|---|---|---|---|
| libmspack (LZX decoder), versi modifikasi proyek Xenia | `core/third_party/mspack/` | LGPL-2.1 (`COPYING.LIB`) | Dekompresi XEX "normal" (LZX). Disertakan sebagai kode sumber lengkap; Anda bebas mengganti/me-relink versi lain. |
| Tabel nama ekspor kernel `xboxkrnl_table.inc`, `xam_table.inc` dari Xenia | `core/third_party/xenia/` | BSD-3-Clause (`LICENSE`) | Hanya pemetaan ordinal → nama fungsi untuk laporan & HLE. Tidak ada kode emulator Xenia yang dipakai. |

Pengetahuan format XEX2 serta tata letak opcode/field AltiVec & VMX128 dicocokkan dengan dokumentasi/struktur di Xenia (`xex2_info.h`, `ppc_opcode_lookup_gen.cc`, `ppc_decode_data.h`, `ppc_emit_altivec.cc`; BSD-3-Clause) dan README XenonRecomp; implementasi loader, AES, SHA-1, decoder PowerPC, recompiler, dan runtime ditulis ulang untuk proyek ini (MIT).
