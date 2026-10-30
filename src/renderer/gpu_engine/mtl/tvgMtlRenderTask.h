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

#ifndef _TVG_MTL_RENDER_TASK_H_
#define _TVG_MTL_RENDER_TASK_H_
 
#include "tvgMtlCompositor.h"

// base class for any renderable objects
struct MtlRenderTask
{
    virtual ~MtlRenderTask() {}
    virtual void stage(MtlCompositor& compositor) = 0;
    virtual void run(MtlContext& context, MtlCompositor& compositor, id<MTLCommandBuffer> encoder) = 0;
};

// task for single shape rendering
struct MtlPaintTask : MtlRenderTask
{
    // shape render properties
    MtlPaint* renderPaint{};
    BlendMethod blendMethod{};

    MtlPaintTask(MtlPaint* renderPaint, BlendMethod blendMethod) :
        renderPaint(renderPaint), blendMethod(blendMethod) {}
    // stage all resources used by this paint
    void stage(MtlCompositor& compositor) override;
    // apply shape execution, including custom blending and clipping
    void run(MtlContext& context, MtlCompositor& compositor, id<MTLCommandBuffer> encoder) override;
};

struct MtlBatchTask : MtlRenderTask
{
    Array<MtlShape*> shapes;
    MtlSolidBatchRange solidRange;
    MtlStencilBatchRange stencilRange;
    bool stencilBatch{};

    MtlBatchTask(MtlShape* first, MtlShape* second, bool stencilBatch);
    void stage(MtlCompositor& compositor) override;
    void run(MtlContext& context, MtlCompositor& compositor, id<MTLCommandBuffer> encoder) override;
};

struct MtlImageBatchTask : MtlRenderTask
{
    Array<MtlImage*> images;
    MtlImageBatchRange range;

    MtlImageBatchTask(MtlImage* first, MtlImage* second);
    void stage(MtlCompositor& compositor) override;
    void run(MtlContext& context, MtlCompositor& compositor, id<MTLCommandBuffer> encoder) override;
};

// task for scene rendering with blending, composition and effect
struct MtlSceneTask : MtlRenderTask
{
    MtlSceneTask* parent{};           // parent scene (nullptr for root scene)
    Array<MtlRenderTask*> children;   // children can be shapes or scenes tasks
    MtlRenderTargetState targetState;
    MtlSceneTask* maskScene{};
    MtlSceneTask* dstScene{};
    MtlMesh composeMesh;
    MtlMesh effectMesh;
    MtlCompose* compose{};  // scene blend/compose properties
    Array<const RenderEffect*> effects;
    bool clearBuffer = true;

    MtlSceneTask(MtlRenderTarget* renderTarget, MtlCompose* compose, MtlSceneTask* parent) :
        parent(parent), targetState{renderTarget, compose->aabb}, compose(compose) {}
    void stage(MtlCompositor& compositor) override;
    void run(MtlContext& context, MtlCompositor& compositor, id<MTLCommandBuffer> encoder) override;
    void runChildren(MtlContext& context, MtlCompositor& compositor, id<MTLCommandBuffer> encoder);
    void runEffect(MtlContext& context, MtlCompositor& compositor, const RenderEffect* effect);
};

#endif  // _TVG_MTL_RENDER_TASK_H_
