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

#include <cassert>
#include <algorithm>
#include "tvgMtlCompositor.h"

template<typename T>
static uint32_t _append(Array<T>& buffer, const T& value)
{
    buffer.push(value);
    return buffer.count - 1;
}

static const MtlPipeline& _gradientPipeline(MtlPipelines& pipelines, MtlRenderSettingsType type, bool convex)
{
    switch (type) {
        case MtlRenderSettingsType::Linear: return pipelines.get(convex ? MtlPipe::LinearConv : MtlPipe::Linear);
        case MtlRenderSettingsType::Radial: return pipelines.get(convex ? MtlPipe::RadialConv : MtlPipe::Radial);
        default: return pipelines.get(convex ? MtlPipe::ConicConv : MtlPipe::Conic);
    }
}

static const MtlPipeline& _gradientBlendPipeline(MtlPipelines& pipelines, MtlRenderSettingsType type, BlendMethod blendMethod)
{
    switch (type) {
        case MtlRenderSettingsType::Linear: return pipelines.linearBlend(blendMethod);
        case MtlRenderSettingsType::Radial: return pipelines.radialBlend(blendMethod);
        default: return pipelines.conicBlend(blendMethod);
    }
}

void MtlCompositor::initialize(MtlContext& context, uint32_t width, uint32_t height)
{
    pipelines.initialize(context);
    frameSlots = dispatch_semaphore_create(MTL_MAX_FRAMES_IN_FLIGHT);
    resize(context, width, height);
    meshDataBlit.blitBox();
}

void MtlCompositor::release(MtlContext& context)
{
    resize(context, 0, 0);
    if (frameSlots) {
        // Metal aborts on disposing a semaphore below its initial value, so wait for frames in flight.
        for (uint32_t i = 0; i < MTL_MAX_FRAMES_IN_FLIGHT; ++i) dispatch_semaphore_wait(frameSlots, DISPATCH_TIME_FOREVER);
        for (uint32_t i = 0; i < MTL_MAX_FRAMES_IN_FLIGHT; ++i) dispatch_semaphore_signal(frameSlots);
        dispatch_release(frameSlots);
        frameSlots = nullptr;
    }
    for (auto& buffer : frames) {
        [buffer release];
        buffer = nil;
    }
    frame = nil;
    stageBufferGeometry = {};
    stageBufferSolidColor = {};
    stageBufferPaint = {};
    stageBufferViewMat = {};
    pipelines.release();
}

void MtlCompositor::resize(MtlContext& context, uint32_t width, uint32_t height)
{
    if (this->width != width || this->height != height) {
        targetTemp1.release(context);
        targetTemp0.release(context);
        ARRAY_FOREACH(attachment, depthStencilAttachments) {
            context.releaseTexture(attachment->texture);
        }
        depthStencilAttachments.clear();
    }

    this->width = width;
    this->height = height;

    if (width > 0 && height > 0 && !targetTemp0.texture) {
        targetTemp0.initialize(context, width, height);
        targetTemp1.initialize(context, width, height);
    }
}

void MtlCompositor::setScissor(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    [renderPassEncoder setScissorRect:MTLScissorRect{x, y, w, h}];
}

void MtlCompositor::setScissorRegion(const RenderRegion& region)
{
    const auto& bounds = currentTargetState->bounds;
    const RenderRegion rect{
        tvg::clamp(region.xMin, bounds.xMin, bounds.xMax) - bounds.xMin, tvg::clamp(region.yMin, bounds.yMin, bounds.yMax) - bounds.yMin,
        tvg::clamp(region.xMax, bounds.xMin, bounds.xMax) - bounds.xMin, tvg::clamp(region.yMax, bounds.yMin, bounds.yMax) - bounds.yMin};
    setScissor(rect.x(), rect.y(), rect.w(), rect.h());
}

id<MTLTexture> MtlCompositor::getDepthStencil(MtlContext& context, uint32_t width, uint32_t height)
{
    ARRAY_FOREACH(attachment, depthStencilAttachments) {
        if (attachment->width == width && attachment->height == height) return attachment->texture;
    }
    auto texture = context.createTexAttachment(width, height, MTLPixelFormatDepth32Float_Stencil8, 4, true);
    // TODO: Add an eviction policy to release unused cached resources.
    depthStencilAttachments.push({width, height, texture});
    return texture;
}

void MtlCompositor::copyTexture(id<MTLTexture> src, id<MTLTexture> dst, MTLSize size, MTLOrigin origin)
{
    auto blit = [commandBuffer blitCommandEncoder];
    [blit copyFromTexture:src sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:size
                toTexture:dst destinationSlice:0 destinationLevel:0 destinationOrigin:origin];
    [blit endEncoding];
}

bool MtlCompositor::copyBackdrop(MtlContext& context, BlendMethod method)
{
    if (context.tiled) return false;
    switch (method) {
        case BlendMethod::Screen:
        case BlendMethod::Exclusion:
        case BlendMethod::Add: return false;
        default: break;
    }
    endRenderPass();
    auto src = currentTargetState->target;
    copyTexture(src->texture, targetTemp0.texture, MTLSizeMake(src->width, src->height, 1), MTLOriginMake(0, 0, 0));
    beginRenderPassMS(context, commandBuffer, *currentTargetState, false);
    return true;
}

void MtlCompositor::bindFrame(uint32_t adjustOffset)
{
    currentState = nil;
    currentDepthStencil = nil;
    [renderPassEncoder setFrontFacingWinding:MTLWindingCounterClockwise];
    for (auto slot : {MtlSlotPosition, MtlSlotAttribute, MtlSlotMask, MtlSlotView, MtlSlotPaint}) {
        [renderPassEncoder setVertexBuffer:frame offset:0 atIndex:slot];
    }
    [renderPassEncoder setVertexBuffer:frame offset:geometryBase + adjustOffset atIndex:MtlSlotAdjust];
    [renderPassEncoder setFragmentBuffer:frame offset:0 atIndex:0];
}

void MtlCompositor::beginRenderPassMS(MtlContext& context, id<MTLCommandBuffer> commandBuffer, const MtlRenderTargetState& targetState, bool clearBuffer)
{
    auto target = targetState.target;
    if (renderPassEncoder && currentTargetState == &targetState && !clearBuffer) return;
    // we must to end render pass first
    endRenderPass();
    currentTargetState = &targetState;
    // start new render pass
    this->commandBuffer = commandBuffer;
    auto depthStencil = getDepthStencil(context, target->width, target->height);
    auto transient = context.tiled && !targetState.resumed;
    auto desc = [MTLRenderPassDescriptor renderPassDescriptor];
    desc.colorAttachments[0].texture = target->attachment(context, transient);
    desc.colorAttachments[0].resolveTexture = target->texture;
    desc.colorAttachments[0].loadAction = clearBuffer ? MTLLoadActionClear : MTLLoadActionLoad;
    desc.colorAttachments[0].storeAction = transient ? MTLStoreActionMultisampleResolve : MTLStoreActionStoreAndMultisampleResolve;
    desc.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
    desc.depthAttachment.texture = depthStencil;
    desc.depthAttachment.loadAction = MTLLoadActionClear;
    desc.depthAttachment.storeAction = MTLStoreActionDontCare;
    desc.depthAttachment.clearDepth = 1.0;
    desc.stencilAttachment.texture = depthStencil;
    desc.stencilAttachment.loadAction = MTLLoadActionClear;
    desc.stencilAttachment.storeAction = MTLStoreActionDontCare;
    desc.stencilAttachment.clearStencil = 0;
    renderPassEncoder = [[commandBuffer renderCommandEncoderWithDescriptor:desc] retain];
    assert(renderPassEncoder);
    bindFrame(targetState.transformOffset);
}

void MtlCompositor::beginRenderPass(MtlContext& context, id<MTLCommandBuffer> commandBuffer, MtlRenderTarget* target, bool clearBuffer)
{
    assert(!renderPassEncoder);
    currentTargetState = nullptr;
    auto desc = [MTLRenderPassDescriptor renderPassDescriptor];
    desc.colorAttachments[0].texture = target->texture;
    desc.colorAttachments[0].loadAction = clearBuffer ? MTLLoadActionClear : MTLLoadActionLoad;
    desc.colorAttachments[0].storeAction = MTLStoreActionStore;
    desc.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
    renderPassEncoder = [[commandBuffer renderCommandEncoderWithDescriptor:desc] retain];
    bindFrame(0);
}

void MtlCompositor::endRenderPass()
{
    if (renderPassEncoder) {
        [renderPassEncoder endEncoding];
        [renderPassEncoder release];
        renderPassEncoder = nil;
    }
}

void MtlCompositor::reset(MtlContext& context)
{
    currentTargetState = nullptr;
    stageBufferGeometry.clear();
    stageBufferSolidColor.clear();
    stageBufferPaint.clear();
    stageBufferViewMat.clear();
}

void MtlCompositor::flush(MtlContext& context)
{
    dispatch_semaphore_wait(frameSlots, DISPATCH_TIME_FOREVER);
    frameIndex = (frameIndex + 1) % MTL_MAX_FRAMES_IN_FLIGHT;

    stageBufferGeometry.append(&meshDataBlit);

    const uint64_t uniformStride = sizeof(MtlShaderTypePaintSettings);
    paintBase = uniformStride * (1 + stageBufferViewMat.count);
    geometryBase = paintBase + uniformStride * stageBufferPaint.count;
    colorBase = geometryBase + ((stageBufferGeometry.vbuffer.count + 3) & ~uint64_t(3));
    indexBase = colorBase + stageBufferSolidColor.vbuffer.count * sizeof(RenderColor);
    const uint64_t size = indexBase + stageBufferGeometry.ibuffer.count;

    auto& buffer = frames[frameIndex];
    if (!buffer || buffer.length < size) {
        [buffer release];
        buffer = [context.device newBufferWithLength:size + size / 2 options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    }
    frame = buffer;

    auto dst = static_cast<uint8_t*>(frame.contents);
    MtlShaderTypeMat4x4f viewMat(width, height);
    memcpy(dst, &viewMat, sizeof(viewMat));
    if (stageBufferViewMat.count > 0) memcpy(dst + uniformStride, stageBufferViewMat.data, stageBufferViewMat.count * uniformStride);
    if (stageBufferPaint.count > 0) memcpy(dst + paintBase, stageBufferPaint.data, stageBufferPaint.count * uniformStride);
    if (stageBufferGeometry.vbuffer.count > 0) memcpy(dst + geometryBase, stageBufferGeometry.vbuffer.data, stageBufferGeometry.vbuffer.count);
    if (stageBufferSolidColor.vbuffer.count > 0) memcpy(dst + colorBase, stageBufferSolidColor.vbuffer.data, stageBufferSolidColor.vbuffer.count * sizeof(RenderColor));
    if (stageBufferGeometry.ibuffer.count > 0) memcpy(dst + indexBase, stageBufferGeometry.ibuffer.data, stageBufferGeometry.ibuffer.count);
}

void MtlCompositor::releaseFrameOn(id<MTLCommandBuffer> commandBuffer)
{
    auto slots = frameSlots;
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer>) { dispatch_semaphore_signal(slots); }];
}

void MtlCompositor::bind(const MtlPipeline& pipeline)
{
    if (currentState != pipeline.state) {
        currentState = pipeline.state;
        [renderPassEncoder setRenderPipelineState:pipeline.state];
    }
    if (currentDepthStencil != pipeline.depthStencil && pipeline.depthStencil) {
        currentDepthStencil = pipeline.depthStencil;
        [renderPassEncoder setDepthStencilState:pipeline.depthStencil];
    }
}

void MtlCompositor::bindView()
{
    [renderPassEncoder setVertexBufferOffset:0 atIndex:MtlSlotView];
}

void MtlCompositor::bindStrokeView(uint32_t index)
{
    [renderPassEncoder setVertexBufferOffset:(index + 1) * sizeof(MtlShaderTypeMat4x4fBlock) atIndex:MtlSlotView];
}

void MtlCompositor::bindPaint(uint32_t index)
{
    const auto offset = paintBase + index * sizeof(MtlShaderTypePaintSettings);
    [renderPassEncoder setVertexBufferOffset:offset atIndex:MtlSlotPaint];
    [renderPassEncoder setFragmentBufferOffset:offset atIndex:0];
}

void MtlCompositor::bindTexture(uint32_t slot, const MtlBinding& binding)
{
    [renderPassEncoder setFragmentTexture:binding.texture atIndex:slot];
    [renderPassEncoder setFragmentSamplerState:binding.sampler atIndex:slot];
}

void MtlCompositor::bindDepth(uint8_t value)
{
    const float depth = value / 255.0f;
    [renderPassEncoder setVertexBytes:&depth length:sizeof(depth) atIndex:MtlSlotDepth];
}

void MtlCompositor::bindOpacity(uint8_t value)
{
    const float opacity = value / 255.0f;
    [renderPassEncoder setFragmentBytes:&opacity length:sizeof(opacity) atIndex:MtlSlotFragmentValue];
}

void MtlCompositor::bindParams(const float* params)
{
    [renderPassEncoder setFragmentBytes:params length:12 * sizeof(float) atIndex:MtlSlotFragmentValue];
}

void MtlCompositor::bindVertex(uint32_t slot, uint64_t offset)
{
    [renderPassEncoder setVertexBufferOffset:geometryBase + offset atIndex:slot];
}

void MtlCompositor::bindColor(uint64_t offset)
{
    [renderPassEncoder setVertexBufferOffset:colorBase + offset atIndex:MtlSlotAttribute];
}

void MtlCompositor::drawIndexed(uint32_t count, uint64_t offset)
{
    [renderPassEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:count indexType:MTLIndexTypeUInt32 indexBuffer:frame indexBufferOffset:indexBase + offset];
}

void MtlCompositor::drawMesh(MtlContext& context, MtlMesh* meshData)
{
    bindVertex(MtlSlotPosition, meshData->voffset);
    drawIndexed(meshData->ibuffer.count, meshData->ioffset);
}

void MtlCompositor::drawMeshSolid(MtlContext& context, MtlMesh* meshData, uint32_t solidColorInd)
{
    bindVertex(MtlSlotPosition, meshData->voffset);
    bindColor(solidColorInd * sizeof(RenderColor));
    drawIndexed(meshData->ibuffer.count, meshData->ioffset);
}

void MtlCompositor::drawMeshImage(MtlContext& context, MtlMesh* meshData)
{
    bindVertex(MtlSlotPosition, meshData->voffset);
    bindVertex(MtlSlotAttribute, meshData->toffset);
    drawIndexed(meshData->ibuffer.count, meshData->ioffset);
}

void MtlCompositor::blit(MtlContext& context, id<MTLCommandBuffer> commandBuffer, MtlRenderTarget* src, id<MTLTexture> dst, bool premultiplied)
{
    auto desc = [MTLRenderPassDescriptor renderPassDescriptor];
    desc.colorAttachments[0].texture = dst;
    desc.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    desc.colorAttachments[0].storeAction = MTLStoreActionStore;
    auto encoder = [commandBuffer renderCommandEncoderWithDescriptor:desc];
    [encoder setRenderPipelineState:pipelines.blit(dst.pixelFormat, premultiplied)];
    [encoder setFragmentTexture:src->texture atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    [encoder endEncoding];
}

void MtlCompositor::requestQuad(MtlMesh& mesh, const MtlRenderTargetState& src, const MtlRenderTargetState& dst, const MtlRenderTargetState* mask)
{
    mesh.blitBox();
    if (mask) {
        mesh.tbuffer.reserve(8);
        mesh.tbuffer.count = 8;
    }
    for (uint32_t i = 0; i < 4; ++i) {
        auto uv = mesh.tbuffer[i];
        auto x = src.bounds.xMin + uv.x * src.bounds.w();
        auto y = src.bounds.yMin + uv.y * src.bounds.h();
        mesh.vbuffer[i] = {(x - dst.bounds.xMin) * 2.0f / dst.target->width - 1.0f,
                           1.0f - (y - dst.bounds.yMin) * 2.0f / dst.target->height};
        mesh.tbuffer[i] = {uv.x * src.bounds.w() / src.target->width, uv.y * src.bounds.h() / src.target->height};
        if (mask) mesh.tbuffer[i + 4] = {(x - mask->bounds.xMin) / mask->target->width, (y - mask->bounds.yMin) / mask->target->height};
    }
    stageBufferGeometry.append(&mesh);
}

void MtlCompositor::requestScene(MtlRenderTargetState& targetState, MtlMesh& mesh, MtlMesh& effectMesh, const MtlRenderTargetState* dst, const MtlRenderTargetState* mask, bool effects)
{
    // Keep paint/stroke matrices destination-independent so shared clips remain reusable.
    const float adjust[] = {float(width) / targetState.target->width, float(height) / targetState.target->height,
                            (float(width) - 2.0f * targetState.bounds.xMin) / targetState.target->width - 1.0f,
                            1.0f - (float(height) - 2.0f * targetState.bounds.yMin) / targetState.target->height};
    targetState.transformOffset = stageBufferGeometry.vbuffer.count;
    stageBufferGeometry.vbuffer.grow(sizeof(adjust));
    memcpy(stageBufferGeometry.vbuffer.data + targetState.transformOffset, adjust, sizeof(adjust));
    stageBufferGeometry.vbuffer.count += sizeof(adjust);
    if (dst) requestQuad(mesh, targetState, *dst, mask);
    if (effects) {
        MtlRenderTargetState scratch{&targetTemp0, {0, 0, int32_t(width), int32_t(height)}};
        requestQuad(effectMesh, scratch, targetState);
    }
}

void MtlCompositor::copyEffectTexture(MtlContext& context, MtlRenderTarget* dst, const MtlRenderTargetState& src, bool clear)
{
    if (clear) {
        beginRenderPass(context, commandBuffer, dst, true);
        endRenderPass();
    }
    copyTexture(src.target->texture, dst->texture, MTLSizeMake(src.bounds.w(), src.bounds.h(), 1), MTLOriginMake(src.bounds.x(), src.bounds.y(), 0));
}

void MtlCompositor::requestShape(MtlShape* shape)
{
    stageBufferGeometry.append(shape);

    auto& shapeSettings = shape->shape.setting;
    auto& shapeSolid = shape->shape.solid;
    if (shapeSettings.fillType == MtlRenderSettingsType::Solid) shapeSolid.colorIdx = stageBufferSolidColor.append(shapeSolid.packedColor());
    else shapeSettings.paintIdx = _append(stageBufferPaint, shapeSettings.settings);

    if (!shape->stroke.mesh.vbuffer.empty()) {
        Matrix viewMatrix{2.0f / width, 0.0f, -1.0f, 0.0f, -2.0f / height, 1.0f, 0.0f, 0.0f, 1.0f};
        MtlShaderTypeMat4x4fBlock strokeViewMat{{viewMatrix * shape->transform}, {}};
        // Test the clip depth during the stroke color draw.
        strokeViewMat.matrix.mat[14] = 128.0f / 255.0f;
        shape->strokeViewMatIdx = _append(stageBufferViewMat, strokeViewMat);
    }

    if (shape->stroke.setting.valid && !shape->stroke.mesh.vbuffer.empty()) {
        auto& strokeSettings = shape->stroke.setting;
        auto& strokeSolid = shape->stroke.solid;
        if (strokeSettings.fillType == MtlRenderSettingsType::Solid) strokeSolid.colorIdx = stageBufferSolidColor.append(strokeSolid.packedColor());
        else strokeSettings.paintIdx = _append(stageBufferPaint, strokeSettings.settings);
    }
    ARRAY_FOREACH(p, shape->clips) {
        requestShape((MtlShape*)(*p));
    }
}

void MtlCompositor::requestImage(MtlImage* image)
{
    stageBufferGeometry.append(image);
    image->setting.paintIdx = _append(stageBufferPaint, image->setting.settings);
    ARRAY_FOREACH(p, image->clips) {
        requestShape((MtlShape*)(*p));
    }
}

void MtlCompositor::requestSolidBatch(const Array<MtlShape*>& renderShapes, MtlSolidBatchRange& range)
{
    stageBufferGeometry.appendSolidBatch(renderShapes, stageBufferSolidColor, range);
    range.viewport = renderShapes[0]->viewport;
}

void MtlCompositor::requestImageBatch(const Array<MtlImage*>& renderImages, MtlImageBatchRange& range)
{
    stageBufferGeometry.appendImageBatch(renderImages, range);
    auto image = renderImages[0];
    image->setting.paintIdx = _append(stageBufferPaint, image->setting.settings);
    range.viewport = image->viewport;
}

void MtlCompositor::requestStencilBatch(const Array<MtlShape*>& renderShapes, MtlStencilBatchRange& range)
{
    stageBufferGeometry.appendStencilBatch(renderShapes, range);
    range.viewport = renderShapes[0]->viewport;
    range.fillRule = renderShapes[0]->fillRule;
    range.solidOnly = true;

    uint32_t pendingColorCount = 0;
    auto colorsStarted = false;

    ARRAY_FOREACH(p, renderShapes) {
        auto rdata = *p;
        auto& settings = rdata->shape.setting;
        const auto count = rdata->bboxMesh.vbuffer.count;

        if (settings.fillType == MtlRenderSettingsType::Solid) {
            if (!colorsStarted) {
                range.colorOffset = static_cast<size_t>(stageBufferSolidColor.vbuffer.count) * sizeof(RenderColor);
                if (pendingColorCount) stageBufferSolidColor.appendRepeated({}, pendingColorCount);
                colorsStarted = true;
            }
            stageBufferSolidColor.appendRepeated(rdata->shape.solid.packedColor(), count);
        } else {
            range.solidOnly = false;
            settings.paintIdx = _append(stageBufferPaint, settings.settings);
            if (colorsStarted) stageBufferSolidColor.appendRepeated({}, count);
            else pendingColorCount += count;
        }
    }
}

void MtlCompositor::renderShape(MtlContext& context, MtlShape* rdata, BlendMethod blendMethod)
{
    auto clipped = !rdata->clips.empty();
    if (clipped) {
        renderClipPath(context, rdata);
        // Clipped custom blends retain normal compositing.
        if (blendMethod != BlendMethod::Screen && blendMethod != BlendMethod::Exclusion && blendMethod != BlendMethod::Add) blendMethod = BlendMethod::Normal;
    }
    if (rdata->strokeFirst) {
        if (blendMethod == BlendMethod::Normal) drawStrokes(context, rdata);
        else blendStrokes(context, rdata, blendMethod);
    }
    if (clipped) clipShape(context, rdata, blendMethod);
    else if (blendMethod == BlendMethod::Normal) drawShape(context, rdata);
    else blendShape(context, rdata, blendMethod);
    if (!rdata->strokeFirst) {
        if (blendMethod == BlendMethod::Normal) drawStrokes(context, rdata);
        else blendStrokes(context, rdata, blendMethod);
    }
    if (clipped) clearClipPath(context, rdata);
}

void MtlCompositor::renderSolidBatch(const MtlSolidBatchRange& range)
{
    setScissorRegion(range.viewport);
    [renderPassEncoder setStencilReferenceValue:0];
    bindView();
    bind(pipelines.get(MtlPipe::SolidBatch));
    bindVertex(MtlSlotPosition, range.vertexOffset);
    bindColor(range.colorOffset);
    drawIndexed(range.indexCount, range.indexOffset);
}

void MtlCompositor::renderImageBatch(MtlImage* image, const MtlImageBatchRange& range)
{
    setScissorRegion(range.viewport);
    [renderPassEncoder setStencilReferenceValue:0];
    bindView();
    bindPaint(image->setting.paintIdx);
    bindTexture(0, image->binding);
    bind(pipelines.get(MtlPipe::ImageDirect));
    bindVertex(MtlSlotPosition, range.vertexOffset);
    bindVertex(MtlSlotAttribute, range.texCoordOffset);
    drawIndexed(range.indexCount, range.indexOffset);
}

void MtlCompositor::renderStencilBatch(const Array<MtlShape*>& renderShapes, const MtlStencilBatchRange& range)
{
    setScissorRegion(range.viewport);
    [renderPassEncoder setStencilReferenceValue:0];
    bindView();
    bind(range.fillRule == FillRule::NonZero ? pipelines.get(MtlPipe::Nonzero) : pipelines.get(MtlPipe::Evenodd));
    bindVertex(MtlSlotPosition, range.stencil.vertexOffset);
    drawIndexed(range.stencil.indexCount, range.stencil.indexOffset);

    bindVertex(MtlSlotPosition, range.cover.vertexOffset);

    // Keep the common all-solid case identical to the original two-draw path.
    if (range.solidOnly) {
        bind(pipelines.get(MtlPipe::SolidStencilBatch));
        bindColor(range.colorOffset);
        drawIndexed(range.cover.indexCount, range.cover.indexOffset);
        return;
    }

    uint32_t firstIndex = 0;
    bool colorsBound = false;

    auto p = renderShapes.begin();
    const auto end = renderShapes.end();
    while (p < end) {
        auto rdata = *p;
        auto& settings = rdata->shape.setting;
        const auto batchFirstIndex = firstIndex;

        do {
            firstIndex += rdata->bboxMesh.ibuffer.count;
            if (++p == end || settings.fillType != MtlRenderSettingsType::Solid) break;
            rdata = *p;
        } while (rdata->shape.setting.fillType == MtlRenderSettingsType::Solid);

        if (settings.fillType == MtlRenderSettingsType::Solid) {
            if (!colorsBound) {
                bindColor(range.colorOffset);
                colorsBound = true;
            }
            bind(pipelines.get(MtlPipe::SolidStencilBatch));
        } else {
            bindPaint(settings.paintIdx);
            bindTexture(0, settings.gradientData.binding);
            bind(_gradientPipeline(pipelines, settings.fillType, false));
        }
        drawIndexed(firstIndex - batchFirstIndex, range.cover.indexOffset + batchFirstIndex * sizeof(uint32_t));
    }
}

void MtlCompositor::renderImage(MtlContext& context, MtlImage* rdata, BlendMethod blendMethod)
{
    // apply clip path if necessary
    if (rdata->clips.count != 0) {
        renderClipPath(context, rdata);
        if (blendMethod != BlendMethod::Screen && blendMethod != BlendMethod::Exclusion && blendMethod != BlendMethod::Add) blendMethod = BlendMethod::Normal;
        clipImage(context, rdata, blendMethod);
        clearClipPath(context, rdata);
    // use custom blending
    } else if (blendMethod != BlendMethod::Normal)
        blendImage(context, rdata, blendMethod);
    // use direct hardware blending
    else drawImage(context, rdata);
}

void MtlCompositor::renderScene(MtlContext& context, MtlRenderTarget* scene, MtlCompose* compose, MtlMesh& mesh)
{
    // use custom blending
    if (compose->blend != BlendMethod::Normal)
        blendScene(context, scene, compose, mesh);
    // use direct hardware blending
    else drawScene(context, scene, compose, mesh);
}

void MtlCompositor::composeScene(MtlContext& context, MtlRenderTarget* src, MtlRenderTarget* mask, MtlCompose* cmp, MtlMesh& mesh)
{
    setScissorRegion(cmp->aabb);
    [renderPassEncoder setStencilReferenceValue:0];
    bindTexture(0, src->bgTexture);
    bindTexture(1, mask->bgTexture);
    bind(pipelines.sceneCompose((uint32_t)cmp->method));
    bindVertex(MtlSlotMask, mesh.toffset + 4 * sizeof(Point));
    drawMeshImage(context, &mesh);
}

void MtlCompositor::drawShape(MtlContext& context, MtlShape* rdata)
{
    if (!rdata->shape.setting.valid || rdata->shape.mesh.vbuffer.empty() || rdata->viewport.invalid()) return;
    auto& settings = rdata->shape.setting;
    auto convex = rdata->convex;
    auto mesh = rdata->convex ? &rdata->shape.mesh : &rdata->bboxMesh;

    setScissorRegion(rdata->viewport);

    // setup stencil rules
    if (!convex) {
        auto& stencilPipeline = (rdata->fillRule == FillRule::NonZero) ? pipelines.get(MtlPipe::Nonzero) : pipelines.get(MtlPipe::Evenodd);
        [renderPassEncoder setStencilReferenceValue:0];
        bindView();
        bind(stencilPipeline);
        // draw to stencil (first pass)
        drawMesh(context, &rdata->shape.mesh);
    }

    // setup fill rules
    [renderPassEncoder setStencilReferenceValue:0];
    bindView();

    if (settings.fillType == MtlRenderSettingsType::Solid) {
        bind(convex ? pipelines.get(MtlPipe::SolidConv) : pipelines.get(MtlPipe::Solid));
        drawMeshSolid(context, mesh, rdata->shape.solid.colorIdx);
    } else {
        bindPaint(settings.paintIdx);
        bindTexture(0, settings.gradientData.binding);
        bind(_gradientPipeline(pipelines, settings.fillType, convex));
        drawMesh(context, mesh);
    }
}

void MtlCompositor::blendShape(MtlContext& context, MtlShape* rdata, BlendMethod blendMethod)
{
    if (!rdata->shape.setting.valid || rdata->shape.mesh.vbuffer.empty() || rdata->viewport.invalid()) return;
    MtlRenderSettings& settings = rdata->shape.setting;
    auto copied = copyBackdrop(context, blendMethod);
    // render shape with blend settings
    setScissorRegion(rdata->viewport);
    // setup stencil rules
    auto& stencilPipeline = (rdata->fillRule == FillRule::NonZero) ? pipelines.get(MtlPipe::Nonzero) : pipelines.get(MtlPipe::Evenodd);
    [renderPassEncoder setStencilReferenceValue:0];
    bindView();
    bind(stencilPipeline);
    // draw to stencil (first pass)
    drawMesh(context, &rdata->shape.mesh);
    // setup fill rules
    [renderPassEncoder setStencilReferenceValue:0];
    bindView();
    if (settings.fillType == MtlRenderSettingsType::Solid) {
        if (copied) bindTexture(1, targetTemp0.bgTexture);
        bind(pipelines.solidBlend(blendMethod));
        drawMeshSolid(context, &rdata->bboxMesh, rdata->shape.solid.colorIdx);
    } else {
        bindPaint(settings.paintIdx);
        bindTexture(0, settings.gradientData.binding);
        if (copied) bindTexture(1, targetTemp0.bgTexture);
        bind(_gradientBlendPipeline(pipelines, settings.fillType, blendMethod));
        drawMesh(context, &rdata->bboxMesh);
    }
}

void MtlCompositor::clipShape(MtlContext& context, MtlShape* rdata, BlendMethod blendMethod)
{
    if (!rdata->shape.setting.valid || rdata->shape.mesh.vbuffer.empty() || rdata->viewport.invalid()) return;
    MtlRenderSettings& settings = rdata->shape.setting;
    setScissorRegion(rdata->viewport);
    // setup stencil rules
    auto& stencilPipeline = (rdata->fillRule == FillRule::NonZero) ? pipelines.get(MtlPipe::Nonzero) : pipelines.get(MtlPipe::Evenodd);
    [renderPassEncoder setStencilReferenceValue:0];
    bindView();
    bind(stencilPipeline);
    // draw to stencil (first pass)
    drawMesh(context, &rdata->shape.mesh);
    // merge depth and stencil buffer
    [renderPassEncoder setStencilReferenceValue:0];
    bindDepth(128);
    bind(pipelines.get(MtlPipe::MergeDepthStencil));
    drawMesh(context, &rdata->bboxMesh);
    // setup fill rules
    [renderPassEncoder setStencilReferenceValue:0];
    bindView();
    if (settings.fillType == MtlRenderSettingsType::Solid) {
        bind(blendMethod == BlendMethod::Normal ? pipelines.get(MtlPipe::Solid) : pipelines.solidBlend(blendMethod));
        drawMeshSolid(context, &rdata->bboxMesh, rdata->shape.solid.colorIdx);
    } else {
        bindPaint(settings.paintIdx);
        bindTexture(0, settings.gradientData.binding);
        bind(blendMethod == BlendMethod::Normal ? _gradientPipeline(pipelines, settings.fillType, false) : _gradientBlendPipeline(pipelines, settings.fillType, blendMethod));
        drawMesh(context, &rdata->bboxMesh);
    }
}

void MtlCompositor::stencilStrokes(MtlContext& context, MtlShape* rdata)
{
    [renderPassEncoder setStencilReferenceValue:255];
    bindStrokeView(rdata->strokeViewMatIdx);
    bind(pipelines.get(MtlPipe::Direct));
    drawMesh(context, &rdata->stroke.mesh);
    if (!rdata->clips.empty()) {
        [renderPassEncoder setStencilReferenceValue:0];
        bindView();
        bindDepth(128);
        bind(pipelines.get(MtlPipe::MergeDepthStencil));
        drawMesh(context, &rdata->bboxMesh);
    }
    [renderPassEncoder setStencilReferenceValue:0];
}

void MtlCompositor::drawStrokes(MtlContext& context, MtlShape* rdata)
{
    if (!rdata->stroke.setting.valid || rdata->stroke.mesh.vbuffer.empty() || rdata->viewport.invalid()) return;
    auto& settings = rdata->stroke.setting;
    auto solid = settings.fillType == MtlRenderSettingsType::Solid;
    auto direct = rdata->strokeOverlapFree || (rdata->stroke.solid.opacity == 255 && (solid ? rdata->stroke.solid.color.a == 255 : settings.opaque));
    auto clipped = !rdata->clips.empty();
    auto strokeView = rdata->strokeViewMatIdx;
    setScissorRegion(rdata->viewport);

    auto mesh = &rdata->stroke.mesh;
    const MtlPipeline* pipeline;
    if (direct) {
        // Draw eligible stroke geometry with stencil tests and writes disabled.
        pipeline = clipped ? &pipelines.clippedStroke(settings.fillType) : (solid ? &pipelines.get(MtlPipe::SolidConv) : &_gradientPipeline(pipelines, settings.fillType, true));
    } else {
        stencilStrokes(context, rdata);
        mesh = &rdata->stroke.bboxMesh;
        pipeline = solid ? &pipelines.get(MtlPipe::Solid) : &_gradientPipeline(pipelines, settings.fillType, false);
    }
    bindStrokeView(strokeView);
    bind(*pipeline);
    if (solid) {
        drawMeshSolid(context, mesh, rdata->stroke.solid.colorIdx);
    } else {
        bindPaint(settings.paintIdx);
        bindTexture(0, settings.gradientData.binding);
        drawMesh(context, mesh);
    }
}

void MtlCompositor::blendStrokes(MtlContext& context, MtlShape* rdata, BlendMethod blendMethod)
{
    if (!rdata->stroke.setting.valid || rdata->stroke.mesh.vbuffer.empty() || rdata->viewport.invalid()) return;
    MtlRenderSettings& settings = rdata->stroke.setting;
    auto strokeView = rdata->strokeViewMatIdx;
    auto copied = copyBackdrop(context, blendMethod);
    setScissorRegion(rdata->viewport);
    stencilStrokes(context, rdata);
    // setup fill rules
    bindStrokeView(strokeView);
    if (settings.fillType == MtlRenderSettingsType::Solid) {
        if (copied) bindTexture(1, targetTemp0.bgTexture);
        bind(pipelines.solidBlend(blendMethod));
        drawMeshSolid(context, &rdata->stroke.bboxMesh, rdata->stroke.solid.colorIdx);
    } else {
        bindPaint(settings.paintIdx);
        bindTexture(0, settings.gradientData.binding);
        if (copied) bindTexture(1, targetTemp0.bgTexture);
        bind(_gradientBlendPipeline(pipelines, settings.fillType, blendMethod));
        drawMesh(context, &rdata->stroke.bboxMesh);
    }
};

void MtlCompositor::drawImage(MtlContext& context, MtlImage* rdata)
{
    if (rdata->viewport.invalid() || !rdata->binding.texture) return;
    MtlRenderSettings& settings = rdata->setting;
    setScissorRegion(rdata->viewport);
    // draw image
    [renderPassEncoder setStencilReferenceValue:0];
    bindView();
    bindPaint(settings.paintIdx);
    bindTexture(0, rdata->binding);
    bind(pipelines.get(MtlPipe::ImageDirect));
    drawMeshImage(context, &rdata->mesh);
}

void MtlCompositor::blendImage(MtlContext& context, MtlImage* rdata, BlendMethod blendMethod)
{
    if (rdata->viewport.invalid() || !rdata->binding.texture) return;
    MtlRenderSettings& settings = rdata->setting;
    auto copied = copyBackdrop(context, blendMethod);
    setScissorRegion(rdata->viewport);
    // setup stencil rules
    [renderPassEncoder setStencilReferenceValue:255];
    bindView();
    bind(pipelines.get(MtlPipe::Direct));
    drawMeshImage(context, &rdata->mesh);
    // blend image
    [renderPassEncoder setStencilReferenceValue:0];
    bindView();
    bindPaint(settings.paintIdx);
    bindTexture(0, rdata->binding);
    if (copied) bindTexture(1, targetTemp0.bgTexture);
    bind(pipelines.imageBlend(blendMethod));
    drawMeshImage(context, &rdata->mesh);
};

void MtlCompositor::clipImage(MtlContext& context, MtlImage* rdata, BlendMethod blendMethod)
{
    if (rdata->viewport.invalid() || !rdata->binding.texture) return;
    MtlRenderSettings& settings = rdata->setting;
    setScissorRegion(rdata->viewport);
    // setup stencil rules
    [renderPassEncoder setStencilReferenceValue:255];
    bindView();
    bind(pipelines.get(MtlPipe::Direct));
    drawMeshImage(context, &rdata->mesh);
    // merge depth and stencil buffer
    [renderPassEncoder setStencilReferenceValue:0];
    bindDepth(128);
    bind(pipelines.get(MtlPipe::MergeDepthStencil));
    drawMeshImage(context, &rdata->mesh);
    // draw image
    [renderPassEncoder setStencilReferenceValue:0];
    bindView();
    bindPaint(settings.paintIdx);
    bindTexture(0, rdata->binding);
    bind(blendMethod == BlendMethod::Normal ? pipelines.get(MtlPipe::Image) : pipelines.imageBlend(blendMethod));
    drawMeshImage(context, &rdata->mesh);
}

void MtlCompositor::drawScene(MtlContext& context, MtlRenderTarget* scene, MtlCompose* compose, MtlMesh& mesh)
{
    // draw scene
    setScissorRegion(compose->aabb);
    [renderPassEncoder setStencilReferenceValue:0];
    bindTexture(0, scene->bgTexture);
    bindOpacity(compose->opacity);
    bind(pipelines.get(MtlPipe::Scene));
    drawMeshImage(context, &mesh);
}

void MtlCompositor::blendScene(MtlContext& context, MtlRenderTarget* scene, MtlCompose* compose, MtlMesh& mesh)
{
    auto copied = copyBackdrop(context, compose->blend);
    // blend scene
    setScissorRegion(compose->aabb);
    [renderPassEncoder setStencilReferenceValue:0];
    bindTexture(0, scene->bgTexture);
    if (copied) bindTexture(1, targetTemp0.bgTexture);
    bindOpacity(compose->opacity);
    bind(pipelines.sceneBlend(compose->blend));
    drawMeshImage(context, &mesh);
}

void MtlCompositor::markupClipPath(MtlContext& context, MtlShape* rdata)
{
    setScissorRegion(rdata->viewport);
    // markup stencil
    if (rdata->stroke.mesh.vbuffer.count > 0) {
        bindStrokeView(rdata->strokeViewMatIdx);
        [renderPassEncoder setStencilReferenceValue:255];
        bind(pipelines.get(MtlPipe::Direct));
        drawMesh(context, &rdata->stroke.mesh);
    } else if (rdata->shape.mesh.vbuffer.count > 0) {
        bindView();
        auto& stencilPipeline = (rdata->fillRule == FillRule::NonZero) ? pipelines.get(MtlPipe::Nonzero) : pipelines.get(MtlPipe::Evenodd);
        [renderPassEncoder setStencilReferenceValue:0];
        bind(stencilPipeline);
        drawMesh(context, &rdata->shape.mesh);
    }
}

void MtlCompositor::renderClipPath(MtlContext& context, MtlPaint* paint)
{
    auto rdata0 = (MtlShape*)paint->clips[0];
    // markup stencil
    markupClipPath(context, rdata0);
    // copy stencil to depth
    [renderPassEncoder setStencilReferenceValue:0];
    bindView();
    bindDepth(128);
    bind(pipelines.get(MtlPipe::CopyStencilToDepth));
    drawMesh(context, &rdata0->bboxMesh);
    // merge clip paths with AND logic
    for (auto p = paint->clips.begin() + 1; p < paint->clips.end(); ++p) {
        auto rdata = (MtlShape*)(*p);
        // markup stencil
        markupClipPath(context, rdata);
        // copy stencil to depth (clear stencil)
        [renderPassEncoder setStencilReferenceValue:0];
        bindView();
        bindDepth(190);
        bind(pipelines.get(MtlPipe::CopyStencilToDepthInterm));
        drawMesh(context, &rdata->bboxMesh);
        // copy depth to stencil
        [renderPassEncoder setStencilReferenceValue:1];
        bindDepth(190);
        bind(pipelines.get(MtlPipe::CopyDepthToStencil));
        drawMesh(context, &rdata->bboxMesh);
        // clear depth current (keep stencil)
        [renderPassEncoder setStencilReferenceValue:0];
        bindDepth(255);
        bind(pipelines.get(MtlPipe::ClearDepth));
        drawMesh(context, &rdata->bboxMesh);
        // clear depth original (keep stencil)
        [renderPassEncoder setStencilReferenceValue:0];
        bindDepth(255);
        bind(pipelines.get(MtlPipe::ClearDepth));
        drawMesh(context, &rdata0->bboxMesh);
        // copy stencil to depth (clear stencil)
        [renderPassEncoder setStencilReferenceValue:0];
        bindDepth(128);
        bind(pipelines.get(MtlPipe::CopyStencilToDepth));
        drawMesh(context, &rdata->bboxMesh);
    }
}

void MtlCompositor::clearClipPath(MtlContext& context, MtlPaint* paint)
{
    setScissor(0, 0, currentTargetState->bounds.w(), currentTargetState->bounds.h());
    // get render data
    ARRAY_FOREACH(p, paint->clips) {
        MtlShape* rdata = (MtlShape*)(*p);
        // set transformations
        [renderPassEncoder setStencilReferenceValue:0];
        bindView();
        bindDepth(255);
        bind(pipelines.get(MtlPipe::ClearDepth));
        drawMesh(context, &rdata->bboxMesh);
    }
}

bool MtlCompositor::motionBlur(MtlContext& context, const MtlRenderTargetState& targetState, const RenderEffectMotionBlur* motion, MtlMesh& mesh)
{
    auto effect = (MtlRenderEffect*)motion->rd;
    auto aabb = targetState.bounds;
    auto dst = targetState.target;

    // The final clipped region is only known at composition time.
    // Clamp bilinear sampling to this region within the full-sized texture.
    float region[4] = {float(aabb.x()), float(aabb.y()), float(aabb.w()), float(aabb.h())};
    memcpy(effect->params + 4, region, sizeof(region));

    copyEffectTexture(context, &targetTemp0, targetState);
    beginRenderPass(context, commandBuffer, dst);
    {
        setScissor(0, 0, aabb.w(), aabb.h());
        bindTexture(0, targetTemp0.bgTextureLinear);
        bindParams(effect->params);
        bind(pipelines.get(MtlPipe::EffectMotionBlur));
        drawMeshImage(context, &mesh);
    }
    endRenderPass();

    return true;
}

bool MtlCompositor::gaussianBlur(MtlContext& context, const MtlRenderTargetState& targetState, const RenderEffectGaussianBlur* blur, MtlMesh& mesh)
{
    auto effect = (MtlRenderEffect*)blur->rd;
    auto aabb = targetState.bounds;
    auto dst = targetState.target;

    copyEffectTexture(context, &targetTemp0, targetState, true);
    if (blur->direction == 0) {  // both
        beginRenderPass(context, commandBuffer, &targetTemp1, true);
        {
            setScissor(aabb.x(), aabb.y(), aabb.w(), aabb.h());
            bindTexture(0, targetTemp0.bgTexture);
            bindParams(effect->params);
            bind(pipelines.get(MtlPipe::EffectGaussianHorz));
            drawMeshImage(context, &meshDataBlit);
        }
        endRenderPass();
        beginRenderPass(context, commandBuffer, dst);
        {
            setScissor(0, 0, aabb.w(), aabb.h());
            bindTexture(0, targetTemp1.bgTexture);
            bindParams(effect->params);
            bind(pipelines.get(MtlPipe::EffectGaussianVert));
            drawMeshImage(context, &mesh);
        }
        endRenderPass();
    } else if (blur->direction == 1) {  // horizontal
        beginRenderPass(context, commandBuffer, dst);
        {
            setScissor(0, 0, aabb.w(), aabb.h());
            bindTexture(0, targetTemp0.bgTexture);
            bindParams(effect->params);
            bind(pipelines.get(MtlPipe::EffectGaussianHorz));
            drawMeshImage(context, &mesh);
        }
        endRenderPass();
    } else if (blur->direction == 2) {  // vertical
        beginRenderPass(context, commandBuffer, dst);
        {
            setScissor(0, 0, aabb.w(), aabb.h());
            bindTexture(0, targetTemp0.bgTexture);
            bindParams(effect->params);
            bind(pipelines.get(MtlPipe::EffectGaussianVert));
            drawMeshImage(context, &mesh);
        }
        endRenderPass();
    }

    return true;
}

bool MtlCompositor::dropShadow(MtlContext& context, const MtlRenderTargetState& targetState, const RenderEffectDropShadow* shadow, MtlMesh& mesh)
{
    auto effect = (MtlRenderEffect*)shadow->rd;
    auto aabb = targetState.bounds;
    auto dst = targetState.target;

    copyEffectTexture(context, &targetTemp1, targetState, true);
    if (!tvg::zero(shadow->sigma)) {
        // horizontal
        beginRenderPass(context, commandBuffer, &targetTemp0, true);
        {
            setScissor(aabb.x(), aabb.y(), aabb.w(), aabb.h());
            bindTexture(0, targetTemp1.bgTexture);
            bindParams(effect->params);
            bind(pipelines.get(MtlPipe::EffectGaussianHorz));
            drawMeshImage(context, &meshDataBlit);
        }
        endRenderPass();
        // vertical
        beginRenderPass(context, commandBuffer, &targetTemp1);
        {
            setScissor(aabb.x(), aabb.y(), aabb.w(), aabb.h());
            bindTexture(0, targetTemp0.bgTexture);
            bindParams(effect->params);
            bind(pipelines.get(MtlPipe::EffectGaussianVert));
            drawMeshImage(context, &meshDataBlit);
        }
        endRenderPass();
    }
    // drop shadow
    copyEffectTexture(context, &targetTemp0, targetState);
    beginRenderPass(context, commandBuffer, dst);
    {
        setScissor(0, 0, aabb.w(), aabb.h());
        bindTexture(0, targetTemp0.bgTexture);
        bindTexture(1, targetTemp1.bgTexture);
        bindParams(effect->params);
        bind(pipelines.get(MtlPipe::EffectDropShadow));
        drawMeshImage(context, &mesh);
    }
    endRenderPass();
    return true;
}

bool MtlCompositor::fillEffect(MtlContext& context, const MtlRenderTargetState& targetState, const RenderEffectFill* fill, MtlMesh& mesh)
{
    auto effect = (MtlRenderEffect*)fill->rd;
    auto aabb = targetState.bounds;
    auto dst = targetState.target;

    copyEffectTexture(context, &targetTemp0, targetState);
    beginRenderPass(context, commandBuffer, dst);
    {
        setScissor(0, 0, aabb.w(), aabb.h());
        bindTexture(0, targetTemp0.bgTexture);
        bindParams(effect->params);
        bind(pipelines.get(MtlPipe::EffectFill));
        drawMeshImage(context, &mesh);
    }
    endRenderPass();

    return true;
}

bool MtlCompositor::tintEffect(MtlContext& context, const MtlRenderTargetState& targetState, const RenderEffectTint* tint, MtlMesh& mesh)
{
    auto effect = (MtlRenderEffect*)tint->rd;
    auto aabb = targetState.bounds;
    auto dst = targetState.target;

    copyEffectTexture(context, &targetTemp0, targetState);
    beginRenderPass(context, commandBuffer, dst);
    {
        setScissor(0, 0, aabb.w(), aabb.h());
        bindTexture(0, targetTemp0.bgTexture);
        bindParams(effect->params);
        bind(pipelines.get(MtlPipe::EffectTint));
        drawMeshImage(context, &mesh);
    }
    endRenderPass();

    return true;
}

bool MtlCompositor::tritoneEffect(MtlContext& context, const MtlRenderTargetState& targetState, const RenderEffectTritone* tritone, MtlMesh& mesh)
{
    auto effect = (MtlRenderEffect*)tritone->rd;
    auto aabb = targetState.bounds;
    auto dst = targetState.target;

    copyEffectTexture(context, &targetTemp0, targetState);
    beginRenderPass(context, commandBuffer, dst);
    {
        setScissor(0, 0, aabb.w(), aabb.h());
        bindTexture(0, targetTemp0.bgTexture);
        bindParams(effect->params);
        bind(pipelines.get(MtlPipe::EffectTritone));
        drawMeshImage(context, &mesh);
    }
    endRenderPass();

    return true;
}
