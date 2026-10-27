// FIFA STREET Recompiled - byte order helpers
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace fsr {

inline uint16_t bswap16(uint16_t v) { return __builtin_bswap16(v); }
inline uint32_t bswap32(uint32_t v) { return __builtin_bswap32(v); }
inline uint64_t bswap64(uint64_t v) { return __builtin_bswap64(v); }

inline uint16_t rd_be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
inline uint32_t rd_be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
inline uint64_t rd_be64(const uint8_t* p) { return uint64_t(rd_be32(p)) << 32 | rd_be32(p + 4); }
inline uint16_t rd_le16(const uint8_t* p) { return uint16_t(p[1] << 8 | p[0]); }
inline uint32_t rd_le32(const uint8_t* p) {
  return uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0];
}
inline void wr_be16(uint8_t* p, uint16_t v) { p[0] = v >> 8; p[1] = uint8_t(v); }
inline void wr_be32(uint8_t* p, uint32_t v) {
  p[0] = v >> 24; p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}
inline void wr_le16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = v >> 8; }
inline void wr_le32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); p[3] = v >> 24;
}

// Bounds-checked reader over a byte buffer. Every accessor fails softly
// (returns 0 and sets ok=false) so a malformed file never crashes the app.
struct Reader {
  const uint8_t* data = nullptr;
  size_t size = 0;
  bool ok = true;
  Reader() = default;
  Reader(const uint8_t* d, size_t s) : data(d), size(s) {}
  explicit Reader(const std::vector<uint8_t>& v) : data(v.data()), size(v.size()) {}
  bool has(size_t off, size_t n) const { return off <= size && n <= size - off; }
  uint8_t u8(size_t off) { if (!has(off, 1)) { ok = false; return 0; } return data[off]; }
  uint16_t be16(size_t off) { if (!has(off, 2)) { ok = false; return 0; } return rd_be16(data + off); }
  uint32_t be32(size_t off) { if (!has(off, 4)) { ok = false; return 0; } return rd_be32(data + off); }
  uint16_t le16(size_t off) { if (!has(off, 2)) { ok = false; return 0; } return rd_le16(data + off); }
  uint32_t le32(size_t off) { if (!has(off, 4)) { ok = false; return 0; } return rd_le32(data + off); }
  const uint8_t* ptr(size_t off, size_t n) { if (!has(off, n)) { ok = false; return nullptr; } return data + off; }
  std::string cstr(size_t off, size_t max) {
    std::string s;
    for (size_t i = 0; i < max && has(off + i, 1) && data[off + i]; i++) s.push_back(char(data[off + i]));
    return s;
  }
};

std::string hex32(uint32_t v);
std::string hexbytes(const uint8_t* p, size_t n);
bool read_file(const std::string& path, std::vector<uint8_t>& out, std::string* err = nullptr);
bool write_file(const std::string& path, const void* data, size_t n);

}  // namespace fsr
