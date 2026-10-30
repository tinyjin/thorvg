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

#include "tvgMtlTextureMgr.h"

static tvg::Inlist<MtlTextureEntry>& _entries(MtlTextureMgr::SurfaceEntry& surfaceEntry, FilterMethod filter)
{
    return (filter == FilterMethod::Bilinear) ? surfaceEntry.bilinear : surfaceEntry.nearest;
}

static MtlTextureEntry* _findEntry(tvg::Inlist<MtlTextureEntry>& entries, id<MTLTexture> texture)
{
    INLIST_FOREACH(entries, entry)
    {
        if (entry->binding.texture == texture) return entry;
    }
    return nullptr;
}

static bool _matches(const MtlTextureEntry& entry, const RenderSurface* surface, MTLPixelFormat format)
{
    auto texture = entry.binding.texture;
    return texture && (texture.width == surface->w) && (texture.height == surface->h) && (texture.pixelFormat == format);
}

MtlTextureMgr::SurfaceEntry* MtlTextureMgr::find(const RenderSurface* surface)
{
    INLIST_FOREACH(surfaces, entry)
    {
        if (entry->surface == surface) return entry;
    }
    return nullptr;
}

MTLPixelFormat MtlTextureMgr::textureFormat(const RenderSurface* surface)
{
    if (surface->cs == ColorSpace::ABGR8888 || surface->cs == ColorSpace::ABGR8888S) return MTLPixelFormatRGBA8Unorm;
    if (surface->cs == ColorSpace::ARGB8888 || surface->cs == ColorSpace::ARGB8888S) {
        return surface->premultiplied ? MTLPixelFormatBGRA8Unorm : MTLPixelFormatRGBA8Unorm;
    }
    return MTLPixelFormatR8Unorm;  // must be
}

void MtlTextureMgr::upload(MtlContext& context, MtlTextureEntry& entry, const RenderSurface* surface, FilterMethod filter)
{
    uint8_t preprocess = entry.preprocess & Queued;
    if (surface->channelSize == sizeof(uint32_t)) {
        if (!surface->premultiplied) preprocess |= Premultiply;
        if (!surface->premultiplied && (surface->cs == ColorSpace::ARGB8888 || surface->cs == ColorSpace::ARGB8888S)) preprocess |= Bgr;
    }
    entry.preprocess = static_cast<MtlTexPrep>(preprocess);

    auto bytesPerRow = surface->stride * CHANNEL_SIZE(surface->cs);
    if (!context.allocateTexture(entry.binding.texture, surface->w, surface->h, textureFormat(surface), surface->data, bytesPerRow)) return;
    entry.binding.sampler = (filter == FilterMethod::Bilinear) ? context.samplerLinearClamp : context.samplerNearestClamp;
}

static id<MTLComputePipelineState> _createPrepPipeline(MtlContext& context, MtlPipelines& pipelines, const char* name)
{
    NSError* error = nil;
    auto pipeline = [context.device newComputePipelineStateWithFunction:pipelines.function(name) error:&error];
    if (!pipeline) TVGERR("MTL_ENGINE", "Failed to create a compute pipeline: %s", error.localizedDescription.UTF8String);
    return pipeline;
}

bool MtlTextureMgr::flushPreprocess(MtlContext& context, MtlPipelines& pipelines)
{
    if (prepRequests.empty()) return true;

    context.flushUploads();
    auto commands = context.uploadCommands();

    ARRAY_FOREACH(p, prepRequests)
    {
        auto* entry = *p;
        auto preprocess = entry->preprocess & (Premultiply | Bgr);
        auto texture = entry->binding.texture;
        auto width = texture.width;
        auto height = texture.height;
        if (!prepStaging || width > prepStaging.width || height > prepStaging.height) {
            context.releaseTexture(prepStaging);
            prepStaging = context.createTexStorage(width, height, MTLPixelFormatRGBA8Unorm);
        }

        auto& pipeline = (preprocess & Bgr) ? prepPremultBgr : prepPremult;
        if (!pipeline) pipeline = _createPrepPipeline(context, pipelines, (preprocess & Bgr) ? "cs_premult_bgr" : "cs_premult");

        auto compute = [commands computeCommandEncoder];
        [compute setComputePipelineState:pipeline];
        [compute setTexture:texture atIndex:0];
        [compute setTexture:prepStaging atIndex:1];
        [compute dispatchThreadgroups:MTLSizeMake((width + 15) / 16, (height + 15) / 16, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
        [compute endEncoding];

        auto blit = [commands blitCommandEncoder];
        [blit copyFromTexture:prepStaging sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(width, height, 1)
                    toTexture:texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
        [blit endEncoding];
        entry->preprocess = PrepNone;
    }
    prepRequests.clear();
    return true;
}

void MtlTextureMgr::releaseEntry(MtlContext& context, MtlTextureEntry& entry)
{
    context.releaseTexture(entry.binding.texture);
    entry.binding = {};
    entry.refCnt = 0;
}

const MtlTextureEntry* MtlTextureMgr::retain(MtlContext& context, const RenderSurface* surface, FilterMethod filter, bool refreshTexture)
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
        entry = new MtlTextureEntry;
        entries.back(entry);
    } else if (!entry) {
        entry = new MtlTextureEntry;
        entries.back(entry);
    }
    if (!entry->binding.texture || refreshTexture) upload(context, *entry, surface, filter);

    if ((entry->preprocess & Queued) && !(entry->preprocess & (Premultiply | Bgr))) {
        ARRAY_FOREACH(p, prepRequests) {
            if (*p != entry) continue;
            *p = prepRequests.last();
            prepRequests.pop();
            break;
        }
        entry->preprocess = PrepNone;
    } else if (entry->binding.texture && (entry->preprocess & (Premultiply | Bgr)) && !(entry->preprocess & Queued)) {
        entry->preprocess = static_cast<MtlTexPrep>(entry->preprocess | Queued);
        prepRequests.push(entry);
    }

    ++entry->refCnt;
    return entry;
}

void MtlTextureMgr::release(MtlContext& context, const RenderSurface* surface, FilterMethod filter, id<MTLTexture> texture)
{
    auto* surfaceEntry = find(surface);
    if (!surfaceEntry) return;

    auto& entries = _entries(*surfaceEntry, filter);
    auto* entry = _findEntry(entries, texture);
    if (!entry) return;

    if (entry->refCnt > 0) --entry->refCnt;
    if (entry->refCnt > 0) return;

    if (entry->preprocess & Queued) {
        for (uint32_t i = 0; i < prepRequests.count; ++i) {
            if (prepRequests[i] != entry) continue;
            prepRequests[i] = prepRequests.last();
            prepRequests.pop();
            break;
        }
        entry->preprocess = PrepNone;
    }

    releaseEntry(context, *entry);
    entries.remove(entry);
    delete (entry);
    if (surfaceEntry->bilinear.empty() && surfaceEntry->nearest.empty()) {
        surfaces.remove(surfaceEntry);
        delete (surfaceEntry);
    }
}

void MtlTextureMgr::clear(MtlContext& context)
{
    prepRequests.clear();
    context.releaseTexture(prepStaging);
    [prepPremult release];
    [prepPremultBgr release];
    prepPremult = prepPremultBgr = nil;

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
