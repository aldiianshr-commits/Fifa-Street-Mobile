# Arsitektur

```
        PC / CI (host)                                   APK (Android ARM64)
 ┌───────────────────────────┐                 ┌────────────────────────────────────────┐
 │ default.xex (milik user)  │                 │ Kotlin UI: library, info, kontrol,     │
 │   │                       │                 │   pengaturan, log                       │
 │   ▼ XexModule (core)      │                 │        │ JNI (NativeBridge)             │
 │ dekripsi AES (kunci user) │                 │        ▼                                │
 │ dekompresi basic/LZX      │                 │ libfsr.so                               │
 │ PE + .pdata + impor       │                 │  ├─ XexModule: load image dari XEX user │
 │   │                       │                 │  ├─ Runtime: memori guest 4 GiB,        │
 │   ▼ fsr-recomp            │   sumber C++    │  │   dispatch tidak langsung, HLE kernel│
 │ penemuan fungsi           │ ──────────────▶ │  ├─ modul hasil recompile (C++→ARM64)   │
 │ PowerPC → C++ per fungsi  │  (dibangun NDK) │  ├─ Session: loop frame, pacing, termal │
 └───────────────────────────┘                 │  ├─ VkRenderer: upload framebuffer,     │
                                               │  │   blit ke swapchain                  │
                                               │  └─ AAudio (uji jalur audio)            │
                                               └────────────────────────────────────────┘
```

## core/ (C++17, dipakai host & Android)

| Berkas | Isi |
|---|---|
| `xex.h/.cc` | Parser XEX2 (header opsional, security info, execution info, import libraries, resource, static lib, TLS), dekripsi AES-128-CBC dengan **session key yang dibuka memakai kunci dari pengguna**, dekompresi basic dan LZX (rantai blok + verifikasi SHA-1 per blok), parser PE (mesin `0x1F2` PowerPC BE), `.pdata`, resolusi rekaman impor. |
| `crypto.cc` | AES-128 (S-box dibangkitkan, diverifikasi vektor FIPS-197) dan SHA-1. |
| `lzx_glue.cc` + `third_party/mspack` | Dekoder LZX libmspack dengan sistem I/O memori. |
| `iso.h/.cc` | Pembaca XDVDFS (offset partisi rebuilt/XGD2/XGD3/XGD1), daftar berkas, ekstraksi; deteksi kontainer STFS (LIVE/PIRS/CON). |
| `ppc.h`, `ppc_decode.cc` | Decoder instruksi PowerPC Xenon (±200 opcode), helper field, disassembler ringkas. Instruksi AltiVec/VMX128 dikenali sebagai kelas `vmx`. |
| `ppc_vmx.h/.cc`, `vmx.h` | Decoder AltiVec + VMX128 (tata letak field mengikuti tabel decoder Xenia) dan semantik vektor per elemen dalam urutan byte big-endian (saturasi → VSCR[SAT], bentuk record → CR6). VMX128 memetakan register v0–v127 dan instruksi khas Xenon (`vmsum3fp128`, `vmsum4fp128`, `vrlimi128`, `vpermwi128`, `vmaddcfp128`, unaligned `lvlx/lvrx/stvlx/stvrx`, …). |
| `recomp.h/.cc` | Penemuan fungsi (seed dari `.pdata`, entry point, ekspor, target `bl`, tail call; batas fungsi non-pdata dipindai sampai terminator) dan generator C++. |
| `runtime.h` | ABI kode hasil generate: `PPCContext` (GPR 64-bit, FPR, CR per bit, XER, LR, CTR, reservation), helper memori big-endian, shift/rotate/carry PowerPC, konversi FP. |
| `runtime.cc`, `host.h` | `Runtime`: reservasi 4 GiB (`mmap` NORESERVE), pemetaan image, slot IAT → thunk, heap guest, stack per thread host, `fsr_call_indirect` (pencarian biner tabel fungsi), trap/syscall/instruksi yang belum diterjemahkan → `GuestFault`. |
| `kernel.cc` | HLE: `DbgPrint`, `KeQueryPerformanceFrequency`, `KeQuerySystemTime`, `KeGetCurrentProcessType`, `NtAllocateVirtualMemory`, `NtFreeVirtualMemory`, `RtlInitAnsiString`, `RtlCompareMemory`, `RtlInitializeCriticalSection`/`Enter`/`Leave`, `KeTlsAlloc`/`Free`/`GetValue`/`SetValue`, `XamInputGetState`, `XamInputGetCapabilities`, `XGetLanguage`. Impor lain → `STATUS_NOT_IMPLEMENTED` + log sekali. |
| `report.cc` | Laporan analisis (teks/JSON) untuk CLI dan aplikasi. |
| `tools/` | `fsr-analyze`, `fsr-recomp`, `mkxex` (ELF homebrew → XEX uji), `mkiso` (fixture XDVDFS). |

### Model kode hasil recompile

Setiap fungsi PowerPC menjadi `void sub_XXXXXXXX(PPCContext& ctx, uint8_t* base)`:

* Semantik 64-bit mode (CR0 dari hasil 64-bit, carry 64-bit), alamat efektif dipotong ke 32 bit (`base + (uint32_t)EA`), data big-endian di-*byteswap* saat load/store.
* Branch lokal → `goto`; `bl` → panggilan C++ langsung (atau `fsr_call_indirect` bila target tidak diketahui); `b` keluar fungsi → *tail call*; `blr` → `return`; `bctr` di dalam fungsi → `switch` atas semua alamat instruksi fungsi (menangani jump table tanpa analisis khusus); `bctrl`/`blrl` → panggilan tidak langsung lewat tabel.
* Thunk impor kernel → `fsr_kernel_call(lib, ordinal)` → HLE.
* `lwarx/stwcx.` & `ldarx/stdcx.` → CAS atomik host; `sync` → fence; `dcbz`/`dcbz128` → memset 32/128 byte.
* FPU: operasi single precision dihitung double lalu dibulatkan ke float (aman untuk +,−,×,÷,√ dari operand float); FMA memakai `std::fma`; `fctiw[z]`, `fctid[z]`, `fcfid`, `fsel`, `fres`, `frsqrte`, perbandingan FP ke CR.
* Modul diberi namespace sendiri dan deskriptor `fsr_module_<nama>` berisi SHA-1 image; runtime menolak XEX yang tidak cocok persis.

## android/

* **Kotlin (tanpa AndroidX):** `MainActivity` (library & pemindaian), `GameActivity` (info, status kompatibilitas, pengaturan per game, impor kunci), `PlayActivity` (SurfaceView + kontrol sentuh + gamepad + menu jeda + overlay statistik + listener termal), `ControlsActivity` (editor layout, remap, profil), `SettingsActivity` (grafis global, info perangkat/GPU, uji runtime, uji audio), `LogActivity`.
* **Native:** `session.cpp` (thread render: panggil `fsr_frame` guest → present; pacing berbasis deadline; turun ke 30 FPS saat status termal ≥ SEVERE), `renderer_vk.cpp` (instance/device/swapchain, image guest RGBA8 pada resolusi internal × render scale, `vkCmdCopyBufferToImage` + `vkCmdBlitImage` dengan filter nearest/linear dan letterbox 16:9, FIFO/MAILBOX/IMMEDIATE), `audio.cpp` (AAudio), `jni_bridge.cpp`.
* Penyimpanan konfigurasi: `SharedPreferences` (JSON) – grafis global, grafis per game, profil kontrol, profil per game, folder game. Kunci XEX pengguna disimpan di penyimpanan privat aplikasi.

## Mengapa tidak ada "fallback" emulator

Runtime hanya dapat menjalankan kode yang sudah diterjemahkan ke C++ dan dikompilasi ke ARM64. Instruksi yang belum didukung menghasilkan `GuestFault` yang dilaporkan, bukan diinterpretasi diam-diam. Game tanpa modul hasil recompile tidak bisa di-*Play* (tombol nonaktif) dan aplikasi menjelaskan alasannya.
