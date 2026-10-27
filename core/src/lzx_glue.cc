// Memory-backed mspack_system for libmspack's LZX decoder (LGPL-2.1,
// third_party/mspack, as modified by the Xenia project).
// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "fsr/xex.h"

extern "C" {
#include "lzx.h"
#include "mspack.h"
void xenia_log(const char* fmt, ...) {
#ifdef FSR_LZX_DEBUG
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(stderr, fmt, ap);
  va_end(ap);
  std::fputc('\n', stderr);
#else
  (void)fmt;
#endif
}
}

namespace fsr {
namespace {

struct MemFile {
  uint8_t* buf;
  off_t size;
  off_t off;
};

int mem_read(mspack_file* f, void* buf, int n) {
  auto* m = reinterpret_cast<MemFile*>(f);
  off_t take = std::min<off_t>(n, m->size - m->off);
  std::memcpy(buf, m->buf + m->off, size_t(take));
  m->off += take;
  return int(take);
}
int mem_write(mspack_file* f, void* buf, int n) {
  auto* m = reinterpret_cast<MemFile*>(f);
  off_t take = std::min<off_t>(n, m->size - m->off);
  std::memcpy(m->buf + m->off, buf, size_t(take));
  m->off += take;
  return int(take);
}
void* mem_alloc(mspack_system*, size_t n) { return std::calloc(n, 1); }
void mem_free(void* p) { std::free(p); }
void mem_copy(void* s, void* d, size_t n) { std::memcpy(d, s, n); }

}  // namespace

int lzx_decompress(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_len, uint32_t window_size) {
  if (!window_size || (window_size & (window_size - 1))) return 1;
  int window_bits = __builtin_ctz(window_size);
  if (window_bits < 15 || window_bits > 21) return 1;
  mspack_system sys{};
  sys.read = mem_read;
  sys.write = mem_write;
  sys.alloc = mem_alloc;
  sys.free = mem_free;
  sys.copy = mem_copy;
  MemFile in{const_cast<uint8_t*>(src), off_t(src_len), 0};
  MemFile out{dst, off_t(dst_len), 0};
  lzxd_stream* s = lzxd_init(&sys, reinterpret_cast<mspack_file*>(&in), reinterpret_cast<mspack_file*>(&out),
                             window_bits, 0, 0x8000, off_t(dst_len), 0);
  if (!s) return 1;
  int rc = lzxd_decompress(s, off_t(dst_len));
  lzxd_free(s);
  return rc;
}

}  // namespace fsr
