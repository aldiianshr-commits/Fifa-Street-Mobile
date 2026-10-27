// FIFA STREET Recompiled - AltiVec/VMX and Xenon VMX128 semantics used by
// generated code. Vector registers are kept in guest (big-endian) byte
// order: b[0] is the most significant byte of element 0. Floating point
// follows IEEE ("Java mode"); denormal flushing (VSCR[NJ]) is not emulated.
// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace fsr {

struct alignas(16) VR {
  uint8_t b[16];
};

namespace vx {

constexpr uint32_t kSat = 1;  // VSCR[SAT]

inline uint16_t h(const VR& v, int i) { return uint16_t(v.b[2 * i] << 8 | v.b[2 * i + 1]); }
inline uint32_t w(const VR& v, int i) {
  return uint32_t(v.b[4 * i]) << 24 | uint32_t(v.b[4 * i + 1]) << 16 | uint32_t(v.b[4 * i + 2]) << 8 | v.b[4 * i + 3];
}
inline float f(const VR& v, int i) { uint32_t u = w(v, i); float x; std::memcpy(&x, &u, 4); return x; }
inline void sh(VR& v, int i, uint16_t x) { v.b[2 * i] = uint8_t(x >> 8); v.b[2 * i + 1] = uint8_t(x); }
inline void sw(VR& v, int i, uint32_t x) {
  v.b[4 * i] = uint8_t(x >> 24); v.b[4 * i + 1] = uint8_t(x >> 16); v.b[4 * i + 2] = uint8_t(x >> 8); v.b[4 * i + 3] = uint8_t(x);
}
inline void sf(VR& v, int i, float x) { uint32_t u; std::memcpy(&u, &x, 4); sw(v, i, u); }

// ---------------------------------------------------------------- memory
inline void lvx(VR& d, uint8_t* base, uint32_t ea) { std::memcpy(d.b, base + (ea & ~15u), 16); }
inline void stvx(const VR& s, uint8_t* base, uint32_t ea) { std::memcpy(base + (ea & ~15u), s.b, 16); }
inline void lvebx(VR& d, uint8_t* base, uint32_t ea) { d.b[ea & 15] = base[ea]; }
inline void lvehx(VR& d, uint8_t* base, uint32_t ea) { ea &= ~1u; std::memcpy(d.b + (ea & 15), base + ea, 2); }
inline void lvewx(VR& d, uint8_t* base, uint32_t ea) { ea &= ~3u; std::memcpy(d.b + (ea & 15), base + ea, 4); }
inline void stvebx(const VR& s, uint8_t* base, uint32_t ea) { base[ea] = s.b[ea & 15]; }
inline void stvehx(const VR& s, uint8_t* base, uint32_t ea) { ea &= ~1u; std::memcpy(base + ea, s.b + (ea & 15), 2); }
inline void stvewx(const VR& s, uint8_t* base, uint32_t ea) { ea &= ~3u; std::memcpy(base + ea, s.b + (ea & 15), 4); }
inline void lvsl(VR& d, uint32_t ea) { for (int i = 0; i < 16; i++) d.b[i] = uint8_t((ea & 15) + i); }
inline void lvsr(VR& d, uint32_t ea) { for (int i = 0; i < 16; i++) d.b[i] = uint8_t(16 - (ea & 15) + i); }
// Xenon/Cell unaligned load/store halves.
inline void lvlx(VR& d, uint8_t* base, uint32_t ea) {
  uint32_t o = ea & 15;
  VR t{};
  std::memcpy(t.b, base + ea, 16 - o);
  d = t;
}
inline void lvrx(VR& d, uint8_t* base, uint32_t ea) {
  uint32_t o = ea & 15;
  VR t{};
  if (o) std::memcpy(t.b + (16 - o), base + (ea & ~15u), o);
  d = t;
}
inline void stvlx(const VR& s, uint8_t* base, uint32_t ea) { std::memcpy(base + ea, s.b, 16 - (ea & 15)); }
inline void stvrx(const VR& s, uint8_t* base, uint32_t ea) {
  uint32_t o = ea & 15;
  if (o) std::memcpy(base + (ea & ~15u), s.b + (16 - o), o);
}

// ---------------------------------------------------------------- helpers
template <typename T> inline T clamp_t(int64_t v, int64_t lo, int64_t hi, uint32_t& vscr) {
  if (v < lo) { vscr |= kSat; return T(lo); }
  if (v > hi) { vscr |= kSat; return T(hi); }
  return T(v);
}

#define FSR_VX_B(name, expr) \
  inline void name(VR& d, const VR& a, const VR& b) { VR t; for (int i = 0; i < 16; i++) { uint8_t x = a.b[i], y = b.b[i]; (void)x; (void)y; t.b[i] = uint8_t(expr); } d = t; }
#define FSR_VX_H(name, expr) \
  inline void name(VR& d, const VR& a, const VR& b) { VR t; for (int i = 0; i < 8; i++) { uint16_t x = h(a, i), y = h(b, i); (void)x; (void)y; sh(t, i, uint16_t(expr)); } d = t; }
#define FSR_VX_W(name, expr) \
  inline void name(VR& d, const VR& a, const VR& b) { VR t; for (int i = 0; i < 4; i++) { uint32_t x = w(a, i), y = w(b, i); (void)x; (void)y; sw(t, i, uint32_t(expr)); } d = t; }
#define FSR_VX_F(name, expr) \
  inline void name(VR& d, const VR& a, const VR& b) { VR t; for (int i = 0; i < 4; i++) { float x = f(a, i), y = f(b, i); (void)x; (void)y; sf(t, i, float(expr)); } d = t; }
#define FSR_VX_BS(name, T, expr, lo, hi) \
  inline void name(VR& d, const VR& a, const VR& b, uint32_t& vscr) { VR t; for (int i = 0; i < 16; i++) { int64_t x = T(a.b[i]), y = T(b.b[i]); t.b[i] = uint8_t(clamp_t<T>(expr, lo, hi, vscr)); } d = t; }
#define FSR_VX_HS(name, T, expr, lo, hi) \
  inline void name(VR& d, const VR& a, const VR& b, uint32_t& vscr) { VR t; for (int i = 0; i < 8; i++) { int64_t x = T(h(a, i)), y = T(h(b, i)); sh(t, i, uint16_t(clamp_t<T>(expr, lo, hi, vscr))); } d = t; }
#define FSR_VX_WS(name, T, expr, lo, hi) \
  inline void name(VR& d, const VR& a, const VR& b, uint32_t& vscr) { VR t; for (int i = 0; i < 4; i++) { int64_t x = T(w(a, i)), y = T(w(b, i)); sw(t, i, uint32_t(clamp_t<T>(expr, lo, hi, vscr))); } d = t; }

// modulo add/sub
FSR_VX_B(vaddubm, x + y) FSR_VX_H(vadduhm, x + y) FSR_VX_W(vadduwm, x + y)
FSR_VX_B(vsububm, x - y) FSR_VX_H(vsubuhm, x - y) FSR_VX_W(vsubuwm, x - y)
FSR_VX_W(vaddcuw, (uint64_t(x) + y) >> 32) FSR_VX_W(vsubcuw, x >= y ? 1 : 0)
// saturating add/sub
FSR_VX_BS(vaddubs, uint8_t, x + y, 0, 255) FSR_VX_HS(vadduhs, uint16_t, x + y, 0, 65535)
FSR_VX_WS(vadduws, uint32_t, x + y, 0, 4294967295ll)
FSR_VX_BS(vaddsbs, int8_t, x + y, -128, 127) FSR_VX_HS(vaddshs, int16_t, x + y, -32768, 32767)
FSR_VX_WS(vaddsws, int32_t, x + y, INT32_MIN, INT32_MAX)
FSR_VX_BS(vsububs, uint8_t, x - y, 0, 255) FSR_VX_HS(vsubuhs, uint16_t, x - y, 0, 65535)
FSR_VX_WS(vsubuws, uint32_t, x - y, 0, 4294967295ll)
FSR_VX_BS(vsubsbs, int8_t, x - y, -128, 127) FSR_VX_HS(vsubshs, int16_t, x - y, -32768, 32767)
FSR_VX_WS(vsubsws, int32_t, x - y, INT32_MIN, INT32_MAX)
// min/max/avg
FSR_VX_B(vmaxub, x > y ? x : y) FSR_VX_H(vmaxuh, x > y ? x : y) FSR_VX_W(vmaxuw, x > y ? x : y)
FSR_VX_B(vminub, x < y ? x : y) FSR_VX_H(vminuh, x < y ? x : y) FSR_VX_W(vminuw, x < y ? x : y)
FSR_VX_B(vmaxsb, int8_t(x) > int8_t(y) ? x : y) FSR_VX_H(vmaxsh, int16_t(x) > int16_t(y) ? x : y)
FSR_VX_W(vmaxsw, int32_t(x) > int32_t(y) ? x : y)
FSR_VX_B(vminsb, int8_t(x) < int8_t(y) ? x : y) FSR_VX_H(vminsh, int16_t(x) < int16_t(y) ? x : y)
FSR_VX_W(vminsw, int32_t(x) < int32_t(y) ? x : y)
FSR_VX_B(vavgub, (uint32_t(x) + y + 1) >> 1) FSR_VX_H(vavguh, (uint32_t(x) + y + 1) >> 1)
FSR_VX_W(vavguw, (uint64_t(x) + y + 1) >> 1)
FSR_VX_B(vavgsb, (int32_t(int8_t(x)) + int8_t(y) + 1) >> 1) FSR_VX_H(vavgsh, (int32_t(int16_t(x)) + int16_t(y) + 1) >> 1)
FSR_VX_W(vavgsw, (int64_t(int32_t(x)) + int32_t(y) + 1) >> 1)
// logical
FSR_VX_B(vand, x & y) FSR_VX_B(vandc, x & ~y) FSR_VX_B(vor, x | y) FSR_VX_B(vxor, x ^ y) FSR_VX_B(vnor, ~(x | y))
// shifts / rotates (element-wise amount from b)
FSR_VX_B(vslb, x << (y & 7)) FSR_VX_H(vslh, x << (y & 15)) FSR_VX_W(vslw, x << (y & 31))
FSR_VX_B(vsrb, x >> (y & 7)) FSR_VX_H(vsrh, x >> (y & 15)) FSR_VX_W(vsrw, x >> (y & 31))
FSR_VX_B(vsrab, int8_t(x) >> (y & 7)) FSR_VX_H(vsrah, int16_t(x) >> (y & 15)) FSR_VX_W(vsraw, int32_t(x) >> (y & 31))
FSR_VX_B(vrlb, (x << (y & 7)) | (x >> ((8 - (y & 7)) & 7)))
FSR_VX_H(vrlh, (x << (y & 15)) | (x >> ((16 - (y & 15)) & 15)))
FSR_VX_W(vrlw, (y & 31) ? (x << (y & 31)) | (x >> (32 - (y & 31))) : x)
// compares (element mask)
FSR_VX_B(vcmpequb_e, x == y ? 0xFF : 0) FSR_VX_H(vcmpequh_e, x == y ? 0xFFFF : 0) FSR_VX_W(vcmpequw_e, x == y ? 0xFFFFFFFFu : 0)
FSR_VX_B(vcmpgtub_e, x > y ? 0xFF : 0) FSR_VX_H(vcmpgtuh_e, x > y ? 0xFFFF : 0) FSR_VX_W(vcmpgtuw_e, x > y ? 0xFFFFFFFFu : 0)
FSR_VX_B(vcmpgtsb_e, int8_t(x) > int8_t(y) ? 0xFF : 0) FSR_VX_H(vcmpgtsh_e, int16_t(x) > int16_t(y) ? 0xFFFF : 0)
FSR_VX_W(vcmpgtsw_e, int32_t(x) > int32_t(y) ? 0xFFFFFFFFu : 0)
inline void vcmpeqfp_e(VR& d, const VR& a, const VR& b) { VR t; for (int i = 0; i < 4; i++) sw(t, i, f(a, i) == f(b, i) ? 0xFFFFFFFFu : 0); d = t; }
inline void vcmpgefp_e(VR& d, const VR& a, const VR& b) { VR t; for (int i = 0; i < 4; i++) sw(t, i, f(a, i) >= f(b, i) ? 0xFFFFFFFFu : 0); d = t; }
inline void vcmpgtfp_e(VR& d, const VR& a, const VR& b) { VR t; for (int i = 0; i < 4; i++) sw(t, i, f(a, i) > f(b, i) ? 0xFFFFFFFFu : 0); d = t; }
inline void vcmpbfp_e(VR& d, const VR& a, const VR& b) {
  VR t;
  for (int i = 0; i < 4; i++) {
    float x = f(a, i), y = f(b, i);
    bool nan = std::isnan(x) || std::isnan(y);
    uint32_t r = 0;
    if (nan || !(x <= y)) r |= 0x80000000u;
    if (nan || !(x >= -y)) r |= 0x40000000u;
    sw(t, i, r);
  }
  d = t;
}
// CR6 update for the record forms: bit0 = all elements true, bit2 = all false.
inline void cr6_from(uint8_t* crb, const VR& d) {
  bool all = true, none = true;
  for (int i = 0; i < 16; i++) { if (d.b[i] != 0xFF) all = false; if (d.b[i] != 0) none = false; }
  crb[24] = all; crb[25] = 0; crb[26] = none; crb[27] = 0;
}
inline void cr6_bounds(uint8_t* crb, const VR& d) {
  bool none = true;
  for (int i = 0; i < 16; i++) if (d.b[i]) none = false;
  crb[24] = 0; crb[25] = 0; crb[26] = none; crb[27] = 0;
}

// float arithmetic
FSR_VX_F(vaddfp, x + y) FSR_VX_F(vsubfp, x - y) FSR_VX_F(vmulfp, x * y)
FSR_VX_F(vmaxfp, std::isnan(x) || std::isnan(y) ? NAN : (x > y ? x : y))
FSR_VX_F(vminfp, std::isnan(x) || std::isnan(y) ? NAN : (x < y ? x : y))
inline void vmaddfp(VR& d, const VR& a, const VR& c, const VR& b) {  // a*c + b (standard operand order)
  VR t;
  for (int i = 0; i < 4; i++) sf(t, i, std::fma(f(a, i), f(c, i), f(b, i)));
  d = t;
}
inline void vnmsubfp(VR& d, const VR& a, const VR& c, const VR& b) {  // -(a*c - b)
  VR t;
  for (int i = 0; i < 4; i++) sf(t, i, -std::fma(f(a, i), f(c, i), -f(b, i)));
  d = t;
}
#define FSR_VX_F1(name, expr) \
  inline void name(VR& d, const VR& b) { VR t; for (int i = 0; i < 4; i++) { float x = f(b, i); sf(t, i, float(expr)); } d = t; }
FSR_VX_F1(vrefp, 1.0f / x) FSR_VX_F1(vrsqrtefp, 1.0f / std::sqrt(x)) FSR_VX_F1(vexptefp, std::exp2(x))
FSR_VX_F1(vlogefp, std::log2(x)) FSR_VX_F1(vrfin, std::nearbyint(x)) FSR_VX_F1(vrfiz, std::trunc(x))
FSR_VX_F1(vrfip, std::ceil(x)) FSR_VX_F1(vrfim, std::floor(x))
inline void vcfux(VR& d, const VR& b, uint32_t uim) { VR t; for (int i = 0; i < 4; i++) sf(t, i, std::ldexp(float(w(b, i)), -int(uim))); d = t; }
inline void vcfsx(VR& d, const VR& b, uint32_t uim) { VR t; for (int i = 0; i < 4; i++) sf(t, i, std::ldexp(float(int32_t(w(b, i))), -int(uim))); d = t; }
inline void vctsxs(VR& d, const VR& b, uint32_t uim, uint32_t& vscr) {
  VR t;
  for (int i = 0; i < 4; i++) {
    double v = std::ldexp(double(f(b, i)), int(uim));
    int32_t r;
    if (std::isnan(v)) { r = 0; vscr |= kSat; }
    else if (v >= 2147483647.0) { r = INT32_MAX; vscr |= kSat; }
    else if (v <= -2147483648.0) { r = INT32_MIN; if (v < -2147483648.0) vscr |= kSat; }
    else r = int32_t(v);
    sw(t, i, uint32_t(r));
  }
  d = t;
}
inline void vctuxs(VR& d, const VR& b, uint32_t uim, uint32_t& vscr) {
  VR t;
  for (int i = 0; i < 4; i++) {
    double v = std::ldexp(double(f(b, i)), int(uim));
    uint32_t r;
    if (std::isnan(v)) { r = 0; vscr |= kSat; }
    else if (v >= 4294967295.0) { r = 0xFFFFFFFFu; if (v > 4294967295.0) vscr |= kSat; }
    else if (v < 0) { r = 0; if (v <= -1.0) vscr |= kSat; }
    else r = uint32_t(v);
    sw(t, i, r);
  }
  d = t;
}

// permutes / splats / merges
inline void vperm(VR& d, const VR& a, const VR& b, const VR& c) {
  VR t;
  for (int i = 0; i < 16; i++) { uint32_t k = c.b[i] & 31; t.b[i] = k < 16 ? a.b[k] : b.b[k - 16]; }
  d = t;
}
inline void vsel(VR& d, const VR& a, const VR& b, const VR& c) {
  VR t;
  for (int i = 0; i < 16; i++) t.b[i] = uint8_t((a.b[i] & ~c.b[i]) | (b.b[i] & c.b[i]));
  d = t;
}
inline void vsldoi(VR& d, const VR& a, const VR& b, uint32_t shb) {
  VR t;
  for (int i = 0; i < 16; i++) { uint32_t k = i + (shb & 15); t.b[i] = k < 16 ? a.b[k] : b.b[k - 16]; }
  d = t;
}
inline void vspltb(VR& d, const VR& b, uint32_t u) { uint8_t x = b.b[u & 15]; VR t; std::memset(t.b, x, 16); d = t; }
inline void vsplth(VR& d, const VR& b, uint32_t u) { uint16_t x = h(b, u & 7); VR t; for (int i = 0; i < 8; i++) sh(t, i, x); d = t; }
inline void vspltw(VR& d, const VR& b, uint32_t u) { uint32_t x = w(b, u & 3); VR t; for (int i = 0; i < 4; i++) sw(t, i, x); d = t; }
inline int32_t simm5(uint32_t u) { return (u & 0x10) ? int32_t(u | 0xFFFFFFE0u) : int32_t(u); }
inline void vspltisb(VR& d, uint32_t u) { std::memset(d.b, uint8_t(simm5(u)), 16); }
inline void vspltish(VR& d, uint32_t u) { for (int i = 0; i < 8; i++) sh(d, i, uint16_t(simm5(u))); }
inline void vspltisw(VR& d, uint32_t u) { for (int i = 0; i < 4; i++) sw(d, i, uint32_t(simm5(u))); }
inline void vmrghb(VR& d, const VR& a, const VR& b) { VR t; for (int i = 0; i < 8; i++) { t.b[2 * i] = a.b[i]; t.b[2 * i + 1] = b.b[i]; } d = t; }
inline void vmrglb(VR& d, const VR& a, const VR& b) { VR t; for (int i = 0; i < 8; i++) { t.b[2 * i] = a.b[8 + i]; t.b[2 * i + 1] = b.b[8 + i]; } d = t; }
inline void vmrghh(VR& d, const VR& a, const VR& b) { VR t; for (int i = 0; i < 4; i++) { sh(t, 2 * i, h(a, i)); sh(t, 2 * i + 1, h(b, i)); } d = t; }
inline void vmrglh(VR& d, const VR& a, const VR& b) { VR t; for (int i = 0; i < 4; i++) { sh(t, 2 * i, h(a, 4 + i)); sh(t, 2 * i + 1, h(b, 4 + i)); } d = t; }
inline void vmrghw(VR& d, const VR& a, const VR& b) { VR t; sw(t, 0, w(a, 0)); sw(t, 1, w(b, 0)); sw(t, 2, w(a, 1)); sw(t, 3, w(b, 1)); d = t; }
inline void vmrglw(VR& d, const VR& a, const VR& b) { VR t; sw(t, 0, w(a, 2)); sw(t, 1, w(b, 2)); sw(t, 2, w(a, 3)); sw(t, 3, w(b, 3)); d = t; }
// whole-register shifts
inline void vslo(VR& d, const VR& a, const VR& b) {
  uint32_t s = (b.b[15] >> 3) & 15;
  VR t{};
  for (int i = 0; i + s < 16; i++) t.b[i] = a.b[i + s];
  d = t;
}
inline void vsro(VR& d, const VR& a, const VR& b) {
  uint32_t s = (b.b[15] >> 3) & 15;
  VR t{};
  for (int i = s; i < 16; i++) t.b[i] = a.b[i - s];
  d = t;
}
inline void vsl(VR& d, const VR& a, const VR& b) {
  uint32_t s = b.b[15] & 7;
  VR t;
  for (int i = 0; i < 16; i++) t.b[i] = uint8_t((a.b[i] << s) | (i < 15 && s ? a.b[i + 1] >> (8 - s) : 0));
  d = t;
}
inline void vsr(VR& d, const VR& a, const VR& b) {
  uint32_t s = b.b[15] & 7;
  VR t;
  for (int i = 0; i < 16; i++) t.b[i] = uint8_t((a.b[i] >> s) | (i > 0 && s ? a.b[i - 1] << (8 - s) : 0));
  d = t;
}
// pack
inline void vpkuhum(VR& d, const VR& a, const VR& b) { VR t; for (int i = 0; i < 8; i++) { t.b[i] = uint8_t(h(a, i)); t.b[8 + i] = uint8_t(h(b, i)); } d = t; }
inline void vpkuwum(VR& d, const VR& a, const VR& b) { VR t; for (int i = 0; i < 4; i++) { sh(t, i, uint16_t(w(a, i))); sh(t, 4 + i, uint16_t(w(b, i))); } d = t; }
inline void vpkuhus(VR& d, const VR& a, const VR& b, uint32_t& s) { VR t; for (int i = 0; i < 8; i++) { t.b[i] = clamp_t<uint8_t>(h(a, i), 0, 255, s); t.b[8 + i] = clamp_t<uint8_t>(h(b, i), 0, 255, s); } d = t; }
inline void vpkuwus(VR& d, const VR& a, const VR& b, uint32_t& s) { VR t; for (int i = 0; i < 4; i++) { sh(t, i, clamp_t<uint16_t>(w(a, i), 0, 65535, s)); sh(t, 4 + i, clamp_t<uint16_t>(w(b, i), 0, 65535, s)); } d = t; }
inline void vpkshus(VR& d, const VR& a, const VR& b, uint32_t& s) { VR t; for (int i = 0; i < 8; i++) { t.b[i] = clamp_t<uint8_t>(int16_t(h(a, i)), 0, 255, s); t.b[8 + i] = clamp_t<uint8_t>(int16_t(h(b, i)), 0, 255, s); } d = t; }
inline void vpkshss(VR& d, const VR& a, const VR& b, uint32_t& s) { VR t; for (int i = 0; i < 8; i++) { t.b[i] = uint8_t(clamp_t<int8_t>(int16_t(h(a, i)), -128, 127, s)); t.b[8 + i] = uint8_t(clamp_t<int8_t>(int16_t(h(b, i)), -128, 127, s)); } d = t; }
inline void vpkswus(VR& d, const VR& a, const VR& b, uint32_t& s) { VR t; for (int i = 0; i < 4; i++) { sh(t, i, clamp_t<uint16_t>(int32_t(w(a, i)), 0, 65535, s)); sh(t, 4 + i, clamp_t<uint16_t>(int32_t(w(b, i)), 0, 65535, s)); } d = t; }
inline void vpkswss(VR& d, const VR& a, const VR& b, uint32_t& s) { VR t; for (int i = 0; i < 4; i++) { sh(t, i, uint16_t(clamp_t<int16_t>(int32_t(w(a, i)), -32768, 32767, s))); sh(t, 4 + i, uint16_t(clamp_t<int16_t>(int32_t(w(b, i)), -32768, 32767, s))); } d = t; }
inline void vpkpx(VR& d, const VR& a, const VR& b) {
  VR t;
  for (int i = 0; i < 8; i++) {
    uint32_t x = i < 4 ? w(a, i) : w(b, i - 4);
    sh(t, i, uint16_t(((x >> 24) & 1) << 15 | ((x >> 19) & 31) << 10 | ((x >> 11) & 31) << 5 | ((x >> 3) & 31)));
  }
  d = t;
}
// unpack
inline void vupkhsb(VR& d, const VR& b) { VR t; for (int i = 0; i < 8; i++) sh(t, i, uint16_t(int16_t(int8_t(b.b[i])))); d = t; }
inline void vupklsb(VR& d, const VR& b) { VR t; for (int i = 0; i < 8; i++) sh(t, i, uint16_t(int16_t(int8_t(b.b[8 + i])))); d = t; }
inline void vupkhsh(VR& d, const VR& b) { VR t; for (int i = 0; i < 4; i++) sw(t, i, uint32_t(int32_t(int16_t(h(b, i))))); d = t; }
inline void vupklsh(VR& d, const VR& b) { VR t; for (int i = 0; i < 4; i++) sw(t, i, uint32_t(int32_t(int16_t(h(b, 4 + i))))); d = t; }
inline uint32_t upkpx1(uint16_t p) {
  return uint32_t((p & 0x8000) ? 0xFF : 0) << 24 | uint32_t((p >> 10) & 31) << 16 | uint32_t((p >> 5) & 31) << 8 | (p & 31);
}
inline void vupkhpx(VR& d, const VR& b) { VR t; for (int i = 0; i < 4; i++) sw(t, i, upkpx1(h(b, i))); d = t; }
inline void vupklpx(VR& d, const VR& b) { VR t; for (int i = 0; i < 4; i++) sw(t, i, upkpx1(h(b, 4 + i))); d = t; }
// multiplies
inline void vmuleub(VR& d, const VR& a, const VR& b) { VR t; for (int i = 0; i < 8; i++) sh(t, i, uint16_t(a.b[2 * i] * b.b[2 * i])); d = t; }
inline void vmuloub(VR& d, const VR& a, const VR& b) { VR t; for (int i = 0; i < 8; i++) sh(t, i, uint16_t(a.b[2 * i + 1] * b.b[2 * i + 1])); d = t; }
inline void vmulesb(VR& d, const VR& a, const VR& b) { VR t; for (int i = 0; i < 8; i++) sh(t, i, uint16_t(int8_t(a.b[2 * i]) * int8_t(b.b[2 * i]))); d = t; }
inline void vmulosb(VR& d, const VR& a, const VR& b) { VR t; for (int i = 0; i < 8; i++) sh(t, i, uint16_t(int8_t(a.b[2 * i + 1]) * int8_t(b.b[2 * i + 1]))); d = t; }
inline void vmuleuh(VR& d, const VR& a, const VR& b) { VR t; for (int i = 0; i < 4; i++) sw(t, i, uint32_t(h(a, 2 * i)) * h(b, 2 * i)); d = t; }
inline void vmulouh(VR& d, const VR& a, const VR& b) { VR t; for (int i = 0; i < 4; i++) sw(t, i, uint32_t(h(a, 2 * i + 1)) * h(b, 2 * i + 1)); d = t; }
inline void vmulesh(VR& d, const VR& a, const VR& b) { VR t; for (int i = 0; i < 4; i++) sw(t, i, uint32_t(int32_t(int16_t(h(a, 2 * i))) * int16_t(h(b, 2 * i)))); d = t; }
inline void vmulosh(VR& d, const VR& a, const VR& b) { VR t; for (int i = 0; i < 4; i++) sw(t, i, uint32_t(int32_t(int16_t(h(a, 2 * i + 1))) * int16_t(h(b, 2 * i + 1)))); d = t; }
// multiply-add / multiply-sum
inline void vmhaddshs(VR& d, const VR& a, const VR& b, const VR& c, uint32_t& s) {
  VR t; for (int i = 0; i < 8; i++) sh(t, i, uint16_t(clamp_t<int16_t>(((int32_t(int16_t(h(a, i))) * int16_t(h(b, i))) >> 15) + int16_t(h(c, i)), -32768, 32767, s))); d = t;
}
inline void vmhraddshs(VR& d, const VR& a, const VR& b, const VR& c, uint32_t& s) {
  VR t; for (int i = 0; i < 8; i++) sh(t, i, uint16_t(clamp_t<int16_t>(((int32_t(int16_t(h(a, i))) * int16_t(h(b, i)) + 0x4000) >> 15) + int16_t(h(c, i)), -32768, 32767, s))); d = t;
}
inline void vmladduhm(VR& d, const VR& a, const VR& b, const VR& c) { VR t; for (int i = 0; i < 8; i++) sh(t, i, uint16_t(h(a, i) * h(b, i) + h(c, i))); d = t; }
inline void vmsumubm(VR& d, const VR& a, const VR& b, const VR& c) {
  VR t; for (int i = 0; i < 4; i++) { uint32_t s = w(c, i); for (int j = 0; j < 4; j++) s += uint32_t(a.b[4 * i + j]) * b.b[4 * i + j]; sw(t, i, s); } d = t;
}
inline void vmsummbm(VR& d, const VR& a, const VR& b, const VR& c) {
  VR t; for (int i = 0; i < 4; i++) { uint32_t s = w(c, i); for (int j = 0; j < 4; j++) s += uint32_t(int32_t(int8_t(a.b[4 * i + j])) * b.b[4 * i + j]); sw(t, i, s); } d = t;
}
inline void vmsumuhm(VR& d, const VR& a, const VR& b, const VR& c) {
  VR t; for (int i = 0; i < 4; i++) sw(t, i, w(c, i) + uint32_t(h(a, 2 * i)) * h(b, 2 * i) + uint32_t(h(a, 2 * i + 1)) * h(b, 2 * i + 1)); d = t;
}
inline void vmsumuhs(VR& d, const VR& a, const VR& b, const VR& c, uint32_t& s) {
  VR t; for (int i = 0; i < 4; i++) sw(t, i, clamp_t<uint32_t>(int64_t(w(c, i)) + int64_t(h(a, 2 * i)) * h(b, 2 * i) + int64_t(h(a, 2 * i + 1)) * h(b, 2 * i + 1), 0, 4294967295ll, s)); d = t;
}
inline void vmsumshm(VR& d, const VR& a, const VR& b, const VR& c) {
  VR t; for (int i = 0; i < 4; i++) sw(t, i, w(c, i) + uint32_t(int32_t(int16_t(h(a, 2 * i))) * int16_t(h(b, 2 * i)) + int32_t(int16_t(h(a, 2 * i + 1))) * int16_t(h(b, 2 * i + 1)))); d = t;
}
inline void vmsumshs(VR& d, const VR& a, const VR& b, const VR& c, uint32_t& s) {
  VR t; for (int i = 0; i < 4; i++) sw(t, i, uint32_t(clamp_t<int32_t>(int64_t(int32_t(w(c, i))) + int64_t(int16_t(h(a, 2 * i))) * int16_t(h(b, 2 * i)) + int64_t(int16_t(h(a, 2 * i + 1))) * int16_t(h(b, 2 * i + 1)), INT32_MIN, INT32_MAX, s))); d = t;
}
// sums across
inline void vsum4ubs(VR& d, const VR& a, const VR& b, uint32_t& s) {
  VR t; for (int i = 0; i < 4; i++) { int64_t v = w(b, i); for (int j = 0; j < 4; j++) v += a.b[4 * i + j]; sw(t, i, clamp_t<uint32_t>(v, 0, 4294967295ll, s)); } d = t;
}
inline void vsum4sbs(VR& d, const VR& a, const VR& b, uint32_t& s) {
  VR t; for (int i = 0; i < 4; i++) { int64_t v = int32_t(w(b, i)); for (int j = 0; j < 4; j++) v += int8_t(a.b[4 * i + j]); sw(t, i, uint32_t(clamp_t<int32_t>(v, INT32_MIN, INT32_MAX, s))); } d = t;
}
inline void vsum4shs(VR& d, const VR& a, const VR& b, uint32_t& s) {
  VR t; for (int i = 0; i < 4; i++) { int64_t v = int64_t(int32_t(w(b, i))) + int16_t(h(a, 2 * i)) + int16_t(h(a, 2 * i + 1)); sw(t, i, uint32_t(clamp_t<int32_t>(v, INT32_MIN, INT32_MAX, s))); } d = t;
}
inline void vsum2sws(VR& d, const VR& a, const VR& b, uint32_t& s) {
  VR t{};
  for (int i = 0; i < 2; i++) {
    int64_t v = int64_t(int32_t(w(b, 2 * i + 1))) + int32_t(w(a, 2 * i)) + int32_t(w(a, 2 * i + 1));
    sw(t, 2 * i + 1, uint32_t(clamp_t<int32_t>(v, INT32_MIN, INT32_MAX, s)));
  }
  d = t;
}
inline void vsumsws(VR& d, const VR& a, const VR& b, uint32_t& s) {
  VR t{};
  int64_t v = int32_t(w(b, 3));
  for (int i = 0; i < 4; i++) v += int32_t(w(a, i));
  sw(t, 3, uint32_t(clamp_t<int32_t>(v, INT32_MIN, INT32_MAX, s)));
  d = t;
}

// ---------------------------------------------------------------- VMX128 extras
inline void vmsum3fp128(VR& d, const VR& a, const VR& b) {
  float r = f(a, 0) * f(b, 0) + f(a, 1) * f(b, 1) + f(a, 2) * f(b, 2);
  for (int i = 0; i < 4; i++) sf(d, i, r);
}
inline void vmsum4fp128(VR& d, const VR& a, const VR& b) {
  float r = f(a, 0) * f(b, 0) + f(a, 1) * f(b, 1) + f(a, 2) * f(b, 2) + f(a, 3) * f(b, 3);
  for (int i = 0; i < 4; i++) sf(d, i, r);
}
// Rotate VB left by z words, then insert into VD where the 4-bit mask (x=8,y=4,z=2,w=1) is set.
inline void vrlimi128(VR& d, const VR& b, uint32_t imm, uint32_t z) {
  VR t = d;
  for (int i = 0; i < 4; i++)
    if (imm & (8u >> i)) sw(t, i, w(b, (i + z) & 3));
  d = t;
}
inline void vpermwi128(VR& d, const VR& b, uint32_t perm) {
  VR t;
  for (int i = 0; i < 4; i++) sw(t, i, w(b, (perm >> (6 - 2 * i)) & 3));
  d = t;
}

#undef FSR_VX_B
#undef FSR_VX_H
#undef FSR_VX_W
#undef FSR_VX_F
#undef FSR_VX_BS
#undef FSR_VX_HS
#undef FSR_VX_WS
#undef FSR_VX_F1

}  // namespace vx
}  // namespace fsr
