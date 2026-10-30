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
#include "tvgMtlCommon.h"
#import <QuartzCore/CAMetalLayer.h>
#include "tvgTaskScheduler.h"
#include "tvgMtlRenderer.h"
#include "tvgMtlRendererImpl.h"

/************************************************************************/
/* MtlPaintPool                                                          */
/************************************************************************/

MtlShape* MtlPaintPool::shape()
{
    if (!shapes.empty()) return shapes.pick();

    auto paint = new MtlShape();
    list.push(paint);
    return paint;
}

MtlImage* MtlPaintPool::image()
{
    if (!images.empty()) return images.pick();

    auto paint = new MtlImage();
    list.push(paint);
    return paint;
}

void MtlPaintPool::recycle(MtlShape* shape)
{
    shapes.push(shape);
}

void MtlPaintPool::recycle(MtlImage* image)
{
    images.push(image);
}

void MtlPaintPool::release(MtlContext& context)
{
    ARRAY_FOREACH(p, list) {
        (*p)->release(context);
        delete (*p);
    }
    shapes.clear();
    images.clear();
    list.clear();
}

/************************************************************************/
/* MtlIntersector Implementation                                         */
/************************************************************************/

struct MtlIntersector
{
    bool intersect(const Array<MtlPaint*>& clips, const Point& pt)
    {
        ARRAY_FOREACH(c, clips) {
            auto clip = static_cast<const MtlShape*>(*c);
            const auto& mesh = clip->shape.mesh;
            if (!clip->shape.bbox.inside(pt) || !gpuPointInFillMesh(pt, mesh.vbuffer.data, mesh.ibuffer.data, mesh.ibuffer.count, clip->fillRule)) return false;
        }
        return true;
    }

    bool intersect(const MtlShape* shape, const RenderRegion& region)
    {
        auto validFill = shape->shape.setting.valid && !shape->shape.mesh.ibuffer.empty();
        auto validStroke = shape->stroke.setting.valid && !shape->stroke.mesh.ibuffer.empty();
        if (!validFill && !validStroke) return false;

        Matrix itransform;
        if (validStroke && !inverse(&shape->transform, &itransform)) validStroke = false;
        auto sizeX = region.sw();
        auto sizeY = region.sh();

        for (int32_t y = 0; y < sizeY; y++) {
            auto py = (y % 2 == 0) ? y : sizeY - y - sizeY % 2;
            for (int32_t x = 0; x < sizeX; x++) {
                Point pt{(float)x + region.xMin, (float)py + region.yMin};
                const auto& mesh = shape->shape.mesh;
                auto hit = validFill ? shape->shape.bbox.inside(pt) && gpuPointInFillMesh(pt, mesh.vbuffer.data, mesh.ibuffer.data, mesh.ibuffer.count, shape->fillRule) : false;
                if (!hit && validStroke) {
                    auto p = pt * itransform;
                    const auto& mesh = shape->stroke.mesh;
                    hit = shape->stroke.bbox.inside(p) && gpuPointInAnyMesh(p, mesh.vbuffer.data, mesh.ibuffer.data, mesh.ibuffer.count);
                }
                if (hit && intersect(shape->clips, pt)) return true;
            }
        }
        return false;
    }

    bool intersect(const MtlImage* image, const RenderRegion& region)
    {
        if (image->mesh.ibuffer.count < 6) return false;

        const auto& mesh = image->mesh;
        Point triangle[6];
        for (uint32_t i = 0; i < 6; ++i) {
            triangle[i] = mesh.vbuffer[mesh.ibuffer[i]];
        }

        auto sizeX = region.sw();
        auto sizeY = region.sh();
        for (int32_t y = 0; y < sizeY; y++) {
            auto py = (y % 2 == 0) ? y : sizeY - y - sizeY % 2;
            for (int32_t x = 0; x < sizeX; x++) {
                Point pt{(float)x + region.xMin, (float)py + region.yMin};
                if (gpuPointInQuad(pt, triangle) && intersect(image->clips, pt)) return true;
            }
        }
        return false;
    }
};

/************************************************************************/
/* MtlRendererImpl Implementation                                            */
/************************************************************************/

static int32_t _rendererCnt = -1;
static StrictKey _rendererMtx;

void MtlRendererImpl::release()
{
    if (!mContext.queue) return;

    commit();

    disposeObjects();
    mTextures.clear(mContext);

    // clear render data paint pools
    mPaintPool.release(mContext);
    mEffectPool.release();

    // clear render  pool
    mRenderTargetPool.release(mContext);

    // clear rendering tree stacks
    mCompositorList.clear();
    mRenderTargetStack.clear();
    mRenderTargetRoot.release(mContext);

    // release context handles
    mCompositor.release(mContext);
    mContext.release();

    // release gpu handles
    clearTargets();
}

void MtlRendererImpl::disposeObjects()
{
    ARRAY_FOREACH(p, mDisposedPaints) {
        auto paint = (MtlPaint*)(*p);
        paint->clips.clear();
        if (paint->type() == Type::Shape) {
            static_cast<MtlShape*>(paint)->reset();
            mPaintPool.recycle(static_cast<MtlShape*>(paint));
        } else {
            static_cast<MtlImage*>(paint)->release(mTextures, mContext);
            mPaintPool.recycle(static_cast<MtlImage*>(paint));
        }
    }
    mDisposedPaints.clear();
}

void MtlRendererImpl::clearTargets()
{
    [targetTexture release];
    [surface release];
    targetTexture = nil;
    surface = nil;
    mSurface.stride = 0;
    mSurface.w = 0;
    mSurface.h = 0;
}

void MtlRendererImpl::surfaceConfigure(CAMetalLayer* layer, bool opaque)
{
    surface = [layer retain];
    surface.device = mContext.device;
    surface.pixelFormat = MTLPixelFormatBGRA8Unorm;
    surface.framebufferOnly = YES;
    surface.opaque = opaque;
    surface.drawableSize = CGSizeMake(mSurface.w, mSurface.h);
}

RenderData MtlRendererImpl::prepare(const RenderShape& rshape, RenderData data, const Matrix& transform, const Array<RenderData>& clips, uint8_t opacity, RenderUpdateFlag flags, bool clipper)
{
    auto shape = data ? (MtlShape*)data : mPaintPool.shape();
    if (!data) flags = RenderUpdateFlag::All;

    // update geometry
    if (flags & (RenderUpdateFlag::Transform | RenderUpdateFlag::Path | RenderUpdateFlag::Stroke)) shape->update(rshape, transform, flags);

    // update transform
    if (flags & RenderUpdateFlag::Transform) shape->transform = transform;

    // update paint settings
    shape->update(rshape, vport, shape->shape.setting.update(mSurface.cs, opacity), shape->stroke.setting.update(mSurface.cs, opacity), opacity);

    // shape
    if (shape->shape.setting.valid) {
        if (rshape.fill && (flags & (RenderUpdateFlag::Gradient | RenderUpdateFlag::Transform))) {
            shape->shape.setting.update(mContext, rshape.fill, &transform, (flags & RenderUpdateFlag::Gradient));
        } else if (flags & (RenderUpdateFlag::Color | RenderUpdateFlag::Gradient)) {
            shape->shape.solid.color = rshape.color;
            shape->shape.setting.fillType = MtlRenderSettingsType::Solid;
        }
    }
    // stroke
    if (shape->stroke.setting.valid) {
        if (rshape.stroke->fill && flags & (RenderUpdateFlag::GradientStroke | RenderUpdateFlag::Transform)) {
            shape->stroke.setting.update(mContext, rshape.stroke->fill, nullptr, (flags & RenderUpdateFlag::GradientStroke));
        } else if (flags & (RenderUpdateFlag::Stroke | RenderUpdateFlag::GradientStroke)) {
            shape->stroke.solid.color = rshape.stroke->color;
            shape->stroke.setting.fillType = MtlRenderSettingsType::Solid;
        }
    }

    if (flags & RenderUpdateFlag::Clip) shape->assign(clips);

    return shape;
}

RenderData MtlRendererImpl::prepare(RenderSurface* surface, RenderData data, const Matrix& transform, const Array<RenderData>& clips, uint8_t opacity, FilterMethod filter, RenderUpdateFlag flags)
{
    auto image = data ? (MtlImage*)data : mPaintPool.image();
    if (!data) flags = RenderUpdateFlag::All;

    // update paint settings
    image->viewport = vport;
    image->transform = transform;
    image->setting.update(surface->cs, opacity);

    if (flags & (RenderUpdateFlag::Transform | RenderUpdateFlag::Image)) image->update(surface, transform);

    // reload texture
    auto cacheStale = !image->binding.texture || (image->stamp != mTextures.stamp);
    auto refreshTex = (flags & RenderUpdateFlag::Image);
    auto update = cacheStale || refreshTex || (image->surface != surface) || (image->filter != filter);
    if (update) {
        image->release(mTextures, mContext);
        auto* entry = mTextures.retain(mContext, surface, filter, refreshTex);
        image->setup(entry->binding, surface, filter, mTextures.stamp);
    }

    if (flags & RenderUpdateFlag::Clip) image->assign(clips);

    return image;
}


bool MtlRendererImpl::preRender()
{
    if (mContext.invalid()) return false;

    mSolidBatch = {};
    mStencilBatch = {};
    mCompositor.reset(mContext);

    assert(mRenderTargetStack.count == 0);
    mRenderTargetStack.push(&mRenderTargetRoot);

    // create root compose settings
    MtlCompose* compose = new MtlCompose();
    compose->aabb = {0, 0, (int32_t)mSurface.w, (int32_t)mSurface.h};
    compose->blend = BlendMethod::Normal;
    compose->method = MaskMethod::None;
    compose->opacity = 255;
    mCompositorList.push(compose);

    // create root scene
    MtlSceneTask* sceneTask = new MtlSceneTask(&mRenderTargetRoot, compose, nullptr);
    sceneTask->targetState.resumed = true;
    mRenderTaskList.push(sceneTask);
    mSceneTaskStack.push(sceneTask);
    return true;
}


bool MtlRendererImpl::renderShape(RenderData data)
{
    auto rdata = (MtlShape*)data;
    MtlSceneTask* sceneTask = mSceneTaskStack.last();

    if (mSolidBatch.draw(sceneTask, rdata, mBlendMethod, mRenderTaskList)) return true;
    if (mStencilBatch.draw(sceneTask, rdata, mBlendMethod, mRenderTaskList)) return true;

    MtlPaintTask* paintTask = new MtlPaintTask(rdata, mBlendMethod);
    sceneTask->children.push(paintTask);
    mRenderTaskList.push(paintTask);
    return true;
}


bool MtlRendererImpl::renderImage(RenderData data)
{
    auto rdata = (MtlImage*)data;
    MtlSceneTask* sceneTask = mSceneTaskStack.last();
    if (mSolidBatch.draw(sceneTask, rdata, mBlendMethod, mRenderTaskList)) return true;

    MtlPaintTask* paintTask = new MtlPaintTask(rdata, mBlendMethod);
    sceneTask->children.push(paintTask);
    mRenderTaskList.push(paintTask);
    return true;
}


bool MtlRendererImpl::postRender()
{
    @autoreleasepool {
        MtlSceneTask* sceneTaskRoot = mSceneTaskStack.last();
        sceneTaskRoot->stage(mCompositor);

        // flush stage data to gpu
        mCompositor.flush(mContext);

        mCommandBuffer = [[mContext.queue commandBuffer] retain];
        mCompositor.releaseFrameOn(mCommandBuffer);

        // run rendering (all the fun is here)
        sceneTaskRoot->clearBuffer = mClearBuffer;
        mClearBuffer = false;
        sceneTaskRoot->run(mContext, mCompositor, mCommandBuffer);

        // clear the render tasks tree
        mSceneTaskStack.pop();
        mRenderTargetStack.pop();
        ARRAY_FOREACH(p, mRenderTaskList) { delete (*p); };
        mRenderTaskList.clear();
        ARRAY_FOREACH(p, mCompositorList) { delete (*p); };
        mCompositorList.clear();
    }
    return true;
}


void MtlRendererImpl::dispose(RenderData data) {
    if (!mContext.queue) return;
    ScopedLock lock(mDisposeKey);
    mDisposedPaints.push(data);
}


bool MtlRendererImpl::bounds(RenderData data, Point* pt4, const Matrix& m)
{
    if (data) {
        auto shape = static_cast<MtlShape*>(data);
        if (shape->stroke.setting.valid) {
            tvg::BBox bbox;
            bbox.init();
            auto& vertexes = shape->stroke.mesh.vbuffer;
            for (uint32_t i = 0; i < vertexes.count; i++) {
                auto pt = vertexes[i] * m;
                bbox.min = min(bbox.min, pt);
                bbox.max = max(bbox.max, pt);
            }
            pt4[0] = bbox.min;
            pt4[1] = {bbox.max.x, bbox.min.y};
            pt4[2] = bbox.max;
            pt4[3] = {bbox.min.x, bbox.max.y};
            return true;
        }
    }
    return false;
}

RenderRegion MtlRendererImpl::region(RenderData data)
{
    return data ? static_cast<MtlPaint*>(data)->region() : RenderRegion{};
}

bool MtlRendererImpl::blend(BlendMethod method)
{
    mBlendMethod = (method == BlendMethod::Composition ? BlendMethod::Normal : method);
    return true;
}


ColorSpace MtlRendererImpl::colorSpace()
{
    return mSurface.cs;
}


const RenderSurface* MtlRendererImpl::mainSurface()
{
    return &mSurface;
}


bool MtlRendererImpl::clear()
{
    if (mContext.invalid()) return false;

    mClearBuffer = true;
    return true;
}


void MtlRendererImpl::commit()
{
    if (!mCommandBuffer) return;
    [mCommandBuffer commit];
    [mCommandBuffer release];
    mCommandBuffer = nil;
}


bool MtlRendererImpl::sync()
{
    if (mContext.invalid()) return false;

    disposeObjects();

    @autoreleasepool {
        id<CAMetalDrawable> drawable = surface ? [surface nextDrawable] : nil;
        auto dstTexture = surface ? drawable.texture : targetTexture;
        if (!dstTexture) {
            TVGERR("MTL_ENGINE", "Failed to acquire destination texture.");
            commit();
            return false;
        }

        if (!mCommandBuffer) mCommandBuffer = [[mContext.queue commandBuffer] retain];

        // insure that surface and offscreen target have the same size
        if (!mClearBuffer && (dstTexture.width == mRenderTargetRoot.width) && (dstTexture.height == mRenderTargetRoot.height)) {
            mCompositor.blit(mContext, mCommandBuffer, &mRenderTargetRoot, dstTexture, mSurface.premultiplied);
        }
        if (drawable) [mCommandBuffer presentDrawable:drawable];
        commit();
    }

    return true;
}

Result MtlRendererImpl::target(void* device, void* queue, void* target, uint32_t w, uint32_t h, ColorSpace cs, int type)
{
    auto opaque = (cs == ColorSpace::XBGR8888);
    if (cs != ColorSpace::ABGR8888 && cs != ColorSpace::ABGR8888S && !opaque) return Result::NonSupport;
    if (opaque && type != 0) return Result::InvalidArguments;

    if (!device || !target) {
        release();
        return Result::Success;
    }

    if (w == 0 || h == 0) return Result::InvalidArguments;

    if (opaque) cs = ColorSpace::ABGR8888;  // Opaque surface composition uses the internal ABGR format.

    auto mtlDevice = (id<MTLDevice>)device;
    auto mtlQueue = (id<MTLCommandQueue>)queue;

    @autoreleasepool {
        // context has been changed, need to recreate all instances
        if (mContext.device != mtlDevice || (mtlQueue && mContext.queue != mtlQueue)) {
            release();
            mContext.initialize(mtlDevice, mtlQueue);
            mRenderTargetPool.initialize(w, h);
            mRenderTargetRoot.initialize(mContext, w, h);
            mCompositor.initialize(mContext, w, h);
            mClearBuffer = true;
        // update render targets dimensions
        } else if ((mSurface.w != w) || (mSurface.h != h) || (type == 0 ? (surface != (CAMetalLayer*)target) : (targetTexture != (id<MTLTexture>)target))) {
            commit();
            mRenderTargetPool.release(mContext);
            mRenderTargetRoot.release(mContext);
            clearTargets();
            mRenderTargetPool.initialize(w, h);
            mRenderTargetRoot.initialize(mContext, w, h);
            mCompositor.resize(mContext, w, h);
            mClearBuffer = true;
        }

        mSurface.stride = w;
        mSurface.w = w;
        mSurface.h = h;
        // Keep alpha for internal composition; opaque mode only affects surface presentation.
        mSurface.cs = cs;
        mSurface.opaque = opaque;
        mSurface.premultiplied = (type == 0) || (cs != ColorSpace::ABGR8888S);

        // configure surface (must be called after context creation)
        if (type == 0) {
            if (surface != (CAMetalLayer*)target) {
                [surface release];
                surface = nil;
                surfaceConfigure((CAMetalLayer*)target, opaque);
            }
        } else if (targetTexture != (id<MTLTexture>)target) {
            [targetTexture release];
            targetTexture = [(id<MTLTexture>)target retain];
        }
    }

    return Result::Success;
}

MtlRendererImpl::~MtlRendererImpl()
{
    release();

    _rendererMtx.lock();
    --_rendererCnt;
    _rendererMtx.unlock();
}


RenderCompositor* MtlRendererImpl::target(const RenderRegion& region, TVG_UNUSED ColorSpace cs, TVG_UNUSED CompositionFlag flags)
{
    // create and setup compose data
    MtlCompose* compose = new MtlCompose();
    compose->aabb = RenderRegion::intersect(region, {0, 0, int32_t(mSurface.w), int32_t(mSurface.h)});
    compose->flags = flags;
    mCompositorList.push(compose);
    return compose;
}


// please, keep beginComposite/endComposite aligned by logic
bool MtlRendererImpl::beginComposite(RenderCompositor* cmp, MaskMethod method, uint8_t opacity)
{
    // current composition
    MtlCompose* compose = (MtlCompose*)cmp;
    // allocate new render target and push to the render tree stack
    MtlRenderTarget* newRenderTarget = mRenderTargetPool.allocate(mContext, compose->aabb);
    mRenderTargetStack.push(newRenderTarget);
    // current and new scenes
    MtlSceneTask* curSceneTask = mSceneTaskStack.last();
    MtlSceneTask* newSceneTask = new MtlSceneTask(newRenderTarget, compose, curSceneTask);
    // setup masking and blending scenes configuration
    if ((compose->flags & (tvg::Blending | tvg::Masking)) == (tvg::Blending | tvg::Masking)) {
        // new scene task must be as masked but not blended
        newSceneTask->compose->method = curSceneTask->compose->method;
        newSceneTask->compose->opacity = 255;
        newSceneTask->compose->blend = BlendMethod::Normal;
        newSceneTask->maskScene = curSceneTask->maskScene;
        newSceneTask->dstScene = curSceneTask;
        // cur scene task must be blended but not masked
        curSceneTask->compose->method = MaskMethod::None;
        curSceneTask->compose->opacity = opacity;
        curSceneTask->compose->blend = mBlendMethod;
        curSceneTask->maskScene = nullptr;
    } else // setup masking scenes configuration
    if (method != MaskMethod::None) {
        // new scene task must be masked
        newSceneTask->compose->masked = true;
        newSceneTask->compose->method = method;
        newSceneTask->compose->opacity = opacity;
        newSceneTask->compose->blend = BlendMethod::Normal;
        newSceneTask->maskScene = curSceneTask;
        newSceneTask->dstScene = curSceneTask->dstScene;
        // cur scene task must not be blended
        curSceneTask->dstScene = nullptr;
    } else // setup blending scenes configuration
    if (method == MaskMethod::None) {
        // new scene task must be blended
        newSceneTask->compose->method = MaskMethod::None;
        newSceneTask->compose->opacity = opacity;
        newSceneTask->compose->blend = mBlendMethod;
        newSceneTask->maskScene = nullptr;
        newSceneTask->dstScene = curSceneTask;
    }
    if (newSceneTask->dstScene) newSceneTask->dstScene->targetState.resumed = true;
    curSceneTask->children.push(newSceneTask);
    mRenderTaskList.push(newSceneTask);
    mSceneTaskStack.push(newSceneTask);

    return true;
}


// please, keep beginComposite/endComposite aligned by logic
bool MtlRendererImpl::endComposite(RenderCompositor* cmp)
{
    // pop targets and scenes from render tree
    mRenderTargetPool.pool.push(mRenderTargetStack.last());
    mSceneTaskStack.pop();
    mRenderTargetStack.pop();
    // in a case of masked target we must pop mask targets and scenes also
    MtlCompose* compose = (MtlCompose*)cmp;
    if (compose->masked) {
        mRenderTargetPool.pool.push(mRenderTargetStack.last());
        mSceneTaskStack.pop();
        mRenderTargetStack.pop();
    }
    return true;
}


void MtlRendererImpl::prepare(RenderEffect* effect, const Matrix& transform)
{
    if (!effect->rd) effect->rd = mEffectPool.allocate();
    auto rdata = (MtlRenderEffect*)effect->rd;

    if (effect->type == SceneEffect::GaussianBlur) {
        rdata->update((RenderEffectGaussianBlur*)effect, transform);
    } else if (effect->type == SceneEffect::DropShadow) {
        rdata->update((RenderEffectDropShadow*)effect, transform);
    } else if (effect->type == SceneEffect::Fill) {
        rdata->update((RenderEffectFill*)effect);
    } else if (effect->type == SceneEffect::Tint) {
        rdata->update((RenderEffectTint*)effect);
    } else if (effect->type == SceneEffect::Tritone) {
        rdata->update((RenderEffectTritone*)effect);
    } else if (effect->type == SceneEffect::MotionBlur) {
        rdata->update((RenderEffectMotionBlur*)effect, transform);
    } else {
        TVGERR("MTL_ENGINE", "Missing effect type? = %d", (int) effect->type);
        return;
    }
}


bool MtlRendererImpl::region(RenderEffect* effect)
{
    auto rdata = (MtlRenderEffect*)effect->rd;

    if (effect->type == SceneEffect::GaussianBlur) {
        auto gaussian = (RenderEffectGaussianBlur*)effect;
        if (gaussian->direction != 2) {
            gaussian->extend.xMin = -rdata->extend;
            gaussian->extend.xMax = +rdata->extend;
        }
        if (gaussian->direction != 1) {
            gaussian->extend.yMin = -rdata->extend;
            gaussian->extend.yMax = +rdata->extend;
        }
        return true;
    } else if (effect->type == SceneEffect::DropShadow) {
        auto dropShadow = (RenderEffectDropShadow*)effect;
        dropShadow->extend.xMin = -std::ceil(rdata->extend + std::abs(rdata->offset.x));
        dropShadow->extend.yMin = -std::ceil(rdata->extend + std::abs(rdata->offset.y));
        dropShadow->extend.xMax = +std::floor(rdata->extend + std::abs(rdata->offset.x));
        dropShadow->extend.yMax = +std::floor(rdata->extend + std::abs(rdata->offset.y));
        return true;
    } else if (effect->type == SceneEffect::MotionBlur) {
        auto x = int32_t(ceilf(fabsf(rdata->offset.x) * 0.5f));
        auto y = int32_t(ceilf(fabsf(rdata->offset.y) * 0.5f));
        effect->extend = {-x, -y, x, y};
        return true;
    }
    return false;
}


bool MtlRendererImpl::render(RenderCompositor* cmp, const RenderEffect* effect, TVG_UNUSED bool direct)
{
    auto sceneTask = mSceneTaskStack.last();
    sceneTask->effects.push(effect);
    return true;
}


void MtlRendererImpl::dispose(RenderEffect* effect)
{
    if (!effect->rd) return;
    mEffectPool.free((MtlRenderEffect*)effect->rd);
    effect->rd = nullptr;
};


bool MtlRendererImpl::preUpdate()
{
    if (mContext.invalid()) return false;

    return true;
}


bool MtlRendererImpl::postUpdate()
{
    if (mContext.invalid()) return false;
    @autoreleasepool {
        auto result = mTextures.flushPreprocess(mContext, mCompositor.pipelineStates());
        mContext.flushUploads();
        return result;
    }
}


void MtlRendererImpl::damage(TVG_UNUSED RenderData rd, TVG_UNUSED const RenderRegion& region)
{
    //TODO
}


bool MtlRendererImpl::partial(TVG_UNUSED bool disable)
{
    //TODO
    return false;
}

uint32_t MtlRendererImpl::dirtyRegions() const
{
    return 1;
}

Result MtlRendererImpl::dirtyRegions(Region* regions, TVG_UNUSED uint32_t cnt) const
{
    *regions = {0, 0, int32_t(mSurface.w), int32_t(mSurface.h)};
    return Result::Success;
}

bool MtlRendererImpl::intersectsShape(RenderData data, TVG_UNUSED const RenderRegion& region)
{
    if (!data) return false;
    auto shape = (MtlShape*)data;
    RenderRegion bbox = {
        (int32_t)shape->bbox.min.x, (int32_t)shape->bbox.min.y,
        (int32_t)shape->bbox.max.x, (int32_t)shape->bbox.max.y};
    if (region.intersected(bbox)) {
        if (region.contained(bbox)) return true;
        MtlIntersector intersector;
        return intersector.intersect(shape, RenderRegion::intersect(region, bbox));
    }
    return false;
}


bool MtlRendererImpl::intersectsImage(RenderData data, TVG_UNUSED const RenderRegion& region)
{
    if (!data) return false;
    auto picture = (MtlImage*)data;
    MtlIntersector intersector;
    if (intersector.intersect(picture, region)) return true;
    return false;
}


bool MtlRenderer::term()
{
    _rendererMtx.lock();
    if (_rendererCnt > 0) {
        _rendererMtx.unlock();
        return false;
    }
    _rendererCnt = -1;
    _rendererMtx.unlock();

    //TODO: clean up global resources

    return true;
}

MtlRenderer* MtlRenderer::gen(uint32_t threads, EngineOption op)
{
    return new MtlRendererImpl(threads, op);
}

MtlRendererImpl::MtlRendererImpl(TVG_UNUSED uint32_t threads, TVG_UNUSED EngineOption op)
{
    _rendererMtx.lock();
    if (_rendererCnt == -1) {
        //TODO: initialize the global engine
        _rendererCnt = 0;
    }
    ++_rendererCnt;
    _rendererMtx.unlock();
}
