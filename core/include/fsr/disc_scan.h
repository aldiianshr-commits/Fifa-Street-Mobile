// FIFA STREET Recompiled - one-step processing of an Xbox 360 disc image:
// validate the filesystem, inventory and classify every file, locate the
// main executable and other modules, read them straight out of the ISO,
// decrypt/analyse/translate-check the PowerPC code, cross-check file paths the
// executable references, and derive a single library status.
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fsr/iso.h"
#include "fsr/report.h"

namespace fsr {

enum class ReqState { Ok, Partial, Missing, Info };
const char* to_string(ReqState s);

struct Requirement {
  std::string id, label;
  ReqState state = ReqState::Info;
  std::string detail;
};

enum class GameStatus { Ready, Partial, Unsupported };
const char* to_string(GameStatus s);

struct CategoryStat {
  uint64_t files = 0, bytes = 0;
};

struct ModuleSummary {  // any XEX on the disc other than the main one
  std::string path;
  uint64_t size = 0;
  bool parsed = false;
  std::string error, title_id, load_status;
  uint32_t module_flags = 0;
  bool is_dll = false, is_patch = false;
};

struct DiscReport {
  bool ok = false;           // filesystem readable
  std::string error;
  uint64_t image_size = 0;
  ContainerKind kind = ContainerKind::Unknown;
  DiscLayout layout;
  std::string platform;      // "xbox360", "xbox", "unknown"
  uint64_t files = 0, dirs = 0, bytes = 0;
  size_t truncated = 0;
  std::vector<std::string> truncated_examples;
  std::map<std::string, CategoryStat> categories;
  std::vector<std::string> fs_warnings;
  std::vector<std::string> special_dirs;  // $SystemUpdate, ...
  std::string main_xex;                   // disc path ("default.xex")
  bool have_report = false;
  AnalysisReport xex;                     // analysis of the main executable
  std::vector<ModuleSummary> modules;
  // File paths found as literals in the decrypted image.
  uint64_t ref_total = 0, ref_found = 0, ref_patterns = 0, ref_other_device = 0;
  std::vector<std::string> ref_missing;   // up to 100
  std::vector<std::string> ref_found_examples;  // up to 20
  bool key_supplied = false;
  std::vector<Requirement> requirements;
  GameStatus status = GameStatus::Unsupported;
  std::string status_reason;
  bool cancelled = false;
  double seconds = 0;
};

// stage, done, total, message. Return false to cancel.
using ScanProgress = std::function<bool(const std::string&, uint64_t, uint64_t, const std::string&)>;

struct ScanOptions {
  const uint8_t* key = nullptr;  // user's 16-byte XEX key, optional
  bool deep = true;              // function discovery + translation check
};

DiscReport scan_disc(std::shared_ptr<const ReadSource> src, const ScanOptions& opt, const ScanProgress& progress);
std::string to_json(const DiscReport& r);
std::string to_text(const DiscReport& r);

// Locates the main executable and loads it (decrypt + decompress) directly
// from the disc for the runtime. Returns false with a reason on failure.
bool load_main_xex(const XisoReader& disc, const uint8_t* key, XexModule& out, std::string* path, std::string& err);

// File classification helper (exposed for tests).
std::string classify_file(const std::string& path, const uint8_t* head, size_t n);

}  // namespace fsr
