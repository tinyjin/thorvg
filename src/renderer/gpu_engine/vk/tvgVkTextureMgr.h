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

#ifndef _TVG_VK_TEXTURE_MGR_H_
#define _TVG_VK_TEXTURE_MGR_H_

#include "tvgArray.h"
#include "tvgInlist.h"
#include "tvgVkPipelines.h"

struct VkTextureEntry
{
    INLIST_ITEM(VkTextureEntry);
    VkTexture texture;
    VkDescriptor descriptor;
    VkImageView storageView{};  // first level written by the premultiply pass
    uint32_t refCnt = 0;
    bool storage = false;  // writable by the premultiply pass
};

struct VkTextureMgr
{
    const VkTextureEntry* retain(VkContext& context, VkPipelines& pipelines, const RenderSurface* surface, FilterMethod filter, bool refreshTexture);
    void release(VkContext& context, const RenderSurface* surface, FilterMethod filter, VkImage texture);
    void clear(VkContext& context);

    struct SurfaceEntry
    {
        INLIST_ITEM(SurfaceEntry);
        const RenderSurface* surface = nullptr;
        tvg::Inlist<VkTextureEntry> bilinear;
        tvg::Inlist<VkTextureEntry> nearest;
    };

    SurfaceEntry* find(const RenderSurface* surface);
    static void upload(VkContext& context, VkPipelines& pipelines, VkTextureEntry& entry, const RenderSurface* surface, FilterMethod filter);
    static void releaseEntry(VkContext& context, VkTextureEntry& entry);
    static VkFormat textureFormat(const RenderSurface* surface);

    tvg::Inlist<SurfaceEntry> surfaces;
    uint16_t stamp = 1;
};

#endif /* _TVG_VK_TEXTURE_MGR_H_ */
