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

#ifndef _TVG_MTL_PIPELINES_H_
#define _TVG_MTL_PIPELINES_H_

#include "tvgLock.h"
#include "tvgMtlCommon.h"

enum class MtlRenderSettingsType;

struct MtlPipeline
{
    id<MTLRenderPipelineState> state{};
    id<MTLDepthStencilState> depthStencil{};
};

enum class MtlPipe : uint32_t
{
    Nonzero, Evenodd, Direct,                                                                         // stencil markup
    CopyStencilToDepth, CopyStencilToDepthInterm, CopyDepthToStencil, MergeDepthStencil, ClearDepth,  // clip path markup
    Solid, Radial, Linear, Conic,                                                                     // normal blend
    SolidConv, SolidBatch, SolidStencilBatch, RadialConv, LinearConv, ConicConv,                      // geometry without stencil cover
    Image, ImageDirect, Scene,
    EffectGaussianVert, EffectGaussianHorz, EffectDropShadow, EffectFill, EffectTint, EffectTritone, EffectMotionBlur,
    Count
};

// Shared by the canvases rendering with the same device. Pipelines are created on first use.
struct MtlPipelines
{
    static MtlPipelines* acquire(MtlContext& context);
    static void release(MtlPipelines* pipelines);

    const MtlPipeline& get(MtlPipe pipe);
    const MtlPipeline& solidBlend(BlendMethod method);
    const MtlPipeline& radialBlend(BlendMethod method);
    const MtlPipeline& linearBlend(BlendMethod method);
    const MtlPipeline& conicBlend(BlendMethod method);
    const MtlPipeline& imageBlend(BlendMethod method);
    const MtlPipeline& sceneBlend(BlendMethod method);
    const MtlPipeline& clippedStroke(MtlRenderSettingsType type);
    const MtlPipeline& sceneCompose(uint32_t method);
    id<MTLRenderPipelineState> blit(MTLPixelFormat format, bool premultiplied);
    id<MTLRenderPipelineState> restore();
    id<MTLComputePipelineState> premultiply(bool bgr);

private:
    enum Material : uint32_t { MaterialSolid = 0, MaterialLinear, MaterialRadial, MaterialConic, MaterialImage, MaterialScene, MaterialCount };

    struct BlitPipeline
    {
        MTLPixelFormat format;
        bool premultiplied;
        id<MTLRenderPipelineState> state;
    };

    void initialize(MtlContext& context);
    void destroy();
    id<MTLFunction> function(const char* name);
    id<MTLLibrary> createLibrary(const char* source);
    id<MTLLibrary> blendLibrary(Material material);
    const MtlPipeline& blendPipeline(Material material, BlendMethod method);

    id<MTLDevice> device{};
    bool tiled{};
    uint32_t refCnt = 1;
    StrictKey key;
    id<MTLLibrary> library{};
    id<MTLLibrary> blendLibraries[MaterialCount]{};
    MtlPipeline pipelines[(uint32_t)MtlPipe::Count]{};
    MtlPipeline blends[MaterialCount][18]{};
    MtlPipeline strokeClip[4]{};
    MtlPipeline compose[11]{};
    Array<BlitPipeline> blits;
    id<MTLRenderPipelineState> restorePipeline{};
    id<MTLComputePipelineState> premultiplies[2]{};
};

#endif // _TVG_MTL_PIPELINES_H_
