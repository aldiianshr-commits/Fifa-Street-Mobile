// Differential test: the same C sources run (a) natively and (b) as
// PowerPC code that went through mkxex -> XEX loader -> fsr-recomp -> host
// compiler. Results must match bit-for-bit.
//   homebrew_test <hb-dir>
// SPDX-License-Identifier: MIT
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "fsr/bytes.h"
#include "fsr/host.h"
#include "fsr/xex.h"

extern "C" {
uint32_t native_selftest(uint32_t* results);
uint32_t native32_selftest(uint32_t* results);
uint32_t native_vectest(uint32_t* results);
uint32_t native_vectest_dump(uint32_t* out);
uint32_t native_frame(uint32_t* fb, int32_t w, int32_t h, uint32_t frame);
void native_demo_reset(void);
// Shims used by the native build of demo.c.
static fsr::PadState g_native_pad;
uint32_t native_XamInputGetState(uint32_t, uint32_t, void* st) {
  struct S { uint32_t packet; uint16_t buttons; uint8_t lt, rt; int16_t lx, ly, rx, ry; };
  S* s = static_cast<S*>(st);
  s->packet = 0; s->buttons = g_native_pad.buttons; s->lt = g_native_pad.left_trigger; s->rt = g_native_pad.right_trigger;
  s->lx = g_native_pad.lx; s->ly = g_native_pad.ly; s->rx = g_native_pad.rx; s->ry = g_native_pad.ry;
  return 0;
}
uint32_t native_DbgPrint(const char*, ...) { return 0; }
}
extern const fsr::RecompModule fsr_module_hb64;
extern const fsr::RecompModule fsr_module_hb32;

constexpr int kResults = 24;

#include "homebrew/vmx128_check.h"

static int run_vmx128(fsr::Runtime& rt, const char* xex_name) {
  std::string report;
  int bad = fsr_tests::check_vmx128(rt, &report);
  std::printf("[%s] %s", xex_name, report.c_str());
  return bad ? 1 : 0;
}
using SelftestFn = uint32_t (*)(uint32_t*);

static int run(const std::string& dir, const char* xex_name, const fsr::RecompModule* mod, SelftestFn native_fn) {
  int fails = 0;
  std::vector<uint8_t> file;
  std::string err;
  fsr::XexModule x;
  if (!fsr::read_file(dir + "/" + xex_name, file, &err) || !x.parse(file, err) ||
      x.load_image(nullptr, err) != fsr::LoadStatus::Ok) {
    std::printf("[%s] load failed: %s\n", xex_name, err.c_str());
    return 1;
  }
  fsr::Runtime rt;
  std::vector<std::string> logs;
  rt.log = [&](const std::string& s) { logs.push_back(s); };
  if (!rt.init(err) || !rt.load(x, mod, err)) { std::printf("[%s] runtime: %s\n", xex_name, err.c_str()); return 1; }

  // 1) Self-test arithmetic/control-flow battery.
  uint32_t native_res[kResults] = {}, guest_res[kResults] = {};
  uint32_t native_hash = native_fn(native_res);
  uint32_t buf = rt.alloc(sizeof guest_res);
  uint64_t ret = 0;
  uint32_t entry = rt.export_addr("fsr_selftest");
  if (!entry || !rt.call(entry, {buf}, &ret, err)) { std::printf("[%s] selftest call failed: %s\n", xex_name, err.c_str()); return 1; }
  for (int i = 0; i < kResults; i++) {
    guest_res[i] = fsr::rd_be32(rt.base() + buf + 4 * i);
    if (guest_res[i] != native_res[i]) {
      std::printf("[%s] result[%d] native=%08X recompiled=%08X  MISMATCH\n", xex_name, i, native_res[i], guest_res[i]);
      fails++;
    }
  }
  if (uint32_t(ret) != native_hash) { std::printf("[%s] hash mismatch %08X vs %08X\n", xex_name, uint32_t(ret), native_hash); fails++; }
  else std::printf("[%s] selftest: %d/%d results identical (hash %08X)\n", xex_name, kResults - fails, kResults, native_hash);

  // 1b) Vector (AltiVec/VMX) battery.
  {
    constexpr int kV = 12;
    uint32_t nv[kV] = {};
    uint32_t nvh = native_vectest(nv);
    uint32_t vbuf = rt.alloc(4 * kV);
    uint64_t vret = 0;
    uint32_t ventry = rt.export_addr("fsr_vectest");
    if (!ventry || !rt.call(ventry, {vbuf}, &vret, err)) {
      std::printf("[%s] vectest call failed: %s\n", xex_name, err.c_str());
      fails++;
    } else {
      int bad = 0;
      for (int i = 0; i < kV; i++) {
        uint32_t g = fsr::rd_be32(rt.base() + vbuf + 4 * i);
        if (g != nv[i]) { std::printf("[%s] vec[%d] native=%08X recompiled=%08X  MISMATCH\n", xex_name, i, nv[i], g); bad++; }
      }
      if (uint32_t(vret) != nvh) bad++;
      if (bad) {
        fails++;
        // Word-level diff of the raw vector outputs to pinpoint the instruction.
        uint32_t nd[136] = {}, dbuf = rt.alloc(4 * 136);
        native_vectest_dump(nd);
        uint32_t dfn = rt.export_addr("fsr_vectest_dump");
        if (dfn && rt.call(dfn, {dbuf}, nullptr, err))
          for (int i = 0; i < 136; i++) {
            uint32_t g = fsr::rd_be32(rt.base() + dbuf + 4 * i);
            if (g != nd[i]) std::printf("    raw[%s %d] native=%08X recompiled=%08X\n", i < 40 ? "intrin" : "intrin2", i < 40 ? i : i - 40, nd[i], g);
          }
      }
      std::printf("[%s] vectest (AltiVec): %d/%d results identical\n", xex_name, kV - (bad > kV ? kV : bad), kV);
    }
  }

  fails += run_vmx128(rt, xex_name);

  // 2) Render demo with scripted input; compare framebuffers every frame.
  const int W = 320, H = 180;
  uint32_t fb_guest = rt.alloc(W * H * 4, 4096);
  std::vector<uint32_t> fb_native(W * H);
  uint32_t frame_fn = rt.export_addr("fsr_frame");
  native_demo_reset();
  int frame_fail = 0;
  for (uint32_t frame = 0; frame < 24; frame++) {
    fsr::PadState p;
    p.buttons = uint16_t((frame & 3) == 1 ? fsr::kPadA : 0) | uint16_t(frame > 10 ? fsr::kPadRight : 0) |
                uint16_t(frame == 5 ? fsr::kPadY | fsr::kPadLB : 0);
    p.lx = int16_t(frame < 8 ? -30000 : 0);
    p.ly = int16_t(frame * 1500);
    p.left_trigger = uint8_t(frame * 10);
    rt.set_pad(p);
    g_native_pad = p;
    uint32_t nb = native_frame(fb_native.data(), W, H, frame);
    uint64_t gb = 0;
    if (!rt.call(frame_fn, {fb_guest, W, H, frame}, &gb, err)) { std::printf("[%s] frame call failed: %s\n", xex_name, err.c_str()); return 1; }
    bool same = uint32_t(gb) == nb;
    for (int i = 0; i < W * H && same; i++) same = fsr::rd_be32(rt.base() + fb_guest + 4 * i) == fb_native[i];
    if (!same) frame_fail++;
  }
  if (frame_fail) { std::printf("[%s] demo: %d/24 frames differ\n", xex_name, frame_fail); fails++; }
  else std::printf("[%s] demo: 24/24 frames identical (input via XamInputGetState HLE, %llu kernel calls)\n", xex_name,
                   (unsigned long long)rt.kernel_calls());
  bool saw_dbg = false;
  for (auto& l : logs) saw_dbg |= l.find("homebrew demo running: 320x180") != std::string::npos;
  if (!saw_dbg) { std::printf("[%s] DbgPrint HLE output missing\n", xex_name); fails++; }
  if (!rt.unimplemented_imports().empty()) { std::printf("[%s] unexpected unimplemented imports\n", xex_name); fails++; }
  return fails;
}

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  int fails = run(argv[1], "hb64.xex", &fsr_module_hb64, native_selftest);
  // The 32-bit build uses the same native results; it has its own module.
  fails += run(argv[1], "hb32.xex", &fsr_module_hb32, native32_selftest);
  std::printf("homebrew_test: %s\n", fails ? "FAILED" : "PASSED");
  return fails ? 1 : 0;
}
