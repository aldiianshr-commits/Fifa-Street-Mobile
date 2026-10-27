// VMX128 encoding/semantics check shared by the host test and the Android
// on-device self-test. Runs core/tests/homebrew/vmx128.S (hand-encoded VMX128
// words) through the recompiled module and recomputes every result with
// plain scalar code.
// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "fsr/bytes.h"
#include "fsr/host.h"

namespace fsr_tests {

// VMX128 encoding/semantics check (core/tests/homebrew/vmx128.S): expected
// values are recomputed here with plain scalar code.
inline int check_vmx128(fsr::Runtime& rt, std::string* report) {
  char line[160];
  std::string err;
  uint32_t fn = rt.export_addr("fsr_vmx128_test");
  if (!fn) { *report += "vmx128 test entry missing\n"; return 1; }
  const float f0[4] = {1.5f, -2.25f, 8.0f, 0.125f}, f1[4] = {0.5f, 3.0f, -1.0f, 0.125f};
  const float f2[4] = {2.0f, -0.5f, 4.0f, 1.0e10f};
  const int32_t i3[4] = {-123456, 0x7F80FF01, 77, -1};
  const uint8_t perm[16] = {0, 17, 2, 19, 31, 30, 29, 28, 4, 5, 6, 7, 16, 16, 15, 15};
  uint32_t in = rt.alloc(80, 16), out = rt.alloc(16 * 17, 16);
  auto fbits = [](float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; };
  for (int i = 0; i < 4; i++) {
    fsr::wr_be32(rt.base() + in + 4 * i, fbits(f0[i]));
    fsr::wr_be32(rt.base() + in + 16 + 4 * i, fbits(f1[i]));
    fsr::wr_be32(rt.base() + in + 32 + 4 * i, fbits(f2[i]));
    fsr::wr_be32(rt.base() + in + 48 + 4 * i, uint32_t(i3[i]));
  }
  std::memcpy(rt.base() + in + 64, perm, 16);
  uint64_t cr = 0;
  if (!rt.call(fn, {in, out}, &cr, err)) { *report += "vmx128 call failed: " + err + "\n"; return 1; }
  uint32_t exp[16][4];
  uint8_t bytes0[16], bytes1[16], bytes3[16];
  for (int i = 0; i < 4; i++) {
    fsr::wr_be32(bytes0 + 4 * i, fbits(f0[i]));
    fsr::wr_be32(bytes1 + 4 * i, fbits(f1[i]));
    fsr::wr_be32(bytes3 + 4 * i, uint32_t(i3[i]));
  }
  float d3 = f0[0] * f1[0] + f0[1] * f1[1] + f0[2] * f1[2];
  float d4 = f2[0] * f1[0] + f2[1] * f1[1] + f2[2] * f1[2] + f2[3] * f1[3];
  for (int i = 0; i < 4; i++) {
    exp[0][i] = fbits(f0[i] + f1[i]);
    exp[1][i] = fbits(f0[i] * f2[i]);
    exp[2][i] = fbits(std::fma(f0[i], f1[i], f2[i]));
    exp[3][i] = fbits(d3);
    exp[4][i] = fbits(d4);
    exp[5][i] = fbits(f0[3 - i]);
    exp[6][i] = fbits(i == 0 ? f0[1] : i == 2 ? f0[3] : f2[i]);
    exp[7][i] = fbits(f1[2]);
    exp[8][i] = f0[i] > f1[i] ? 0xFFFFFFFFu : 0;
    exp[9][i] = f0[i] > f1[i] ? fbits(f1[i]) : fbits(f0[i]);
    exp[12][i] = fbits(float(i3[i]) / 16.0f);
    double v = double(f0[i]) * 4.0;
    exp[13][i] = uint32_t(int32_t(v));
    exp[15][i] = uint32_t(-3);
  }
  uint8_t pb[16], sb[16];
  for (int i = 0; i < 16; i++) {
    int k = perm[i] & 31;
    pb[i] = k < 16 ? bytes0[k] : bytes1[k - 16];
    int s = i + 6;
    sb[i] = s < 16 ? bytes0[s] : bytes1[s - 16];
  }
  for (int i = 0; i < 4; i++) {
    exp[10][i] = fsr::rd_be32(pb + 4 * i);
    exp[11][i] = fsr::rd_be32(sb + 4 * i);
    uint16_t lo = uint16_t(int16_t(int8_t(bytes3[2 * i + 1]))), hi = uint16_t(int16_t(int8_t(bytes3[2 * i])));
    exp[14][i] = uint32_t(hi) << 16 | lo;
  }
  int bad = 0;
  for (int v = 0; v < 16; v++)
    for (int i = 0; i < 4; i++) {
      uint32_t g = fsr::rd_be32(rt.base() + out + 16 * v + 4 * i);
      if (g != exp[v][i]) { bad++; std::snprintf(line, sizeof line, "  vmx128 out[%d].%d expected %08X got %08X\n", v, i, exp[v][i], g); *report += line; }
    }
  bool any = false, all = true;
  for (int i = 0; i < 4; i++) { any |= f0[i] > f1[i]; all &= f0[i] > f1[i]; }
  uint32_t cr6 = (uint32_t(cr) >> 4) & 0xF, cr6_exp = (all ? 8u : 0u) | (!any ? 2u : 0u);
  if (cr6 != cr6_exp) { bad++; std::snprintf(line, sizeof line, "  vmx128 CR6 expected %X got %X\n", cr6_exp, cr6); *report += line; }
  std::snprintf(line, sizeof line, "vmx128 (hand-encoded): %d/65 checks passed\n", 65 - bad);
  *report += line;
  return bad;
}

}  // namespace fsr_tests
