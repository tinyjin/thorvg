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

#ifndef _TVG_VK_PIPELINES_H_
#define _TVG_VK_PIPELINES_H_

#include "tvgLock.h"
#include "tvgVkCommon.h"
#include "tvgVkShaderSrc.h"

enum class VkRenderSettingsType { None = 0, Solid = 1, Linear = 2, Radial = 3, Conic = 4 };

// sources of the custom blend shader, see shaders/blend.frag
enum class VkMaterial { Solid = 0, Linear, Radial, Conic, Image, Scene, Count };

enum class VkEffect { GaussianVert = 0, GaussianHorz, MotionBlur, Fill, Tint, Tritone, Count };

enum class VkPipe
{
    // stencil markup
    NonZero, EvenOdd, Direct,
    // clip path markup
    CopyStencilToDepth,        // depth 0.50, clear stencil
    CopyStencilToDepthInterm,  // depth 0.75, clear stencil
    CopyDepthToStencil,        // depth 0.50 and 0.75, update stencil
    MergeDepthStencil,         // depth 0.75, update stencil
    ClearDepth,                // depth 1.00, clear stencil
    // normal blend
    Solid, SolidConv, SolidBatch, SolidStencilBatch,
    Linear, Radial, Conic, LinearConv, RadialConv, ConicConv,
    Image, ImageDirect, Scene,
    Restore,
    Shadow,
    Count
};

// Shared by the canvases rendering with the same device. Pipelines are created on first use.
struct VkPipelines
{
    VkDescriptorSetLayout layoutUniforms{};  // set 0: view and paint dynamic uniform buffers
    VkDescriptorSetLayout layoutTexture{};   // set 1, 2: a combined image sampler
    VkDescriptorSetLayout layoutBackdrop{};  // set 3: the multisampled color as an input attachment
    VkDescriptorSetLayout layoutPrepare{};   // compute: storage buffer and storage image
    VkPipelineLayout pipelineLayout{};
    VkPipelineLayout prepareLayout{};
    VkRenderPass passMS{};
    VkRenderPass passSS[2]{};  // index 0: clear, 1: load

    static VkPipelines* acquire(VkContext& context);
    static void release(VkContext& context, VkPipelines* pipelines);

    VkPipeline get(VkContext& context, VkPipe pipe);
    VkPipeline clippedStroke(VkContext& context, VkRenderSettingsType type);
    VkPipeline blend(VkContext& context, VkMaterial material, BlendMethod method);
    VkPipeline compose(VkContext& context, MaskMethod method);
    VkPipeline effect(VkContext& context, VkEffect effect);
    VkPipeline blit(VkContext& context, VkFormat format, bool premultiplied);
    VkPipeline premultiply(VkContext& context, bool bgr);
    VkRenderPass blitPass(VkContext& context, VkFormat format, bool present);

    // the hardware blend methods do not read the backdrop
    static bool hardware(BlendMethod method)
    {
        return method == BlendMethod::Screen || method == BlendMethod::Exclusion || method == BlendMethod::Add;
    }

private:
    enum Shader { StencilVert, DepthVert, SolidVert, GradientVert, ImageVert, QuadVert, ComposeVert,
                  SolidFrag, LinearFrag, RadialFrag, ConicFrag, ImageFrag, SceneFrag, BlendFrag, ComposeFrag, BlitFrag, EffectFrag, ShadowFrag,
                  PremultiplyComp, ShaderCount };
    enum class Input { Stencil, Solid, SolidBatch, Image, Quad, Compose };
    enum class Depth { None, NonZero, EvenOdd, Direct, CopyStencilToDepth, CopyStencilToDepthInterm, CopyDepthToStencil, MergeDepthStencil, ClearDepth, Shape, Scene, Conv, ClipEqual };
    enum class Blend { None, Src, Normal, Screen, Exclusion, Add };

    struct Desc
    {
        Shader vs;
        Shader fs;  // ShaderCount: no fragment stage
        Input input;
        Depth depth;
        Blend blend;
        VkRenderPass pass;
        VkSampleCountFlagBits samples;
        int32_t spec[2];
        uint32_t specCount;
    };

    struct BlitPipeline
    {
        VkFormat format;
        bool premultiplied;
        VkPipeline pipeline;
    };

    struct BlitPass
    {
        VkFormat format;
        bool present;
        VkRenderPass pass;
    };

    VkDevice device{};
    uint32_t refCnt = 1;
    StrictKey key;
    VkPipelineCache cache{};
    VkShaderModule modules[ShaderCount]{};
    VkPipeline pipes[(int)VkPipe::Count]{};
    VkPipeline strokeClips[4]{};
    VkPipeline blends[(int)VkMaterial::Count][18]{};
    VkPipeline composes[11]{};
    VkPipeline effects[(int)VkEffect::Count]{};
    VkPipeline premultiplies[2]{};
    Array<BlitPipeline> blits;
    Array<BlitPass> blitPasses;

    bool initialize(VkContext& context);
    void destroy(VkContext& context);
    VkShaderModule module(VkContext& context, Shader shader);
    VkPipeline create(VkContext& context, const Desc& desc);
    VkRenderPass pass(VkContext& context, VkFormat format, bool present);
    Desc desc(VkPipe pipe);
};

#endif // _TVG_VK_PIPELINES_H_
