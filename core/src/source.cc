// SPDX-License-Identifier: MIT
#include "fsr/source.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>

namespace fsr {

std::shared_ptr<FdSource> FdSource::adopt(int fd, std::string* err) {
  if (fd < 0) {
    if (err) *err = "invalid file descriptor";
    return nullptr;
  }
  struct stat st {};
  if (fstat(fd, &st) != 0) {
    if (err) *err = std::string("fstat failed: ") + std::strerror(errno);
    ::close(fd);
    return nullptr;
  }
  off_t end = lseek(fd, 0, SEEK_END);
  if (end < 0 || !S_ISREG(st.st_mode)) {
    if (err)
      *err = "the storage provider returned a non-seekable stream (not a regular file); random access is not possible";
    ::close(fd);
    return nullptr;
  }
  return std::shared_ptr<FdSource>(new FdSource(fd, uint64_t(end)));
}

std::shared_ptr<FdSource> FdSource::open(const std::string& path, std::string* err) {
  int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    if (err) *err = "cannot open " + path + ": " + std::strerror(errno);
    return nullptr;
  }
  return adopt(fd, err);
}

FdSource::~FdSource() {
  if (fd_ >= 0) ::close(fd_);
}

bool FdSource::read(uint64_t offset, void* dst, size_t n) const {
  if (offset > size_ || n > size_ - offset) return false;
  auto* p = static_cast<uint8_t*>(dst);
  while (n) {
    ssize_t r = ::pread(fd_, p, n, off_t(offset));
    if (r < 0 && errno == EINTR) continue;
    if (r <= 0) return false;
    p += r;
    offset += uint64_t(r);
    n -= size_t(r);
  }
  return true;
}

bool MemorySource::read(uint64_t offset, void* dst, size_t n) const {
  if (offset > data_.size() || n > data_.size() - offset) return false;
  std::memcpy(dst, data_.data() + offset, n);
  return true;
}

}  // namespace fsr
