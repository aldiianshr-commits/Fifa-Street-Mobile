// fsr-analyze / fsr-recomp command line tools.
//   fsr-analyze <file.xex> [--key keyfile] [--json] [--shallow]
//   fsr-recomp  <file.xex> --out <dir> [--name module] [--key keyfile]
//               [--syms file.syms] [--export name=0xADDR]... [--no-comments]
// SPDX-License-Identifier: MIT
#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "fsr/bytes.h"
#include "fsr/recomp.h"
#include "fsr/report.h"

using namespace fsr;

static int analyze_main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: fsr-analyze file.xex [--key k] [--json] [--shallow]\n"); return 2; }
  std::string path = argv[1], keyp;
  bool json = false, deep = true;
  for (int i = 2; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--key" && i + 1 < argc) keyp = argv[++i];
    else if (a == "--json") json = true;
    else if (a == "--shallow") deep = false;
  }
  uint8_t key[16];
  std::string err;
  if (!keyp.empty() && !load_key_file(keyp, key, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 2; }
  auto r = analyze_xex(path, keyp.empty() ? nullptr : key, deep);
  std::string s = json ? to_json(r) + "\n" : to_text(r);
  std::fwrite(s.data(), 1, s.size(), stdout);
  return r.parsed ? 0 : 1;
}

static int recomp_main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: fsr-recomp file.xex --out dir [--name n] [--key k] [--syms f] [--export n=0xA]\n");
    return 2;
  }
  std::string path = argv[1], out = "recomp_out", name = "game", keyp, syms;
  std::vector<std::pair<std::string, uint32_t>> exports;
  bool comments = true;
  for (int i = 2; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--out" && i + 1 < argc) out = argv[++i];
    else if (a == "--name" && i + 1 < argc) name = argv[++i];
    else if (a == "--key" && i + 1 < argc) keyp = argv[++i];
    else if (a == "--syms" && i + 1 < argc) syms = argv[++i];
    else if (a == "--no-comments") comments = false;
    else if (a == "--export" && i + 1 < argc) {
      std::string e = argv[++i];
      auto eq = e.find('=');
      if (eq == std::string::npos) exports.emplace_back(e, 0);  // resolved from --syms
      else exports.emplace_back(e.substr(0, eq), uint32_t(std::stoul(e.substr(eq + 1), nullptr, 0)));
    }
  }
  std::vector<uint8_t> file;
  std::string err;
  if (!read_file(path, file, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
  XexModule x;
  if (!x.parse(std::move(file), err)) { std::fprintf(stderr, "parse: %s\n", err.c_str()); return 1; }
  uint8_t key[16];
  if (!keyp.empty() && !load_key_file(keyp, key, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
  LoadStatus st = x.load_image(keyp.empty() ? nullptr : key, err);
  if (st != LoadStatus::Ok) { std::fprintf(stderr, "load: %s (%s)\n", to_string(st), err.c_str()); return 1; }
  RecompInput in = make_input(x, name);
  in.exports = exports;
  if (!syms.empty()) {
    std::vector<uint8_t> sd;
    if (read_file(syms, sd)) {
      std::string s(sd.begin(), sd.end());
      size_t p = 0;
      while (p < s.size()) {
        size_t nl = s.find('\n', p);
        std::string line = s.substr(p, nl == std::string::npos ? std::string::npos : nl - p);
        p = nl == std::string::npos ? s.size() : nl + 1;
        auto sp = line.find(' ');
        if (sp == std::string::npos) continue;
        uint32_t addr = uint32_t(std::stoul(line.substr(sp + 1), nullptr, 16));
        in.extra_functions.push_back(addr);
        for (auto& ex : in.exports)
          if (!ex.second && ex.first == line.substr(0, sp)) ex.second = addr;
      }
    }
  }
  for (auto& ex : in.exports)
    if (!ex.second) { std::fprintf(stderr, "export %s not found in --syms\n", ex.first.c_str()); return 1; }
  RecompOptions opt;
  opt.emit_comments = comments;
  RecompOutput res = recompile(in, opt);
  ::mkdir(out.c_str(), 0755);
  for (auto& [fn, content] : res.files)
    if (!write_file(out + "/" + fn, content.data(), content.size())) {
      std::fprintf(stderr, "cannot write %s\n", fn.c_str());
      return 1;
    }
  auto& s = res.stats;
  std::printf("fsr-recomp: %s -> %s/ (%zu files)\n  functions %llu, instructions %llu, translated %llu (%.2f%%), "
              "vmx %llu, invalid %llu\n",
              path.c_str(), out.c_str(), res.files.size(), (unsigned long long)s.functions,
              (unsigned long long)s.instructions, (unsigned long long)s.translated, s.coverage() * 100,
              (unsigned long long)s.vmx, (unsigned long long)s.invalid);
  return 0;
}

int main(int argc, char** argv) {
  std::string self = argv[0];
  auto slash = self.find_last_of('/');
  if (slash != std::string::npos) self = self.substr(slash + 1);
  if (self.find("recomp") != std::string::npos) return recomp_main(argc, argv);
  return analyze_main(argc, argv);
}
