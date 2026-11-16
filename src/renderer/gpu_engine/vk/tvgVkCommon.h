/*
 * Copyright (c) 2026 ThorVG project. All rights reserved.

 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _TVG_VK_COMMON_H_
#define _TVG_VK_COMMON_H_

// The engine never links the Vulkan loader. Every entry point is resolved through the
// vkGetInstanceProcAddr given by the application, device functions without the loader trampoline.
#ifndef VK_NO_PROTOTYPES
    #define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include "tvgGpuCommon.h"

#define VK_MAX_FRAMES 2
#define VK_IDLE_FRAMES 120  // cached render targets unused for this many frames are released

#define VK_INSTANCE_FUNCTIONS(X) \
    X(vkGetDeviceProcAddr) \
    X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkGetPhysicalDeviceFormatProperties) \
    X(vkGetPhysicalDeviceSurfaceSupportKHR) \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)

#define VK_DEVICE_FUNCTIONS(X) \
    X(vkAllocateCommandBuffers) \
    X(vkAllocateDescriptorSets) \
    X(vkAllocateMemory) \
    X(vkBeginCommandBuffer) \
    X(vkBindBufferMemory) \
    X(vkBindImageMemory) \
    X(vkCmdBeginRenderPass) \
    X(vkCmdBindDescriptorSets) \
    X(vkCmdBindIndexBuffer) \
    X(vkCmdBindPipeline) \
    X(vkCmdBindVertexBuffers) \
    X(vkCmdBlitImage) \
    X(vkCmdClearColorImage) \
    X(vkCmdCopyBufferToImage) \
    X(vkCmdCopyImage) \
    X(vkCmdDispatch) \
    X(vkCmdDraw) \
    X(vkCmdDrawIndexed) \
    X(vkCmdEndRenderPass) \
    X(vkCmdPipelineBarrier) \
    X(vkCmdPushConstants) \
    X(vkCmdSetScissor) \
    X(vkCmdSetStencilReference) \
    X(vkCmdSetViewport) \
    X(vkCreateBuffer) \
    X(vkCreateCommandPool) \
    X(vkCreateComputePipelines) \
    X(vkCreateDescriptorPool) \
    X(vkCreateDescriptorSetLayout) \
    X(vkCreateFence) \
    X(vkCreateFramebuffer) \
    X(vkCreateGraphicsPipelines) \
    X(vkCreateImage) \
    X(vkCreateImageView) \
    X(vkCreatePipelineCache) \
    X(vkCreatePipelineLayout) \
    X(vkCreateRenderPass) \
    X(vkCreateSampler) \
    X(vkCreateSemaphore) \
    X(vkCreateShaderModule) \
    X(vkDestroyBuffer) \
    X(vkDestroyCommandPool) \
    X(vkDestroyDescriptorPool) \
    X(vkDestroyDescriptorSetLayout) \
    X(vkDestroyFence) \
    X(vkDestroyFramebuffer) \
    X(vkDestroyImage) \
    X(vkDestroyImageView) \
    X(vkDestroyPipeline) \
    X(vkDestroyPipelineCache) \
    X(vkDestroyPipelineLayout) \
    X(vkDestroyRenderPass) \
    X(vkDestroySampler) \
    X(vkDestroySemaphore) \
    X(vkDestroyShaderModule) \
    X(vkEndCommandBuffer) \
    X(vkFlushMappedMemoryRanges) \
    X(vkFreeDescriptorSets) \
    X(vkFreeMemory) \
    X(vkGetBufferMemoryRequirements) \
    X(vkGetImageMemoryRequirements) \
    X(vkMapMemory) \
    X(vkQueueSubmit) \
    X(vkQueueWaitIdle) \
    X(vkResetCommandPool) \
    X(vkResetDescriptorPool) \
    X(vkResetFences) \
    X(vkUpdateDescriptorSets) \
    X(vkWaitForFences)

#define VK_SWAPCHAIN_FUNCTIONS(X) \
    X(vkAcquireNextImageKHR) \
    X(vkCreateSwapchainKHR) \
    X(vkDestroySwapchainKHR) \
    X(vkGetSwapchainImagesKHR) \
    X(vkQueuePresentKHR)

struct VkContext;

struct VkGpuBuffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint8_t* mapped = nullptr;
    VkDeviceSize size = 0;
    bool coherent = true;
};

struct VkTexture
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t levels = 1;

    bool valid() const { return image != VK_NULL_HANDLE; }
};

// A descriptor set remembers its pool so it can be returned individually.
struct VkDescriptor
{
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
};

// Resources released while the GPU may still use them. They are destroyed when the
// frame slot holding them is reused, after its fence has signaled.
struct VkGarbage
{
    Array<VkBuffer> buffers;
    Array<VkImage> images;
    Array<VkImageView> views;
    Array<VkDeviceMemory> memories;
    Array<VkFramebuffer> framebuffers;
    Array<VkDescriptor> descriptors;

    void flush(VkContext& context);
};

struct VkFrame
{
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands[2]{};  // the rendering, and the presentation that waits for the swapchain image
    uint32_t recording = 0;         // index of the command buffer being recorded
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;   // swapchain image acquisition
    VkDescriptorPool transient = VK_NULL_HANDLE;  // descriptor sets living for one frame
    VkGpuBuffer staging;
    VkDeviceSize stagingUsed = 0;
    uint32_t stagingIdle = 0;  // consecutive frames that used a small part of the staging buffer
    VkGarbage garbage;
    bool submitted = false;
};

// Copies from the frame staging buffer into the gradient ramp atlas, recorded in one batch.
struct VkRampAtlas
{
    static constexpr uint32_t WIDTH = 512;

    VkTexture texture;
    VkDescriptor sets[3];  // FillSpread::Pad, Reflect, Repeat
    Array<uint32_t> freeRows;
    uint32_t rows = 0;  // allocated row high-water mark

    struct Upload
    {
        VkBuffer buffer;
        VkBufferImageCopy region;
    };
    Array<Upload> uploads;

    uint32_t allocate(VkContext& context);
    void free(uint32_t row);
    void upload(VkContext& context, uint32_t row, const void* data);
    void flush(VkContext& context);
    void release(VkContext& context);
    VkDescriptorSet descriptor(FillSpread spread) const { return sets[(int)spread].set; }

private:
    bool grow(VkContext& context, uint32_t height);
};

struct VkContext
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;

    #define VK_DECLARE(name) PFN_##name name = nullptr;
    VK_INSTANCE_FUNCTIONS(VK_DECLARE)
    VK_DEVICE_FUNCTIONS(VK_DECLARE)
    VK_SWAPCHAIN_FUNCTIONS(VK_DECLARE)
    #undef VK_DECLARE

    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkPhysicalDeviceLimits limits{};
    VkFormat depthStencilFormat = VK_FORMAT_UNDEFINED;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;

    VkSampler samplerNearestClamp = VK_NULL_HANDLE;
    VkSampler samplerNearestRepeat = VK_NULL_HANDLE;
    VkSampler samplerLinearClamp = VK_NULL_HANDLE;
    VkSampler samplerLinearRepeat = VK_NULL_HANDLE;
    VkSampler samplerLinearMirror = VK_NULL_HANDLE;
    VkSampler samplerImage = VK_NULL_HANDLE;  // mipmapped images

    VkDescriptorSetLayout layoutUniforms = VK_NULL_HANDLE;  // set 0: view and paint dynamic uniform buffers
    VkDescriptorSetLayout layoutTexture = VK_NULL_HANDLE;   // set 1, 2: a combined image sampler
    VkDescriptorSetLayout layoutPrepare = VK_NULL_HANDLE;   // compute: storage buffer and storage image
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout prepareLayout = VK_NULL_HANDLE;

    VkRampAtlas ramps;

    bool initialize(const VkCanvas::Context& ctx);
    void release();
    bool invalid() const { return device == VK_NULL_HANDLE; }

    // frames: the command buffer of the frame being recorded is opened on demand
    VkCommandBuffer commands();
    // starts the presentation commands, so the rendering is submitted without waiting for the swapchain
    VkCommandBuffer split();
    bool submit(VkSemaphore wait = VK_NULL_HANDLE, VkSemaphore signal = VK_NULL_HANDLE);
    void wait();
    void idle();  // discards the frame being recorded and waits for the GPU
    uint32_t frameIndex() const { return current; }
    VkFrame& frame() { return frames[current]; }
    bool recording() const { return opened; }

    // staging: returns the offset of the copied data inside the returned buffer
    VkBuffer stage(const void* data, VkDeviceSize size, VkDeviceSize& offset);
    VkDescriptorSet transientSet(VkDescriptorSetLayout layout);

    // resources
    bool createBuffer(VkGpuBuffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage, bool host);
    bool createTexture(VkTexture& texture, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT, bool transient = false, uint32_t levels = 1);
    VkFramebuffer createFramebuffer(VkRenderPass renderPass, const VkImageView* views, uint32_t count, uint32_t width, uint32_t height);
    VkDescriptor allocate(VkDescriptorSetLayout layout);
    VkDescriptor allocateTexture(const VkTexture& texture, VkSampler sampler);
    void updateTexture(VkDescriptorSet set, const VkTexture& texture, VkSampler sampler);
    void flush(const VkGpuBuffer& buffer, VkDeviceSize size);

    // deferred destruction
    void release(VkGpuBuffer& buffer);
    void release(VkTexture& texture);
    void release(VkDescriptor& descriptor);
    void release(VkFramebuffer& framebuffer);
    void release(VkImageView& view);

    // records an image layout transition with a conservative dependency
    void transition(VkCommandBuffer cmd, VkTexture& texture, VkImageLayout layout);
    void transition(VkCommandBuffer cmd, VkTexture& texture0, VkImageLayout layout0, VkTexture& texture1, VkImageLayout layout1);
    // fills the mip chain from the first level, leaving the texture ready to be sampled
    void generateMipmaps(VkCommandBuffer cmd, VkTexture& texture);

private:
    VkFrame frames[VK_MAX_FRAMES];
    Array<VkDescriptorPool> pools;
    uint32_t current = 0;
    bool opened = false;

    bool load(const VkCanvas::Context& ctx);
    int32_t memoryType(uint32_t bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred);
    VkDeviceMemory allocate(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred, bool* coherent = nullptr);
    VkSampler createSampler(VkFilter filter, VkSamplerAddressMode modeU, VkSamplerAddressMode modeV, VkSamplerMipmapMode mipmap = VK_SAMPLER_MIPMAP_MODE_NEAREST);
    VkDescriptorPool createPool(bool freeable, uint32_t sets);
    VkGarbage& garbage();
    void open();
    void destroy(VkGpuBuffer& buffer);
};

#endif // _TVG_VK_COMMON_H_
