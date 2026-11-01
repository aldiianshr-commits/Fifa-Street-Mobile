// FIFA STREET Recompiled - XEX analysis report (used by fsr-analyze and the
// Android library scanner through JNI).
// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "fsr/recomp.h"
#include "fsr/xex.h"

namespace fsr {

struct ImportSummary {
  std::string lib, name;
  uint16_t ordinal = 0;
  bool function = false;
  bool implemented = false;
};

struct AnalysisReport {
  std::string path;
  bool parsed = false;
  std::string error;
  XexInfo info;
  bool image_loaded = false;
  LoadStatus load_status = LoadStatus::Ok;
  std::string load_error;
  std::vector<PeSection> sections;
  size_t pdata_functions = 0;
  RecompStats stats;
  bool deep = false;
  std::vector<ImportSummary> imports;
  size_t imports_implemented = 0;
  std::string image_sha1;
  std::string module;  // registered recompiled module matching this image, if any
};

// key may be null. deep=true runs function discovery + instruction coverage.
AnalysisReport analyze_xex(const std::string& path, const uint8_t* key, bool deep);
// Same, from an in-memory XEX (e.g. read directly out of a disc image).
// When keep != nullptr the parsed/loaded module is left there for reuse.
AnalysisReport analyze_xex_buffer(std::vector<uint8_t> file, const std::string& label, const uint8_t* key, bool deep,
                                  XexModule* keep, const AnalyzeProgress& progress);
std::string region_text(uint32_t region);
std::string jstr(const std::string& s);
std::string to_json(const AnalysisReport& r);
std::string to_text(const AnalysisReport& r);
bool load_key_file(const std::string& path, uint8_t key[16], std::string* err = nullptr);

}  // namespace fsr
