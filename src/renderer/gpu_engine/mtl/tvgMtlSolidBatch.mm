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

#include "tvgMtlSolidBatch.h"

static inline bool eligible(const MtlShape* rdata, BlendMethod blendMethod)
{
    if (blendMethod != BlendMethod::Normal) return false;
    if (!rdata->shape.setting.valid || rdata->shape.setting.fillType != MtlRenderSettingsType::Solid) return false;
    if (!rdata->convex || rdata->viewport.invalid() || !rdata->clips.empty()) return false;
    if (rdata->shape.mesh.vbuffer.empty() || rdata->shape.mesh.ibuffer.empty()) return false;
    if (rdata->stroke.setting.valid && !rdata->stroke.mesh.ibuffer.empty()) return false;
    return true;
}

static inline bool eligible(const MtlImage* rdata, BlendMethod blendMethod)
{
    if (blendMethod != BlendMethod::Normal) return false;
    if (rdata->viewport.invalid() || !rdata->clips.empty() || !rdata->binding.texture) return false;
    if (rdata->mesh.vbuffer.empty() || rdata->mesh.ibuffer.empty()) return false;
    return true;
}

static inline bool appendable(MtlSceneTask* batchSceneTask, MtlRenderTask* batchTask, const RenderRegion& batchViewport, MtlSceneTask* sceneTask, const MtlPaint* rdata, const Array<MtlRenderTask*>& renderTaskList)
{
    // Any task submitted after the candidate is an implicit batch boundary.
    if (batchSceneTask != sceneTask) return false;
    if (sceneTask->children.last() != batchTask) return false;
    if (renderTaskList.last() != batchTask) return false;
    if (!(batchViewport == rdata->viewport)) return false;
    return true;
}

static inline MtlRenderTask* emitSingle(MtlSceneTask* sceneTask, MtlPaint* rdata, Array<MtlRenderTask*>& renderTaskList)
{
    auto task = new MtlPaintTask(rdata, BlendMethod::Normal);
    sceneTask->children.push(task);
    renderTaskList.push(task);
    return task;
}

static inline MtlRenderTask* promote(MtlSceneTask* sceneTask, MtlRenderTask* task, MtlRenderTask* batchTask, Array<MtlRenderTask*>& renderTaskList)
{
    // Tasks are staged only after the tree is complete, so replacing its tail is safe.
    sceneTask->children.last() = batchTask;
    renderTaskList.last() = batchTask;
    delete task;

    return batchTask;
}

bool MtlSolidBatch::draw(MtlSceneTask* sceneTask, MtlShape* rdata, BlendMethod blendMethod, Array<MtlRenderTask*>& renderTaskList)
{
    if (!eligible(rdata, blendMethod)) return false;

    if (type != Type::Shape || !appendable(this->sceneTask, task, viewport, sceneTask, rdata, renderTaskList)) {
        task = emitSingle(sceneTask, rdata, renderTaskList);
        this->sceneTask = sceneTask;
        first = rdata;
        viewport = rdata->viewport;
        type = Type::Shape;
        batched = false;
        return true;
    }

    if (!batched) {
        task = promote(sceneTask, task, new MtlBatchTask(static_cast<MtlShape*>(first), rdata, false), renderTaskList);
        batched = true;
    } else static_cast<MtlBatchTask*>(task)->shapes.push(rdata);
    return true;
}

bool MtlSolidBatch::draw(MtlSceneTask* sceneTask, MtlImage* rdata, BlendMethod blendMethod, Array<MtlRenderTask*>& renderTaskList)
{
    if (!eligible(rdata, blendMethod)) return false;

    if (type != Type::Picture || !appendable(this->sceneTask, task, viewport, sceneTask, rdata, renderTaskList) ||
        static_cast<MtlImage*>(first)->binding != rdata->binding ||
        static_cast<MtlImage*>(first)->setting.settings.options.vec[3] != rdata->setting.settings.options.vec[3]) {
        task = emitSingle(sceneTask, rdata, renderTaskList);
        this->sceneTask = sceneTask;
        first = rdata;
        viewport = rdata->viewport;
        type = Type::Picture;
        batched = false;
        return true;
    }

    if (!batched) {
        task = promote(sceneTask, task, new MtlImageBatchTask(static_cast<MtlImage*>(first), rdata), renderTaskList);
        batched = true;
    } else static_cast<MtlImageBatchTask*>(task)->images.push(rdata);
    return true;
}
