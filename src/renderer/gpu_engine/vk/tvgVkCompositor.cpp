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
#include "tvgVkCompositor.h"

#define VK_UNIFORM_STRIDE 256

// see shaders/common.glsl
struct VkPushConstants
{
    float params[12];
    float value;
};

static VkPipeline _gradientPipeline(VkPipelines& pipelines, VkContext& context, VkRenderSettingsType type, bool convex)
{
    switch (type) {
        case VkRenderSettingsType::Linear: return pipelines.get(context, convex ? VkPipe::LinearConv : VkPipe::Linear);
        case VkRenderSettingsType::Radial: return pipelines.get(context, convex ? VkPipe::RadialConv : VkPipe::Radial);
        default: return pipelines.get(context, convex ? VkPipe::ConicConv : VkPipe::Conic);
    }
}


static VkPipeline _gradientBlendPipeline(VkPipelines& pipelines, VkContext& context, VkRenderSettingsType type, BlendMethod blendMethod)
{
    switch (type) {
        case VkRenderSettingsType::Linear: return pipelines.blend(context, VkMaterial::Linear, blendMethod);
        case VkRenderSettingsType::Radial: return pipelines.blend(context, VkMaterial::Radial, blendMethod);
        default: return pipelines.blend(context, VkMaterial::Conic, blendMethod);
    }
}


static VkDeviceSize _align(VkDeviceSize value, VkDeviceSize alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}


void VkCompositor::initialize(VkContext& context, uint32_t width, uint32_t height)
{
    ctx = &context;
    pipelines.initialize(context);
    resize(context, width, height);
    meshDataBlit.blitBox();
    meshDataPresent.blitBox();
    for (uint32_t i = 0; i < 3; ++i) meshDataPresent.tbuffer.push(meshDataPresent.tbuffer[i]);
}


void VkCompositor::release(VkContext& context)
{
    resize(context, 0, 0);
    for (auto& f : frames) {
        context.release(f.uniforms);
        context.release(f.buffer);
    }
    frame = VK_NULL_HANDLE;
    stageBufferGeometry = {};
    stageBufferSolidColor = {};
    stageBufferPaint = {};
    stageBufferViewMat = {};
    pipelines.release(context);
    ctx = nullptr;
}


void VkCompositor::resize(VkContext& context, uint32_t width, uint32_t height)
{
    if (this->width != width || this->height != height) {
        targetTemp1.release(context);
        targetTemp0.release(context);
        for (auto& attachment : depthStencilAttachments) {
            context.release(attachment.texture);
        }
        depthStencilAttachments.clear();
        for (auto& attachment : transientAttachments) {
            context.release(attachment.texture);
        }
        transientAttachments.clear();
    }

    this->width = width;
    this->height = height;

    if (width > 0 && height > 0 && !targetTemp0.current.texture.valid()) {
        targetTemp0.initialize(context, width, height);
        targetTemp1.initialize(context, width, height);
    }
}

/************************************************************************/
/* Bindings                                                             */
/************************************************************************/

void VkCompositor::setViewport(uint32_t width, uint32_t height)
{
    VkViewport viewport{0.0f, 0.0f, float(width), float(height), 0.0f, 1.0f};
    ctx->vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
}


void VkCompositor::setScissor(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    VkRect2D rect{{x, y}, {w, h}};
    ctx->vkCmdSetScissor(commandBuffer, 0, 1, &rect);
}


void VkCompositor::setScissorRegion(const RenderRegion& region)
{
    const auto& bounds = currentTargetState->bounds;
    const RenderRegion rect{
        tvg::clamp(region.xMin, bounds.xMin, bounds.xMax) - bounds.xMin, tvg::clamp(region.yMin, bounds.yMin, bounds.yMax) - bounds.yMin,
        tvg::clamp(region.xMax, bounds.xMin, bounds.xMax) - bounds.xMin, tvg::clamp(region.yMax, bounds.yMin, bounds.yMax) - bounds.yMin};
    setScissor(rect.x(), rect.y(), rect.w(), rect.h());
}


void VkCompositor::resetBindings()
{
    currentPipeline = VK_NULL_HANDLE;
    for (auto& set : currentSets) set = VK_NULL_HANDLE;
    viewOffset = paintOffset = 0;
    uniformsDirty = true;
    // every statically used push constant must hold a value
    VkPushConstants constants{};
    ctx->vkCmdPushConstants(commandBuffer, ctx->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants), &constants);
    ctx->vkCmdBindIndexBuffer(commandBuffer, frame, indexBase, VK_INDEX_TYPE_UINT32);
}


void VkCompositor::bind(VkPipeline pipeline)
{
    if (currentPipeline == pipeline) return;
    currentPipeline = pipeline;
    ctx->vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
}


void VkCompositor::bindView()
{
    if (viewOffset == 0) return;
    viewOffset = 0;
    uniformsDirty = true;
}


void VkCompositor::bindStrokeView(uint32_t index)
{
    auto offset = (index + 1) * VK_UNIFORM_STRIDE;
    if (viewOffset == offset) return;
    viewOffset = offset;
    uniformsDirty = true;
}


void VkCompositor::bindPaint(uint32_t index)
{
    auto offset = uint32_t(paintBase + index * VK_UNIFORM_STRIDE);
    if (paintOffset == offset) return;
    paintOffset = offset;
    uniformsDirty = true;
}


void VkCompositor::bindTexture(uint32_t set, VkDescriptorSet descriptor)
{
    if (currentSets[set] == descriptor) return;
    currentSets[set] = descriptor;
    ctx->vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pipelineLayout, set, 1, &descriptor, 0, nullptr);
}


void VkCompositor::bindVertex(uint32_t binding, VkDeviceSize offset)
{
    offset += geometryBase;
    ctx->vkCmdBindVertexBuffers(commandBuffer, binding, 1, &frame, &offset);
}


void VkCompositor::bindColor(VkDeviceSize offset)
{
    offset += colorBase;
    ctx->vkCmdBindVertexBuffers(commandBuffer, 2, 1, &frame, &offset);
}


void VkCompositor::pushValue(float value)
{
    ctx->vkCmdPushConstants(commandBuffer, ctx->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, offsetof(VkPushConstants, value), sizeof(float), &value);
}


void VkCompositor::pushParams(const float* params)
{
    ctx->vkCmdPushConstants(commandBuffer, ctx->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VkPushConstants::params), params);
}


void VkCompositor::setStencilReference(uint32_t reference)
{
    ctx->vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
}


void VkCompositor::drawIndexed(uint32_t count, VkDeviceSize offset)
{
    if (uniformsDirty) {
        auto& uniforms = frames[ctx->frameIndex()].uniforms.set;
        const uint32_t offsets[]{viewOffset, paintOffset};
        ctx->vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pipelineLayout, 0, 1, &uniforms, 2, offsets);
        uniformsDirty = false;
    }
    ctx->vkCmdDrawIndexed(commandBuffer, count, 1, uint32_t(offset / sizeof(uint32_t)), 0, 0);
}

/************************************************************************/
/* Render Passes                                                        */
/************************************************************************/

// The attachments are shared by the targets of one size, they go with the last target of that size.
void VkCompositor::evict(VkContext& context, const VkRenderTargetPool& targets)
{
    auto unused = [&](uint32_t w, uint32_t h) {
        if (w == width && h == height) return false;
        for (auto& p : targets.list) {
            if (p->width == w && p->height == h) return false;
        }
        return true;
    };
    for (auto attachments : {&depthStencilAttachments, &transientAttachments}) {
        for (uint32_t i = 0; i < attachments->count;) {
            auto& attachment = (*attachments)[i];
            if (!unused(attachment.width, attachment.height)) {
                ++i;
                continue;
            }
            context.release(attachment.texture);
            attachment = attachments->last();
            attachments->pop();
        }
    }
}


VkImageView VkCompositor::getDepthStencilView(VkContext& context, uint32_t width, uint32_t height)
{
    for (auto& attachment : depthStencilAttachments) {
        if (attachment.width == width && attachment.height == height) return attachment.texture.view;
    }
    // cleared on load and discarded on store, so tile based GPUs never back it with memory
    VkTexture texture;
    if (!context.createTexture(texture, width, height, context.depthStencilFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_SAMPLE_COUNT_4_BIT, true)) return VK_NULL_HANDLE;
    depthStencilAttachments.push({width, height, texture});
    return texture.view;
}


VkImageView VkCompositor::getTransientView(VkContext& context, uint32_t width, uint32_t height)
{
    for (auto& attachment : transientAttachments) {
        if (attachment.width == width && attachment.height == height) return attachment.texture.view;
    }
    VkTexture texture;
    if (!context.createTexture(texture, width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_SAMPLE_COUNT_4_BIT, true)) return VK_NULL_HANDLE;
    transientAttachments.push({width, height, texture});
    return texture.view;
}


void VkCompositor::copyTexture(VkContext& context, VkTexture& src, VkTexture& dst, uint32_t width, uint32_t height, int32_t x, int32_t y)
{
    context.transition(commandBuffer, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstOffset = {x, y, 0};
    region.extent = {width, height, 1};
    context.vkCmdCopyImage(commandBuffer, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    context.transition(commandBuffer, src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, dst, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}


bool VkCompositor::copyBackdrop(VkContext& context, BlendMethod method)
{
    if (VkPipelines::hardware(method)) return false;
    endRenderPass();
    auto src = currentTargetState->target;
    copyTexture(context, src->current.texture, targetTemp0.current.texture, src->width, src->height, 0, 0);
    beginRenderPassMS(context, commandBuffer, *currentTargetState, false);
    return true;
}


void VkCompositor::beginRenderPassMS(VkContext& context, VkCommandBuffer commandBuffer, const VkRenderTargetState& targetState, bool clearBuffer)
{
    auto target = targetState.target;
    if (renderPass && currentTargetState == &targetState && !clearBuffer) return;
    // we must to end render pass first
    endRenderPass();
    currentTargetState = &targetState;
    this->commandBuffer = commandBuffer;

    auto restore = !clearBuffer && target->flip(context);
    if (restore) context.transition(commandBuffer, target->previous.texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    auto depthStencil = getDepthStencilView(context, target->width, target->height);
    VkClearValue clearValues[3]{};
    clearValues[2].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    info.renderPass = pipelines.passMS;
    info.framebuffer = target->getFramebuffer(context, pipelines, getTransientView(context, target->width, target->height), depthStencil);
    info.renderArea = {{0, 0}, {target->width, target->height}};
    info.clearValueCount = 3;
    info.pClearValues = clearValues;
    if (!info.framebuffer) return;
    context.vkCmdBeginRenderPass(commandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    renderPass = true;
    target->current.texture.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;  // the final layout of the resolve

    resetBindings();
    setViewport(target->width, target->height);
    if (restore) {
        setScissor(0, 0, target->width, target->height);
        setStencilReference(0);
        bindTexture(1, target->previous.bgTexture.set);
        bind(pipelines.get(context, VkPipe::Restore));
        drawMeshImage(context, &meshDataBlit);
    }
    bindVertex(0, targetState.transformOffset);
}


void VkCompositor::beginRenderPass(VkContext& context, VkCommandBuffer commandBuffer, VkRenderTarget* target, bool clearBuffer)
{
    assert(!renderPass);
    currentTargetState = nullptr;
    this->commandBuffer = commandBuffer;
    if (!clearBuffer) context.transition(commandBuffer, target->current.texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    VkClearValue clearValue{};
    VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    info.renderPass = pipelines.passSS[clearBuffer ? 0 : 1];
    info.framebuffer = target->getFramebufferSS(context, pipelines);
    info.renderArea = {{0, 0}, {target->width, target->height}};
    info.clearValueCount = 1;
    info.pClearValues = &clearValue;
    if (!info.framebuffer) return;
    context.vkCmdBeginRenderPass(commandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    renderPass = true;
    target->current.texture.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    resetBindings();
    setViewport(target->width, target->height);
}


void VkCompositor::endRenderPass()
{
    if (renderPass) {
        ctx->vkCmdEndRenderPass(commandBuffer);
        renderPass = false;
    }
}


void VkCompositor::reset(VkContext& context)
{
    currentTargetState = nullptr;
    stageBufferGeometry.clear();
    stageBufferSolidColor.clear();
    stageBufferPaint.clear();
    stageBufferViewMat.clear();
}


bool VkCompositor::flush(VkContext& context)
{
    context.commands();  // the frame slot must be open to reuse its buffer
    stageBufferGeometry.append(&meshDataBlit);
    stageBufferGeometry.append(&meshDataPresent);

    paintBase = VK_UNIFORM_STRIDE * (1 + stageBufferViewMat.count);
    geometryBase = paintBase + VK_UNIFORM_STRIDE * stageBufferPaint.count;
    colorBase = geometryBase + _align(stageBufferGeometry.vbuffer.count, 4);
    indexBase = colorBase + stageBufferSolidColor.vbuffer.count * sizeof(RenderColor);
    const auto size = indexBase + stageBufferGeometry.ibuffer.count;

    auto& f = frames[context.frameIndex()];
    if (f.buffer.size < size) {
        context.release(f.buffer);
        auto usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if (!context.createBuffer(f.buffer, size + size / 2, usage, true)) {
            frame = VK_NULL_HANDLE;
            return false;
        }
        if (!f.uniforms.set) f.uniforms = context.allocate(context.layoutUniforms);
        VkDescriptorBufferInfo infos[]{{f.buffer.buffer, 0, sizeof(VkShaderTypeMat4x4f)}, {f.buffer.buffer, 0, sizeof(VkShaderTypeVec4f) + sizeof(VkShaderTypeGradSettings)}};
        VkWriteDescriptorSet writes[2]{{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
        for (uint32_t i = 0; i < 2; ++i) {
            writes[i].dstSet = f.uniforms.set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            writes[i].pBufferInfo = &infos[i];
        }
        context.vkUpdateDescriptorSets(context.device, 2, writes, 0, nullptr);
    }
    frame = f.buffer.buffer;

    auto dst = f.buffer.mapped;
    VkShaderTypeMat4x4f viewMat(width, height);
    memcpy(dst, &viewMat, sizeof(viewMat));
    if (stageBufferViewMat.count > 0) memcpy(dst + VK_UNIFORM_STRIDE, stageBufferViewMat.data, stageBufferViewMat.count * VK_UNIFORM_STRIDE);
    if (stageBufferPaint.count > 0) memcpy(dst + paintBase, stageBufferPaint.data, stageBufferPaint.count * VK_UNIFORM_STRIDE);
    if (stageBufferGeometry.vbuffer.count > 0) memcpy(dst + geometryBase, stageBufferGeometry.vbuffer.data, stageBufferGeometry.vbuffer.count);
    if (stageBufferSolidColor.vbuffer.count > 0) memcpy(dst + colorBase, stageBufferSolidColor.vbuffer.data, stageBufferSolidColor.vbuffer.count * sizeof(RenderColor));
    if (stageBufferGeometry.ibuffer.count > 0) memcpy(dst + indexBase, stageBufferGeometry.ibuffer.data, stageBufferGeometry.ibuffer.count);
    context.flush(f.buffer, size);

    // the gradient ramps uploaded during the update must land before any draw
    context.ramps.flush(context);
    return true;
}

/************************************************************************/
/* Staging                                                              */
/************************************************************************/

void VkCompositor::requestQuad(VkMesh& mesh, const VkRenderTargetState& src, const VkRenderTargetState& dst, const VkRenderTargetState* mask)
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


void VkCompositor::requestScene(VkRenderTargetState& targetState, VkMesh& mesh, VkMesh& effectMesh, const VkRenderTargetState* dst, const VkRenderTargetState* mask, bool effects)
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
        VkRenderTargetState scratch{&targetTemp0, {0, 0, int32_t(width), int32_t(height)}};
        requestQuad(effectMesh, scratch, targetState);
    }
}


void VkCompositor::copyEffectTexture(VkContext& context, VkRenderTarget* dst, const VkRenderTargetState& src, bool clear)
{
    if (clear) {
        context.transition(commandBuffer, dst->current.texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkClearColorValue color{};
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        context.vkCmdClearColorImage(commandBuffer, dst->current.texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
    }
    copyTexture(context, src.target->current.texture, dst->current.texture, src.bounds.w(), src.bounds.h(), src.bounds.x(), src.bounds.y());
}


void VkCompositor::requestShape(VkShape* shape)
{
    stageBufferGeometry.append(shape);

    auto& shapeSettings = shape->shape.setting;
    auto& shapeSolid = shape->shape.solid;
    if (shapeSettings.fillType == VkRenderSettingsType::Solid) shapeSolid.colorIdx = stageBufferSolidColor.append(shapeSolid.packedColor());
    else {
        stageBufferPaint.push(shapeSettings.settings);
        shapeSettings.paintIdx = stageBufferPaint.count - 1;
    }

    if (!shape->stroke.mesh.vbuffer.empty()) {
        Matrix viewMatrix{2.0f / width, 0.0f, -1.0f, 0.0f, -2.0f / height, 1.0f, 0.0f, 0.0f, 1.0f};
        VkShaderTypeMat4x4fBlock strokeViewMat{{viewMatrix * shape->transform}, {}};
        // Test the clip depth during the stroke color draw.
        strokeViewMat.matrix.mat[14] = 128.0f / 255.0f;
        stageBufferViewMat.push(strokeViewMat);
        shape->strokeViewMatIdx = stageBufferViewMat.count - 1;
    }

    if (shape->stroke.setting.valid && !shape->stroke.mesh.vbuffer.empty()) {
        auto& strokeSettings = shape->stroke.setting;
        auto& strokeSolid = shape->stroke.solid;
        if (strokeSettings.fillType == VkRenderSettingsType::Solid) strokeSolid.colorIdx = stageBufferSolidColor.append(strokeSolid.packedColor());
        else {
            stageBufferPaint.push(strokeSettings.settings);
            strokeSettings.paintIdx = stageBufferPaint.count - 1;
        }
    }
    for (auto& p : shape->clips) {
        requestShape((VkShape*)p);
    }
}


void VkCompositor::requestImage(VkPicture* image)
{
    stageBufferGeometry.append(image);
    stageBufferPaint.push(image->setting.settings);
    image->setting.paintIdx = stageBufferPaint.count - 1;
    for (auto& p : image->clips) {
        requestShape((VkShape*)p);
    }
}


void VkCompositor::requestSolidBatch(const Array<VkShape*>& renderShapes, VkSolidBatchRange& range)
{
    stageBufferGeometry.appendSolidBatch(renderShapes, stageBufferSolidColor, range);
    range.viewport = renderShapes[0]->viewport;
}


void VkCompositor::requestImageBatch(const Array<VkPicture*>& renderImages, VkImageBatchRange& range)
{
    stageBufferGeometry.appendImageBatch(renderImages, range);
    auto image = renderImages[0];
    stageBufferPaint.push(image->setting.settings);
    image->setting.paintIdx = stageBufferPaint.count - 1;
    range.viewport = image->viewport;
}


void VkCompositor::requestStencilBatch(const Array<VkShape*>& renderShapes, VkStencilBatchRange& range)
{
    stageBufferGeometry.appendStencilBatch(renderShapes, range);
    range.viewport = renderShapes[0]->viewport;
    range.fillRule = renderShapes[0]->fillRule;
    range.solidOnly = true;

    uint32_t pendingColorCount = 0;
    auto colorsStarted = false;

    for (auto& p : renderShapes) {
        auto rdata = p;
        auto& settings = rdata->shape.setting;
        const auto count = rdata->bboxMesh.vbuffer.count;

        if (settings.fillType == VkRenderSettingsType::Solid) {
            if (!colorsStarted) {
                range.colorOffset = static_cast<size_t>(stageBufferSolidColor.vbuffer.count) * sizeof(RenderColor);
                if (pendingColorCount) stageBufferSolidColor.appendRepeated({}, pendingColorCount);
                colorsStarted = true;
            }
            stageBufferSolidColor.appendRepeated(rdata->shape.solid.packedColor(), count);
        } else {
            range.solidOnly = false;
            stageBufferPaint.push(settings.settings);
            settings.paintIdx = stageBufferPaint.count - 1;
            if (colorsStarted) stageBufferSolidColor.appendRepeated({}, count);
            else pendingColorCount += count;
        }
    }
}

/************************************************************************/
/* Rendering                                                            */
/************************************************************************/

void VkCompositor::renderShape(VkContext& context, VkShape* rdata, BlendMethod blendMethod)
{
    auto clipped = !rdata->clips.empty();
    if (clipped) {
        renderClipPath(context, rdata);
        // Clipped custom blends retain normal compositing.
        if (!VkPipelines::hardware(blendMethod)) blendMethod = BlendMethod::Normal;
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


void VkCompositor::renderSolidBatch(const VkSolidBatchRange& range)
{
    setScissorRegion(range.viewport);
    setStencilReference(0);
    bindView();
    bind(pipelines.get(*ctx, VkPipe::SolidBatch));
    bindVertex(1, range.vertexOffset);
    bindColor(range.colorOffset);
    drawIndexed(range.indexCount, range.indexOffset);
}


void VkCompositor::renderImageBatch(VkPicture* image, const VkImageBatchRange& range)
{
    setScissorRegion(range.viewport);
    setStencilReference(0);
    bindView();
    bindPaint(image->setting.paintIdx);
    bindTexture(1, image->descriptor);
    bind(pipelines.get(*ctx, VkPipe::ImageDirect));
    bindVertex(1, range.vertexOffset);
    bindVertex(2, range.texCoordOffset);
    drawIndexed(range.indexCount, range.indexOffset);
}


void VkCompositor::renderStencilBatch(VkContext& context, const Array<VkShape*>& renderShapes, const VkStencilBatchRange& range)
{
    setScissorRegion(range.viewport);
    setStencilReference(0);
    bindView();
    bind(pipelines.get(context, range.fillRule == FillRule::NonZero ? VkPipe::NonZero : VkPipe::EvenOdd));
    bindVertex(1, range.stencil.vertexOffset);
    drawIndexed(range.stencil.indexCount, range.stencil.indexOffset);

    bindVertex(1, range.cover.vertexOffset);

    // Keep the common all-solid case identical to the original two-draw path.
    if (range.solidOnly) {
        bind(pipelines.get(context, VkPipe::SolidStencilBatch));
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
            const auto indexCount = rdata->bboxMesh.ibuffer.count;
            const uint64_t nextIndex = static_cast<uint64_t>(firstIndex) + indexCount;
            firstIndex = static_cast<uint32_t>(nextIndex);
            if (++p == end || settings.fillType != VkRenderSettingsType::Solid) break;
            rdata = *p;
        } while (rdata->shape.setting.fillType == VkRenderSettingsType::Solid);

        if (settings.fillType == VkRenderSettingsType::Solid) {
            if (!colorsBound) {
                bindColor(range.colorOffset);
                colorsBound = true;
            }
            bind(pipelines.get(context, VkPipe::SolidStencilBatch));
        } else {
            bindPaint(settings.paintIdx);
            bindTexture(1, settings.ramps(context));
            bind(_gradientPipeline(pipelines, context, settings.fillType, false));
        }
        drawIndexed(firstIndex - batchFirstIndex, range.cover.indexOffset + batchFirstIndex * sizeof(uint32_t));
    }
}


void VkCompositor::renderImage(VkContext& context, VkPicture* rdata, BlendMethod blendMethod)
{
    // apply clip path if necessary
    if (rdata->clips.count != 0) {
        renderClipPath(context, rdata);
        if (!VkPipelines::hardware(blendMethod)) blendMethod = BlendMethod::Normal;
        clipImage(context, rdata, blendMethod);
        clearClipPath(context, rdata);
    // use custom blending
    } else if (blendMethod != BlendMethod::Normal)
        blendImage(context, rdata, blendMethod);
    // use direct hardware blending
    else drawImage(context, rdata);
}


void VkCompositor::renderScene(VkContext& context, VkRenderTarget* scene, VkCompose* compose, VkMesh& mesh)
{
    // use custom blending
    if (compose->blend != BlendMethod::Normal)
        blendScene(context, scene, compose, mesh);
    // use direct hardware blending
    else drawScene(context, scene, compose, mesh);
}


void VkCompositor::composeScene(VkContext& context, VkRenderTarget* src, VkRenderTarget* mask, VkCompose* cmp, VkMesh& mesh)
{
    setScissorRegion(cmp->aabb);
    setStencilReference(0);
    bindTexture(1, src->current.bgTexture.set);
    bindTexture(2, mask->current.bgTexture.set);
    bind(pipelines.compose(context, cmp->method));
    bindVertex(3, mesh.toffset + 4 * sizeof(Point));
    drawMeshImage(context, &mesh);
}


void VkCompositor::blit(VkContext& context, VkCommandBuffer commandBuffer, VkRenderTarget* src, VkFramebuffer dst, VkFormat format, uint32_t width, uint32_t height, uint32_t turns, bool premultiplied, bool present)
{
    this->commandBuffer = commandBuffer;
    VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    info.renderPass = pipelines.blitPass(context, format, present);
    info.framebuffer = dst;
    info.renderArea = {{0, 0}, {width, height}};
    context.vkCmdBeginRenderPass(commandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    renderPass = true;
    currentTargetState = nullptr;
    resetBindings();
    setViewport(width, height);
    setScissor(0, 0, width, height);
    auto aligned = (turns & 1) ? (height == src->width && width == src->height) : (width == src->width && height == src->height);
    bindTexture(1, aligned ? src->current.bgTexture.set : src->getTextureLinear(context));
    bind(pipelines.blit(context, format, premultiplied));
    bindVertex(1, meshDataPresent.voffset);
    bindVertex(2, meshDataPresent.toffset + ((4 - turns) % 4) * sizeof(Point));
    drawIndexed(meshDataPresent.ibuffer.count, meshDataPresent.ioffset);
    endRenderPass();
}


void VkCompositor::drawMesh(VkContext& context, VkMesh* meshData)
{
    bindVertex(1, meshData->voffset);
    drawIndexed(meshData->ibuffer.count, meshData->ioffset);
}


void VkCompositor::drawMeshSolid(VkContext& context, VkMesh* meshData, uint32_t solidColorInd)
{
    bindVertex(1, meshData->voffset);
    bindColor(solidColorInd * sizeof(RenderColor));
    drawIndexed(meshData->ibuffer.count, meshData->ioffset);
}


void VkCompositor::drawMeshImage(VkContext& context, VkMesh* meshData)
{
    bindVertex(1, meshData->voffset);
    bindVertex(2, meshData->toffset);
    drawIndexed(meshData->ibuffer.count, meshData->ioffset);
}


void VkCompositor::drawShape(VkContext& context, VkShape* rdata)
{
    if (!rdata->shape.setting.valid || rdata->shape.mesh.vbuffer.empty() || rdata->viewport.invalid()) return;
    auto& settings = rdata->shape.setting;
    auto convex = rdata->convex;
    auto mesh = rdata->convex ? &rdata->shape.mesh : &rdata->bboxMesh;

    setScissorRegion(rdata->viewport);
    bindView();

    // setup stencil rules
    if (!convex) {
        setStencilReference(0);
        bind(pipelines.get(context, rdata->fillRule == FillRule::NonZero ? VkPipe::NonZero : VkPipe::EvenOdd));
        // draw to stencil (first pass)
        drawMesh(context, &rdata->shape.mesh);
    }

    // setup fill rules
    setStencilReference(0);
    if (settings.fillType == VkRenderSettingsType::Solid) {
        bind(pipelines.get(context, convex ? VkPipe::SolidConv : VkPipe::Solid));
        drawMeshSolid(context, mesh, rdata->shape.solid.colorIdx);
    } else {
        bindPaint(settings.paintIdx);
        bindTexture(1, settings.ramps(context));
        bind(_gradientPipeline(pipelines, context, settings.fillType, convex));
        drawMesh(context, mesh);
    }
}


void VkCompositor::blendShape(VkContext& context, VkShape* rdata, BlendMethod blendMethod)
{
    if (!rdata->shape.setting.valid || rdata->shape.mesh.vbuffer.empty() || rdata->viewport.invalid()) return;
    auto& settings = rdata->shape.setting;
    auto copied = copyBackdrop(context, blendMethod);
    // render shape with blend settings
    setScissorRegion(rdata->viewport);
    bindView();
    // setup stencil rules
    setStencilReference(0);
    bind(pipelines.get(context, rdata->fillRule == FillRule::NonZero ? VkPipe::NonZero : VkPipe::EvenOdd));
    // draw to stencil (first pass)
    drawMesh(context, &rdata->shape.mesh);
    // setup fill rules
    setStencilReference(0);
    if (settings.fillType == VkRenderSettingsType::Solid) {
        // the custom blend shader declares the primary texture for every material
        if (copied) {
            bindTexture(1, targetTemp0.current.bgTexture.set);
            bindTexture(2, targetTemp0.current.bgTexture.set);
        }
        bind(pipelines.blend(context, VkMaterial::Solid, blendMethod));
        drawMeshSolid(context, &rdata->bboxMesh, rdata->shape.solid.colorIdx);
    } else {
        bindPaint(settings.paintIdx);
        bindTexture(1, settings.ramps(context));
        if (copied) bindTexture(2, targetTemp0.current.bgTexture.set);
        bind(_gradientBlendPipeline(pipelines, context, settings.fillType, blendMethod));
        drawMesh(context, &rdata->bboxMesh);
    }
}


void VkCompositor::clipShape(VkContext& context, VkShape* rdata, BlendMethod blendMethod)
{
    if (!rdata->shape.setting.valid || rdata->shape.mesh.vbuffer.empty() || rdata->viewport.invalid()) return;
    auto& settings = rdata->shape.setting;
    setScissorRegion(rdata->viewport);
    bindView();
    // setup stencil rules
    setStencilReference(0);
    bind(pipelines.get(context, rdata->fillRule == FillRule::NonZero ? VkPipe::NonZero : VkPipe::EvenOdd));
    // draw to stencil (first pass)
    drawMesh(context, &rdata->shape.mesh);
    // merge depth and stencil buffer
    setStencilReference(0);
    pushValue(128.0f / 255.0f);
    bind(pipelines.get(context, VkPipe::MergeDepthStencil));
    drawMesh(context, &rdata->bboxMesh);
    // setup fill rules
    setStencilReference(0);
    if (settings.fillType == VkRenderSettingsType::Solid) {
        bind(blendMethod == BlendMethod::Normal ? pipelines.get(context, VkPipe::Solid) : pipelines.blend(context, VkMaterial::Solid, blendMethod));
        drawMeshSolid(context, &rdata->bboxMesh, rdata->shape.solid.colorIdx);
    } else {
        bindPaint(settings.paintIdx);
        bindTexture(1, settings.ramps(context));
        bind(blendMethod == BlendMethod::Normal ? _gradientPipeline(pipelines, context, settings.fillType, false) : _gradientBlendPipeline(pipelines, context, settings.fillType, blendMethod));
        drawMesh(context, &rdata->bboxMesh);
    }
}


void VkCompositor::stencilStrokes(VkContext& context, VkShape* rdata)
{
    setStencilReference(255);
    bindStrokeView(rdata->strokeViewMatIdx);
    bind(pipelines.get(context, VkPipe::Direct));
    drawMesh(context, &rdata->stroke.mesh);
    if (!rdata->clips.empty()) {
        setStencilReference(0);
        bindView();
        pushValue(128.0f / 255.0f);
        bind(pipelines.get(context, VkPipe::MergeDepthStencil));
        drawMesh(context, &rdata->bboxMesh);
    }
    setStencilReference(0);
}


void VkCompositor::drawStrokes(VkContext& context, VkShape* rdata)
{
    if (!rdata->stroke.setting.valid || rdata->stroke.mesh.vbuffer.empty() || rdata->viewport.invalid()) return;
    auto& settings = rdata->stroke.setting;
    auto solid = settings.fillType == VkRenderSettingsType::Solid;
    auto direct = rdata->strokeOverlapFree || (rdata->stroke.solid.opacity == 255 && (solid ? rdata->stroke.solid.color.a == 255 : settings.opaque));
    auto clipped = !rdata->clips.empty();
    setScissorRegion(rdata->viewport);

    auto mesh = &rdata->stroke.mesh;
    VkPipeline pipeline;
    if (direct) {
        // Draw eligible stroke geometry with stencil tests and writes disabled.
        pipeline = clipped ? pipelines.clippedStroke(context, settings.fillType) : (solid ? pipelines.get(context, VkPipe::SolidConv) : _gradientPipeline(pipelines, context, settings.fillType, true));
    } else {
        stencilStrokes(context, rdata);
        mesh = &rdata->stroke.bboxMesh;
        pipeline = solid ? pipelines.get(context, VkPipe::Solid) : _gradientPipeline(pipelines, context, settings.fillType, false);
    }
    bindStrokeView(rdata->strokeViewMatIdx);
    bind(pipeline);
    if (solid) {
        drawMeshSolid(context, mesh, rdata->stroke.solid.colorIdx);
    } else {
        bindPaint(settings.paintIdx);
        bindTexture(1, settings.ramps(context));
        drawMesh(context, mesh);
    }
}


void VkCompositor::blendStrokes(VkContext& context, VkShape* rdata, BlendMethod blendMethod)
{
    if (!rdata->stroke.setting.valid || rdata->stroke.mesh.vbuffer.empty() || rdata->viewport.invalid()) return;
    auto& settings = rdata->stroke.setting;
    auto copied = copyBackdrop(context, blendMethod);
    setScissorRegion(rdata->viewport);
    stencilStrokes(context, rdata);
    // setup fill rules
    bindStrokeView(rdata->strokeViewMatIdx);
    if (settings.fillType == VkRenderSettingsType::Solid) {
        if (copied) {
            bindTexture(1, targetTemp0.current.bgTexture.set);
            bindTexture(2, targetTemp0.current.bgTexture.set);
        }
        bind(pipelines.blend(context, VkMaterial::Solid, blendMethod));
        drawMeshSolid(context, &rdata->stroke.bboxMesh, rdata->stroke.solid.colorIdx);
    } else {
        bindPaint(settings.paintIdx);
        bindTexture(1, settings.ramps(context));
        if (copied) bindTexture(2, targetTemp0.current.bgTexture.set);
        bind(_gradientBlendPipeline(pipelines, context, settings.fillType, blendMethod));
        drawMesh(context, &rdata->stroke.bboxMesh);
    }
}


void VkCompositor::drawImage(VkContext& context, VkPicture* rdata)
{
    if (rdata->viewport.invalid() || !rdata->descriptor) return;
    setScissorRegion(rdata->viewport);
    // draw image
    setStencilReference(0);
    bindView();
    bindPaint(rdata->setting.paintIdx);
    bindTexture(1, rdata->descriptor);
    bind(pipelines.get(context, VkPipe::ImageDirect));
    drawMeshImage(context, &rdata->mesh);
}


void VkCompositor::blendImage(VkContext& context, VkPicture* rdata, BlendMethod blendMethod)
{
    if (rdata->viewport.invalid() || !rdata->descriptor) return;
    auto copied = copyBackdrop(context, blendMethod);
    setScissorRegion(rdata->viewport);
    bindView();
    // setup stencil rules
    setStencilReference(255);
    bind(pipelines.get(context, VkPipe::Direct));
    drawMeshImage(context, &rdata->mesh);
    // blend image
    setStencilReference(0);
    bindPaint(rdata->setting.paintIdx);
    bindTexture(1, rdata->descriptor);
    if (copied) bindTexture(2, targetTemp0.current.bgTexture.set);
    bind(pipelines.blend(context, VkMaterial::Image, blendMethod));
    drawMeshImage(context, &rdata->mesh);
};


void VkCompositor::clipImage(VkContext& context, VkPicture* rdata, BlendMethod blendMethod)
{
    if (rdata->viewport.invalid() || !rdata->descriptor) return;
    setScissorRegion(rdata->viewport);
    bindView();
    // setup stencil rules
    setStencilReference(255);
    bind(pipelines.get(context, VkPipe::Direct));
    drawMeshImage(context, &rdata->mesh);
    // merge depth and stencil buffer
    setStencilReference(0);
    pushValue(128.0f / 255.0f);
    bind(pipelines.get(context, VkPipe::MergeDepthStencil));
    drawMeshImage(context, &rdata->mesh);
    // draw image
    setStencilReference(0);
    bindPaint(rdata->setting.paintIdx);
    bindTexture(1, rdata->descriptor);
    bind(blendMethod == BlendMethod::Normal ? pipelines.get(context, VkPipe::Image) : pipelines.blend(context, VkMaterial::Image, blendMethod));
    drawMeshImage(context, &rdata->mesh);
}


void VkCompositor::drawScene(VkContext& context, VkRenderTarget* scene, VkCompose* compose, VkMesh& mesh)
{
    // draw scene
    setScissorRegion(compose->aabb);
    setStencilReference(0);
    bindTexture(1, scene->current.bgTexture.set);
    pushValue(compose->opacity / 255.0f);
    bind(pipelines.get(context, VkPipe::Scene));
    drawMeshImage(context, &mesh);
}


void VkCompositor::blendScene(VkContext& context, VkRenderTarget* scene, VkCompose* compose, VkMesh& mesh)
{
    auto copied = copyBackdrop(context, compose->blend);
    // blend scene
    setScissorRegion(compose->aabb);
    setStencilReference(0);
    bindTexture(1, scene->current.bgTexture.set);
    if (copied) bindTexture(2, targetTemp0.current.bgTexture.set);
    pushValue(compose->opacity / 255.0f);
    bind(pipelines.blend(context, VkMaterial::Scene, compose->blend));
    drawMeshImage(context, &mesh);
}


void VkCompositor::markupClipPath(VkContext& context, VkShape* rdata)
{
    setScissorRegion(RenderRegion::intersect(clipRegion, rdata->viewport));
    // markup stencil
    if (rdata->stroke.mesh.vbuffer.count > 0) {
        bindStrokeView(rdata->strokeViewMatIdx);
        setStencilReference(255);
        bind(pipelines.get(context, VkPipe::Direct));
        drawMesh(context, &rdata->stroke.mesh);
    } else if (rdata->shape.mesh.vbuffer.count > 0) {
        bindView();
        setStencilReference(0);
        bind(pipelines.get(context, rdata->fillRule == FillRule::NonZero ? VkPipe::NonZero : VkPipe::EvenOdd));
        drawMesh(context, &rdata->shape.mesh);
    }
}


// The clip only has to be resolved where the paint draws, the clip paths themselves may span the whole scene.
static RenderRegion _drawRegion(VkPaint* paint)
{
    BBox bbox;
    if (paint->type() == Type::Shape) bbox = static_cast<VkShape*>(paint)->bbox;
    else {
        bbox.init();
        for (auto& p : static_cast<VkPicture*>(paint)->mesh.vbuffer) {
            bbox.min = tvg::min(bbox.min, p);
            bbox.max = tvg::max(bbox.max, p);
        }
    }
    // one pixel of margin keeps the antialiased edge samples
    RenderRegion region{int32_t(floorf(bbox.min.x)) - 1, int32_t(floorf(bbox.min.y)) - 1, int32_t(ceilf(bbox.max.x)) + 1, int32_t(ceilf(bbox.max.y)) + 1};
    region.intersect(paint->viewport);
    if (region.invalid()) return {};
    return region;
}


void VkCompositor::renderClipPath(VkContext& context, VkPaint* paint)
{
    clipRegion = _drawRegion(paint);
    auto rdata0 = (VkShape*)paint->clips[0];
    // markup stencil
    markupClipPath(context, rdata0);
    // copy stencil to depth
    setStencilReference(0);
    bindView();
    pushValue(128.0f / 255.0f);
    bind(pipelines.get(context, VkPipe::CopyStencilToDepth));
    drawMesh(context, &rdata0->bboxMesh);
    // merge clip paths with AND logic
    for (auto p = paint->clips.begin() + 1; p < paint->clips.end(); ++p) {
        auto rdata = (VkShape*)(*p);
        // markup stencil
        markupClipPath(context, rdata);
        // copy stencil to depth (clear stencil)
        setStencilReference(0);
        bindView();
        pushValue(190.0f / 255.0f);
        bind(pipelines.get(context, VkPipe::CopyStencilToDepthInterm));
        drawMesh(context, &rdata->bboxMesh);
        // copy depth to stencil
        setStencilReference(1);
        bind(pipelines.get(context, VkPipe::CopyDepthToStencil));
        drawMesh(context, &rdata->bboxMesh);
        // clear depth current (keep stencil)
        setStencilReference(0);
        pushValue(1.0f);
        bind(pipelines.get(context, VkPipe::ClearDepth));
        drawMesh(context, &rdata->bboxMesh);
        // clear depth original (keep stencil)
        drawMesh(context, &rdata0->bboxMesh);
        // copy stencil to depth (clear stencil)
        pushValue(128.0f / 255.0f);
        bind(pipelines.get(context, VkPipe::CopyStencilToDepth));
        drawMesh(context, &rdata->bboxMesh);
    }
}


void VkCompositor::clearClipPath(VkContext& context, VkPaint* paint)
{
    setScissorRegion(clipRegion);
    setStencilReference(0);
    bindView();
    pushValue(1.0f);
    bind(pipelines.get(context, VkPipe::ClearDepth));
    for (auto& p : paint->clips) {
        drawMesh(context, &((VkShape*)p)->bboxMesh);
    }
}

/************************************************************************/
/* Effects                                                              */
/************************************************************************/

bool VkCompositor::motionBlur(VkContext& context, const VkRenderTargetState& targetState, const RenderEffectMotionBlur* motion, VkMesh& mesh)
{
    auto effect = (VkRenderEffect*)motion->rd;
    auto aabb = targetState.bounds;
    auto dst = targetState.target;

    // The final clipped region is only known at composition time.
    // Clamp bilinear sampling to this region within the full-sized texture.
    float params[12];
    memcpy(params, effect->params, sizeof(params));
    params[4] = float(aabb.x());
    params[5] = float(aabb.y());
    params[6] = float(aabb.w());
    params[7] = float(aabb.h());

    copyEffectTexture(context, &targetTemp0, targetState);
    beginRenderPass(context, commandBuffer, dst);
    {
        setScissor(0, 0, aabb.w(), aabb.h());
        bindTexture(1, targetTemp0.getTextureLinear(context));
        pushParams(params);
        bind(pipelines.effect(context, VkEffect::MotionBlur));
        drawMeshImage(context, &mesh);
    }
    endRenderPass();

    return true;
}


bool VkCompositor::gaussianBlur(VkContext& context, const VkRenderTargetState& targetState, const RenderEffectGaussianBlur* blur, VkMesh& mesh)
{
    auto effect = (VkRenderEffect*)blur->rd;
    auto aabb = targetState.bounds;
    auto dst = targetState.target;

    copyEffectTexture(context, &targetTemp0, targetState, true);
    if (blur->direction == 0) {  // both
        beginRenderPass(context, commandBuffer, &targetTemp1, true);
        {
            setScissor(aabb.x(), aabb.y(), aabb.w(), aabb.h());
            bindTexture(1, targetTemp0.current.bgTexture.set);
            pushParams(effect->params);
            bind(pipelines.effect(context, VkEffect::GaussianHorz));
            drawMeshImage(context, &meshDataBlit);
        }
        endRenderPass();
        beginRenderPass(context, commandBuffer, dst);
        {
            setScissor(0, 0, aabb.w(), aabb.h());
            bindTexture(1, targetTemp1.current.bgTexture.set);
            pushParams(effect->params);
            bind(pipelines.effect(context, VkEffect::GaussianVert));
            drawMeshImage(context, &mesh);
        }
        endRenderPass();
    } else if (blur->direction == 1 || blur->direction == 2) {  // horizontal or vertical
        beginRenderPass(context, commandBuffer, dst);
        {
            setScissor(0, 0, aabb.w(), aabb.h());
            bindTexture(1, targetTemp0.current.bgTexture.set);
            pushParams(effect->params);
            bind(pipelines.effect(context, blur->direction == 1 ? VkEffect::GaussianHorz : VkEffect::GaussianVert));
            drawMeshImage(context, &mesh);
        }
        endRenderPass();
    }

    return true;
}


bool VkCompositor::dropShadow(VkContext& context, const VkRenderTargetState& targetState, const RenderEffectDropShadow* shadow, VkMesh& mesh)
{
    auto effect = (VkRenderEffect*)shadow->rd;
    auto aabb = targetState.bounds;
    auto dst = targetState.target;

    copyEffectTexture(context, &targetTemp1, targetState, true);
    if (!tvg::zero(shadow->sigma)) {
        // horizontal
        beginRenderPass(context, commandBuffer, &targetTemp0, true);
        {
            setScissor(aabb.x(), aabb.y(), aabb.w(), aabb.h());
            bindTexture(1, targetTemp1.current.bgTexture.set);
            pushParams(effect->params);
            bind(pipelines.effect(context, VkEffect::GaussianHorz));
            drawMeshImage(context, &meshDataBlit);
        }
        endRenderPass();
        // vertical
        beginRenderPass(context, commandBuffer, &targetTemp1);
        {
            setScissor(aabb.x(), aabb.y(), aabb.w(), aabb.h());
            bindTexture(1, targetTemp0.current.bgTexture.set);
            pushParams(effect->params);
            bind(pipelines.effect(context, VkEffect::GaussianVert));
            drawMeshImage(context, &meshDataBlit);
        }
        endRenderPass();
    }
    // drop shadow
    copyEffectTexture(context, &targetTemp0, targetState);
    beginRenderPass(context, commandBuffer, dst);
    {
        setScissor(0, 0, aabb.w(), aabb.h());
        bindTexture(1, targetTemp0.current.bgTexture.set);
        bindTexture(2, targetTemp1.current.bgTexture.set);
        pushParams(effect->params);
        bind(pipelines.get(context, VkPipe::Shadow));
        drawMeshImage(context, &mesh);
    }
    endRenderPass();
    return true;
}


bool VkCompositor::fillEffect(VkContext& context, const VkRenderTargetState& targetState, const RenderEffectFill* fill, VkMesh& mesh)
{
    auto effect = (VkRenderEffect*)fill->rd;
    auto aabb = targetState.bounds;
    auto dst = targetState.target;

    copyEffectTexture(context, &targetTemp0, targetState);
    beginRenderPass(context, commandBuffer, dst);
    {
        setScissor(0, 0, aabb.w(), aabb.h());
        bindTexture(1, targetTemp0.current.bgTexture.set);
        pushParams(effect->params);
        bind(pipelines.effect(context, VkEffect::Fill));
        drawMeshImage(context, &mesh);
    }
    endRenderPass();

    return true;
}


bool VkCompositor::tintEffect(VkContext& context, const VkRenderTargetState& targetState, const RenderEffectTint* tint, VkMesh& mesh)
{
    auto effect = (VkRenderEffect*)tint->rd;
    auto aabb = targetState.bounds;
    auto dst = targetState.target;

    copyEffectTexture(context, &targetTemp0, targetState);
    beginRenderPass(context, commandBuffer, dst);
    {
        setScissor(0, 0, aabb.w(), aabb.h());
        bindTexture(1, targetTemp0.current.bgTexture.set);
        pushParams(effect->params);
        bind(pipelines.effect(context, VkEffect::Tint));
        drawMeshImage(context, &mesh);
    }
    endRenderPass();

    return true;
}


bool VkCompositor::tritoneEffect(VkContext& context, const VkRenderTargetState& targetState, const RenderEffectTritone* tritone, VkMesh& mesh)
{
    auto effect = (VkRenderEffect*)tritone->rd;
    auto aabb = targetState.bounds;
    auto dst = targetState.target;

    copyEffectTexture(context, &targetTemp0, targetState);
    beginRenderPass(context, commandBuffer, dst);
    {
        setScissor(0, 0, aabb.w(), aabb.h());
        bindTexture(1, targetTemp0.current.bgTexture.set);
        pushParams(effect->params);
        bind(pipelines.effect(context, VkEffect::Tritone));
        drawMeshImage(context, &mesh);
    }
    endRenderPass();

    return true;
}
