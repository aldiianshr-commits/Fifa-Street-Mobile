// A running title: XEX image + recompiled module + runtime + render loop.
// SPDX-License-Identifier: MIT
#pragma once
#include <android/native_window.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "fsr/host.h"
#include "fsr/xex.h"
#include "renderer_vk.h"

namespace fsr_android {

struct GraphicsConfig {
  uint32_t width = 1280, height = 720;  // internal resolution before render scale
  int scale_pct = 100;                  // render scale 50..100
  int fps_cap = 60;                     // 30 or 60
  bool vsync = true;
  bool linear = true;
  int aspect = 0;                       // 0 = 16:9, 1 = stretch
  bool thermal_adapt = true;            // drop to 30 FPS while the device is throttling
};

struct SessionStats {
  float fps = 0, frame_ms = 0, guest_ms = 0, present_ms = 0;
  uint32_t width = 0, height = 0;
  uint64_t frames = 0, kernel_calls = 0;
  bool throttled = false;
};

const fsr::RecompModule* find_module_for_image(const std::vector<uint8_t>& image);
std::string modules_json();

class Session {
 public:
  static std::unique_ptr<Session> create(const std::string& xex_path, const std::string& key_path, std::string& err);
  ~Session();

  void set_window(ANativeWindow* win);  // takes ownership of one reference
  void start();
  void pause();
  void resume();
  void stop();
  bool running() const { return running_; }
  void set_pad(const fsr::PadState& p) { rt_.set_pad(p); }
  void set_graphics(const GraphicsConfig& g);
  void set_thermal_status(int status);  // PowerManager.THERMAL_STATUS_*
  SessionStats stats() const;
  std::string last_error() const;
  std::string gpu_name() const;

 private:
  Session() = default;
  void loop();

  fsr::XexModule xex_;
  fsr::Runtime rt_;
  const fsr::RecompModule* mod_ = nullptr;
  uint32_t frame_fn_ = 0, fb_ = 0;

  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  bool paused_ = false, quit_ = false, window_dirty_ = false;
  ANativeWindow* window_ = nullptr;
  GraphicsConfig gfx_;
  int thermal_ = 0;
  SessionStats stats_;
  std::string error_, gpu_;
};

}  // namespace fsr_android
