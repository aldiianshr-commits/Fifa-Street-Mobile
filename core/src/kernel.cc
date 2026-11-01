// High-level emulation (HLE) of the Xbox 360 kernel/XAM exports that the
// runtime currently implements. Everything else is reported as unimplemented
// (returns STATUS_NOT_IMPLEMENTED) so gaps are visible in the log.
// SPDX-License-Identifier: MIT
#include <strings.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

#include "fsr/bytes.h"
#include "fsr/host.h"

namespace fsr {
namespace {

#define ARG(n) ctx.r[3 + (n)]
#define RET(v) (ctx.r[3] = uint64_t(v))

std::string guest_cstr(uint8_t* base, uint32_t a, size_t max = 4096) {
  std::string s;
  if (!a) return "(null)";
  for (size_t i = 0; i < max && base[a + i]; i++) s.push_back(char(base[a + i]));
  return s;
}

void DbgPrint(PPCContext& ctx, uint8_t* base, Runtime& rt) {
  std::string fmt = guest_cstr(base, uint32_t(ARG(0))), out;
  int argi = 1;
  for (size_t i = 0; i < fmt.size(); i++) {
    if (fmt[i] != '%') { out.push_back(fmt[i]); continue; }
    size_t j = i + 1;
    while (j < fmt.size() && std::strchr("-+ #0123456789.lh", fmt[j])) j++;
    if (j >= fmt.size()) break;
    char c = fmt[j];
    uint64_t v = argi < 8 ? ARG(argi) : 0;
    char buf[64];
    switch (c) {
      case 'd': case 'i': std::snprintf(buf, sizeof buf, "%d", int32_t(v)); out += buf; argi++; break;
      case 'u': std::snprintf(buf, sizeof buf, "%u", uint32_t(v)); out += buf; argi++; break;
      case 'x': std::snprintf(buf, sizeof buf, "%x", uint32_t(v)); out += buf; argi++; break;
      case 'X': std::snprintf(buf, sizeof buf, "%X", uint32_t(v)); out += buf; argi++; break;
      case 'p': std::snprintf(buf, sizeof buf, "%08X", uint32_t(v)); out += buf; argi++; break;
      case 'c': out.push_back(char(v)); argi++; break;
      case 's': out += guest_cstr(base, uint32_t(v)); argi++; break;
      case '%': out.push_back('%'); break;
      default: out.push_back('%'); out.push_back(c); break;
    }
    i = j;
  }
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
  rt.logf("[guest] %s", out.c_str());
  RET(0);
}

void KeQueryPerformanceFrequency(PPCContext& ctx, uint8_t*, Runtime&) { RET(49875000); }

void KeQuerySystemTime(PPCContext& ctx, uint8_t* base, Runtime&) {
  using namespace std::chrono;
  // FILETIME: 100ns ticks since 1601-01-01.
  uint64_t t = uint64_t(duration_cast<microseconds>(system_clock::now().time_since_epoch()).count()) * 10 +
               116444736000000000ull;
  if (ARG(0)) st64(base, uint32_t(ARG(0)), t);
  RET(0);
}

void KeGetCurrentProcessType(PPCContext& ctx, uint8_t*, Runtime&) { RET(1); /* title process */ }

void NtAllocateVirtualMemory(PPCContext& ctx, uint8_t* base, Runtime& rt) {
  uint32_t pbase = uint32_t(ARG(0)), psize = uint32_t(ARG(1));
  uint32_t want = ld32(base, pbase);
  uint32_t size = (ld32(base, psize) + 0xFFFu) & ~0xFFFu;
  if (want) {  // whole guest space is backed; honour fixed requests as-is
    RET(0);
    return;
  }
  uint32_t a = rt.alloc(size, 0x10000);
  if (!a) { RET(0xC0000017u); return; }  // STATUS_NO_MEMORY
  st32(base, pbase, a);
  st32(base, psize, size);
  RET(0);
}

void NtFreeVirtualMemory(PPCContext& ctx, uint8_t*, Runtime&) { RET(0); }

void RtlInitAnsiString(PPCContext& ctx, uint8_t* base, Runtime&) {
  uint32_t dst = uint32_t(ARG(0)), src = uint32_t(ARG(1));
  uint16_t len = src ? uint16_t(std::strlen(reinterpret_cast<char*>(base + src))) : 0;
  st16(base, dst, len);
  st16(base, dst + 2, src ? uint16_t(len + 1) : 0);
  st32(base, dst + 4, src);
  RET(0);
}

void RtlCompareMemory(PPCContext& ctx, uint8_t* base, Runtime&) {
  uint32_t a = uint32_t(ARG(0)), b = uint32_t(ARG(1)), n = uint32_t(ARG(2)), i = 0;
  while (i < n && base[a + i] == base[b + i]) i++;
  RET(i);
}

// Critical sections: the runtime currently executes guest code on a single
// host thread, so these only need to keep the structure consistent.
void RtlInitializeCriticalSection(PPCContext& ctx, uint8_t* base, Runtime&) {
  std::memset(base + uint32_t(ARG(0)), 0, 28);
  RET(0);
}
void RtlEnterCriticalSection(PPCContext& ctx, uint8_t*, Runtime&) { RET(0); }
void RtlLeaveCriticalSection(PPCContext& ctx, uint8_t*, Runtime&) { RET(0); }

std::mutex g_tls_mu;
std::map<uint32_t, uint64_t> g_tls;
uint32_t g_tls_next = 0;
void KeTlsAlloc(PPCContext& ctx, uint8_t*, Runtime&) {
  std::lock_guard<std::mutex> l(g_tls_mu);
  uint32_t i = g_tls_next++;
  g_tls[i] = 0;
  RET(i);
}
void KeTlsFree(PPCContext& ctx, uint8_t*, Runtime&) {
  std::lock_guard<std::mutex> l(g_tls_mu);
  g_tls.erase(uint32_t(ARG(0)));
  RET(1);
}
void KeTlsGetValue(PPCContext& ctx, uint8_t*, Runtime&) {
  std::lock_guard<std::mutex> l(g_tls_mu);
  RET(g_tls[uint32_t(ARG(0))]);
}
void KeTlsSetValue(PPCContext& ctx, uint8_t*, Runtime&) {
  std::lock_guard<std::mutex> l(g_tls_mu);
  g_tls[uint32_t(ARG(0))] = uint32_t(ARG(1));
  RET(1);
}

constexpr uint32_t X_ERROR_SUCCESS = 0;
constexpr uint32_t X_ERROR_DEVICE_NOT_CONNECTED = 1167;

void XamInputGetState(PPCContext& ctx, uint8_t* base, Runtime& rt) {
  uint32_t user = uint32_t(ARG(0)), st = uint32_t(ARG(2));
  PadState p = rt.pad();
  if (user != 0 || !p.connected) { RET(X_ERROR_DEVICE_NOT_CONNECTED); return; }
  static std::atomic<uint32_t> packet{0};
  if (st) {
    st32(base, st, packet++);
    st16(base, st + 4, p.buttons);
    st8(base, st + 6, p.left_trigger);
    st8(base, st + 7, p.right_trigger);
    st16(base, st + 8, uint16_t(p.lx));
    st16(base, st + 10, uint16_t(p.ly));
    st16(base, st + 12, uint16_t(p.rx));
    st16(base, st + 14, uint16_t(p.ry));
  }
  RET(X_ERROR_SUCCESS);
}

void XamInputGetCapabilities(PPCContext& ctx, uint8_t* base, Runtime& rt) {
  uint32_t user = uint32_t(ARG(0)), caps = uint32_t(ARG(2));
  if (user != 0 || !rt.pad().connected) { RET(X_ERROR_DEVICE_NOT_CONNECTED); return; }
  if (caps) {
    std::memset(base + caps, 0, 20);
    st8(base, caps, 1);      // XINPUT_DEVTYPE_GAMEPAD
    st8(base, caps + 1, 1);  // XINPUT_DEVSUBTYPE_GAMEPAD
    st16(base, caps + 4, 0xF3FF);
    st8(base, caps + 6, 0xFF);
    st8(base, caps + 7, 0xFF);
    for (int i = 0; i < 4; i++) st16(base, caps + 8 + 2 * i, 0xFFC0);
  }
  RET(X_ERROR_SUCCESS);
}

void XGetLanguage(PPCContext& ctx, uint8_t*, Runtime&) { RET(1); /* English */ }


// ---------------------------------------------------------------- file system
// Read-only disc device (see vfs.h). Struct layouts follow Xenia's xbox.h:
// ANSI_STRING {u16 len, u16 max, u32 ptr}, OBJECT_ATTRIBUTES {u32 root,
// u32 name, u32 attr}, IO_STATUS_BLOCK {u32 status, u32 information}.
constexpr uint32_t kStatusSuccess = 0, kStatusEndOfFile = 0xC0000011u, kStatusInvalidHandle = 0xC0000008u,
                   kStatusNameNotFound = 0xC0000034u, kStatusPathNotFound = 0xC000003Au,
                   kStatusAccessDenied = 0xC0000022u, kStatusInvalidParameter = 0xC000000Du,
                   kStatusInfoLength = 0xC0000004u, kStatusInvalidClass = 0xC0000003u,
                   kStatusNoSuchFile = 0xC000000Fu, kStatusNoMedia = 0xC0000178u;

std::string guest_ansi(uint8_t* base, uint32_t ansi) {
  if (!ansi) return {};
  uint16_t len = ld16(base, ansi);
  uint32_t p = ld32(base, ansi + 4);
  if (!p) return {};
  return std::string(reinterpret_cast<char*>(base + p), len);
}

uint32_t stack_arg(PPCContext& ctx, uint8_t* base, int index /* >= 8 */) {
  return ld32(base, uint32_t(ctx.r[1]) + 0x54 + uint32_t(index - 8) * 8);
}

uint32_t open_common(PPCContext& ctx, uint8_t* base, Runtime& rt, uint32_t handle_out, uint32_t attrs, uint32_t iosb,
                     uint32_t disposition, uint32_t options, const char* fn) {
  if (!attrs) return kStatusInvalidParameter;
  std::string path = guest_ansi(base, ld32(base, attrs + 4));
  DiscVfs* vfs = rt.vfs();
  std::string dpath, dev;
  uint32_t status;
  uint32_t handle = 0xFFFFFFFFu;
  if (!vfs) status = kStatusNoMedia;
  else if (!DiscVfs::translate(path, &dpath, &dev)) status = kStatusPathNotFound;
  else if (disposition != 1 /* FILE_OPEN */ && disposition != 3 /* FILE_OPEN_IF */) status = kStatusAccessDenied;
  else {
    std::lock_guard<std::mutex> l(vfs->mutex());
    const IsoEntry* e = nullptr;
    bool root = false;
    if (!vfs->resolve(dpath, &e, &root)) {
      status = kStatusNameNotFound;
      vfs->failed_opens++;
    } else if ((options & 0x1 /* FILE_DIRECTORY_FILE */) && !(root || e->dir)) {
      status = kStatusNameNotFound;
    } else {
      handle = vfs->open(e);
      status = kStatusSuccess;
    }
  }
  if (status != kStatusSuccess)
    rt.logf("[fs] %s(\"%s\") -> %08X%s", fn, path.c_str(), status,
            dev.empty() || vfs ? "" : " (no disc mounted)");
  if (handle_out) st32(base, handle_out, handle);
  if (iosb) {
    st32(base, iosb, status);
    st32(base, iosb + 4, status == kStatusSuccess ? 1 /* FILE_OPENED */ : 0);
  }
  return status;
}

void NtCreateFile(PPCContext& ctx, uint8_t* base, Runtime& rt) {
  uint32_t options = stack_arg(ctx, base, 8);
  RET(open_common(ctx, base, rt, uint32_t(ARG(0)), uint32_t(ARG(2)), uint32_t(ARG(3)), uint32_t(ARG(7)), options,
                  "NtCreateFile"));
}

void NtOpenFile(PPCContext& ctx, uint8_t* base, Runtime& rt) {
  RET(open_common(ctx, base, rt, uint32_t(ARG(0)), uint32_t(ARG(2)), uint32_t(ARG(3)), 1, uint32_t(ARG(4)),
                  "NtOpenFile"));
}

void NtReadFile(PPCContext& ctx, uint8_t* base, Runtime& rt) {
  uint32_t h = uint32_t(ARG(0)), iosb = uint32_t(ARG(4)), buf = uint32_t(ARG(5)), len = uint32_t(ARG(6)),
           offp = uint32_t(ARG(7));
  DiscVfs* vfs = rt.vfs();
  uint32_t status = kStatusInvalidHandle, got = 0;
  if (vfs) {
    std::lock_guard<std::mutex> l(vfs->mutex());
    OpenFile* f = vfs->lookup(h);
    if (f && f->entry && !f->entry->dir) {
      uint64_t off = f->position;
      if (offp) {
        uint64_t v = ld64(base, offp);
        if (v != 0xFFFFFFFFFFFFFFFEull /* FILE_USE_FILE_POINTER_POSITION */) off = v;
      }
      if (off >= f->entry->size) {
        status = kStatusEndOfFile;
      } else {
        uint64_t n = std::min<uint64_t>(len, f->entry->size - off);
        if (vfs->disc().read_range(*f->entry, off, base + buf, size_t(n))) {
          got = uint32_t(n);
          f->position = off + n;
          vfs->bytes_read += n;
          status = kStatusSuccess;
        } else {
          status = 0xC000009Cu;  // STATUS_DEVICE_DATA_ERROR (read beyond truncated image)
        }
      }
    } else if (f) {
      status = kStatusInvalidParameter;  // directory handle
    }
  }
  if (iosb) {
    st32(base, iosb, status);
    st32(base, iosb + 4, got);
  }
  RET(status);
}

void write_network_info(uint8_t* base, uint32_t p, const IsoEntry* e, uint64_t filetime) {
  std::memset(base + p, 0, 56);
  for (int i = 0; i < 4; i++) st64(base, p + 8 * i, filetime);
  uint64_t size = e ? e->size : 0;
  st64(base, p + 32, (size + 2047) & ~uint64_t(2047));
  st64(base, p + 40, size);
  st32(base, p + 48, e ? (e->dir ? 0x10u : (e->attributes ? e->attributes : 0x80u)) | 0x01u : 0x11u);
}

void NtQueryInformationFile(PPCContext& ctx, uint8_t* base, Runtime& rt) {
  uint32_t h = uint32_t(ARG(0)), iosb = uint32_t(ARG(1)), info = uint32_t(ARG(2)), len = uint32_t(ARG(3)),
           cls = uint32_t(ARG(4));
  DiscVfs* vfs = rt.vfs();
  uint32_t status = kStatusInvalidHandle, written = 0;
  if (vfs) {
    std::lock_guard<std::mutex> l(vfs->mutex());
    if (OpenFile* f = vfs->lookup(h)) {
      uint64_t size = f->entry ? f->entry->size : 0;
      bool dir = !f->entry || f->entry->dir;
      switch (cls) {
        case 5:  // FileStandardInformation
          if (len < 24) { status = kStatusInfoLength; break; }
          std::memset(base + info, 0, 24);
          st64(base, info, (size + 2047) & ~uint64_t(2047));
          st64(base, info + 8, size);
          st32(base, info + 16, 1);
          st8(base, info + 21, dir ? 1 : 0);
          status = kStatusSuccess; written = 24;
          break;
        case 14:  // FilePositionInformation
          if (len < 8) { status = kStatusInfoLength; break; }
          st64(base, info, f->position);
          status = kStatusSuccess; written = 8;
          break;
        case 34:  // FileNetworkOpenInformation
          if (len < 56) { status = kStatusInfoLength; break; }
          write_network_info(base, info, f->entry, vfs->disc().layout().filetime);
          status = kStatusSuccess; written = 56;
          break;
        default:
          status = kStatusInvalidClass;
          rt.logf("[fs] NtQueryInformationFile class %u not implemented", cls);
      }
    }
  }
  if (iosb) { st32(base, iosb, status); st32(base, iosb + 4, written); }
  RET(status);
}

void NtSetInformationFile(PPCContext& ctx, uint8_t* base, Runtime& rt) {
  uint32_t h = uint32_t(ARG(0)), iosb = uint32_t(ARG(1)), info = uint32_t(ARG(2)), len = uint32_t(ARG(3)),
           cls = uint32_t(ARG(4));
  DiscVfs* vfs = rt.vfs();
  uint32_t status = kStatusInvalidHandle;
  if (vfs) {
    std::lock_guard<std::mutex> l(vfs->mutex());
    if (OpenFile* f = vfs->lookup(h)) {
      if (cls == 14 && len >= 8) { f->position = ld64(base, info); status = kStatusSuccess; }
      else if (cls == 14) status = kStatusInfoLength;
      else { status = kStatusAccessDenied; rt.logf("[fs] NtSetInformationFile class %u rejected (read-only disc)", cls); }
    }
  }
  if (iosb) { st32(base, iosb, status); st32(base, iosb + 4, 0); }
  RET(status);
}

void NtQueryFullAttributesFile(PPCContext& ctx, uint8_t* base, Runtime& rt) {
  uint32_t attrs = uint32_t(ARG(0)), info = uint32_t(ARG(1));
  DiscVfs* vfs = rt.vfs();
  std::string dpath;
  if (!vfs || !attrs || !DiscVfs::translate(guest_ansi(base, ld32(base, attrs + 4)), &dpath)) {
    RET(kStatusNoSuchFile);
    return;
  }
  std::lock_guard<std::mutex> l(vfs->mutex());
  const IsoEntry* e = nullptr;
  bool root = false;
  if (!vfs->resolve(dpath, &e, &root)) { RET(kStatusNoSuchFile); return; }
  if (info) write_network_info(base, info, e, vfs->disc().layout().filetime);
  RET(kStatusSuccess);
}

void NtClose(PPCContext& ctx, uint8_t*, Runtime& rt) {
  DiscVfs* vfs = rt.vfs();
  bool ok = false;
  if (vfs) {
    std::lock_guard<std::mutex> l(vfs->mutex());
    ok = vfs->close(uint32_t(ARG(0)));
  }
  RET(ok ? kStatusSuccess : kStatusInvalidHandle);
}

struct Entry {
  const char* lib;
  const char* name;
  HleFn fn;
};
const Entry kHle[] = {
    {"xboxkrnl.exe", "DbgPrint", DbgPrint},
    {"xboxkrnl.exe", "KeQueryPerformanceFrequency", KeQueryPerformanceFrequency},
    {"xboxkrnl.exe", "KeQuerySystemTime", KeQuerySystemTime},
    {"xboxkrnl.exe", "KeGetCurrentProcessType", KeGetCurrentProcessType},
    {"xboxkrnl.exe", "NtAllocateVirtualMemory", NtAllocateVirtualMemory},
    {"xboxkrnl.exe", "NtFreeVirtualMemory", NtFreeVirtualMemory},
    {"xboxkrnl.exe", "RtlInitAnsiString", RtlInitAnsiString},
    {"xboxkrnl.exe", "RtlCompareMemory", RtlCompareMemory},
    {"xboxkrnl.exe", "RtlInitializeCriticalSection", RtlInitializeCriticalSection},
    {"xboxkrnl.exe", "RtlEnterCriticalSection", RtlEnterCriticalSection},
    {"xboxkrnl.exe", "RtlLeaveCriticalSection", RtlLeaveCriticalSection},
    {"xboxkrnl.exe", "KeTlsAlloc", KeTlsAlloc},
    {"xboxkrnl.exe", "KeTlsFree", KeTlsFree},
    {"xboxkrnl.exe", "KeTlsGetValue", KeTlsGetValue},
    {"xboxkrnl.exe", "KeTlsSetValue", KeTlsSetValue},
    {"xam.xex", "XamInputGetState", XamInputGetState},
    {"xam.xex", "XamInputGetCapabilities", XamInputGetCapabilities},
    {"xam.xex", "XGetLanguage", XGetLanguage},
    {"xboxkrnl.exe", "NtCreateFile", NtCreateFile},
    {"xboxkrnl.exe", "NtOpenFile", NtOpenFile},
    {"xboxkrnl.exe", "NtReadFile", NtReadFile},
    {"xboxkrnl.exe", "NtQueryInformationFile", NtQueryInformationFile},
    {"xboxkrnl.exe", "NtSetInformationFile", NtSetInformationFile},
    {"xboxkrnl.exe", "NtQueryFullAttributesFile", NtQueryFullAttributesFile},
    {"xboxkrnl.exe", "NtClose", NtClose},
};

bool ieq(const std::string& a, const char* b) {
  return a.size() == std::strlen(b) && strncasecmp(a.c_str(), b, a.size()) == 0;
}

}  // namespace

HleFn find_hle(const std::string& lib, uint16_t ordinal, std::string* name_out) {
  const char* name = kernel_export_name(lib, ordinal);
  if (name_out) *name_out = name ? name : "";
  if (!name) return nullptr;
  for (const auto& e : kHle)
    if (ieq(lib, e.lib) && std::strcmp(name, e.name) == 0) return e.fn;
  return nullptr;
}

std::vector<std::string> implemented_hle_names() {
  std::vector<std::string> v;
  for (const auto& e : kHle) v.push_back(std::string(e.lib) + "!" + e.name);
  return v;
}

}  // namespace fsr
