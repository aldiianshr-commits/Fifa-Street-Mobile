// mkiso - builds a small XDVDFS image (test fixture only) from files given as
// "disc/path=host/path" pairs.  usage: mkiso out.iso [--xgd2] path=file ...
// SPDX-License-Identifier: MIT
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

static uint32_t place(const std::vector<uint8_t>& d) {
  uint32_t s = next_sector;
  uint64_t off = part + uint64_t(s) * 2048;
  if (img.size() < off + d.size()) img.resize(off + d.size());
  std::copy(d.begin(), d.end(), img.begin() + off);
  next_sector += uint32_t((d.size() + 2047) / 2048) + (d.empty() ? 1 : 0);
  return s;
}

static void layout(Node& n) {
  for (auto& [name, k] : n.kids) {
    if (k.dir) layout(k);
    else { k.sector = place(k.data); k.size = uint32_t(k.data.size()); }
  }
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
  n.sector = place(dir);
  n.size = uint32_t(dir.size());
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
  uint64_t off = part + 32 * 2048;
  if (img.size() < off + 2048) img.resize(off + 2048);
  std::copy(vd.begin(), vd.end(), img.begin() + off);
  if (!write_file(argv[1], img.data(), img.size())) return 1;
  std::printf("mkiso: %s (%zu bytes)\n", argv[1], img.size());
  return 0;
}
