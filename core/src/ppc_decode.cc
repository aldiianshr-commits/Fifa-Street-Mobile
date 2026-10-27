// SPDX-License-Identifier: MIT
#include <cstdio>

#include "fsr/ppc.h"
#include "fsr/ppc_vmx.h"

namespace fsr::ppc {

const char* op_name(Op op) {
  static const char* names[] = {
#define FSR_NAME(n) #n,
      FSR_PPC_OPS(FSR_NAME)
#undef FSR_NAME
  };
  return unsigned(op) < unsigned(Op::count) ? names[unsigned(op)] : "?";
}

static Op decode19(uint32_t r) {
  switch ((r >> 1) & 0x3FF) {
    case 0: return Op::mcrf;
    case 16: return Op::bclr;
    case 33: return Op::crnor;
    case 129: return Op::crandc;
    case 150: return Op::isync;
    case 193: return Op::crxor;
    case 225: return Op::crnand;
    case 257: return Op::crand;
    case 289: return Op::creqv;
    case 417: return Op::crorc;
    case 449: return Op::cror;
    case 528: return Op::bcctr;
  }
  return Op::invalid;
}

static Op decode30(uint32_t r) {
  switch ((r >> 2) & 7) {
    case 0: return Op::rldicl;
    case 1: return Op::rldicr;
    case 2: return Op::rldic;
    case 3: return Op::rldimi;
  }
  switch ((r >> 1) & 0xF) {
    case 8: return Op::rldcl;
    case 9: return Op::rldcr;
  }
  return Op::invalid;
}

static Op decode31(uint32_t r) {
  // XO-form (9-bit extended opcode, OE in bit 21).
  switch ((r >> 1) & 0x1FF) {
    case 8: return Op::subfc;
    case 9: return Op::mulhdu;
    case 10: return Op::addc;
    case 11: return Op::mulhwu;
    case 40: return Op::subf;
    case 73: return Op::mulhd;
    case 75: return Op::mulhw;
    case 104: return Op::neg;
    case 136: return Op::subfe;
    case 138: return Op::adde;
    case 200: return Op::subfze;
    case 202: return Op::addze;
    case 232: return Op::subfme;
    case 233: return Op::mulld;
    case 234: return Op::addme;
    case 235: return Op::mullw;
    case 266: return Op::add;
    case 457: return Op::divdu;
    case 459: return Op::divwu;
    case 489: return Op::divd;
    case 491: return Op::divw;
  }
  if (((r >> 2) & 0x1FF) == 413) return Op::sradi;  // XS-form
  switch ((r >> 1) & 0x3FF) {
    case 0: return Op::cmp;
    case 4: return Op::tw;
    case 19: return Op::mfcr;
    case 20: return Op::lwarx;
    case 21: return Op::ldx;
    case 23: return Op::lwzx;
    case 24: return Op::slw;
    case 26: return Op::cntlzw;
    case 27: return Op::sld;
    case 28: return Op::and_;
    case 32: return Op::cmpl;
    case 53: return Op::ldux;
    case 54: return Op::dcbst;
    case 55: return Op::lwzux;
    case 58: return Op::cntlzd;
    case 60: return Op::andc;
    case 68: return Op::td;
    case 83: return Op::mfmsr;
    case 84: return Op::ldarx;
    case 86: return Op::dcbf;
    case 87: return Op::lbzx;
    case 119: return Op::lbzux;
    case 124: return Op::nor;
    case 144: return Op::mtcrf;
    case 146: return Op::mtmsr;
    case 149: return Op::stdx;
    case 150: return Op::stwcx;
    case 151: return Op::stwx;
    case 178: return Op::mtmsrd;
    case 181: return Op::stdux;
    case 183: return Op::stwux;
    case 214: return Op::stdcx;
    case 215: return Op::stbx;
    case 246: return Op::dcbtst;
    case 247: return Op::stbux;
    case 278: return Op::dcbt;
    case 279: return Op::lhzx;
    case 284: return Op::eqv;
    case 311: return Op::lhzux;
    case 316: return Op::xor_;
    case 339: return Op::mfspr;
    case 341: return Op::lwax;
    case 343: return Op::lhax;
    case 371: return Op::mftb;
    case 373: return Op::lwaux;
    case 375: return Op::lhaux;
    case 407: return Op::sthx;
    case 412: return Op::orc;
    case 439: return Op::sthux;
    case 444: return Op::or_;
    case 467: return Op::mtspr;
    case 470: return Op::dcbi;
    case 476: return Op::nand;
    case 512: return Op::mcrxr;
    case 534: return Op::lwbrx;
    case 535: return Op::lfsx;
    case 536: return Op::srw;
    case 539: return Op::srd;
    case 567: return Op::lfsux;
    case 598: return Op::sync;
    case 599: return Op::lfdx;
    case 631: return Op::lfdux;
    case 662: return Op::stwbrx;
    case 663: return Op::stfsx;
    case 695: return Op::stfsux;
    case 727: return Op::stfdx;
    case 759: return Op::stfdux;
    case 790: return Op::lhbrx;
    case 792: return Op::sraw;
    case 794: return Op::srad;
    case 824: return Op::srawi;
    case 854: return Op::eieio;
    case 918: return Op::sthbrx;
    case 922: return Op::extsh;
    case 954: return Op::extsb;
    case 982: return Op::icbi;
    case 983: return Op::stfiwx;
    case 986: return Op::extsw;
    case 1014: return Op::dcbz;
    // AltiVec / VMX128 loads and stores that live under primary opcode 31.
    case 6: case 7: case 38: case 39: case 71: case 103: case 135: case 167: case 199:
    case 231: case 359: case 487: case 519: case 551: case 647: case 679: case 775:
    case 807: case 903: case 935:
      return Op::vmx;
  }
  return Op::invalid;
}

static Op decode59(uint32_t r) {
  switch ((r >> 1) & 0x1F) {
    case 18: return Op::fdivs;
    case 20: return Op::fsubs;
    case 21: return Op::fadds;
    case 22: return Op::fsqrts;
    case 24: return Op::fres;
    case 25: return Op::fmuls;
    case 28: return Op::fmsubs;
    case 29: return Op::fmadds;
    case 30: return Op::fnmsubs;
    case 31: return Op::fnmadds;
  }
  return Op::invalid;
}

static Op decode63(uint32_t r) {
  switch ((r >> 1) & 0x1F) {
    case 18: return Op::fdiv;
    case 20: return Op::fsub;
    case 21: return Op::fadd;
    case 22: return Op::fsqrt;
    case 23: return Op::fsel;
    case 25: return Op::fmul;
    case 26: return Op::frsqrte;
    case 28: return Op::fmsub;
    case 29: return Op::fmadd;
    case 30: return Op::fnmsub;
    case 31: return Op::fnmadd;
  }
  switch ((r >> 1) & 0x3FF) {
    case 0: return Op::fcmpu;
    case 12: return Op::frsp;
    case 14: return Op::fctiw;
    case 15: return Op::fctiwz;
    case 32: return Op::fcmpo;
    case 38: return Op::mtfsb1;
    case 40: return Op::fneg;
    case 64: return Op::mcrfs;
    case 70: return Op::mtfsb0;
    case 72: return Op::fmr;
    case 134: return Op::mtfsfi;
    case 136: return Op::fnabs;
    case 264: return Op::fabs;
    case 583: return Op::mffs;
    case 711: return Op::mtfsf;
    case 814: return Op::fctid;
    case 815: return Op::fctidz;
    case 846: return Op::fcfid;
  }
  return Op::invalid;
}

Op decode(uint32_t r) {
  switch (r >> 26) {
    case 2: return Op::tdi;
    case 3: return Op::twi;
    case 4: return Op::vmx;
    case 5: case 6: return Op::vmx;  // VMX128 extended forms
    case 7: return Op::mulli;
    case 8: return Op::subfic;
    case 10: return Op::cmpli;
    case 11: return Op::cmpi;
    case 12: return Op::addic;
    case 13: return Op::addic_rc;
    case 14: return Op::addi;
    case 15: return Op::addis;
    case 16: return Op::bc;
    case 17: return Op::sc;
    case 18: return Op::b;
    case 19: return decode19(r);
    case 20: return Op::rlwimi;
    case 21: return Op::rlwinm;
    case 23: return Op::rlwnm;
    case 24: return Op::ori;
    case 25: return Op::oris;
    case 26: return Op::xori;
    case 27: return Op::xoris;
    case 28: return Op::andi_rc;
    case 29: return Op::andis_rc;
    case 30: return decode30(r);
    case 31: return decode31(r);
    case 32: return Op::lwz;
    case 33: return Op::lwzu;
    case 34: return Op::lbz;
    case 35: return Op::lbzu;
    case 36: return Op::stw;
    case 37: return Op::stwu;
    case 38: return Op::stb;
    case 39: return Op::stbu;
    case 40: return Op::lhz;
    case 41: return Op::lhzu;
    case 42: return Op::lha;
    case 43: return Op::lhau;
    case 44: return Op::sth;
    case 45: return Op::sthu;
    case 46: return Op::lmw;
    case 47: return Op::stmw;
    case 48: return Op::lfs;
    case 49: return Op::lfsu;
    case 50: return Op::lfd;
    case 51: return Op::lfdu;
    case 52: return Op::stfs;
    case 53: return Op::stfsu;
    case 54: return Op::stfd;
    case 55: return Op::stfdu;
    case 58:
      switch (r & 3) {
        case 0: return Op::ld;
        case 1: return Op::ldu;
        case 2: return Op::lwa;
      }
      return Op::invalid;
    case 59: return decode59(r);
    case 62:
      switch (r & 3) {
        case 0: return Op::std;
        case 1: return Op::stdu;
      }
      return Op::invalid;
    case 63: return decode63(r);
  }
  return Op::invalid;
}

std::string disasm(uint32_t r, uint32_t addr) {
  Op op = decode(r);
  char buf[96];
  switch (op) {
    case Op::b:
      std::snprintf(buf, sizeof buf, "b%s%s 0x%08X", f_lk(r) ? "l" : "", f_aa(r) ? "a" : "",
                    branch_target(r, addr));
      break;
    case Op::bc:
      std::snprintf(buf, sizeof buf, "bc%s %u,%u,0x%08X", f_lk(r) ? "l" : "", f_bo(r), f_bi(r),
                    branch_target(r, addr));
      break;
    case Op::addi: case Op::addis: case Op::mulli: case Op::subfic: case Op::addic:
    case Op::addic_rc:
      std::snprintf(buf, sizeof buf, "%s r%u,r%u,%d", op_name(op), f_rt(r), f_ra(r), f_simm(r));
      break;
    case Op::ori: case Op::oris: case Op::xori: case Op::xoris: case Op::andi_rc: case Op::andis_rc:
      std::snprintf(buf, sizeof buf, "%s r%u,r%u,0x%X", op_name(op), f_ra(r), f_rt(r), f_uimm(r));
      break;
    case Op::lwz: case Op::lwzu: case Op::lbz: case Op::lbzu: case Op::stw: case Op::stwu:
    case Op::stb: case Op::stbu: case Op::lhz: case Op::lhzu: case Op::lha: case Op::lhau:
    case Op::sth: case Op::sthu: case Op::lmw: case Op::stmw: case Op::lfs: case Op::lfsu:
    case Op::lfd: case Op::lfdu: case Op::stfs: case Op::stfsu: case Op::stfd: case Op::stfdu:
      std::snprintf(buf, sizeof buf, "%s %c%u,%d(r%u)", op_name(op),
                    (op >= Op::lfs && op <= Op::stfdu) ? 'f' : 'r', f_rt(r), f_simm(r), f_ra(r));
      break;
    case Op::ld: case Op::ldu: case Op::lwa: case Op::std: case Op::stdu:
      std::snprintf(buf, sizeof buf, "%s r%u,%d(r%u)", op_name(op), f_rt(r), f_ds(r), f_ra(r));
      break;
    case Op::rlwinm: case Op::rlwimi:
      std::snprintf(buf, sizeof buf, "%s r%u,r%u,%u,%u,%u", op_name(op), f_ra(r), f_rt(r), f_sh(r),
                    f_mb(r), f_me(r));
      break;
    case Op::vmx: {
      VOp v = decode_vmx(r);
      if (v == VOp::unknown) std::snprintf(buf, sizeof buf, "vmx? 0x%08X", r);
      else if (vop_is_vmx128(v))
        std::snprintf(buf, sizeof buf, "%s v%u,v%u,v%u", vop_name(v), v128_d(r), v128_a(r), v128_b(r));
      else std::snprintf(buf, sizeof buf, "%s v%u,v%u,v%u", vop_name(v), v_d(r), v_a(r), v_b(r));
    } break;
    case Op::invalid:
      std::snprintf(buf, sizeof buf, "%s 0x%08X", op_name(op), r);
      break;
    default:
      std::snprintf(buf, sizeof buf, "%s r%u,r%u,r%u", op_name(op), f_rt(r), f_ra(r), f_rb(r));
      break;
  }
  return buf;
}

}  // namespace fsr::ppc
