// SPDX-License-Identifier: MIT
#include <cstdio>

#include "fsr/bytes.h"

namespace fsr {

std::string hex32(uint32_t v) {
  char b[16];
  std::snprintf(b, sizeof b, "%08X", v);
  return b;
}

std::string hexbytes(const uint8_t* p, size_t n) {
  static const char* d = "0123456789ABCDEF";
  std::string s;
  for (size_t i = 0; i < n; i++) { s.push_back(d[p[i] >> 4]); s.push_back(d[p[i] & 15]); }
  return s;
}

bool read_file(const std::string& path, std::vector<uint8_t>& out, std::string* err) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) { if (err) *err = "cannot open " + path; return false; }
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (n < 0) { std::fclose(f); if (err) *err = "cannot size " + path; return false; }
  out.resize(size_t(n));
  size_t got = n ? std::fread(out.data(), 1, size_t(n), f) : 0;
  std::fclose(f);
  if (got != size_t(n)) { if (err) *err = "short read " + path; return false; }
  return true;
}

bool write_file(const std::string& path, const void* data, size_t n) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  bool ok = std::fwrite(data, 1, n, f) == n;
  return std::fclose(f) == 0 && ok;
}

}  // namespace fsr
