// FIFA STREET Recompiled - XEX2 container + PE image loader.
// Format knowledge cross-checked against Xenia (BSD) kernel/util/xex2_info.h.
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fsr {

enum XexOptKey : uint32_t {
  kXexResourceInfo = 0x000002FF,
  kXexFileFormatInfo = 0x000003FF,
  kXexDeltaPatchDescriptor = 0x000005FF,
  kXexBaseReference = 0x00000405,
  kXexOriginalBaseAddress = 0x00010001,
  kXexEntryPoint = 0x00010100,
  kXexImageBaseAddress = 0x00010201,
  kXexImportLibraries = 0x000103FF,
  kXexOriginalPeName = 0x000183FF,
  kXexStaticLibraries = 0x000200FF,
  kXexTlsInfo = 0x00020104,
  kXexDefaultStackSize = 0x00020200,
  kXexDefaultHeapSize = 0x00020401,
  kXexSystemFlags = 0x00030000,
  kXexExecutionInfo = 0x00040006,
  kXexTitleWorkspaceSize = 0x00040201,
  kXexAdditionalTitleMemory = 0x00040801,
};

enum class XexEncryption : uint16_t { None = 0, Normal = 1 };
enum class XexCompression : uint16_t { None = 0, Basic = 1, Normal = 2, Delta = 3 };

struct XexImportRecord {
  uint32_t record_addr = 0;  // guest address of the record slot
  uint8_t type = 0;          // 0 = variable, 1 = function thunk
  uint16_t ordinal = 0;
  std::string name;          // from export tables; "" when unknown
};

struct XexImportLibrary {
  std::string name;
  uint32_t version = 0, version_min = 0;
  std::vector<uint32_t> record_addrs;
  std::vector<XexImportRecord> records;  // filled by resolve_imports()
};

struct XexResource {
  std::string name;
  uint32_t address = 0, size = 0;
};

struct XexStaticLib {
  std::string name;
  uint16_t major = 0, minor = 0, build = 0, qfe = 0;
};

struct XexInfo {
  uint32_t module_flags = 0, header_size = 0, security_offset = 0, header_count = 0;
  std::vector<std::pair<uint32_t, uint32_t>> opt_headers;  // key, value/offset
  uint32_t entry_point = 0, image_base = 0, original_base = 0;
  uint32_t image_size = 0, load_address = 0, image_flags = 0, region = 0, allowed_media = 0;
  uint32_t stack_size = 0, heap_size = 0, system_flags = 0, title_workspace = 0;
  bool has_exec_info = false;
  uint32_t media_id = 0, version = 0, base_version = 0, title_id = 0, savegame_id = 0;
  uint8_t platform = 0, executable_type = 0, disc_number = 0, disc_count = 0;
  XexEncryption encryption = XexEncryption::None;
  XexCompression compression = XexCompression::None;
  uint32_t lzx_window = 0;
  uint32_t page_descriptor_count = 0;
  uint8_t encrypted_session_key[16] = {};
  std::string original_pe_name;
  std::vector<XexStaticLib> static_libs;
  std::vector<XexResource> resources;
  std::vector<XexImportLibrary> imports;
  uint32_t tls_slot_count = 0, tls_raw_address = 0, tls_data_size = 0, tls_raw_size = 0;
};

struct PeSection {
  std::string name;
  uint32_t va = 0, vsize = 0, raw_offset = 0, raw_size = 0, flags = 0;
  bool executable() const { return flags & 0x20000000u; }  // IMAGE_SCN_MEM_EXECUTE
};

struct FunctionEntry {
  uint32_t start = 0, size = 0;
};

enum class LoadStatus {
  Ok,
  NeedsKey,          // image is encrypted and no key was supplied
  KeyMismatch,       // decryption produced garbage (wrong key)
  UnsupportedFormat, // delta patch or unknown compression
  DecompressFailed,
  BadPe,
};
const char* to_string(LoadStatus s);

class XexModule {
 public:
  // Parses headers only (cheap; used by the library scanner).
  bool parse(std::vector<uint8_t> file, std::string& err);
  // Decrypts (with the user-provided 16-byte console XEX key, if needed),
  // decompresses and maps the PE image. Then parses PE sections, .pdata and
  // import records.
  LoadStatus load_image(const uint8_t* user_key, std::string& err);

  const XexInfo& info() const { return info_; }
  const std::vector<uint8_t>& image() const { return image_; }
  uint32_t base() const { return info_.load_address; }
  bool image_loaded() const { return !image_.empty(); }
  bool in_image(uint32_t addr, uint32_t n = 1) const {
    return addr >= base() && uint64_t(addr) + n <= uint64_t(base()) + image_.size();
  }
  uint32_t read32(uint32_t addr) const;

  const std::vector<PeSection>& sections() const { return sections_; }
  const std::vector<FunctionEntry>& pdata() const { return pdata_; }

  // Raw header bytes (for diagnostics).
  const std::vector<uint8_t>& file() const { return file_; }

 private:
  bool parse_opt_headers(std::string& err);
  bool parse_pe(std::string& err);
  void resolve_imports();
  bool derive_session_key(const uint8_t* user_key, uint8_t out[16]);

  std::vector<uint8_t> file_;
  std::vector<uint8_t> image_;
  XexInfo info_;
  std::vector<PeSection> sections_;
  std::vector<FunctionEntry> pdata_;
  // Basic compression block list / normal compression first block.
  std::vector<std::pair<uint32_t, uint32_t>> basic_blocks_;
  uint32_t first_block_size_ = 0;
  uint8_t first_block_hash_[20] = {};
};

// Ordinal -> name lookups (tables derived from Xenia, BSD-3-Clause).
const char* kernel_export_name(const std::string& lib, uint16_t ordinal, bool* is_variable = nullptr);
int kernel_export_ordinal(const std::string& lib, const std::string& name);

// LZX (libmspack, LGPL-2.1). Returns 0 on success.
int lzx_decompress(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_len, uint32_t window_size);

}  // namespace fsr
