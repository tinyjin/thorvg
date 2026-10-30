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

#include "tvgMtlRenderTask.h"

//***********************************************************************
// MtlBatchTask
//***********************************************************************

MtlBatchTask::MtlBatchTask(MtlShape* first, MtlShape* second, bool stencilBatch) :
    shapes{2}, stencilBatch(stencilBatch)
{
    shapes.push(first);
    shapes.push(second);
}

void MtlBatchTask::stage(MtlCompositor& compositor)
{
    if (stencilBatch) compositor.requestStencilBatch(shapes, stencilRange);
    else compositor.requestSolidBatch(shapes, solidRange);
}

void MtlBatchTask::run(MtlContext&, MtlCompositor& compositor, id<MTLCommandBuffer>)
{
    if (stencilBatch) compositor.renderStencilBatch(shapes, stencilRange);
    else compositor.renderSolidBatch(solidRange);
}

//***********************************************************************
// MtlImageBatchTask
//***********************************************************************

MtlImageBatchTask::MtlImageBatchTask(MtlImage* first, MtlImage* second) : images{2}
{
    images.push(first);
    images.push(second);
}

void MtlImageBatchTask::stage(MtlCompositor& compositor)
{
    compositor.requestImageBatch(images, range);
}

void MtlImageBatchTask::run(MtlContext&, MtlCompositor& compositor, id<MTLCommandBuffer>)
{
    compositor.renderImageBatch(images[0], range);
}

//***********************************************************************
// MtlPaintTask
//***********************************************************************

void MtlPaintTask::stage(MtlCompositor& compositor)
{
    if (renderPaint->type() == tvg::Type::Shape) compositor.requestShape((MtlShape*)renderPaint);
    if (renderPaint->type() == tvg::Type::Picture) compositor.requestImage((MtlImage*)renderPaint);
}

void MtlPaintTask::run(MtlContext& context, MtlCompositor& compositor, id<MTLCommandBuffer> encoder)
{
    if (renderPaint->type() == tvg::Type::Shape) compositor.renderShape(context, (MtlShape*)renderPaint, blendMethod);
    if (renderPaint->type() == tvg::Type::Picture) compositor.renderImage(context, (MtlImage*)renderPaint, blendMethod);
}

//***********************************************************************
// MtlSceneTask
//***********************************************************************

void MtlSceneTask::stage(MtlCompositor& compositor)
{
    if (targetState.bounds.invalid()) return;
    compositor.requestScene(targetState, composeMesh, effectMesh, dstScene ? &dstScene->targetState : nullptr, maskScene ? &maskScene->targetState : nullptr, !effects.empty());
    // stage all resources used by the scene tree
    ARRAY_FOREACH(task, children) {
        (*task)->stage(compositor);
    }
}

// run all, including all shapes drawing, blending, composition and effect
void MtlSceneTask::run(MtlContext& context, MtlCompositor& compositor, id<MTLCommandBuffer> encoder)
{
    if (targetState.bounds.invalid()) return;
    // Preserve the canvas contents when clearing is disabled; temporary scenes always clear.
    compositor.beginRenderPassMS(context, encoder, targetState, clearBuffer);
    runChildren(context, compositor, encoder);  // run all children (scenes and shapes)
    compositor.endRenderPass();

    // apply effect for current scene
    ARRAY_FOREACH(effect, effects) {
        runEffect(context, compositor, *effect);
    }

    if (!dstScene) return;  // there's no point in continuing (e.g., the root scene)

    // scene blending
    if (compose->method == MaskMethod::None) {
        compositor.beginRenderPassMS(context, encoder, dstScene->targetState, false);
        compositor.renderScene(context, targetState.target, compose, composeMesh);
        // scene composition (for scenes, that have a handle to mask)
    } else if (maskScene) {
        compositor.beginRenderPassMS(context, encoder, dstScene->targetState, false);
        compositor.composeScene(context, targetState.target, maskScene->targetState.target, compose, composeMesh);
    }
}


void MtlSceneTask::runChildren(MtlContext& context, MtlCompositor& compositor, id<MTLCommandBuffer> encoder)
{
    ARRAY_FOREACH(task, children) {
        MtlRenderTask* renderTask = *task;
        // we need to restore current render pass without clear
        compositor.beginRenderPassMS(context, encoder, targetState, false);
        // run children (shape or scene)
        renderTask->run(context, compositor, encoder);
    }
}

void MtlSceneTask::runEffect(MtlContext& context, MtlCompositor& compositor, const RenderEffect* effect)
{
    switch (effect->type) {
        case SceneEffect::GaussianBlur: compositor.gaussianBlur(context, targetState, (RenderEffectGaussianBlur*)effect, effectMesh); break;
        case SceneEffect::DropShadow: compositor.dropShadow(context, targetState, (RenderEffectDropShadow*)effect, effectMesh); break;
        case SceneEffect::Fill: compositor.fillEffect(context, targetState, (RenderEffectFill*)effect, effectMesh); break;
        case SceneEffect::Tint: compositor.tintEffect(context, targetState, (RenderEffectTint*)effect, effectMesh); break;
        case SceneEffect::Tritone: compositor.tritoneEffect(context, targetState, (RenderEffectTritone*)effect, effectMesh); break;
        case SceneEffect::MotionBlur: compositor.motionBlur(context, targetState, static_cast<const RenderEffectMotionBlur*>(effect), effectMesh); break;
        default: break;
    }
}
