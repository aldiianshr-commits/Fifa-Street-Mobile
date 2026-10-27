// mkxex - packs a statically linked big-endian PowerPC ELF (our own homebrew
// test programs) into an XEX2 container with a PE image, .pdata function
// table and kernel import records. Used only to produce legal test inputs for
// the loader/recompiler; it is not a general XEX authoring tool.
//
// usage: mkxex in.elf out.xex [--import lib:Name]... [--basic] [--key keyfile]
//        [--syms out.syms]
// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "fsr/bytes.h"
#include "fsr/crypto.h"
#include "fsr/xex.h"

using namespace fsr;

struct Sec {
  std::string name;
  uint32_t type = 0;
  uint64_t flags = 0, addr = 0, off = 0, size = 0;
  uint32_t link = 0;
  uint64_t entsize = 0;
};
struct Sym {
  std::string name;
  uint64_t value = 0, size = 0;
  uint8_t type = 0;
  uint16_t shndx = 0;
};

static void die(const std::string& m) {
  std::fprintf(stderr, "mkxex: %s\n", m.c_str());
  std::exit(1);
}

static bool load_key(const std::string& path, uint8_t key[16]) {
  std::vector<uint8_t> d;
  if (!read_file(path, d)) return false;
  if (d.size() == 16) { std::memcpy(key, d.data(), 16); return true; }
  std::string hex;
  for (uint8_t c : d) if (std::isxdigit(c)) hex.push_back(char(c));
  if (hex.size() != 32) return false;
  for (int i = 0; i < 16; i++) key[i] = uint8_t(std::stoul(hex.substr(2 * i, 2), nullptr, 16));
  return true;
}

int main(int argc, char** argv) {
  if (argc < 3) die("usage: mkxex in.elf out.xex [--import lib:Name]... [--basic] [--key keyfile] [--syms out]");
  std::string in = argv[1], outp = argv[2], syms_out, key_path;
  bool basic = false;
  std::vector<std::pair<std::string, std::string>> imports;
  for (int i = 3; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--basic") basic = true;
    else if (a == "--key" && i + 1 < argc) key_path = argv[++i];
    else if (a == "--syms" && i + 1 < argc) syms_out = argv[++i];
    else if (a == "--import" && i + 1 < argc) {
      std::string s = argv[++i];
      auto c = s.find(':');
      if (c == std::string::npos) die("bad --import " + s);
      imports.emplace_back(s.substr(0, c), s.substr(c + 1));
    } else die("unknown arg " + a);
  }

  std::vector<uint8_t> elf;
  std::string err;
  if (!read_file(in, elf, &err)) die(err);
  if (elf.size() < 64 || std::memcmp(elf.data(), "\x7F" "ELF", 4) != 0) die("not ELF");
  bool is64 = elf[4] == 2;
  if (elf[5] != 2) die("ELF must be big-endian");
  Reader r(elf);
  uint64_t entry = is64 ? r.be32(24 + 4) | (uint64_t(r.be32(24)) << 32) : r.be32(24);
  uint64_t shoff = is64 ? (uint64_t(r.be32(40)) << 32 | r.be32(44)) : r.be32(32);
  uint16_t shentsize = is64 ? r.be16(58) : r.be16(46);
  uint16_t shnum = is64 ? r.be16(60) : r.be16(48);
  uint16_t shstrndx = is64 ? r.be16(62) : r.be16(50);
  auto rd64 = [&](size_t o) { return uint64_t(r.be32(o)) << 32 | r.be32(o + 4); };
  std::vector<Sec> secs(shnum);
  for (uint16_t i = 0; i < shnum; i++) {
    size_t o = shoff + size_t(i) * shentsize;
    Sec& s = secs[i];
    s.type = r.be32(o + 4);
    if (is64) {
      s.flags = rd64(o + 8); s.addr = rd64(o + 16); s.off = rd64(o + 24); s.size = rd64(o + 32);
      s.link = r.be32(o + 40); s.entsize = rd64(o + 56);
    } else {
      s.flags = r.be32(o + 8); s.addr = r.be32(o + 12); s.off = r.be32(o + 16); s.size = r.be32(o + 20);
      s.link = r.be32(o + 24); s.entsize = r.be32(o + 36);
    }
    s.name = std::to_string(r.be32(o));  // resolved below
  }
  if (!r.ok) die("truncated ELF section table");
  const Sec& shstr = secs[shstrndx];
  for (uint16_t i = 0; i < shnum; i++) {
    size_t o = shoff + size_t(i) * shentsize;
    secs[i].name = r.cstr(shstr.off + r.be32(o), 64);
  }

  // Symbols.
  std::vector<Sym> syms;
  for (auto& s : secs) {
    if (s.type != 2 /* SHT_SYMTAB */) continue;
    const Sec& strtab = secs[s.link];
    size_t esz = is64 ? 24 : 16;
    for (size_t o = s.off; o + esz <= s.off + s.size; o += esz) {
      Sym y;
      y.name = r.cstr(strtab.off + r.be32(o), 256);
      if (is64) { y.type = r.u8(o + 4) & 15; y.shndx = r.be16(o + 6); y.value = rd64(o + 8); y.size = rd64(o + 16); }
      else { y.value = r.be32(o + 4); y.size = r.be32(o + 8); y.type = r.u8(o + 12) & 15; y.shndx = r.be16(o + 14); }
      if (!y.name.empty()) syms.push_back(y);
    }
  }

  // Image layout: [0x0000 headers][ELF alloc sections][.pdata][.fsrimp].
  uint64_t lo = ~0ull, hi = 0;
  for (auto& s : secs)
    if ((s.flags & 2) && s.size) { lo = std::min(lo, s.addr); hi = std::max(hi, s.addr + s.size); }
  if (lo == ~0ull) die("no allocatable sections");
  const uint32_t base = uint32_t(lo & ~0xFFFFull);
  if (lo - base < 0x1000) die("link the first section at least 0x1000 above a 64K-aligned base");
  uint32_t cursor = uint32_t((hi + 0xFFF) & ~0xFFFull) - base;

  struct OutSec { std::string name; uint32_t rva, size, flags; };
  std::vector<OutSec> out_secs;
  for (auto& s : secs) {
    if (!(s.flags & 2) || !s.size) continue;
    uint32_t fl = (s.flags & 4) ? 0x60000020u : (s.type == 8 ? 0xC0000080u : (s.flags & 1) ? 0xC0000040u : 0x40000040u);
    out_secs.push_back({s.name.substr(0, 8), uint32_t(s.addr - base), uint32_t(s.size), fl});
  }

  // Function table from STT_FUNC symbols.
  std::vector<std::pair<uint32_t, uint32_t>> funcs;
  std::map<std::string, uint32_t> symaddr;
  for (auto& y : syms) {
    symaddr[y.name] = uint32_t(y.value);
    if (y.type == 2 && y.size && y.value >= base) funcs.emplace_back(uint32_t(y.value), uint32_t(y.size));
  }
  std::sort(funcs.begin(), funcs.end());
  funcs.erase(std::unique(funcs.begin(), funcs.end()), funcs.end());
  uint32_t pdata_rva = cursor, pdata_size = uint32_t(funcs.size() * 8 + 8);
  cursor = (pdata_rva + pdata_size + 0xFFF) & ~0xFFFu;
  out_secs.push_back({".pdata", pdata_rva, pdata_size, 0x40000040u});

  // Import records: one IAT slot (type 0) per import, the thunk lives in code.
  std::map<std::string, std::vector<std::pair<std::string, int>>> libs;
  for (auto& [lib, name] : imports) {
    int ord = kernel_export_ordinal(lib, name);
    if (ord < 0) die("unknown export " + lib + "!" + name);
    if (!symaddr.count(name)) die("no thunk symbol for import " + name);
    libs[lib].emplace_back(name, ord);
  }
  uint32_t imp_rva = cursor, imp_size = uint32_t(imports.size() * 4 + 4);
  cursor = (imp_rva + imp_size + 0xFFF) & ~0xFFFu;
  if (!imports.empty()) out_secs.push_back({".fsrimp", imp_rva, imp_size, 0xC0000040u});
  const uint32_t image_size = cursor;

  std::vector<uint8_t> img(image_size, 0);
  for (auto& s : secs)
    if ((s.flags & 2) && s.size && s.type != 8) {
      if (s.off + s.size > elf.size()) die("section beyond file: " + s.name);
      std::memcpy(img.data() + (s.addr - base), elf.data() + s.off, s.size);
    }
  for (size_t i = 0; i < funcs.size(); i++) {
    wr_be32(&img[pdata_rva + 8 * i], funcs[i].first);
    wr_be32(&img[pdata_rva + 8 * i + 4], (funcs[i].second / 4) << 8);
  }
  // PE headers.
  img[0] = 'M'; img[1] = 'Z';
  wr_le32(&img[0x3C], 0x80);
  std::memcpy(&img[0x80], "PE\0\0", 4);
  uint32_t fh = 0x84;
  wr_le16(&img[fh], 0x01F2);
  wr_le16(&img[fh + 2], uint16_t(out_secs.size()));
  wr_le16(&img[fh + 16], 0xE0);
  wr_le16(&img[fh + 18], 0x0102);
  uint32_t oh = fh + 20;
  wr_le16(&img[oh], 0x010B);
  wr_le32(&img[oh + 16], uint32_t(entry) - base);
  wr_le32(&img[oh + 28], base);
  wr_le32(&img[oh + 56], image_size);
  uint32_t sh = oh + 0xE0;
  if (sh + 40 * out_secs.size() > 0x1000) die("too many sections");
  for (auto& s : out_secs) {
    std::memcpy(&img[sh], s.name.c_str(), std::min<size_t>(8, s.name.size()));
    wr_le32(&img[sh + 8], s.size);
    wr_le32(&img[sh + 12], s.rva);
    wr_le32(&img[sh + 16], s.size);
    wr_le32(&img[sh + 20], s.rva);
    wr_le32(&img[sh + 36], s.flags);
    sh += 40;
  }

  // Import table records + patch thunks/IAT slots in the image.
  std::vector<uint8_t> imp;
  std::vector<std::string> lib_names;
  for (auto& [lib, _] : libs) lib_names.push_back(lib);
  {
    std::vector<uint8_t> st;
    for (auto& n : lib_names) {
      st.insert(st.end(), n.begin(), n.end());
      st.push_back(0);
      while (st.size() % 4) st.push_back(0);
    }
    imp.resize(12);
    wr_be32(&imp[4], uint32_t(st.size()));
    wr_be32(&imp[8], uint32_t(lib_names.size()));
    imp.insert(imp.end(), st.begin(), st.end());
    uint32_t slot = imp_rva;
    for (size_t li = 0; li < lib_names.size(); li++) {
      auto& list = libs[lib_names[li]];
      std::vector<uint32_t> recs;
      for (auto& [name, ord] : list) {
        uint32_t iat = base + slot;
        slot += 4;
        wr_be32(&img[iat - base], uint32_t(ord));
        uint32_t thunk = symaddr[name];
        wr_be32(&img[thunk - base], 0x01000000u | uint32_t(ord));
        recs.push_back(iat);
        recs.push_back(thunk);
      }
      std::vector<uint8_t> L(0x28 + 4 * recs.size(), 0);
      wr_be32(&L[0], uint32_t(L.size()));
      wr_be32(&L[0x18], 0x00000001);
      wr_be32(&L[0x1C], 0x20000000);
      wr_be32(&L[0x20], 0x20000000);
      wr_be16(&L[0x24], uint16_t(li));
      wr_be16(&L[0x26], uint16_t(recs.size()));
      for (size_t k = 0; k < recs.size(); k++) wr_be32(&L[0x28 + 4 * k], recs[k]);
      imp.insert(imp.end(), L.begin(), L.end());
    }
    wr_be32(&imp[0], uint32_t(imp.size()));
  }

  // Encryption / compression of the data area.
  uint8_t key[16] = {}, session[16] = {}, enc_session[16] = {};
  bool encrypt = !key_path.empty();
  if (encrypt) {
    if (!load_key(key_path, key)) die("bad key file " + key_path);
    std::mt19937 rng(0x46535221);
    for (auto& b : session) b = uint8_t(rng());
    Aes128(key).encrypt_block(session, enc_session);
  }
  std::vector<std::pair<uint32_t, uint32_t>> blocks;  // basic compression
  std::vector<uint8_t> data;
  if (basic) {
    const uint32_t chunk = 0x8000;
    for (uint32_t off = 0; off < image_size; off += chunk) {
      uint32_t n = std::min(chunk, image_size - off), last = 0;
      for (uint32_t i = 0; i < n; i++) if (img[off + i]) last = i + 1;
      uint32_t dsz = (last + 15) & ~15u;
      if (dsz > n) dsz = n;
      data.insert(data.end(), img.begin() + off, img.begin() + off + dsz);
      blocks.emplace_back(dsz, n - dsz);
    }
  } else {
    data = img;
  }
  if (encrypt) {
    uint8_t iv[16] = {};
    Aes128 aes(session);
    aes.cbc_encrypt(data.data(), data.data(), data.size() & ~size_t(15), iv);
  }

  // Assemble XEX2 header.
  std::vector<std::pair<uint32_t, std::vector<uint8_t>>> blobs;  // key -> data (offset headers)
  std::vector<std::pair<uint32_t, uint32_t>> values;             // key -> inline value
  {
    std::vector<uint8_t> ff(8 + (basic ? 8 * blocks.size() : 0));
    wr_be32(&ff[0], uint32_t(ff.size()));
    wr_be16(&ff[4], encrypt ? 1 : 0);
    wr_be16(&ff[6], basic ? 1 : 0);
    for (size_t i = 0; i < blocks.size(); i++) {
      wr_be32(&ff[8 + 8 * i], blocks[i].first);
      wr_be32(&ff[12 + 8 * i], blocks[i].second);
    }
    blobs.emplace_back(kXexFileFormatInfo, ff);
    std::vector<uint8_t> ei(24, 0);
    wr_be32(&ei[0], 0x12345678);
    wr_be32(&ei[4], 0x00010000);
    wr_be32(&ei[12], 0x46535231);  // 'FSR1' homebrew test title id
    ei[18] = 1; ei[19] = 1;
    blobs.emplace_back(kXexExecutionInfo, ei);
    if (!imports.empty()) blobs.emplace_back(kXexImportLibraries, imp);
    std::string pe_name = "homebrew.exe";
    std::vector<uint8_t> pn(4 + ((pe_name.size() + 4) & ~3u), 0);
    wr_be32(&pn[0], uint32_t(pn.size()));
    std::memcpy(&pn[4], pe_name.data(), pe_name.size());
    blobs.emplace_back(kXexOriginalPeName, pn);
    values.emplace_back(kXexEntryPoint, uint32_t(entry));
    values.emplace_back(kXexImageBaseAddress, base);
    values.emplace_back(kXexDefaultStackSize, 0x40000);
  }
  uint32_t count = uint32_t(blobs.size() + values.size());
  uint32_t off = 0x18 + 8 * count;
  std::vector<uint8_t> hdr(off, 0);
  std::vector<std::pair<uint32_t, uint32_t>> table;
  for (auto& [k, v] : values) table.emplace_back(k, v);
  for (auto& [k, b] : blobs) {
    while (hdr.size() % 8) hdr.push_back(0);
    table.emplace_back(k, uint32_t(hdr.size()));
    hdr.insert(hdr.end(), b.begin(), b.end());
  }
  std::sort(table.begin(), table.end());
  while (hdr.size() % 16) hdr.push_back(0);
  uint32_t sec_off = uint32_t(hdr.size());
  std::vector<uint8_t> sec(0x184, 0);
  wr_be32(&sec[0x4], image_size);
  wr_be32(&sec[0x10C], 0);
  wr_be32(&sec[0x110], base);
  std::memcpy(&sec[0x150], enc_session, 16);
  hdr.insert(hdr.end(), sec.begin(), sec.end());
  while (hdr.size() % 0x1000) hdr.push_back(0);
  wr_be32(&sec[0], 0);
  std::memcpy(&hdr[0], "XEX2", 4);
  wr_be32(&hdr[4], 1);
  wr_be32(&hdr[8], uint32_t(hdr.size()));
  wr_be32(&hdr[0x10], sec_off);
  wr_be32(&hdr[0x14], count);
  for (size_t i = 0; i < table.size(); i++) {
    wr_be32(&hdr[0x18 + 8 * i], table[i].first);
    wr_be32(&hdr[0x18 + 8 * i + 4], table[i].second);
  }
  wr_be32(&hdr[sec_off], uint32_t(hdr.size()));
  hdr.insert(hdr.end(), data.begin(), data.end());
  if (!write_file(outp, hdr.data(), hdr.size())) die("cannot write " + outp);

  if (!syms_out.empty()) {
    std::string s;
    for (auto& y : syms)
      if (y.type == 2 && y.value) s += y.name + " " + hex32(uint32_t(y.value)) + "\n";
    write_file(syms_out, s.data(), s.size());
  }
  std::printf("mkxex: %s base=%08X image=%u bytes funcs=%zu imports=%zu %s%s\n", outp.c_str(), base, image_size,
              funcs.size(), imports.size(), basic ? "basic " : "", encrypt ? "encrypted" : "");
  return 0;
}
