// SPDX-License-Identifier: MIT
#include "renderer_vk.h"

#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <vulkan/vulkan_android.h>

#include <algorithm>
#include <cstring>

#include "native_log.h"

namespace fsr_android {

#define VKCHECK(expr, what)                                              \
  do {                                                                   \
    VkResult _r = (expr);                                                \
    if (_r != VK_SUCCESS) {                                              \
      err = std::string(what) + " failed (VkResult " + std::to_string(_r) + ")"; \
      return false;                                                      \
    }                                                                    \
  } while (0)

static VkInstance make_instance(std::string& err) {
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "FIFA STREET Recompiled";
  app.apiVersion = VK_API_VERSION_1_1;
  const char* exts[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ci.pApplicationInfo = &app;
  ci.enabledExtensionCount = 2;
  ci.ppEnabledExtensionNames = exts;
  VkInstance inst = VK_NULL_HANDLE;
  VkResult r = vkCreateInstance(&ci, nullptr, &inst);
  if (r != VK_SUCCESS) {
    err = "vkCreateInstance failed (" + std::to_string(r) + ")";
    return VK_NULL_HANDLE;
  }
  return inst;
}

bool VkRenderer::query(GpuInfo& out, std::string& err) {
  VkInstance inst = make_instance(err);
  if (!inst) return false;
  uint32_t n = 0;
  vkEnumeratePhysicalDevices(inst, &n, nullptr);
  if (!n) { vkDestroyInstance(inst, nullptr); err = "no Vulkan device"; return false; }
  std::vector<VkPhysicalDevice> devs(n);
  vkEnumeratePhysicalDevices(inst, &n, devs.data());
  VkPhysicalDeviceProperties p;
  vkGetPhysicalDeviceProperties(devs[0], &p);
  out.name = p.deviceName;
  out.api = p.apiVersion;
  out.driver = p.driverVersion;
  out.vendor = p.vendorID;
  out.device = p.deviceID;
  VkFormatProperties fp;
  vkGetPhysicalDeviceFormatProperties(devs[0], VK_FORMAT_R8G8B8A8_UNORM, &fp);
  out.blit_linear = fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
  vkDestroyInstance(inst, nullptr);
  return true;
}

VkRenderer::~VkRenderer() { shutdown(); }

uint32_t VkRenderer::find_memory(uint32_t bits, VkMemoryPropertyFlags flags) const {
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(phys_, &mp);
  for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
    if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags) return i;
  return UINT32_MAX;
}

bool VkRenderer::init(ANativeWindow* window, std::string& err) {
  shutdown();
  window_ = window;
  instance_ = make_instance(err);
  if (!instance_) return false;
  VkAndroidSurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
  sci.window = window;
  VKCHECK(vkCreateAndroidSurfaceKHR(instance_, &sci, nullptr, &surface_), "vkCreateAndroidSurfaceKHR");

  uint32_t n = 0;
  vkEnumeratePhysicalDevices(instance_, &n, nullptr);
  if (!n) { err = "no Vulkan physical device"; return false; }
  std::vector<VkPhysicalDevice> devs(n);
  vkEnumeratePhysicalDevices(instance_, &n, devs.data());
  for (auto d : devs) {
    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qp(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, qp.data());
    for (uint32_t i = 0; i < qn; i++) {
      VkBool32 present = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface_, &present);
      if ((qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) { phys_ = d; qfamily_ = i; break; }
    }
    if (phys_) break;
  }
  if (!phys_) { err = "no Vulkan queue with graphics+present support"; return false; }
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(phys_, &props);
  info_.name = props.deviceName;
  info_.api = props.apiVersion;
  info_.driver = props.driverVersion;
  info_.vendor = props.vendorID;
  info_.device = props.deviceID;
  VkFormatProperties fp;
  vkGetPhysicalDeviceFormatProperties(phys_, VK_FORMAT_R8G8B8A8_UNORM, &fp);
  info_.blit_linear = fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
  if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
    err = "GPU cannot blit from RGBA8 images";
    return false;
  }

  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex = qfamily_;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;
  const char* dext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = 1;
  dci.ppEnabledExtensionNames = dext;
  VKCHECK(vkCreateDevice(phys_, &dci, nullptr, &device_), "vkCreateDevice");
  vkGetDeviceQueue(device_, qfamily_, 0, &queue_);

  VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pci.queueFamilyIndex = qfamily_;
  VKCHECK(vkCreateCommandPool(device_, &pci, nullptr, &pool_), "vkCreateCommandPool");
  for (auto& f : frames_) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VKCHECK(vkAllocateCommandBuffers(device_, &ai, &f.cmd), "vkAllocateCommandBuffers");
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VKCHECK(vkCreateFence(device_, &fci, nullptr, &f.fence), "vkCreateFence");
    VkSemaphoreCreateInfo sci2{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VKCHECK(vkCreateSemaphore(device_, &sci2, nullptr, &f.acquired), "vkCreateSemaphore");
    VKCHECK(vkCreateSemaphore(device_, &sci2, nullptr, &f.rendered), "vkCreateSemaphore");
  }

  uint32_t pmn = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(phys_, surface_, &pmn, nullptr);
  std::vector<VkPresentModeKHR> pms(pmn);
  vkGetPhysicalDeviceSurfacePresentModesKHR(phys_, surface_, &pmn, pms.data());
  for (auto m : pms) {
    if (m == VK_PRESENT_MODE_MAILBOX_KHR) info_.mailbox = true;
    if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) info_.immediate = true;
  }
  if (!create_swapchain(true, err)) return false;
  logf("[vk] %s, Vulkan %u.%u.%u, swapchain %ux%u, linear blit %s", info_.name.c_str(), VK_VERSION_MAJOR(info_.api),
       VK_VERSION_MINOR(info_.api), VK_VERSION_PATCH(info_.api), sc_extent_.width, sc_extent_.height,
       info_.blit_linear ? "yes" : "no");
  return true;
}

bool VkRenderer::create_swapchain(bool vsync, std::string& err) {
  VkSurfaceCapabilitiesKHR caps;
  VKCHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_, surface_, &caps), "surface caps");
  uint32_t fn = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, surface_, &fn, nullptr);
  std::vector<VkSurfaceFormatKHR> fmts(fn);
  vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, surface_, &fn, fmts.data());
  VkSurfaceFormatKHR chosen = fmts.empty() ? VkSurfaceFormatKHR{VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} : fmts[0];
  for (auto& f : fmts)
    if (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) { chosen = f; break; }
  VkFormatProperties fp;
  vkGetPhysicalDeviceFormatProperties(phys_, chosen.format, &fp);
  if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
    err = "swapchain format does not support blit destination";
    return false;
  }
  if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
    err = "swapchain images cannot be transfer destinations on this device";
    return false;
  }
  VkExtent2D ext = caps.currentExtent;
  if (ext.width == UINT32_MAX) {
    ext.width = uint32_t(ANativeWindow_getWidth(window_));
    ext.height = uint32_t(ANativeWindow_getHeight(window_));
  }
  VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
  if (!vsync) mode = info_.mailbox ? VK_PRESENT_MODE_MAILBOX_KHR : info_.immediate ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR;
  uint32_t count = std::max(caps.minImageCount, 3u);
  if (caps.maxImageCount) count = std::min(count, caps.maxImageCount);
  VkCompositeAlphaFlagBitsKHR alpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                                          ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                                          : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
  VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  ci.surface = surface_;
  ci.minImageCount = count;
  ci.imageFormat = chosen.format;
  ci.imageColorSpace = chosen.colorSpace;
  ci.imageExtent = ext;
  ci.imageArrayLayers = 1;
  ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR & caps.supportedTransforms ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                                                                                     : caps.currentTransform;
  ci.compositeAlpha = alpha;
  ci.presentMode = mode;
  ci.clipped = VK_TRUE;
  ci.oldSwapchain = swapchain_;
  VkSwapchainKHR sc = VK_NULL_HANDLE;
  VKCHECK(vkCreateSwapchainKHR(device_, &ci, nullptr, &sc), "vkCreateSwapchainKHR");
  if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
  swapchain_ = sc;
  sc_format_ = chosen.format;
  sc_extent_ = ext;
  sc_vsync_ = vsync;
  uint32_t in = 0;
  vkGetSwapchainImagesKHR(device_, swapchain_, &in, nullptr);
  sc_images_.resize(in);
  vkGetSwapchainImagesKHR(device_, swapchain_, &in, sc_images_.data());
  return true;
}

void VkRenderer::destroy_swapchain() {
  if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
  swapchain_ = VK_NULL_HANDLE;
  sc_images_.clear();
}

void VkRenderer::destroy_guest_image() {
  if (guest_) vkDestroyImage(device_, guest_, nullptr);
  if (guest_mem_) vkFreeMemory(device_, guest_mem_, nullptr);
  guest_ = VK_NULL_HANDLE;
  guest_mem_ = VK_NULL_HANDLE;
  for (auto& f : frames_) {
    if (f.staging_ptr) vkUnmapMemory(device_, f.staging_mem);
    if (f.staging) vkDestroyBuffer(device_, f.staging, nullptr);
    if (f.staging_mem) vkFreeMemory(device_, f.staging_mem, nullptr);
    f.staging = VK_NULL_HANDLE;
    f.staging_mem = VK_NULL_HANDLE;
    f.staging_ptr = nullptr;
    f.staging_size = 0;
  }
  guest_w_ = guest_h_ = 0;
  guest_initialized_ = false;
}

bool VkRenderer::ensure_guest_image(uint32_t w, uint32_t h, std::string& err) {
  if (guest_ && w == guest_w_ && h == guest_h_) return true;
  vkDeviceWaitIdle(device_);
  destroy_guest_image();
  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = VK_FORMAT_R8G8B8A8_UNORM;
  ici.extent = {w, h, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VKCHECK(vkCreateImage(device_, &ici, nullptr, &guest_), "vkCreateImage");
  VkMemoryRequirements mr;
  vkGetImageMemoryRequirements(device_, guest_, &mr);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = mr.size;
  mai.memoryTypeIndex = find_memory(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (mai.memoryTypeIndex == UINT32_MAX) { err = "no device-local memory"; return false; }
  VKCHECK(vkAllocateMemory(device_, &mai, nullptr, &guest_mem_), "vkAllocateMemory(image)");
  VKCHECK(vkBindImageMemory(device_, guest_, guest_mem_, 0), "vkBindImageMemory");
  for (auto& f : frames_) {
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = VkDeviceSize(w) * h * 4;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VKCHECK(vkCreateBuffer(device_, &bci, nullptr, &f.staging), "vkCreateBuffer");
    vkGetBufferMemoryRequirements(device_, f.staging, &mr);
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = find_memory(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mai.memoryTypeIndex == UINT32_MAX) { err = "no host-visible memory"; return false; }
    VKCHECK(vkAllocateMemory(device_, &mai, nullptr, &f.staging_mem), "vkAllocateMemory(staging)");
    VKCHECK(vkBindBufferMemory(device_, f.staging, f.staging_mem, 0), "vkBindBufferMemory");
    VKCHECK(vkMapMemory(device_, f.staging_mem, 0, bci.size, 0, &f.staging_ptr), "vkMapMemory");
    f.staging_size = bci.size;
  }
  guest_w_ = w;
  guest_h_ = h;
  guest_initialized_ = false;
  return true;
}

static void barrier(VkCommandBuffer cmd, VkImage img, VkImageLayout from, VkImageLayout to, VkAccessFlags src_access,
                    VkAccessFlags dst_access, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.oldLayout = from;
  b.newLayout = to;
  b.srcAccessMask = src_access;
  b.dstAccessMask = dst_access;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = img;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

bool VkRenderer::present(const uint8_t* rgba, uint32_t w, uint32_t h, const RenderSettings& s, std::string& err) {
  if (!device_ || !swapchain_) { err = "renderer not ready"; return false; }
  if (s.vsync != sc_vsync_) {
    vkDeviceWaitIdle(device_);
    if (!create_swapchain(s.vsync, err)) return false;
  }
  if (!ensure_guest_image(w, h, err)) return false;
  Frame& f = frames_[frame_index_];
  frame_index_ = (frame_index_ + 1) % kFrames;
  vkWaitForFences(device_, 1, &f.fence, VK_TRUE, UINT64_MAX);

  uint32_t idx = 0;
  VkResult r = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, f.acquired, VK_NULL_HANDLE, &idx);
  if (r == VK_ERROR_OUT_OF_DATE_KHR) {
    vkDeviceWaitIdle(device_);
    return create_swapchain(s.vsync, err);
  }
  if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) { err = "vkAcquireNextImageKHR " + std::to_string(r); return false; }
  vkResetFences(device_, 1, &f.fence);
  std::memcpy(f.staging_ptr, rgba, size_t(w) * h * 4);

  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkResetCommandBuffer(f.cmd, 0);
  vkBeginCommandBuffer(f.cmd, &bi);
  barrier(f.cmd, guest_, guest_initialized_ ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
  VkBufferImageCopy copy{};
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.imageExtent = {w, h, 1};
  vkCmdCopyBufferToImage(f.cmd, f.staging, guest_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
  barrier(f.cmd, guest_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT);
  guest_initialized_ = true;

  VkImage dst = sc_images_[idx];
  barrier(f.cmd, dst, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
  VkClearColorValue black{};
  black.float32[3] = 1.0f;
  VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdClearColorImage(f.cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
  // Destination rectangle: letterboxed 16:9 or stretched.
  int32_t dx0 = 0, dy0 = 0, dx1 = int32_t(sc_extent_.width), dy1 = int32_t(sc_extent_.height);
  if (s.aspect == 0) {
    double target = 16.0 / 9.0, screen = double(sc_extent_.width) / sc_extent_.height;
    if (screen > target) {
      int32_t vw = int32_t(sc_extent_.height * target);
      dx0 = (int32_t(sc_extent_.width) - vw) / 2;
      dx1 = dx0 + vw;
    } else {
      int32_t vh = int32_t(sc_extent_.width / target);
      dy0 = (int32_t(sc_extent_.height) - vh) / 2;
      dy1 = dy0 + vh;
    }
  }
  VkImageBlit blit{};
  blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.srcOffsets[1] = {int32_t(w), int32_t(h), 1};
  blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.dstOffsets[0] = {dx0, dy0, 0};
  blit.dstOffsets[1] = {dx1, dy1, 1};
  VkFilter filter = (s.linear && info_.blit_linear) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  vkCmdBlitImage(f.cmd, guest_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                 filter);
  barrier(f.cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_WRITE_BIT, 0,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
  vkEndCommandBuffer(f.cmd);

  VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.waitSemaphoreCount = 1;
  si.pWaitSemaphores = &f.acquired;
  si.pWaitDstStageMask = &wait_stage;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &f.cmd;
  si.signalSemaphoreCount = 1;
  si.pSignalSemaphores = &f.rendered;
  VKCHECK(vkQueueSubmit(queue_, 1, &si, f.fence), "vkQueueSubmit");
  VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &f.rendered;
  pi.swapchainCount = 1;
  pi.pSwapchains = &swapchain_;
  pi.pImageIndices = &idx;
  r = vkQueuePresentKHR(queue_, &pi);
  if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
    vkDeviceWaitIdle(device_);
    return create_swapchain(s.vsync, err);
  }
  if (r != VK_SUCCESS) { err = "vkQueuePresentKHR " + std::to_string(r); return false; }
  return true;
}

void VkRenderer::shutdown() {
  if (device_) {
    vkDeviceWaitIdle(device_);
    destroy_guest_image();
    for (auto& f : frames_) {
      if (f.fence) vkDestroyFence(device_, f.fence, nullptr);
      if (f.acquired) vkDestroySemaphore(device_, f.acquired, nullptr);
      if (f.rendered) vkDestroySemaphore(device_, f.rendered, nullptr);
      f = Frame{};
    }
    if (pool_) vkDestroyCommandPool(device_, pool_, nullptr);
    pool_ = VK_NULL_HANDLE;
    destroy_swapchain();
    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
  }
  if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
  surface_ = VK_NULL_HANDLE;
  if (instance_) vkDestroyInstance(instance_, nullptr);
  instance_ = VK_NULL_HANDLE;
  phys_ = VK_NULL_HANDLE;
}

}  // namespace fsr_android
