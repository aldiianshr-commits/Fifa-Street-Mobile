// SPDX-License-Identifier: MIT
#include "fsr/report.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "fsr/bytes.h"
#include "fsr/host.h"

namespace fsr {

bool load_key_file(const std::string& path, uint8_t key[16], std::string* err) {
  std::vector<uint8_t> d;
  if (!read_file(path, d, err)) return false;
  if (d.size() == 16) { std::memcpy(key, d.data(), 16); return true; }
  std::string hex;
  for (uint8_t c : d)
    if (std::isxdigit(c)) hex.push_back(char(c));
  if (hex.size() != 32) {
    if (err) *err = "key file must hold 16 raw bytes or 32 hex digits";
    return false;
  }
  for (int i = 0; i < 16; i++) key[i] = uint8_t(std::stoul(hex.substr(2 * i, 2), nullptr, 16));
  return true;
}

AnalysisReport analyze_xex(const std::string& path, const uint8_t* key, bool deep) {
  AnalysisReport r;
  r.path = path;
  std::vector<uint8_t> file;
  if (!read_file(path, file, &r.error)) return r;
  XexModule x;
  if (!x.parse(std::move(file), r.error)) return r;
  r.parsed = true;
  r.info = x.info();
  std::string err;
  r.load_status = x.load_image(key, err);
  r.load_error = err;
  r.image_loaded = r.load_status == LoadStatus::Ok;
  if (!r.image_loaded) return r;
  r.info = x.info();  // now with resolved import names
  r.sections = x.sections();
  r.pdata_functions = x.pdata().size();
  for (auto& lib : r.info.imports) {
    for (size_t i = 0; i < lib.records.size(); i++) {
      const auto& rec = lib.records[i];
      if (rec.type == 1) continue;  // counted via its IAT slot
      ImportSummary s;
      s.lib = lib.name;
      s.ordinal = rec.ordinal;
      s.name = rec.name.empty() ? "#" + std::to_string(rec.ordinal) : rec.name;
      s.function = i + 1 < lib.records.size() && lib.records[i + 1].type == 1;
      std::string dummy;
      s.implemented = find_hle(lib.name, rec.ordinal, &dummy) != nullptr;
      if (s.implemented) r.imports_implemented++;
      r.imports.push_back(s);
    }
  }
  if (deep) {
    r.deep = true;
    RecompInput in = make_input(x, "game");
    r.stats = analyze(in);
  }
  return r;
}

static std::string jstr(const std::string& s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') { o.push_back('\\'); o.push_back(c); }
    else if (uint8_t(c) < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
    else o.push_back(c);
  }
  return o + "\"";
}

std::string to_json(const AnalysisReport& r) {
  std::ostringstream o;
  const auto& i = r.info;
  o << "{\"path\":" << jstr(r.path) << ",\"parsed\":" << (r.parsed ? "true" : "false")
    << ",\"error\":" << jstr(r.error);
  if (r.parsed) {
    o << ",\"title_id\":" << jstr(hex32(i.title_id)) << ",\"media_id\":" << jstr(hex32(i.media_id))
      << ",\"version\":" << jstr(hex32(i.version)) << ",\"disc\":" << int(i.disc_number) << ",\"discs\":"
      << int(i.disc_count) << ",\"entry\":" << jstr(hex32(i.entry_point)) << ",\"base\":" << jstr(hex32(i.load_address))
      << ",\"image_size\":" << i.image_size << ",\"encryption\":" << int(i.encryption)
      << ",\"compression\":" << int(i.compression) << ",\"pe_name\":" << jstr(i.original_pe_name)
      << ",\"region\":" << jstr(hex32(i.region)) << ",\"tls_slots\":" << i.tls_slot_count;
    o << ",\"static_libs\":[";
    for (size_t k = 0; k < i.static_libs.size(); k++) {
      auto& l = i.static_libs[k];
      o << (k ? "," : "") << jstr(l.name + " " + std::to_string(l.major) + "." + std::to_string(l.minor) + "." +
                                  std::to_string(l.build) + "." + std::to_string(l.qfe));
    }
    o << "],\"libraries\":[";
    for (size_t k = 0; k < i.imports.size(); k++) o << (k ? "," : "") << jstr(i.imports[k].name);
    o << "],\"load_status\":" << jstr(to_string(r.load_status)) << ",\"load_error\":" << jstr(r.load_error);
  }
  if (r.image_loaded) {
    o << ",\"sections\":[";
    for (size_t k = 0; k < r.sections.size(); k++) {
      auto& s = r.sections[k];
      o << (k ? "," : "") << "{\"name\":" << jstr(s.name) << ",\"va\":" << jstr(hex32(s.va)) << ",\"size\":" << s.vsize
        << ",\"exec\":" << (s.executable() ? "true" : "false") << "}";
    }
    o << "],\"pdata_functions\":" << r.pdata_functions << ",\"imports_total\":" << r.imports.size()
      << ",\"imports_implemented\":" << r.imports_implemented << ",\"imports\":[";
    for (size_t k = 0; k < r.imports.size(); k++) {
      auto& m = r.imports[k];
      o << (k ? "," : "") << "{\"lib\":" << jstr(m.lib) << ",\"name\":" << jstr(m.name) << ",\"ord\":" << m.ordinal
        << ",\"fn\":" << (m.function ? "true" : "false") << ",\"impl\":" << (m.implemented ? "true" : "false") << "}";
    }
    o << "]";
    if (r.deep) {
      auto& s = r.stats;
      o << ",\"functions\":" << s.functions << ",\"instructions\":" << s.instructions << ",\"translated\":" << s.translated
        << ",\"vmx\":" << s.vmx << ",\"invalid\":" << s.invalid << ",\"dynamic_functions\":" << s.dynamic_functions
        << ",\"indirect_sites\":" << s.indirect_call_sites << ",\"coverage\":" << s.coverage();
    }
  }
  o << "}";
  return o.str();
}

std::string to_text(const AnalysisReport& r) {
  std::ostringstream o;
  if (!r.parsed) { o << "ERROR: " << r.error << "\n"; return o.str(); }
  const auto& i = r.info;
  const char* enc[] = {"none", "AES (normal)"};
  const char* cmp[] = {"none", "basic", "LZX (normal)", "delta"};
  o << "XEX2 " << r.path << "\n"
    << "  title id     : " << hex32(i.title_id) << "   media id: " << hex32(i.media_id) << "   version: " << hex32(i.version)
    << "\n  disc         : " << int(i.disc_number) << "/" << int(i.disc_count) << "\n  original PE  : " << i.original_pe_name
    << "\n  base / entry : " << hex32(i.load_address) << " / " << hex32(i.entry_point) << "\n  image size   : "
    << i.image_size << " bytes\n  encryption   : " << (unsigned(i.encryption) < 2 ? enc[unsigned(i.encryption)] : "?")
    << "\n  compression  : " << (unsigned(i.compression) < 4 ? cmp[unsigned(i.compression)] : "?") << "\n";
  for (auto& l : i.static_libs)
    o << "  static lib   : " << l.name << " " << l.major << "." << l.minor << "." << l.build << "." << l.qfe << "\n";
  o << "  image load   : " << to_string(r.load_status) << (r.load_error.empty() ? "" : " - " + r.load_error) << "\n";
  if (!r.image_loaded) return o.str();
  for (auto& s : r.sections)
    o << "  section " << s.name << " va=" << hex32(s.va) << " size=" << s.vsize << (s.executable() ? " [code]" : "") << "\n";
  o << "  .pdata functions : " << r.pdata_functions << "\n";
  o << "  imports          : " << r.imports.size() << " (" << r.imports_implemented << " implemented by runtime HLE)\n";
  if (r.deep) {
    auto& s = r.stats;
    char pct[32];
    std::snprintf(pct, sizeof pct, "%.2f%%", s.coverage() * 100);
    o << "  functions found  : " << s.functions << " (" << s.dynamic_functions << " with bctr dispatch)\n"
      << "  instructions     : " << s.instructions << "\n  translated       : " << s.translated << " (" << pct << ")\n"
      << "  VMX/VMX128       : " << s.vmx << " (not yet translated)\n  invalid words    : " << s.invalid << "\n";
  }
  return o.str();
}

}  // namespace fsr
