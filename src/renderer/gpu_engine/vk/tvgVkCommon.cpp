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

#include <cstring>
#include "tvgVkCommon.h"

#define VK_STAGING_RING_MIN (1u << 20)
#define VK_STAGING_SHRINK_FRAMES 120  // a staging buffer mostly unused for this many frames is released
#define VK_DESCRIPTOR_POOL_SETS 256

static VkDeviceSize _align(VkDeviceSize value, VkDeviceSize alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}


static bool _depthStencil(VkFormat format)
{
    return format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D16_UNORM_S8_UINT;
}


static void _access(VkImageLayout layout, VkPipelineStageFlags& stage, VkAccessFlags& access)
{
    switch (layout) {
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            access = VK_ACCESS_SHADER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            access = VK_ACCESS_TRANSFER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            access = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_GENERAL:
            stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            break;
        default:
            stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            access = 0;
            break;
    }
}

/************************************************************************/
/* VkGarbage                                                            */
/************************************************************************/

void VkGarbage::flush(VkContext& context)
{
    auto device = context.device;
    for (auto& p : framebuffers) context.vkDestroyFramebuffer(device, p, nullptr);
    for (auto& p : views) context.vkDestroyImageView(device, p, nullptr);
    for (auto& p : images) context.vkDestroyImage(device, p, nullptr);
    for (auto& p : buffers) context.vkDestroyBuffer(device, p, nullptr);
    for (auto& p : memories) context.vkFreeMemory(device, p, nullptr);
    for (auto& p : descriptors) context.vkFreeDescriptorSets(device, p.pool, 1, &p.set);
    for (auto& p : swapchains) context.vkDestroySwapchainKHR(device, p, nullptr);
    framebuffers.clear();
    views.clear();
    images.clear();
    buffers.clear();
    memories.clear();
    descriptors.clear();
    swapchains.clear();
}

/************************************************************************/
/* VkRampAtlas                                                          */
/************************************************************************/

bool VkRampAtlas::grow(VkContext& context, uint32_t height)
{
    VkTexture grown;
    auto usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!context.createTexture(grown, WIDTH, height, VK_FORMAT_R8G8B8A8_UNORM, usage)) return false;

    auto cmd = context.commands();
    if (texture.valid()) {
        // keep the existing rows, including the pending uploads into them
        flush(context);
        context.transition(cmd, texture, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        context.transition(cmd, grown, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageCopy region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.extent = {WIDTH, texture.height, 1};
        context.vkCmdCopyImage(cmd, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, grown.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }
    context.transition(cmd, grown, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    context.release(texture);
    for (auto& set : sets) context.release(set);
    texture = grown;
    sets[(int)FillSpread::Pad] = context.allocateTexture(texture, context.samplerLinearClamp);
    sets[(int)FillSpread::Reflect] = context.allocateTexture(texture, context.samplerLinearMirror);
    sets[(int)FillSpread::Repeat] = context.allocateTexture(texture, context.samplerLinearRepeat);
    return true;
}


uint32_t VkRampAtlas::allocate(VkContext& context)
{
    if (context.invalid()) return UINT32_MAX;
    auto row = freeRows.empty() ? rows : freeRows.last();
    if (row >= texture.height) {
        auto limit = context.limits.maxImageDimension2D;
        auto height = texture.height ? texture.height * 2 : 64;
        if (height > limit) height = limit;
        if (row >= height || !grow(context, height)) return UINT32_MAX;
    }
    if (freeRows.empty()) ++rows;
    else freeRows.pop();
    return row;
}


void VkRampAtlas::free(uint32_t row)
{
    if (row != UINT32_MAX) freeRows.push(row);
}


void VkRampAtlas::upload(VkContext& context, uint32_t row, const void* data)
{
    if (row == UINT32_MAX || context.invalid()) return;
    VkDeviceSize offset;
    auto buffer = context.stage(data, WIDTH * 4, offset);
    if (!buffer) return;
    VkBufferImageCopy region{};
    region.bufferOffset = offset;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {0, int32_t(row), 0};
    region.imageExtent = {WIDTH, 1, 1};
    uploads.push({buffer, region});
}


void VkRampAtlas::flush(VkContext& context)
{
    if (uploads.empty()) return;
    auto cmd = context.commands();
    context.transition(cmd, texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    for (uint32_t i = 0; i < uploads.count;) {
        VkBufferImageCopy regions[64];
        auto buffer = uploads[i].buffer;
        uint32_t count = 0;
        while (i < uploads.count && uploads[i].buffer == buffer && count < 64) regions[count++] = uploads[i++].region;
        context.vkCmdCopyBufferToImage(cmd, buffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, count, regions);
    }
    context.transition(cmd, texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    uploads.clear();
}


void VkRampAtlas::release(VkContext& context)
{
    uploads.clear();
    freeRows.clear();
    rows = 0;
    for (auto& set : sets) context.release(set);
    context.release(texture);
}

/************************************************************************/
/* VkContext                                                            */
/************************************************************************/

bool VkContext::load(const VkCanvas::Context& ctx)
{
    auto getInstanceProcAddr = (PFN_vkGetInstanceProcAddr)ctx.getInstanceProcAddr;
    if (!getInstanceProcAddr) return false;

    #define VK_LOAD_INSTANCE(name) name = (PFN_##name)getInstanceProcAddr(instance, #name);
    VK_INSTANCE_FUNCTIONS(VK_LOAD_INSTANCE)
    #undef VK_LOAD_INSTANCE
    if (!vkGetDeviceProcAddr || !vkGetPhysicalDeviceProperties || !vkGetPhysicalDeviceMemoryProperties || !vkGetPhysicalDeviceFormatProperties) return false;

    #define VK_LOAD_DEVICE(name) name = (PFN_##name)vkGetDeviceProcAddr(device, #name); if (!name) return false;
    VK_DEVICE_FUNCTIONS(VK_LOAD_DEVICE)
    #undef VK_LOAD_DEVICE

    // optional, available when the application enabled VK_KHR_swapchain
    #define VK_LOAD_SWAPCHAIN(name) name = (PFN_##name)vkGetDeviceProcAddr(device, #name);
    VK_SWAPCHAIN_FUNCTIONS(VK_LOAD_SWAPCHAIN)
    #undef VK_LOAD_SWAPCHAIN

    return true;
}


bool VkContext::initialize(const VkCanvas::Context& ctx)
{
    instance = (VkInstance)ctx.instance;
    physicalDevice = (VkPhysicalDevice)ctx.physicalDevice;
    device = (VkDevice)ctx.device;
    queue = (VkQueue)ctx.queue;
    queueFamily = ctx.queueFamilyIndex;

    if (!load(ctx)) {
        TVGERR("VK_ENGINE", "Failed to resolve the Vulkan entry points.");
        device = VK_NULL_HANDLE;
        return false;
    }

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    limits = properties.limits;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    // the specification guarantees one of the two packed depth stencil formats
    for (auto format : {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT}) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depthStencilFormat = format;
            break;
        }
    }

    VkPipelineCacheCreateInfo cacheInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    vkCreatePipelineCache(device, &cacheInfo, nullptr, &pipelineCache);

    samplerNearestClamp = createSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    samplerNearestRepeat = createSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    samplerLinearClamp = createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    // the ramp atlas wraps along a row only
    samplerLinearRepeat = createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    samplerLinearMirror = createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    samplerImage = createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_MIPMAP_MODE_LINEAR);

    // descriptor set layouts
    {
        VkDescriptorSetLayoutBinding bindings[2]{};
        for (uint32_t i = 0; i < 2; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = 2;
        info.pBindings = bindings;
        vkCreateDescriptorSetLayout(device, &info, nullptr, &layoutUniforms);
    }
    {
        VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = 1;
        info.pBindings = &binding;
        vkCreateDescriptorSetLayout(device, &info, nullptr, &layoutTexture);
    }
    {
        VkDescriptorSetLayoutBinding bindings[2]{
            {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = 2;
        info.pBindings = bindings;
        vkCreateDescriptorSetLayout(device, &info, nullptr, &layoutPrepare);
    }

    // pipeline layouts
    {
        VkDescriptorSetLayout sets[]{layoutUniforms, layoutTexture, layoutTexture};
        VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64};
        VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        info.setLayoutCount = 3;
        info.pSetLayouts = sets;
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges = &range;
        vkCreatePipelineLayout(device, &info, nullptr, &pipelineLayout);
    }
    {
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 20};
        VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        info.setLayoutCount = 1;
        info.pSetLayouts = &layoutPrepare;
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges = &range;
        vkCreatePipelineLayout(device, &info, nullptr, &prepareLayout);
    }

    // frames
    for (auto& frame : frames) {
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = queueFamily;
        vkCreateCommandPool(device, &poolInfo, nullptr, &frame.pool);

        VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocInfo.commandPool = frame.pool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 2;
        vkAllocateCommandBuffers(device, &allocInfo, frame.commands);

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        vkCreateFence(device, &fenceInfo, nullptr, &frame.fence);

        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.acquired);

        frame.transient = createPool(false, 64);
    }

    if (!depthStencilFormat || !pipelineLayout || !prepareLayout || !frames[VK_MAX_FRAMES - 1].transient) {
        TVGERR("VK_ENGINE", "Failed to initialize the Vulkan context.");
        release();
        return false;
    }
    return true;
}


void VkContext::release()
{
    if (!device) return;

    idle();
    ramps.release(*this);

    for (auto& frame : frames) {
        destroy(frame.staging);
        frame.garbage.flush(*this);
        if (frame.transient) vkDestroyDescriptorPool(device, frame.transient, nullptr);
        if (frame.acquired) vkDestroySemaphore(device, frame.acquired, nullptr);
        if (frame.fence) vkDestroyFence(device, frame.fence, nullptr);
        if (frame.pool) vkDestroyCommandPool(device, frame.pool, nullptr);
        frame = VkFrame();
    }
    for (auto& p : pools) vkDestroyDescriptorPool(device, p, nullptr);
    pools.clear();

    if (prepareLayout) vkDestroyPipelineLayout(device, prepareLayout, nullptr);
    if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    if (layoutPrepare) vkDestroyDescriptorSetLayout(device, layoutPrepare, nullptr);
    if (layoutTexture) vkDestroyDescriptorSetLayout(device, layoutTexture, nullptr);
    if (layoutUniforms) vkDestroyDescriptorSetLayout(device, layoutUniforms, nullptr);
    for (auto sampler : {samplerNearestClamp, samplerNearestRepeat, samplerLinearClamp, samplerLinearRepeat, samplerLinearMirror, samplerImage}) {
        if (sampler) vkDestroySampler(device, sampler, nullptr);
    }
    if (pipelineCache) vkDestroyPipelineCache(device, pipelineCache, nullptr);

    *this = VkContext();
}


void VkContext::open()
{
    if (opened) return;
    auto& f = frames[current];
    if (f.submitted) {
        vkWaitForFences(device, 1, &f.fence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &f.fence);
        f.submitted = false;
    }
    f.garbage.flush(*this);
    vkResetCommandPool(device, f.pool, 0);
    vkResetDescriptorPool(device, f.transient, 0);
    // a big upload grows the staging buffer, release it once the uploads become small again
    if (f.staging.size > VK_STAGING_RING_MIN && f.stagingUsed < f.staging.size / 4) {
        if (++f.stagingIdle > VK_STAGING_SHRINK_FRAMES) {
            f.garbage.buffers.push(f.staging.buffer);
            f.garbage.memories.push(f.staging.memory);
            f.staging = VkGpuBuffer();
            f.stagingIdle = 0;
        }
    } else f.stagingIdle = 0;
    f.stagingUsed = 0;
    f.recording = 0;

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(f.commands[0], &beginInfo);
    opened = true;
}


VkCommandBuffer VkContext::commands()
{
    open();
    return frames[current].commands[frames[current].recording];
}


VkCommandBuffer VkContext::split()
{
    open();
    auto& f = frames[current];
    if (f.recording == 0) {
        vkEndCommandBuffer(f.commands[0]);
        f.recording = 1;
        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(f.commands[1], &beginInfo);
    }
    return f.commands[1];
}


bool VkContext::submit(VkSemaphore wait, VkSemaphore signal)
{
    if (!opened) return false;
    auto& f = frames[current];
    vkEndCommandBuffer(f.commands[f.recording]);
    opened = false;

    // only the presentation batch waits for the swapchain image, the fence covers both batches
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo infos[2]{{VK_STRUCTURE_TYPE_SUBMIT_INFO}, {VK_STRUCTURE_TYPE_SUBMIT_INFO}};
    for (uint32_t i = 0; i <= f.recording; ++i) {
        infos[i].commandBufferCount = 1;
        infos[i].pCommandBuffers = &f.commands[i];
    }
    auto& last = infos[f.recording];
    last.waitSemaphoreCount = wait ? 1 : 0;
    last.pWaitSemaphores = &wait;
    last.pWaitDstStageMask = &waitStage;
    last.signalSemaphoreCount = signal ? 1 : 0;
    last.pSignalSemaphores = &signal;
    auto result = vkQueueSubmit(queue, f.recording + 1, infos, f.fence);
    f.submitted = (result == VK_SUCCESS);
    current = (current + 1) % VK_MAX_FRAMES;
    if (result != VK_SUCCESS) {
        TVGERR("VK_ENGINE", "Failed to submit the frame (%d).", (int)result);
        return false;
    }
    return true;
}


void VkContext::wait()
{
    for (auto& frame : frames) {
        if (!frame.submitted) continue;
        vkWaitForFences(device, 1, &frame.fence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &frame.fence);
        frame.submitted = false;
    }
}


void VkContext::idle()
{
    if (opened) {
        vkEndCommandBuffer(frames[current].commands[frames[current].recording]);
        opened = false;
    }
    wait();
}


VkGarbage& VkContext::garbage()
{
    // the open frame, otherwise the latest submission whose fence covers all previous work
    return opened ? frames[current].garbage : frames[(current + VK_MAX_FRAMES - 1) % VK_MAX_FRAMES].garbage;
}


VkBuffer VkContext::stage(const void* data, VkDeviceSize size, VkDeviceSize& offset)
{
    open();
    auto& f = frames[current];
    auto usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    // 16 bytes satisfy the texel size and the copy offset rules of every format in use
    offset = _align(f.stagingUsed, 16);
    if (!f.staging.buffer || offset + size > f.staging.size) {
        auto capacity = f.staging.size * 2;
        if (capacity < VK_STAGING_RING_MIN) capacity = VK_STAGING_RING_MIN;
        while (capacity < size * 2) capacity *= 2;
        release(f.staging);
        if (!createBuffer(f.staging, capacity, usage, true)) return VK_NULL_HANDLE;
        offset = 0;
    }
    memcpy(f.staging.mapped + offset, data, size);
    if (!f.staging.coherent) {
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = f.staging.memory;
        range.offset = offset & ~(limits.nonCoherentAtomSize - 1);
        range.size = VK_WHOLE_SIZE;
        vkFlushMappedMemoryRanges(device, 1, &range);
    }
    f.stagingUsed = offset + size;
    return f.staging.buffer;
}


VkDescriptorSet VkContext::transientSet(VkDescriptorSetLayout layout)
{
    open();
    VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    info.descriptorPool = frames[current].transient;
    info.descriptorSetCount = 1;
    info.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device, &info, &set) == VK_SUCCESS) return set;

    // the frame pool is exhausted, borrow a persistent set and return it with this frame
    auto descriptor = allocate(layout);
    set = descriptor.set;
    release(descriptor);
    return set;
}


int32_t VkContext::memoryType(uint32_t bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred)
{
    for (auto flags : {required | preferred, required}) {
        for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
            if ((bits & (1u << i)) && (memoryProperties.memoryTypes[i].propertyFlags & flags) == flags) return int32_t(i);
        }
    }
    return -1;
}


VkDeviceMemory VkContext::allocate(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred, bool* coherent)
{
    auto type = memoryType(requirements.memoryTypeBits, required, preferred);
    if (type < 0) return VK_NULL_HANDLE;
    VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    info.allocationSize = requirements.size;
    info.memoryTypeIndex = uint32_t(type);
    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (vkAllocateMemory(device, &info, nullptr, &memory) != VK_SUCCESS) return VK_NULL_HANDLE;
    if (coherent) *coherent = memoryProperties.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    return memory;
}


bool VkContext::createBuffer(VkGpuBuffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage, bool host)
{
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &info, nullptr, &buffer.buffer) != VK_SUCCESS) return false;

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, buffer.buffer, &requirements);

    VkMemoryPropertyFlags required = host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkMemoryPropertyFlags preferred = host ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : 0;
    // geometry and uniforms read by the GPU every frame favor device local memory mapped to the host
    if (host && (usage & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT))) {
        buffer.memory = allocate(requirements, required | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &buffer.coherent);
    }
    if (!buffer.memory) buffer.memory = allocate(requirements, required, preferred, &buffer.coherent);
    if (!buffer.memory || vkBindBufferMemory(device, buffer.buffer, buffer.memory, 0) != VK_SUCCESS) {
        destroy(buffer);
        return false;
    }
    if (host && vkMapMemory(device, buffer.memory, 0, VK_WHOLE_SIZE, 0, (void**)&buffer.mapped) != VK_SUCCESS) {
        destroy(buffer);
        return false;
    }
    buffer.size = size;
    return true;
}


void VkContext::flush(const VkGpuBuffer& buffer, VkDeviceSize size)
{
    if (buffer.coherent || size == 0) return;
    VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    range.memory = buffer.memory;
    range.size = VK_WHOLE_SIZE;
    vkFlushMappedMemoryRanges(device, 1, &range);
}


void VkContext::destroy(VkGpuBuffer& buffer)
{
    if (buffer.buffer) vkDestroyBuffer(device, buffer.buffer, nullptr);
    if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
    buffer = VkGpuBuffer();
}


bool VkContext::createTexture(VkTexture& texture, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkSampleCountFlagBits samples, bool transient, uint32_t levels)
{
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1};
    info.mipLevels = levels;
    info.arrayLayers = 1;
    info.samples = samples;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage | (transient ? VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT : 0);
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &info, nullptr, &texture.image) != VK_SUCCESS) {
        texture = VkTexture();
        return false;
    }

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, texture.image, &requirements);
    texture.memory = allocate(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, transient ? VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT : 0);
    if (!texture.memory) texture.memory = allocate(requirements, 0, 0);
    if (!texture.memory || vkBindImageMemory(device, texture.image, texture.memory, 0) != VK_SUCCESS) {
        if (texture.memory) vkFreeMemory(device, texture.memory, nullptr);
        vkDestroyImage(device, texture.image, nullptr);
        texture = VkTexture();
        return false;
    }

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = texture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = {VkImageAspectFlags(_depthStencil(format) ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : VK_IMAGE_ASPECT_COLOR_BIT), 0, levels, 0, 1};
    vkCreateImageView(device, &viewInfo, nullptr, &texture.view);

    texture.format = format;
    texture.width = width;
    texture.height = height;
    texture.levels = levels;
    texture.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}


VkFramebuffer VkContext::createFramebuffer(VkRenderPass renderPass, const VkImageView* views, uint32_t count, uint32_t width, uint32_t height)
{
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = renderPass;
    info.attachmentCount = count;
    info.pAttachments = views;
    info.width = width;
    info.height = height;
    info.layers = 1;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    vkCreateFramebuffer(device, &info, nullptr, &framebuffer);
    return framebuffer;
}


VkSampler VkContext::createSampler(VkFilter filter, VkSamplerAddressMode modeU, VkSamplerAddressMode modeV, VkSamplerMipmapMode mipmap)
{
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = filter;
    info.minFilter = filter;
    info.mipmapMode = mipmap;
    info.addressModeU = modeU;
    info.addressModeV = modeV;
    info.addressModeW = modeV;
    info.maxLod = VK_LOD_CLAMP_NONE;
    VkSampler sampler = VK_NULL_HANDLE;
    vkCreateSampler(device, &info, nullptr, &sampler);
    return sampler;
}


VkDescriptorPool VkContext::createPool(bool freeable, uint32_t sets)
{
    VkDescriptorPoolSize sizes[]{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sets},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 2 * VK_MAX_FRAMES},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, sets},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, sets}};
    VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    info.flags = freeable ? VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT : 0;
    info.maxSets = sets;
    info.poolSizeCount = 4;
    info.pPoolSizes = sizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    vkCreateDescriptorPool(device, &info, nullptr, &pool);
    return pool;
}


VkDescriptor VkContext::allocate(VkDescriptorSetLayout layout)
{
    VkDescriptor descriptor;
    VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    info.descriptorSetCount = 1;
    info.pSetLayouts = &layout;
    for (int32_t i = int32_t(pools.count) - 1; i >= 0; --i) {
        info.descriptorPool = pools[i];
        if (vkAllocateDescriptorSets(device, &info, &descriptor.set) == VK_SUCCESS) {
            descriptor.pool = pools[i];
            return descriptor;
        }
    }
    auto pool = createPool(true, VK_DESCRIPTOR_POOL_SETS);
    if (!pool) return descriptor;
    pools.push(pool);
    info.descriptorPool = pool;
    if (vkAllocateDescriptorSets(device, &info, &descriptor.set) == VK_SUCCESS) descriptor.pool = pool;
    else descriptor.set = VK_NULL_HANDLE;
    return descriptor;
}


void VkContext::updateTexture(VkDescriptorSet set, const VkTexture& texture, VkSampler sampler)
{
    VkDescriptorImageInfo image{sampler, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}


VkDescriptor VkContext::allocateTexture(const VkTexture& texture, VkSampler sampler)
{
    auto descriptor = allocate(layoutTexture);
    if (descriptor.set) updateTexture(descriptor.set, texture, sampler);
    return descriptor;
}


void VkContext::release(VkGpuBuffer& buffer)
{
    if (buffer.buffer) garbage().buffers.push(buffer.buffer);
    if (buffer.memory) garbage().memories.push(buffer.memory);
    buffer = VkGpuBuffer();
}


void VkContext::release(VkTexture& texture)
{
    auto& bin = garbage();
    if (texture.view) bin.views.push(texture.view);
    if (texture.image) bin.images.push(texture.image);
    if (texture.memory) bin.memories.push(texture.memory);
    texture = VkTexture();
}


void VkContext::release(VkDescriptor& descriptor)
{
    if (descriptor.set) garbage().descriptors.push(descriptor);
    descriptor = VkDescriptor();
}


void VkContext::release(VkFramebuffer& framebuffer)
{
    if (framebuffer) garbage().framebuffers.push(framebuffer);
    framebuffer = VK_NULL_HANDLE;
}


void VkContext::release(VkImageView& view)
{
    if (view) garbage().views.push(view);
    view = VK_NULL_HANDLE;
}


void VkContext::release(VkSwapchainKHR& swapchain)
{
    if (swapchain) garbage().swapchains.push(swapchain);
    swapchain = VK_NULL_HANDLE;
}


void VkContext::transition(VkCommandBuffer cmd, VkTexture& texture, VkImageLayout layout)
{
    // read-only layouts need no dependency among themselves
    if (texture.layout == layout && layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && layout != VK_IMAGE_LAYOUT_GENERAL && layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) return;

    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    VkPipelineStageFlags srcStage, dstStage;
    _access(texture.layout, srcStage, barrier.srcAccessMask);
    _access(layout, dstStage, barrier.dstAccessMask);
    barrier.oldLayout = texture.layout;
    barrier.newLayout = layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, texture.levels, 0, 1};
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    texture.layout = layout;
}


void VkContext::transition(VkCommandBuffer cmd, VkTexture& texture0, VkImageLayout layout0, VkTexture& texture1, VkImageLayout layout1)
{
    VkImageMemoryBarrier barriers[2];
    VkPipelineStageFlags srcStages = 0, dstStages = 0;
    VkTexture* textures[]{&texture0, &texture1};
    VkImageLayout layouts[]{layout0, layout1};
    for (uint32_t i = 0; i < 2; ++i) {
        VkPipelineStageFlags srcStage, dstStage;
        auto& barrier = barriers[i];
        barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        _access(textures[i]->layout, srcStage, barrier.srcAccessMask);
        _access(layouts[i], dstStage, barrier.dstAccessMask);
        barrier.oldLayout = textures[i]->layout;
        barrier.newLayout = layouts[i];
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = textures[i]->image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, textures[i]->levels, 0, 1};
        srcStages |= srcStage;
        dstStages |= dstStage;
        textures[i]->layout = layouts[i];
    }
    vkCmdPipelineBarrier(cmd, srcStages, dstStages, 0, 0, nullptr, 0, nullptr, 2, barriers);
}


void VkContext::generateMipmaps(VkCommandBuffer cmd, VkTexture& texture)
{
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    // the first level holds the uploaded pixels, the others are undefined
    auto width = int32_t(texture.width), height = int32_t(texture.height);
    for (uint32_t level = 1; level < texture.levels; ++level) {
        VkPipelineStageFlags srcStage, dstStage;
        barrier.subresourceRange.baseMipLevel = level - 1;
        barrier.oldLayout = level == 1 ? texture.layout : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        _access(barrier.oldLayout, srcStage, barrier.srcAccessMask);
        _access(barrier.newLayout, dstStage, barrier.dstAccessMask);
        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        barrier.subresourceRange.baseMipLevel = level;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1};
        blit.srcOffsets[1] = {width, height, 1};
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
        blit.dstOffsets[1] = {width, height, 1};
        vkCmdBlitImage(cmd, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
    }

    // every level but the last is a transfer source now
    VkImageMemoryBarrier barriers[2]{barrier, barrier};
    barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, texture.levels - 1, 0, 1};
    barriers[0].oldLayout = texture.levels > 1 ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : texture.layout;
    barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, texture.levels - 1, 1, 0, 1};
    barriers[1].oldLayout = texture.levels > 1 ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : texture.layout;
    barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    for (auto& b : barriers) {
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    auto first = texture.levels > 1 ? 0 : 1;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2 - first, barriers + first);
    texture.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}
