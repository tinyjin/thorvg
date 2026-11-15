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

#include "tvgVkRenderTarget.h"

static uint32_t _renderTargetSize(uint32_t value, uint32_t limit)
{
    uint32_t size = 1;
    while (size < value && size < limit) {
        if (size > limit / 2) return limit;
        size <<= 1;
    }
    return size < limit ? size : limit;
}

static bool _createResolve(VkContext& context, VkRenderTarget::Resolve& resolve, uint32_t width, uint32_t height)
{
    auto usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!context.createTexture(resolve.texture, width, height, VK_FORMAT_R8G8B8A8_UNORM, usage)) return false;
    resolve.bgTexture = context.allocateTexture(resolve.texture, context.samplerNearestRepeat);
    return true;
}

static void _releaseResolve(VkContext& context, VkRenderTarget::Resolve& resolve)
{
    context.release(resolve.bgTextureLinear);
    context.release(resolve.bgTexture);
    context.release(resolve.framebuffer);
    context.release(resolve.framebufferSS);
    context.release(resolve.texture);
}

bool VkRenderTarget::initialize(VkContext& context, uint32_t width, uint32_t height)
{
    this->width = width;
    this->height = height;
    if (!_createResolve(context, current, width, height)) return false;
    // a target can be sampled before anything renders into it, such as a mask scene out of the viewport
    auto cmd = context.commands();
    context.transition(cmd, current.texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue color{};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    context.vkCmdClearColorImage(cmd, current.texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
    context.transition(cmd, current.texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return true;
}

bool VkRenderTarget::flip(VkContext& context)
{
    if (!previous.texture.valid() && !_createResolve(context, previous, width, height)) return false;
    std::swap(current, previous);
    return true;
}

VkFramebuffer VkRenderTarget::getFramebuffer(VkContext& context, VkPipelines& pipelines, VkImageView transient, VkImageView depthStencil)
{
    if (!current.framebuffer) {
        VkImageView views[]{transient, current.texture.view, depthStencil};
        current.framebuffer = context.createFramebuffer(pipelines.passMS, views, 3, width, height);
    }
    return current.framebuffer;
}

VkFramebuffer VkRenderTarget::getFramebufferSS(VkContext& context, VkPipelines& pipelines)
{
    if (!current.framebufferSS) current.framebufferSS = context.createFramebuffer(pipelines.passSS[0], &current.texture.view, 1, width, height);
    return current.framebufferSS;
}

VkDescriptorSet VkRenderTarget::getTextureLinear(VkContext& context)
{
    if (!current.bgTextureLinear.set) current.bgTextureLinear = context.allocateTexture(current.texture, context.samplerLinearClamp);
    return current.bgTextureLinear.set;
}

void VkRenderTarget::release(VkContext& context)
{
    _releaseResolve(context, previous);
    _releaseResolve(context, current);
    height = 0;
    width = 0;
}

//*****************************************************************************
// render target pool
//*****************************************************************************

VkRenderTarget* VkRenderTargetPool::allocate(VkContext& context, const RenderRegion& region)
{
    auto targetWidth = _renderTargetSize(region.sw() > 1 ? region.sw() : 1, width);
    auto targetHeight = _renderTargetSize(region.sh() > 1 ? region.sh() : 1, height);
    for (uint32_t i = 0; i < pool.count; ++i) {
        auto target = pool[i];
        if (target->width != targetWidth || target->height != targetHeight) continue;
        pool[i] = pool.last();
        pool.pop();
        target->used = frame;
        return target;
    }

    auto renderTarget = new VkRenderTarget;
    renderTarget->initialize(context, targetWidth, targetHeight);
    renderTarget->used = frame;
    list.push(renderTarget);

    return renderTarget;
}

// Called once per frame after all targets returned to the pool, the destruction waits for the frames in flight.
bool VkRenderTargetPool::evict(VkContext& context)
{
    ++frame;
    auto evicted = false;
    for (uint32_t i = 0; i < pool.count;) {
        auto target = pool[i];
        if (frame - target->used <= VK_IDLE_FRAMES) {
            ++i;
            continue;
        }
        pool[i] = pool.last();
        pool.pop();
        for (uint32_t j = 0; j < list.count; ++j) {
            if (list[j] != target) continue;
            list[j] = list.last();
            list.pop();
            break;
        }
        target->release(context);
        delete(target);
        evicted = true;
    }
    return evicted;
}

void VkRenderTargetPool::initialize(uint32_t width, uint32_t height)
{
    this->width = width;
    this->height = height;
}

void VkRenderTargetPool::release(VkContext& context)
{
    ARRAY_FOREACH(p, list) {
       (*p)->release(context);
       delete(*p);
    }
    list.clear();
    pool.clear();
    height = 0;
    width = 0;
}
