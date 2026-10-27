// SPDX-License-Identifier: MIT
#include "fsr/xex.h"

#include <algorithm>
#include <cstring>

#include "fsr/bytes.h"
#include "fsr/crypto.h"

namespace fsr {

const char* to_string(LoadStatus s) {
  switch (s) {
    case LoadStatus::Ok: return "ok";
    case LoadStatus::NeedsKey: return "needs-key";
    case LoadStatus::KeyMismatch: return "key-mismatch";
    case LoadStatus::UnsupportedFormat: return "unsupported-format";
    case LoadStatus::DecompressFailed: return "decompress-failed";
    case LoadStatus::BadPe: return "bad-pe";
  }
  return "?";
}

bool XexModule::parse(std::vector<uint8_t> file, std::string& err) {
  file_ = std::move(file);
  image_.clear();
  sections_.clear();
  pdata_.clear();
  info_ = XexInfo{};
  Reader r(file_);
  if (file_.size() < 0x18 || std::memcmp(file_.data(), "XEX2", 4) != 0) {
    if (file_.size() >= 4 && (std::memcmp(file_.data(), "XEX1", 4) == 0 ||
                              std::memcmp(file_.data(), "XEX%", 4) == 0 ||
                              std::memcmp(file_.data(), "XEX-", 4) == 0))
      err = "pre-release XEX format (not XEX2) is not supported";
    else
      err = "not an XEX2 file (bad magic)";
    return false;
  }
  info_.module_flags = r.be32(4);
  info_.header_size = r.be32(8);
  info_.security_offset = r.be32(0x10);
  info_.header_count = r.be32(0x14);
  if (info_.header_count > 4096 || info_.header_size > file_.size()) {
    err = "corrupt XEX header (header count/size out of range)";
    return false;
  }
  for (uint32_t i = 0; i < info_.header_count; i++)
    info_.opt_headers.emplace_back(r.be32(0x18 + 8 * i), r.be32(0x18 + 8 * i + 4));
  if (!r.ok) { err = "truncated optional header table"; return false; }

  // Security info.
  uint32_t so = info_.security_offset;
  info_.image_size = r.be32(so + 0x4);
  info_.image_flags = r.be32(so + 0x10C);
  info_.load_address = r.be32(so + 0x110);
  if (const uint8_t* k = r.ptr(so + 0x150, 16)) std::memcpy(info_.encrypted_session_key, k, 16);
  info_.region = r.be32(so + 0x178);
  info_.allowed_media = r.be32(so + 0x17C);
  info_.page_descriptor_count = r.be32(so + 0x180);
  if (!r.ok) { err = "truncated security info"; return false; }
  if (info_.image_size == 0 || info_.image_size > 512u * 1024 * 1024) {
    err = "implausible image size " + hex32(info_.image_size);
    return false;
  }
  return parse_opt_headers(err);
}

bool XexModule::parse_opt_headers(std::string& err) {
  Reader r(file_);
  bool have_format = false;
  for (auto [key, val] : info_.opt_headers) {
    switch (key) {
      case kXexEntryPoint: info_.entry_point = val; break;
      case kXexImageBaseAddress: info_.image_base = val; break;
      case kXexOriginalBaseAddress: info_.original_base = val; break;
      case kXexDefaultStackSize: info_.stack_size = val; break;
      case kXexDefaultHeapSize: info_.heap_size = val; break;
      case kXexSystemFlags: info_.system_flags = val; break;
      case kXexTitleWorkspaceSize: info_.title_workspace = val; break;
      case kXexExecutionInfo:
        info_.has_exec_info = r.has(val, 24);
        info_.media_id = r.be32(val);
        info_.version = r.be32(val + 4);
        info_.base_version = r.be32(val + 8);
        info_.title_id = r.be32(val + 12);
        info_.platform = r.u8(val + 16);
        info_.executable_type = r.u8(val + 17);
        info_.disc_number = r.u8(val + 18);
        info_.disc_count = r.u8(val + 19);
        info_.savegame_id = r.be32(val + 20);
        break;
      case kXexTlsInfo:
        info_.tls_slot_count = r.be32(val);
        info_.tls_raw_address = r.be32(val + 4);
        info_.tls_data_size = r.be32(val + 8);
        info_.tls_raw_size = r.be32(val + 12);
        break;
      case kXexOriginalPeName: {
        uint32_t sz = r.be32(val);
        info_.original_pe_name = r.cstr(val + 4, sz > 4 ? sz - 4 : 0);
      } break;
      case kXexStaticLibraries: {
        uint32_t sz = r.be32(val);
        for (uint32_t o = 4; o + 16 <= sz && r.ok; o += 16) {
          XexStaticLib lib;
          lib.name = r.cstr(val + o, 8);
          lib.major = r.be16(val + o + 8);
          lib.minor = r.be16(val + o + 10);
          lib.build = r.be16(val + o + 12);
          lib.qfe = r.u8(val + o + 15);
          info_.static_libs.push_back(lib);
        }
      } break;
      case kXexResourceInfo: {
        uint32_t sz = r.be32(val);
        for (uint32_t o = 4; o + 16 <= sz && r.ok; o += 16) {
          XexResource res;
          res.name = r.cstr(val + o, 8);
          res.address = r.be32(val + o + 8);
          res.size = r.be32(val + o + 12);
          info_.resources.push_back(res);
        }
      } break;
      case kXexFileFormatInfo: {
        have_format = true;
        uint32_t sz = r.be32(val);
        info_.encryption = XexEncryption(r.be16(val + 4));
        info_.compression = XexCompression(r.be16(val + 6));
        if (info_.compression == XexCompression::Basic) {
          for (uint32_t o = 8; o + 8 <= sz && r.ok; o += 8)
            basic_blocks_.emplace_back(r.be32(val + o), r.be32(val + o + 4));
        } else if (info_.compression == XexCompression::Normal) {
          info_.lzx_window = r.be32(val + 8);
          first_block_size_ = r.be32(val + 12);
          if (const uint8_t* h = r.ptr(val + 16, 20)) std::memcpy(first_block_hash_, h, 20);
        }
      } break;
      case kXexImportLibraries: {
        uint32_t total = r.be32(val);
        uint32_t st_size = r.be32(val + 4);
        uint32_t st_count = r.be32(val + 8);
        std::vector<std::string> names;
        for (uint32_t i = 0, o = 0; i < st_size && o < st_count && r.ok; o++) {
          std::string s = r.cstr(val + 12 + i, st_size - i);
          names.push_back(s);
          i += uint32_t(s.size()) + 1;
          if (i % 4) i += 4 - i % 4;
        }
        uint32_t off = st_size + 12;
        while (off + 0x28 <= total && r.ok) {
          uint32_t lib_size = r.be32(val + off);
          if (!lib_size) break;
          XexImportLibrary lib;
          lib.version = r.be32(val + off + 0x1C);
          lib.version_min = r.be32(val + off + 0x20);
          uint16_t name_index = r.be16(val + off + 0x24) & 0xFF;
          uint16_t count = r.be16(val + off + 0x26);
          lib.name = name_index < names.size() ? names[name_index] : "?";
          for (uint32_t i = 0; i < count && r.ok; i++)
            lib.record_addrs.push_back(r.be32(val + off + 0x28 + 4 * i));
          info_.imports.push_back(std::move(lib));
          off += lib_size;
        }
      } break;
      default: break;
    }
  }
  if (!r.ok) { err = "truncated optional header data"; return false; }
  if (!have_format) { err = "missing file format info header"; return false; }
  if (!info_.load_address) info_.load_address = info_.image_base;
  if (!info_.image_base) info_.image_base = info_.load_address;
  return true;
}

bool XexModule::derive_session_key(const uint8_t* user_key, uint8_t out[16]) {
  if (!user_key) return false;
  Aes128 aes(user_key);
  aes.decrypt_block(info_.encrypted_session_key, out);
  return true;
}

LoadStatus XexModule::load_image(const uint8_t* user_key, std::string& err) {
  image_.clear();
  const uint32_t hs = info_.header_size;
  if (hs > file_.size()) { err = "header size beyond file"; return LoadStatus::BadPe; }
  const uint8_t* src = file_.data() + hs;
  const size_t src_len = file_.size() - hs;

  // 1) Decrypt the data area (AES-128-CBC, zero IV) when required.
  std::vector<uint8_t> plain;
  const uint8_t* data = src;
  if (info_.encryption == XexEncryption::Normal) {
    uint8_t session[16];
    if (!derive_session_key(user_key, session)) {
      err = "image is encrypted; supply your own console XEX key or a decrypted XEX";
      return LoadStatus::NeedsKey;
    }
    plain.resize(src_len);
    uint8_t iv[16] = {};
    Aes128 aes(session);
    aes.cbc_decrypt(src, plain.data(), src_len & ~size_t(15), iv);
    data = plain.data();
  } else if (info_.encryption != XexEncryption::None) {
    err = "unknown encryption type";
    return LoadStatus::UnsupportedFormat;
  }

  // 2) Decompress into the image buffer.
  image_.assign(info_.image_size, 0);
  switch (info_.compression) {
    case XexCompression::None:
      std::memcpy(image_.data(), data, std::min<size_t>(src_len, image_.size()));
      break;
    case XexCompression::Basic: {
      size_t s = 0, d = 0;
      for (auto [dsz, zsz] : basic_blocks_) {
        if (s + dsz > src_len || d + dsz > image_.size()) {
          err = "basic compression block overflows";
          image_.clear();
          return LoadStatus::DecompressFailed;
        }
        std::memcpy(image_.data() + d, data + s, dsz);
        s += dsz;
        d += size_t(dsz) + zsz;
      }
    } break;
    case XexCompression::Normal: {
      std::vector<uint8_t> lzx;
      lzx.reserve(src_len);
      size_t p = 0;
      uint32_t bsize = first_block_size_;
      uint8_t bhash[20];
      std::memcpy(bhash, first_block_hash_, 20);
      while (bsize) {
        if (p + bsize > src_len || bsize < 24) {
          err = "compressed block chain runs past end of file";
          image_.clear();
          return LoadStatus::DecompressFailed;
        }
        uint8_t h[20];
        Sha1::hash(data + p, bsize, h);
        if (std::memcmp(h, bhash, 20) != 0) {
          err = info_.encryption == XexEncryption::Normal
                    ? "block hash mismatch: the supplied key is wrong for this XEX"
                    : "block hash mismatch: file is corrupt";
          image_.clear();
          return info_.encryption == XexEncryption::Normal ? LoadStatus::KeyMismatch
                                                           : LoadStatus::DecompressFailed;
        }
        uint32_t next_size = rd_be32(data + p);
        uint8_t next_hash[20];
        std::memcpy(next_hash, data + p + 4, 20);
        size_t q = p + 24, end = p + bsize;
        while (q + 2 <= end) {
          uint32_t chunk = rd_be16(data + q);
          q += 2;
          if (!chunk) break;
          if (q + chunk > end) { err = "chunk overflows block"; image_.clear(); return LoadStatus::DecompressFailed; }
          lzx.insert(lzx.end(), data + q, data + q + chunk);
          q += chunk;
        }
        p = end;
        bsize = next_size;
        std::memcpy(bhash, next_hash, 20);
      }
      if (lzx_decompress(lzx.data(), lzx.size(), image_.data(), image_.size(), info_.lzx_window) != 0) {
        err = "LZX decompression failed";
        image_.clear();
        return LoadStatus::DecompressFailed;
      }
    } break;
    case XexCompression::Delta:
      err = "delta-patch XEX (title update) cannot be loaded standalone";
      image_.clear();
      return LoadStatus::UnsupportedFormat;
    default:
      err = "unknown compression type";
      image_.clear();
      return LoadStatus::UnsupportedFormat;
  }

  if (image_.size() < 2 || image_[0] != 'M' || image_[1] != 'Z') {
    bool enc = info_.encryption == XexEncryption::Normal;
    err = enc ? "decrypted image has no MZ header: the supplied key is wrong"
              : "image has no MZ header (corrupt or still encrypted)";
    image_.clear();
    return enc ? LoadStatus::KeyMismatch : LoadStatus::BadPe;
  }
  if (!parse_pe(err)) { image_.clear(); return LoadStatus::BadPe; }
  resolve_imports();
  return LoadStatus::Ok;
}

uint32_t XexModule::read32(uint32_t addr) const {
  if (!in_image(addr, 4)) return 0;
  return rd_be32(image_.data() + (addr - base()));
}

bool XexModule::parse_pe(std::string& err) {
  Reader r(image_);
  uint32_t pe = r.le32(0x3C);
  if (r.le32(pe) != 0x00004550) { err = "missing PE signature"; return false; }
  uint16_t machine = r.le16(pe + 4);
  if (machine != 0x01F2) { err = "PE machine is not PowerPC big-endian (0x1F2)"; return false; }
  uint16_t nsec = r.le16(pe + 6);
  uint16_t opt_size = r.le16(pe + 20);
  uint32_t sh = pe + 24 + opt_size;
  for (uint16_t i = 0; i < nsec && r.ok; i++, sh += 40) {
    PeSection s;
    s.name = r.cstr(sh, 8);
    s.vsize = r.le32(sh + 8);
    s.va = r.le32(sh + 12);
    s.raw_size = r.le32(sh + 16);
    s.raw_offset = r.le32(sh + 20);
    s.flags = r.le32(sh + 36);
    sections_.push_back(s);
  }
  if (!r.ok) { err = "truncated PE section table"; return false; }
  for (auto& s : sections_) {
    if (s.name != ".pdata") continue;
    uint32_t n = std::min<uint32_t>(s.vsize ? s.vsize : s.raw_size, uint32_t(image_.size()) - std::min<uint32_t>(s.va, uint32_t(image_.size())));
    for (uint32_t o = 0; o + 8 <= n; o += 8) {
      uint32_t begin = rd_be32(image_.data() + s.va + o);
      uint32_t bits = rd_be32(image_.data() + s.va + o + 4);
      if (!begin) break;
      if (begin < base()) begin += base();
      FunctionEntry f;
      f.start = begin;
      f.size = ((bits >> 8) & 0x3FFFFF) * 4;
      pdata_.push_back(f);
    }
  }
  std::sort(pdata_.begin(), pdata_.end(), [](auto& a, auto& b) { return a.start < b.start; });
  return true;
}

void XexModule::resolve_imports() {
  for (auto& lib : info_.imports) {
    lib.records.clear();
    for (uint32_t addr : lib.record_addrs) {
      XexImportRecord rec;
      rec.record_addr = addr;
      uint32_t v = read32(addr);
      rec.type = uint8_t(v >> 24);
      rec.ordinal = uint16_t(v & 0xFFFF);
      const char* n = kernel_export_name(lib.name, rec.ordinal);
      rec.name = n ? n : "";
      lib.records.push_back(rec);
    }
  }
}

}  // namespace fsr
