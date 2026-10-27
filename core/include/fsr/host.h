// FIFA STREET Recompiled - host side of the runtime: guest memory, module
// binding, kernel HLE dispatch, input state. Used by the Android app and the
// host test tools.
// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "fsr/runtime.h"
#include "fsr/xex.h"

namespace fsr {

struct GuestFault : std::runtime_error {
  uint32_t addr;
  explicit GuestFault(uint32_t a, const std::string& m) : std::runtime_error(m), addr(a) {}
};

// XInput-compatible pad state written by the Android input layer.
struct PadState {
  uint16_t buttons = 0;  // XINPUT_GAMEPAD_* bits
  uint8_t left_trigger = 0, right_trigger = 0;
  int16_t lx = 0, ly = 0, rx = 0, ry = 0;
  bool connected = true;
};
enum PadButton : uint16_t {
  kPadUp = 0x0001, kPadDown = 0x0002, kPadLeft = 0x0004, kPadRight = 0x0008,
  kPadStart = 0x0010, kPadBack = 0x0020, kPadLThumb = 0x0040, kPadRThumb = 0x0080,
  kPadLB = 0x0100, kPadRB = 0x0200, kPadA = 0x1000, kPadB = 0x2000, kPadX = 0x4000, kPadY = 0x8000,
};

class Runtime {
 public:
  Runtime();
  ~Runtime();
  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  bool init(std::string& err);
  uint8_t* base() const { return mem_; }

  // Map a loaded XEX image and bind the recompiled module generated from it.
  bool load(const XexModule& xex, const RecompModule* mod, std::string& err);
  const RecompModule* module() const { return mod_; }

  // Call guest code at addr on the current host thread.
  bool call(uint32_t addr, const std::vector<uint64_t>& args, uint64_t* ret, std::string& err);
  uint32_t export_addr(const std::string& name) const;

  // Simple guest heap (bump allocator in 0x40000000..0x7F000000).
  uint32_t alloc(uint32_t size, uint32_t align = 16);

  GuestFunc lookup(uint32_t addr) const;

  // Host services.
  std::function<void(const std::string&)> log;
  void logf(const char* fmt, ...);
  PadState pad() const;
  void set_pad(const PadState& p);
  uint64_t kernel_calls() const { return kernel_calls_.load(); }
  std::set<std::string> unimplemented_imports() const;

  static Runtime* current();

  // Used by kernel.cc.
  void note_unimplemented_import(const std::string& name);
  std::atomic<uint64_t> kernel_calls_{0};

 private:
  uint8_t* mem_ = nullptr;
  size_t mem_size_ = 0;
  const RecompModule* mod_ = nullptr;
  uint32_t heap_next_ = 0x40000000u;
  std::mutex heap_mu_;
  mutable std::mutex pad_mu_;
  PadState pad_;
  mutable std::mutex imp_mu_;
  std::set<std::string> unimpl_imports_;
};

// HLE kernel dispatch (kernel.cc).
using HleFn = void (*)(PPCContext&, uint8_t*, Runtime&);
HleFn find_hle(const std::string& lib, uint16_t ordinal, std::string* name_out);
std::vector<std::string> implemented_hle_names();

}  // namespace fsr
