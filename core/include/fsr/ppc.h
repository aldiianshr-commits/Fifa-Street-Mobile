// FIFA STREET Recompiled - PowerPC (Xenon) instruction decoder.
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string>

namespace fsr::ppc {

#define FSR_PPC_OPS(X)                                                                          \
  X(invalid) X(tdi) X(twi) X(mulli) X(subfic) X(cmpli) X(cmpi) X(addic) X(addic_rc) X(addi)     \
  X(addis) X(bc) X(sc) X(b) X(mcrf) X(bclr) X(crnor) X(crandc) X(isync) X(crxor) X(crnand)      \
  X(crand) X(creqv) X(crorc) X(cror) X(bcctr) X(rlwimi) X(rlwinm) X(rlwnm) X(ori) X(oris)       \
  X(xori) X(xoris) X(andi_rc) X(andis_rc) X(rldicl) X(rldicr) X(rldic) X(rldimi) X(rldcl)       \
  X(rldcr) X(cmp) X(tw) X(subfc) X(mulhdu) X(addc) X(mulhwu) X(mfcr) X(lwarx) X(ldx) X(lwzx)    \
  X(slw) X(cntlzw) X(sld) X(and_) X(cmpl) X(subf) X(ldux) X(dcbst) X(lwzux) X(cntlzd)           \
  X(andc) X(td) X(mulhd) X(mulhw) X(mfmsr) X(ldarx) X(dcbf) X(lbzx) X(neg) X(lbzux) X(nor)      \
  X(subfe) X(adde) X(mtcrf) X(mtmsr) X(stdx) X(stwcx) X(stwx) X(mtmsrd) X(stdux) X(stwux)       \
  X(subfze) X(addze) X(stdcx) X(stbx) X(subfme) X(mulld) X(addme) X(mullw) X(dcbtst) X(stbux)   \
  X(add) X(dcbt) X(lhzx) X(eqv) X(lhzux) X(xor_) X(mfspr) X(lwax) X(lhax) X(mftb) X(lwaux)      \
  X(lhaux) X(sthx) X(orc) X(sthux) X(or_) X(divdu) X(divwu) X(mtspr) X(dcbi) X(nand) X(divd)    \
  X(divw) X(mcrxr) X(lwbrx) X(lfsx) X(srw) X(srd) X(lfsux) X(sync) X(lfdx) X(lfdux) X(stwbrx)   \
  X(stfsx) X(stfsux) X(stfdx) X(stfdux) X(lhbrx) X(sraw) X(srad) X(srawi) X(sradi) X(eieio)     \
  X(sthbrx) X(extsh) X(extsb) X(icbi) X(stfiwx) X(extsw) X(dcbz) X(lwz) X(lwzu) X(lbz) X(lbzu)  \
  X(stw) X(stwu) X(stb) X(stbu) X(lhz) X(lhzu) X(lha) X(lhau) X(sth) X(sthu) X(lmw) X(stmw)     \
  X(lfs) X(lfsu) X(lfd) X(lfdu) X(stfs) X(stfsu) X(stfd) X(stfdu) X(ld) X(ldu) X(lwa) X(std)    \
  X(stdu) X(fdivs) X(fsubs) X(fadds) X(fsqrts) X(fres) X(fmuls) X(fmsubs) X(fmadds) X(fnmsubs)  \
  X(fnmadds) X(fdiv) X(fsub) X(fadd) X(fsqrt) X(fsel) X(fmul) X(frsqrte) X(fmsub) X(fmadd)      \
  X(fnmsub) X(fnmadd) X(fcmpu) X(frsp) X(fctiw) X(fctiwz) X(fcmpo) X(mtfsb1) X(fneg) X(mcrfs)   \
  X(mtfsb0) X(fmr) X(mtfsfi) X(fnabs) X(fabs) X(mffs) X(mtfsf) X(fctid) X(fctidz) X(fcfid)      \
  X(vmx) /* any AltiVec / VMX128 instruction (not yet translated) */

enum class Op : uint16_t {
#define FSR_ENUM(n) n,
  FSR_PPC_OPS(FSR_ENUM)
#undef FSR_ENUM
  count
};

Op decode(uint32_t raw);
const char* op_name(Op op);
std::string disasm(uint32_t raw, uint32_t addr);

// Field helpers (PowerPC bit numbering: bit 0 = MSB).
inline uint32_t f_opcd(uint32_t r) { return r >> 26; }
inline uint32_t f_rt(uint32_t r) { return (r >> 21) & 31; }
inline uint32_t f_ra(uint32_t r) { return (r >> 16) & 31; }
inline uint32_t f_rb(uint32_t r) { return (r >> 11) & 31; }
inline uint32_t f_rc(uint32_t r) { return (r >> 6) & 31; }
inline int32_t f_simm(uint32_t r) { return int16_t(r & 0xFFFF); }
inline uint32_t f_uimm(uint32_t r) { return r & 0xFFFF; }
inline int32_t f_ds(uint32_t r) { return int16_t(r & 0xFFFC); }
inline bool f_rcbit(uint32_t r) { return r & 1; }
inline bool f_oe(uint32_t r) { return (r >> 10) & 1; }
inline bool f_lk(uint32_t r) { return r & 1; }
inline bool f_aa(uint32_t r) { return (r >> 1) & 1; }
inline uint32_t f_bo(uint32_t r) { return (r >> 21) & 31; }
inline uint32_t f_bi(uint32_t r) { return (r >> 16) & 31; }
inline uint32_t f_crfd(uint32_t r) { return (r >> 23) & 7; }
inline uint32_t f_crfs(uint32_t r) { return (r >> 18) & 7; }
inline bool f_l(uint32_t r) { return (r >> 21) & 1; }
inline uint32_t f_sh(uint32_t r) { return (r >> 11) & 31; }
inline uint32_t f_mb(uint32_t r) { return (r >> 6) & 31; }
inline uint32_t f_me(uint32_t r) { return (r >> 1) & 31; }
inline uint32_t f_sh64(uint32_t r) { return ((r >> 11) & 31) | ((r & 2) << 4); }
inline uint32_t f_mb64(uint32_t r) { return ((r >> 6) & 31) | (r & 0x20); }
inline uint32_t f_spr(uint32_t r) { return ((r >> 16) & 31) | (((r >> 11) & 31) << 5); }
inline uint32_t f_crm(uint32_t r) { return (r >> 12) & 0xFF; }
inline int32_t f_li(uint32_t r) { return int32_t((r & 0x03FFFFFC) << 6) >> 6; }
inline int32_t f_bd(uint32_t r) { return int16_t(r & 0xFFFC); }
inline uint32_t f_tbr(uint32_t r) { return f_spr(r); }

// Branch target helpers.
inline uint32_t branch_target(uint32_t raw, uint32_t addr) {
  Op op = decode(raw);
  if (op == Op::b) return f_aa(raw) ? uint32_t(f_li(raw)) : addr + uint32_t(f_li(raw));
  if (op == Op::bc) return f_aa(raw) ? uint32_t(f_bd(raw)) : addr + uint32_t(f_bd(raw));
  return 0;
}

// 64-bit PowerPC mask: ones from bit mb to bit me (bit 0 = MSB), wrapping.
constexpr uint64_t mask64(uint32_t mb, uint32_t me) {
  uint64_t a = ~uint64_t(0) >> mb;
  uint64_t b = me >= 63 ? ~uint64_t(0) : ~(~uint64_t(0) >> (me + 1));
  return mb <= me ? (a & b) : (a | b);
}

}  // namespace fsr::ppc
