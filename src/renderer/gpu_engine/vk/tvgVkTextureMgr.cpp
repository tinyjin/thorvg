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

#include <algorithm>
#include "tvgVkTextureMgr.h"

static tvg::Inlist<VkTextureEntry>& _entries(VkTextureMgr::SurfaceEntry& surfaceEntry, FilterMethod filter)
{
    return (filter == FilterMethod::Bilinear) ? surfaceEntry.bilinear : surfaceEntry.nearest;
}

static VkTextureEntry* _findEntry(tvg::Inlist<VkTextureEntry>& entries, VkImage texture)
{
    INLIST_FOREACH(entries, entry)
    {
        if (entry->texture.image == texture) return entry;
    }
    return nullptr;
}

static bool _premultiply(const RenderSurface* surface)
{
    return surface->channelSize == sizeof(uint32_t) && !surface->premultiplied;
}

static bool _matches(const VkTextureEntry& entry, const RenderSurface* surface, VkFormat format)
{
    return entry.texture.valid() && entry.texture.width == surface->w && entry.texture.height == surface->h && entry.texture.format == format && (entry.storage || !_premultiply(surface));
}

VkTextureMgr::SurfaceEntry* VkTextureMgr::find(const RenderSurface* surface)
{
    INLIST_FOREACH(surfaces, entry)
    {
        if (entry->surface == surface) return entry;
    }
    return nullptr;
}

VkFormat VkTextureMgr::textureFormat(const RenderSurface* surface)
{
    if (surface->cs == ColorSpace::ABGR8888 || surface->cs == ColorSpace::ABGR8888S) return VK_FORMAT_R8G8B8A8_UNORM;
    if (surface->cs == ColorSpace::ARGB8888 || surface->cs == ColorSpace::ARGB8888S) {
        // straight alpha pixels are swizzled by the premultiply pass, which writes RGBA
        return surface->premultiplied ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
    }
    return VK_FORMAT_R8_UNORM;  // must be
}

// Minified images sample a box filtered mip level, close to the area averaging of the software rasterizer.
static uint32_t _levels(const RenderSurface* surface, FilterMethod filter)
{
    if (filter != FilterMethod::Bilinear) return 1;
    uint32_t levels = 1;
    for (auto size = std::max(surface->w, surface->h); size > 1; size >>= 1) ++levels;
    return levels;
}

void VkTextureMgr::upload(VkContext& context, VkPipelines& pipelines, VkTextureEntry& entry, const RenderSurface* surface, FilterMethod filter)
{
    auto premultiply = _premultiply(surface);
    auto bgr = premultiply && (surface->cs == ColorSpace::ARGB8888 || surface->cs == ColorSpace::ARGB8888S);
    auto format = textureFormat(surface);
    auto levels = _levels(surface, filter);

    if (!_matches(entry, surface, format)) {
        context.release(entry.descriptor);
        context.release(entry.storageView);
        context.release(entry.texture);
        auto usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | (premultiply ? VK_IMAGE_USAGE_STORAGE_BIT : 0);
        if (!context.createTexture(entry.texture, surface->w, surface->h, format, usage, VK_SAMPLE_COUNT_1_BIT, false, levels)) return;
        entry.storage = premultiply;
    }

    auto size = VkDeviceSize(surface->stride) * surface->h * surface->channelSize;
    VkDeviceSize offset;
    auto buffer = context.stage(surface->data, size, offset);
    if (!buffer) return;

    auto cmd = context.commands();
    if (premultiply) {
        // a storage view addresses a single level
        if (!entry.storageView) {
            VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            viewInfo.image = entry.texture.image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = format;
            viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            context.vkCreateImageView(context.device, &viewInfo, nullptr, &entry.storageView);
        }
        VkDescriptorImageInfo imageInfo{VK_NULL_HANDLE, entry.storageView, VK_IMAGE_LAYOUT_GENERAL};
        context.transition(cmd, entry.texture, VK_IMAGE_LAYOUT_GENERAL);
        context.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.premultiply(context, bgr));

        // rows are dispatched in bands that fit the storage buffer range, the descriptor offset must be aligned
        auto alignment = context.limits.minStorageBufferOffsetAlignment;
        auto rowSize = VkDeviceSize(surface->stride) * sizeof(uint32_t);
        auto bandRows = uint32_t((context.limits.maxStorageBufferRange - alignment) / rowSize);
        for (uint32_t row = 0; row < surface->h; row += bandRows) {
            auto rows = std::min(bandRows, surface->h - row);
            auto start = offset + row * rowSize;
            auto base = start - start % alignment;
            VkDescriptorBufferInfo bufferInfo{buffer, base, rows * rowSize + (start - base)};
            auto set = context.transientSet(context.layoutPrepare);
            VkWriteDescriptorSet writes[2]{{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
            writes[0].dstSet = set;
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[0].pBufferInfo = &bufferInfo;
            writes[1].dstSet = set;
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[1].pImageInfo = &imageInfo;
            context.vkUpdateDescriptorSets(context.device, 2, writes, 0, nullptr);

            const uint32_t constants[]{surface->w, rows, surface->stride, uint32_t((start - base) / sizeof(uint32_t)), row};
            context.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, context.prepareLayout, 0, 1, &set, 0, nullptr);
            context.vkCmdPushConstants(cmd, context.prepareLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), constants);
            context.vkCmdDispatch(cmd, (surface->w + 15) / 16, (rows + 15) / 16, 1);
        }
    } else {
        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.bufferRowLength = surface->stride;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {surface->w, surface->h, 1};
        context.transition(cmd, entry.texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        context.vkCmdCopyBufferToImage(cmd, buffer, entry.texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }
    context.generateMipmaps(cmd, entry.texture);

    if (!entry.descriptor.set) entry.descriptor = context.allocateTexture(entry.texture, (filter == FilterMethod::Bilinear) ? context.samplerImage : context.samplerNearestClamp);
}

void VkTextureMgr::releaseEntry(VkContext& context, VkTextureEntry& entry)
{
    context.release(entry.descriptor);
    context.release(entry.storageView);
    context.release(entry.texture);
    entry.refCnt = 0;
}

const VkTextureEntry* VkTextureMgr::retain(VkContext& context, VkPipelines& pipelines, const RenderSurface* surface, FilterMethod filter, bool refreshTexture)
{
    auto* surfaceEntry = find(surface);
    if (!surfaceEntry) {
        surfaceEntry = new SurfaceEntry;
        surfaceEntry->surface = surface;
        surfaces.back(surfaceEntry);
    }

    auto& entries = _entries(*surfaceEntry, filter);
    auto* entry = entries.tail;
    auto format = textureFormat(surface);
    if (entry && refreshTexture && !_matches(*entry, surface, format) && entry->refCnt > 0) {
        entry = new VkTextureEntry;
        entries.back(entry);
    } else if (!entry) {
        entry = new VkTextureEntry;
        entries.back(entry);
    }
    if (!entry->texture.valid() || refreshTexture) upload(context, pipelines, *entry, surface, filter);

    ++entry->refCnt;
    return entry;
}

void VkTextureMgr::release(VkContext& context, const RenderSurface* surface, FilterMethod filter, VkImage texture)
{
    auto* surfaceEntry = find(surface);
    if (!surfaceEntry) return;

    auto& entries = _entries(*surfaceEntry, filter);
    auto* entry = _findEntry(entries, texture);
    if (!entry) return;

    if (entry->refCnt > 0) --entry->refCnt;
    if (entry->refCnt > 0) return;

    releaseEntry(context, *entry);
    entries.remove(entry);
    delete (entry);
    if (surfaceEntry->bilinear.empty() && surfaceEntry->nearest.empty()) {
        surfaces.remove(surfaceEntry);
        delete (surfaceEntry);
    }
}

void VkTextureMgr::clear(VkContext& context)
{
    while (auto* surfaceEntry = surfaces.front()) {
        while (auto* entry = surfaceEntry->bilinear.front()) {
            releaseEntry(context, *entry);
            delete (entry);
        }
        while (auto* entry = surfaceEntry->nearest.front()) {
            releaseEntry(context, *entry);
            delete (entry);
        }
        delete (surfaceEntry);
    }
    if (++stamp == 0) stamp = 1;
}
