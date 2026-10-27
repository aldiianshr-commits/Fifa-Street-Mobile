// SPDX-License-Identifier: MIT
#include "fsr/recomp.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <sstream>

#include "fsr/bytes.h"
#include "fsr/crypto.h"
#include "fsr/ppc.h"

namespace fsr {
using namespace ppc;

namespace {

std::string hx(uint64_t v) {
  char b[32];
  std::snprintf(b, sizeof b, "0x%llX", (unsigned long long)v);
  return b;
}
std::string hx32(uint32_t v) {
  char b[16];
  std::snprintf(b, sizeof b, "%08X", v);
  return b;
}
std::string sdec(int64_t v) {
  // Emit as a uint64_t-compatible literal that preserves sign extension.
  char b[48];
  if (v < 0) std::snprintf(b, sizeof b, "uint64_t(int64_t(%lld))", (long long)v);
  else std::snprintf(b, sizeof b, "uint64_t(%lld)", (long long)v);
  return b;
}
std::string R(uint32_t n) { return "ctx.r[" + std::to_string(n) + "]"; }
std::string F(uint32_t n) { return "ctx.f[" + std::to_string(n) + "].d"; }
std::string FU(uint32_t n) { return "ctx.f[" + std::to_string(n) + "].u"; }
std::string RA0(uint32_t n) { return n ? R(n) : std::string("uint64_t(0)"); }
std::string fname(uint32_t a) { return "sub_" + hx32(a); }

struct Code {
  const uint8_t* image;
  uint32_t base, size;
  bool in(uint32_t a) const { return a >= base && uint64_t(a) + 4 <= uint64_t(base) + size; }
  uint32_t word(uint32_t a) const { return in(a) ? rd_be32(image + (a - base)) : 0; }
};

bool in_ranges(const RecompInput& in, uint32_t a) {
  for (auto& r : in.code_ranges)
    if (a >= r.first && a < r.second) return true;
  return false;
}

uint32_t range_end(const RecompInput& in, uint32_t a) {
  for (auto& r : in.code_ranges)
    if (a >= r.first && a < r.second) return r.second;
  return a;
}

bool is_terminator(uint32_t raw) {
  Op op = decode(raw);
  if (op == Op::b) return !f_lk(raw);
  if ((op == Op::bclr || op == Op::bcctr) && !f_lk(raw)) return (f_bo(raw) & 0x14) == 0x14;
  return false;
}

}  // namespace

RecompInput make_input(const XexModule& xex, const std::string& module_name) {
  RecompInput in;
  in.module_name = module_name;
  in.image = xex.image().data();
  in.base = xex.base();
  in.size = uint32_t(xex.image().size());
  in.entry_point = xex.info().entry_point;
  for (auto& s : xex.sections())
    if (s.executable()) in.code_ranges.emplace_back(xex.base() + s.va, xex.base() + s.va + s.vsize);
  in.pdata = xex.pdata();
  for (auto& lib : xex.info().imports) {
    uint32_t li = uint32_t(in.libs.size());
    in.libs.push_back(lib.name);
    for (auto& rec : lib.records)
      if (rec.type == 1) in.thunks.push_back({rec.record_addr, li, rec.ordinal, rec.name});
  }
  return in;
}

std::vector<FunctionInfo> discover_functions(const RecompInput& in) {
  Code code{in.image, in.base, in.size};
  std::set<uint32_t> thunk_addrs;
  for (auto& t : in.thunks) thunk_addrs.insert(t.addr);

  // pdata functions define authoritative bounds.
  std::vector<FunctionInfo> pd;
  for (auto& f : in.pdata) {
    if (!f.size || !in_ranges(in, f.start) || thunk_addrs.count(f.start)) continue;
    pd.push_back({f.start, f.start + f.size, true, false});
  }
  std::sort(pd.begin(), pd.end(), [](auto& a, auto& b) { return a.start < b.start; });
  auto containing_pdata = [&](uint32_t a) -> const FunctionInfo* {
    auto it = std::upper_bound(pd.begin(), pd.end(), a, [](uint32_t v, const FunctionInfo& f) { return v < f.start; });
    if (it == pd.begin()) return nullptr;
    --it;
    return a < it->end ? &*it : nullptr;
  };

  std::set<uint32_t> seeds;
  for (auto& f : pd) seeds.insert(f.start);
  if (in.entry_point && in_ranges(in, in.entry_point)) seeds.insert(in.entry_point);
  for (uint32_t a : in.extra_functions)
    if (in_ranges(in, a) && !thunk_addrs.count(a)) seeds.insert(a);
  for (auto& e : in.exports)
    if (in_ranges(in, e.second) && !thunk_addrs.count(e.second)) seeds.insert(e.second);

  // Bounds for a non-pdata entry: linear scan to a terminator past every
  // local forward branch, stopping at the next pdata function.
  auto scan_end = [&](uint32_t start) -> uint32_t {
    if (auto* f = containing_pdata(start)) return f->end;
    uint32_t limit = range_end(in, start);
    auto next = std::upper_bound(pd.begin(), pd.end(), start, [](uint32_t v, const FunctionInfo& f) { return v < f.start; });
    if (next != pd.end() && next->start < limit) limit = next->start;
    uint32_t furthest = start;
    for (uint32_t a = start; a < limit; a += 4) {
      uint32_t raw = code.word(a);
      Op op = decode(raw);
      if (op == Op::bc || (op == Op::b && !f_lk(raw))) {
        uint32_t t = branch_target(raw, a);
        if (t > a && t < limit && t - a < 0x10000) furthest = std::max(furthest, t);
      }
      if (op == Op::invalid && raw == 0) return a;  // padding
      if (is_terminator(raw) && a >= furthest) return a + 4;
    }
    return limit;
  };

  // Iterate: follow direct calls/tail calls to discover more entries.
  std::set<uint32_t> scanned;
  bool changed = true;
  while (changed) {
    changed = false;
    std::vector<uint32_t> todo;
    for (uint32_t s : seeds)
      if (!scanned.count(s)) todo.push_back(s);
    for (uint32_t s : todo) {
      scanned.insert(s);
      uint32_t end = scan_end(s);
      for (uint32_t a = s; a < end; a += 4) {
        uint32_t raw = code.word(a);
        Op op = decode(raw);
        if (op != Op::b && op != Op::bc) continue;
        uint32_t t = branch_target(raw, a);
        if (!in_ranges(in, t) || thunk_addrs.count(t)) continue;
        bool is_call = f_lk(raw) && t != a + 4;
        bool outside = t < s || t >= end;
        if ((is_call || outside) && !seeds.count(t)) {
          seeds.insert(t);
          changed = true;
        }
      }
    }
  }

  std::vector<FunctionInfo> out;
  for (uint32_t s : seeds) {
    FunctionInfo fi;
    fi.start = s;
    const FunctionInfo* p = containing_pdata(s);
    fi.from_pdata = p && p->start == s;
    fi.end = scan_end(s);
    if (fi.end <= fi.start) fi.end = fi.start + 4;
    for (uint32_t a = fi.start; a < fi.end; a += 4) {
      uint32_t raw = code.word(a);
      if (decode(raw) == Op::bcctr && !f_lk(raw)) fi.dynamic_dispatch = true;
    }
    out.push_back(fi);
  }
  return out;
}

namespace {

class Emitter {
 public:
  Emitter(const RecompInput& in, const std::set<uint32_t>& funcs, const std::set<uint32_t>& thunks,
          RecompStats& st)
      : funcs_(funcs), thunks_(thunks), st_(st), code_{in.image, in.base, in.size} {}

  std::string function(const FunctionInfo& f, bool comments);

 private:
  std::string call_expr(uint32_t target) const {
    if (funcs_.count(target) || thunks_.count(target)) return fname(target) + "(ctx, base);";
    return "fsr::fsr_call_indirect(ctx, base, " + hx(target) + ");";
  }
  bool emit(uint32_t raw, uint32_t addr, std::ostringstream& o);
  std::string bc_cond(uint32_t raw, std::ostringstream& o, bool allow_ctr);

  const std::set<uint32_t>& funcs_;
  const std::set<uint32_t>& thunks_;
  RecompStats& st_;
  Code code_;
  const FunctionInfo* cur_ = nullptr;
  bool local(uint32_t t) const { return t >= cur_->start && t < cur_->end; }
};

std::string Emitter::bc_cond(uint32_t raw, std::ostringstream& o, bool allow_ctr) {
  uint32_t bo = f_bo(raw), bi = f_bi(raw);
  std::string cond;
  if (allow_ctr && !(bo & 4)) {
    o << "  ctx.ctr--;\n";
    cond = (bo & 2) ? "ctx.ctr == 0" : "ctx.ctr != 0";
  }
  if (!(bo & 16)) {
    std::string c = std::string((bo & 8) ? "" : "!") + "ctx.crb[" + std::to_string(bi) + "]";
    cond = cond.empty() ? c : "(" + cond + ") && " + c;
  }
  return cond.empty() ? "true" : cond;
}

std::string Emitter::function(const FunctionInfo& f, bool comments) {
  cur_ = &f;
  std::ostringstream o;
  std::set<uint32_t> labels;
  for (uint32_t a = f.start; a < f.end; a += 4) {
    uint32_t raw = code_.word(a);
    Op op = decode(raw);
    if (op == Op::b || op == Op::bc) {
      uint32_t t = branch_target(raw, a);
      if (local(t) && !(f_lk(raw))) labels.insert(t);
    }
  }
  o << "FSR_FUNC(" << fname(f.start) << ") {\n";
  o << "  uint32_t ea; uint64_t t0, t1; uint32_t target; (void)ea; (void)t0; (void)t1; (void)target;\n";
  for (uint32_t a = f.start; a < f.end; a += 4) {
    uint32_t raw = code_.word(a);
    if (f.dynamic_dispatch || labels.count(a)) o << "loc_" << hx32(a) << ":\n";
    if (raw == 0) {  // zero word: padding/data inside the function range
      st_.padding_words++;
      o << "  fsr::fsr_unimplemented(ctx, base, " << hx(a) << ", 0); // zero word (padding)\n";
      continue;
    }
    if (comments) o << "  // " << hx32(a) << ": " << disasm(raw, a) << "\n";
    st_.instructions++;
    if (emit(raw, a, o)) st_.translated++;
  }
  // Falling off the end of a function: continue into the next code.
  o << "  " << call_expr(f.end) << "\n  return;\n";
  if (f.dynamic_dispatch) {
    o << "dispatch:\n  switch (target) {\n";
    for (uint32_t a = f.start; a < f.end; a += 4) o << "    case " << hx(a) << ": goto loc_" << hx32(a) << ";\n";
    o << "    default: fsr::fsr_call_indirect(ctx, base, target); return;\n  }\n";
  }
  o << "}\n\n";
  return o.str();
}

bool Emitter::emit(uint32_t raw, uint32_t addr, std::ostringstream& o) {
  Op op = decode(raw);
  st_.op_histogram[op_name(op)]++;
  const uint32_t rt = f_rt(raw), ra = f_ra(raw), rb = f_rb(raw), rc = f_rc(raw);
  const bool rec = f_rcbit(raw);
  const std::string next = hx(addr + 4);
  auto cr0 = [&](const std::string& reg) {
    if (rec) o << "  fsr::set_cr0(ctx, " << reg << ");\n";
  };
  auto cr1 = [&]() {
    if (rec) o << "  ctx.crb[4] = ctx.crb[5] = ctx.crb[6] = 0; ctx.crb[7] = ctx.xer_so;\n";
  };
  auto ea_d = [&](bool update) {
    std::string base_reg = update ? R(ra) : RA0(ra);
    return "(" + base_reg + " + " + sdec(f_simm(raw)) + ")";
  };
  auto ea_ds = [&](bool update) {
    std::string base_reg = update ? R(ra) : RA0(ra);
    return "(" + base_reg + " + " + sdec(f_ds(raw)) + ")";
  };
  auto ea_x = [&](bool update) { return "(" + (update ? R(ra) : RA0(ra)) + " + " + R(rb) + ")"; };
  // Generic load/store emitters. `val` is C++ that consumes `ea`.
  auto load = [&](const std::string& eaexpr, bool update, const std::string& assign) {
    o << "  t0 = " << eaexpr << "; ea = uint32_t(t0);\n  " << assign << "\n";
    if (update) o << "  " << R(ra) << " = t0;\n";
  };
  auto ov_flag = [&](const std::string& expr) {
    if (f_oe(raw)) o << "  ctx.xer_ov = (" << expr << ") ? 1 : 0; ctx.xer_so |= ctx.xer_ov;\n";
  };

  switch (op) {
    // ---------------------------------------------------------------- branches
    case Op::b: {
      uint32_t t = branch_target(raw, addr);
      if (f_lk(raw)) {
        o << "  ctx.lr = " << next << ";\n";
        if (t != addr + 4) o << "  " << call_expr(t) << "\n";
      } else if (local(t)) {
        o << "  goto loc_" << hx32(t) << ";\n";
      } else {
        o << "  " << call_expr(t) << "\n  return;\n";
      }
      return true;
    }
    case Op::bc: {
      uint32_t t = branch_target(raw, addr);
      std::string c = bc_cond(raw, o, true);
      if (f_lk(raw)) {
        o << "  if (" << c << ") { ctx.lr = " << next << ";";
        if (t != addr + 4) o << " " << call_expr(t);
        o << " }\n";
      } else if (local(t)) {
        o << "  if (" << c << ") goto loc_" << hx32(t) << ";\n";
      } else {
        o << "  if (" << c << ") { " << call_expr(t) << " return; }\n";
      }
      return true;
    }
    case Op::bclr: {
      std::string c = bc_cond(raw, o, true);
      if (f_lk(raw)) {
        st_.indirect_call_sites++;
        o << "  if (" << c << ") { target = uint32_t(ctx.lr); ctx.lr = " << next
          << "; fsr::fsr_call_indirect(ctx, base, target); }\n";
      } else {
        o << "  if (" << c << ") return;\n";
      }
      return true;
    }
    case Op::bcctr: {
      std::string c = bc_cond(raw, o, false);
      st_.indirect_call_sites++;
      if (f_lk(raw)) {
        o << "  if (" << c << ") { ctx.lr = " << next
          << "; fsr::fsr_call_indirect(ctx, base, uint32_t(ctx.ctr)); }\n";
      } else {
        o << "  if (" << c << ") { target = uint32_t(ctx.ctr); goto dispatch; }\n";
      }
      return true;
    }
    case Op::sc:
      o << "  fsr::fsr_syscall(ctx, base, " << hx(addr) << ");\n";
      return true;
    case Op::tw: case Op::twi: case Op::td: case Op::tdi: {
      uint32_t to = rt;
      bool w = op == Op::tw || op == Op::twi;
      std::string a = w ? "int64_t(int32_t(" + R(ra) + "))" : "int64_t(" + R(ra) + ")";
      std::string b = (op == Op::twi || op == Op::tdi) ? "int64_t(" + std::to_string(f_simm(raw)) + ")"
                      : (w ? "int64_t(int32_t(" + R(rb) + "))" : "int64_t(" + R(rb) + ")");
      std::string ua = w ? "uint64_t(uint32_t(" + R(ra) + "))" : R(ra);
      std::string ub = (op == Op::twi || op == Op::tdi)
                           ? (w ? "uint64_t(uint32_t(int32_t(" + std::to_string(f_simm(raw)) + ")))"
                                : "uint64_t(int64_t(" + std::to_string(f_simm(raw)) + "))")
                           : (w ? "uint64_t(uint32_t(" + R(rb) + "))" : R(rb));
      std::vector<std::string> cs;
      if (to & 16) cs.push_back(a + " < " + b);
      if (to & 8) cs.push_back(a + " > " + b);
      if (to & 4) cs.push_back(a + " == " + b);
      if (to & 2) cs.push_back(ua + " < " + ub);
      if (to & 1) cs.push_back(ua + " > " + ub);
      if (cs.empty()) return true;
      o << "  if (";
      for (size_t i = 0; i < cs.size(); i++) o << (i ? " || " : "") << "(" << cs[i] << ")";
      o << ") fsr::fsr_trap(ctx, base, " << hx(addr) << ");\n";
      return true;
    }
    // ---------------------------------------------------------------- CR ops
    case Op::mcrf:
      o << "  std::memcpy(ctx.crb + " << 4 * f_crfd(raw) << ", ctx.crb + " << 4 * f_crfs(raw) << ", 4);\n";
      return true;
    case Op::crand: case Op::cror: case Op::crxor: case Op::crnand: case Op::crnor:
    case Op::creqv: case Op::crandc: case Op::crorc: {
      std::string A = "ctx.crb[" + std::to_string(ra) + "]", B = "ctx.crb[" + std::to_string(rb) + "]";
      std::string e;
      switch (op) {
        case Op::crand: e = A + " & " + B; break;
        case Op::cror: e = A + " | " + B; break;
        case Op::crxor: e = A + " ^ " + B; break;
        case Op::crnand: e = "!(" + A + " & " + B + ")"; break;
        case Op::crnor: e = "!(" + A + " | " + B + ")"; break;
        case Op::creqv: e = "!(" + A + " ^ " + B + ")"; break;
        case Op::crandc: e = A + " & !" + B; break;
        default: e = A + " | !" + B; break;
      }
      o << "  ctx.crb[" << rt << "] = uint8_t((" << e << ") & 1);\n";
      return true;
    }
    case Op::mfcr: o << "  " << R(rt) << " = fsr::get_cr(ctx);\n"; return true;
    case Op::mtcrf: o << "  fsr::set_cr_field_bits(ctx, uint32_t(" << R(rt) << "), " << f_crm(raw) << ");\n"; return true;
    case Op::mcrxr:
      o << "  ctx.crb[" << 4 * f_crfd(raw) << "] = ctx.xer_so; ctx.crb[" << 4 * f_crfd(raw) + 1
        << "] = ctx.xer_ov; ctx.crb[" << 4 * f_crfd(raw) + 2 << "] = ctx.xer_ca; ctx.crb["
        << 4 * f_crfd(raw) + 3 << "] = 0; ctx.xer_so = ctx.xer_ov = ctx.xer_ca = 0;\n";
      return true;
    case Op::isync: case Op::eieio: case Op::dcbt: case Op::dcbtst: case Op::dcbf: case Op::dcbst:
    case Op::icbi: case Op::dcbi:
      o << "  // (cache/ordering hint: no-op)\n";
      return true;
    case Op::sync: o << "  std::atomic_thread_fence(std::memory_order_seq_cst);\n"; return true;
    case Op::dcbz:
      o << "  fsr::dcbz(base, uint32_t(" << ea_x(false) << "), " << ((rt & 1) ? 128 : 32) << ");\n";
      return true;
    // ---------------------------------------------------------------- SPR
    case Op::mfspr: {
      uint32_t spr = f_spr(raw);
      if (spr == 8) o << "  " << R(rt) << " = ctx.lr;\n";
      else if (spr == 9) o << "  " << R(rt) << " = ctx.ctr;\n";
      else if (spr == 1) o << "  " << R(rt) << " = fsr::get_xer(ctx);\n";
      else if (spr == 268 || spr == 269) o << "  " << R(rt) << " = fsr::fsr_timebase()" << (spr == 269 ? " >> 32" : "") << ";\n";
      else { o << "  " << R(rt) << " = 0; // mfspr " << spr << " (unsupported SPR reads 0)\n"; }
      return true;
    }
    case Op::mtspr: {
      uint32_t spr = f_spr(raw);
      if (spr == 8) o << "  ctx.lr = " << R(rt) << ";\n";
      else if (spr == 9) o << "  ctx.ctr = " << R(rt) << ";\n";
      else if (spr == 1) o << "  fsr::set_xer(ctx, " << R(rt) << ");\n";
      else o << "  // mtspr " << spr << " ignored\n";
      return true;
    }
    case Op::mftb: o << "  " << R(rt) << " = fsr::fsr_timebase()" << (f_tbr(raw) == 269 ? " >> 32" : "") << ";\n"; return true;
    case Op::mfmsr: o << "  " << R(rt) << " = 0x8000; // MSR[EE]\n"; return true;
    case Op::mtmsr: case Op::mtmsrd: o << "  // mtmsr(d) ignored in user mode\n"; return true;
    // ---------------------------------------------------------------- integer arithmetic
    case Op::addi:
      o << "  " << R(rt) << " = " << RA0(ra) << " + " << sdec(f_simm(raw)) << ";\n";
      return true;
    case Op::addis:
      o << "  " << R(rt) << " = " << RA0(ra) << " + " << sdec(int64_t(f_simm(raw)) * 65536) << ";\n";
      return true;
    case Op::addic: case Op::addic_rc:
      o << "  " << R(rt) << " = fsr::add_carry(ctx, " << R(ra) << ", " << sdec(f_simm(raw)) << ", 0);\n";
      if (op == Op::addic_rc) o << "  fsr::set_cr0(ctx, " << R(rt) << ");\n";
      return true;
    case Op::subfic:
      o << "  " << R(rt) << " = fsr::add_carry(ctx, ~" << R(ra) << ", " << sdec(f_simm(raw)) << ", 1);\n";
      return true;
    case Op::mulli:
      o << "  " << R(rt) << " = " << R(ra) << " * " << sdec(f_simm(raw)) << ";\n";
      return true;
    case Op::add:
      o << "  t0 = " << R(ra) << "; t1 = " << R(rb) << "; " << R(rt) << " = t0 + t1;\n";
      if (f_oe(raw)) o << "  fsr::set_ov_add(ctx, t0, t1, " << R(rt) << ");\n";
      cr0(R(rt));
      return true;
    case Op::subf:
      o << "  t0 = ~" << R(ra) << "; t1 = " << R(rb) << "; " << R(rt) << " = t1 + t0 + 1;\n";
      if (f_oe(raw)) o << "  fsr::set_ov_add(ctx, t0, t1, " << R(rt) << ");\n";
      cr0(R(rt));
      return true;
    case Op::neg:
      o << "  t0 = " << R(ra) << "; " << R(rt) << " = uint64_t(0) - t0;\n";
      ov_flag("t0 == 0x8000000000000000ull");
      cr0(R(rt));
      return true;
    case Op::addc: case Op::adde: case Op::subfc: case Op::subfe: case Op::addze: case Op::addme:
    case Op::subfze: case Op::subfme: {
      std::string a = (op == Op::subfc || op == Op::subfe || op == Op::subfze || op == Op::subfme) ? "~" + R(ra) : R(ra);
      std::string b, cin;
      switch (op) {
        case Op::addc: b = R(rb); cin = "0"; break;
        case Op::adde: b = R(rb); cin = "ctx.xer_ca"; break;
        case Op::subfc: b = R(rb); cin = "1"; break;
        case Op::subfe: b = R(rb); cin = "ctx.xer_ca"; break;
        case Op::addze: case Op::subfze: b = "0"; cin = "ctx.xer_ca"; break;
        default: b = "~uint64_t(0)"; cin = "ctx.xer_ca"; break;
      }
      o << "  t0 = " << a << "; t1 = " << b << "; " << R(rt) << " = fsr::add_carry(ctx, t0, t1, " << cin << ");\n";
      if (f_oe(raw)) o << "  fsr::set_ov_add(ctx, t0, t1, " << R(rt) << ");\n";
      cr0(R(rt));
      return true;
    }
    case Op::mullw:
      o << "  " << R(rt) << " = uint64_t(int64_t(int32_t(" << R(ra) << ")) * int64_t(int32_t(" << R(rb) << ")));\n";
      ov_flag("int64_t(" + R(rt) + ") != int64_t(int32_t(" + R(rt) + "))");
      cr0(R(rt));
      return true;
    case Op::mulhw:
      o << "  " << R(rt) << " = uint64_t(int64_t(int32_t((int64_t(int32_t(" << R(ra) << ")) * int64_t(int32_t("
        << R(rb) << "))) >> 32)));\n";
      cr0(R(rt));
      return true;
    case Op::mulhwu:
      o << "  " << R(rt) << " = (uint64_t(uint32_t(" << R(ra) << ")) * uint64_t(uint32_t(" << R(rb) << "))) >> 32;\n";
      cr0(R(rt));
      return true;
    case Op::mulld:
      ov_flag("((__int128)int64_t(" + R(ra) + ") * int64_t(" + R(rb) + ")) != (__int128)int64_t(" + R(ra) + " * " + R(rb) + ")");
      o << "  " << R(rt) << " = " << R(ra) << " * " << R(rb) << ";\n";
      cr0(R(rt));
      return true;
    case Op::mulhd:
      o << "  " << R(rt) << " = uint64_t(((__int128)int64_t(" << R(ra) << ") * int64_t(" << R(rb) << ")) >> 64);\n";
      cr0(R(rt));
      return true;
    case Op::mulhdu:
      o << "  " << R(rt) << " = uint64_t(((unsigned __int128)" << R(ra) << " * " << R(rb) << ") >> 64);\n";
      cr0(R(rt));
      return true;
    case Op::divw: case Op::divwu: case Op::divd: case Op::divdu: {
      const char* fn = op == Op::divw ? "divw_s" : op == Op::divwu ? "divw_u" : op == Op::divd ? "divd_s" : "divd_u";
      if (f_oe(raw)) {
        std::string ov = op == Op::divw ? "int32_t(" + R(rb) + ") == 0 || (int32_t(" + R(ra) + ") == INT32_MIN && int32_t(" + R(rb) + ") == -1)"
                       : op == Op::divwu ? "uint32_t(" + R(rb) + ") == 0"
                       : op == Op::divd ? R(rb) + " == 0 || (int64_t(" + R(ra) + ") == INT64_MIN && int64_t(" + R(rb) + ") == -1)"
                       : R(rb) + " == 0";
        ov_flag(ov);
      }
      o << "  " << R(rt) << " = fsr::" << fn << "(" << R(ra) << ", " << R(rb) << ");\n";
      cr0(R(rt));
      return true;
    }
    // ---------------------------------------------------------------- compare
    case Op::cmp:
      if (f_l(raw)) o << "  fsr::set_cr_signed(ctx, " << f_crfd(raw) << ", int64_t(" << R(ra) << "), int64_t(" << R(rb) << "));\n";
      else o << "  fsr::set_cr_signed(ctx, " << f_crfd(raw) << ", int32_t(" << R(ra) << "), int32_t(" << R(rb) << "));\n";
      return true;
    case Op::cmpl:
      if (f_l(raw)) o << "  fsr::set_cr_unsigned(ctx, " << f_crfd(raw) << ", " << R(ra) << ", " << R(rb) << ");\n";
      else o << "  fsr::set_cr_unsigned(ctx, " << f_crfd(raw) << ", uint32_t(" << R(ra) << "), uint32_t(" << R(rb) << "));\n";
      return true;
    case Op::cmpi:
      if (f_l(raw)) o << "  fsr::set_cr_signed(ctx, " << f_crfd(raw) << ", int64_t(" << R(ra) << "), " << f_simm(raw) << ");\n";
      else o << "  fsr::set_cr_signed(ctx, " << f_crfd(raw) << ", int32_t(" << R(ra) << "), " << f_simm(raw) << ");\n";
      return true;
    case Op::cmpli:
      if (f_l(raw)) o << "  fsr::set_cr_unsigned(ctx, " << f_crfd(raw) << ", " << R(ra) << ", " << f_uimm(raw) << "u);\n";
      else o << "  fsr::set_cr_unsigned(ctx, " << f_crfd(raw) << ", uint32_t(" << R(ra) << "), " << f_uimm(raw) << "u);\n";
      return true;
    // ---------------------------------------------------------------- logical
    case Op::ori: o << "  " << R(ra) << " = " << R(rt) << " | " << hx(f_uimm(raw)) << "ull;\n"; return true;
    case Op::oris: o << "  " << R(ra) << " = " << R(rt) << " | " << hx(uint64_t(f_uimm(raw)) << 16) << "ull;\n"; return true;
    case Op::xori: o << "  " << R(ra) << " = " << R(rt) << " ^ " << hx(f_uimm(raw)) << "ull;\n"; return true;
    case Op::xoris: o << "  " << R(ra) << " = " << R(rt) << " ^ " << hx(uint64_t(f_uimm(raw)) << 16) << "ull;\n"; return true;
    case Op::andi_rc:
      o << "  " << R(ra) << " = " << R(rt) << " & " << hx(f_uimm(raw)) << "ull;\n  fsr::set_cr0(ctx, " << R(ra) << ");\n";
      return true;
    case Op::andis_rc:
      o << "  " << R(ra) << " = " << R(rt) << " & " << hx(uint64_t(f_uimm(raw)) << 16) << "ull;\n  fsr::set_cr0(ctx, " << R(ra) << ");\n";
      return true;
    case Op::and_: case Op::or_: case Op::xor_: case Op::nand: case Op::nor: case Op::eqv: case Op::andc: case Op::orc: {
      std::string S = R(rt), B = R(rb), e;
      switch (op) {
        case Op::and_: e = S + " & " + B; break;
        case Op::or_: e = S + " | " + B; break;
        case Op::xor_: e = S + " ^ " + B; break;
        case Op::nand: e = "~(" + S + " & " + B + ")"; break;
        case Op::nor: e = "~(" + S + " | " + B + ")"; break;
        case Op::eqv: e = "~(" + S + " ^ " + B + ")"; break;
        case Op::andc: e = S + " & ~" + B; break;
        default: e = S + " | ~" + B; break;
      }
      o << "  " << R(ra) << " = " << e << ";\n";
      cr0(R(ra));
      return true;
    }
    case Op::extsb: o << "  " << R(ra) << " = uint64_t(int64_t(int8_t(" << R(rt) << ")));\n"; cr0(R(ra)); return true;
    case Op::extsh: o << "  " << R(ra) << " = uint64_t(int64_t(int16_t(" << R(rt) << ")));\n"; cr0(R(ra)); return true;
    case Op::extsw: o << "  " << R(ra) << " = uint64_t(int64_t(int32_t(" << R(rt) << ")));\n"; cr0(R(ra)); return true;
    case Op::cntlzw: o << "  " << R(ra) << " = fsr::cntlz32(" << R(rt) << ");\n"; cr0(R(ra)); return true;
    case Op::cntlzd: o << "  " << R(ra) << " = fsr::cntlz64(" << R(rt) << ");\n"; cr0(R(ra)); return true;
    // ---------------------------------------------------------------- rotate / shift
    case Op::rlwinm: {
      uint64_t m = mask64(f_mb(raw) + 32, f_me(raw) + 32);
      o << "  " << R(ra) << " = fsr::dup32(fsr::rotl32(uint32_t(" << R(rt) << "), " << f_sh(raw) << ")) & " << hx(m) << "ull;\n";
      cr0(R(ra));
      return true;
    }
    case Op::rlwnm: {
      uint64_t m = mask64(f_mb(raw) + 32, f_me(raw) + 32);
      o << "  " << R(ra) << " = fsr::dup32(fsr::rotl32(uint32_t(" << R(rt) << "), uint32_t(" << R(rb) << ") & 31)) & " << hx(m) << "ull;\n";
      cr0(R(ra));
      return true;
    }
    case Op::rlwimi: {
      uint64_t m = mask64(f_mb(raw) + 32, f_me(raw) + 32);
      o << "  " << R(ra) << " = (fsr::dup32(fsr::rotl32(uint32_t(" << R(rt) << "), " << f_sh(raw) << ")) & " << hx(m)
        << "ull) | (" << R(ra) << " & " << hx(~m) << "ull);\n";
      cr0(R(ra));
      return true;
    }
    case Op::rldicl: case Op::rldicr: case Op::rldic: case Op::rldimi: {
      uint32_t sh = f_sh64(raw), mb = f_mb64(raw);
      uint64_t m = op == Op::rldicl ? mask64(mb, 63) : op == Op::rldicr ? mask64(0, mb)
                 : mask64(mb, 63 - sh);
      std::string rot = "fsr::rotl64(" + R(rt) + ", " + std::to_string(sh) + ")";
      if (op == Op::rldimi)
        o << "  " << R(ra) << " = (" << rot << " & " << hx(m) << "ull) | (" << R(ra) << " & " << hx(~m) << "ull);\n";
      else
        o << "  " << R(ra) << " = " << rot << " & " << hx(m) << "ull;\n";
      cr0(R(ra));
      return true;
    }
    case Op::rldcl: case Op::rldcr: {
      uint32_t mb = f_mb64(raw);
      uint64_t m = op == Op::rldcl ? mask64(mb, 63) : mask64(0, mb);
      o << "  " << R(ra) << " = fsr::rotl64(" << R(rt) << ", uint32_t(" << R(rb) << ") & 63) & " << hx(m) << "ull;\n";
      cr0(R(ra));
      return true;
    }
    case Op::slw: o << "  " << R(ra) << " = fsr::ppc_slw(" << R(rt) << ", " << R(rb) << ");\n"; cr0(R(ra)); return true;
    case Op::srw: o << "  " << R(ra) << " = fsr::ppc_srw(" << R(rt) << ", " << R(rb) << ");\n"; cr0(R(ra)); return true;
    case Op::sld: o << "  " << R(ra) << " = fsr::ppc_sld(" << R(rt) << ", " << R(rb) << ");\n"; cr0(R(ra)); return true;
    case Op::srd: o << "  " << R(ra) << " = fsr::ppc_srd(" << R(rt) << ", " << R(rb) << ");\n"; cr0(R(ra)); return true;
    case Op::sraw: o << "  " << R(ra) << " = fsr::ppc_sraw(ctx, " << R(rt) << ", uint32_t(" << R(rb) << ") & 63);\n"; cr0(R(ra)); return true;
    case Op::srawi: o << "  " << R(ra) << " = fsr::ppc_sraw(ctx, " << R(rt) << ", " << f_sh(raw) << ");\n"; cr0(R(ra)); return true;
    case Op::srad: o << "  " << R(ra) << " = fsr::ppc_srad(ctx, " << R(rt) << ", uint32_t(" << R(rb) << ") & 127);\n"; cr0(R(ra)); return true;
    case Op::sradi: o << "  " << R(ra) << " = fsr::ppc_srad(ctx, " << R(rt) << ", " << f_sh64(raw) << ");\n"; cr0(R(ra)); return true;
    // ---------------------------------------------------------------- integer loads
    case Op::lbz: case Op::lbzu: load(ea_d(op == Op::lbzu), op == Op::lbzu, R(rt) + " = fsr::ld8(base, ea);"); return true;
    case Op::lhz: case Op::lhzu: load(ea_d(op == Op::lhzu), op == Op::lhzu, R(rt) + " = fsr::ld16(base, ea);"); return true;
    case Op::lha: case Op::lhau: load(ea_d(op == Op::lhau), op == Op::lhau, R(rt) + " = uint64_t(int64_t(int16_t(fsr::ld16(base, ea))));"); return true;
    case Op::lwz: case Op::lwzu: load(ea_d(op == Op::lwzu), op == Op::lwzu, R(rt) + " = fsr::ld32(base, ea);"); return true;
    case Op::ld: case Op::ldu: load(ea_ds(op == Op::ldu), op == Op::ldu, R(rt) + " = fsr::ld64(base, ea);"); return true;
    case Op::lwa: load(ea_ds(false), false, R(rt) + " = uint64_t(int64_t(int32_t(fsr::ld32(base, ea))));"); return true;
    case Op::lbzx: case Op::lbzux: load(ea_x(op == Op::lbzux), op == Op::lbzux, R(rt) + " = fsr::ld8(base, ea);"); return true;
    case Op::lhzx: case Op::lhzux: load(ea_x(op == Op::lhzux), op == Op::lhzux, R(rt) + " = fsr::ld16(base, ea);"); return true;
    case Op::lhax: case Op::lhaux: load(ea_x(op == Op::lhaux), op == Op::lhaux, R(rt) + " = uint64_t(int64_t(int16_t(fsr::ld16(base, ea))));"); return true;
    case Op::lwzx: case Op::lwzux: load(ea_x(op == Op::lwzux), op == Op::lwzux, R(rt) + " = fsr::ld32(base, ea);"); return true;
    case Op::lwax: case Op::lwaux: load(ea_x(op == Op::lwaux), op == Op::lwaux, R(rt) + " = uint64_t(int64_t(int32_t(fsr::ld32(base, ea))));"); return true;
    case Op::ldx: case Op::ldux: load(ea_x(op == Op::ldux), op == Op::ldux, R(rt) + " = fsr::ld64(base, ea);"); return true;
    case Op::lhbrx: load(ea_x(false), false, R(rt) + " = fsr::ld16le(base, ea);"); return true;
    case Op::lwbrx: load(ea_x(false), false, R(rt) + " = fsr::ld32le(base, ea);"); return true;
    case Op::lwarx: load(ea_x(false), false, R(rt) + " = fsr::lwarx(ctx, base, ea);"); return true;
    case Op::ldarx: load(ea_x(false), false, R(rt) + " = fsr::ldarx(ctx, base, ea);"); return true;
    case Op::lmw:
      o << "  ea = uint32_t(" << ea_d(false) << ");\n";
      for (uint32_t r = rt, i = 0; r < 32; r++, i++) o << "  " << R(r) << " = fsr::ld32(base, ea + " << 4 * i << ");\n";
      return true;
    // ---------------------------------------------------------------- integer stores
    case Op::stb: case Op::stbu: load(ea_d(op == Op::stbu), op == Op::stbu, "fsr::st8(base, ea, uint8_t(" + R(rt) + "));"); return true;
    case Op::sth: case Op::sthu: load(ea_d(op == Op::sthu), op == Op::sthu, "fsr::st16(base, ea, uint16_t(" + R(rt) + "));"); return true;
    case Op::stw: case Op::stwu: load(ea_d(op == Op::stwu), op == Op::stwu, "fsr::st32(base, ea, uint32_t(" + R(rt) + "));"); return true;
    case Op::std: case Op::stdu: load(ea_ds(op == Op::stdu), op == Op::stdu, "fsr::st64(base, ea, " + R(rt) + ");"); return true;
    case Op::stbx: case Op::stbux: load(ea_x(op == Op::stbux), op == Op::stbux, "fsr::st8(base, ea, uint8_t(" + R(rt) + "));"); return true;
    case Op::sthx: case Op::sthux: load(ea_x(op == Op::sthux), op == Op::sthux, "fsr::st16(base, ea, uint16_t(" + R(rt) + "));"); return true;
    case Op::stwx: case Op::stwux: load(ea_x(op == Op::stwux), op == Op::stwux, "fsr::st32(base, ea, uint32_t(" + R(rt) + "));"); return true;
    case Op::stdx: case Op::stdux: load(ea_x(op == Op::stdux), op == Op::stdux, "fsr::st64(base, ea, " + R(rt) + ");"); return true;
    case Op::sthbrx: load(ea_x(false), false, "fsr::st16le(base, ea, uint16_t(" + R(rt) + "));"); return true;
    case Op::stwbrx: load(ea_x(false), false, "fsr::st32le(base, ea, uint32_t(" + R(rt) + "));"); return true;
    case Op::stwcx:
      load(ea_x(false), false, "t1 = fsr::stwcx(ctx, base, ea, uint32_t(" + R(rt) + "));");
      o << "  ctx.crb[0] = 0; ctx.crb[1] = 0; ctx.crb[2] = uint8_t(t1); ctx.crb[3] = ctx.xer_so;\n";
      return true;
    case Op::stdcx:
      load(ea_x(false), false, "t1 = fsr::stdcx(ctx, base, ea, " + R(rt) + ");");
      o << "  ctx.crb[0] = 0; ctx.crb[1] = 0; ctx.crb[2] = uint8_t(t1); ctx.crb[3] = ctx.xer_so;\n";
      return true;
    case Op::stmw:
      o << "  ea = uint32_t(" << ea_d(false) << ");\n";
      for (uint32_t r = rt, i = 0; r < 32; r++, i++) o << "  fsr::st32(base, ea + " << 4 * i << ", uint32_t(" << R(r) << "));\n";
      return true;
    // ---------------------------------------------------------------- FP loads/stores
    case Op::lfs: case Op::lfsu: load(ea_d(op == Op::lfsu), op == Op::lfsu, F(rt) + " = double(fsr::f32_from_bits(fsr::ld32(base, ea)));"); return true;
    case Op::lfd: case Op::lfdu: load(ea_d(op == Op::lfdu), op == Op::lfdu, FU(rt) + " = fsr::ld64(base, ea);"); return true;
    case Op::lfsx: case Op::lfsux: load(ea_x(op == Op::lfsux), op == Op::lfsux, F(rt) + " = double(fsr::f32_from_bits(fsr::ld32(base, ea)));"); return true;
    case Op::lfdx: case Op::lfdux: load(ea_x(op == Op::lfdux), op == Op::lfdux, FU(rt) + " = fsr::ld64(base, ea);"); return true;
    case Op::stfs: case Op::stfsu: load(ea_d(op == Op::stfsu), op == Op::stfsu, "fsr::st32(base, ea, fsr::f32_bits(float(" + F(rt) + ")));"); return true;
    case Op::stfd: case Op::stfdu: load(ea_d(op == Op::stfdu), op == Op::stfdu, "fsr::st64(base, ea, " + FU(rt) + ");"); return true;
    case Op::stfsx: case Op::stfsux: load(ea_x(op == Op::stfsux), op == Op::stfsux, "fsr::st32(base, ea, fsr::f32_bits(float(" + F(rt) + ")));"); return true;
    case Op::stfdx: case Op::stfdux: load(ea_x(op == Op::stfdux), op == Op::stfdux, "fsr::st64(base, ea, " + FU(rt) + ");"); return true;
    case Op::stfiwx: load(ea_x(false), false, "fsr::st32(base, ea, uint32_t(" + FU(rt) + "));"); return true;
    // ---------------------------------------------------------------- FP arithmetic
    case Op::fadd: case Op::fsub: case Op::fmul: case Op::fdiv: case Op::fadds: case Op::fsubs: case Op::fmuls: case Op::fdivs: {
      bool single = op == Op::fadds || op == Op::fsubs || op == Op::fmuls || op == Op::fdivs;
      std::string b = (op == Op::fmul || op == Op::fmuls) ? F(rc) : F(rb);
      const char* sym = (op == Op::fadd || op == Op::fadds) ? "+" : (op == Op::fsub || op == Op::fsubs) ? "-"
                       : (op == Op::fmul || op == Op::fmuls) ? "*" : "/";
      std::string e = F(ra) + " " + sym + " " + b;
      o << "  " << F(rt) << " = " << (single ? "fsr::round_single(" + e + ")" : e) << ";\n";
      cr1();
      return true;
    }
    case Op::fmadd: case Op::fmsub: case Op::fnmadd: case Op::fnmsub:
    case Op::fmadds: case Op::fmsubs: case Op::fnmadds: case Op::fnmsubs: {
      bool single = op == Op::fmadds || op == Op::fmsubs || op == Op::fnmadds || op == Op::fnmsubs;
      bool sub = op == Op::fmsub || op == Op::fmsubs || op == Op::fnmsub || op == Op::fnmsubs;
      bool negate = op == Op::fnmadd || op == Op::fnmadds || op == Op::fnmsub || op == Op::fnmsubs;
      std::string e = "std::fma(" + F(ra) + ", " + F(rc) + ", " + (sub ? "-" : "") + F(rb) + ")";
      if (negate) e = "-(" + e + ")";
      o << "  " << F(rt) << " = " << (single ? "fsr::round_single(" + e + ")" : e) << ";\n";
      cr1();
      return true;
    }
    case Op::fsqrt: o << "  " << F(rt) << " = std::sqrt(" << F(rb) << ");\n"; cr1(); return true;
    case Op::fsqrts: o << "  " << F(rt) << " = fsr::round_single(std::sqrt(" << F(rb) << "));\n"; cr1(); return true;
    case Op::fres: o << "  " << F(rt) << " = fsr::round_single(1.0 / " << F(rb) << ");\n"; cr1(); return true;
    case Op::frsqrte: o << "  " << F(rt) << " = 1.0 / std::sqrt(" << F(rb) << ");\n"; cr1(); return true;
    case Op::fsel:
      o << "  " << F(rt) << " = (" << F(ra) << " >= 0.0) ? " << F(rc) << " : " << F(rb) << ";\n";
      cr1();
      return true;
    case Op::frsp: o << "  " << F(rt) << " = fsr::round_single(" << F(rb) << ");\n"; cr1(); return true;
    case Op::fmr: o << "  " << FU(rt) << " = " << FU(rb) << ";\n"; cr1(); return true;
    case Op::fneg: o << "  " << FU(rt) << " = " << FU(rb) << " ^ 0x8000000000000000ull;\n"; cr1(); return true;
    case Op::fabs: o << "  " << FU(rt) << " = " << FU(rb) << " & 0x7FFFFFFFFFFFFFFFull;\n"; cr1(); return true;
    case Op::fnabs: o << "  " << FU(rt) << " = " << FU(rb) << " | 0x8000000000000000ull;\n"; cr1(); return true;
    case Op::fctiw: o << "  " << FU(rt) << " = fsr::fctiw_bits(" << F(rb) << ", false);\n"; cr1(); return true;
    case Op::fctiwz: o << "  " << FU(rt) << " = fsr::fctiw_bits(" << F(rb) << ", true);\n"; cr1(); return true;
    case Op::fctid: o << "  " << FU(rt) << " = fsr::fctid_bits(" << F(rb) << ", false);\n"; cr1(); return true;
    case Op::fctidz: o << "  " << FU(rt) << " = fsr::fctid_bits(" << F(rb) << ", true);\n"; cr1(); return true;
    case Op::fcfid: o << "  " << F(rt) << " = double(int64_t(" << FU(rb) << "));\n"; cr1(); return true;
    case Op::fcmpu: case Op::fcmpo:
      o << "  fsr::set_cr_fp(ctx, " << f_crfd(raw) << ", " << F(ra) << ", " << F(rb) << ");\n";
      return true;
    case Op::mffs: o << "  " << FU(rt) << " = ctx.fpscr;\n"; cr1(); return true;
    case Op::mtfsf: o << "  ctx.fpscr = uint32_t(" << FU(rb) << "); // rounding-mode changes not emulated\n"; return true;
    case Op::mtfsfi: case Op::mtfsb0: case Op::mtfsb1: case Op::mcrfs:
      o << "  // FPSCR bit op ignored (default rounding)\n";
      return true;
    // ---------------------------------------------------------------- not yet translated
    case Op::vmx:
      st_.vmx++;
      st_.unimplemented_histogram["vmx/vmx128"]++;
      o << "  fsr::fsr_unimplemented(ctx, base, " << hx(addr) << ", " << hx(raw) << ");\n";
      return false;
    case Op::invalid:
    default:
      st_.invalid++;
      st_.unimplemented_histogram["invalid"]++;
      o << "  fsr::fsr_unimplemented(ctx, base, " << hx(addr) << ", " << hx(raw) << ");\n";
      return false;
  }
}

}  // namespace

RecompStats analyze(const RecompInput& in, std::vector<FunctionInfo>* out_funcs) {
  RecompOutput out;
  auto funcs = discover_functions(in);
  RecompStats st;
  st.functions = funcs.size();
  Code code{in.image, in.base, in.size};
  for (auto& f : funcs) {
    if (f.dynamic_dispatch) st.dynamic_functions++;
    for (uint32_t a = f.start; a < f.end; a += 4) {
      uint32_t raw = code.word(a);
      if (raw == 0) { st.padding_words++; continue; }
      Op op = decode(raw);
      st.instructions++;
      st.op_histogram[op_name(op)]++;
      if (op == Op::vmx) { st.vmx++; st.unimplemented_histogram["vmx/vmx128"]++; }
      else if (op == Op::invalid) { st.invalid++; st.unimplemented_histogram["invalid"]++; }
      else st.translated++;
      if ((op == Op::bcctr || op == Op::bclr) && f_lk(raw)) st.indirect_call_sites++;
      if (op == Op::bcctr && !f_lk(raw)) st.indirect_call_sites++;
    }
  }
  if (out_funcs) *out_funcs = std::move(funcs);
  return st;
}

RecompOutput recompile(const RecompInput& in, const RecompOptions& opt) {
  RecompOutput out;
  out.functions = discover_functions(in);
  std::set<uint32_t> funcs, thunks;
  for (auto& f : out.functions) funcs.insert(f.start);
  for (auto& t : in.thunks) thunks.insert(t.addr);
  RecompStats& st = out.stats;
  st.functions = out.functions.size();
  for (auto& f : out.functions) st.dynamic_functions += f.dynamic_dispatch;

  const std::string mod = in.module_name;
  // Declarations header.
  std::ostringstream h;
  const std::string ns = "fsr_gen_" + mod;
  h << "// Generated by fsr-recomp. Do not edit.\n#pragma once\n#include \"fsr/runtime.h\"\n\nnamespace " << ns << " {\n";
  for (uint32_t a : funcs) h << "FSR_FUNC(" << fname(a) << ");\n";
  for (auto& t : in.thunks)
    if (!funcs.count(t.addr)) h << "FSR_FUNC(" << fname(t.addr) << ");\n";
  h << "}  // namespace " << ns << "\n";
  // Fall-through targets that are not functions resolve via the indirect path,
  // but every call_expr target must be declared: collect function ends too.
  out.files.emplace_back(mod + "_decls.h", h.str());

  Emitter em(in, funcs, thunks, st);
  size_t file_index = 0, count = 0;
  std::ostringstream body;
  auto flush = [&]() {
    if (!count) return;
    std::ostringstream f;
    f << "// Generated by fsr-recomp from " << mod << ". Do not edit.\n";
    f << "#include <atomic>\n#include <cmath>\n#include <cstring>\n#include \"" << mod << "_decls.h\"\n";
    f << "#if defined(__clang__)\n#pragma clang diagnostic ignored \"-Wunused-label\"\n#pragma clang diagnostic ignored \"-Wunused-variable\"\n#endif\n\n";
    f << "namespace " << ns << " {\n\n" << body.str() << "}  // namespace " << ns << "\n";
    char name[64];
    std::snprintf(name, sizeof name, "_funcs_%03zu.cpp", file_index++);
    out.files.emplace_back(mod + name, f.str());
    body.str("");
    body.clear();
    count = 0;
  };
  for (auto& f : out.functions) {
    body << em.function(f, opt.emit_comments);
    if (++count >= opt.functions_per_file) flush();
  }
  // Import thunks.
  for (auto& t : in.thunks) {
    if (funcs.count(t.addr)) continue;
    body << "// import " << in.libs[t.lib] << " #" << t.ordinal << " " << t.name << "\n";
    body << "FSR_FUNC(" << fname(t.addr) << ") { fsr::fsr_kernel_call(ctx, base, " << t.lib << ", " << t.ordinal
         << "); }\n\n";
    count++;
  }
  flush();

  // Function table + module descriptor.
  std::vector<uint32_t> all(funcs.begin(), funcs.end());
  for (auto& t : in.thunks)
    if (!funcs.count(t.addr)) all.push_back(t.addr);
  std::sort(all.begin(), all.end());
  uint8_t sha[20];
  Sha1::hash(in.image, in.size, sha);
  std::ostringstream tbl;
  tbl << "// Generated by fsr-recomp. Do not edit.\n#include \"" << mod << "_decls.h\"\n\n";
  tbl << "static const fsr::FuncEntry kFuncs[] = {\n";
  for (uint32_t a : all) tbl << "  {" << hx(a) << ", " << ns << "::" << fname(a) << "},\n";
  tbl << "};\n\n";
  tbl << "static const char* const kLibs[] = {";
  for (auto& l : in.libs) tbl << "\"" << l << "\", ";
  tbl << "nullptr};\n";
  tbl << "static const fsr::NamedAddr kExports[] = {";
  for (auto& e : in.exports) tbl << "{\"" << e.first << "\", " << hx(e.second) << "}, ";
  tbl << "{nullptr, 0}};\n\n";
  tbl << "extern const fsr::RecompModule fsr_module_" << mod << ";\n";
  tbl << "const fsr::RecompModule fsr_module_" << mod << " = {\n  \"" << mod << "\", " << hx(in.base) << ", "
      << hx(in.size) << ", " << hx(in.entry_point) << ",\n  {";
  for (int i = 0; i < 20; i++) tbl << (i ? "," : "") << hx(sha[i]);
  tbl << "},\n  kFuncs, " << all.size() << ", " << (st.vmx + st.invalid) << ",\n  kLibs, " << in.libs.size() << ", kExports, "
      << in.exports.size() << "\n};\n";
  out.files.emplace_back(mod + "_table.cpp", tbl.str());
  return out;
}

}  // namespace fsr
