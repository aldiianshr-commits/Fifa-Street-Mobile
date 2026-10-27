// SPDX-License-Identifier: MIT
#include <sys/mman.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "fsr/bytes.h"
#include "fsr/crypto.h"
#include "fsr/host.h"

namespace fsr {

namespace {
thread_local Runtime* t_runtime = nullptr;
thread_local Runtime* t_stack_owner = nullptr;
thread_local uint32_t t_stack = 0;
constexpr uint64_t kGuestSpace = 0x100000000ull;  // 4 GiB
constexpr uint32_t kHeapEnd = 0x7F000000u;
constexpr uint32_t kStackSize = 1u << 20;
}  // namespace

Runtime::Runtime() = default;
Runtime::~Runtime() {
  if (mem_) munmap(mem_, mem_size_);
}

Runtime* Runtime::current() { return t_runtime; }

bool Runtime::init(std::string& err) {
  if (mem_) return true;
  void* p = mmap(nullptr, kGuestSpace, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (p == MAP_FAILED) {
    err = "cannot reserve 4 GiB guest address space";
    return false;
  }
  mem_ = static_cast<uint8_t*>(p);
  mem_size_ = kGuestSpace;
  return true;
}

void Runtime::logf(const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (log) log(buf);
  else std::fprintf(stderr, "%s\n", buf);
}

PadState Runtime::pad() const { std::lock_guard<std::mutex> l(pad_mu_); return pad_; }
void Runtime::set_pad(const PadState& p) { std::lock_guard<std::mutex> l(pad_mu_); pad_ = p; }

void Runtime::note_unimplemented_import(const std::string& name) {
  std::lock_guard<std::mutex> l(imp_mu_);
  if (unimpl_imports_.insert(name).second) logf("[kernel] unimplemented import: %s", name.c_str());
}
std::set<std::string> Runtime::unimplemented_imports() const {
  std::lock_guard<std::mutex> l(imp_mu_);
  return unimpl_imports_;
}

uint32_t Runtime::alloc(uint32_t size, uint32_t align) {
  std::lock_guard<std::mutex> l(heap_mu_);
  uint32_t a = (heap_next_ + align - 1) & ~(align - 1);
  if (uint64_t(a) + size > kHeapEnd) return 0;
  heap_next_ = a + size;
  std::memset(mem_ + a, 0, size);
  return a;
}

bool Runtime::load(const XexModule& xex, const RecompModule* mod, std::string& err) {
  if (!mem_) { err = "runtime not initialised"; return false; }
  if (!xex.image_loaded()) { err = "XEX image not loaded"; return false; }
  const auto& img = xex.image();
  if (mod) {
    uint8_t sha[20];
    Sha1::hash(img.data(), img.size(), sha);
    if (mod->image_base != xex.base() || mod->image_size != img.size() || std::memcmp(sha, mod->image_sha1, 20) != 0) {
      err = "this XEX does not match the recompiled module '" + std::string(mod->name) +
            "' (different build/region/version). Re-run fsr-recomp on this exact executable.";
      return false;
    }
  }
  std::memcpy(mem_ + xex.base(), img.data(), img.size());
  mod_ = mod;

  // Resolve import records: IAT slots of function imports point at their
  // thunk; pure variable imports get zeroed storage in the guest heap.
  for (auto& lib : xex.info().imports) {
    for (size_t i = 0; i < lib.records.size(); i++) {
      const auto& rec = lib.records[i];
      if (rec.type != 0) continue;
      uint32_t value;
      if (i + 1 < lib.records.size() && lib.records[i + 1].type == 1 && lib.records[i + 1].ordinal == rec.ordinal) {
        value = lib.records[i + 1].record_addr;
      } else {
        value = alloc(256);
      }
      wr_be32(mem_ + rec.record_addr, value);
    }
  }
  return true;
}

GuestFunc Runtime::lookup(uint32_t addr) const {
  if (!mod_) return nullptr;
  const FuncEntry* b = mod_->funcs;
  const FuncEntry* e = b + mod_->func_count;
  auto it = std::lower_bound(b, e, addr, [](const FuncEntry& f, uint32_t a) { return f.addr < a; });
  return (it != e && it->addr == addr) ? it->fn : nullptr;
}

uint32_t Runtime::export_addr(const std::string& name) const {
  if (!mod_) return 0;
  for (uint32_t i = 0; i < mod_->export_count; i++)
    if (name == mod_->exports[i].name) return mod_->exports[i].addr;
  return 0;
}

bool Runtime::call(uint32_t addr, const std::vector<uint64_t>& args, uint64_t* ret, std::string& err) {
  GuestFunc fn = lookup(addr);
  if (!fn) { err = "no recompiled function at " + hex32(addr); return false; }
  if (t_stack_owner != this || !t_stack) {
    t_stack = alloc(kStackSize, 4096);
    t_stack_owner = this;
  }
  uint32_t stack = t_stack;
  if (!stack) { err = "guest heap exhausted"; return false; }
  PPCContext ctx{};
  ctx.runtime = this;
  ctx.reserve_addr = ~0ull;
  ctx.r[1] = stack + kStackSize - 0x100;
  ctx.r[12] = addr;  // ELFv2 global entry convention (harmless otherwise)
  for (size_t i = 0; i < args.size() && i < 8; i++) ctx.r[3 + i] = args[i];
  ctx.lr = 0;
  Runtime* prev = t_runtime;
  t_runtime = this;
  bool ok = true;
  try {
    fn(ctx, mem_);
  } catch (const GuestFault& f) {
    err = f.what();
    ok = false;
  }
  t_runtime = prev;
  if (ret) *ret = ctx.r[3];
  return ok;
}

// ---------------------------------------------------------------- services
void fsr_call_indirect(PPCContext& ctx, uint8_t* base, uint32_t addr) {
  auto* rt = static_cast<Runtime*>(ctx.runtime);
  GuestFunc fn = rt ? rt->lookup(addr) : nullptr;
  if (!fn) throw GuestFault(addr, "indirect branch to unknown code address 0x" + hex32(addr));
  fn(ctx, base);
}

void fsr_kernel_call(PPCContext& ctx, uint8_t* base, uint32_t lib, uint32_t ordinal) {
  auto* rt = static_cast<Runtime*>(ctx.runtime);
  rt->kernel_calls_++;
  const RecompModule* m = rt->module();
  std::string libname = (m && lib < m->lib_count) ? m->libs[lib] : "?";
  std::string name;
  HleFn fn = find_hle(libname, uint16_t(ordinal), &name);
  if (fn) {
    fn(ctx, base, *rt);
    return;
  }
  rt->note_unimplemented_import(libname + "!" + (name.empty() ? "#" + std::to_string(ordinal) : name));
  ctx.r[3] = 0xC0000002u;  // STATUS_NOT_IMPLEMENTED
}

void fsr_trap(PPCContext&, uint8_t*, uint32_t addr) {
  throw GuestFault(addr, "guest trap instruction at 0x" + hex32(addr));
}

void fsr_syscall(PPCContext&, uint8_t*, uint32_t addr) {
  throw GuestFault(addr, "guest 'sc' (syscall) at 0x" + hex32(addr) + " is not supported");
}

void fsr_unimplemented(PPCContext&, uint8_t*, uint32_t addr, uint32_t raw) {
  throw GuestFault(addr, "untranslated instruction 0x" + hex32(raw) + " at 0x" + hex32(addr) +
                             " (VMX/VMX128 or unknown opcode)");
}

uint64_t fsr_timebase() {
  // Xenon time base runs at 49.875 MHz.
  using namespace std::chrono;
  auto ns = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
  return uint64_t((unsigned __int128)uint64_t(ns) * 49875 / 1000000);
}

}  // namespace fsr
