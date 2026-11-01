// SPDX-License-Identifier: MIT
#include "fsr/iso.h"

#include <strings.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "fsr/bytes.h"

namespace fsr {
namespace {
constexpr uint32_t kSector = 2048;
const char kMagic[] = "MICROSOFT*XBOX*MEDIA";
struct Part {
  uint64_t offset;
  const char* name;
};
// Game partition offsets seen in dumps (cross-checked with Xenia's
// DiscImageDevice probing list) plus the original Xbox XGD1 offset.
const Part kPartitions[] = {
    {0x00000000ull, "rebuilt/XISO (partisi game saja)"},
    {0x0FD90000ull, "XGD2 (Xbox 360)"},
    {0x02080000ull, "XGD3 (Xbox 360)"},
    {0x0000FB20ull, "offset non-standar 0xFB20"},
    {0x00020600ull, "offset non-standar 0x20600"},
    {0x18300000ull, "XGD1 (Xbox original)"},
};
}  // namespace

std::string normalize_disc_path(const std::string& p) {
  std::string s;
  for (char c : p) s.push_back(c == '\\' ? '/' : char(std::tolower(static_cast<unsigned char>(c))));
  while (!s.empty() && s[0] == '/') s.erase(0, 1);
  while (!s.empty() && s.back() == '/') s.pop_back();
  // collapse "//" and "./"
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '/' && !o.empty() && o.back() == '/') continue;
    o.push_back(s[i]);
  }
  return o;
}

ContainerKind sniff_container(const ReadSource& src, std::string* detail) {
  uint8_t head[4] = {};
  if (!src.read(0, head, 4)) return ContainerKind::Unknown;
  if (std::memcmp(head, "XEX2", 4) == 0) return ContainerKind::Xex;
  if (std::memcmp(head, "LIVE", 4) == 0 || std::memcmp(head, "PIRS", 4) == 0 || std::memcmp(head, "CON ", 4) == 0) {
    if (detail) *detail = std::string(reinterpret_cast<char*>(head), 4);
    return ContainerKind::Stfs;
  }
  for (const auto& p : kPartitions) {
    char m[20];
    if (src.read(p.offset + 32 * kSector, m, 20) && std::memcmp(m, kMagic, 20) == 0) {
      if (detail) *detail = p.name;
      return ContainerKind::Xdvdfs;
    }
  }
  return ContainerKind::Unknown;
}

ContainerKind sniff_container(const std::string& path, std::string* detail) {
  std::string err;
  auto src = FdSource::open(path, &err);
  if (!src) return ContainerKind::Unknown;
  return sniff_container(*src, detail);
}

bool XisoReader::open(const std::string& path, std::string& err) {
  auto src = FdSource::open(path, &err);
  if (!src) return false;
  return open(std::shared_ptr<const ReadSource>(src), err);
}

bool XisoReader::open(std::shared_ptr<const ReadSource> src, std::string& err) {
  src_ = std::move(src);
  entries_.clear();
  warnings_.clear();
  layout_ = DiscLayout{};
  for (const auto& p : kPartitions) {
    uint8_t vd[kSector];
    if (!src_->read(p.offset + 32 * kSector, vd, sizeof vd)) continue;
    if (std::memcmp(vd, kMagic, 20) != 0) continue;
    layout_.partition_offset = p.offset;
    layout_.layout = p.name;
    layout_.root_sector = rd_le32(vd + 20);
    layout_.root_size = rd_le32(vd + 24);
    layout_.filetime = uint64_t(rd_le32(vd + 28)) | uint64_t(rd_le32(vd + 32)) << 32;
    layout_.trailer_magic_ok = std::memcmp(vd + 0x7EC, kMagic, 20) == 0;
    if (!layout_.trailer_magic_ok) warnings_.push_back("volume descriptor trailer magic missing (image may be damaged)");
    if (layout_.root_size < 14 || layout_.root_size > (32u << 20)) {
      err = "root directory size out of range (" + std::to_string(layout_.root_size) + " bytes): damaged image";
      return false;
    }
    if (!walk(p.offset + uint64_t(layout_.root_sector) * kSector, layout_.root_size, "", 0, err)) return false;
    if (entries_.empty()) {
      err = "XDVDFS root directory is empty";
      return false;
    }
    return true;
  }
  err = "no XDVDFS volume descriptor (\"MICROSOFT*XBOX*MEDIA\") at any known partition offset: not an Xbox/Xbox 360 game disc image";
  return false;
}

bool XisoReader::walk(uint64_t dir_off, uint32_t dir_size, const std::string& prefix, int depth, std::string& err) {
  if (depth > 32) { err = "directory tree deeper than 32 levels (corrupt)"; return false; }
  if (dir_size > (64u << 20)) { err = "directory larger than 64 MiB (corrupt)"; return false; }
  if (!dir_size) return true;
  std::vector<uint8_t> d(dir_size);
  if (!src_->read(dir_off, d.data(), d.size())) {
    err = "directory '" + (prefix.empty() ? std::string("/") : prefix) + "' lies beyond the end of the image (truncated ISO)";
    return false;
  }
  std::vector<uint32_t> stack{0};
  std::vector<uint8_t> seen(dir_size / 4 + 1, 0);
  while (!stack.empty()) {
    uint32_t o = stack.back();
    stack.pop_back();
    if (o + 14 > dir_size || seen[o / 4]) continue;
    seen[o / 4] = 1;
    uint16_t left = rd_le16(&d[o]), right = rd_le16(&d[o + 2]);
    if (left == 0xFFFF && right == 0xFFFF) continue;
    uint32_t sector = rd_le32(&d[o + 4]), size = rd_le32(&d[o + 8]);
    uint8_t attr = d[o + 12], nlen = d[o + 13];
    if (o + 14 + nlen > dir_size || nlen == 0) continue;
    std::string name(reinterpret_cast<char*>(&d[o + 14]), nlen);
    bool printable = std::all_of(name.begin(), name.end(), [](char c) { return uint8_t(c) >= 0x20 && c != '/' && c != '\\'; });
    if (!printable) {
      warnings_.push_back("skipped directory entry with invalid name in '" + prefix + "'");
      continue;
    }
    IsoEntry e;
    e.path = prefix.empty() ? name : prefix + "/" + name;
    e.sector = sector;
    e.offset = layout_.partition_offset + uint64_t(sector) * kSector;
    e.size = size;
    e.attributes = attr;
    e.dir = attr & 0x10;
    entries_.push_back(e);
    if (e.dir && !walk(e.offset, size, e.path, depth + 1, err)) return false;
    if (left && left != 0xFFFF) stack.push_back(uint32_t(left) * 4);
    if (right && right != 0xFFFF) stack.push_back(uint32_t(right) * 4);
  }
  return true;
}

const IsoEntry* XisoReader::find(const std::string& path) const {
  std::string want = normalize_disc_path(path);
  for (auto& e : entries_)
    if (strcasecmp(e.path.c_str(), want.c_str()) == 0) return &e;
  return nullptr;
}

bool XisoReader::read(const IsoEntry& e, std::vector<uint8_t>& out, uint64_t max_bytes) const {
  if (e.dir || e.size > max_bytes) return false;
  out.resize(e.size);
  return e.size == 0 || src_->read(e.offset, out.data(), out.size());
}

bool XisoReader::read_range(const IsoEntry& e, uint64_t off, void* dst, size_t n) const {
  if (e.dir || off > e.size || n > e.size - off) return false;
  return src_->read(e.offset + off, dst, n);
}

size_t XisoReader::truncated_files() const {
  size_t n = 0;
  for (auto& e : entries_)
    if (!e.dir && e.offset + e.size > src_->size()) n++;
  return n;
}

}  // namespace fsr
