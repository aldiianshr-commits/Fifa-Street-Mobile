// Vulkan presenter: uploads the guest framebuffer produced by recompiled code
// into a GPU image at the configured internal resolution and blits it to the
// swapchain (aspect fit/stretch, nearest/linear filtering, FIFO/MAILBOX).
// NOTE: this is not an Xenos (Xbox 360 GPU) translator - see docs.
// SPDX-License-Identifier: MIT
#pragma once
#include <android/native_window.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace fsr_android {

struct RenderSettings {
  bool vsync = true;
  bool linear = true;
  int aspect = 0;  // 0 = keep 16:9 (letterbox), 1 = stretch to screen
};

struct GpuInfo {
  std::string name;
  uint32_t api = 0, driver = 0, vendor = 0, device = 0;
  bool blit_linear = false;
  bool mailbox = false, immediate = false;
};

class VkRenderer {
 public:
  ~VkRenderer();
  bool init(ANativeWindow* window, std::string& err);
  void shutdown();
  bool ready() const { return swapchain_ != VK_NULL_HANDLE; }
  // rgba: tightly packed width*height*4 bytes (R,G,B,A).
  bool present(const uint8_t* rgba, uint32_t w, uint32_t h, const RenderSettings& s, std::string& err);
  const GpuInfo& info() const { return info_; }
  static bool query(GpuInfo& out, std::string& err);  // instance-only probe for the settings screen

 private:
  bool create_swapchain(bool vsync, std::string& err);
  void destroy_swapchain();
  bool ensure_guest_image(uint32_t w, uint32_t h, std::string& err);
  void destroy_guest_image();
  uint32_t find_memory(uint32_t bits, VkMemoryPropertyFlags flags) const;

  ANativeWindow* window_ = nullptr;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice phys_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  uint32_t qfamily_ = 0;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkFormat sc_format_ = VK_FORMAT_UNDEFINED;
  VkExtent2D sc_extent_{};
  bool sc_vsync_ = true;
  std::vector<VkImage> sc_images_;
  VkCommandPool pool_ = VK_NULL_HANDLE;

  static constexpr int kFrames = 2;
  struct Frame {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE, rendered = VK_NULL_HANDLE;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    void* staging_ptr = nullptr;
    VkDeviceSize staging_size = 0;
  } frames_[kFrames];
  int frame_index_ = 0;

  VkImage guest_ = VK_NULL_HANDLE;
  VkDeviceMemory guest_mem_ = VK_NULL_HANDLE;
  uint32_t guest_w_ = 0, guest_h_ = 0;
  bool guest_initialized_ = false;
  GpuInfo info_;
};

}  // namespace fsr_android
