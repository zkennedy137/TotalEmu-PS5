#include "vk_present.hpp"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

// RADV's single ICD entry point (the driver is linked into the title).
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance, const char*);
// PS5_Mesa's WSI: show each picture for rate+1 refreshes (used to halve 120->60).
extern "C" int wsi_videoout_set_flip_rate(int rate);

namespace vkp {
namespace {

// ---- function pointers, loaded from the ICD ----
#define VK_FUNCS_GLOBAL(X) \
    X(vkCreateInstance) X(vkEnumerateInstanceExtensionProperties)
#define VK_FUNCS_INSTANCE(X) \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkGetPhysicalDeviceDisplayPropertiesKHR) X(vkGetDisplayModePropertiesKHR) \
    X(vkCreateDisplayPlaneSurfaceKHR) X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR) X(vkGetPhysicalDeviceSurfaceSupportKHR) \
    X(vkDestroySurfaceKHR) X(vkCreateDevice) X(vkGetDeviceProcAddr)
#define VK_FUNCS_DEVICE(X) \
    X(vkGetDeviceQueue) X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) \
    X(vkAcquireNextImageKHR) X(vkQueuePresentKHR) X(vkCreateImage) X(vkDestroyImage) \
    X(vkGetImageMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) X(vkBindImageMemory) \
    X(vkMapMemory) X(vkUnmapMemory) X(vkGetImageSubresourceLayout) X(vkCreateCommandPool) \
    X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) \
    X(vkCmdPipelineBarrier) X(vkCmdBlitImage) X(vkQueueSubmit) X(vkQueueWaitIdle) X(vkDeviceWaitIdle) \
    X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) \
    X(vkResetFences) X(vkResetCommandPool) X(vkDestroyDevice)

#define DECLARE(fn) PFN_##fn fn = nullptr;
VK_FUNCS_GLOBAL(DECLARE)
VK_FUNCS_INSTANCE(DECLARE)
VK_FUNCS_DEVICE(DECLARE)
#undef DECLARE

PFN_vkGetInstanceProcAddr gipa = nullptr;

VkInstance instance = VK_NULL_HANDLE;
VkPhysicalDevice gpu = VK_NULL_HANDLE;
VkDevice device = VK_NULL_HANDLE;
VkQueue queue = VK_NULL_HANDLE;
uint32_t queueFamily = 0;
VkSurfaceKHR surface = VK_NULL_HANDLE;
VkSwapchainKHR swapchain = VK_NULL_HANDLE;
VkFormat swapFormat = VK_FORMAT_B8G8R8A8_UNORM;
VkExtent2D extent{};
std::vector<VkImage> swapImages;

// One upload image (host-visible, linear) reused each frame; grown as needed.
VkImage upImage = VK_NULL_HANDLE;
VkDeviceMemory upMem = VK_NULL_HANDLE;
void* upMapped = nullptr;
int upW = 0, upH = 0;
VkDeviceSize upRowPitch = 0;
VkImageLayout upLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

VkCommandPool pool = VK_NULL_HANDLE;
VkCommandBuffer cmd = VK_NULL_HANDLE;
VkSemaphore acquireSem = VK_NULL_HANDLE, drawSem = VK_NULL_HANDLE;
VkFence fence = VK_NULL_HANDLE;
char info[160] = "Vulkan";
void (*g_sink)(const char*) = nullptr;
void step(const char* m) { if (g_sink) g_sink(m); }

bool loadGlobal() {
    gipa = (PFN_vkGetInstanceProcAddr)vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, "vkGetInstanceProcAddr");
    if (!gipa) gipa = (PFN_vkGetInstanceProcAddr)&vk_icdGetInstanceProcAddr;
#define LOAD(fn) fn = (PFN_##fn)gipa(VK_NULL_HANDLE, #fn); if (!fn) return false;
    VK_FUNCS_GLOBAL(LOAD)
#undef LOAD
    return true;
}
bool loadInstance() {
#define LOAD(fn) fn = (PFN_##fn)gipa(instance, #fn); if (!fn) return false;
    VK_FUNCS_INSTANCE(LOAD)
#undef LOAD
    return true;
}
bool loadDevice() {
#define LOAD(fn) fn = (PFN_##fn)vkGetDeviceProcAddr(device, #fn); if (!fn) return false;
    VK_FUNCS_DEVICE(LOAD)
#undef LOAD
    return true;
}

bool fail(char* e, int n, const char* what, VkResult r = VK_SUCCESS) {
    if (r != VK_SUCCESS) std::snprintf(e, n, "%s (VkResult %d)", what, (int)r);
    else std::snprintf(e, n, "%s", what);
    return false;
}

uint32_t memType(uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(gpu, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    return 0;
}

void barrier(VkImage img, VkImageLayout from, VkImageLayout to, VkAccessFlags srcA, VkAccessFlags dstA,
             VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = from;
    b.newLayout = to;
    b.srcAccessMask = srcA;
    b.dstAccessMask = dstA;
    b.image = img;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
}

bool ensureUpload(int w, int h, char* e, int n) {
    if (upImage && w == upW && h == upH) return true;
    if (upImage) { vkDestroyImage(device, upImage, nullptr); upImage = VK_NULL_HANDLE; }
    if (upMem) { vkUnmapMemory(device, upMem); vkFreeMemory(device, upMem, nullptr); upMem = VK_NULL_HANDLE; }

    VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ic.imageType = VK_IMAGE_TYPE_2D;
    ic.format = VK_FORMAT_B8G8R8A8_UNORM;  // our ARGB8888 bytes are B,G,R,A little-endian
    ic.extent = {(uint32_t)w, (uint32_t)h, 1};
    ic.mipLevels = ic.arrayLayers = 1;
    ic.samples = VK_SAMPLE_COUNT_1_BIT;
    ic.tiling = VK_IMAGE_TILING_LINEAR;
    ic.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ic.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    VkResult r = vkCreateImage(device, &ic, nullptr, &upImage);
    if (r != VK_SUCCESS) return fail(e, n, "vkCreateImage(upload)", r);

    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(device, upImage, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = memType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    r = vkAllocateMemory(device, &ai, nullptr, &upMem);
    if (r != VK_SUCCESS) return fail(e, n, "vkAllocateMemory(upload)", r);
    vkBindImageMemory(device, upImage, upMem, 0);
    vkMapMemory(device, upMem, 0, mr.size, 0, &upMapped);

    VkImageSubresource sub{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
    VkSubresourceLayout sl{};
    vkGetImageSubresourceLayout(device, upImage, &sub, &sl);
    upRowPitch = sl.rowPitch;
    upW = w;
    upH = h;
    upLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    return true;
}

}  // namespace

bool open(char* error, int errorSize, int* widthOut, int* heightOut) {
    step("vk: loadGlobal");
    if (!loadGlobal()) return fail(error, errorSize, "the RADV driver has no entry points");

    const char* exts[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_DISPLAY_EXTENSION_NAME};
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "PS Launcher";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ii.pApplicationInfo = &app;
    ii.enabledExtensionCount = 2;
    ii.ppEnabledExtensionNames = exts;
    step("vk: createInstance");
    VkResult r = vkCreateInstance(&ii, nullptr, &instance);
    if (r != VK_SUCCESS) return fail(error, errorSize, "vkCreateInstance", r);
    if (!loadInstance()) return fail(error, errorSize, "instance functions missing");

    step("vk: enumerate GPUs");
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance, &n, nullptr);
    if (!n) return fail(error, errorSize, "no Vulkan device");
    std::vector<VkPhysicalDevice> gpus(n);
    vkEnumeratePhysicalDevices(instance, &n, gpus.data());
    gpu = gpus[0];
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(gpu, &props);

    // Display plane surface: biggest mode, highest refresh (PSSwanStation's choice).
    step("vk: display properties");
    uint32_t dc = 0;
    vkGetPhysicalDeviceDisplayPropertiesKHR(gpu, &dc, nullptr);
    if (!dc) return fail(error, errorSize, "no display");
    std::vector<VkDisplayPropertiesKHR> displays(dc);
    vkGetPhysicalDeviceDisplayPropertiesKHR(gpu, &dc, displays.data());
    uint32_t mc = 0;
    vkGetDisplayModePropertiesKHR(gpu, displays[0].display, &mc, nullptr);
    if (!mc) return fail(error, errorSize, "display has no mode");
    std::vector<VkDisplayModePropertiesKHR> modes(mc);
    vkGetDisplayModePropertiesKHR(gpu, displays[0].display, &mc, modes.data());
    auto score = [](const VkDisplayModePropertiesKHR& m) {
        return (long long)m.parameters.visibleRegion.width * m.parameters.visibleRegion.height * 1000000 +
               m.parameters.refreshRate;
    };
    const VkDisplayModePropertiesKHR* best = &modes[0];
    for (auto& m : modes) if (score(m) > score(*best)) best = &m;
    extent = best->parameters.visibleRegion;
    float refresh = best->parameters.refreshRate / 1000.f;
    if (refresh > 100.f) wsi_videoout_set_flip_rate(1);  // keep 60 on a 120 Hz panel

    VkDisplaySurfaceCreateInfoKHR si{VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR};
    si.displayMode = best->displayMode;
    si.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    si.globalAlpha = 1.0f;
    si.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
    si.imageExtent = extent;
    step("vk: create surface");
    r = vkCreateDisplayPlaneSurfaceKHR(instance, &si, nullptr, &surface);
    if (r != VK_SUCCESS) return fail(error, errorSize, "vkCreateDisplayPlaneSurfaceKHR", r);

    // Device with one graphics+present queue.
    uint32_t qc = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qc, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qc);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qc, qf.data());
    bool found = false;
    for (uint32_t i = 0; i < qc; ++i) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &present);
        if ((qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) { queueFamily = i; found = true; break; }
    }
    if (!found) return fail(error, errorSize, "no graphics+present queue");

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = queueFamily;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;
    const char* devExt[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = 1;
    di.ppEnabledExtensionNames = devExt;
    step("vk: create device");
    r = vkCreateDevice(gpu, &di, nullptr, &device);
    if (r != VK_SUCCESS) return fail(error, errorSize, "vkCreateDevice", r);
    if (!loadDevice()) return fail(error, errorSize, "device functions missing");
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    // Swapchain: FIFO, B8G8R8A8 if offered.
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps);
    if (caps.currentExtent.width != 0xffffffffu) extent = caps.currentExtent;
    uint32_t fc = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &fc, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fc);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &fc, formats.data());
    VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM,
                                                                     VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                                                : formats[0];
    for (auto& f : formats) if (f.format == VK_FORMAT_B8G8R8A8_UNORM) { chosen = f; break; }
    swapFormat = chosen.format;
    uint32_t imgCount = std::max<uint32_t>(3, caps.minImageCount);
    if (caps.maxImageCount) imgCount = std::min(imgCount, caps.maxImageCount);

    VkSwapchainCreateInfoKHR sc{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sc.surface = surface;
    sc.minImageCount = imgCount;
    sc.imageFormat = chosen.format;
    sc.imageColorSpace = chosen.colorSpace;
    sc.imageExtent = extent;
    sc.imageArrayLayers = 1;
    sc.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sc.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                          ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;
    sc.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                            ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    sc.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sc.clipped = VK_TRUE;
    step("vk: create swapchain");
    r = vkCreateSwapchainKHR(device, &sc, nullptr, &swapchain);
    if (r != VK_SUCCESS) return fail(error, errorSize, "vkCreateSwapchainKHR", r);
    uint32_t ic = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &ic, nullptr);
    swapImages.resize(ic);
    vkGetSwapchainImagesKHR(device, swapchain, &ic, swapImages.data());

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = queueFamily;
    vkCreateCommandPool(device, &pci, nullptr, &pool);
    VkCommandBufferAllocateInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbi.commandPool = pool;
    cbi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbi.commandBufferCount = 1;
    vkAllocateCommandBuffers(device, &cbi, &cmd);
    VkSemaphoreCreateInfo semi{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vkCreateSemaphore(device, &semi, nullptr, &acquireSem);
    vkCreateSemaphore(device, &semi, nullptr, &drawSem);
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(device, &fi, nullptr, &fence);

    step("vk: ready");
    std::snprintf(info, sizeof info, "RADV %ux%u @ %.2fHz", extent.width, extent.height, refresh);
    step("vk: described");
    if (widthOut) *widthOut = (int)extent.width;
    if (heightOut) *heightOut = (int)extent.height;
    step("vk: returning");
    return true;
}

bool present(const uint32_t* pixels, int width, int height, int pitch) {
    if (!device || !swapchain) return false;
    static int frame = 0;
    const bool trace = frame < 2;  // log the first two frames only
    if (trace) step("present: enter");
    char e[160];
    if (!ensureUpload(width, height, e, sizeof e)) { if (trace) step(e); return false; }

    // Copy the CPU frame into the mapped linear image, respecting its row pitch.
    for (int y = 0; y < height; ++y)
        std::memcpy((uint8_t*)upMapped + (VkDeviceSize)y * upRowPitch,
                    (const uint8_t*)pixels + (size_t)y * pitch, (size_t)width * 4);
    if (trace) step("present: uploaded");

    uint32_t idx = 0;
    VkResult r = vkAcquireNextImageKHR(device, swapchain, 2000000000ull, acquireSem, VK_NULL_HANDLE, &idx);
    if (trace) step("present: acquired");
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) return false;
    if (r != VK_SUCCESS) { if (trace) step("present: acquire failed"); return false; }

    vkResetCommandPool(device, pool, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    barrier(upImage, upLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
    barrier(swapImages[idx], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {width, height, 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[1] = {(int)extent.width, (int)extent.height, 1};
    vkCmdBlitImage(cmd, upImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapImages[idx],
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

    // Leave the upload image in GENERAL so next frame's host write + barrier is valid.
    barrier(upImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_HOST_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT);
    barrier(swapImages[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    vkEndCommandBuffer(cmd);
    upLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &acquireSem;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &drawSem;
    vkResetFences(device, 1, &fence);
    vkQueueSubmit(queue, 1, &submit, fence);
    if (trace) step("present: submitted");

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &drawSem;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain;
    pi.pImageIndices = &idx;
    r = vkQueuePresentKHR(queue, &pi);
    if (trace) step("present: presented");
    // Finite wait: if the GPU wedges, don't hang the app (the OS kills a hung title).
    if (vkWaitForFences(device, 1, &fence, VK_TRUE, 2000000000ull) != VK_SUCCESS && trace)
        step("present: fence timeout");
    if (trace) step("present: done");
    ++frame;
    return r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR;
}

void close() {
    if (device) vkDeviceWaitIdle(device);
    if (upMem) { vkUnmapMemory(device, upMem); vkFreeMemory(device, upMem, nullptr); upMem = VK_NULL_HANDLE; }
    if (upImage) { vkDestroyImage(device, upImage, nullptr); upImage = VK_NULL_HANDLE; }
    if (fence) vkDestroyFence(device, fence, nullptr);
    if (acquireSem) vkDestroySemaphore(device, acquireSem, nullptr);
    if (drawSem) vkDestroySemaphore(device, drawSem, nullptr);
    if (pool) vkDestroyCommandPool(device, pool, nullptr);
    if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
    if (device) vkDestroyDevice(device, nullptr);
    if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
    if (instance) vkDestroyInstance(instance, nullptr);
    device = VK_NULL_HANDLE;
    swapchain = VK_NULL_HANDLE;
}

const char* describe() { return info; }
void setLogSink(void (*sink)(const char*)) { g_sink = sink; }

}  // namespace vkp
