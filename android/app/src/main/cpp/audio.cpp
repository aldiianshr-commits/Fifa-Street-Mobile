// AAudio output path test (sine tone). Game audio (XAudio2 / XMA decoding) is
// not implemented yet; this only verifies the device audio backend works.
// SPDX-License-Identifier: MIT
#include <aaudio/AAudio.h>

#include <atomic>
#include <cmath>
#include <mutex>

#include "native_log.h"

namespace fsr_android {
namespace {
std::mutex g_mu;
AAudioStream* g_stream = nullptr;
std::atomic<double> g_phase{0};

aaudio_data_callback_result_t cb(AAudioStream* s, void*, void* data, int32_t frames) {
  float* out = static_cast<float*>(data);
  int32_t ch = AAudioStream_getChannelCount(s);
  double rate = AAudioStream_getSampleRate(s);
  double ph = g_phase.load();
  for (int32_t i = 0; i < frames; i++) {
    float v = float(0.15 * std::sin(ph));
    ph += 2.0 * M_PI * 440.0 / rate;
    if (ph > 2.0 * M_PI) ph -= 2.0 * M_PI;
    for (int32_t c = 0; c < ch; c++) out[i * ch + c] = v;
  }
  g_phase.store(ph);
  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
}  // namespace

bool audio_test_tone(bool on) {
  std::lock_guard<std::mutex> l(g_mu);
  if (!on) {
    if (g_stream) {
      AAudioStream_requestStop(g_stream);
      AAudioStream_close(g_stream);
      g_stream = nullptr;
    }
    return true;
  }
  if (g_stream) return true;
  AAudioStreamBuilder* b = nullptr;
  if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return false;
  AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
  AAudioStreamBuilder_setChannelCount(b, 2);
  AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
  AAudioStreamBuilder_setDataCallback(b, cb, nullptr);
  aaudio_result_t r = AAudioStreamBuilder_openStream(b, &g_stream);
  AAudioStreamBuilder_delete(b);
  if (r != AAUDIO_OK) {
    logf("[audio] open failed: %s", AAudio_convertResultToText(r));
    g_stream = nullptr;
    return false;
  }
  r = AAudioStream_requestStart(g_stream);
  logf("[audio] AAudio stream %d Hz, %d ch, burst %d: %s", AAudioStream_getSampleRate(g_stream),
       AAudioStream_getChannelCount(g_stream), AAudioStream_getFramesPerBurst(g_stream), AAudio_convertResultToText(r));
  return r == AAUDIO_OK;
}

}  // namespace fsr_android
