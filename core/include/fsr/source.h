// FIFA STREET Recompiled - random-access byte sources. A disc image is read
// in place through pread() on a file descriptor (on Android: the descriptor
// handed out by the Storage Access Framework), so nothing is extracted.
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fsr {

class ReadSource {
 public:
  virtual ~ReadSource() = default;
  virtual uint64_t size() const = 0;
  // Reads exactly n bytes at offset; false on short read or error.
  virtual bool read(uint64_t offset, void* dst, size_t n) const = 0;
  virtual std::string describe() const = 0;
};

class FdSource : public ReadSource {
 public:
  // Takes ownership of fd. Fails (returns nullptr) if the descriptor is not
  // seekable (e.g. a pipe from a streaming cloud provider).
  static std::shared_ptr<FdSource> adopt(int fd, std::string* err);
  static std::shared_ptr<FdSource> open(const std::string& path, std::string* err);
  ~FdSource() override;
  uint64_t size() const override { return size_; }
  bool read(uint64_t offset, void* dst, size_t n) const override;
  std::string describe() const override { return "fd:" + std::to_string(fd_); }

 private:
  FdSource(int fd, uint64_t size) : fd_(fd), size_(size) {}
  int fd_;
  uint64_t size_;
};

class MemorySource : public ReadSource {
 public:
  explicit MemorySource(std::vector<uint8_t> data) : data_(std::move(data)) {}
  uint64_t size() const override { return data_.size(); }
  bool read(uint64_t offset, void* dst, size_t n) const override;
  std::string describe() const override { return "memory"; }

 private:
  std::vector<uint8_t> data_;
};

// A window [offset, offset+size) of another source (one file inside a disc).
class SliceSource : public ReadSource {
 public:
  SliceSource(std::shared_ptr<const ReadSource> parent, uint64_t offset, uint64_t size)
      : parent_(std::move(parent)), offset_(offset), size_(size) {}
  uint64_t size() const override { return size_; }
  bool read(uint64_t offset, void* dst, size_t n) const override {
    if (offset > size_ || n > size_ - offset) return false;
    return parent_->read(offset_ + offset, dst, n);
  }
  std::string describe() const override { return parent_->describe() + "+slice"; }

 private:
  std::shared_ptr<const ReadSource> parent_;
  uint64_t offset_, size_;
};

}  // namespace fsr
