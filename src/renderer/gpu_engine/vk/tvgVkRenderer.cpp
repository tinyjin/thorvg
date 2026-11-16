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
#include "tvgTaskScheduler.h"
#include "tvgVkRenderer.h"

/************************************************************************/
/* VkPaintPool                                                          */
/************************************************************************/

VkShape* VkPaintPool::shape()
{
    if (!shapes.empty()) return shapes.pick();

    auto paint = new VkShape();
    list.push(paint);
    return paint;
}

VkPicture* VkPaintPool::image()
{
    if (!images.empty()) return images.pick();

    auto paint = new VkPicture();
    list.push(paint);
    return paint;
}

void VkPaintPool::recycle(VkShape* shape)
{
    shapes.push(shape);
}

void VkPaintPool::recycle(VkPicture* image)
{
    images.push(image);
}

void VkPaintPool::release(VkContext& context)
{
    for (auto& p : list) p->release(context);
}

VkPaintPool::~VkPaintPool()
{
    for (auto& p : list) delete (p);
}

/************************************************************************/
/* VkIntersector Implementation                                         */
/************************************************************************/

struct VkIntersector
{
    bool intersect(const Array<VkPaint*>& clips, const Point& pt)
    {
        for (auto& c : clips) {
            auto clip = static_cast<const VkShape*>(c);
            const auto& mesh = clip->shape.mesh;
            if (!clip->shape.bbox.inside(pt) || !gpuPointInFillMesh(pt, mesh.vbuffer.data, mesh.ibuffer.data, mesh.ibuffer.count, clip->fillRule)) return false;
        }
        return true;
    }

    bool intersect(const VkShape* shape, const RenderRegion& region)
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

    bool intersect(const VkPicture* image, const RenderRegion& region)
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
/* VkRenderer Implementation                                            */
/************************************************************************/

static int32_t _rendererCnt = -1;
static StrictKey _rendererMtx;

void VkRenderer::release()
{
    if (mContext.invalid()) return;

    mContext.idle();
    disposeObjects();
    mTextures.clear(mContext);

    mPaintPool.release(mContext);

    // clear render  pool
    mRenderTargetPool.release(mContext);

    // clear rendering tree stacks
    mCompositorList.clear();
    mRenderTargetStack.clear();
    mRenderTargetRoot.release(mContext);

    releaseTarget();
    mCompositor.release(mContext);
    mContext.release();
    mFlushed = false;
    mSurface.stride = 0;
    mSurface.w = 0;
    mSurface.h = 0;
}

void VkRenderer::disposeObjects()
{
    ScopedLock lock(mDisposeKey);
    for (auto& p : mDisposedPaints) {
        auto paint = (VkPaint*)p;
        paint->clips.clear();
        if (paint->type() == Type::Shape) {
            static_cast<VkShape*>(paint)->reset();
            mPaintPool.recycle(static_cast<VkShape*>(paint));
        } else {
            static_cast<VkPicture*>(paint)->release(mTextures, mContext);
            mPaintPool.recycle(static_cast<VkPicture*>(paint));
        }
    }
    mDisposedPaints.clear();
}

void VkRenderer::releaseTarget(bool keepSurface)
{
    for (auto& p : mTarget.framebuffers) mContext.release(p);
    for (auto& p : mTarget.views) mContext.release(p);
    // the presentation engine may still wait on the semaphores, which a fence does not cover
    if (mTarget.swapchain) mContext.vkQueueWaitIdle(mContext.queue);
    for (auto& p : mTarget.presented) mContext.vkDestroySemaphore(mContext.device, p, nullptr);
    if (mTarget.swapchain) mContext.vkDestroySwapchainKHR(mContext.device, mTarget.swapchain, nullptr);
    auto surface = mTarget.surface;
    mTarget = VkPresentTarget();
    if (keepSurface) mTarget.surface = surface;
}

static uint32_t _turns(VkSurfaceTransformFlagBitsKHR transform)
{
    switch (transform) {
        case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR: return 1;
        case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR: return 2;
        case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR: return 3;
        default: return 0;
    }
}

// the canvas size in the orientation of the surface, the presentation engine scales it to the window
VkExtent2D VkRenderer::swapchainExtent(const VkSurfaceCapabilitiesKHR& caps, uint32_t turns)
{
    if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0) return {0, 0};
    auto w = (turns & 1) ? mSurface.h : mSurface.w;
    auto h = (turns & 1) ? mSurface.w : mSurface.h;
    return {tvg::clamp(w, caps.minImageExtent.width, caps.maxImageExtent.width), tvg::clamp(h, caps.minImageExtent.height, caps.maxImageExtent.height)};
}

Result VkRenderer::createSwapchain(VkSurfaceKHR surface)
{
    auto& ctx = mContext;
    if (!ctx.vkCreateSwapchainKHR || !ctx.vkGetPhysicalDeviceSurfaceCapabilitiesKHR || !ctx.vkGetPhysicalDeviceSurfaceFormatsKHR) {
        TVGERR("VK_ENGINE", "VK_KHR_surface and VK_KHR_swapchain must be enabled to render on a surface.");
        return Result::NonSupport;
    }
    VkBool32 supported = VK_FALSE;
    ctx.vkGetPhysicalDeviceSurfaceSupportKHR(ctx.physicalDevice, ctx.queueFamily, surface, &supported);
    if (!supported) {
        TVGERR("VK_ENGINE", "The queue can not present to the surface.");
        return Result::NonSupport;
    }

    VkSurfaceCapabilitiesKHR caps;
    if (ctx.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physicalDevice, surface, &caps) != VK_SUCCESS) return Result::Unknown;

    // any 8-bit UNORM format, the blit writes the color as it is
    uint32_t count = 0;
    ctx.vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physicalDevice, surface, &count, nullptr);
    Array<VkSurfaceFormatKHR> formats(count);
    ctx.vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physicalDevice, surface, &count, formats.data);
    formats.count = count;
    if (count == 0) return Result::NonSupport;
    auto format = formats[0];
    if (format.format == VK_FORMAT_UNDEFINED) format.format = VK_FORMAT_B8G8R8A8_UNORM;
    for (auto& p : formats) {
        if (p.format == VK_FORMAT_B8G8R8A8_UNORM || p.format == VK_FORMAT_R8G8B8A8_UNORM) {
            format = p;
            break;
        }
    }

    auto premultiplied = (mSurface.cs != ColorSpace::ABGR8888S && mSurface.cs != ColorSpace::ARGB8888S);
    auto alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!mSurface.opaque) {
        auto wanted = premultiplied ? VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR : VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
        if (caps.supportedCompositeAlpha & wanted) {
            alpha = wanted;
            mSurface.premultiplied = premultiplied;
        } else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) {
            alpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        } else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
            alpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        }
    }
    if (!(caps.supportedCompositeAlpha & alpha)) alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    // the surface keeps its swapchain retired while it has no area, such as a minimized window
    if (mTarget.surface != surface) releaseTarget();
    mTarget.surface = surface;
    auto turns = _turns(caps.currentTransform);
    auto extent = swapchainExtent(caps, turns);
    if (extent.width == 0 || extent.height == 0) {
        releaseTarget(true);
        mTarget.outdated = true;
        return Result::Success;
    }
    auto imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface;
    info.minImageCount = imageCount;
    info.imageFormat = format.format;
    info.imageColorSpace = format.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    // a rotation is applied by the blit, the presentation engine would otherwise rotate every frame
    info.preTransform = (turns || !(caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)) ? caps.currentTransform : VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    info.compositeAlpha = alpha;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    info.oldSwapchain = mTarget.swapchain;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    auto result = ctx.vkCreateSwapchainKHR(ctx.device, &info, nullptr, &swapchain);
    releaseTarget(true);
    if (result != VK_SUCCESS) {
        // retried by the next frame
        mTarget.outdated = true;
        TVGERR("VK_ENGINE", "Failed to create the swapchain (%d).", (int)result);
        return Result::Unknown;
    }

    mTarget.swapchain = swapchain;
    mTarget.format = format.format;
    mTarget.extent = extent;
    mTarget.turns = turns;

    ctx.vkGetSwapchainImagesKHR(ctx.device, swapchain, &count, nullptr);
    Array<VkImage> images(count);
    ctx.vkGetSwapchainImagesKHR(ctx.device, swapchain, &count, images.data);
    images.count = count;
    auto pass = mCompositor.pipelineStates().blitPass(ctx, format.format, true);
    for (auto& p : images) {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = p;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format.format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        ctx.vkCreateImageView(ctx.device, &viewInfo, nullptr, &view);
        mTarget.views.push(view);
        mTarget.framebuffers.push(ctx.createFramebuffer(pass, &view, 1, extent.width, extent.height));
        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore semaphore = VK_NULL_HANDLE;
        ctx.vkCreateSemaphore(ctx.device, &semaphoreInfo, nullptr, &semaphore);
        mTarget.presented.push(semaphore);
    }
    return Result::Success;
}

Result VkRenderer::createImageTarget(VkImage image)
{
    releaseTarget();

    auto format = (mSurface.cs == ColorSpace::ARGB8888 || mSurface.cs == ColorSpace::ARGB8888S) ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view = VK_NULL_HANDLE;
    if (mContext.vkCreateImageView(mContext.device, &viewInfo, nullptr, &view) != VK_SUCCESS) return Result::InvalidArguments;

    mTarget.image = image;
    mTarget.format = format;
    mTarget.extent = {mSurface.w, mSurface.h};
    mTarget.views.push(view);
    mTarget.framebuffers.push(mContext.createFramebuffer(mCompositor.pipelineStates().blitPass(mContext, format, false), &view, 1, mSurface.w, mSurface.h));
    return Result::Success;
}

bool VkRenderer::present()
{
    auto& ctx = mContext;

    if (!mTarget.surface) {
        if (mTarget.framebuffers.empty()) return ctx.submit();
        mCompositor.blit(ctx, ctx.commands(), &mRenderTargetRoot, mTarget.framebuffers[0], mTarget.format, mTarget.extent.width, mTarget.extent.height, 0, mSurface.premultiplied, false);
        return ctx.submit();
    }

    // a frame without a presentable image still submits its uploads and rendering
    if (mTarget.outdated) createSwapchain(mTarget.surface);
    if (!mTarget.swapchain) return ctx.submit();

    uint32_t index = 0;
    auto acquired = ctx.frame().acquired;
    auto result = ctx.vkAcquireNextImageKHR(ctx.device, mTarget.swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        if (createSwapchain(mTarget.surface) != Result::Success || !mTarget.swapchain) return ctx.submit();
        result = ctx.vkAcquireNextImageKHR(ctx.device, mTarget.swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &index);
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        TVGERR("VK_ENGINE", "Failed to acquire the swapchain image (%d).", (int)result);
        mTarget.outdated = true;
        return ctx.submit();
    }

    mCompositor.blit(ctx, ctx.split(), &mRenderTargetRoot, mTarget.framebuffers[index], mTarget.format, mTarget.extent.width, mTarget.extent.height, mTarget.turns, mSurface.premultiplied, true);
    auto presented = mTarget.presented[index];
    if (!ctx.submit(acquired, presented)) return false;

    VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &presented;
    info.swapchainCount = 1;
    info.pSwapchains = &mTarget.swapchain;
    info.pImageIndices = &index;
    result = ctx.vkQueuePresentKHR(ctx.queue, &info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) mTarget.outdated = true;
    else if (result == VK_SUBOPTIMAL_KHR) {
        // recreate only for a new size or rotation, a mismatch the swapchain can not resolve would recreate it every frame
        VkSurfaceCapabilitiesKHR caps;
        if (ctx.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physicalDevice, mTarget.surface, &caps) == VK_SUCCESS) {
            auto turns = _turns(caps.currentTransform);
            auto extent = swapchainExtent(caps, turns);
            mTarget.outdated = (turns != mTarget.turns || extent.width != mTarget.extent.width || extent.height != mTarget.extent.height);
        }
    } else if (result != VK_SUCCESS) return false;
    return true;
}

RenderData VkRenderer::prepare(const RenderShape& rshape, RenderData data, const Matrix& transform, const Array<RenderData>& clips, uint8_t opacity, RenderUpdateFlag flags, bool clipper)
{
    auto shape = data ? (VkShape*)data : mPaintPool.shape();
    if (!data) flags = RenderUpdateFlag::All;

    // update geometry
    if (flags & (RenderUpdateFlag::Transform | RenderUpdateFlag::Path | RenderUpdateFlag::Stroke)) shape->update(rshape, transform, flags);

    // update transform
    if (flags & RenderUpdateFlag::Transform) shape->transform = transform;

    // update paint settings
    shape->update(rshape, vport, shape->shape.setting.update(mSurface.cs, opacity), shape->stroke.setting.update(mSurface.cs, opacity), opacity);

    // shape
    if (shape->shape.setting.valid) {
        if (rshape.fill) {
            auto lost = shape->shape.setting.ramp == UINT32_MAX;
            if (lost || (flags & (RenderUpdateFlag::Gradient | RenderUpdateFlag::Transform))) {
                shape->shape.setting.update(mContext, rshape.fill, &transform, lost || (flags & RenderUpdateFlag::Gradient));
            }
        } else {
            shape->shape.solid.color = rshape.color;
            shape->shape.setting.fillType = VkRenderSettingsType::Solid;
        }
    }
    // stroke
    if (shape->stroke.setting.valid) {
        if (rshape.stroke->fill) {
            auto lost = shape->stroke.setting.ramp == UINT32_MAX;
            if (lost || (flags & (RenderUpdateFlag::GradientStroke | RenderUpdateFlag::Transform))) {
                shape->stroke.setting.update(mContext, rshape.stroke->fill, nullptr, lost || (flags & RenderUpdateFlag::GradientStroke));
            }
        } else {
            shape->stroke.solid.color = rshape.stroke->color;
            shape->stroke.setting.fillType = VkRenderSettingsType::Solid;
        }
    }

    if (flags & RenderUpdateFlag::Clip) shape->assign(clips);

    return shape;
}

RenderData VkRenderer::prepare(RenderSurface* surface, RenderData data, const Matrix& transform, const Array<RenderData>& clips, uint8_t opacity, FilterMethod filter, RenderUpdateFlag flags)
{
    auto image = data ? (VkPicture*)data : mPaintPool.image();
    if (!data) flags = RenderUpdateFlag::All;

    // update paint settings
    image->viewport = vport;
    image->transform = transform;
    image->setting.update(surface->cs, opacity);

    if (flags & (RenderUpdateFlag::Transform | RenderUpdateFlag::Image)) image->update(surface, transform);

    // reload texture
    auto cacheStale = !image->texture || (image->stamp != mTextures.stamp);
    auto refreshTex = (flags & RenderUpdateFlag::Image);
    auto update = cacheStale || refreshTex || (image->surface != surface) || (image->filter != filter);
    if (update && !mContext.invalid()) {
        image->release(mTextures, mContext);
        auto* entry = mTextures.retain(mContext, mCompositor.pipelineStates(), surface, filter, refreshTex);
        image->setup(entry->texture.image, entry->descriptor.set, surface, filter, mTextures.stamp);
    }

    if (flags & RenderUpdateFlag::Clip) image->assign(clips);

    return image;
}


bool VkRenderer::preRender()
{
    if (mContext.invalid()) return false;

    mSolidBatch = {};
    mStencilBatch = {};
    mCompositor.reset(mContext);

    assert(mRenderTargetStack.count == 0);
    mRenderTargetStack.push(&mRenderTargetRoot);

    // create root compose settings
    VkCompose* compose = new VkCompose();
    compose->aabb = {0, 0, (int32_t)mSurface.w, (int32_t)mSurface.h};
    compose->blend = BlendMethod::Normal;
    compose->method = MaskMethod::None;
    compose->opacity = 255;
    mCompositorList.push(compose);

    // create root scene
    VkSceneTask* sceneTask = new VkSceneTask(&mRenderTargetRoot, compose, nullptr);
    mRenderTaskList.push(sceneTask);
    mSceneTaskStack.push(sceneTask);
    return true;
}


bool VkRenderer::renderShape(RenderData data)
{
    auto rdata = (VkShape*)data;
    VkSceneTask* sceneTask = mSceneTaskStack.last();

    if (mSolidBatch.draw(sceneTask, rdata, mBlendMethod, mRenderTaskList)) return true;
    if (mStencilBatch.draw(sceneTask, rdata, mBlendMethod, mRenderTaskList)) return true;

    VkPaintTask* paintTask = new VkPaintTask(rdata, mBlendMethod);
    sceneTask->children.push(paintTask);
    mRenderTaskList.push(paintTask);
    return true;
}


bool VkRenderer::renderImage(RenderData data)
{
    auto rdata = (VkPicture*)data;
    VkSceneTask* sceneTask = mSceneTaskStack.last();
    if (mSolidBatch.draw(sceneTask, rdata, mBlendMethod, mRenderTaskList)) return true;

    VkPaintTask* paintTask = new VkPaintTask(rdata, mBlendMethod);
    sceneTask->children.push(paintTask);
    mRenderTaskList.push(paintTask);
    return true;
}


bool VkRenderer::postRender()
{
    VkSceneTask* sceneTaskRoot = mSceneTaskStack.last();
    sceneTaskRoot->stage(mCompositor);

    // flush stage data to gpu, then run rendering (all the fun is here)
    mFlushed = mCompositor.flush(mContext);
    if (mFlushed) {
        sceneTaskRoot->clearBuffer = mClearBuffer;
        sceneTaskRoot->run(mContext, mCompositor, mContext.commands());
        mCompositor.endRenderPass();
    }
    if (mRenderTargetPool.evict(mContext)) mCompositor.evict(mContext, mRenderTargetPool);

    // clear the render tasks tree
    mSceneTaskStack.pop();
    mRenderTargetStack.pop();
    for (auto& p : mRenderTaskList) { delete (p); };
    mRenderTaskList.clear();
    for (auto& p : mCompositorList) { delete (p); };
    mCompositorList.clear();
    return true;
}


void VkRenderer::dispose(RenderData data) {
    ScopedLock lock(mDisposeKey);
    mDisposedPaints.push(data);
}


bool VkRenderer::bounds(RenderData data, Point* pt4, const Matrix& m)
{
    if (data) {
        auto shape = static_cast<VkShape*>(data);
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

RenderRegion VkRenderer::region(RenderData data)
{
    return data ? static_cast<VkPaint*>(data)->region() : RenderRegion{};
}

bool VkRenderer::blend(BlendMethod method)
{
    mBlendMethod = (method == BlendMethod::Composition ? BlendMethod::Normal : method);
    return true;
}


ColorSpace VkRenderer::colorSpace()
{
    return mSurface.cs;
}


const RenderSurface* VkRenderer::mainSurface()
{
    return &mSurface;
}


bool VkRenderer::clear()
{
    if (mContext.invalid()) return false;

    mClearBuffer = true;
    return true;
}


bool VkRenderer::sync()
{
    if (mContext.invalid()) return false;

    disposeObjects();

    // the blit geometry lives in the frame buffer as well
    if (!mFlushed) {
        mCompositor.reset(mContext);
        mFlushed = mCompositor.flush(mContext);
    }
    auto ret = mFlushed ? present() : mContext.submit();

    mFlushed = false;
    mClearBuffer = false;

    return ret;
}

Result VkRenderer::target(const VkCanvas::Context& ctx, uint64_t target, uint32_t w, uint32_t h, ColorSpace cs, int type)
{
    // detach first: the application may destroy its Vulkan objects right after this
    if (!ctx.device || !target) {
        release();
        return Result::Success;
    }

    auto opaque = (cs == ColorSpace::XBGR8888);
    if (cs != ColorSpace::ABGR8888 && cs != ColorSpace::ABGR8888S && cs != ColorSpace::ARGB8888 && cs != ColorSpace::ARGB8888S && !opaque) return Result::NonSupport;
    if (opaque && type != 0) return Result::InvalidArguments;

    if (w == 0 || h == 0 || !ctx.instance || !ctx.physicalDevice || !ctx.queue || !ctx.getInstanceProcAddr) return Result::InvalidArguments;

    if (opaque) cs = ColorSpace::ABGR8888;  // Opaque surface composition uses the internal ABGR format.

    // context has been changed, need to recreate all instances
    if (mContext.device != (VkDevice)ctx.device || mContext.instance != (VkInstance)ctx.instance || mContext.physicalDevice != (VkPhysicalDevice)ctx.physicalDevice ||
        mContext.queue != (VkQueue)ctx.queue || mContext.queueFamily != ctx.queueFamilyIndex) {
        release();
        if (!mContext.initialize(ctx)) return Result::Unknown;
        mRenderTargetPool.initialize(w, h);
        mRenderTargetRoot.initialize(mContext, w, h);
        mCompositor.initialize(mContext, w, h);
    // update render targets dimensions
    } else if ((mSurface.w != w) || (mSurface.h != h)) {
        mRenderTargetPool.release(mContext);
        mRenderTargetRoot.release(mContext);
        mRenderTargetPool.initialize(w, h);
        mRenderTargetRoot.initialize(mContext, w, h);
        mCompositor.resize(mContext, w, h);
    }

    mSurface.stride = w;
    mSurface.w = w;
    mSurface.h = h;
    // Keep alpha for internal composition; opaque mode only affects surface presentation.
    mSurface.cs = cs;
    mSurface.premultiplied = (type != 0) ? (cs != ColorSpace::ABGR8888S && cs != ColorSpace::ARGB8888S) : true;
    mSurface.opaque = opaque;

    return (type == 0) ? createSwapchain((VkSurfaceKHR)target) : createImageTarget((VkImage)target);
}

VkRenderer::~VkRenderer()
{
    release();

    _rendererMtx.lock();
    --_rendererCnt;
    _rendererMtx.unlock();
}


RenderCompositor* VkRenderer::target(const RenderRegion& region, TVG_UNUSED ColorSpace cs, TVG_UNUSED CompositionFlag flags)
{
    // create and setup compose data
    VkCompose* compose = new VkCompose();
    compose->aabb = RenderRegion::intersect(region, {0, 0, int32_t(mSurface.w), int32_t(mSurface.h)});
    compose->flags = flags;
    mCompositorList.push(compose);
    return compose;
}


// please, keep beginComposite/endComposite aligned by logic
bool VkRenderer::beginComposite(RenderCompositor* cmp, MaskMethod method, uint8_t opacity)
{
    // current composition
    VkCompose* compose = (VkCompose*)cmp;
    // allocate new render target and push to the render tree stack
    VkRenderTarget* newRenderTarget = mRenderTargetPool.allocate(mContext, compose->aabb);
    mRenderTargetStack.push(newRenderTarget);
    // current and new scenes
    VkSceneTask* curSceneTask = mSceneTaskStack.last();
    VkSceneTask* newSceneTask = new VkSceneTask(newRenderTarget, compose, curSceneTask);
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
    curSceneTask->children.push(newSceneTask);
    mRenderTaskList.push(newSceneTask);
    mSceneTaskStack.push(newSceneTask);

    return true;
}


// please, keep beginComposite/endComposite aligned by logic
bool VkRenderer::endComposite(RenderCompositor* cmp)
{
    // pop targets and scenes from render tree
    mRenderTargetPool.pool.push(mRenderTargetStack.last());
    mSceneTaskStack.pop();
    mRenderTargetStack.pop();
    // in a case of masked target we must pop mask targets and scenes also
    VkCompose* compose = (VkCompose*)cmp;
    if (compose->masked) {
        mRenderTargetPool.pool.push(mRenderTargetStack.last());
        mSceneTaskStack.pop();
        mRenderTargetStack.pop();
    }
    return true;
}


void VkRenderer::prepare(RenderEffect* effect, const Matrix& transform)
{
    if (!effect->rd) effect->rd = mEffectPool.allocate();
    auto rdata = (VkRenderEffect*)effect->rd;

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
        TVGERR("VK_ENGINE", "Missing effect type? = %d", (int) effect->type);
        return;
    }
}


bool VkRenderer::region(RenderEffect* effect)
{
    auto rdata = (VkRenderEffect*)effect->rd;

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


bool VkRenderer::render(RenderCompositor* cmp, const RenderEffect* effect, TVG_UNUSED bool direct)
{
    auto sceneTask = mSceneTaskStack.last();
    sceneTask->effects.push(effect);
    return true;
}


void VkRenderer::dispose(RenderEffect* effect)
{
    if (!effect->rd) return;
    mEffectPool.free((VkRenderEffect*)effect->rd);
    effect->rd = nullptr;
};


bool VkRenderer::preUpdate()
{
    if (mContext.invalid()) return false;

    return true;
}


bool VkRenderer::postUpdate()
{
    if (mContext.invalid()) return false;
    return true;
}


void VkRenderer::damage(TVG_UNUSED RenderData rd, TVG_UNUSED const RenderRegion& region)
{
    //TODO
}


bool VkRenderer::partial(TVG_UNUSED bool disable)
{
    //TODO
    return false;
}

uint32_t VkRenderer::dirtyRegions() const
{
    return 1;
}

Result VkRenderer::dirtyRegions(Region* regions, TVG_UNUSED uint32_t cnt) const
{
    *regions = {0, 0, int32_t(mSurface.w), int32_t(mSurface.h)};
    return Result::Success;
}

bool VkRenderer::intersectsShape(RenderData data, TVG_UNUSED const RenderRegion& region)
{
    if (!data) return false;
    auto shape = (VkShape*)data;
    RenderRegion bbox = {
        (int32_t)shape->bbox.min.x, (int32_t)shape->bbox.min.y,
        (int32_t)shape->bbox.max.x, (int32_t)shape->bbox.max.y};
    if (region.intersected(bbox)) {
        if (region.contained(bbox)) return true;
        VkIntersector intersector;
        return intersector.intersect(shape, RenderRegion::intersect(region, bbox));
    }
    return false;
}


bool VkRenderer::intersectsImage(RenderData data, TVG_UNUSED const RenderRegion& region)
{
    if (!data) return false;
    auto picture = (VkPicture*)data;
    VkIntersector intersector;
    if (intersector.intersect(picture, region)) return true;
    return false;
}


bool VkRenderer::term()
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

VkRenderer::VkRenderer(TVG_UNUSED uint32_t threads, TVG_UNUSED EngineOption op)
{
    _rendererMtx.lock();
    if (_rendererCnt == -1) {
        //TODO: initialize the global engine
        _rendererCnt = 0;
    }
    ++_rendererCnt;
    _rendererMtx.unlock();
}
