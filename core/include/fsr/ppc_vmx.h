// FIFA STREET Recompiled - AltiVec (VMX) and Xenon VMX128 decoder.
// Opcode/field layout cross-checked with Xenia's generated decoder tables
// (BSD-3-Clause, ppc_opcode_lookup_gen.cc / ppc_decode_data.h).
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>

namespace fsr::ppc {

#define FSR_VMX_OPS(X)                                                                                            \
  X(unknown)                                                                                                      \
  /* standard loads/stores (opcode 31) */                                                                         \
  X(lvsl) X(lvsr) X(lvebx) X(lvehx) X(lvewx) X(lvx) X(lvxl) X(stvebx) X(stvehx) X(stvewx) X(stvx) X(stvxl)        \
  X(lvlx) X(lvrx) X(stvlx) X(stvrx) X(lvlxl) X(lvrxl) X(stvlxl) X(stvrxl)                                         \
  /* VMX128 loads/stores (opcode 4) */                                                                            \
  X(lvsl128) X(lvsr128) X(lvewx128) X(lvx128) X(stvewx128) X(stvx128) X(lvxl128) X(stvxl128) X(lvlx128)           \
  X(lvrx128) X(stvlx128) X(stvrx128) X(lvlxl128) X(lvrxl128) X(stvlxl128) X(stvrxl128)                            \
  /* VX form */                                                                                                   \
  X(vaddubm) X(vmaxub) X(vrlb) X(vmuloub) X(vaddfp) X(vmrghb) X(vpkuhum) X(vadduhm) X(vmaxuh) X(vrlh) X(vmulouh)  \
  X(vsubfp) X(vmrghh) X(vpkuwum) X(vadduwm) X(vmaxuw) X(vrlw) X(vmrghw) X(vpkuhus) X(vpkuwus) X(vmaxsb) X(vslb)   \
  X(vmulosb) X(vrefp) X(vmrglb) X(vpkshus) X(vmaxsh) X(vslh) X(vmulosh) X(vrsqrtefp) X(vmrglh) X(vpkswus)         \
  X(vaddcuw) X(vmaxsw) X(vslw) X(vexptefp) X(vmrglw) X(vpkshss) X(vsl) X(vlogefp) X(vpkswss) X(vaddubs) X(vminub) \
  X(vsrb) X(vmuleub) X(vrfin) X(vspltb) X(vupkhsb) X(vadduhs) X(vminuh) X(vsrh) X(vmuleuh) X(vrfiz) X(vsplth)     \
  X(vupkhsh) X(vadduws) X(vminuw) X(vsrw) X(vrfip) X(vspltw) X(vupklsb) X(vsr) X(vrfim) X(vupklsh) X(vaddsbs)     \
  X(vminsb) X(vsrab) X(vmulesb) X(vcfux) X(vspltisb) X(vpkpx) X(vaddshs) X(vminsh) X(vsrah) X(vmulesh) X(vcfsx)   \
  X(vspltish) X(vupkhpx) X(vaddsws) X(vminsw) X(vsraw) X(vctuxs) X(vspltisw) X(vctsxs) X(vupklpx) X(vsububm)      \
  X(vavgub) X(vand) X(vmaxfp) X(vslo) X(vsubuhm) X(vavguh) X(vandc) X(vminfp) X(vsro) X(vsubuwm) X(vavguw) X(vor) \
  X(vxor) X(vavgsb) X(vnor) X(vavgsh) X(vsubcuw) X(vavgsw) X(vsububs) X(mfvscr) X(vsum4ubs) X(vsubuhs) X(mtvscr)  \
  X(vsum4shs) X(vsubuws) X(vsum2sws) X(vsubsbs) X(vsum4sbs) X(vsubshs) X(vsubsws) X(vsumsws)                      \
  /* VC form */                                                                                                   \
  X(vcmpequb) X(vcmpequh) X(vcmpequw) X(vcmpeqfp) X(vcmpgefp) X(vcmpgtub) X(vcmpgtuh) X(vcmpgtuw) X(vcmpgtfp)     \
  X(vcmpgtsb) X(vcmpgtsh) X(vcmpgtsw) X(vcmpbfp)                                                                  \
  /* VA form */                                                                                                   \
  X(vmhaddshs) X(vmhraddshs) X(vmladduhm) X(vmsumubm) X(vmsummbm) X(vmsumuhm) X(vmsumuhs) X(vmsumshm)             \
  X(vmsumshs) X(vsel) X(vperm) X(vsldoi) X(vmaddfp) X(vnmsubfp)                                                   \
  /* VMX128 */                                                                                                    \
  X(vsldoi128) X(vperm128) X(vaddfp128) X(vsubfp128) X(vmulfp128) X(vmaddfp128) X(vmaddcfp128) X(vnmsubfp128)     \
  X(vmsum3fp128) X(vmsum4fp128) X(vpkshss128) X(vand128) X(vpkshus128) X(vandc128) X(vpkswss128) X(vnor128)       \
  X(vpkswus128) X(vor128) X(vpkuhum128) X(vxor128) X(vpkuhus128) X(vsel128) X(vpkuwum128) X(vslo128)              \
  X(vpkuwus128) X(vsro128) X(vpermwi128) X(vpkd3d128) X(vrlimi128) X(vcfpsxws128) X(vcfpuxws128) X(vcsxwfp128)    \
  X(vcuxwfp128) X(vrfim128) X(vrfin128) X(vrfip128) X(vrfiz128) X(vrefp128) X(vrsqrtefp128) X(vexptefp128)        \
  X(vlogefp128) X(vspltw128) X(vspltisw128) X(vupkd3d128) X(vcmpeqfp128) X(vcmpgefp128) X(vcmpgtfp128)           \
  X(vcmpbfp128) X(vcmpequw128) X(vrlw128) X(vslw128) X(vsraw128) X(vsrw128) X(vmaxfp128) X(vminfp128)             \
  X(vmrghw128) X(vmrglw128) X(vupkhsb128) X(vupklsb128)

enum class VOp : uint16_t {
#define FSR_VENUM(n) n,
  FSR_VMX_OPS(FSR_VENUM)
#undef FSR_VENUM
  count
};

VOp decode_vmx(uint32_t raw);
const char* vop_name(VOp op);
bool vop_is_vmx128(VOp op);

// PowerPC bit-numbered field extraction (bit 0 = MSB).
constexpr uint32_t bits(uint32_t v, uint32_t a, uint32_t b) { return (v >> (31 - b)) & ((1u << (b - a + 1)) - 1); }

// Standard VX/VA/VC operand fields.
inline uint32_t v_d(uint32_t r) { return (r >> 21) & 31; }
inline uint32_t v_a(uint32_t r) { return (r >> 16) & 31; }
inline uint32_t v_b(uint32_t r) { return (r >> 11) & 31; }
inline uint32_t v_c(uint32_t r) { return (r >> 6) & 31; }
inline bool v_rc(uint32_t r) { return (r >> 10) & 1; }

// VMX128 operand fields.
inline uint32_t v128_d(uint32_t r) { return ((r >> 21) & 31) | (((r >> 2) & 3) << 5); }
inline uint32_t v128_a(uint32_t r) { return ((r >> 16) & 31) | (((r >> 5) & 1) << 5) | (((r >> 10) & 1) << 6); }
inline uint32_t v128_b(uint32_t r) { return ((r >> 11) & 31) | ((r & 3) << 5); }
inline uint32_t v128_c3(uint32_t r) { return (r >> 6) & 7; }        // VX128_2 (vperm128)
inline uint32_t v128_imm(uint32_t r) { return (r >> 16) & 31; }     // VX128_3/4
inline uint32_t v128_z(uint32_t r) { return (r >> 6) & 3; }         // VX128_4
inline uint32_t v128_sh(uint32_t r) { return (r >> 6) & 15; }       // VX128_5
inline bool v128_rc(uint32_t r) { return (r >> 6) & 1; }            // VX128_R
inline uint32_t v128_perm(uint32_t r) { return ((r >> 16) & 31) | (((r >> 6) & 7) << 5); }  // VX128_P

}  // namespace fsr::ppc
