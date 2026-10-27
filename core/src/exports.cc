// Kernel export ordinal tables. The .inc files come from Xenia
// (third_party/xenia, BSD-3-Clause) and contain only names/ordinals.
// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

#include "fsr/xex.h"

namespace fsr {
namespace {

enum ExportType { kFunction = 0, kVariable = 1 };
struct ExportName {
  uint16_t ordinal;
  const char* name;
  ExportType type;
};

#define XE_EXPORT(module, ordinal, name, type) {uint16_t(ordinal), #name, type}
const ExportName kKernel[] = {
#include "xboxkrnl_table.inc"
};
const ExportName kXam[] = {
#include "xam_table.inc"
};
#undef XE_EXPORT

template <size_t N>
const ExportName* find(const ExportName (&t)[N], uint16_t ord) {
  for (const auto& e : t)
    if (e.ordinal == ord) return &e;
  return nullptr;
}

bool ieq(const std::string& a, const char* b) {
  if (a.size() != std::strlen(b)) return false;
  for (size_t i = 0; i < a.size(); i++)
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  return true;
}

}  // namespace

const char* kernel_export_name(const std::string& lib, uint16_t ordinal, bool* is_variable) {
  const ExportName* e = nullptr;
  if (ieq(lib, "xboxkrnl.exe")) e = find(kKernel, ordinal);
  else if (ieq(lib, "xam.xex")) e = find(kXam, ordinal);
  if (e && is_variable) *is_variable = e->type == kVariable;
  return e ? e->name : nullptr;
}

int kernel_export_ordinal(const std::string& lib, const std::string& name) {
  auto scan = [&](auto& t) -> int {
    for (const auto& e : t)
      if (name == e.name) return e.ordinal;
    return -1;
  };
  if (ieq(lib, "xboxkrnl.exe")) return scan(kKernel);
  if (ieq(lib, "xam.xex")) return scan(kXam);
  return -1;
}

}  // namespace fsr
