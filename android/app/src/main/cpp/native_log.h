// Native log: forwards to logcat and keeps a ring buffer the UI can drain.
// SPDX-License-Identifier: MIT
#pragma once
#include <string>

namespace fsr_android {
void log_line(const std::string& line);
void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
std::string drain_log();
}  // namespace fsr_android
