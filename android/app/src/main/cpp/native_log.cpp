// SPDX-License-Identifier: MIT
#include "native_log.h"

#include <android/log.h>

#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>

namespace fsr_android {
namespace {
std::mutex g_mu;
std::deque<std::string> g_lines;
constexpr size_t kMax = 2000;
}  // namespace

void log_line(const std::string& line) {
  __android_log_write(ANDROID_LOG_INFO, "FSR", line.c_str());
  std::lock_guard<std::mutex> l(g_mu);
  g_lines.push_back(line);
  while (g_lines.size() > kMax) g_lines.pop_front();
}

void logf(const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  log_line(buf);
}

std::string drain_log() {
  std::lock_guard<std::mutex> l(g_mu);
  std::string out;
  for (auto& s : g_lines) { out += s; out += '\n'; }
  g_lines.clear();
  return out;
}

}  // namespace fsr_android
