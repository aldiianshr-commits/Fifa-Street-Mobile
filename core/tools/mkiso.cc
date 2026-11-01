// mkiso - builds a small XDVDFS image (test fixture only) from files given as
// "disc/path=host/path" pairs.  usage: mkiso out.iso [--xgd2] path=file ...
// SPDX-License-Identifier: MIT
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "fsr/bytes.h"

using namespace fsr;

struct Node {
  std::map<std::string, Node> kids;
  std::vector<uint8_t> data;
  bool dir = true;
  uint32_t sector = 0, size = 0;
};

static std::vector<uint8_t> img;
static uint64_t part = 0;
static uint32_t next_sector = 33;

// img holds partition-relative bytes; the partition offset is applied when the
// file is written (sparse), so an XGD2-layout fixture stays small on disk.
static uint32_t place(const std::vector<uint8_t>& d) {
  uint32_t s = next_sector;
  uint64_t off = uint64_t(s) * 2048;
  if (img.size() < off + d.size()) img.resize(off + d.size());
  std::copy(d.begin(), d.end(), img.begin() + off);
  next_sector += uint32_t((d.size() + 2047) / 2048) + (d.empty() ? 1 : 0);
  return s;
}

// Directory tables first (like mastered discs), then file data.
static std::vector<uint8_t> dir_bytes(const Node& n) {
  std::vector<uint8_t> dir;
  std::vector<size_t> offs;
  for (auto& [name, k] : n.kids) {
    offs.push_back(dir.size());
    std::vector<uint8_t> e(14 + name.size(), 0);
    wr_le32(&e[4], k.sector);
    wr_le32(&e[8], k.size);
    e[12] = k.dir ? 0x10 : 0x80;
    e[13] = uint8_t(name.size());
    std::copy(name.begin(), name.end(), e.begin() + 14);
    while (e.size() % 4) e.push_back(0xFF);
    dir.insert(dir.end(), e.begin(), e.end());
  }
  for (size_t i = 0; i + 1 < offs.size(); i++) wr_le16(&dir[offs[i] + 2], uint16_t(offs[i + 1] / 4));
  while (dir.size() % 2048) dir.push_back(0xFF);
  return dir;
}

static void reserve_dirs(Node& n) {
  n.size = uint32_t(dir_bytes(n).size());
  n.sector = next_sector;
  next_sector += n.size / 2048;
  for (auto& [name, k] : n.kids)
    if (k.dir) reserve_dirs(k);
}

static void place_files(Node& n) {
  for (auto& [name, k] : n.kids) {
    if (k.dir) place_files(k);
    else { k.sector = place(k.data); k.size = uint32_t(k.data.size()); }
  }
}

static void write_dirs(Node& n) {
  std::vector<uint8_t> d = dir_bytes(n);
  uint64_t off = uint64_t(n.sector) * 2048;
  if (img.size() < off + d.size()) img.resize(off + d.size());
  std::copy(d.begin(), d.end(), img.begin() + off);
  for (auto& [name, k] : n.kids)
    if (k.dir) write_dirs(k);
}

static void layout(Node& n) {
  reserve_dirs(n);
  place_files(n);
  write_dirs(n);
}

int main(int argc, char** argv) {
  if (argc < 3) { std::fprintf(stderr, "usage: mkiso out.iso [--xgd2] disc/path=host/file ...\n"); return 2; }
  Node root;
  for (int i = 2; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--xgd2") { part = 0xFD90000ull; continue; }
    auto eq = a.find('=');
    if (eq == std::string::npos) return 2;
    std::string dpath = a.substr(0, eq);
    Node* cur = &root;
    size_t p = 0;
    while (true) {
      size_t sl = dpath.find('/', p);
      std::string part_name = dpath.substr(p, sl == std::string::npos ? std::string::npos : sl - p);
      cur = &cur->kids[part_name];
      if (sl == std::string::npos) break;
      p = sl + 1;
    }
    cur->dir = false;
    if (!read_file(a.substr(eq + 1), cur->data)) { std::fprintf(stderr, "cannot read %s\n", a.c_str()); return 1; }
  }
  layout(root);
  std::vector<uint8_t> vd(2048, 0);
  std::copy_n("MICROSOFT*XBOX*MEDIA", 20, vd.begin());
  wr_le32(&vd[20], root.sector);
  wr_le32(&vd[24], root.size);
  std::copy_n("MICROSOFT*XBOX*MEDIA", 20, vd.begin() + 0x7EC);
  uint64_t off = 32 * 2048;
  if (img.size() < off + 2048) img.resize(off + 2048);
  std::copy(vd.begin(), vd.end(), img.begin() + off);
  while (img.size() % 2048) img.push_back(0);
  int fd = ::open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) return 1;
  if (ftruncate(fd, off_t(part + img.size())) != 0 || pwrite(fd, img.data(), img.size(), off_t(part)) != ssize_t(img.size())) {
    ::close(fd);
    return 1;
  }
  ::close(fd);
  std::printf("mkiso: %s (%llu bytes, partition at 0x%llX)\n", argv[1], (unsigned long long)(part + img.size()),
              (unsigned long long)part);
  return 0;
}
