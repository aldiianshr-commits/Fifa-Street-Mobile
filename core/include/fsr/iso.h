// FIFA STREET Recompiled - Xbox / Xbox 360 disc image (XDVDFS, "GDFX")
// reader. Works on any ReadSource, so an ISO is read in place (no
// extraction) and files can be streamed to the runtime's game:\ device.
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fsr/source.h"

namespace fsr {

struct IsoEntry {
  std::string path;  // '/'-separated, relative to root
  uint64_t offset = 0, size = 0;  // absolute offset in the image
  uint32_t sector = 0;
  uint8_t attributes = 0;
  bool dir = false;
};

enum class ContainerKind { Unknown, Xex, Xdvdfs, Stfs };
ContainerKind sniff_container(const std::string& path, std::string* detail = nullptr);
ContainerKind sniff_container(const ReadSource& src, std::string* detail = nullptr);

struct DiscLayout {
  uint64_t partition_offset = 0;
  std::string layout;        // "XGD2", "XGD3", "XGD1 (Xbox)", "rebuilt/XISO", ...
  uint32_t root_sector = 0, root_size = 0;
  uint64_t filetime = 0;     // volume creation FILETIME
  bool trailer_magic_ok = false;
};

class XisoReader {
 public:
  bool open(const std::string& path, std::string& err);
  bool open(std::shared_ptr<const ReadSource> src, std::string& err);

  const std::vector<IsoEntry>& entries() const { return entries_; }
  const DiscLayout& layout() const { return layout_; }
  uint64_t partition_offset() const { return layout_.partition_offset; }
  const std::vector<std::string>& warnings() const { return warnings_; }
  std::shared_ptr<const ReadSource> source() const { return src_; }

  // Case-insensitive lookup; accepts '/' or '\\' separators and an optional
  // leading separator.
  const IsoEntry* find(const std::string& path) const;
  bool read(const IsoEntry& e, std::vector<uint8_t>& out, uint64_t max_bytes = 512ull << 20) const;
  bool read_range(const IsoEntry& e, uint64_t off, void* dst, size_t n) const;
  // Number of files whose data extends past the end of the image.
  size_t truncated_files() const;

 private:
  bool walk(uint64_t dir_off, uint32_t dir_size, const std::string& prefix, int depth, std::string& err);
  std::shared_ptr<const ReadSource> src_;
  DiscLayout layout_;
  std::vector<IsoEntry> entries_;
  std::vector<std::string> warnings_;
};

std::string normalize_disc_path(const std::string& p);  // lower-case, '/' separators, no leading '/'

}  // namespace fsr
