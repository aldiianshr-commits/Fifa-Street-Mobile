/* FIFA STREET Recompiled - homebrew file-system test (original code, MIT).
 * Reads files from the mounted disc (game:\ / d:\) through the real Xbox 360
 * kernel imports, the same way a retail title does. Guest structures use
 * 32-bit pointers (Xbox 360 ABI), so pointers are stored as uint32_t.
 */
#include <stdint.h>

typedef struct { uint16_t length, max_length; uint32_t buffer; } ANSI_STRING;
typedef struct { uint32_t root_directory, object_name, attributes; } OBJECT_ATTRIBUTES;
typedef struct { uint32_t status, information; } IO_STATUS_BLOCK;

uint32_t NtOpenFile(uint32_t* handle, uint32_t access, OBJECT_ATTRIBUTES* oa, IO_STATUS_BLOCK* iosb, uint32_t options);
uint32_t NtReadFile(uint32_t handle, uint32_t event, uint32_t apc, uint32_t apc_ctx, IO_STATUS_BLOCK* iosb,
                    void* buffer, uint32_t length, uint64_t* offset);
uint32_t NtQueryInformationFile(uint32_t handle, IO_STATUS_BLOCK* iosb, void* info, uint32_t length, uint32_t cls);
uint32_t NtSetInformationFile(uint32_t handle, IO_STATUS_BLOCK* iosb, void* info, uint32_t length, uint32_t cls);
uint32_t NtQueryFullAttributesFile(OBJECT_ATTRIBUTES* oa, void* info);
uint32_t NtClose(uint32_t handle);

#define P32(p) ((uint32_t)(uintptr_t)(p))

static uint32_t slen(const char* s) {
  uint32_t n = 0;
  while (s[n]) n++;
  return n;
}

static uint32_t open_path(const char* path, uint32_t* handle, IO_STATUS_BLOCK* iosb) {
  ANSI_STRING name;
  OBJECT_ATTRIBUTES oa;
  name.length = (uint16_t)slen(path);
  name.max_length = (uint16_t)(name.length + 1);
  name.buffer = P32(path);
  oa.root_directory = 0xFFFFFFFDu; /* ObDosDevices */
  oa.object_name = P32(&name);
  oa.attributes = 0x40; /* OBJ_CASE_INSENSITIVE */
  return NtOpenFile(handle, 0x80100000u /* GENERIC_READ|SYNCHRONIZE */, &oa, iosb, 0x20 /* FILE_SYNCHRONOUS_IO_NONALERT */);
}

static uint32_t crc32(const uint8_t* p, uint32_t n, uint32_t crc) {
  crc = ~crc;
  for (uint32_t i = 0; i < n; i++) {
    crc ^= p[i];
    for (int k = 0; k < 8; k++) crc = (crc & 1) ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
  }
  return ~crc;
}

/* out[0]  open status            out[1]  CRC32 of the whole file (7-byte reads)
 * out[2]  total bytes read       out[3]  status of the read at EOF
 * out[4]  FileStandardInformation EndOfFile (low)   out[5] position after reads
 * out[6]  4 bytes read at explicit offset 3 (big-endian word)
 * out[7]  NtQueryFullAttributesFile size of d:\MEDIA\Readme.TXT (case-insensitive)
 * out[8]  status opening a missing file        out[9] status of NtClose on a closed handle
 * out[10] status opening cache:\x (unmounted device) */
uint32_t fsr_fileio_test(uint32_t* out) {
  IO_STATUS_BLOCK iosb;
  uint32_t h = 0;
  for (int i = 0; i < 11; i++) out[i] = 0xFFFFFFFFu;
  out[0] = open_path("game:\\data\\hello.txt", &h, &iosb);
  if (out[0] != 0) return 1;
  uint8_t buf[7];
  uint32_t crc = 0, total = 0, st;
  for (;;) {
    st = NtReadFile(h, 0, 0, 0, &iosb, buf, sizeof buf, 0);
    if (st != 0) break;
    crc = crc32(buf, iosb.information, crc);
    total += iosb.information;
  }
  out[1] = crc;
  out[2] = total;
  out[3] = st;
  uint8_t std_info[24];
  NtQueryInformationFile(h, &iosb, std_info, sizeof std_info, 5);
  out[4] = (uint32_t)(*(uint64_t*)(std_info + 8));
  uint64_t pos;
  NtQueryInformationFile(h, &iosb, &pos, sizeof pos, 14);
  out[5] = (uint32_t)pos;
  uint64_t off = 3;
  uint8_t w[4] = {0, 0, 0, 0};
  NtReadFile(h, 0, 0, 0, &iosb, w, 4, &off);
  out[6] = (uint32_t)w[0] << 24 | (uint32_t)w[1] << 16 | (uint32_t)w[2] << 8 | w[3];
  NtClose(h);
  out[9] = NtClose(h);

  ANSI_STRING name;
  OBJECT_ATTRIBUTES oa;
  const char* p2 = "d:\\MEDIA\\Readme.TXT";
  name.length = (uint16_t)slen(p2);
  name.max_length = name.length;
  name.buffer = P32(p2);
  oa.root_directory = 0xFFFFFFFDu;
  oa.object_name = P32(&name);
  oa.attributes = 0x40;
  uint8_t net[56];
  if (NtQueryFullAttributesFile(&oa, net) == 0) out[7] = (uint32_t)(*(uint64_t*)(net + 40));

  out[8] = open_path("game:\\data\\does_not_exist.bin", &h, &iosb);
  out[10] = open_path("cache:\\x.bin", &h, &iosb);
  return 0;
}
