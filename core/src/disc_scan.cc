// SPDX-License-Identifier: MIT
#include "fsr/disc_scan.h"

#include <strings.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <set>
#include <sstream>

#include "fsr/bytes.h"
#include "fsr/host.h"
#include "fsr/vfs.h"

namespace fsr {

const char* to_string(ReqState s) {
  switch (s) {
    case ReqState::Ok: return "ok";
    case ReqState::Partial: return "partial";
    case ReqState::Missing: return "missing";
    case ReqState::Info: return "info";
  }
  return "?";
}

const char* to_string(GameStatus s) {
  switch (s) {
    case GameStatus::Ready: return "ready";
    case GameStatus::Partial: return "partial";
    case GameStatus::Unsupported: return "unsupported";
  }
  return "?";
}

namespace {

std::string lower(std::string s) {
  for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::string ext_of(const std::string& p) {
  auto slash = p.find_last_of('/');
  auto dot = p.find_last_of('.');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
  return lower(p.substr(dot + 1));
}
bool head_is(const uint8_t* h, size_t n, const char* magic) {
  size_t m = std::strlen(magic);
  return n >= m && std::memcmp(h, magic, m) == 0;
}
std::string fmt_bytes(uint64_t b) {
  char buf[32];
  if (b >= (1ull << 30)) std::snprintf(buf, sizeof buf, "%.2f GB", b / 1073741824.0);
  else if (b >= (1ull << 20)) std::snprintf(buf, sizeof buf, "%.1f MB", b / 1048576.0);
  else if (b >= 1024) std::snprintf(buf, sizeof buf, "%.1f KB", b / 1024.0);
  else std::snprintf(buf, sizeof buf, "%llu B", (unsigned long long)b);
  return buf;
}

}  // namespace

std::string classify_file(const std::string& path, const uint8_t* h, size_t n) {
  // Magic first (format-level evidence), then extension.
  if (head_is(h, n, "XEX2")) return "executable";
  if (head_is(h, n, "XEX1") || head_is(h, n, "XEX%") || head_is(h, n, "XEX-")) return "executable";
  if (head_is(h, n, "BIKi") || head_is(h, n, "BIKf") || head_is(h, n, "KB2")) return "video";
  if (n >= 4 && h[0] == 0x30 && h[1] == 0x26 && h[2] == 0xB2 && h[3] == 0x75) return "video";  // ASF/WMV
  if (head_is(h, n, "RIFF") || head_is(h, n, "WBND") || head_is(h, n, "SDBK") || head_is(h, n, "XGSF") ||
      head_is(h, n, "XWB") || head_is(h, n, "SBNK"))
    return "audio";
  if (head_is(h, n, "XPR2") || head_is(h, n, "XPR0") || head_is(h, n, "DDS ") || head_is(h, n, "\x89PNG") ||
      head_is(h, n, "BM"))
    return "texture";
  if (head_is(h, n, "XUIZ") || head_is(h, n, "XUIB") || head_is(h, n, "XUIS")) return "ui";
  if (head_is(h, n, "BIGF") || head_is(h, n, "BIG4") || head_is(h, n, "PK\x03\x04") || head_is(h, n, "MSCF"))
    return "archive";
  if (head_is(h, n, "XDBF")) return "metadata";
  if (head_is(h, n, "<?xml") || head_is(h, n, "\xEF\xBB\xBF<")) return "config";
  std::string e = ext_of(path);
  static const std::map<std::string, std::string> kExt = {
      {"xex", "executable"}, {"xexp", "title-patch"}, {"dll", "executable"},
      {"wmv", "video"}, {"bik", "video"}, {"bk2", "video"}, {"vp6", "video"},
      {"xma", "audio"}, {"xwb", "audio"}, {"xsb", "audio"}, {"xgs", "audio"}, {"wav", "audio"}, {"snd", "audio"},
      {"sbr", "audio"}, {"sbs", "audio"}, {"mpc", "audio"}, {"ast", "audio"},
      {"xpr", "texture"}, {"dds", "texture"}, {"png", "texture"}, {"tga", "texture"}, {"jpg", "texture"},
      {"xur", "ui"}, {"xui", "ui"}, {"xzp", "ui"}, {"swf", "ui"}, {"gfx", "ui"},
      {"big", "archive"}, {"viv", "archive"}, {"zip", "archive"}, {"cab", "archive"}, {"arc", "archive"}, {"pak", "archive"},
      {"xml", "config"}, {"ini", "config"}, {"cfg", "config"}, {"txt", "config"}, {"json", "config"}, {"lua", "script"},
      {"ttf", "font"}, {"fnt", "font"}, {"xtt", "font"},
      {"fx", "shader"}, {"fxo", "shader"}, {"xvu", "shader"}, {"xpu", "shader"}, {"fxc", "shader"},
      {"xdbf", "metadata"}, {"gpd", "metadata"}, {"spa", "metadata"},
  };
  auto it = kExt.find(e);
  return it == kExt.end() ? "other" : it->second;
}

bool load_main_xex(const XisoReader& disc, const uint8_t* key, XexModule& out, std::string* path, std::string& err) {
  const IsoEntry* e = disc.find("default.xex");
  if (!e) {
    for (auto& f : disc.entries())
      if (!f.dir && f.path.find('/') == std::string::npos && ext_of(f.path) == "xex") { e = &f; break; }
  }
  if (!e) { err = "no executable (default.xex) on the disc"; return false; }
  if (path) *path = e->path;
  std::vector<uint8_t> data;
  if (!disc.read(*e, data, 256ull << 20)) { err = "cannot read " + e->path + " from the disc image"; return false; }
  if (!out.parse(std::move(data), err)) return false;
  LoadStatus st = out.load_image(key, err);
  if (st != LoadStatus::Ok) { err = std::string(to_string(st)) + ": " + err; return false; }
  return true;
}

namespace {

// Printable ASCII and UTF-16BE string literals that name a disc path.
void collect_refs(const std::vector<uint8_t>& img, std::vector<std::string>& out) {
  auto consider = [&](std::string s) {
    std::string l = lower(s);
    if (l.rfind("game:\\", 0) == 0 || l.rfind("d:\\", 0) == 0 || l.rfind("\\device\\cdrom0", 0) == 0 ||
        l.rfind("game:/", 0) == 0 || l.rfind("cache:\\", 0) == 0 || l.rfind("hdd:\\", 0) == 0 ||
        l.rfind("update:\\", 0) == 0 || l.rfind("dlc:\\", 0) == 0)
      out.push_back(s);
  };
  std::string cur;
  for (size_t i = 0; i < img.size(); i++) {
    uint8_t c = img[i];
    if (c >= 0x20 && c < 0x7F) { cur.push_back(char(c)); continue; }
    if (cur.size() >= 4) consider(cur);
    cur.clear();
  }
  if (cur.size() >= 4) consider(cur);
  cur.clear();
  for (int phase = 0; phase < 2; phase++) {
    for (size_t i = phase; i + 1 < img.size(); i += 2) {
      uint8_t hi = img[i], lo = img[i + 1];
      if (hi == 0 && lo >= 0x20 && lo < 0x7F) { cur.push_back(char(lo)); continue; }
      if (cur.size() >= 4) consider(cur);
      cur.clear();
    }
    if (cur.size() >= 4) consider(cur);
    cur.clear();
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
}

void add_req(DiscReport& r, const std::string& id, const std::string& label, ReqState s, const std::string& d) {
  r.requirements.push_back({id, label, s, d});
}

}  // namespace

DiscReport scan_disc(std::shared_ptr<const ReadSource> src, const ScanOptions& opt, const ScanProgress& progress) {
  auto t0 = std::chrono::steady_clock::now();
  DiscReport r;
  r.key_supplied = opt.key != nullptr;
  auto step = [&](const std::string& stage, uint64_t done, uint64_t total, const std::string& msg) {
    if (progress && !progress(stage, done, total, msg)) r.cancelled = true;
    return !r.cancelled;
  };
  auto finish = [&]() {
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return r;
  };
  if (!src) { r.error = "no input"; return finish(); }
  r.image_size = src->size();
  step("validate", 0, 1, "Memeriksa format image");
  std::string detail;
  r.kind = sniff_container(*src, &detail);
  if (r.kind == ContainerKind::Stfs) {
    r.error = "file is an STFS package (" + detail + ", e.g. Games on Demand), not a disc image; STFS is not supported yet";
  } else if (r.kind == ContainerKind::Xex) {
    r.error = "file is a bare XEX executable, not a disc image";
  } else if (r.kind == ContainerKind::Unknown) {
    r.error = "not an Xbox 360 disc image: no XDVDFS volume descriptor found at any known partition offset";
  }
  auto disc = std::make_shared<XisoReader>();
  if (r.kind == ContainerKind::Xdvdfs && !disc->open(src, r.error)) r.kind = ContainerKind::Unknown;
  if (r.kind != ContainerKind::Xdvdfs) {
    add_req(r, "filesystem", "Format image disc", ReqState::Missing, r.error);
    r.status = GameStatus::Unsupported;
    r.status_reason = r.error;
    return finish();
  }
  r.ok = true;
  r.layout = disc->layout();
  r.fs_warnings = disc->warnings();
  if (!step("filesystem", 1, 1, "Filesystem XDVDFS terbaca: " + r.layout.layout)) return finish();

  // ---- inventory + classification (reads 16 bytes per file, in place)
  const auto& ents = disc->entries();
  uint64_t idx = 0;
  bool has_xbe = false;
  std::vector<const IsoEntry*> xexes;
  for (const auto& e : ents) {
    idx++;
    if (e.dir) {
      r.dirs++;
      if (e.path.find('/') == std::string::npos && e.path[0] == '$') r.special_dirs.push_back(e.path);
      continue;
    }
    r.files++;
    r.bytes += e.size;
    bool trunc = e.offset + e.size > src->size();
    if (trunc) {
      r.truncated++;
      if (r.truncated_examples.size() < 10) r.truncated_examples.push_back(e.path);
    }
    uint8_t head[16] = {};
    size_t n = trunc ? 0 : size_t(std::min<uint64_t>(16, e.size));
    if (n && !src->read(e.offset, head, n)) n = 0;
    std::string cat = classify_file(e.path, head, n);
    auto& cs = r.categories[cat];
    cs.files++;
    cs.bytes += e.size;
    if (cat == "executable" && (ext_of(e.path) == "xex" || head_is(head, n, "XEX2"))) xexes.push_back(&e);
    if (strcasecmp(e.path.c_str(), "default.xbe") == 0) has_xbe = true;
    if ((idx & 63) == 0 && !step("inventory", idx, ents.size(), e.path)) return finish();
  }
  step("inventory", ents.size(), ents.size(), std::to_string(r.files) + " berkas");

  // ---- main executable
  const IsoEntry* main = disc->find("default.xex");
  if (!main)
    for (auto* x : xexes)
      if (x->path.find('/') == std::string::npos) { main = x; break; }
  if (!main && !xexes.empty()) main = xexes.front();
  r.platform = main ? "xbox360" : (has_xbe ? "xbox" : "unknown");
  add_req(r, "filesystem", "Filesystem XDVDFS", r.fs_warnings.empty() ? ReqState::Ok : ReqState::Partial,
          r.layout.layout + ", " + std::to_string(r.files) + " berkas / " + std::to_string(r.dirs) + " folder, " +
              fmt_bytes(r.bytes) + (r.fs_warnings.empty() ? "" : "; peringatan: " + r.fs_warnings.front()));
  add_req(r, "completeness", "Kelengkapan data", r.truncated ? ReqState::Missing : ReqState::Ok,
          r.truncated ? std::to_string(r.truncated) + " berkas melewati akhir file ISO (" + fmt_bytes(r.image_size) +
                            "): dump terpotong/tidak lengkap, mis. " + r.truncated_examples.front()
                      : "semua " + std::to_string(r.files) + " berkas berada di dalam image (" + fmt_bytes(r.image_size) + ")");
  if (!main) {
    std::string why = has_xbe ? "disc berisi default.xbe: ini game Xbox original, bukan Xbox 360"
                              : "tidak ada executable XEX (default.xex) di disc";
    add_req(r, "executable", "Executable utama", ReqState::Missing, why);
    r.status = GameStatus::Unsupported;
    r.status_reason = why;
    return finish();
  }
  r.main_xex = main->path;
  if (!step("xex", 0, 1, "Membaca " + main->path + " langsung dari ISO")) return finish();
  std::vector<uint8_t> xdata;
  if (main->offset + main->size > src->size() || !disc->read(*main, xdata, 256ull << 20)) {
    std::string why = main->path + " tidak dapat dibaca dari image (terpotong atau rusak)";
    add_req(r, "executable", "Executable utama", ReqState::Missing, why);
    r.status = GameStatus::Unsupported;
    r.status_reason = why;
    return finish();
  }
  step("decrypt", 0, 1, "Header XEX, dekripsi & dekompresi");
  XexModule xm;
  AnalyzeProgress ap = [&](uint64_t d, uint64_t t) { return step("analyze", d, t, "Analisis & cek terjemahan fungsi PowerPC"); };
  r.xex = analyze_xex_buffer(std::move(xdata), main->path, opt.key, opt.deep, &xm, ap);
  r.have_report = true;
  if (r.cancelled) return finish();
  const auto& a = r.xex;
  const auto& info = a.info;
  if (!a.parsed) {
    add_req(r, "executable", "Executable utama", ReqState::Missing, main->path + ": " + a.error);
    r.status = GameStatus::Unsupported;
    r.status_reason = a.error;
    return finish();
  }
  char tid[64];
  std::snprintf(tid, sizeof tid, "Title ID %08X, media %08X, versi %08X, disc %u/%u", info.title_id, info.media_id,
                info.version, info.disc_number, info.disc_count);
  add_req(r, "executable", "Executable utama", ReqState::Ok, main->path + " (" + fmt_bytes(main->size) + "), " + tid);
  add_req(r, "region", "Region", ReqState::Info, region_text(info.region));

  // ---- encryption / compression
  const char* cmp[] = {"tanpa kompresi", "kompresi basic", "kompresi LZX", "delta patch"};
  std::string cdesc = unsigned(info.compression) < 4 ? cmp[unsigned(info.compression)] : "kompresi tidak dikenal";
  if (info.encryption == XexEncryption::None) {
    add_req(r, "decryption", "Enkripsi", ReqState::Ok, "image tidak terenkripsi");
  } else {
    switch (a.load_status) {
      case LoadStatus::Ok:
        add_req(r, "decryption", "Enkripsi", ReqState::Ok, "AES; didekripsi dengan kunci XEX milik pengguna (hash blok & header MZ valid)");
        break;
      case LoadStatus::NeedsKey:
        add_req(r, "decryption", "Enkripsi", ReqState::Missing,
                "image XEX terenkripsi AES-128. Aplikasi tidak menyertakan kunci; impor kunci XEX dari konsol Anda sendiri. "
                "Tanpa kunci, kode game tidak dapat dibaca sehingga analisis CPU, impor, dan referensi file tidak bisa dilakukan.");
        break;
      case LoadStatus::KeyMismatch:
        add_req(r, "decryption", "Enkripsi", ReqState::Missing,
                "kunci yang diimpor tidak cocok dengan XEX ini (" + a.load_error + ")");
        break;
      default:
        add_req(r, "decryption", "Enkripsi", ReqState::Missing, a.load_error);
    }
  }
  if (info.encryption == XexEncryption::None || a.image_loaded || a.load_status == LoadStatus::DecompressFailed ||
      a.load_status == LoadStatus::UnsupportedFormat) {
    ReqState cs = a.image_loaded ? ReqState::Ok : ReqState::Missing;
    add_req(r, "decompression", "Dekompresi", cs,
            cdesc + (a.image_loaded ? " → image PE " + fmt_bytes(info.image_size) : ": " + a.load_error));
  } else {
    add_req(r, "decompression", "Dekompresi", ReqState::Info, cdesc + " (menunggu dekripsi)");
  }
  if (info.compression == XexCompression::Delta) {
    r.status = GameStatus::Unsupported;
    r.status_reason = "executable utama adalah delta patch (title update), bukan executable lengkap";
    return finish();
  }

  // ---- other modules on the disc
  if (!step("modules", 0, xexes.size(), "Memeriksa modul XEX lain")) return finish();
  std::set<std::string> module_names;
  for (auto* x : xexes) {
    if (x == main) continue;
    ModuleSummary m;
    m.path = x->path;
    m.size = x->size;
    std::vector<uint8_t> d;
    XexModule mx;
    std::string err;
    if (x->offset + x->size <= src->size() && disc->read(*x, d, 256ull << 20) && mx.parse(std::move(d), err)) {
      m.parsed = true;
      m.module_flags = mx.info().module_flags;
      m.is_dll = m.module_flags & 0x8;
      m.is_patch = m.module_flags & 0x70;
      char b[16];
      std::snprintf(b, sizeof b, "%08X", mx.info().title_id);
      m.title_id = b;
      m.load_status = to_string(mx.load_image(opt.key, err));
    } else {
      m.error = err.empty() ? "unreadable" : err;
    }
    auto slash = x->path.find_last_of('/');
    module_names.insert(lower(slash == std::string::npos ? x->path : x->path.substr(slash + 1)));
    r.modules.push_back(m);
  }
  {
    size_t dlls = 0;
    std::string names;
    for (auto& m : r.modules) {
      if (m.is_dll) dlls++;
      if (names.size() < 300) names += (names.empty() ? "" : ", ") + m.path;
    }
    if (r.modules.empty())
      add_req(r, "modules", "Modul executable lain", ReqState::Ok, "tidak ada modul XEX tambahan di disc");
    else
      add_req(r, "modules", "Modul executable lain", dlls ? ReqState::Partial : ReqState::Info,
              std::to_string(r.modules.size()) + " XEX tambahan (" + names + ")" +
                  (dlls ? "; " + std::to_string(dlls) +
                              " modul DLL belum dapat dimuat (loader multi-modul runtime belum dibuat)"
                        : "; bukan DLL, tidak dimuat otomatis oleh executable utama"));
  }

  // ---- code-dependent checks (require the decrypted image)
  if (!a.image_loaded) {
    std::string why = "membutuhkan image XEX yang sudah didekripsi";
    add_req(r, "cpu", "Terjemahan CPU PowerPC → ARM64", ReqState::Missing, why);
    add_req(r, "imports", "Impor kernel/XAM (HLE)", ReqState::Missing, why);
    add_req(r, "data_files", "File data yang dirujuk", ReqState::Info,
            "seluruh ISO di-mount sebagai game:\\ dan d:\\; daftar rujukan belum dapat diperiksa (" + why + ")");
  } else {
    const auto& s = a.stats;
    if (!a.deep) {
      add_req(r, "cpu", "Terjemahan CPU PowerPC → ARM64", ReqState::Info, "analisis mendalam dilewati");
    } else {
      std::string untr;
      std::vector<std::pair<std::string, uint64_t>> h(s.unimplemented_histogram.begin(), s.unimplemented_histogram.end());
      std::sort(h.begin(), h.end(), [](auto& x, auto& y) { return x.second > y.second; });
      for (size_t i = 0; i < h.size() && i < 6; i++) untr += (i ? ", " : "") + h[i].first + " ×" + std::to_string(h[i].second);
      char b[256];
      std::snprintf(b, sizeof b, "%llu fungsi, %llu instruksi, %.2f%% dapat diterjemahkan (VMX %llu/%llu, VMX128 %llu)",
                    (unsigned long long)s.functions, (unsigned long long)s.instructions, s.coverage() * 100,
                    (unsigned long long)s.vmx_translated, (unsigned long long)s.vmx, (unsigned long long)s.vmx128);
      add_req(r, "cpu", "Terjemahan CPU PowerPC → ARM64", s.translated == s.instructions ? ReqState::Ok : ReqState::Partial,
              std::string(b) + (untr.empty() ? "" : "; belum: " + untr));
    }
    // imports
    std::string missing;
    size_t nmissing = 0;
    std::set<std::string> libs;
    for (auto& im : a.imports) {
      libs.insert(lower(im.lib));
      if (!im.implemented) {
        nmissing++;
        if (nmissing <= 15) missing += (missing.empty() ? "" : ", ") + im.name;
      }
    }
    std::string libnote;
    for (auto& l : libs)
      if (l != "xboxkrnl.exe" && l != "xam.xex")
        libnote += "; library '" + l + "' " + (module_names.count(l) ? "ada di disc tetapi loader modul belum dibuat" : "tidak dikenal/ tidak tersedia");
    add_req(r, "imports", "Impor kernel/XAM (HLE)",
            nmissing == 0 && libnote.empty() ? ReqState::Ok : (a.imports_implemented ? ReqState::Partial : ReqState::Missing),
            std::to_string(a.imports_implemented) + "/" + std::to_string(a.imports.size()) + " diimplementasikan" +
                (nmissing ? "; belum: " + missing + (nmissing > 15 ? ", …" : "") : "") + libnote);
    auto imported = [&](const char* n) {
      for (auto& im : a.imports)
        if (im.name == n) return !im.implemented;
      return false;
    };
    if (imported("ExCreateThread") || imported("KeWaitForSingleObject") || imported("KeWaitForMultipleObjects"))
      add_req(r, "threads", "Thread & sinkronisasi guest", ReqState::Missing,
              "game membuat thread (ExCreateThread/KeWait*); scheduler thread guest belum dibuat");
    bool audio = false;
    for (auto& im : a.imports)
      if (im.name.find("XAudio") != std::string::npos || im.name.find("XMA") != std::string::npos) audio = true;
    if (audio)
      add_req(r, "audio", "Audio (XAudio2/XMA)", ReqState::Missing, "game memakai XAudio/XMA; decoder XMA & mixer belum dibuat");

    // referenced files
    step("references", 0, 1, "Mencocokkan path file yang dirujuk executable dengan isi ISO");
    std::vector<std::string> refs;
    collect_refs(xm.image(), refs);
    for (auto& ref : refs) {
      if (ref.find('%') != std::string::npos || ref.find('*') != std::string::npos) { r.ref_patterns++; continue; }
      std::string dp;
      if (!DiscVfs::translate(ref, &dp)) { r.ref_other_device++; continue; }
      if (dp.empty()) continue;
      r.ref_total++;
      if (disc->find(dp)) {
        r.ref_found++;
        if (r.ref_found_examples.size() < 20) r.ref_found_examples.push_back(ref);
      } else if (r.ref_missing.size() < 100) {
        r.ref_missing.push_back(ref);
      }
    }
    std::string d = "seluruh ISO di-mount sebagai game:\\ dan d:\\ (dibaca langsung, tanpa ekstraksi). Literal path di kode: " +
                    std::to_string(r.ref_found) + "/" + std::to_string(r.ref_total) + " ditemukan di ISO, " +
                    std::to_string(r.ref_patterns) + " pola dinamis (%s/*), " + std::to_string(r.ref_other_device) +
                    " ke perangkat lain (cache:/hdd:/update:)";
    if (r.ref_total > r.ref_found)
      d += ". Tidak ditemukan (bisa opsional/ dibuat saat runtime, tidak dapat dipastikan statis): " + r.ref_missing.front();
    // A literal path that is absent may be optional (probed with
    // NtQueryFullAttributesFile) or created at runtime, so a few misses are
    // reported but do not block; most references missing indicates a
    // stripped/incomplete image.
    uint64_t miss = r.ref_total - r.ref_found;
    ReqState ds = miss == 0 ? ReqState::Ok : (miss * 2 > r.ref_total ? ReqState::Partial : ReqState::Info);
    if (ds == ReqState::Partial) d += ". Sebagian besar rujukan tidak ada: image kemungkinan dipangkas/tidak lengkap";
    add_req(r, "data_files", "File data yang dirujuk", ds, d);
  }

  // ---- native module + launch path
  size_t nmods = 0;
  const RecompModule* const* mods = registered_modules(&nmods);
  const RecompModule* mod = nullptr;
  for (size_t i = 0; i < nmods; i++)
    if (mods[i]->name == a.module) mod = mods[i];
  if (mod) {
    add_req(r, "native", "Modul native ARM64 (hasil recompile)", ReqState::Ok,
            std::string("modul '") + mod->name + "' terpasang di APK, SHA-1 image cocok, " +
                std::to_string(mod->func_count) + " fungsi");
    bool frame = false;
    for (uint32_t i = 0; i < mod->export_count; i++)
      if (std::strcmp(mod->exports[i].name, "fsr_frame") == 0) frame = true;
    add_req(r, "launch", "Jalur eksekusi & grafis", frame ? ReqState::Ok : ReqState::Missing,
            frame ? "entry fsr_frame: framebuffer guest → presenter Vulkan"
                  : "entry retail (mainCRTStartup) membutuhkan thread guest, scheduler, dan penerjemah GPU Xenos → Vulkan yang belum dibuat");
  } else {
    add_req(r, "native", "Modul native ARM64 (hasil recompile)", ReqState::Missing,
            a.image_loaded
                ? "kode PowerPC sudah dianalisis di perangkat, tetapi APK ini belum berisi modul ARM64 hasil kompilasi untuk image ini "
                  "(SHA-1 " + a.image_sha1.substr(0, 12) + "…). Tahap C++ → ARM64 memerlukan toolchain clang/NDK yang tidak ada di perangkat; "
                  "jalankan fsr-recomp pada ISO ini di PC lalu build APK pribadi."
                : "membutuhkan image terdekripsi sebelum dapat di-recompile");
    add_req(r, "gpu", "Grafis Xenos → Vulkan", ReqState::Missing,
            "penerjemah perintah GPU Xenos (PM4, shader microcode, EDRAM, tekstur tiled) ke Vulkan belum dibuat");
  }

  // ---- status
  bool all_ok = true;
  for (auto& q : r.requirements)
    if (q.state == ReqState::Missing || q.state == ReqState::Partial) {
      all_ok = false;
      if (r.status_reason.empty()) r.status_reason = q.label + ": " + q.detail;
    }
  r.status = all_ok ? GameStatus::Ready : GameStatus::Partial;
  if (all_ok) r.status_reason = "semua persyaratan runtime terpenuhi";
  step("done", 1, 1, to_string(r.status));
  return finish();
}

std::string to_json(const DiscReport& r) {
  std::ostringstream o;
  o << "{\"ok\":" << (r.ok ? "true" : "false") << ",\"error\":" << jstr(r.error) << ",\"status\":" << jstr(to_string(r.status))
    << ",\"status_reason\":" << jstr(r.status_reason) << ",\"image_size\":" << r.image_size
    << ",\"platform\":" << jstr(r.platform) << ",\"layout\":" << jstr(r.layout.layout)
    << ",\"partition_offset\":" << r.layout.partition_offset << ",\"files\":" << r.files << ",\"dirs\":" << r.dirs
    << ",\"bytes\":" << r.bytes << ",\"truncated\":" << r.truncated << ",\"main_xex\":" << jstr(r.main_xex)
    << ",\"key_supplied\":" << (r.key_supplied ? "true" : "false") << ",\"seconds\":" << r.seconds
    << ",\"cancelled\":" << (r.cancelled ? "true" : "false");
  o << ",\"categories\":{";
  bool first = true;
  for (auto& [k, v] : r.categories) {
    o << (first ? "" : ",") << jstr(k) << ":{\"files\":" << v.files << ",\"bytes\":" << v.bytes << "}";
    first = false;
  }
  o << "},\"special_dirs\":[";
  for (size_t i = 0; i < r.special_dirs.size(); i++) o << (i ? "," : "") << jstr(r.special_dirs[i]);
  o << "],\"fs_warnings\":[";
  for (size_t i = 0; i < r.fs_warnings.size(); i++) o << (i ? "," : "") << jstr(r.fs_warnings[i]);
  o << "],\"truncated_examples\":[";
  for (size_t i = 0; i < r.truncated_examples.size(); i++) o << (i ? "," : "") << jstr(r.truncated_examples[i]);
  o << "],\"modules\":[";
  for (size_t i = 0; i < r.modules.size(); i++) {
    auto& m = r.modules[i];
    o << (i ? "," : "") << "{\"path\":" << jstr(m.path) << ",\"size\":" << m.size << ",\"parsed\":" << (m.parsed ? "true" : "false")
      << ",\"title_id\":" << jstr(m.title_id) << ",\"dll\":" << (m.is_dll ? "true" : "false") << ",\"patch\":"
      << (m.is_patch ? "true" : "false") << ",\"load_status\":" << jstr(m.load_status) << ",\"error\":" << jstr(m.error) << "}";
  }
  o << "],\"refs\":{\"total\":" << r.ref_total << ",\"found\":" << r.ref_found << ",\"patterns\":" << r.ref_patterns
    << ",\"other_device\":" << r.ref_other_device << ",\"missing\":[";
  for (size_t i = 0; i < r.ref_missing.size(); i++) o << (i ? "," : "") << jstr(r.ref_missing[i]);
  o << "],\"found_examples\":[";
  for (size_t i = 0; i < r.ref_found_examples.size(); i++) o << (i ? "," : "") << jstr(r.ref_found_examples[i]);
  o << "]},\"requirements\":[";
  for (size_t i = 0; i < r.requirements.size(); i++) {
    auto& q = r.requirements[i];
    o << (i ? "," : "") << "{\"id\":" << jstr(q.id) << ",\"label\":" << jstr(q.label) << ",\"state\":" << jstr(to_string(q.state))
      << ",\"detail\":" << jstr(q.detail) << "}";
  }
  o << "]";
  if (r.have_report) o << ",\"xex\":" << to_json(r.xex);
  o << "}";
  return o.str();
}

std::string to_text(const DiscReport& r) {
  std::ostringstream o;
  o << "Disc image: " << fmt_bytes(r.image_size) << "  layout: " << (r.layout.layout.empty() ? "-" : r.layout.layout)
    << "  platform: " << (r.platform.empty() ? "-" : r.platform) << "\n";
  if (!r.ok) o << "  error: " << r.error << "\n";
  if (r.ok) {
    o << "  files: " << r.files << "  dirs: " << r.dirs << "  data: " << fmt_bytes(r.bytes) << "  truncated: " << r.truncated << "\n";
    for (auto& [k, v] : r.categories) o << "    " << k << ": " << v.files << " (" << fmt_bytes(v.bytes) << ")\n";
    if (!r.main_xex.empty()) o << "  main executable: " << r.main_xex << "\n";
  }
  o << "  requirements:\n";
  for (auto& q : r.requirements) o << "    [" << to_string(q.state) << "] " << q.label << ": " << q.detail << "\n";
  o << "  STATUS: " << to_string(r.status) << " - " << r.status_reason << "\n";
  char b[64];
  std::snprintf(b, sizeof b, "  (%.2f s)\n", r.seconds);
  o << b;
  return o.str();
}

}  // namespace fsr
