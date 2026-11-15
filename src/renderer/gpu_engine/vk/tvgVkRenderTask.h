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

#ifndef _TVG_VK_RENDER_TASK_H_
#define _TVG_VK_RENDER_TASK_H_
 
#include "tvgVkCompositor.h"

// base class for any renderable objects
struct VkRenderTask
{
    virtual ~VkRenderTask() {}
    virtual void stage(VkCompositor& compositor) = 0;
    virtual void run(VkContext& context, VkCompositor& compositor, VkCommandBuffer encoder) = 0;
};

// task for single shape rendering
struct VkPaintTask : VkRenderTask
{
    // shape render properties
    VkPaint* renderPaint{};
    BlendMethod blendMethod{};

    VkPaintTask(VkPaint* renderPaint, BlendMethod blendMethod) :
        renderPaint(renderPaint), blendMethod(blendMethod) {}
    // stage all resources used by this paint
    void stage(VkCompositor& compositor) override;
    // apply shape execution, including custom blending and clipping
    void run(VkContext& context, VkCompositor& compositor, VkCommandBuffer encoder) override;
};

struct VkBatchTask : VkRenderTask
{
    Array<VkShape*> shapes;
    VkSolidBatchRange solidRange;
    VkStencilBatchRange stencilRange;
    bool stencilBatch{};

    VkBatchTask(VkShape* first, VkShape* second, bool stencilBatch);
    void stage(VkCompositor& compositor) override;
    void run(VkContext& context, VkCompositor& compositor, VkCommandBuffer encoder) override;
};

struct VkImageBatchTask : VkRenderTask
{
    Array<VkPicture*> images;
    VkImageBatchRange range;

    VkImageBatchTask(VkPicture* first, VkPicture* second);
    void stage(VkCompositor& compositor) override;
    void run(VkContext& context, VkCompositor& compositor, VkCommandBuffer encoder) override;
};

// task for scene rendering with blending, composition and effect
struct VkSceneTask : VkRenderTask
{
    VkSceneTask* parent{};           // parent scene (nullptr for root scene)
    Array<VkRenderTask*> children;   // children can be shapes or scenes tasks
    VkRenderTargetState targetState;
    VkSceneTask* maskScene{};
    VkSceneTask* dstScene{};
    VkMesh composeMesh;
    VkMesh effectMesh;
    VkCompose* compose{};  // scene blend/compose properties
    Array<const RenderEffect*> effects;
    bool clearBuffer = true;

    VkSceneTask(VkRenderTarget* renderTarget, VkCompose* compose, VkSceneTask* parent) :
        parent(parent), targetState{renderTarget, compose->aabb}, compose(compose) {}
    void stage(VkCompositor& compositor) override;
    void run(VkContext& context, VkCompositor& compositor, VkCommandBuffer encoder) override;
    void runChildren(VkContext& context, VkCompositor& compositor, VkCommandBuffer encoder);
    void runEffect(VkContext& context, VkCompositor& compositor, const RenderEffect* effect);
};

#endif  // _TVG_VK_RENDER_TASK_H_
