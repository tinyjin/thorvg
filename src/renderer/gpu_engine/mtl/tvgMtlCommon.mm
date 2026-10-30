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

#include "tvgMtlCommon.h"

#pragma clang diagnostic ignored "-Wunguarded-availability-new"

void MtlContext::initialize(id<MTLDevice> device, id<MTLCommandQueue> queue)
{
    this->device = [device retain];
    this->queue = queue ? [queue retain] : [device newCommandQueue];

    samplerNearestClamp = createSampler(MTLSamplerMinMagFilterNearest, MTLSamplerAddressModeClampToEdge, 1);
    samplerNearestRepeat = createSampler(MTLSamplerMinMagFilterNearest, MTLSamplerAddressModeRepeat, 1);
    samplerLinearRepeat = createSampler(MTLSamplerMinMagFilterLinear, MTLSamplerAddressModeRepeat, 4);
    samplerLinearMirror = createSampler(MTLSamplerMinMagFilterLinear, MTLSamplerAddressModeMirrorRepeat, 4);
    samplerLinearClamp = createSampler(MTLSamplerMinMagFilterLinear, MTLSamplerAddressModeClampToEdge, 4);

    // A selector test instead of @available avoids linking the compiler runtime version check.
    tiled = [device respondsToSelector:@selector(supportsFamily:)] && [device supportsFamily:MTLGPUFamilyApple1];
}

void MtlContext::release()
{
    flushUploads();
    [samplerLinearClamp release];
    [samplerLinearMirror release];
    [samplerLinearRepeat release];
    [samplerNearestRepeat release];
    [samplerNearestClamp release];
    [queue release];
    [device release];
    samplerLinearClamp = samplerLinearMirror = samplerLinearRepeat = samplerNearestRepeat = samplerNearestClamp = nil;
    queue = nil;
    device = nil;
}

id<MTLSamplerState> MtlContext::createSampler(MTLSamplerMinMagFilter filter, MTLSamplerAddressMode addrMode, uint32_t anisotropy)
{
    auto desc = [[MTLSamplerDescriptor alloc] init];
    desc.minFilter = filter;
    desc.magFilter = filter;
    desc.sAddressMode = addrMode;
    desc.tAddressMode = addrMode;
    desc.rAddressMode = addrMode;
    desc.maxAnisotropy = anisotropy;
    auto sampler = [device newSamplerStateWithDescriptor:desc];
    [desc release];
    return sampler;
}

id<MTLTexture> MtlContext::createTexture(uint32_t width, uint32_t height, MTLPixelFormat format)
{
    auto desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:width height:height mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    return [device newTextureWithDescriptor:desc];
}

id<MTLTexture> MtlContext::createTexStorage(uint32_t width, uint32_t height, MTLPixelFormat format)
{
    auto desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:width height:height mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsageRenderTarget;
    desc.storageMode = MTLStorageModePrivate;
    return [device newTextureWithDescriptor:desc];
}

id<MTLTexture> MtlContext::createTexAttachment(uint32_t width, uint32_t height, MTLPixelFormat format, uint32_t sampleCount, bool transient)
{
    auto desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:width height:height mipmapped:NO];
    desc.usage = MTLTextureUsageRenderTarget;
    desc.storageMode = MTLStorageModePrivate;
    if (transient && tiled) desc.storageMode = MTLStorageModeMemoryless;
    if (sampleCount > 1) {
        desc.textureType = MTLTextureType2DMultisample;
        desc.sampleCount = sampleCount;
    }
    return [device newTextureWithDescriptor:desc];
}

bool MtlContext::allocateTexture(id<MTLTexture>& texture, uint32_t width, uint32_t height, MTLPixelFormat format, const void* data, uint32_t bytesPerRow)
{
    if (texture && texture.width == width && texture.height == height && texture.pixelFormat == format) {
        auto staging = [device newBufferWithBytes:data length:uint64_t(bytesPerRow) * height options:MTLResourceStorageModeShared];
        uploadCommands();
        if (!uploadEncoder) uploadEncoder = [[uploads blitCommandEncoder] retain];
        [uploadEncoder copyFromBuffer:staging sourceOffset:0 sourceBytesPerRow:bytesPerRow sourceBytesPerImage:uint64_t(bytesPerRow) * height
                           sourceSize:MTLSizeMake(width, height, 1) toTexture:texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
        [staging release];
        return false;
    }
    releaseTexture(texture);
    texture = createTexture(width, height, format);
    [texture replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0 withBytes:data bytesPerRow:bytesPerRow];
    return true;
}

void MtlContext::releaseTexture(id<MTLTexture>& texture)
{
    [texture release];
    texture = nil;
}

id<MTLCommandBuffer> MtlContext::uploadCommands()
{
    if (!uploads) uploads = [[queue commandBuffer] retain];
    return uploads;
}

void MtlContext::flushUploads()
{
    if (uploadEncoder) {
        [uploadEncoder endEncoding];
        [uploadEncoder release];
        uploadEncoder = nil;
    }
    if (uploads) {
        [uploads commit];
        [uploads release];
        uploads = nil;
    }
}
