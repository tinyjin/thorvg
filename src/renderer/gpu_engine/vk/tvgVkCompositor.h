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

#ifndef _TVG_VK_COMPOSITOR_H_
#define _TVG_VK_COMPOSITOR_H_

#include "tvgVkRenderTarget.h"
#include "tvgVkRenderData.h"

struct VkCompose: RenderCompositor
{
    BlendMethod blend{};
    RenderRegion aabb{};
    CompositionFlag flags{};
    bool masked{}; // indicate if composition allocates more than one render target
};

struct VkRenderTargetState
{
    VkRenderTarget* target{};
    RenderRegion bounds{};       // Canvas coordinates.
    uint32_t transformOffset{};  // Byte offset in the geometry buffer.
};

class VkCompositor
{
private:
    VkContext* ctx{};
    // pipelines
    VkPipelines pipelines{};
    // stage buffers
    VkStageBufferGeometry stageBufferGeometry{};
    VkStageBufferSolidColor stageBufferSolidColor{};
    Array<VkShaderTypePaintSettings> stageBufferPaint;
    Array<VkShaderTypeMat4x4fBlock> stageBufferViewMat;
    struct DepthStencilAttachment
    {
        uint32_t width, height;
        VkTexture texture;
    };
    Array<DepthStencilAttachment> depthStencilAttachments;
    Array<DepthStencilAttachment> transientAttachments;
    // per frame storage of all staged data: [view matrices | paint settings | geometry | colors | indices]
    struct FrameBuffer
    {
        VkGpuBuffer buffer;
        VkDescriptor uniforms;
    };
    FrameBuffer frames[VK_MAX_FRAMES];
    VkBuffer frame{};
    VkDeviceSize paintBase{}, geometryBase{}, colorBase{}, indexBase{};
    // current render pass handles
    VkCommandBuffer commandBuffer{};
    bool renderPass{};
    const VkRenderTargetState* currentTargetState{};
    RenderRegion clipRegion{};  // where the active clip paths are resolved
    // bound state
    VkPipeline currentPipeline{};
    VkDescriptorSet currentSets[3]{};
    uint32_t viewOffset{}, paintOffset{};
    bool uniformsDirty{};
    // intermediate render targets
    VkRenderTarget targetTemp0;
    VkRenderTarget targetTemp1;
    // composition and blend geometries
    VkMesh meshDataBlit;
    VkMesh meshDataPresent;  // tex coords repeated so that an offset rotates the blit
    // render target dimensions
    uint32_t width{};
    uint32_t height{};

    // viewport utilities
    void setViewport(uint32_t width, uint32_t height);
    void setScissor(int32_t x, int32_t y, uint32_t w, uint32_t h);
    void setScissorRegion(const RenderRegion& region);
    VkImageView getDepthStencilView(VkContext& context, uint32_t width, uint32_t height);
    VkImageView getTransientView(VkContext& context, uint32_t width, uint32_t height);
    void requestQuad(VkMesh& mesh, const VkRenderTargetState& src, const VkRenderTargetState& dst, const VkRenderTargetState* mask = nullptr);
    void copyTexture(VkContext& context, VkTexture& src, VkTexture& dst, uint32_t width, uint32_t height, int32_t x, int32_t y);
    void copyEffectTexture(VkContext& context, VkRenderTarget* dst, const VkRenderTargetState& src, bool clear = false);
    bool copyBackdrop(VkContext& context, BlendMethod method);

    // bindings
    void resetBindings();
    void bind(VkPipeline pipeline);
    void bindView();
    void bindStrokeView(uint32_t index);
    void bindPaint(uint32_t index);
    void bindTexture(uint32_t set, VkDescriptorSet descriptor);
    void bindVertex(uint32_t binding, VkDeviceSize offset);
    void bindColor(VkDeviceSize offset);
    void pushValue(float value);
    void pushParams(const float* params);
    void setStencilReference(uint32_t reference);
    void drawIndexed(uint32_t count, VkDeviceSize offset);

    // base meshes draw
    void drawMesh(VkContext& context, VkMesh* meshData);
    void drawMeshSolid(VkContext& context, VkMesh* meshData, uint32_t solidColorInd);
    void drawMeshImage(VkContext& context, VkMesh* meshData);

    // shapes
    void drawShape(VkContext& context, VkShape* rdata);
    void blendShape(VkContext& context, VkShape* rdata, BlendMethod blendMethod);
    void clipShape(VkContext& context, VkShape* rdata, BlendMethod blendMethod);

    // strokes
    void stencilStrokes(VkContext& context, VkShape* rdata);
    void drawStrokes(VkContext& context, VkShape* rdata);
    void blendStrokes(VkContext& context, VkShape* rdata, BlendMethod blendMethod);

    // images
    void drawImage(VkContext& context, VkPicture* rdata);
    void blendImage(VkContext& context, VkPicture* rdata, BlendMethod blendMethod);
    void clipImage(VkContext& context, VkPicture* rdata, BlendMethod blendMethod);

    // scenes
    void drawScene(VkContext& context, VkRenderTarget* scene, VkCompose* compose, VkMesh& mesh);
    void blendScene(VkContext& context, VkRenderTarget* src, VkCompose* compose, VkMesh& mesh);

    // the renderer prioritizes clipping with the stroke over the shape's fill
    void markupClipPath(VkContext& context, VkShape* rdata);
    void renderClipPath(VkContext& context, VkPaint* paint);
    void clearClipPath(VkContext& context, VkPaint* paint);
public:
    void initialize(VkContext& context, uint32_t width, uint32_t height);
    void release(VkContext& context);
    void resize(VkContext& context, uint32_t width, uint32_t height);
    void evict(VkContext& context, const VkRenderTargetPool& targets);
    VkPipelines& pipelineStates() { return pipelines; }

    // render passes workflow
    void beginRenderPassMS(VkContext& context, VkCommandBuffer commandBuffer, const VkRenderTargetState& targetState, bool clearBuffer);
    void beginRenderPass(VkContext& context, VkCommandBuffer commandBuffer, VkRenderTarget* target, bool clearBuffer = false);
    void endRenderPass();

    // stage buffers operations
    void reset(VkContext& context);
    bool flush(VkContext& context);

    // request shapes for drawing (staging)
    void requestShape(VkShape* shape);
    void requestImage(VkPicture* image);
    void requestSolidBatch(const Array<VkShape*>& renderShapes, VkSolidBatchRange& range);
    void requestImageBatch(const Array<VkPicture*>& renderImages, VkImageBatchRange& range);
    void requestStencilBatch(const Array<VkShape*>& renderShapes, VkStencilBatchRange& range);
    void requestScene(VkRenderTargetState& targetState, VkMesh& mesh, VkMesh& effectMesh, const VkRenderTargetState* dst, const VkRenderTargetState* mask, bool effects);

    // render shapes, images and scenes
    void renderShape(VkContext& context, VkShape* rdata, BlendMethod blendMethod);
    void renderSolidBatch(const VkSolidBatchRange& range);
    void renderImageBatch(VkPicture* image, const VkImageBatchRange& range);
    void renderStencilBatch(VkContext& context, const Array<VkShape*>& renderShapes, const VkStencilBatchRange& range);
    void renderImage(VkContext& context, VkPicture* rdata, BlendMethod blendMethod);
    void renderScene(VkContext& context, VkRenderTarget* scene, VkCompose* compose, VkMesh& mesh);
    void composeScene(VkContext& context, VkRenderTarget* src, VkRenderTarget* mask, VkCompose* compose, VkMesh& mesh);

    // blit render target to the presentable target
    void blit(VkContext& context, VkCommandBuffer commandBuffer, VkRenderTarget* src, VkFramebuffer dst, VkFormat format, uint32_t width, uint32_t height, uint32_t turns, bool premultiplied, bool present);

    // effects
    bool gaussianBlur(VkContext& context, const VkRenderTargetState& targetState, const RenderEffectGaussianBlur* blur, VkMesh& mesh);
    bool dropShadow(VkContext& context, const VkRenderTargetState& targetState, const RenderEffectDropShadow* shadow, VkMesh& mesh);
    bool fillEffect(VkContext& context, const VkRenderTargetState& targetState, const RenderEffectFill* fill, VkMesh& mesh);
    bool tintEffect(VkContext& context, const VkRenderTargetState& targetState, const RenderEffectTint* tint, VkMesh& mesh);
    bool tritoneEffect(VkContext& context, const VkRenderTargetState& targetState, const RenderEffectTritone* tritone, VkMesh& mesh);
    bool motionBlur(VkContext& context, const VkRenderTargetState& targetState, const RenderEffectMotionBlur* params, VkMesh& mesh);
};

#endif // _TVG_VK_COMPOSITOR_H_
