// SPDX-License-Identifier: MIT
#include "fsr/ppc_vmx.h"

namespace fsr::ppc {

const char* vop_name(VOp op) {
  static const char* names[] = {
#define FSR_VNAME(n) #n,
      FSR_VMX_OPS(FSR_VNAME)
#undef FSR_VNAME
  };
  return unsigned(op) < unsigned(VOp::count) ? names[unsigned(op)] : "?";
}

bool vop_is_vmx128(VOp op) { return op >= VOp::lvsl128 && op <= VOp::stvrxl128 ? true : op >= VOp::vsldoi128; }

static VOp decode31(uint32_t r) {
  switch (bits(r, 21, 30)) {
    case 6: return VOp::lvsl;
    case 38: return VOp::lvsr;
    case 7: return VOp::lvebx;
    case 39: return VOp::lvehx;
    case 71: return VOp::lvewx;
    case 103: return VOp::lvx;
    case 359: return VOp::lvxl;
    case 135: return VOp::stvebx;
    case 167: return VOp::stvehx;
    case 199: return VOp::stvewx;
    case 231: return VOp::stvx;
    case 487: return VOp::stvxl;
    case 519: return VOp::lvlx;
    case 551: return VOp::lvrx;
    case 647: return VOp::stvlx;
    case 679: return VOp::stvrx;
    case 775: return VOp::lvlxl;
    case 807: return VOp::lvrxl;
    case 903: return VOp::stvlxl;
    case 935: return VOp::stvrxl;
  }
  return VOp::unknown;
}

static VOp decode4(uint32_t r) {
  switch ((bits(r, 21, 27) << 4) | bits(r, 30, 31)) {
    case 0b00000000011: return VOp::lvsl128;
    case 0b00001000011: return VOp::lvsr128;
    case 0b00010000011: return VOp::lvewx128;
    case 0b00011000011: return VOp::lvx128;
    case 0b00110000011: return VOp::stvewx128;
    case 0b00111000011: return VOp::stvx128;
    case 0b01011000011: return VOp::lvxl128;
    case 0b01111000011: return VOp::stvxl128;
    case 0b10000000011: return VOp::lvlx128;
    case 0b10001000011: return VOp::lvrx128;
    case 0b10100000011: return VOp::stvlx128;
    case 0b10101000011: return VOp::stvrx128;
    case 0b11000000011: return VOp::lvlxl128;
    case 0b11001000011: return VOp::lvrxl128;
    case 0b11100000011: return VOp::stvlxl128;
    case 0b11101000011: return VOp::stvrxl128;
  }
  switch (bits(r, 21, 31)) {
    case 0: return VOp::vaddubm;     case 2: return VOp::vmaxub;      case 4: return VOp::vrlb;
    case 8: return VOp::vmuloub;     case 10: return VOp::vaddfp;     case 12: return VOp::vmrghb;
    case 14: return VOp::vpkuhum;    case 64: return VOp::vadduhm;    case 66: return VOp::vmaxuh;
    case 68: return VOp::vrlh;       case 72: return VOp::vmulouh;    case 74: return VOp::vsubfp;
    case 76: return VOp::vmrghh;     case 78: return VOp::vpkuwum;    case 128: return VOp::vadduwm;
    case 130: return VOp::vmaxuw;    case 132: return VOp::vrlw;      case 140: return VOp::vmrghw;
    case 142: return VOp::vpkuhus;   case 206: return VOp::vpkuwus;   case 258: return VOp::vmaxsb;
    case 260: return VOp::vslb;      case 264: return VOp::vmulosb;   case 266: return VOp::vrefp;
    case 268: return VOp::vmrglb;    case 270: return VOp::vpkshus;   case 322: return VOp::vmaxsh;
    case 324: return VOp::vslh;      case 328: return VOp::vmulosh;   case 330: return VOp::vrsqrtefp;
    case 332: return VOp::vmrglh;    case 334: return VOp::vpkswus;   case 384: return VOp::vaddcuw;
    case 386: return VOp::vmaxsw;    case 388: return VOp::vslw;      case 394: return VOp::vexptefp;
    case 396: return VOp::vmrglw;    case 398: return VOp::vpkshss;   case 452: return VOp::vsl;
    case 458: return VOp::vlogefp;   case 462: return VOp::vpkswss;   case 512: return VOp::vaddubs;
    case 514: return VOp::vminub;    case 516: return VOp::vsrb;      case 520: return VOp::vmuleub;
    case 522: return VOp::vrfin;     case 524: return VOp::vspltb;    case 526: return VOp::vupkhsb;
    case 576: return VOp::vadduhs;   case 578: return VOp::vminuh;    case 580: return VOp::vsrh;
    case 584: return VOp::vmuleuh;   case 586: return VOp::vrfiz;     case 588: return VOp::vsplth;
    case 590: return VOp::vupkhsh;   case 640: return VOp::vadduws;   case 642: return VOp::vminuw;
    case 644: return VOp::vsrw;      case 650: return VOp::vrfip;     case 652: return VOp::vspltw;
    case 654: return VOp::vupklsb;   case 708: return VOp::vsr;       case 714: return VOp::vrfim;
    case 718: return VOp::vupklsh;   case 768: return VOp::vaddsbs;   case 770: return VOp::vminsb;
    case 772: return VOp::vsrab;     case 776: return VOp::vmulesb;   case 778: return VOp::vcfux;
    case 780: return VOp::vspltisb;  case 782: return VOp::vpkpx;     case 832: return VOp::vaddshs;
    case 834: return VOp::vminsh;    case 836: return VOp::vsrah;     case 840: return VOp::vmulesh;
    case 842: return VOp::vcfsx;     case 844: return VOp::vspltish;  case 846: return VOp::vupkhpx;
    case 896: return VOp::vaddsws;   case 898: return VOp::vminsw;    case 900: return VOp::vsraw;
    case 906: return VOp::vctuxs;    case 908: return VOp::vspltisw;  case 970: return VOp::vctsxs;
    case 974: return VOp::vupklpx;   case 1024: return VOp::vsububm;  case 1026: return VOp::vavgub;
    case 1028: return VOp::vand;     case 1034: return VOp::vmaxfp;   case 1036: return VOp::vslo;
    case 1088: return VOp::vsubuhm;  case 1090: return VOp::vavguh;   case 1092: return VOp::vandc;
    case 1098: return VOp::vminfp;   case 1100: return VOp::vsro;     case 1152: return VOp::vsubuwm;
    case 1154: return VOp::vavguw;   case 1156: return VOp::vor;      case 1220: return VOp::vxor;
    case 1282: return VOp::vavgsb;   case 1284: return VOp::vnor;     case 1346: return VOp::vavgsh;
    case 1408: return VOp::vsubcuw;  case 1410: return VOp::vavgsw;   case 1536: return VOp::vsububs;
    case 1540: return VOp::mfvscr;   case 1544: return VOp::vsum4ubs; case 1600: return VOp::vsubuhs;
    case 1604: return VOp::mtvscr;   case 1608: return VOp::vsum4shs; case 1664: return VOp::vsubuws;
    case 1672: return VOp::vsum2sws; case 1792: return VOp::vsubsbs;  case 1800: return VOp::vsum4sbs;
    case 1856: return VOp::vsubshs;  case 1920: return VOp::vsubsws;  case 1928: return VOp::vsumsws;
  }
  switch (bits(r, 22, 31)) {
    case 6: return VOp::vcmpequb;    case 70: return VOp::vcmpequh;   case 134: return VOp::vcmpequw;
    case 198: return VOp::vcmpeqfp;  case 454: return VOp::vcmpgefp;  case 518: return VOp::vcmpgtub;
    case 582: return VOp::vcmpgtuh;  case 646: return VOp::vcmpgtuw;  case 710: return VOp::vcmpgtfp;
    case 774: return VOp::vcmpgtsb;  case 838: return VOp::vcmpgtsh;  case 902: return VOp::vcmpgtsw;
    case 966: return VOp::vcmpbfp;
  }
  switch (bits(r, 26, 31)) {
    case 32: return VOp::vmhaddshs;  case 33: return VOp::vmhraddshs; case 34: return VOp::vmladduhm;
    case 36: return VOp::vmsumubm;   case 37: return VOp::vmsummbm;   case 38: return VOp::vmsumuhm;
    case 39: return VOp::vmsumuhs;   case 40: return VOp::vmsumshm;   case 41: return VOp::vmsumshs;
    case 42: return VOp::vsel;       case 43: return VOp::vperm;      case 44: return VOp::vsldoi;
    case 46: return VOp::vmaddfp;    case 47: return VOp::vnmsubfp;
  }
  if (bits(r, 27, 27)) return VOp::vsldoi128;
  return VOp::unknown;
}

static VOp decode5(uint32_t r) {
  if (((bits(r, 22, 22) << 5) | bits(r, 27, 27)) == 0) return VOp::vperm128;
  switch ((bits(r, 22, 25) << 2) | bits(r, 27, 27)) {
    case 0b000001: return VOp::vaddfp128;    case 0b000101: return VOp::vsubfp128;
    case 0b001001: return VOp::vmulfp128;    case 0b001101: return VOp::vmaddfp128;
    case 0b010001: return VOp::vmaddcfp128;  case 0b010101: return VOp::vnmsubfp128;
    case 0b011001: return VOp::vmsum3fp128;  case 0b011101: return VOp::vmsum4fp128;
    case 0b100000: return VOp::vpkshss128;   case 0b100001: return VOp::vand128;
    case 0b100100: return VOp::vpkshus128;   case 0b100101: return VOp::vandc128;
    case 0b101000: return VOp::vpkswss128;   case 0b101001: return VOp::vnor128;
    case 0b101100: return VOp::vpkswus128;   case 0b101101: return VOp::vor128;
    case 0b110000: return VOp::vpkuhum128;   case 0b110001: return VOp::vxor128;
    case 0b110100: return VOp::vpkuhus128;   case 0b110101: return VOp::vsel128;
    case 0b111000: return VOp::vpkuwum128;   case 0b111001: return VOp::vslo128;
    case 0b111100: return VOp::vpkuwus128;   case 0b111101: return VOp::vsro128;
  }
  return VOp::unknown;
}

static VOp decode6(uint32_t r) {
  if (((bits(r, 21, 22) << 5) | bits(r, 26, 27)) == 0b0100001) return VOp::vpermwi128;
  switch ((bits(r, 21, 23) << 4) | bits(r, 26, 27)) {
    case 0b1100001: return VOp::vpkd3d128;
    case 0b1110001: return VOp::vrlimi128;
  }
  switch (bits(r, 21, 27)) {
    case 0b0100011: return VOp::vcfpsxws128;  case 0b0100111: return VOp::vcfpuxws128;
    case 0b0101011: return VOp::vcsxwfp128;   case 0b0101111: return VOp::vcuxwfp128;
    case 0b0110011: return VOp::vrfim128;     case 0b0110111: return VOp::vrfin128;
    case 0b0111011: return VOp::vrfip128;     case 0b0111111: return VOp::vrfiz128;
    case 0b1100011: return VOp::vrefp128;     case 0b1100111: return VOp::vrsqrtefp128;
    case 0b1101011: return VOp::vexptefp128;  case 0b1101111: return VOp::vlogefp128;
    case 0b1110011: return VOp::vspltw128;    case 0b1110111: return VOp::vspltisw128;
    case 0b1111111: return VOp::vupkd3d128;
  }
  switch ((bits(r, 22, 24) << 3) | bits(r, 27, 27)) {
    case 0b000000: return VOp::vcmpeqfp128;  case 0b001000: return VOp::vcmpgefp128;
    case 0b010000: return VOp::vcmpgtfp128;  case 0b011000: return VOp::vcmpbfp128;
    case 0b100000: return VOp::vcmpequw128;
  }
  switch ((bits(r, 22, 25) << 2) | bits(r, 27, 27)) {
    case 0b000101: return VOp::vrlw128;    case 0b001101: return VOp::vslw128;
    case 0b010101: return VOp::vsraw128;   case 0b011101: return VOp::vsrw128;
    case 0b101000: return VOp::vmaxfp128;  case 0b101100: return VOp::vminfp128;
    case 0b110000: return VOp::vmrghw128;  case 0b110100: return VOp::vmrglw128;
    case 0b111000: return VOp::vupkhsb128; case 0b111100: return VOp::vupklsb128;
  }
  return VOp::unknown;
}

VOp decode_vmx(uint32_t raw) {
  switch (raw >> 26) {
    case 4: return decode4(raw);
    case 5: return decode5(raw);
    case 6: return decode6(raw);
    case 31: return decode31(raw);
  }
  return VOp::unknown;
}

}  // namespace fsr::ppc
