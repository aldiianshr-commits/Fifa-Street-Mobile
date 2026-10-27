// FIFA STREET Recompiled - Xbox 360 disc image (XDVDFS / XGD) reader, so a
// user's own ISO dump can be scanned and default.xex extracted without
// unpacking the whole disc.
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace fsr {

struct IsoEntry {
  std::string path;  // '/'-separated, relative to root
  uint64_t offset = 0, size = 0;
  bool dir = false;
};

enum class ContainerKind { Unknown, Xex, Xdvdfs, Stfs };
ContainerKind sniff_container(const std::string& path, std::string* detail = nullptr);

class XisoReader {
 public:
  ~XisoReader();
  bool open(const std::string& path, std::string& err);
  const std::vector<IsoEntry>& entries() const { return entries_; }
  uint64_t partition_offset() const { return part_; }
  const IsoEntry* find(const std::string& path) const;  // case-insensitive
  bool read(const IsoEntry& e, std::vector<uint8_t>& out, uint64_t max_bytes = 512ull << 20) const;

 private:
  bool read_at(uint64_t off, void* dst, size_t n) const;
  bool walk(uint64_t dir_off, uint32_t dir_size, const std::string& prefix, int depth, std::string& err);
  FILE* f_ = nullptr;
  uint64_t part_ = 0, file_size_ = 0;
  std::vector<IsoEntry> entries_;
};

}  // namespace fsr
