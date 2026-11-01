// SPDX-License-Identifier: MIT
#include "session.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "fsr/bytes.h"
#include "fsr/crypto.h"
#include "fsr/disc_scan.h"
#include "fsr/report.h"
#include "native_log.h"

extern const fsr::RecompModule fsr_module_hb64;

namespace fsr_android {
namespace {
using Clock = std::chrono::steady_clock;
// Every recompiled module compiled into this APK is registered with the core
// registry. A commercial title can only run after its executable has been
// recompiled (fsr-recomp, on a PC, from the user's own disc) and built in.
struct RegisterModules {
  RegisterModules() { fsr::register_module(&fsr_module_hb64); }
} g_register;
}  // namespace

const fsr::RecompModule* find_module_for_image(const std::vector<uint8_t>& image) {
  return fsr::find_registered_module(image);
}

std::string modules_json() {
  size_t n = 0;
  const fsr::RecompModule* const* mods = fsr::registered_modules(&n);
  std::string s = "[";
  for (size_t i = 0; i < n; i++) {
    auto* m = mods[i];
    s += (i ? "," : "");
    s += "{\"name\":\"" + std::string(m->name) + "\",\"base\":\"" + fsr::hex32(m->image_base) + "\",\"size\":" +
         std::to_string(m->image_size) + ",\"functions\":" + std::to_string(m->func_count) + ",\"sha1\":\"" +
         fsr::hexbytes(m->image_sha1, 20) + "\",\"untranslated\":" + std::to_string(m->unimplemented_sites) + "}";
  }
  return s + "]";
}

static bool key_from(const std::string& key_path, uint8_t key[16]) {
  return !key_path.empty() && fsr::load_key_file(key_path, key, nullptr);
}

bool Session::bind(std::string& err) {
  mod_ = find_module_for_image(xex_.image());
  if (!mod_) {
    err = "Executable ini belum memiliki modul ARM64 hasil recompile di dalam APK. Kode PowerPC-nya harus di-recompile "
          "(fsr-recomp) lalu dibangun ke APK; runtime tidak memakai emulator/interpreter sebagai cadangan.";
    return false;
  }
  rt_.log = [](const std::string& l) { log_line(l); };
  if (!rt_.init(err) || !rt_.load(xex_, mod_, err)) return false;
  frame_fn_ = rt_.export_addr("fsr_frame");
  if (!frame_fn_) {
    err = "modul tidak memiliki entry fsr_frame; entry retail membutuhkan thread guest dan GPU Xenos yang belum dibuat";
    return false;
  }
  fb_ = rt_.alloc(1920 * 1080 * 4, 4096);
  logf("[session] module '%s' bound: %u functions, image %08X+%X", mod_->name, mod_->func_count, mod_->image_base,
       mod_->image_size);
  return true;
}

std::unique_ptr<Session> Session::create(const std::string& xex_path, const std::string& key_path, std::string& err) {
  std::unique_ptr<Session> s(new Session());
  std::vector<uint8_t> file;
  if (!fsr::read_file(xex_path, file, &err)) return nullptr;
  if (!s->xex_.parse(std::move(file), err)) return nullptr;
  uint8_t key[16];
  fsr::LoadStatus st = s->xex_.load_image(key_from(key_path, key) ? key : nullptr, err);
  if (st != fsr::LoadStatus::Ok) {
    err = std::string("XEX image: ") + fsr::to_string(st) + " - " + err;
    return nullptr;
  }
  if (!s->bind(err)) return nullptr;
  return s;
}

std::unique_ptr<Session> Session::create_from_disc(int fd, const std::string& key_path, std::string& err) {
  auto src = fsr::FdSource::adopt(fd, &err);
  if (!src) return nullptr;
  auto disc = std::make_shared<fsr::XisoReader>();
  if (!disc->open(src, err)) return nullptr;
  std::unique_ptr<Session> s(new Session());
  uint8_t key[16];
  std::string inner;
  if (!fsr::load_main_xex(*disc, key_from(key_path, key) ? key : nullptr, s->xex_, &inner, err)) return nullptr;
  if (!s->bind(err)) return nullptr;
  s->rt_.mount_disc(std::make_shared<fsr::DiscVfs>(disc));
  logf("[session] disc mounted as game:\\ (%s, %zu entries), executable %s read in place", disc->layout().layout.c_str(),
       disc->entries().size(), inner.c_str());
  return s;
}

Session::~Session() {
  stop();
  if (window_) ANativeWindow_release(window_);
}

void Session::set_window(ANativeWindow* win) {
  std::unique_lock<std::mutex> l(mu_);
  if (win && win == window_) {
    // Same surface, new size (e.g. system bars hidden): keep the Vulkan device,
    // only rebuild the swapchain.
    ANativeWindow_release(win);
    window_resized_ = true;
    cv_.notify_all();
    return;
  }
  ANativeWindow* old = window_;
  window_ = win;
  window_dirty_ = true;
  cv_.notify_all();
  // The render thread must drop the old surface before Android destroys it.
  if (running_ && thread_.joinable())
    cv_.wait_for(l, std::chrono::seconds(2), [&] { return !window_dirty_ || !running_; });
  if (old) ANativeWindow_release(old);
}

void Session::start() {
  if (running_) return;
  {
    std::lock_guard<std::mutex> l(mu_);
    quit_ = false;
    paused_ = false;
    error_.clear();
  }
  running_ = true;
  thread_ = std::thread([this] { loop(); });
}

void Session::pause() {
  std::lock_guard<std::mutex> l(mu_);
  paused_ = true;
}

void Session::resume() {
  std::lock_guard<std::mutex> l(mu_);
  paused_ = false;
  cv_.notify_all();
}

void Session::stop() {
  {
    std::lock_guard<std::mutex> l(mu_);
    quit_ = true;
    cv_.notify_all();
  }
  if (thread_.joinable()) thread_.join();
  running_ = false;
}

void Session::set_graphics(const GraphicsConfig& g) {
  std::lock_guard<std::mutex> l(mu_);
  gfx_ = g;
}

void Session::set_thermal_status(int status) {
  std::lock_guard<std::mutex> l(mu_);
  if (status != thermal_) logf("[session] thermal status %d", status);
  thermal_ = status;
}

SessionStats Session::stats() const {
  std::lock_guard<std::mutex> l(mu_);
  SessionStats s = stats_;
  s.kernel_calls = rt_.kernel_calls();
  return s;
}

std::string Session::last_error() const {
  std::lock_guard<std::mutex> l(mu_);
  return error_;
}

std::string Session::gpu_name() const {
  std::lock_guard<std::mutex> l(mu_);
  return gpu_;
}

void Session::loop() {
  VkRenderer renderer;
  uint32_t frame = 0;
  auto next = Clock::now();
  auto window_start = Clock::now();
  uint32_t window_frames = 0;
  double acc_frame = 0, acc_guest = 0, acc_present = 0;
  double acc_wait = 0, acc_acq = 0, acc_copy = 0, acc_sub = 0, acc_qp = 0;
  while (true) {
    GraphicsConfig g;
    int thermal;
    {
      std::unique_lock<std::mutex> l(mu_);
      while (true) {
        if (window_dirty_) {
          renderer.shutdown();
          window_dirty_ = false;
          if (window_) {
            std::string err;
            if (!renderer.init(window_, err)) {
              error_ = "Vulkan: " + err;
              logf("[session] %s", error_.c_str());
              cv_.notify_all();
              quit_ = true;
            } else {
              gpu_ = renderer.info().name;
            }
          }
          cv_.notify_all();
        }
        if (window_resized_ && !window_dirty_) {
          window_resized_ = false;
          std::string err;
          if (renderer.ready() && !renderer.resize(err)) logf("[session] swapchain resize failed: %s", err.c_str());
        }
        if (quit_ || (!paused_ && window_ && renderer.ready())) break;
        cv_.wait(l);
      }
      if (quit_) break;
      g = gfx_;
      thermal = thermal_;
    }
    // Internal resolution with render scale (kept even, clamped to the buffer).
    uint32_t w = std::max<uint32_t>(160, (g.width * uint32_t(g.scale_pct) / 100) & ~1u);
    uint32_t h = std::max<uint32_t>(90, (g.height * uint32_t(g.scale_pct) / 100) & ~1u);
    if (w > 1920) w = 1920;
    if (h > 1080) h = 1080;
    bool throttled = g.thermal_adapt && thermal >= 3;  // THERMAL_STATUS_SEVERE and above
    int cap = throttled ? 30 : g.fps_cap;

    auto t0 = Clock::now();
    std::string err;
    uint64_t ret = 0;
    if (!rt_.call(frame_fn_, {fb_, w, h, frame}, &ret, err)) {
      std::lock_guard<std::mutex> l(mu_);
      error_ = "guest: " + err;
      logf("[session] %s", error_.c_str());
      break;
    }
    auto t1 = Clock::now();
    RenderSettings rs;
    rs.vsync = g.vsync;
    rs.linear = g.linear;
    rs.aspect = g.aspect;
    if (!renderer.present(rt_.base() + fb_, w, h, rs, err)) {
      std::lock_guard<std::mutex> l(mu_);
      error_ = "present: " + err;
      logf("[session] %s", error_.c_str());
      break;
    }
    auto t2 = Clock::now();
    frame++;

    // Frame pacing: fixed deadlines, resynchronised if we fall behind.
    auto period = std::chrono::nanoseconds(1000000000LL / std::max(1, cap));
    next += period;
    auto now = Clock::now();
    if (next < now - period) next = now;
    else std::this_thread::sleep_until(next);
    auto t3 = Clock::now();

    acc_guest += std::chrono::duration<double, std::milli>(t1 - t0).count();
    acc_present += std::chrono::duration<double, std::milli>(t2 - t1).count();
    {
      const PresentTimings& pt = renderer.last_timings();
      acc_wait += pt.wait; acc_acq += pt.acquire; acc_copy += pt.copy; acc_sub += pt.submit; acc_qp += pt.present;
    }
    acc_frame += std::chrono::duration<double, std::milli>(t3 - t0).count();
    window_frames++;
    double span = std::chrono::duration<double>(t3 - window_start).count();
    if (span >= 0.5) {
      std::lock_guard<std::mutex> l(mu_);
      stats_.fps = float(window_frames / span);
      stats_.frame_ms = float(acc_frame / window_frames);
      stats_.guest_ms = float(acc_guest / window_frames);
      stats_.present_ms = float(acc_present / window_frames);
      stats_.wait_ms = float(acc_wait / window_frames);
      stats_.acquire_ms = float(acc_acq / window_frames);
      stats_.copy_ms = float(acc_copy / window_frames);
      stats_.submit_ms = float(acc_sub / window_frames);
      stats_.queue_present_ms = float(acc_qp / window_frames);
      const SwapInfo& si = renderer.swap_info();
      stats_.sc_images = si.images; stats_.sc_mode = uint32_t(si.mode); stats_.sc_transform = si.transform;
      stats_.sc_current_transform = si.current_transform; stats_.sc_recreations = si.recreations;
      stats_.sc_suboptimal = si.suboptimal;
      stats_.width = w;
      stats_.height = h;
      stats_.frames = frame;
      stats_.throttled = throttled;
      window_start = t3;
      window_frames = 0;
      acc_frame = acc_guest = acc_present = 0;
      acc_wait = acc_acq = acc_copy = acc_sub = acc_qp = 0;
    }
  }
  renderer.shutdown();
  std::lock_guard<std::mutex> l(mu_);
  running_ = false;
  window_dirty_ = false;
  cv_.notify_all();
}

}  // namespace fsr_android
