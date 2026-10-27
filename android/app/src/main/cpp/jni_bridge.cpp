// JNI entry points for com.aldiianshr.fifastreet.NativeBridge.
// SPDX-License-Identifier: MIT
#include <android/native_window_jni.h>
#include <jni.h>
#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

#include "fsr/bytes.h"
#include "fsr/host.h"
#include "fsr/iso.h"
#include "fsr/report.h"
#include "native_log.h"
#include "renderer_vk.h"
#include "session.h"

extern "C" uint32_t native_selftest(uint32_t* results);

namespace fsr_android {
bool audio_test_tone(bool on);
}

using namespace fsr_android;

namespace {
std::string str(JNIEnv* env, jstring s) {
  if (!s) return {};
  const char* c = env->GetStringUTFChars(s, nullptr);
  std::string r = c ? c : "";
  if (c) env->ReleaseStringUTFChars(s, c);
  return r;
}
jstring jstr(JNIEnv* env, const std::string& s) { return env->NewStringUTF(s.c_str()); }
Session* S(jlong h) { return reinterpret_cast<Session*>(h); }
std::mutex g_err_mu;
std::string g_create_error;

std::string json_escape(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') { o.push_back('\\'); o.push_back(c); }
    else if (uint8_t(c) < 0x20) o += ' ';
    else o.push_back(c);
  }
  return o;
}
}  // namespace

#define JFN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_com_aldiianshr_fifastreet_NativeBridge_##name

JFN(jstring, analyzeXex)(JNIEnv* env, jobject, jstring path, jstring key_path, jboolean deep) {
  std::string kp = str(env, key_path);
  uint8_t key[16];
  bool have_key = !kp.empty() && fsr::load_key_file(kp, key, nullptr);
  auto r = fsr::analyze_xex(str(env, path), have_key ? key : nullptr, deep);
  std::string json = fsr::to_json(r);
  // Is there a recompiled module in this APK for exactly this image?
  if (r.image_loaded) {
    std::vector<uint8_t> file;
    fsr::XexModule x;
    std::string err;
    if (fsr::read_file(str(env, path), file) && x.parse(std::move(file), err) &&
        x.load_image(have_key ? key : nullptr, err) == fsr::LoadStatus::Ok) {
      auto* m = find_module_for_image(x.image());
      json.insert(json.size() - 1, std::string(",\"module\":") + (m ? "\"" + std::string(m->name) + "\"" : "null"));
    }
  }
  return jstr(env, json);
}

JFN(jint, sniff)(JNIEnv* env, jobject, jstring path) {
  switch (fsr::sniff_container(str(env, path))) {
    case fsr::ContainerKind::Xex: return 1;
    case fsr::ContainerKind::Xdvdfs: return 2;
    case fsr::ContainerKind::Stfs: return 3;
    default: return 0;
  }
}

JFN(jstring, isoListJson)(JNIEnv* env, jobject, jstring path) {
  std::string p = str(env, path), err;
  fsr::XisoReader r;
  if (!r.open(p, err)) return jstr(env, "{\"ok\":false,\"error\":\"" + json_escape(err) + "\"}");
  struct stat st {};
  ::stat(p.c_str(), &st);
  uint64_t fsize = uint64_t(st.st_size);
  std::string s = "{\"ok\":true,\"partition\":" + std::to_string(r.partition_offset()) + ",\"entries\":[";
  size_t truncated = 0, files = 0;
  uint64_t bytes = 0;
  bool first = true;
  for (auto& e : r.entries()) {
    if (!e.dir) {
      files++;
      bytes += e.size;
      if (e.offset + e.size > fsize) truncated++;
    }
    if (r.entries().size() <= 4000) {
      s += std::string(first ? "" : ",") + "{\"p\":\"" + json_escape(e.path) + "\",\"s\":" + std::to_string(e.size) +
           ",\"d\":" + (e.dir ? "true" : "false") + "}";
      first = false;
    }
  }
  s += "],\"files\":" + std::to_string(files) + ",\"bytes\":" + std::to_string(bytes) +
       ",\"truncated\":" + std::to_string(truncated) + "}";
  return jstr(env, s);
}

JFN(jboolean, isoExtract)(JNIEnv* env, jobject, jstring iso, jstring entry, jstring out) {
  std::string err;
  fsr::XisoReader r;
  if (!r.open(str(env, iso), err)) return JNI_FALSE;
  const fsr::IsoEntry* e = r.find(str(env, entry));
  std::vector<uint8_t> data;
  if (!e || !r.read(*e, data)) return JNI_FALSE;
  return fsr::write_file(str(env, out), data.data(), data.size()) ? JNI_TRUE : JNI_FALSE;
}

JFN(jstring, modulesJson)(JNIEnv* env, jobject) { return jstr(env, modules_json()); }

JFN(jstring, hleJson)(JNIEnv* env, jobject) {
  std::string s = "[";
  bool first = true;
  for (auto& n : fsr::implemented_hle_names()) { s += (first ? "\"" : ",\"") + n + "\""; first = false; }
  return jstr(env, s + "]");
}

JFN(jstring, gpuInfo)(JNIEnv* env, jobject) {
  GpuInfo g;
  std::string err;
  if (!VkRenderer::query(g, err)) return jstr(env, "{\"ok\":false,\"error\":\"" + json_escape(err) + "\"}");
  char buf[512];
  std::snprintf(buf, sizeof buf,
                "{\"ok\":true,\"name\":\"%s\",\"api\":\"%u.%u.%u\",\"driver\":%u,\"vendor\":%u,\"device\":%u,"
                "\"linear_blit\":%s}",
                json_escape(g.name).c_str(), VK_VERSION_MAJOR(g.api), VK_VERSION_MINOR(g.api), VK_VERSION_PATCH(g.api),
                g.driver, g.vendor, g.device, g.blit_linear ? "true" : "false");
  return jstr(env, buf);
}

// On-device differential test: native ARM64 build of selftest.c versus the
// PowerPC build that went through XEX -> recompiler -> NDK clang.
JFN(jstring, selfTest)(JNIEnv* env, jobject, jstring xex_path) {
  std::string err;
  std::vector<uint8_t> file;
  fsr::XexModule x;
  if (!fsr::read_file(str(env, xex_path), file, &err) || !x.parse(std::move(file), err) ||
      x.load_image(nullptr, err) != fsr::LoadStatus::Ok)
    return jstr(env, "{\"ok\":false,\"error\":\"" + json_escape(err) + "\"}");
  const fsr::RecompModule* m = find_module_for_image(x.image());
  if (!m) return jstr(env, "{\"ok\":false,\"error\":\"no recompiled module for this XEX\"}");
  fsr::Runtime rt;
  rt.log = [](const std::string& l) { log_line(l); };
  if (!rt.init(err) || !rt.load(x, m, err)) return jstr(env, "{\"ok\":false,\"error\":\"" + json_escape(err) + "\"}");
  uint32_t entry = rt.export_addr("fsr_selftest");
  uint32_t buf = rt.alloc(80);
  uint32_t native_res[20] = {};
  const int N = 200;
  using C = std::chrono::steady_clock;
  auto t0 = C::now();
  uint32_t nh = 0;
  for (int i = 0; i < N; i++) nh = native_selftest(native_res);
  auto t1 = C::now();
  uint64_t gh = 0;
  bool ok = true;
  for (int i = 0; i < N && ok; i++) ok = rt.call(entry, {buf}, &gh, err);
  auto t2 = C::now();
  if (!ok) return jstr(env, "{\"ok\":false,\"error\":\"" + json_escape(err) + "\"}");
  std::string s = "{\"ok\":true,\"results\":[";
  int match = 0;
  for (int i = 0; i < 20; i++) {
    uint32_t g = fsr::rd_be32(rt.base() + buf + 4 * i);
    bool eq = g == native_res[i];
    match += eq;
    s += std::string(i ? "," : "") + "{\"native\":\"" + fsr::hex32(native_res[i]) + "\",\"recomp\":\"" + fsr::hex32(g) +
         "\",\"match\":" + (eq ? "true" : "false") + "}";
  }
  double nus = std::chrono::duration<double, std::micro>(t1 - t0).count() / N;
  double gus = std::chrono::duration<double, std::micro>(t2 - t1).count() / N;
  s += "],\"matched\":" + std::to_string(match) + ",\"hash_native\":\"" + fsr::hex32(nh) + "\",\"hash_recomp\":\"" +
       fsr::hex32(uint32_t(gh)) + "\",\"native_us\":" + std::to_string(nus) + ",\"recomp_us\":" + std::to_string(gus) +
       ",\"iterations\":" + std::to_string(N) + "}";
  logf("[selftest] %d/20 match, native %.1f us, recompiled %.1f us per run", match, nus, gus);
  return jstr(env, s);
}

JFN(jlong, sessionCreate)(JNIEnv* env, jobject, jstring xex, jstring key) {
  std::string err;
  auto s = Session::create(str(env, xex), str(env, key), err);
  std::lock_guard<std::mutex> l(g_err_mu);
  g_create_error = err;
  if (!s) {
    logf("[session] create failed: %s", err.c_str());
    return 0;
  }
  return reinterpret_cast<jlong>(s.release());
}

JFN(jstring, lastCreateError)(JNIEnv* env, jobject) {
  std::lock_guard<std::mutex> l(g_err_mu);
  return jstr(env, g_create_error);
}

JFN(void, sessionSetSurface)(JNIEnv* env, jobject, jlong h, jobject surface) {
  if (!h) return;
  ANativeWindow* w = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
  S(h)->set_window(w);
}

JFN(void, sessionStart)(JNIEnv*, jobject, jlong h) { if (h) S(h)->start(); }
JFN(void, sessionPause)(JNIEnv*, jobject, jlong h) { if (h) S(h)->pause(); }
JFN(void, sessionResume)(JNIEnv*, jobject, jlong h) { if (h) S(h)->resume(); }
JFN(void, sessionStop)(JNIEnv*, jobject, jlong h) { if (h) S(h)->stop(); }
JFN(void, sessionDestroy)(JNIEnv*, jobject, jlong h) { delete S(h); }
JFN(jboolean, sessionRunning)(JNIEnv*, jobject, jlong h) { return h && S(h)->running(); }

JFN(void, sessionSetPad)(JNIEnv*, jobject, jlong h, jint buttons, jint lt, jint rt, jint lx, jint ly, jint rx, jint ry,
                         jboolean connected) {
  if (!h) return;
  fsr::PadState p;
  p.buttons = uint16_t(buttons);
  p.left_trigger = uint8_t(std::min(255, std::max(0, int(lt))));
  p.right_trigger = uint8_t(std::min(255, std::max(0, int(rt))));
  auto clamp16 = [](jint v) { return int16_t(std::min(32767, std::max(-32768, int(v)))); };
  p.lx = clamp16(lx);
  p.ly = clamp16(ly);
  p.rx = clamp16(rx);
  p.ry = clamp16(ry);
  p.connected = connected;
  S(h)->set_pad(p);
}

JFN(void, sessionSetGraphics)(JNIEnv*, jobject, jlong h, jint w, jint hh, jint scale, jint fps, jboolean vsync,
                              jboolean linear, jint aspect, jboolean thermal) {
  if (!h) return;
  GraphicsConfig g;
  g.width = uint32_t(w);
  g.height = uint32_t(hh);
  g.scale_pct = std::min(100, std::max(50, int(scale)));
  g.fps_cap = fps;
  g.vsync = vsync;
  g.linear = linear;
  g.aspect = aspect;
  g.thermal_adapt = thermal;
  S(h)->set_graphics(g);
}

JFN(void, sessionSetThermal)(JNIEnv*, jobject, jlong h, jint status) { if (h) S(h)->set_thermal_status(status); }

JFN(jfloatArray, sessionStats)(JNIEnv* env, jobject, jlong h) {
  jfloatArray a = env->NewFloatArray(9);
  if (!h) return a;
  SessionStats s = S(h)->stats();
  float v[9] = {s.fps, s.frame_ms, s.guest_ms, s.present_ms, float(s.width), float(s.height), float(s.frames),
                float(s.kernel_calls), s.throttled ? 1.f : 0.f};
  env->SetFloatArrayRegion(a, 0, 9, v);
  return a;
}

JFN(jstring, sessionError)(JNIEnv* env, jobject, jlong h) { return jstr(env, h ? S(h)->last_error() : ""); }
JFN(jstring, sessionGpu)(JNIEnv* env, jobject, jlong h) { return jstr(env, h ? S(h)->gpu_name() : ""); }
JFN(jstring, drainLog)(JNIEnv* env, jobject) { return jstr(env, drain_log()); }
JFN(jboolean, audioTestTone)(JNIEnv*, jobject, jboolean on) { return audio_test_tone(on) ? JNI_TRUE : JNI_FALSE; }
