// FIFA STREET Recompiled - XEX analysis report (used by fsr-analyze and the
// Android library scanner through JNI).
// SPDX-License-Identifier: MIT
#pragma once
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
};

// key may be null. deep=true runs function discovery + instruction coverage.
AnalysisReport analyze_xex(const std::string& path, const uint8_t* key, bool deep);
std::string to_json(const AnalysisReport& r);
std::string to_text(const AnalysisReport& r);
bool load_key_file(const std::string& path, uint8_t key[16], std::string* err = nullptr);

}  // namespace fsr
