// FIFA STREET Recompiled - runtime interface for statically recompiled code.
// Generated C++ (see fsr-recomp) includes this header. Guest memory is a
// flat 4 GiB reservation; guest data is big-endian, host is little-endian.
// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "fsr/vmx.h"

namespace fsr {

union FReg {
  double d;
  uint64_t u;
};

struct PPCContext {
  uint64_t r[32];
  FReg f[32];
  uint64_t lr;
  uint64_t ctr;
  uint8_t crb[32];  // one byte per CR bit (bit 0 = CR0.LT)
  uint8_t xer_so, xer_ov, xer_ca;
  uint32_t fpscr;
  uint64_t reserve_addr;  // ~0 = no reservation
  uint32_t reserve_val;
  uint64_t reserve_val64;
  uint32_t thread_id;
  void* runtime;  // owning Runtime*
  uint32_t vscr;   // VSCR (bit 0 = SAT, bit 16 = NJ)
  uint32_t vrsave;
  VR v[128];       // VMX (v0-v31) + VMX128 (v32-v127), big-endian byte order
};

using GuestFunc = void (*)(PPCContext& ctx, uint8_t* base);

struct FuncEntry {
  uint32_t addr;
  GuestFunc fn;
};

struct NamedAddr {
  const char* name;
  uint32_t addr;
};

// A recompiled module, as emitted by fsr-recomp (fsr_module_<name>.cpp).
struct RecompModule {
  const char* name;
  uint32_t image_base;
  uint32_t image_size;
  uint32_t entry_point;
  uint8_t image_sha1[20];  // SHA-1 of the mapped image the code was generated from
  const FuncEntry* funcs;  // sorted by addr
  uint32_t func_count;
  uint32_t unimplemented_sites;  // instructions emitted as fsr_unimplemented()
  const char* const* libs;       // import library names, indexed by fsr_kernel_call(lib)
  uint32_t lib_count;
  const NamedAddr* exports;      // optional named entry points (homebrew test harness)
  uint32_t export_count;
};

// ---- services implemented by the runtime (runtime.cc / kernel.cc) ----
void fsr_call_indirect(PPCContext& ctx, uint8_t* base, uint32_t addr);
void fsr_kernel_call(PPCContext& ctx, uint8_t* base, uint32_t lib, uint32_t ordinal);
void fsr_trap(PPCContext& ctx, uint8_t* base, uint32_t addr);
void fsr_syscall(PPCContext& ctx, uint8_t* base, uint32_t addr);
void fsr_unimplemented(PPCContext& ctx, uint8_t* base, uint32_t addr, uint32_t raw);
uint64_t fsr_timebase();

// ---- memory helpers used by generated code ----
inline uint8_t* gptr(uint8_t* base, uint32_t a) { return base + a; }
inline uint8_t ld8(uint8_t* b, uint32_t a) { return b[a]; }
inline uint16_t ld16(uint8_t* b, uint32_t a) { uint16_t v; std::memcpy(&v, b + a, 2); return __builtin_bswap16(v); }
inline uint32_t ld32(uint8_t* b, uint32_t a) { uint32_t v; std::memcpy(&v, b + a, 4); return __builtin_bswap32(v); }
inline uint64_t ld64(uint8_t* b, uint32_t a) { uint64_t v; std::memcpy(&v, b + a, 8); return __builtin_bswap64(v); }
inline void st8(uint8_t* b, uint32_t a, uint8_t v) { b[a] = v; }
inline void st16(uint8_t* b, uint32_t a, uint16_t v) { v = __builtin_bswap16(v); std::memcpy(b + a, &v, 2); }
inline void st32(uint8_t* b, uint32_t a, uint32_t v) { v = __builtin_bswap32(v); std::memcpy(b + a, &v, 4); }
inline void st64(uint8_t* b, uint32_t a, uint64_t v) { v = __builtin_bswap64(v); std::memcpy(b + a, &v, 8); }
inline uint16_t ld16le(uint8_t* b, uint32_t a) { uint16_t v; std::memcpy(&v, b + a, 2); return v; }
inline uint32_t ld32le(uint8_t* b, uint32_t a) { uint32_t v; std::memcpy(&v, b + a, 4); return v; }
inline void st16le(uint8_t* b, uint32_t a, uint16_t v) { std::memcpy(b + a, &v, 2); }
inline void st32le(uint8_t* b, uint32_t a, uint32_t v) { std::memcpy(b + a, &v, 4); }

inline float f32_from_bits(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
inline uint32_t f32_bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

// ---- integer helpers ----
inline uint32_t rotl32(uint32_t x, uint32_t n) { n &= 31; return n ? (x << n) | (x >> (32 - n)) : x; }
inline uint64_t rotl64(uint64_t x, uint32_t n) { n &= 63; return n ? (x << n) | (x >> (64 - n)) : x; }
inline uint64_t dup32(uint32_t x) { return uint64_t(x) << 32 | x; }

inline void set_cr_signed(PPCContext& c, int field, int64_t a, int64_t b) {
  uint8_t* f = c.crb + 4 * field;
  f[0] = a < b; f[1] = a > b; f[2] = a == b; f[3] = c.xer_so;
}
inline void set_cr_unsigned(PPCContext& c, int field, uint64_t a, uint64_t b) {
  uint8_t* f = c.crb + 4 * field;
  f[0] = a < b; f[1] = a > b; f[2] = a == b; f[3] = c.xer_so;
}
inline void set_cr0(PPCContext& c, uint64_t v) { set_cr_signed(c, 0, int64_t(v), 0); }
inline void set_cr_fp(PPCContext& c, int field, double a, double b) {
  uint8_t* f = c.crb + 4 * field;
  bool un = std::isnan(a) || std::isnan(b);
  f[0] = !un && a < b; f[1] = !un && a > b; f[2] = !un && a == b; f[3] = un;
}
inline uint32_t get_cr(const PPCContext& c) {
  uint32_t v = 0;
  for (int i = 0; i < 32; i++) v |= uint32_t(c.crb[i] & 1) << (31 - i);
  return v;
}
inline void set_cr_field_bits(PPCContext& c, uint32_t value, uint32_t crm) {
  for (int fld = 0; fld < 8; fld++)
    if (crm & (0x80u >> fld))
      for (int b = 0; b < 4; b++) c.crb[4 * fld + b] = (value >> (31 - (4 * fld + b))) & 1;
}
inline uint32_t get_xer(const PPCContext& c) {
  return uint32_t(c.xer_so) << 31 | uint32_t(c.xer_ov) << 30 | uint32_t(c.xer_ca) << 29;
}
inline void set_xer(PPCContext& c, uint64_t v) {
  c.xer_so = (v >> 31) & 1; c.xer_ov = (v >> 30) & 1; c.xer_ca = (v >> 29) & 1;
}

inline uint64_t divw_s(uint64_t a, uint64_t b) {
  int32_t x = int32_t(a), y = int32_t(b);
  if (y == 0 || (x == INT32_MIN && y == -1)) return 0;
  return uint32_t(x / y);
}
inline uint64_t divw_u(uint64_t a, uint64_t b) {
  uint32_t x = uint32_t(a), y = uint32_t(b);
  return y ? x / y : 0;
}
inline uint64_t divd_s(uint64_t a, uint64_t b) {
  int64_t x = int64_t(a), y = int64_t(b);
  if (y == 0 || (x == INT64_MIN && y == -1)) return 0;
  return uint64_t(x / y);
}
inline uint64_t divd_u(uint64_t a, uint64_t b) { return b ? a / b : 0; }

// Shifts (PowerPC semantics, 64-bit mode).
inline uint64_t ppc_slw(uint64_t s, uint64_t b) { uint32_t n = b & 63; return n > 31 ? 0 : uint32_t(uint32_t(s) << n); }
inline uint64_t ppc_srw(uint64_t s, uint64_t b) { uint32_t n = b & 63; return n > 31 ? 0 : uint32_t(s) >> n; }
inline uint64_t ppc_sld(uint64_t s, uint64_t b) { uint32_t n = b & 127; return n > 63 ? 0 : s << n; }
inline uint64_t ppc_srd(uint64_t s, uint64_t b) { uint32_t n = b & 127; return n > 63 ? 0 : s >> n; }
inline uint64_t ppc_sraw(PPCContext& c, uint64_t s, uint32_t n) {
  int32_t v = int32_t(s);
  if (n > 31) { c.xer_ca = v < 0; return uint64_t(int64_t(v < 0 ? -1 : 0)); }
  c.xer_ca = v < 0 && n && (uint32_t(v) & ((1u << n) - 1)) != 0;
  return uint64_t(int64_t(v >> n));
}
inline uint64_t ppc_srad(PPCContext& c, uint64_t s, uint32_t n) {
  int64_t v = int64_t(s);
  if (n > 63) { c.xer_ca = v < 0; return uint64_t(v < 0 ? -1 : 0); }
  c.xer_ca = v < 0 && n && (uint64_t(v) & ((uint64_t(1) << n) - 1)) != 0;
  return uint64_t(v >> n);
}

// Carrying adds: returns a+b+cin, sets CA.
inline uint64_t add_carry(PPCContext& c, uint64_t a, uint64_t b, uint64_t cin) {
  unsigned __int128 s = (unsigned __int128)a + b + cin;
  c.xer_ca = uint8_t(s >> 64);
  return uint64_t(s);
}
inline void set_ov_add(PPCContext& c, uint64_t a, uint64_t b, uint64_t r) {
  c.xer_ov = ((~(a ^ b) & (a ^ r)) >> 63) & 1;
  c.xer_so |= c.xer_ov;
}

inline uint64_t cntlz32(uint64_t v) { uint32_t x = uint32_t(v); return x ? __builtin_clz(x) : 32; }
inline uint64_t cntlz64(uint64_t v) { return v ? __builtin_clzll(v) : 64; }

// Floating point helpers.
inline double round_single(double v) { return double(float(v)); }
inline uint64_t fctiw_bits(double v, bool truncate) {
  double r = truncate ? std::trunc(v) : std::nearbyint(v);
  int32_t i;
  if (std::isnan(v)) i = INT32_MIN;
  else if (r >= 2147483647.0) i = INT32_MAX;
  else if (r <= -2147483648.0) i = INT32_MIN;
  else i = int32_t(r);
  return 0xFFF8000000000000ull | uint32_t(i);
}
inline uint64_t fctid_bits(double v, bool truncate) {
  double r = truncate ? std::trunc(v) : std::nearbyint(v);
  if (std::isnan(v)) return uint64_t(INT64_MIN);
  if (r >= 9223372036854775807.0) return uint64_t(INT64_MAX);
  if (r <= -9223372036854775808.0) return uint64_t(INT64_MIN);
  return uint64_t(int64_t(r));
}

// Cache-line zeroing (dcbz = 32 bytes, Xenon dcbz128 = 128 bytes).
inline void dcbz(uint8_t* b, uint32_t a, uint32_t line) { std::memset(b + (a & ~(line - 1)), 0, line); }

// Reservation (lwarx/stwcx.) implemented with host atomics.
inline uint32_t lwarx(PPCContext& c, uint8_t* b, uint32_t a) {
  uint32_t raw = __atomic_load_n(reinterpret_cast<uint32_t*>(b + (a & ~3u)), __ATOMIC_ACQUIRE);
  c.reserve_addr = a; c.reserve_val = raw;
  return __builtin_bswap32(raw);
}
inline bool stwcx(PPCContext& c, uint8_t* b, uint32_t a, uint32_t v) {
  if (c.reserve_addr != a) { c.reserve_addr = ~0ull; return false; }
  uint32_t expect = c.reserve_val;
  uint32_t nv = __builtin_bswap32(v);
  c.reserve_addr = ~0ull;
  return __atomic_compare_exchange_n(reinterpret_cast<uint32_t*>(b + (a & ~3u)), &expect, nv, false,
                                     __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
inline uint64_t ldarx(PPCContext& c, uint8_t* b, uint32_t a) {
  uint64_t raw = __atomic_load_n(reinterpret_cast<uint64_t*>(b + (a & ~7u)), __ATOMIC_ACQUIRE);
  c.reserve_addr = a; c.reserve_val64 = raw;
  return __builtin_bswap64(raw);
}
inline bool stdcx(PPCContext& c, uint8_t* b, uint32_t a, uint64_t v) {
  if (c.reserve_addr != a) { c.reserve_addr = ~0ull; return false; }
  uint64_t expect = c.reserve_val64;
  c.reserve_addr = ~0ull;
  return __atomic_compare_exchange_n(reinterpret_cast<uint64_t*>(b + (a & ~7u)), &expect, __builtin_bswap64(v),
                                     false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

}  // namespace fsr

// Generated function signature.
#define FSR_FUNC(name) void name(fsr::PPCContext& ctx, uint8_t* base)
