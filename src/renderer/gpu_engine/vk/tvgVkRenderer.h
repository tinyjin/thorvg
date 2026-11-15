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

#ifndef _TVG_VK_RENDERER_H_
#define _TVG_VK_RENDERER_H_

#include "tvgRender.h"
#include "tvgVkSolidBatch.h"
#include "tvgVkStencilBatch.h"
#include "tvgVkTextureMgr.h"

struct VkPaintPool
{
    Array<VkPaint*> list;      // full list of created paints
    Array<VkShape*> shapes;    // reuse list
    Array<VkPicture*> images;  // reuse list

    ~VkPaintPool();
    void recycle(VkShape* shape);
    void recycle(VkPicture* image);
    void release(VkContext& context);

    VkShape* shape();
    VkPicture* image();
};

// the presentable destination: a swapchain owned by the renderer or an image owned by the application
struct VkPresentTarget
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    uint32_t turns = 0;  // clockwise quarter turns the content is pre-rotated by
    Array<VkImageView> views;
    Array<VkFramebuffer> framebuffers;
    Array<VkSemaphore> presented;  // per swapchain image
    bool outdated = false;
};

struct VkRenderer : RenderMethod
{
    //main features
    bool preUpdate() override;
    RenderData prepare(const RenderShape& rshape, RenderData data, const Matrix& transform, const Array<RenderData>& clips, uint8_t opacity, RenderUpdateFlag flags, bool clipper) override;
    RenderData prepare(RenderSurface* surface, RenderData data, const Matrix& transform, const Array<RenderData>& clips, uint8_t opacity, FilterMethod filter, RenderUpdateFlag flags) override;
    bool postUpdate() override;
    bool preRender() override;
    bool renderShape(RenderData data) override;
    bool renderImage(RenderData data) override;
    bool postRender() override;
    void dispose(RenderData data) override;
    RenderRegion region(RenderData data) override;
    bool bounds(RenderData data, Point* pt4, const Matrix& m) override;
    bool blend(BlendMethod method) override;
    ColorSpace colorSpace() override;
    const RenderSurface* mainSurface() override;
    bool clear() override;
    bool sync() override;
    bool intersectsImage(RenderData data, const RenderRegion& region) override;
    bool intersectsShape(RenderData data, const RenderRegion& region) override;
    Result target(const VkCanvas::Context& ctx, uint64_t target, uint32_t w, uint32_t h, ColorSpace cs, int type = 0);

    //composition
    RenderCompositor* target(const RenderRegion& region, ColorSpace cs, CompositionFlag flags) override;
    bool beginComposite(RenderCompositor* cmp, MaskMethod method, uint8_t opacity) override;
    bool endComposite(RenderCompositor* cmp) override;

    //post effects
    void prepare(RenderEffect* effect, const Matrix& transform) override;
    bool region(RenderEffect* effect) override;
    bool render(RenderCompositor* cmp, const RenderEffect* effect, bool direct) override;
    void dispose(RenderEffect* effect) override;

    //partial rendering
    void damage(RenderData rd, const RenderRegion& region) override;
    bool partial(bool disable) override;
    uint32_t dirtyRegions() const override;
    Result dirtyRegions(Region* regions, uint32_t cnt) const override;

    VkRenderer(uint32_t threads, EngineOption op);
    static bool term();

private:
    ~VkRenderer();
    void release();
    void disposeObjects();

    void releaseTarget(bool keepSurface = false);
    VkExtent2D swapchainExtent(const VkSurfaceCapabilitiesKHR& caps, uint32_t turns);
    Result createSwapchain(VkSurfaceKHR surface);
    Result createImageTarget(VkImage image);
    bool present();

    RenderSurface mSurface;  // main surface
    VkRenderTarget mRenderTargetRoot;
    Array<VkCompose*> mCompositorList;
    Array<VkRenderTarget*> mRenderTargetStack;
    Array<VkSceneTask*> mSceneTaskStack;
    Array<VkRenderTask*> mRenderTaskList;
    VkSolidBatch mSolidBatch;
    VkStencilBatch mStencilBatch;
    VkRenderTargetPool mRenderTargetPool;
    VkPaintPool mPaintPool;
    VkRenderEffectPool mEffectPool;
    VkTextureMgr mTextures;
    VkContext mContext;
    VkCompositor mCompositor;
    VkPresentTarget mTarget;
    BlendMethod mBlendMethod = BlendMethod::Normal;

    // disposable data list
    Array<RenderData> mDisposedPaints;
    Key mDisposeKey{};

    bool mClearBuffer{};
    bool mFlushed{};  // the frame geometry has been staged
};

#endif /* _TVG_VK_RENDERER_H_ */
