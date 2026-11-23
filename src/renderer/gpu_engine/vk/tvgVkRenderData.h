/*
 * Copyright (c) 2023 - 2026 ThorVG project. All rights reserved.

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

#ifndef _TVG_VK_RENDER_DATA_H_
#define _TVG_VK_RENDER_DATA_H_

#include "tvgVkMesh.h"
#include "tvgVkShaderTypes.h"
#include "tvgVkPipelines.h"

struct VkTextureMgr;
struct VkStageBufferSolidColor;

static_assert(sizeof(RenderColor) == 4, "Solid color vertex data must remain tightly packed RGBA8");

struct VkSolidFill
{
    uint32_t colorIdx;
    RenderColor color;
    uint8_t opacity;

    RenderColor packedColor() const
    {
        return {color.r, color.g, color.b, MULTIPLY(color.a, opacity)};
    }
};

struct VkRenderSettings
{
    uint32_t paintIdx;  // index of the staged paint uniforms
    VkShaderTypePaintSettings settings;
    uint32_t ramp = UINT32_MAX;  // row in the gradient ramp atlas
    Array<Fill::ColorStop> stops;  // contents of the ramp row, a gradient set again with the same stops keeps it
    VkRenderSettingsType fillType{};
    float opacityMultiplier = 1.0f;
    bool valid = false;
    bool opaque;
    FillSpread spread{};  // cached here to use existing tail padding

    uint8_t update(tvg::ColorSpace cs, uint8_t opacity);
    void update(VkContext& context, const Fill* fill, const Matrix* transform, bool updateColorRamp);
    void release(VkContext& context);
    VkDescriptorSet ramps(VkContext& context) const { return context.ramps.descriptor(spread); }
};

struct VkPaint
{
    RenderRegion viewport;
    Array<VkPaint*> clips;
    Matrix transform;

    virtual ~VkPaint(){};
    virtual void release(VkContext& context) = 0;
    virtual RenderRegion region() = 0;
    virtual Type type() { return Type::Undefined; };
    void assign(const Array<RenderData>& clips);
    RenderRegion drawRegion();
};

struct VkShape : VkPaint
{
    struct
    {
        VkRenderSettings setting;
        VkSolidFill solid;
        VkMesh mesh;
        BBox bbox;
    } shape;

    struct
    {
        VkRenderSettings setting;
        VkSolidFill solid;
        VkMesh mesh;
        VkMesh bboxMesh;
        BBox bbox;
    } stroke;

    VkMesh bboxMesh;
    BBox bbox;
    uint32_t strokeViewMatIdx;
    FillRule fillRule;
    bool convex;
    bool strokeFirst;
    bool strokeOverlapFree;

    void expand(const BBox& bb);
    void update(const RenderShape& rshape, const RenderRegion& vport, uint8_t shapeOpacity, uint8_t strokeOpacity, uint8_t opacity);
    void update(const RenderShape& rshape, const Matrix& transform, RenderUpdateFlag flag);
    void reset();
    void release(VkContext& context) override;

    RenderRegion region() override
    {
        auto& min = bbox.min;
        auto& max = bbox.max;
        return {int32_t(nearbyint(min.x)), int32_t(nearbyint(min.y)), int32_t(nearbyint(max.x)), int32_t(nearbyint(max.y))};
    }

    Type type() override { return Type::Shape; };
};

struct VkPicture : VkPaint
{
    const RenderSurface* surface{};
    VkRenderSettings setting;
    VkImage texture{};
    VkDescriptorSet descriptor{};
    VkMesh mesh;
    uint16_t stamp = 0;
    FilterMethod filter = FilterMethod::Bilinear;

    void update(const RenderSurface* surface, const Matrix& transform);
    void setup(VkImage texture, VkDescriptorSet descriptor, const RenderSurface* surface, FilterMethod filter, uint16_t stamp);
    void release(VkTextureMgr& textures, VkContext& context);
    void reset();
    void release(VkContext& context) override;

    // TODO: return an elaborate bbox
    RenderRegion region() override { return viewport; }
    Type type() override { return Type::Picture; };
};

struct VkGeometryRange
{
    size_t vertexOffset;
    size_t indexOffset;
    uint32_t vertexCount;
    uint32_t indexCount;
};

struct VkSolidBatchRange : VkGeometryRange
{
    size_t colorOffset{};
    RenderRegion viewport;
};

struct VkImageBatchRange : VkGeometryRange
{
    size_t texCoordOffset{};
    RenderRegion viewport;
};

struct VkStencilBatchRange
{
    VkGeometryRange stencil;
    VkGeometryRange cover;
    size_t colorOffset{};
    RenderRegion viewport;
    FillRule fillRule;
    bool solidOnly;
};

// effect parameters are pushed as constants when drawing
struct VkRenderEffect
{
    float params[12]{};
    uint32_t extend;
    Point offset;

    void update(VkShaderTypeEffectParams& params);
    void update(RenderEffectMotionBlur* motion, const Matrix& transform);
    void update(RenderEffectGaussianBlur* gaussian, const Matrix& transform);
    void update(RenderEffectDropShadow* dropShadow, const Matrix& transform);
    void update(RenderEffectFill* fill);
    void update(RenderEffectTint* tint);
    void update(RenderEffectTritone* tritone);
};

struct VkRenderEffectPool
{
    // pool contains all created but unused render effects
    Array<VkRenderEffect*> mPool;
    // list contains all created render effects to ensure that all created instances will be released
    Array<VkRenderEffect*> mList;

    ~VkRenderEffectPool();
    VkRenderEffect* allocate();
    void free(VkRenderEffect* rdata);
};

// The staged data of a frame is written into one host visible buffer at flush time.
struct VkStageBufferGeometry
{
    Array<uint8_t> vbuffer;
    Array<uint8_t> ibuffer;
    void appendBatch(const Array<VkShape*>& renderShapes, VkGeometryRange& range, bool cover);

    void append(VkMesh* meshData);
    void append(VkShape* renderShape);
    void append(VkPicture* renderPicture);
    void appendSolidBatch(const Array<VkShape*>& renderShapes, VkStageBufferSolidColor& colors, VkSolidBatchRange& range);
    void appendImageBatch(const Array<VkPicture*>& renderImages, VkImageBatchRange& range);
    void appendStencilBatch(const Array<VkShape*>& renderShapes, VkStencilBatchRange& range);
    void clear();
};

struct VkStageBufferSolidColor
{
    Array<RenderColor> vbuffer;

    uint32_t append(const RenderColor& value)
    {
        vbuffer.push(value);
        return vbuffer.count - 1;
    }

    void appendRepeated(const RenderColor& value, uint32_t count);
    void clear();
};

#endif // _TVG_VK_RENDER_DATA_H_
