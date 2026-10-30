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

#ifndef _TVG_MTL_COMMON_H_
#define _TVG_MTL_COMMON_H_

// MacTypes declares global Point and Rect, which collide with tvg::Point.
#define Point MacTypesPoint
#define Rect MacTypesRect
#import <Metal/Metal.h>
#undef Point
#undef Rect

#include "tvgGpuCommon.h"

#define MTL_MAX_FRAMES_IN_FLIGHT 3

enum MtlBufferSlot : uint32_t { MtlSlotAdjust = 0, MtlSlotPosition = 1, MtlSlotAttribute = 2, MtlSlotMask = 3, MtlSlotView = 4, MtlSlotPaint = 5, MtlSlotDepth = 6 };
// Fragment slot 0 stays bound to the frame buffer for paint offsets, so inline values need their own slot.
enum : uint32_t { MtlSlotFragmentValue = 1 };

struct MtlBinding
{
    id<MTLTexture> texture{};
    id<MTLSamplerState> sampler{};

    bool operator==(const MtlBinding& rhs) const { return texture == rhs.texture && sampler == rhs.sampler; }
    bool operator!=(const MtlBinding& rhs) const { return !(*this == rhs); }
};

struct MtlContext
{
    id<MTLDevice> device{};
    id<MTLCommandQueue> queue{};
    id<MTLSamplerState> samplerNearestClamp{};
    id<MTLSamplerState> samplerNearestRepeat{};
    id<MTLSamplerState> samplerLinearRepeat{};
    id<MTLSamplerState> samplerLinearMirror{};
    id<MTLSamplerState> samplerLinearClamp{};
    id<MTLCommandBuffer> uploads{};
    id<MTLBlitCommandEncoder> uploadEncoder{};
    bool tiled{};

    void initialize(id<MTLDevice> device, id<MTLCommandQueue> queue);
    void release();

    id<MTLTexture> createTexture(uint32_t width, uint32_t height, MTLPixelFormat format);
    id<MTLTexture> createTexStorage(uint32_t width, uint32_t height, MTLPixelFormat format);
    id<MTLTexture> createTexAttachment(uint32_t width, uint32_t height, MTLPixelFormat format, uint32_t sampleCount, bool transient);
    bool allocateTexture(id<MTLTexture>& texture, uint32_t width, uint32_t height, MTLPixelFormat format, const void* data, uint32_t bytesPerRow);
    void releaseTexture(id<MTLTexture>& texture);

    id<MTLCommandBuffer> uploadCommands();
    void flushUploads();
    bool invalid() const { return !device; }

private:
    id<MTLSamplerState> createSampler(MTLSamplerMinMagFilter filter, MTLSamplerAddressMode addrMode, uint32_t anisotropy);
};

#endif // _TVG_MTL_COMMON_H_
