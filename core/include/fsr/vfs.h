// FIFA STREET Recompiled - guest virtual file system. The disc image is
// mounted read-only as game:\ (aliases d:\ and \Device\Cdrom0\) and every
// guest read is served straight from the ISO through pread().
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "fsr/iso.h"

namespace fsr {

struct OpenFile {
  const IsoEntry* entry = nullptr;  // nullptr = root directory
  uint64_t position = 0;
};

class DiscVfs {
 public:
  explicit DiscVfs(std::shared_ptr<XisoReader> disc) : disc_(std::move(disc)) {}
  // Translates a guest path ("game:\\data\\x.big", "d:\\x", "\\Device\\Cdrom0\\x",
  // "\\??\\game:\\x") to a disc-relative path. Returns false for paths on other
  // devices (cache:, hdd:, ...), which are not mounted.
  static bool translate(const std::string& guest, std::string* disc_path, std::string* device = nullptr);
  // Resolves a translated path; root ("") is a directory with entry == nullptr.
  bool resolve(const std::string& disc_path, const IsoEntry** out, bool* is_root) const;

  uint32_t open(const IsoEntry* e);  // returns handle
  bool close(uint32_t handle);
  OpenFile* lookup(uint32_t handle);
  std::mutex& mutex() { return mu_; }
  const XisoReader& disc() const { return *disc_; }
  uint64_t bytes_read = 0, opens = 0, failed_opens = 0;

 private:
  std::shared_ptr<XisoReader> disc_;
  std::mutex mu_;
  std::map<uint32_t, OpenFile> handles_;
  uint32_t next_ = 0xF8000004u;
};

}  // namespace fsr
