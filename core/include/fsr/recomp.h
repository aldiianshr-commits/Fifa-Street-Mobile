// FIFA STREET Recompiled - PowerPC -> C++ static recompiler.
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "fsr/xex.h"

namespace fsr {

struct ImportThunk {
  uint32_t addr = 0;
  uint32_t lib = 0;  // index into RecompInput::libs
  uint16_t ordinal = 0;
  std::string name;
};

struct RecompInput {
  std::string module_name = "game";
  const uint8_t* image = nullptr;
  uint32_t base = 0, size = 0;
  uint32_t entry_point = 0;
  std::vector<std::pair<uint32_t, uint32_t>> code_ranges;  // [start, end)
  std::vector<FunctionEntry> pdata;
  std::vector<uint32_t> extra_functions;                   // user/ELF supplied entries
  std::vector<std::string> libs;
  std::vector<ImportThunk> thunks;
  std::vector<std::pair<std::string, uint32_t>> exports;  // named entry points
};

struct FunctionInfo {
  uint32_t start = 0, end = 0;
  bool from_pdata = false;
  bool dynamic_dispatch = false;  // contains bctr (jump table / computed goto)
};

struct RecompStats {
  uint64_t functions = 0;
  uint64_t instructions = 0;       // decoded instructions inside functions
  uint64_t translated = 0;         // emitted as real C++
  uint64_t vmx = 0;                // AltiVec/VMX128 instructions (all)
  uint64_t vmx_translated = 0;     // ... of which translated
  uint64_t vmx128 = 0;             // ... of which VMX128 encodings
  uint64_t invalid = 0;            // undecodable words inside functions
  uint64_t padding_words = 0;      // zero words (alignment/data) inside function ranges
  uint64_t dynamic_functions = 0;  // functions using bctr dispatch
  uint64_t indirect_call_sites = 0;
  std::map<std::string, uint64_t> op_histogram;
  std::map<std::string, uint64_t> unimplemented_histogram;  // "vmx", "invalid"...
  double coverage() const { return instructions ? double(translated) / double(instructions) : 0.0; }
};

struct RecompOptions {
  uint32_t functions_per_file = 1500;
  bool emit_comments = true;
};

struct RecompOutput {
  std::vector<std::pair<std::string, std::string>> files;  // filename, contents
  RecompStats stats;
  std::vector<FunctionInfo> functions;
};

// Builds a RecompInput from a loaded XEX module.
RecompInput make_input(const XexModule& xex, const std::string& module_name);
// Function discovery only (fast; used for on-device analysis).
std::vector<FunctionInfo> discover_functions(const RecompInput& in);
RecompStats analyze(const RecompInput& in, std::vector<FunctionInfo>* out_funcs = nullptr);
// Full C++ generation.
RecompOutput recompile(const RecompInput& in, const RecompOptions& opt = {});

}  // namespace fsr
