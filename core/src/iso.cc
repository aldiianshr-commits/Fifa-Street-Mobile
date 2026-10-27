// SPDX-License-Identifier: MIT
#include "fsr/iso.h"

#include <strings.h>

#include <cstring>

#include "fsr/bytes.h"

namespace fsr {
namespace {
constexpr uint32_t kSector = 2048;
const char kMagic[] = "MICROSOFT*XBOX*MEDIA";
// Game partition offsets: rebuilt/XDVDFS-only, XGD2, XGD3, XGD1.
const uint64_t kPartitions[] = {0, 0xFD90000ull, 0x2080000ull, 0x18300000ull};
}  // namespace

ContainerKind sniff_container(const std::string& path, std::string* detail) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return ContainerKind::Unknown;
  uint8_t head[4] = {};
  size_t n = std::fread(head, 1, 4, f);
  ContainerKind k = ContainerKind::Unknown;
  if (n == 4 && std::memcmp(head, "XEX2", 4) == 0) k = ContainerKind::Xex;
  else if (n == 4 && (std::memcmp(head, "LIVE", 4) == 0 || std::memcmp(head, "PIRS", 4) == 0 ||
                      std::memcmp(head, "CON ", 4) == 0)) {
    k = ContainerKind::Stfs;
    if (detail) *detail = std::string(reinterpret_cast<char*>(head), 4);
  } else {
    for (uint64_t p : kPartitions) {
      char m[20];
      if (fseeko(f, off_t(p + 32 * kSector), SEEK_SET) == 0 && std::fread(m, 1, 20, f) == 20 &&
          std::memcmp(m, kMagic, 20) == 0) {
        k = ContainerKind::Xdvdfs;
        break;
      }
    }
  }
  std::fclose(f);
  return k;
}

XisoReader::~XisoReader() {
  if (f_) std::fclose(f_);
}

bool XisoReader::read_at(uint64_t off, void* dst, size_t n) const {
  if (off + n > file_size_) return false;
  if (fseeko(f_, off_t(off), SEEK_SET) != 0) return false;
  return std::fread(dst, 1, n, f_) == n;
}

bool XisoReader::open(const std::string& path, std::string& err) {
  f_ = std::fopen(path.c_str(), "rb");
  if (!f_) { err = "cannot open " + path; return false; }
  fseeko(f_, 0, SEEK_END);
  file_size_ = uint64_t(ftello(f_));
  for (uint64_t p : kPartitions) {
    uint8_t vd[0x800];
    if (!read_at(p + 32 * kSector, vd, sizeof vd)) continue;
    if (std::memcmp(vd, kMagic, 20) != 0) continue;
    part_ = p;
    uint32_t root_sector = rd_le32(vd + 20), root_size = rd_le32(vd + 24);
    entries_.clear();
    return walk(p + uint64_t(root_sector) * kSector, root_size, "", 0, err);
  }
  err = "no XDVDFS volume descriptor found (not an Xbox 360 game disc image)";
  return false;
}

bool XisoReader::walk(uint64_t dir_off, uint32_t dir_size, const std::string& prefix, int depth, std::string& err) {
  if (depth > 32 || dir_size > (64u << 20)) { err = "directory tree too deep or too large"; return false; }
  if (!dir_size) return true;
  std::vector<uint8_t> d(dir_size);
  if (!read_at(dir_off, d.data(), d.size())) { err = "directory read beyond end of image"; return false; }
  // Iterative traversal of the on-disc binary tree (offsets in dwords).
  std::vector<uint32_t> stack{0};
  std::vector<uint8_t> seen(dir_size / 4 + 1, 0);
  while (!stack.empty()) {
    uint32_t o = stack.back();
    stack.pop_back();
    if (o + 14 > dir_size || seen[o / 4]) continue;
    seen[o / 4] = 1;
    uint16_t left = rd_le16(&d[o]), right = rd_le16(&d[o + 2]);
    if (left == 0xFFFF && right == 0xFFFF) continue;  // padding
    uint32_t sector = rd_le32(&d[o + 4]), size = rd_le32(&d[o + 8]);
    uint8_t attr = d[o + 12], nlen = d[o + 13];
    if (o + 14 + nlen > dir_size) continue;
    std::string name(reinterpret_cast<char*>(&d[o + 14]), nlen);
    IsoEntry e;
    e.path = prefix.empty() ? name : prefix + "/" + name;
    e.offset = part_ + uint64_t(sector) * kSector;
    e.size = size;
    e.dir = attr & 0x10;
    entries_.push_back(e);
    if (e.dir && !walk(e.offset, size, e.path, depth + 1, err)) return false;
    if (left && left != 0xFFFF) stack.push_back(uint32_t(left) * 4);
    if (right && right != 0xFFFF) stack.push_back(uint32_t(right) * 4);
  }
  return true;
}

const IsoEntry* XisoReader::find(const std::string& path) const {
  for (auto& e : entries_)
    if (strcasecmp(e.path.c_str(), path.c_str()) == 0) return &e;
  return nullptr;
}

bool XisoReader::read(const IsoEntry& e, std::vector<uint8_t>& out, uint64_t max_bytes) const {
  if (e.dir || e.size > max_bytes) return false;
  out.resize(e.size);
  return read_at(e.offset, out.data(), out.size());
}

}  // namespace fsr
