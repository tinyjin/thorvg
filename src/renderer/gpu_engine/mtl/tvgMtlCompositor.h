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

#ifndef _TVG_MTL_COMPOSITOR_H_
#define _TVG_MTL_COMPOSITOR_H_

#include "tvgMtlRenderTarget.h"
#include "tvgMtlRenderData.h"

struct MtlCompose: RenderCompositor
{
    BlendMethod blend{};
    RenderRegion aabb{};
    CompositionFlag flags{};
    bool masked{}; // indicate if composition allocates more than one render target
};

struct MtlRenderTargetState
{
    MtlRenderTarget* target{};
    RenderRegion bounds{};       // Canvas coordinates.
    uint32_t transformOffset{};  // Byte offset in the geometry buffer.
    bool resumed{};
};

class MtlCompositor
{
private:
    // pipelines
    MtlPipelines pipelines{};
    // stage buffers
    MtlStageBufferGeometry stageBufferGeometry{};
    MtlStageBufferSolidColor stageBufferSolidColor{};
    Array<MtlShaderTypePaintSettings> stageBufferPaint;
    Array<MtlShaderTypeMat4x4fBlock> stageBufferViewMat;
    struct DepthStencilAttachment
    {
        uint32_t width, height;
        id<MTLTexture> texture;
    };
    Array<DepthStencilAttachment> depthStencilAttachments;
    id<MTLBuffer> frames[MTL_MAX_FRAMES_IN_FLIGHT]{};
    id<MTLBuffer> frame{};
    dispatch_semaphore_t frameSlots{};
    uint32_t frameIndex{};
    uint64_t paintBase{}, geometryBase{}, colorBase{}, indexBase{};
    // current render pass handles
    id<MTLRenderCommandEncoder> renderPassEncoder{};
    id<MTLCommandBuffer> commandBuffer{};
    id<MTLRenderPipelineState> currentState{};
    id<MTLDepthStencilState> currentDepthStencil{};
    const MtlRenderTargetState* currentTargetState{};
    // intermediate render targets
    MtlRenderTarget targetTemp0;
    MtlRenderTarget targetTemp1;
    // composition and blend geometries
    MtlMesh meshDataBlit;
    // render target dimensions
    uint32_t width{};
    uint32_t height{};

    // viewport utilities
    void setScissor(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    void setScissorRegion(const RenderRegion& region);
    id<MTLTexture> getDepthStencil(MtlContext& context, uint32_t width, uint32_t height);
    void requestQuad(MtlMesh& mesh, const MtlRenderTargetState& src, const MtlRenderTargetState& dst, const MtlRenderTargetState* mask = nullptr);
    void copyTexture(id<MTLTexture> src, id<MTLTexture> dst, MTLSize size, MTLOrigin origin);
    void copyEffectTexture(MtlContext& context, MtlRenderTarget* dst, const MtlRenderTargetState& src, bool clear = false);
    bool copyBackdrop(MtlContext& context, BlendMethod method);

    void bindFrame(uint32_t adjustOffset);
    void bind(const MtlPipeline& pipeline);
    void bindView();
    void bindStrokeView(uint32_t index);
    void bindPaint(uint32_t index);
    void bindTexture(uint32_t slot, const MtlBinding& binding);
    void bindDepth(uint8_t value);
    void bindOpacity(uint8_t value);
    void bindParams(const float* params);
    void bindVertex(uint32_t slot, uint64_t offset);
    void bindColor(uint64_t offset);
    void drawIndexed(uint32_t count, uint64_t offset);

    // base meshes draw
    void drawMesh(MtlContext& context, MtlMesh* meshData);
    void drawMeshSolid(MtlContext& context, MtlMesh* meshData, uint32_t solidColorInd);
    void drawMeshImage(MtlContext& context, MtlMesh* meshData);

    // shapes
    void drawShape(MtlContext& context, MtlShape* rdata);
    void blendShape(MtlContext& context, MtlShape* rdata, BlendMethod blendMethod);
    void clipShape(MtlContext& context, MtlShape* rdata, BlendMethod blendMethod);

    // strokes
    void stencilStrokes(MtlContext& context, MtlShape* rdata);
    void drawStrokes(MtlContext& context, MtlShape* rdata);
    void blendStrokes(MtlContext& context, MtlShape* rdata, BlendMethod blendMethod);

    // images
    void drawImage(MtlContext& context, MtlImage* rdata);
    void blendImage(MtlContext& context, MtlImage* rdata, BlendMethod blendMethod);
    void clipImage(MtlContext& context, MtlImage* rdata, BlendMethod blendMethod);

    // scenes
    void drawScene(MtlContext& context, MtlRenderTarget* scene, MtlCompose* compose, MtlMesh& mesh);
    void blendScene(MtlContext& context, MtlRenderTarget* src, MtlCompose* compose, MtlMesh& mesh);

    // the renderer prioritizes clipping with the stroke over the shape's fill
    void markupClipPath(MtlContext& context, MtlShape* rdata);
    void renderClipPath(MtlContext& context, MtlPaint* paint);
    void clearClipPath(MtlContext& context, MtlPaint* paint);
public:
    void initialize(MtlContext& context, uint32_t width, uint32_t height);
    void release(MtlContext& context);
    void resize(MtlContext& context, uint32_t width, uint32_t height);

    // render passes workflow
    void beginRenderPassMS(MtlContext& context, id<MTLCommandBuffer> commandBuffer, const MtlRenderTargetState& targetState, bool clearBuffer);
    void beginRenderPass(MtlContext& context, id<MTLCommandBuffer> commandBuffer, MtlRenderTarget* target, bool clearBuffer = false);
    void endRenderPass();

    // stage buffers operations
    void reset(MtlContext& context);
    void flush(MtlContext& context);
    void releaseFrameOn(id<MTLCommandBuffer> commandBuffer);
    MtlPipelines& pipelineStates() { return pipelines; }

    // request shapes for drawing (staging)
    void requestShape(MtlShape* shape);
    void requestImage(MtlImage* image);
    void requestSolidBatch(const Array<MtlShape*>& renderShapes, MtlSolidBatchRange& range);
    void requestImageBatch(const Array<MtlImage*>& renderImages, MtlImageBatchRange& range);
    void requestStencilBatch(const Array<MtlShape*>& renderShapes, MtlStencilBatchRange& range);
    void requestScene(MtlRenderTargetState& targetState, MtlMesh& mesh, MtlMesh& effectMesh, const MtlRenderTargetState* dst, const MtlRenderTargetState* mask, bool effects);

    // render shapes, images and scenes
    void renderShape(MtlContext& context, MtlShape* rdata, BlendMethod blendMethod);
    void renderSolidBatch(const MtlSolidBatchRange& range);
    void renderImageBatch(MtlImage* image, const MtlImageBatchRange& range);
    void renderStencilBatch(const Array<MtlShape*>& renderShapes, const MtlStencilBatchRange& range);
    void renderImage(MtlContext& context, MtlImage* rdata, BlendMethod blendMethod);
    void renderScene(MtlContext& context, MtlRenderTarget* scene, MtlCompose* compose, MtlMesh& mesh);
    void composeScene(MtlContext& context, MtlRenderTarget* src, MtlRenderTarget* mask, MtlCompose* compose, MtlMesh& mesh);

    void blit(MtlContext& context, id<MTLCommandBuffer> commandBuffer, MtlRenderTarget* src, id<MTLTexture> dst, bool premultiplied);

    // effects
    bool gaussianBlur(MtlContext& context, const MtlRenderTargetState& targetState, const RenderEffectGaussianBlur* blur, MtlMesh& mesh);
    bool dropShadow(MtlContext& context, const MtlRenderTargetState& targetState, const RenderEffectDropShadow* shadow, MtlMesh& mesh);
    bool fillEffect(MtlContext& context, const MtlRenderTargetState& targetState, const RenderEffectFill* fill, MtlMesh& mesh);
    bool tintEffect(MtlContext& context, const MtlRenderTargetState& targetState, const RenderEffectTint* tint, MtlMesh& mesh);
    bool tritoneEffect(MtlContext& context, const MtlRenderTargetState& targetState, const RenderEffectTritone* tritone, MtlMesh& mesh);
    bool motionBlur(MtlContext& context, const MtlRenderTargetState& targetState, const RenderEffectMotionBlur* params, MtlMesh& mesh);
};

#endif // _TVG_MTL_COMPOSITOR_H_
