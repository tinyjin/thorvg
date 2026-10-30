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

#include "tvgMtlRenderTarget.h"

static uint32_t _renderTargetSize(uint32_t value, uint32_t limit)
{
    uint32_t size = 1;
    while (size < value && size < limit) {
        if (size > limit / 2) return limit;
        size <<= 1;
    }
    return size < limit ? size : limit;
}

void MtlRenderTarget::initialize(MtlContext& context, uint32_t width, uint32_t height)
{
    this->width = width;
    this->height = height;
    texture = context.createTexStorage(width, height, MTLPixelFormatRGBA8Unorm);
    bgTexture = {texture, context.samplerNearestRepeat};
    bgTextureLinear = {texture, context.samplerLinearClamp};
}

void MtlRenderTarget::release(MtlContext& context)
{
    context.releaseTexture(textureRestore);
    context.releaseTexture(textureTile);
    context.releaseTexture(textureMS);
    context.releaseTexture(texture);
    bgTexture = {};
    bgTextureLinear = {};
    height = 0;
    width = 0;
}

// The samples never leave the pass: memoryless on tile based GPUs, a resumed pass restores them from the resolved result.
id<MTLTexture> MtlRenderTarget::attachment(MtlContext& context)
{
    auto& attachment = context.tiled ? textureTile : textureMS;
    if (!attachment) attachment = context.createTexAttachment(width, height, MTLPixelFormatRGBA8Unorm, 4, context.tiled);
    return attachment;
}

// resolves into the other texture so that the current result can be read back into the pass
void MtlRenderTarget::flip(MtlContext& context)
{
    if (!textureRestore) textureRestore = context.createTexStorage(width, height, MTLPixelFormatRGBA8Unorm);
    std::swap(texture, textureRestore);
    bgTexture.texture = bgTextureLinear.texture = texture;
}

//*****************************************************************************
// render target pool
//*****************************************************************************

MtlRenderTarget* MtlRenderTargetPool::allocate(MtlContext& context, const RenderRegion& region)
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

    auto renderTarget = new MtlRenderTarget;
    renderTarget->initialize(context, targetWidth, targetHeight);
    renderTarget->used = frame;
    list.push(renderTarget);

    return renderTarget;
}

// Called once per frame after all targets returned to the pool, the command buffers in flight retain what they use.
bool MtlRenderTargetPool::evict(MtlContext& context)
{
    ++frame;
    auto evicted = false;
    for (uint32_t i = 0; i < pool.count;) {
        auto target = pool[i];
        if (frame - target->used <= MTL_IDLE_FRAMES) {
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

void MtlRenderTargetPool::initialize(uint32_t width, uint32_t height)
{
    this->width = width;
    this->height = height;
}

void MtlRenderTargetPool::release(MtlContext& context)
{
    for (auto& p : list) {
       p->release(context);
       delete(p);
    }
    list.clear();
    pool.clear();
    height = 0;
    width = 0;
}
