// SPDX-License-Identifier: MIT
#include "fsr/vfs.h"

#include <cctype>

namespace fsr {

static bool starts_ci(const std::string& s, const char* p) {
  size_t n = 0;
  for (; p[n]; n++)
    if (n >= s.size() || std::tolower(static_cast<unsigned char>(s[n])) != std::tolower(static_cast<unsigned char>(p[n])))
      return false;
  return true;
}

bool DiscVfs::translate(const std::string& guest, std::string* disc_path, std::string* device) {
  std::string g = guest;
  for (auto& c : g)
    if (c == '/') c = '\\';
  if (starts_ci(g, "\\??\\")) g = g.substr(4);
  static const char* kMounts[] = {"game:", "d:", "\\device\\cdrom0", "\\device\\harddisk0\\partition0\\game"};
  for (const char* m : kMounts) {
    if (starts_ci(g, m)) {
      std::string rest = g.substr(std::string(m).size());
      if (!rest.empty() && rest[0] != '\\') continue;  // e.g. "gamedata:"
      if (device) *device = m;
      if (disc_path) *disc_path = normalize_disc_path(rest);
      return true;
    }
  }
  if (device) {
    auto colon = g.find(':');
    *device = colon == std::string::npos ? g.substr(0, g.find('\\', 1)) : g.substr(0, colon + 1);
  }
  return false;
}

bool DiscVfs::resolve(const std::string& disc_path, const IsoEntry** out, bool* is_root) const {
  *is_root = disc_path.empty();
  *out = nullptr;
  if (*is_root) return true;
  *out = disc_->find(disc_path);
  return *out != nullptr;
}

uint32_t DiscVfs::open(const IsoEntry* e) {
  uint32_t h = next_;
  next_ += 4;
  handles_[h] = OpenFile{e, 0};
  opens++;
  return h;
}

bool DiscVfs::close(uint32_t handle) { return handles_.erase(handle) != 0; }

OpenFile* DiscVfs::lookup(uint32_t handle) {
  auto it = handles_.find(handle);
  return it == handles_.end() ? nullptr : &it->second;
}

}  // namespace fsr
