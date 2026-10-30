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

#ifndef _TVG_MTL_TEXTURE_MGR_H_
#define _TVG_MTL_TEXTURE_MGR_H_

#include "tvgArray.h"
#include "tvgInlist.h"
#include "tvgMtlPipelines.h"

enum MtlTexPrep : uint8_t
{
    PrepNone = 0,
    Premultiply = 1 << 0,
    Bgr = 1 << 1,
    Queued = 1 << 2
};

struct MtlTextureEntry
{
    INLIST_ITEM(MtlTextureEntry);
    MtlBinding binding{};
    MtlTexPrep preprocess = PrepNone;
    uint32_t refCnt = 0;
};

struct MtlTextureMgr
{
    const MtlTextureEntry* retain(MtlContext& context, const RenderSurface* surface, FilterMethod filter, bool refreshTexture);
    void release(MtlContext& context, const RenderSurface* surface, FilterMethod filter, id<MTLTexture> texture);
    bool flushPreprocess(MtlContext& context, MtlPipelines& pipelines);
    void clear(MtlContext& context);

    struct SurfaceEntry
    {
        INLIST_ITEM(SurfaceEntry);
        const RenderSurface* surface = nullptr;
        tvg::Inlist<MtlTextureEntry> bilinear;
        tvg::Inlist<MtlTextureEntry> nearest;
    };

    SurfaceEntry* find(const RenderSurface* surface);
    static void upload(MtlContext& context, MtlTextureEntry& entry, const RenderSurface* surface, FilterMethod filter);
    static void releaseEntry(MtlContext& context, MtlTextureEntry& entry);
    static MTLPixelFormat textureFormat(const RenderSurface* surface);

    tvg::Inlist<SurfaceEntry> surfaces;
    id<MTLComputePipelineState> prepPremultBgr{};
    id<MTLComputePipelineState> prepPremult{};
    id<MTLTexture> prepStaging{};
    Array<MtlTextureEntry*> prepRequests;
    uint16_t stamp = 1;
};

#endif /* _TVG_MTL_TEXTURE_MGR_H_ */
