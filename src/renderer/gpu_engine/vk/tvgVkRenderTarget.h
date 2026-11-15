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

#ifndef _TVG_VK_RENDER_TARGET_H_
#define _TVG_VK_RENDER_TARGET_H_

#include "tvgVkPipelines.h"
#include "tvgRender.h"

struct VkRenderTarget
{
    struct Resolve
    {
        VkTexture texture;
        VkFramebuffer framebuffer{};
        VkFramebuffer framebufferSS{};
        VkDescriptor bgTexture;         // Nearest + repeat for general compositing and pixel-aligned effects.
        VkDescriptor bgTextureLinear;   // Linear + clamp for subpixel sampling, such as motion blur.
    };

    Resolve current;
    Resolve previous;  // read back when a pass resumes
    uint32_t width{}, height{};
    uint32_t used{};   // frame of the last allocation from the pool

    bool initialize(VkContext& context, uint32_t width, uint32_t height);
    bool flip(VkContext& context);
    VkFramebuffer getFramebuffer(VkContext& context, VkPipelines& pipelines, VkImageView transient, VkImageView depthStencil);
    VkFramebuffer getFramebufferSS(VkContext& context, VkPipelines& pipelines);
    VkDescriptorSet getTextureLinear(VkContext& context);
    void release(VkContext& context);
};

struct VkRenderTargetPool
{
    Array<VkRenderTarget*> list, pool;
    uint32_t width{}, height{};
    uint32_t frame{};

    VkRenderTarget* allocate(VkContext& context, const RenderRegion& region);
    bool evict(VkContext& context);
    void initialize(uint32_t width, uint32_t height);
    void release(VkContext& context);
};

#endif // _TVG_VK_RENDER_TARGET_H_
